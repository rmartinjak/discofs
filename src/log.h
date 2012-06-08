/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_LOG_H
#define FS2GO_LOG_H

#include "funcs.h"

enum log_levels
{
    LOG_NONE,
    LOG_ERROR,
    LOG_INFO,
    LOG_VERBOSE,
    LOG_DEBUG,
    LOG_FSOP
};



#if defined(LOG_ENABLE_WHERE) || defined(LOG_ENABLE_DEBUG)
#define WHERE __FILE__ ":" STR(__LINE__)
#define LOG_PRINT(level, ...) log_print(level, WHERE, __func__, __VA_ARGS__)
#define LOG_ERROR(msg) log_error(WHERE, __func__, msg)
#else
#define WHERE ""
#define LOG_PRINT(level, ...) log_print(level, "", "", __VA_ARGS__)
#define LOG_ERROR(msg) log_error("", "", msg)
#endif


#ifdef DEBUG_FSOPS
#define LOG_ENABLE_DEBUG
#define FSOP(...) LOG_PRINT(LOG_FSOP, __VA_ARGS__)
#else
#define FSOP(...)
#endif


#ifdef LOG_ENABLE_DEBUG
#define LOG_ENABLE_VERBOSE
#define DEBUG(...) LOG_PRINT(LOG_DEBUG, __VA_ARGS__)
#define DEBUGTIME(call)                                                     \
{                                                                           \
    struct timespec before, after; long long td;                            \
    clock_gettime(CLOCK_REALTIME, &before);                                 \
    call;                                                                   \
    clock_gettime(CLOCK_REALTIME, &after);                                  \
    td = (after.tv_sec - before.tv_sec) * 1000000000                        \
        + after.tv_nsec - before.tv_nsec;                                   \
    DEBUG("TIME " STR(call) ": %lld\n", td);                                \
}
#else
#define DEBUG(...)
#define DEBUGTIME(call)
#endif


#ifdef LOG_ENABLE_VERBOSE
#define LOG_ENABLE_INFO
#define VERBOSE(...) LOG_PRINT(LOG_VERBOSE, __VA_ARGS__)
#else
#define VERBOSE(...)
#endif


#ifdef LOG_ENABLE_INFO
#define LOG_ENABLE_ERROR
#define INFO(...) LOG_PRINT(LOG_INFO, __VA_ARGS__)
#else
#define INFO(...)
#endif


#ifdef LOG_ENABLE_ERROR
#define ERROR(...) LOG_PRINT(LOG_ERROR, __VA_ARGS__)
#if defined(LOG_ENABLE_WHERE) || defined(LOG_ENABLE_DEBUG)
#define PERROR(msg) log_error(WHERE, __func__, msg)
#else
#endif
#else
#define ERROR(...)
#define PERROR(msg)
#endif


void log_init(int level, const char *path);
void log_destroy(void);
void log_error(const char *where, const char *func, const char *s);
void log_print(int level, const char *where, const char *func, const char *fmt, ...);

#endif
