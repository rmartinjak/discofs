/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "db.h"

#include "log.h"
#include "funcs.h"
#include "queue.h"
#include "job.h"
#include "sync.h"

#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <sys/types.h>

#define ERRMSG(msg) ERROR(msg ": %s\n", sqlite3_errmsg(db))

#define PREPARE(sql, stmt)                                                  \
do {                                                                        \
    if (sqlite3_prepare_v2(db, sql, -1, stmt, NULL) != SQLITE_OK)           \
        {                                                                   \
            ERRMSG("preparing statement");                                  \
            return DB_ERROR;                                                \
        }                                                                   \
} while (0)

static sqlite3 *db;
static pthread_mutex_t m_db = PTHREAD_MUTEX_INITIALIZER;

static char *column_text(sqlite3_stmt *stmt, int n)
{
    const unsigned char *p = sqlite3_column_text(stmt, n);

    if (!p)
        return NULL;

    return strdup((const char*)p);
}

/********************/
/* GENERAL DB STUFF */
/********************/

void db_open(void)
{
    pthread_mutex_lock(&m_db);
}

void db_close(void)
{
    pthread_mutex_unlock(&m_db);
}

int db_init(const char *path, int clear)
{
    VERBOSE("initializing db in %s\n", path);

    if (sqlite3_open(path, &db) != SQLITE_OK)
    {
        ERROR("error initializing db: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    db_open();

#define NEW_TABLE(t, sql)                                                   \
    {                                                                       \
        if (sqlite3_exec(db, "CREATE TABLE " t " ( " sql " );",             \
                    NULL, NULL, NULL))                                      \
        {                                                                   \
            db_close();                                                     \
            ERROR("couldn't create table " t "\n");                         \
            return -1;                                                      \
        }                                                                   \
    }

#define CREATE_TABLE(t, sql)                                                \
    {                                                                       \
        if (clear)                                                          \
            sqlite3_exec(db, "DROP TABLE " t ";", NULL, NULL, NULL);        \
                                                                            \
        if (sqlite3_exec(db, "SELECT * FROM " t " LIMIT 1;",                \
                    NULL, NULL, NULL))                                      \
        {                                                                   \
            NEW_TABLE(t, sql);                                              \
        }                                                                   \
    }

    /* create tables */
    CREATE_TABLE(TABLE_CFG, SCHEMA_CFG);
    CREATE_TABLE(TABLE_JOB, SCHEMA_JOB);
    CREATE_TABLE(TABLE_SYNC, SCHEMA_SYNC);
    CREATE_TABLE(TABLE_HARDLINK, SCHEMA_HARDLINK);

#undef NEW_TABLE
#undef CREATE_TABLE

    DEBUG("db initialization finished\n");
    db_close();
    return 0;
}

int db_destroy(void)
{
    VERBOSE("closing database connection\n");
    sqlite3_close(db);
    return 0;
}


/**********/
/* CONFIG */
/**********/

int db_cfg_delete(const char *option)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("DELETE FROM " TABLE_CFG " WHERE option=?;", &stmt);

    sqlite3_bind_text(stmt, 1, option, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("deleting config option");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_cfg_set_int(const char *option, int val)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("INSERT OR REPLACE INTO " TABLE_CFG " (option, nval) VALUES (?, ?);", &stmt);
    sqlite3_bind_text(stmt, 1, option, -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, val);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_cfg_set_int");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_cfg_set_str(char *option, const char *val)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("INSERT INTO " TABLE_CFG " (option, tval) VALUES (?, ?);", &stmt);
    sqlite3_bind_text(stmt, 1, option, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, val, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_cfg_set_str");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_cfg_get_int(const char *option, int *buf)
{
    int res = DB_OK, sql_res;
    sqlite3_stmt *stmt;

    if (!buf)
        return DB_ERROR;

    db_open();

    PREPARE("SELECT nval FROM " TABLE_CFG " WHERE option=?;", &stmt);
    sqlite3_bind_text(stmt, 1, option, -1, SQLITE_STATIC);

    sql_res = sqlite3_step(stmt);

    if (sql_res == SQLITE_ROW)
        *buf = sqlite3_column_int(stmt, 0);
    else if (sql_res != SQLITE_DONE)
    {
        ERRMSG("db_cfg_get_int");
        res = DB_ERROR;
    }
    else
        res = DB_NOTFOUND;

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_cfg_get_str(const char *option, char **buf)
{
    int res = DB_OK, sql_res;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("SELECT tval FROM " TABLE_CFG " WHERE option=?;", &stmt);
    sqlite3_bind_text(stmt, 1, option, -1, SQLITE_STATIC);

    sql_res = sqlite3_step(stmt);

    if (sql_res == SQLITE_ROW)
        *buf = column_text(stmt, 0);

    else if (sql_res != SQLITE_DONE)
    {
        ERRMSG("db_cfg_get_str");
        res = DB_ERROR;
    }
    else
        res = DB_NOTFOUND;

    sqlite3_finalize(stmt);
    db_close();
    return res;
}


/*******/
/* JOB */
/*******/

int db_job_store(const struct job *j)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    if (!j->path)
    {
        errno = EINVAL;
        return DB_ERROR;
    }

    DEBUG("storing job for %s\n", j->path);

    db_open();

#define COLS "rowid, prio, op, time, attempts, path, n1, n2, s1, s2"
#define VALS "?, ?, ?, ?, ?, ?, ?, ?, ?, ?"
    PREPARE("INSERT OR REPLACE INTO " TABLE_JOB " (" COLS ") VALUES (" VALS ");", &stmt);
#undef COLS
#undef VALS

    if (j->id <= 0)
        sqlite3_bind_null (stmt, 1);
    else
        sqlite3_bind_int64(stmt, 1, j->id);

    sqlite3_bind_int  (stmt,  2, OP_PRIO(j->op));
    sqlite3_bind_int  (stmt,  3, j->op);

    sqlite3_bind_int64(stmt,  4, j->time);
    sqlite3_bind_int  (stmt,  5, j->attempts);

    sqlite3_bind_text (stmt,  6, j->path,   -1, SQLITE_STATIC);

    sqlite3_bind_int64(stmt,  7, j->n1);
    sqlite3_bind_int64(stmt,  8, j->n2);

    sqlite3_bind_text (stmt,  9, j->s1,     -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 10, j->s2,     -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_store_job:");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

#define SELECT_JOB "SELECT rowid, op, time, attempts, path, n1, n2, s1, s2 FROM " TABLE_JOB " "
int db_job_get(struct job **j)
{
    int res = DB_OK, sql_res;
    sqlite3_stmt *stmt;
    time_t now = time(NULL);
    struct job *p;

    *j = NULL;

    db_open();

    PREPARE(SELECT_JOB "WHERE time <? ORDER BY prio DESC, time ASC LIMIT 1;", &stmt);
    sqlite3_bind_int64(stmt, 1, now);

    sql_res = sqlite3_step(stmt);

    if (sql_res == SQLITE_ROW)
    {
        p = job_alloc();

        if (!p)
        {
            res = DB_ERROR;
        }
        else
        {
            p->id       = sqlite3_column_int64(stmt, 0);
            p->op       = sqlite3_column_int(stmt, 1);

            p->time     = sqlite3_column_int64(stmt, 2);
            p->attempts = sqlite3_column_int(stmt, 3);

            p->path     = column_text(stmt, 4);

            p->n1       = sqlite3_column_int64(stmt, 5);
            p->n2       = sqlite3_column_int64(stmt, 6);

            p->s1       = column_text(stmt, 7);
            p->s2       = column_text(stmt, 8);

            *j = p;
        }
    }
    /* if no rows returned, sql_res would be SQLITE_DONE */
    else if (sql_res != SQLITE_DONE)
    {
        ERRMSG("db_job_get");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_job_exists(const char *path, int opmask)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();
    if (opmask == JOB_ANY)
    {
        PREPARE("SELECT rowid FROM " TABLE_JOB " WHERE path=?;", &stmt);
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    }
    else
    {
        PREPARE("SELECT rowid FROM " TABLE_JOB " WHERE path=? AND (op & ?) != 0;", &stmt);
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 2, opmask);
    }

    res = (sqlite3_step(stmt) == SQLITE_ROW);

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_job_delete(const char *path, int opmask)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();
    if (opmask == JOB_ANY)
    {
        PREPARE("DELETE FROM " TABLE_JOB " WHERE path=?;", &stmt);
        sqlite3_bind_text (stmt, 1, path, -1, SQLITE_STATIC);
    }
    else
    {
        PREPARE("DELETE FROM " TABLE_JOB " WHERE path=? AND (op & ?) != 0;", &stmt);
        sqlite3_bind_text (stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_int  (stmt, 2, opmask);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_job_delete");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_job_delete_id(job_id id)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("DELETE FROM " TABLE_JOB " WHERE rowid=?;", &stmt);
    sqlite3_bind_int64(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_job_delete_id");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_job_delete_rename_to(const char *path)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("DELETE FROM " TABLE_JOB " WHERE op=? AND s1=?;", &stmt);
    sqlite3_bind_int  (stmt, 1, JOB_RENAME);
    sqlite3_bind_text (stmt, 2, path, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_job_delete_rename_to");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}


/********/
/* SYNC */
/********/

int db_load_sync(sync_load_cb_t callback)
{
    int res = DB_OK, sql_res;
    sqlite3_stmt *stmt;
    char *path;
    sync_xtime_t mtime, ctime;

    db_open();

#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    PREPARE("SELECT path, mtime_s, mtime_ns, ctime_s, ctime_ns FROM " TABLE_SYNC ";", &stmt);
#else
    PREPARE("SELECT path, mtime_s, ctime_s FROM " TABLE_SYNC ";", &stmt);
#endif

    while ((sql_res = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        path = column_text(stmt, 0);

#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
        mtime.tv_sec  = sqlite3_column_int64(stmt, 1);
        mtime.tv_nsec = sqlite3_column_int64(stmt, 2);

        ctime.tv_sec  = sqlite3_column_int64(stmt, 3);
        ctime.tv_nsec = sqlite3_column_int64(stmt, 4);
#else
        mtime sqlite3_column_int64(stmt, 1);
        ctime sqlite3_column_int64(stmt, 2);
#endif

        if (callback(path, mtime, ctime) == NULL)
        {
            PERROR("db_load_syncs in callback()");
            free(path);
            res = DB_ERROR;
            break;
        }
        free(path);
    }

    if (sql_res != SQLITE_DONE)
    {
        ERRMSG("db_load_syncs in sqlite3_step()");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_store_sync(const struct sync *s)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    PREPARE("INSERT OR REPLACE INTO " TABLE_SYNC " (path, mtime_s, mtime_ns, ctime_s, ctime_ns) VALUES (?, ?, ?, ?, ?)", &stmt);
    sqlite3_bind_text (stmt, 1, s->path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, s->mtime.tv_sec);
    sqlite3_bind_int64(stmt, 3, s->mtime.tv_nsec);
    sqlite3_bind_int64(stmt, 4, s->ctime.tv_sec);
    sqlite3_bind_int64(stmt, 5, s->ctime.tv_nsec);
#else
    PREPARE("INSERT OR REPLACE INTO " TABLE_SYNC " (path, mtime_s, ctime_s) VALUES (?, ?, ?)", &stmt);
    sqlite3_bind_text (stmt, 1, s->path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, s->mtime);
    sqlite3_bind_int64(stmt, 3, s->ctime);
#endif

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("error setting sync");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}


/************/
/* HARDLINK */
/************/

int db_hardlink_get(ino_t inode, queue *q)
{
    int res = DB_OK, sql_res;
    sqlite3_stmt *stmt;
    char *path;

    db_open();

    PREPARE("SELECT path FROM " TABLE_HARDLINK " WHERE inode=?;", &stmt);
    sqlite3_bind_int64(stmt, 1, inode);

    while ((sql_res = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        path = column_text(stmt, 0);
        if (path)
            q_enqueue(q, path);
    }

    if (sql_res != SQLITE_DONE)
    {
        ERRMSG("db_hardlink_get");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_hardlink_add(const char *path, ino_t inode)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("INSERT OR REPLACE INTO " TABLE_HARDLINK " (path, inode) VALUES (?, ?);", &stmt);
    sqlite3_bind_text (stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, inode);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_hardlink_add");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

int db_hardlink_remove(const char *path)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("DELETE FROM " TABLE_HARDLINK " WHERE path=?;", &stmt);
    sqlite3_bind_text (stmt, 1, path, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_hardlink_remove");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}

/***********************/
/* DELETE/RENAME PATHS */
/***********************/

#define DB_x_DELETE_PATH(name, table, column)                               \
int db_ ## name ## _delete_path(const char *path)                           \
{                                                                           \
    int res = DB_OK;                                                        \
    sqlite3_stmt *stmt;                                                     \
                                                                            \
    db_open();                                                              \
                                                                            \
    PREPARE("DELETE FROM " table " WHERE " # column "=?;", &stmt);          \
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);                    \
                                                                            \
    if (sqlite3_step(stmt) != SQLITE_DONE)                                  \
    {                                                                       \
        ERRMSG("db_" #name "_delete_path");                                 \
        res = DB_ERROR;                                                     \
    }                                                                       \
                                                                            \
    sqlite3_finalize(stmt);                                                 \
    db_close();                                                             \
    return res;                                                             \
}

#define DB_x_RENAME_FILE(name, table, column)                               \
int db_ ## name ## _rename_file(const char *from, const char *to)           \
{                                                                           \
    int res = DB_OK;                                                        \
    sqlite3_stmt *stmt;                                                     \
                                                                            \
    db_open();                                                              \
                                                                            \
    PREPARE("UPDATE " table " SET " column "=? WHERE " column "=?;", &stmt);\
    sqlite3_bind_text(stmt, 1, to,   -1, SQLITE_STATIC);                    \
    sqlite3_bind_text(stmt, 2, from, -1, SQLITE_STATIC);                    \
                                                                            \
    if (sqlite3_step(stmt) != SQLITE_DONE)                                  \
    {                                                                       \
        ERRMSG("db_" #name "_rename_file");                                 \
        res = DB_ERROR;                                                     \
    }                                                                       \
                                                                            \
    sqlite3_finalize(stmt);                                                 \
    db_close();                                                             \
    return res;                                                             \
}

#define DB_x_RENAME_DIR(name, table, column)                                \
int db_ ## name ## _rename_dir(const char *from, const char *to)            \
{                                                                           \
    int res = DB_OK, sql_res;                                               \
    sqlite3_stmt *stmt;                                                     \
    size_t from_len, to_len;                                                \
    char *p, *pat, *oldpath, *newpath;                                      \
    queue *q = q_init();                                                    \
                                                                            \
    from_len = strlen(from);                                                \
    to_len = strlen(to);                                                    \
                                                                            \
    if ((pat = malloc(from_len+2)) == NULL)                                 \
    {                                                                       \
        errno = ENOMEM;                                                     \
        return DB_ERROR;                                                    \
    }                                                                       \
    memcpy(pat, from, from_len);                                            \
    memcpy(pat+from_len, "%\0", 2);                                         \
                                                                            \
                                                                            \
    PREPARE("SELECT path FROM " table " WHERE " column " LIKE ?;", &stmt);  \
    sqlite3_bind_text(stmt, 1, pat, -1, SQLITE_STATIC);                     \
                                                                            \
    while ((sql_res = sqlite3_step(stmt)) == SQLITE_ROW)                    \
    {                                                                       \
        p = column_text(stmt, 0);                                           \
        if (p) q_enqueue(q, p);                                             \
    }                                                                       \
                                                                            \
    if (sql_res != SQLITE_DONE)                                             \
    {                                                                       \
        ERRMSG("db_" #name "_rename_dir");                                  \
        res = DB_ERROR;                                                     \
    }                                                                       \
                                                                            \
    sqlite3_finalize(stmt);                                                 \
    free(pat);                                                              \
                                                                            \
                                                                            \
    PREPARE("UPDATE " table " set " column "=? WHERE " column "=?;", &stmt);\
                                                                            \
    while (res == DB_OK && (oldpath = q_dequeue(q)))                        \
    {                                                                       \
        newpath = join_path2(to, to_len, oldpath+from_len, 0);              \
        if (!newpath)                                                       \
        {                                                                   \
            errno = ENOMEM;                                                 \
            res = DB_ERROR;                                                 \
            break;                                                          \
        }                                                                   \
                                                                            \
        sqlite3_bind_text(stmt, 1, newpath, -1, SQLITE_STATIC);             \
        sqlite3_bind_text(stmt, 2, oldpath, -1, SQLITE_STATIC);             \
                                                                            \
        if (sqlite3_step(stmt) != SQLITE_DONE)                              \
        {                                                                   \
            ERRMSG("db_" #name "_rename_dir");                              \
            res = DB_ERROR;                                                 \
            break;                                                          \
        }                                                                   \
                                                                            \
        free(newpath);                                                      \
        free(oldpath);                                                      \
        sqlite3_reset(stmt);                                                \
    }                                                                       \
                                                                            \
    sqlite3_finalize(stmt);                                                 \
    q_free(q, free);                                                        \
    return res;                                                             \
}

#define DB_x_(n, t, c)      \
DB_x_DELETE_PATH(n, t, c)   \
DB_x_RENAME_FILE(n, t, c)   \
DB_x_RENAME_DIR (n, t, c)

DB_x_(job, TABLE_JOB, "path")
DB_x_(sync, TABLE_SYNC, "path")
DB_x_(hardlink, TABLE_HARDLINK, "path")
