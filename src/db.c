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
#include "hashtable.h"

#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <limits.h>

/* welcme to macro hell ;) */

#define ERRMSG(msg) ERROR(msg ": %s\n", sqlite3_errmsg(db))

#define EXITIFNOT(call, val) { if ((call) != val) { ERRMSG(STR(call)); FINALIZE(); db_close(); return DB_ERROR; } }
#define EXITNOK(call) EXITIFNOT(call, SQLITE_OK)

#define VARS int res=DB_OK, bindpos=1, colpos=0; sqlite3_stmt *stmt

#define PREPARE(q) EXITNOK(sqlite3_prepare_v2(db, q, -1, &stmt, NULL))
#define STEP() sqlite3_step(stmt)
#define RESET() { sqlite3_reset(stmt); bindpos=1; colpos=0; }
#define FINALIZE() { sqlite3_finalize(stmt); bindpos=1; colpos=0; }

#define BIND_INT64(v) EXITNOK(sqlite3_bind_int64(stmt, bindpos++, (sqlite3_int64)v))
#define BIND_INT(v) EXITNOK(sqlite3_bind_int(stmt, bindpos++, (int)v))
#define BIND_TEXT(v) EXITNOK(sqlite3_bind_text(stmt, bindpos++, v, DB_ERROR, SQLITE_STATIC))

#define BIND_TIME_T(v) BIND_INT64(v)
#define BIND_LONG(v) BIND_INT64(v)
#define BIND_JOBP(v) BIND_INT64(v)
#define BIND_ROWID(v) BIND_INT64(v)

#define COL_INT64(t) (t)sqlite3_column_int64(stmt, colpos++)
#define COL_INT() sqlite3_column_int(stmt, colpos++)

#define COL_ROWID() COL_INT64(long long)
#define COL_TIME_T() COL_INT64(time_t)
#define COL_LONG() COL_INT64(long)
#define COL_JOBP() COL_INT64(jobp_t)
#define COL_TEXT() (sqlite3_column_text(stmt, colpos++)) ? strdup((char *)sqlite3_column_text(stmt, colpos-1)) : NULL

sqlite3 *db;
char *db_fn;
static pthread_mutex_t m_db = PTHREAD_MUTEX_INITIALIZER;

static int db_rename_dir_(const char *sql_select, const char *sql_update,
        const char *pat, const char *from, size_t from_len, const char *to, size_t to_len);

/* ====== GENERAL DB STUFF ====== */
void db_open(void)
{
    pthread_mutex_lock(&m_db);
}

void db_close(void)
{
    pthread_mutex_unlock(&m_db);
}

int db_init(const char *fn, int clear)
{
    STRDUP(db_fn, fn);

    VERBOSE("initializing db in %s\n", db_fn);

    if (sqlite3_open(db_fn, &db) != SQLITE_OK) {
        FATAL("sqlite error: %s\n", sqlite3_errmsg(db));
    }

    db_open();

#define NEW_TABLE(t, sql) \
    { \
        if (sqlite3_exec(db, "CREATE TABLE " t " ( " sql " );", NULL, NULL, NULL)) { \
            FATAL("couldn't create table " t "\n"); \
        } \
    }

#define CREATE_TABLE(t, sql) \
    { \
        if (sqlite3_exec(db, "SELECT * FROM " t " LIMIT 1;", NULL, NULL, NULL)) { \
            NEW_TABLE(t, sql); \
        } \
        if (clear) { \
            sqlite3_exec(db, "DELETE FROM " t ";", NULL, NULL, NULL); \
        } \
    }

    /* create tables */
    CREATE_TABLE(TABLE_CFG, SCHEMA_CFG);
    CREATE_TABLE(TABLE_JOB, SCHEMA_JOB);
    CREATE_TABLE(TABLE_SYNC, SCHEMA_SYNC);

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
    if (db_fn)
        free(db_fn);
    return 0;
}

/* ====== CONFIG ====== */
#define CFG_DEL(o) \
{ \
    PREPARE("DELETE FROM " TABLE_CFG " WHERE option=?;"); \
    BIND_TEXT(o); \
    STEP(); \
    FINALIZE(); \
}

int db_cfg_delete(const char *option)
{
    VARS;
    db_open();
    PREPARE("DELETE FROM " TABLE_CFG " WHERE option=?;"); \
        BIND_TEXT(option);

    if (STEP() != SQLITE_DONE) {
        ERRMSG("deleting config option");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_cfg_set_int(const char *option, int val)
{
    VARS;

    db_open();
    CFG_DEL(option);

    PREPARE("INSERT INTO " TABLE_CFG " (option, nval) VALUES (?, ?);");
    BIND_TEXT(option);
    BIND_INT(val);

    if (STEP() != SQLITE_DONE) {
        ERRMSG("db_cfg_set_int");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_cfg_set_str(char *option, const char *val)
{
    VARS;

    db_open();
    CFG_DEL(option);

    PREPARE("INSERT INTO " TABLE_CFG " (option, tval) VALUES (?, ?);");
    BIND_TEXT(option);
    BIND_TEXT(val);

    if (STEP() != SQLITE_DONE) {
        ERRMSG("db_cfg_set_str");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_cfg_get_int(const char *option, int *buf)
{
    VARS;

    db_open();
    PREPARE("SELECT nval FROM " TABLE_CFG " WHERE option=?;");
    BIND_TEXT(option);

    if (STEP() != SQLITE_ROW) {
        ERRMSG("db_cfg_get_int");
        res = DB_ERROR;
    }
    else {
        *buf = COL_INT();
    }

    FINALIZE();
    db_close();
    return res;
}

int db_cfg_get_str(const char *option, char **buf)
{
    VARS;

    db_open();
    PREPARE("SELECT tval FROM " TABLE_CFG " WHERE option=?;");
    BIND_TEXT(option);

    if (STEP() != SQLITE_ROW) {
        ERRMSG("db_cfg_get_str");
        res = DB_ERROR;
    }
    else {
        *buf = COL_TEXT();
    }

    FINALIZE();
    db_close();
    return res;
}

/* ====== JOB ====== */
int db_store_job(const struct job *j)
{
    VARS;
#if HAVE_CLOCK_GETTIME
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
#else
    time_t now;
    now = time(NULL);
#endif

    if (!j->path) {
        errno = EINVAL;
        return DB_ERROR;
    }

    DEBUG("storing job for %s\n", j->path);
    if (j-> op == JOB_PUSH)
        DEBUG("it's a PUSH job\n");
    if (j-> op == JOB_PULL)
        DEBUG("it's a PULL job\n");

    if (j->op == JOB_PUSH || j->op == JOB_PULL)
        db_delete_jobs(j->path, JOB_PUSH|JOB_PULL);

    db_open();

#define COLNAMES_JOB "prio, op, attempts, time_s, time_ns, path, param1, param2, sparam1, sparam2"
    PREPARE("INSERT INTO " TABLE_JOB " (" COLNAMES_JOB ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    BIND_INT(j->prio);
    BIND_INT(j->op);
    BIND_INT(j->attempts);
#if HAVE_CLOCK_GETTIME
    BIND_TIME_T(now.tv_sec);
    BIND_LONG(now.tv_nsec);
#else
    BIND_TIME_T(now);
    BIND_LONG(0);
#endif
    BIND_TEXT(j->path);
    BIND_JOBP(j->param1);
    BIND_JOBP(j->param2);
    BIND_TEXT(j->sparam1);
    BIND_TEXT(j->sparam2);

    if (STEP() != SQLITE_DONE) {
        ERRMSG("db_store_job:");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_defer_job(long long id)
{
    VARS;

    db_open();

    PREPARE("UPDATE " TABLE_JOB " set time_s=time_s+?, attempts=attempts+1 where rowid=?;");

    BIND_INT(JOB_DEFER_TIME);
    BIND_ROWID(id);

    if (STEP() != SQLITE_DONE) {
        ERRMSG("deleting jobs by path");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_has_job(const char *path, int opmask)
{
    VARS;

    db_open();
    if (opmask == JOB_ANY) {
        PREPARE("SELECT rowid FROM " TABLE_JOB " WHERE path=?;");
        BIND_TEXT(path)
    }
    else {
        PREPARE("SELECT rowid FROM " TABLE_JOB " WHERE path=? AND (op & ?) != 0;");
        BIND_TEXT(path)
            BIND_INT(opmask);
    }

    res = (STEP() == SQLITE_ROW);

    FINALIZE();
    db_close();

    return res;
}

#define COLS_JOB(j) \
{ \
    colpos = 0; \
    j->rowid = COL_ROWID(); \
    j->prio = COL_INT(); \
    j->op = COL_INT(); \
    j->attempts = COL_INT(); \
    j->path = COL_TEXT(); \
    j->param1 = COL_JOBP(); \
    j->param2 = COL_JOBP(); \
    j->sparam1 = COL_TEXT(); \
    j->sparam2 = COL_TEXT(); \
}
#define SELECT_JOB "SELECT rowid, prio, op, attempts, path, param1, param2, sparam1, sparam2 FROM " TABLE_JOB " "
int db_get_jobs(queue *qu)
{
    VARS;
    int sql_res;
    struct job *j;
#if HAVE_CLOCK_GETTIME
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
#else
    time_t now;
    now = time(NULL);
#endif

    db_open();
#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    PREPARE(SELECT_JOB "WHERE time_s<? OR (time_s=? AND time_ns <?) ORDER BY prio DESC, time_s ASC, time_ns ASC;");
    BIND_TIME_T(now.tv_sec);
    BIND_LONG(now.tv_nsec);
#else
    PREPARE(SELECT_JOB "ORDER BY prio DESC, time_s ASC WHERE time_s<?;");
    BIND_TIME_T(now);
#endif

    while ((sql_res = STEP()) == SQLITE_ROW) {
        colpos = 0;
        j = malloc(sizeof (struct job));
        JOB_INIT(j);
        if (!j) {
            res = DB_ERROR;
            break;
        }
        COLS_JOB(j);
        if (q_enqueue(qu, j) == -1) {
            res = DB_ERROR;
            break;
        }
    }
    if (sql_res != SQLITE_DONE) {
        ERRMSG("db_rename_dir");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_get_job_by_id(struct job *j, long long id)
{
    VARS;

    db_open();
    PREPARE(SELECT_JOB "WHERE rowid=?;");

    BIND_ROWID(id);

    if (STEP() != SQLITE_ROW) {
        /* clear j */
        JOB_INIT(j);
        res = DB_ERROR;
    }
    else {
        COLS_JOB(j);
    }

    FINALIZE();
    db_close();
    return res;
}

int db_delete_jobs(const char *path, int opmask)
{
    VARS;

    db_open();
    if (opmask == JOB_ANY) {
        PREPARE("DELETE FROM " TABLE_JOB " WHERE path=?;");
        BIND_TEXT(path);
    }
    else {
        PREPARE("DELETE FROM " TABLE_JOB " WHERE path=? AND (op & ?) != 0;");
        BIND_TEXT(path);
        BIND_INT(opmask);
    }

    if (STEP() != SQLITE_DONE) {
        ERRMSG("deleting jobs by path");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_delete_job_id(long long id)
{
    VARS;

    db_open();
    PREPARE("DELETE FROM " TABLE_JOB " WHERE rowid=?;");
    BIND_ROWID(id);

    if (STEP() != SQLITE_DONE) {
        ERRMSG("delete job by id");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

/* ====== SYNC ====== */
int db_load_sync(sync_load_cb_t callback)
{
    VARS;
    int sql_res;
    char *path;
    sync_xtime_t mtime, ctime;

    db_open();

#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    PREPARE("SELECT path, mtime_s, mtime_ns, ctime_s, ctime_ns FROM " TABLE_SYNC ";");
#else
    PREPARE("SELECT path, mtime_s, ctime_s FROM " TABLE_SYNC ";");
#endif

    while ((sql_res = STEP()) == SQLITE_ROW) {
        colpos = 0;
        path = COL_TEXT();

#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
        mtime.tv_sec = COL_TIME_T();
        mtime.tv_nsec = COL_LONG();

        ctime.tv_sec = COL_TIME_T();
        ctime.tv_nsec = COL_LONG();
#else
        mtime = COL_TIME_T();
        ctime = COL_TIME_T();
#endif

        if (callback(path, mtime, ctime) == NULL) {
            PERROR("db_load_syncs in callback()");
            free(path);
            res = DB_ERROR;
            break;
        }
        free(path);
    }

    if (sql_res != SQLITE_DONE) {
        ERRMSG("db_load_syncs in sqlite3_step()");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_store_sync(const struct sync *s)
{
    VARS;

    db_open();
#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    PREPARE("INSERT OR REPLACE INTO " TABLE_SYNC " (path, mtime_s, mtime_ns, ctime_s, ctime_ns) VALUES (?, ?, ?, ?, ?)");
    BIND_TEXT(s->path);
    BIND_TIME_T(s->mtime.tv_sec);
    BIND_LONG(s->mtime.tv_nsec);
    BIND_TIME_T(s->ctime.tv_sec);
    BIND_LONG(s->ctime.tv_nsec);
#else
    PREPARE("INSERT OR REPLACE INTO " TABLE_SYNC " (path, mtime_s, ctime_s) VALUES (?, ?, ?)");
    BIND_TEXT(s->path);
    BIND_TIME_T(s->mtime);
    BIND_TIME_T(s->ctime);
#endif

    if (STEP() != SQLITE_DONE) {
        ERRMSG("error setting sync");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_delete_path(const char *path)
{
    VARS;
#define DELETE(table) \
    { \
        if (res == DB_OK) { \
            PREPARE("DELETE FROM " table " WHERE path=?;");\
            BIND_TEXT(path); \
            if (STEP() != SQLITE_DONE) { \
                ERRMSG("delete_path in table " table); \
                res = DB_ERROR; \
            }; \
            FINALIZE(); } \
    }

    db_open();

    DELETE(TABLE_SYNC);
    DELETE(TABLE_JOB);

    db_close();
    return res;
#undef DELETE
}

int db_rename_file(const char *from, const char *to)
{
    VARS;

#define RENAME(table) if (res == DB_OK) { \
    PREPARE("UPDATE " table " SET path=? WHERE path=?;"); \
    BIND_TEXT(to); BIND_TEXT(from); \
    if (STEP() != SQLITE_DONE) { \
        ERRMSG("rename in table " table); \
        res = DB_ERROR; \
    }; \
    FINALIZE(); }

    db_open();

    RENAME(TABLE_SYNC);
    RENAME(TABLE_JOB);

    db_close();
    return res;
#undef RENAME
}

static int db_rename_dir_(const char *sql_select, const char *sql_update,
        const char *pat, const char *from, size_t from_len, const char *to, size_t to_len)
{
    VARS;
    int sql_res;
    queue q = QUEUE_INIT;
    char *oldpath, *newpath;

    db_open();

    PREPARE(sql_select)
    BIND_TEXT(pat);
    while ((sql_res = STEP()) == SQLITE_ROW) {
        colpos = 0;
        q_enqueue(&q, COL_TEXT());
    }
    if (sql_res != SQLITE_DONE) {
        ERRMSG("db_rename_dir");
        res = DB_ERROR;
    }
    FINALIZE();

    PREPARE(sql_update);
    while (res == DB_OK && (oldpath = q_dequeue(&q))) {
        newpath = join_path(to, to_len, oldpath+from_len, strlen(oldpath)-from_len);
        if (!newpath) {
            errno = ENOMEM;
            return DB_ERROR;
        }
        BIND_TEXT(newpath);
        BIND_TEXT(oldpath);
        if (STEP() != SQLITE_DONE) {
            ERRMSG("db_rename_dir");
            res = DB_ERROR;
            q_clear(&q, 1);
        }
        free(newpath);
        free(oldpath);
        RESET();
    }
    FINALIZE();

    db_close();
    return res;
}

int db_rename_dir(const char *from, const char *to)
{
    int res = DB_OK;
    char *pat;
    size_t from_len, to_len;

    from_len = strlen(from);
    to_len = strlen(to);

    if ((pat = malloc(from_len+2)) == NULL) {
        errno = ENOMEM;
        return DB_ERROR;
    }
    memcpy(pat, from, from_len);
    memcpy(pat+from_len, "%\0", 2);

#define RENAME(t) if (res == DB_OK) { \
    res = db_rename_dir_("SELECT path FROM " t " WHERE path LIKE ?;", \
            "UPDATE " t " set path=? WHERE path=?;", \
            pat, from, from_len, to, to_len); \
}
    RENAME(TABLE_JOB);
    RENAME(TABLE_SYNC);
#undef RENAME
    free(pat);
    return res;
    }

int deb_delete_path(const char *path)
{
    VARS;
#define DELETE(t) if (res == DB_OK) { \
    PREPARE("DELETE FROM " t " WHERE path=?;"); \
    BIND_TEXT(path); \
    if (STEP() != SQLITE_DONE) { \
        ERRMSG("delete_path in table " t); \
        res = DB_ERROR; \
    }; \
    FINALIZE(); }

    db_open();
    DELETE(TABLE_SYNC);
    DELETE(TABLE_JOB);
    db_close();

    return res;
}
