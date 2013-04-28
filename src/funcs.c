/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "funcs.h"

#include "discofs.h"
#include "log.h"

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <mntent.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#if HAVE_SETXATTR
#include <attr/xattr.h>
#endif

#if !(HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME)
#include <utime.h>
#endif


/* djb2, a simple string hashing function */
unsigned long djb2(const char *str, size_t n)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++) && n--)
        hash = ((hash << 5) + hash) + c;

    return hash;
}

/* join two path elements */
char *join_path2(const char *p1, size_t n1, const char *p2, size_t n2)
{
    char *ret, *p;

    if (!p1 || !p2)
        return NULL;

    if (!n1)
        n1 = strlen(p1);
    if (!n2)
        n2 = strlen(p2);

    ret = malloc(n1 + n2 + 2);
    if (!ret)
        return NULL;

    memcpy(ret, p1, n1+1);

    p = ret + n1 - 1;

    /* append trailing '/' to p1 if not already present */
    if (*p != '/')
        *(++p) = '/';

    /* ignore leading '/' of p2 */
    if (*p2 == '/')
    {
        p2++;
        n2--;
    }

    memcpy(++p, p2, n2+1);
    return ret;
}

/* compare time */

int is_running(const char *pidfile)
{
    FILE *f;
    char line[16];
    char *endptr;
    pid_t pid;

    if (!pidfile)
        return 1;

    f = fopen(pidfile, "r");
    if (!f)
    {
        ERROR("cannot open pid file %s", pidfile);
        PERROR("");
        return 0;
    }
    fgets(line, sizeof line, f);
    fclose(f);

    pid = strtol(line, &endptr, 10);
    if (*line == '\0' || (*endptr != '\0' && *endptr != '\n'))
    {
        ERROR("failed getting pid from file %s", pidfile);
        return 0;
    }

    return (kill(pid, 0) == 0);
}

/* check if fs is mounted by comparing st_dev with st_dev of parent dir */
int is_mounted(const char *mpoint)
{
    int res;
    struct stat st;
    char *parent;
    dev_t dev;

    res = lstat(mpoint, &st);
    if (res)
        return 0;
    dev = st.st_dev;

    parent = dirname_r(mpoint);
    if (!parent)
        return 0;

    res = lstat(parent, &st);
    free(parent);
    if (res)
        return 0;

    return (dev != st.st_dev);
}

/* check if host is reachable */
int is_reachable(const char *host)
{
    int res=0;
    int status;
    pid_t pid;

    if (!host || strcmp(host, "") == 0)
        return 1;

    pid = fork();

    if (pid == 0)
    {
        close(1);
        close(2);
        execlp("ping", "ping", "-c", PING_COUNT, "-i", PING_INTERVAL, "-w", PING_DEADLINE, host, NULL);
    }

    else
    {
        waitpid(pid, &status, 0);
        res = WEXITSTATUS(status);
    }

    return (res == 0);
}

int copy_rec(const char *from, const char *to)
{
    int res = 0;
    char *subfrom;
    char *subto;

    size_t d_len, from_len, to_len;

    size_t bufsize;
    DIR *dirp;
    struct dirent *buf;
    struct dirent *ent;

    struct stat st;

    if (lstat(from, &st) == -1)
        return -1;
    if (mkdir(to, st.st_mode))
        return -1;

    dirp = opendir(from);
    if (!dirp)
    {
        return -1;
    }
    bufsize = dirent_buf_size(dirp);
    buf = malloc(bufsize);

    from_len = strlen(from);
    to_len = strlen(to);

    DEBUG("calling readdir_r()");
    while((res = readdir_r(dirp, buf, &ent)) == 0 && ent)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        if ((res = lstat(ent->d_name, &st)))
            break;

        d_len = strlen(ent->d_name);

        subfrom = join_path2(from, from_len, ent->d_name, d_len);
        subto = join_path2(to, to_len, ent->d_name, d_len);

        res = (S_ISDIR(st.st_mode)) ?  copy_rec(subfrom, subto) : copy_file(subfrom, subto);

        free(subfrom);
        free(subto);

        if (res)
            break;
    }

    free(buf);
    closedir(dirp);
    if (res)
        return -1;
    return 0;
}

int copy_symlink(const char *from, const char *to)
{
    int res;
    struct stat st;
    ssize_t bufsz;
    char *buf;

    if (lstat(from, &st) == -1)
        return -1;

    bufsz = (size_t)st.st_size;

    buf = malloc(bufsz + 1);
    if (!buf)
        return -1;

    buf[bufsz] = '\0';

    if (readlink(from, buf, bufsz) != bufsz)
    {
        free(buf);
        return -1;
    }
    res = symlink(buf, to);
    free(buf);
    return res;
}

int copy_file(const char *from, const char *to)
{
    int res;
    struct stat st;

    if (lstat(from, &st) == -1)
        return -1;

    /* SYMLINK */
    if (S_ISLNK(st.st_mode))
    {
        if (copy_symlink(from, to) == -1)
            return -1;
    }
    /* REGULAR FILE */
    else if (S_ISREG(st.st_mode))
    {
        char buf[TRANSFER_SIZE];
        int fdread, fdwrite;
        /* open source file for reading */
        fdread = open(from, O_RDONLY);

        if (fdread == -1)
        {
            return -1;
        }

        /* open target file for writing */
        fdwrite = open(to, O_WRONLY|O_CREAT|O_TRUNC, 0666);

        if (fdwrite == -1)
        {
            close(fdread);
            return -1;
        }

        while ((res = read(fdread, buf, sizeof buf)) > 0)
        {
            if (write(fdwrite, buf, res) < 0)
            {
                close(fdread);
                close(fdwrite);
                return -1;
            }
        }
        close(fdread);
        close(fdwrite);

        if (res < 0)
            return -1;
    }
    /* everythin else is not supported */
    else
    {
        errno = ENOTSUP;
        return -1;
    }

    /* data is finished, copy attributes */
    copy_attrs(from, to);

    return 0;
}

int copy_attrs(const char *from, const char *to)
{
    struct stat st;

    if (lstat(from, &st) == -1)
        return -1;

    if (!(discofs_options.copyattr & COPYATTR_NO_MODE))
    {
        if (chmod(to, st.st_mode) == -1)
            PERROR("copy_attrs: setting mode failed");
    }

    if (!(discofs_options.copyattr & COPYATTR_NO_OWNER))
    {
        if (lchown(to, st.st_uid, (gid_t)-1) == -1)
            PERROR("copy_attrs: setting owner failed");
    }

    if (!(discofs_options.copyattr & COPYATTR_NO_GROUP))
    {
        if (lchown(to, (uid_t)-1, st.st_gid) == -1)
            PERROR("copy_attrs: setting group failed");
    }

#if HAVE_SETXATTR
    if ((discofs_options.fs_features & FEAT_XATTR) && !(discofs_options.copyattr & COPYATTR_NO_XATTR))
    {
        if (copy_xattrs(from, to) == -1)
            PERROR("copy_attrs: copy_xattrs failed");
    }
#endif

    return 0;
}

int clone_dir(const char *from, const char *to)
{
    int res;
    struct stat st;
    if (lstat(from, &st) == -1)
        return -1;

    res = mkdir(to, st.st_mode);
    copy_attrs(from, to);

    return res;
}

#if HAVE_SETXATTR
int copy_xattrs(const char *from, const char *to)
{
    ssize_t bufsz;
    ssize_t valsz;
    char *attrlist;
    int nattr;
    void *val;

    char *p;

    bufsz = llistxattr(from, NULL, 0);
    if (bufsz == -1)
        return -1;

    attrlist = malloc(bufsz);

    if (attrlist == NULL)
        return -1;


    if (llistxattr(from, attrlist, bufsz) == -1)
    {
        free(attrlist);
        return -1;
    }

    /* determine number of attrs */
    nattr = 0;
    p = attrlist;
    while (bufsz--)
    {
        if (*(p++) == '\0')
            ++nattr;
    }

    p = attrlist;
    while (nattr--)
    {
        valsz = lgetxattr(from, p, NULL, 0);
        if (valsz == -1 || !(val = malloc(valsz)))
        {
            free(attrlist);
            return -1;
        }

        if (lgetxattr(from, p, val, valsz) == -1
                || lsetxattr(to, p, val, valsz, 0) == -1) {
            free(val);
            free(attrlist);
            return -1;
        }

        free(val);

        p += strlen(p) + 1;
    }
    free(attrlist);

    return 0;
}
#endif

int is_nonexist(const char *path)
{
    struct stat st;

    if (lstat(path, &st) == -1 && errno == ENOENT)
    {
        return 1;
    }
    return 0;
}

int is_reg(const char *path)
{
    struct stat st;

    if (lstat(path, &st) == -1)
        return 0;

    return S_ISREG(st.st_mode);
}

int is_lnk(const char *path)
{
    struct stat st;

    if (lstat(path, &st) == -1)
        return 0;

    return S_ISLNK(st.st_mode);
}

int is_dir(const char *path)
{
    struct stat st;

    if (lstat(path, &st) == -1)
        return 0;

    return S_ISDIR(st.st_mode);
}

char *affix_filename(const char *path, const char *prefix, const char *suffix)
{
    char *p;
    char *tmp;

    size_t pref_len, suff_len;

    pref_len = (prefix) ? strlen(prefix) : 0;
    suff_len = (suffix) ? strlen(suffix) : 0;

    p = malloc(strlen(path) + pref_len + suff_len + 1);
    if (!p)
        return NULL;

    /* concatenate: dirname + prefix + basename + suffix */

    /* copy dirname */
    tmp = dirname_r(path);
    if (!tmp)
    {
        free(p);
        return NULL;
    }
    strcpy(p, tmp);
    free(tmp);

    /* append "/" and prefix */
    if (p[1] != '\0')
        strcat(p, "/");
    if (prefix)
        strcat(p, prefix);

    /* append filename */
    tmp = basename_r(path);
    if (!tmp)
    {
        free(p);
        return NULL;
    }
    strcat(p, tmp);
    free(tmp);

    /* append suffix */
    if (suffix)
        strcat(p, suffix);

    return p;
}

/* threadsafe version of dirname() */
char *dirname_r(const char *path)
{
    char *out;
    char *p;

    out = strdup(path);
    if (!out)
        return NULL;

    p = strrchr(out, '/');
    if (*(p+1) == '\0')
    {
        do
            p--;
        while (p > out && *p != '/');
    }
    if (p)
    {
        if (p == out)
            p++;
        *p = '\0';
        /* out = realloc(out, p-out); */
    }
    return out;
}

/* threadsafe version of basename() */
char *basename_r(const char *path)
{
    char *copy;
    char *out;
    char *p;

    copy = strdup(path);
    if (!copy)
        return NULL;

    p = strrchr(copy, '/');
    if (p)
    {
        out = strdup(++p);
        free(copy);
    }
    else
        out = copy;

    return out;
}

/* create a directory and all its parents */
int mkdir_rec(const char *path)
{
    int n = 0;
    int i;
    char *p, *ptmp;
    struct stat st;

    p = strdup(path);

    if (!p)
    {
        errno = ENOMEM;
        return -1;
    }

    while (p && stat(p, &st) == -1)
    {
        ptmp = dirname_r(p);
        free(p);
        p = ptmp;
        n++;
    }
    free(p);

    if (!n)
        return 0;

    while (n--)
    {
        p = strdup(path);
        if (!p)
        {
            errno = ENOMEM;
            return -1;
        }

        i = n;
        while (i--)
        {
            ptmp = dirname_r(p);
            free(p);
            p = ptmp;
        }
        if (!p || mkdir(p, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
        {
            free(p);
            return -1;
        }
        free(p);
    }

    return 0;
}

int rmdir_rec(const char *path)
{
    int res;
    char *subpath;

    size_t dbufsize;
    DIR *dirp;
    struct dirent *dbuf;
    struct dirent *ent;

    struct stat st;

    dirp = opendir(path);
    if (!dirp)
    {
        return -1;
    }
    dbufsize = dirent_buf_size(dirp);
    dbuf = malloc(dbufsize);

    while((res = readdir_r(dirp, dbuf, &ent)) == 0 && ent)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        subpath = join_path(path, ent->d_name);
        if (!subpath || lstat(subpath, &st) == -1)
        {
            closedir(dirp);
            free(subpath);
            free(dbuf);
            return -1;
        }

        if (S_ISDIR(st.st_mode))
        {
            res = rmdir_rec(subpath);
        }
        else
        {
            res = unlink(subpath);
        }
        free(subpath);
        if (res == -1)
        {
            closedir(dirp);
            free(dbuf);
            return -1;
        }
    }
    closedir(dirp);
    free(dbuf);
    if (res)
    {
        errno = res;
        return -1;
    }
    return rmdir(path);
}

/* from http://womble.decadent.org.uk/readdir_r-advisory.html */
size_t dirent_buf_size(DIR * dirp)
{
    long name_max;
    size_t name_end;
#   if HAVE_FPATHCONF && HAVE_DIRFD && defined(_PC_NAME_MAX)
    name_max = fpathconf(dirfd(dirp), _PC_NAME_MAX);
    if (name_max == -1)
#           if defined(NAME_MAX)
        name_max = (NAME_MAX > 255) ? NAME_MAX : 255;
#           else
    return (size_t)(-1);
#           endif
#   else
#       if defined(NAME_MAX)
    name_max = (NAME_MAX > 255) ? NAME_MAX : 255;
#       else
#           error "buffer size for readdir_r cannot be determined"
#       endif
#   endif
    name_end = (size_t)offsetof(struct dirent, d_name) + name_max + 1;
    return (name_end > sizeof (struct dirent)
            ? name_end : sizeof (struct dirent));
}
