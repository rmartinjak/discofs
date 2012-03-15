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

#if DEBUG_SQL
#define STEP() (DEBUG("SQL: %s\n", sqlite3_sql(stmt)), sqlite3_step(stmt))
#else
#define STEP() sqlite3_step(stmt)
#endif

#define RESET() { sqlite3_reset(stmt); bindpos=1; colpos=0; }
#define FINALIZE() { sqlite3_finalize(stmt); bindpos=1; colpos=0; }

#define BIND_INT64(v) EXITNOK(sqlite3_bind_int64(stmt, bindpos++, (sqlite3_int64)v))
#define BIND_NULL() EXITNOK(sqlite3_bind_null(stmt, bindpos++))
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
#define COL_JOBP() COL_INT64(job_param)
#define COL_TEXT() (sqlite3_column_text(stmt, colpos++)) ? strdup((char *)sqlite3_column_text(stmt, colpos-1)) : NULL

sqlite3 *db;
char *db_fn;
static pthread_mutex_t m_db = PTHREAD_MUTEX_INITIALIZER;


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

int db_init(const char *fn, int clear)
{
    STRDUP(db_fn, fn);

    VERBOSE("initializing db in %s\n", db_fn);

    if (sqlite3_open(db_fn, &db) != SQLITE_OK)
    {
        FATAL("sqlite error: %s\n", sqlite3_errmsg(db));
    }

    db_open();

#define NEW_TABLE(t, sql)                                                      \
    {                                                                          \
        if (sqlite3_exec(db, "CREATE TABLE " t " ( " sql " );",                \
                    NULL, NULL, NULL))                                         \
        {                                                                      \
            FATAL("couldn't create table " t "\n");                            \
        }                                                                      \
    }

#define CREATE_TABLE(t, sql)                                                   \
    {                                                                          \
        if (sqlite3_exec(db, "SELECT * FROM " t " LIMIT 1;",                   \
                    NULL, NULL, NULL))                                         \
        {                                                                      \
            NEW_TABLE(t, sql);                                                 \
        }                                                                      \
        if (clear)                                                             \
        {                                                                      \
            sqlite3_exec(db, "DELETE FROM " t ";", NULL, NULL, NULL);          \
        }                                                                      \
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


/**********/
/* CONFIG */
/**********/

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

    if (STEP() != SQLITE_DONE)
    {
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

    if (STEP() != SQLITE_DONE)
    {
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

    if (STEP() != SQLITE_DONE)
    {
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
    int sql_res;

    db_open();
    PREPARE("SELECT nval FROM " TABLE_CFG " WHERE option=?;");
    BIND_TEXT(option);

    sql_res = STEP();

    if (sql_res == SQLITE_ROW)
    {
        *buf = COL_INT();
    }
    else if (sql_res == SQLITE_ERROR)
    {
        ERRMSG("db_cfg_get_int");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_cfg_get_str(const char *option, char **buf)
{
    VARS;
    int sql_res;

    db_open();
    PREPARE("SELECT tval FROM " TABLE_CFG " WHERE option=?;");
    BIND_TEXT(option);

    sql_res = STEP();

    if (sql_res == SQLITE_ROW)
    {
        *buf = COL_TEXT();
    }
    else if (sql_res == SQLITE_ERROR)
    {
        ERRMSG("db_cfg_get_str");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}


/*******/
/* JOB */
/*******/

int db_job_store(const struct job *j)
{
    VARS;

    if (!j->path)
    {
        errno = EINVAL;
        return DB_ERROR;
    }

    DEBUG("storing job for %s\n", j->path);

    db_open();

#define COLS "rowid, prio, op, time, attempts, path, n1, n2, s1, s2"
#define VALS "?, ?, ?, ?, ?, ?, ?, ?, ?, ?"
    PREPARE("INSERT OR REPLACE INTO " TABLE_JOB " (" COLS ") VALUES (" VALS ");");
#undef COLS
#undef VALS

    if (j->id <= 0)
    {
        BIND_NULL();
    }
    else
    {
        BIND_ROWID(j->id);
    }

    BIND_INT(OP_PRIO(j->op));
    BIND_INT(j->op);

    BIND_TIME_T(j->time);
    BIND_INT(j->attempts);

    BIND_TEXT(j->path);

    BIND_JOBP(j->n1);
    BIND_JOBP(j->n2);

    BIND_TEXT(j->s1);
    BIND_TEXT(j->s2);

    if (STEP() != SQLITE_DONE)
    {
        ERRMSG("db_store_job:");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

#define SELECT_JOB "SELECT rowid, op, time, attempts, path, n1, n2, s1, s2 FROM " TABLE_JOB " "
int db_job_get(struct job **j)
{
    VARS;
    int sql_res;
    time_t now = time(NULL);
    struct job *p;

    *j = NULL;

    db_open();

    PREPARE(SELECT_JOB "WHERE time <? ORDER BY prio DESC, time ASC LIMIT 1;");
    BIND_TIME_T(now);

    sql_res = STEP();
    if (sql_res == SQLITE_ROW)
    {
        p = job_alloc();

        if (!p)
        {
            res = DB_ERROR;
        }
        else
        {
            p->id = COL_ROWID();
            p->op = COL_INT();

            p->time = COL_TIME_T();
            p->attempts = COL_INT();

            p->path = COL_TEXT();

            p->n1 = COL_JOBP();
            p->n2 = COL_JOBP();

            p->s1 = COL_TEXT();
            p->s2 = COL_TEXT();

            *j = p;
        }
    }
    /* if no rows returned, sql_res would be SQLITE_DONE */
    else if (sql_res != SQLITE_DONE)
    {
        ERRMSG("db_job_get");
        res = DB_ERROR;
    }

    FINALIZE();

    db_close();
    return res;
}

int db_job_exists(const char *path, int opmask)
{
    VARS;

    db_open();
    if (opmask == JOB_ANY)
    {
        PREPARE("SELECT rowid FROM " TABLE_JOB " WHERE path=?;");
        BIND_TEXT(path);
    }
    else
    {
        PREPARE("SELECT rowid FROM " TABLE_JOB " WHERE path=? AND (op & ?) != 0;");
        BIND_TEXT(path);
        BIND_INT(opmask);
    }

    res = (STEP() == SQLITE_ROW);

    FINALIZE();
    db_close();

    return res;
}

int db_job_delete(const char *path, int opmask)
{
    VARS;

    db_open();
    if (opmask == JOB_ANY)
    {
        PREPARE("DELETE FROM " TABLE_JOB " WHERE path=?;");
        BIND_TEXT(path);
    }
    else
    {
        PREPARE("DELETE FROM " TABLE_JOB " WHERE path=? AND (op & ?) != 0;");
        BIND_TEXT(path);
        BIND_INT(opmask);
    }

    if (STEP() != SQLITE_DONE)
    {
        ERRMSG("db_job_delete");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_job_delete_id(job_id id)
{
    VARS;

    db_open();
    PREPARE("DELETE FROM " TABLE_JOB " WHERE rowid=?;");
    BIND_ROWID(id);

    if (STEP() != SQLITE_DONE)
    {
        ERRMSG("db_job_delete_id");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}

int db_job_delete_rename_to(const char *path)
{
    VARS;

    db_open();

    PREPARE("DELETE FROM " TABLE_JOB " WHERE op=? AND s1=?;");
    BIND_INT(JOB_RENAME);
    BIND_TEXT(path);

    if (STEP() != SQLITE_DONE)
    {
        ERRMSG("db_job_delete_rename_to");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}


/********/
/* SYNC */
/********/

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

    while ((sql_res = STEP()) == SQLITE_ROW)
    {
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

    if (STEP() != SQLITE_DONE)
    {
        ERRMSG("error setting sync");
        res = DB_ERROR;
    }

    FINALIZE();
    db_close();
    return res;
}


/***********************/
/* DELETE/RENAME PATHS */
/***********************/

#define DB_x_DELETE_PATH(name, table, column)                                  \
int db_ ## name ## _delete_path(const char *path)                              \
{                                                                              \
    VARS;                                                                      \
                                                                               \
    db_open();                                                                 \
                                                                               \
    PREPARE("DELETE FROM " table " WHERE " # column "=?;");                    \
    BIND_TEXT(path);                                                           \
                                                                               \
    if (STEP() != SQLITE_DONE)                                                 \
    {                                                                          \
        ERRMSG("db_" #name "_delete_path");                                    \
        res = DB_ERROR;                                                        \
    }                                                                          \
                                                                               \
    FINALIZE();                                                                \
                                                                               \
    db_close();                                                                \
    return res;                                                                \
}

#define DB_x_RENAME_FILE(name, table, column)                                  \
int db_ ## name ## _rename_file(const char *from, const char *to)              \
{                                                                              \
    VARS;                                                                      \
                                                                               \
    db_open();                                                                 \
                                                                               \
    PREPARE("UPDATE " table " SET " column "=? WHERE " column "=?;");          \
    BIND_TEXT(to); BIND_TEXT(from);                                            \
    if (STEP() != SQLITE_DONE)                                                 \
    {                                                                          \
        ERRMSG("db_" #name "_rename_file");                                    \
        res = DB_ERROR;                                                        \
    }                                                                          \
    FINALIZE();                                                                \
                                                                               \
                                                                               \
    db_close();                                                                \
    return res;                                                                \
}

#define DB_x_RENAME_DIR(name, table, column)                                   \
int db_ ## name ## _rename_dir(const char *from, const char *to)               \
{                                                                              \
    VARS;                                                                      \
    int sql_res;                                                               \
    char *pat;                                                                 \
    size_t from_len, to_len;                                                   \
    char *oldpath, *newpath;                                                   \
    queue *q = q_init();                                                       \
                                                                               \
    from_len = strlen(from);                                                   \
    to_len = strlen(to);                                                       \
                                                                               \
    if ((pat = malloc(from_len+2)) == NULL)                                    \
    {                                                                          \
        errno = ENOMEM;                                                        \
        return DB_ERROR;                                                       \
    }                                                                          \
    memcpy(pat, from, from_len);                                               \
    memcpy(pat+from_len, "%\0", 2);                                            \
                                                                               \
                                                                               \
    PREPARE("SELECT path FROM " table " WHERE " column " LIKE ?;");            \
    BIND_TEXT(pat);                                                            \
    free(pat);                                                                 \
                                                                               \
    while ((sql_res = STEP()) == SQLITE_ROW)                                   \
    {                                                                          \
        colpos = 0;                                                            \
        q_enqueue(q, COL_TEXT());                                              \
    }                                                                          \
                                                                               \
    if (sql_res != SQLITE_DONE)                                                \
    {                                                                          \
        ERRMSG("db_" #name "_rename_dir");                                     \
        res = DB_ERROR;                                                        \
    }                                                                          \
    FINALIZE();                                                                \
                                                                               \
                                                                               \
    PREPARE("UPDATE " table " set " column "=? WHERE " column "=?;")           \
                                                                               \
    while (res == DB_OK && (oldpath = q_dequeue(q)))                           \
    {                                                                          \
        newpath = join_path2(to, to_len, oldpath+from_len, 0);                 \
        if (!newpath)                                                          \
        {                                                                      \
            errno = ENOMEM;                                                    \
            res = DB_ERROR;                                                    \
            break;                                                             \
        }                                                                      \
        BIND_TEXT(newpath);                                                    \
        BIND_TEXT(oldpath);                                                    \
        if (STEP() != SQLITE_DONE)                                             \
        {                                                                      \
            ERRMSG("db_" #name "_rename_dir");                                 \
            res = DB_ERROR;                                                    \
            break;                                                             \
        }                                                                      \
        free(newpath);                                                         \
        free(oldpath);                                                         \
        RESET();                                                               \
    }                                                                          \
    FINALIZE();                                                                \
                                                                               \
    q_free(q, free);                                                           \
    return res;                                                                \
}

#define DB_x_(n, t, c)      \
DB_x_DELETE_PATH(n, t, c)   \
DB_x_RENAME_FILE(n, t, c)   \
DB_x_RENAME_DIR (n, t, c)

DB_x_(job, TABLE_JOB, "path")

DB_x_(sync, TABLE_SYNC, "path")
