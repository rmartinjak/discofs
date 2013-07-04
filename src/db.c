/* discofs - disconnected file system
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


/*=============*
 * DEFINITIONS *
 *=============*/

/*------------------*
 * tables & schemas *
 *------------------*/

#define TABLE_CFG " config "
#define SCHEMA_CFG " option TEXT UNIQUE, nval INTEGER, tval TEXT "

#define TABLE_JOB " job "
#define SCHEMA_JOB " "              \
    "rowid INTEGER PRIMARY KEY,"    \
    "prio INTEGER,"                 \
    "op INTEGER,"                   \
    "time INTEGER,"                 \
    "attempts INTEGER,"             \
    "path TEXT,"                    \
    "n1 INTEGER,"                   \
    "n2 INTEGER,"                   \
    "s1 TEXT,"                      \
    "s2 TEXT"                       \
    " "

#define TABLE_SYNC " sync "
#define SCHEMA_SYNC " "             \
    "path TEXT UNIQUE NOT NULL,"    \
    "mtime_s INTEGER,"              \
    "mtime_ns INTEGER,"             \
    "ctime_s INTEGER,"              \
    "ctime_ns INTEGER"              \
    " "

/*--------------------*
 * convenience macros *
 *--------------------*/

#define ERRMSG(msg) ERROR(msg ": %s", sqlite3_errmsg(db))

#define PREPARE(sql, stmt)                                                  \
do {                                                                        \
    if (sqlite3_prepare_v2(db, sql, -1, stmt, NULL) != SQLITE_OK)           \
    {                                                                       \
        ERRMSG("preparing statement");                                      \
        db_close();                                                         \
        return DB_ERROR;                                                    \
    }                                                                       \
} while (0)


/* database object */
static sqlite3 *db;

/* mutex because only one function should access the database */
static pthread_mutex_t m_db = PTHREAD_MUTEX_INITIALIZER;


/*-------------------*
 * static prototypes *
 *-------------------*/

static char *column_text(sqlite3_stmt *stmt, int n);
static void db_open(void);
static void db_close(void);


/*==================*
 * STATIC FUNCTIONS *
 *==================*/

static char *column_text(sqlite3_stmt *stmt, int n)
{
    const unsigned char *p = sqlite3_column_text(stmt, n);

    if (!p)
        return NULL;

    return strdup((const char*)p);
}

static void db_open(void)
{
    pthread_mutex_lock(&m_db);
}

static void db_close(void)
{
    pthread_mutex_unlock(&m_db);
}


/*====================*
 * EXPORTED FUNCTIONS *
 *====================*/

int db_init(const char *path, int clear)
{
    VERBOSE("initializing db in %s", path);

    if (sqlite3_open(path, &db) != SQLITE_OK)
    {
        ERROR("error initializing db: %s", sqlite3_errmsg(db));
        return -1;
    }

    db_open();

#define NEW_TABLE(t, sql)                                                   \
    {                                                                       \
        if (sqlite3_exec(db, "CREATE TABLE " t " ( " sql " );",             \
                    NULL, NULL, NULL))                                      \
        {                                                                   \
            db_close();                                                     \
            ERROR("couldn't create table " t "");                           \
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

#undef NEW_TABLE
#undef CREATE_TABLE

    DEBUG("db initialization finished");
    db_close();
    return 0;
}

int db_destroy(void)
{
    VERBOSE("closing database connection");
    sqlite3_close(db);
    return 0;
}


/*--------*
 * config *
 *--------*/

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


/*-----*
 * job *
 *-----*/

int db_job_store(const struct job *j)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    if (!j->path)
    {
        errno = EINVAL;
        return DB_ERROR;
    }

    DEBUG("storing %s on %s in db", job_opstr(j->op), j->path);

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

int db_job_get(struct job **j)
{
    int res = DB_OK, sql_res;
    sqlite3_stmt *stmt;
    time_t now = time(NULL);
    struct job *p;

    *j = NULL;

    db_open();

    PREPARE("SELECT rowid, op, time, attempts, path, n1, n2, s1, s2 FROM "
            TABLE_JOB " WHERE time < ? ORDER BY prio DESC, time ASC LIMIT 1;",
            &stmt);
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


/*------*
 * sync *
 *------*/

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

        callback(path, mtime, ctime);
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

int db_sync_delete_path(const char *path)
{
    int res = DB_OK;
    sqlite3_stmt *stmt;

    db_open();

    PREPARE("DELETE FROM " TABLE_SYNC " WHERE path=?;", &stmt);
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ERRMSG("db_sync_delete_path");
        res = DB_ERROR;
    }

    sqlite3_finalize(stmt);
    db_close();
    return res;
}


/*--------------*
 * rename paths *
 *--------------*/

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
    char *pat;                                                              \
                                                                            \
    PREPARE("UPDATE " table " SET " column " = replace(" column ", ?, ?) "  \
        "WHERE " column " LIKE ?;", &stmt);                                 \
                                                                            \
    if ((pat = malloc(strlen(from) + strlen("/%") + 1)) == NULL)            \
    {                                                                       \
        sqlite3_finalize(stmt);                                             \
        errno = ENOMEM;                                                     \
        return DB_ERROR;                                                    \
    }                                                                       \
    strcpy(pat, from);                                                      \
    strcat(pat, "/%");                                                      \
                                                                            \
    sqlite3_bind_text(stmt, 1, from, -1, SQLITE_STATIC);                    \
    sqlite3_bind_text(stmt, 2, to, -1, SQLITE_STATIC);                      \
    sqlite3_bind_text(stmt, 3, pat, -1, SQLITE_STATIC);                     \
                                                                            \
    sql_res = sqlite3_step(stmt);                                           \
                                                                            \
    if (sql_res != SQLITE_DONE)                                             \
    {                                                                       \
        ERRMSG("db_" #name "_rename_dir");                                  \
        res = DB_ERROR;                                                     \
    }                                                                       \
                                                                            \
    sqlite3_finalize(stmt);                                                 \
                                                                            \
    if (!strcmp(table, TABLE_JOB))                                          \
    {                                                                       \
        PREPARE("UPDATE " table " SET s1 = replace(s1, ?, ?) "              \
            "WHERE (op = ? OR op = ?) AND s1 LIKE ?;", &stmt);              \
                                                                            \
        sqlite3_bind_text(stmt, 1, from, -1, SQLITE_STATIC);                \
        sqlite3_bind_text(stmt, 2, to, -1, SQLITE_STATIC);                  \
        sqlite3_bind_int (stmt, 3, JOB_RENAME);                             \
        sqlite3_bind_int (stmt, 4, JOB_LINK);                               \
        sqlite3_bind_text(stmt, 5, pat, -1, SQLITE_STATIC);                 \
                                                                            \
        sql_res = sqlite3_step(stmt);                                       \
                                                                            \
        if (sql_res != SQLITE_DONE)                                         \
        {                                                                   \
            ERRMSG("db_" #name "_rename_dir");                              \
            res = DB_ERROR;                                                 \
        }                                                                   \
        sqlite3_finalize(stmt);                                             \
    }                                                                       \
                                                                            \
    free(pat);                                                              \
    return res;                                                             \
}

#define DB_x_(n, t, c)      \
DB_x_RENAME_FILE(n, t, c)   \
DB_x_RENAME_DIR (n, t, c)

DB_x_(job, TABLE_JOB, "path")
DB_x_(sync, TABLE_SYNC, "path")
