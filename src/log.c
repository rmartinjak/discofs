/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "log.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static pthread_mutex_t m_log_print = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_log_error = PTHREAD_MUTEX_INITIALIZER;

static int log_maxlvl = LOG_NONE;
static char *log_lvlstr[] = { "", "ERROR", "INFO", "VERBOSE", "DEBUG", "FSOP" };

static char *log_filename = NULL;


void log_init(int level, const char *file)
{
    log_maxlvl = level;

    if (file)
        log_filename = strdup(file);

    log_print(LOG_VERBOSE, "Logging initialized with level %d\n", level);
}

void log_destroy()
{
    log_maxlvl = LOG_NONE;

    if (log_filename)
        free(log_filename);
}

void log_error(const char *s)
{
    pthread_mutex_lock(&m_log_error);
    log_print(LOG_ERROR, "%s: %s\n", s, strerror(errno));
    pthread_mutex_unlock(&m_log_error);
}

#if HAVE_VFPRINTF
void log_print(int level, const char *fmt, ...)
{
    FILE *f;
    time_t now;
    char *ctim;
    va_list ap;

    va_start(ap, fmt);

    if (level <= log_maxlvl)
    {
        pthread_mutex_lock(&m_log_print);
        f = NULL;

        if (log_filename)
        {
            f = fopen(log_filename, "a");
        }
        if (!f)
            f = stderr;

        now = time(NULL);
        ctim = strdup(ctime(&now));

        /* remove \n */
        *(ctim + strlen(ctim)-1) = '0';

        fprintf(f, "%s %s: ", ctim, log_lvlstr[level]);

        vfprintf(f, fmt, ap);

        if (f != stderr)
            fclose(f);

        free(ctim);
        pthread_mutex_unlock(&m_log_print);
    }
    va_end(ap);
}
#else
void log_print(int level, const char *fmt, ...)
{
    return;
}
#endif
