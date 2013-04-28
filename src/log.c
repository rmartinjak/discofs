/* discofs - disconnected file system
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

static char *log_lvlstr[] = { "", "ERROR", "INFO", "VERBOSE", "DEBUG", "FSOP" };

static int loglvl = LOG_NONE;
static FILE *logf = NULL;


void log_init(int level, const char *file)
{
    loglvl = level;

    if (!file)
        logf = stderr;
    else {
        if ((logf = fopen(file, "a")) == NULL)
        {
            perror("error opening log file");
            fprintf(stderr, "%s\n", "falling back to standard error");
            logf = stderr;
        }
    }

    log_print(LOG_VERBOSE, "", "", "Logging initialized with level %s", log_lvlstr[level]);
}

void log_destroy(void)
{
    loglvl = LOG_NONE;

    if (logf != stderr)
        fclose(logf);
}

void log_error(const char *where, const char *func, const char *s)
{
    pthread_mutex_lock(&m_log_error);
    log_print(LOG_ERROR, where, func, "%s: %s", s, strerror(errno));
    pthread_mutex_unlock(&m_log_error);
}

#if HAVE_VFPRINTF
void log_print(int level, const char *where, const char *func, const char *fmt, ...)
{
    time_t now;
    char *ctim;
    va_list ap;

    va_start(ap, fmt);

    if (level <= loglvl)
    {
        pthread_mutex_lock(&m_log_print);

        now = time(NULL);
        if (!(ctim = strdup(ctime(&now))))
        {
            pthread_mutex_lock(&m_log_print);
            va_end(ap);
            return;
        }

        /* remove \n */
        ctim[strlen(ctim)-1] = '\0';

        fprintf(logf, "%s %s: %s %s\t", ctim, log_lvlstr[level], where, func);

        vfprintf(logf, fmt, ap);
        fprintf(logf, "\n");

        fflush(logf);

        free(ctim);
        pthread_mutex_unlock(&m_log_print);
    }

    va_end(ap);
}
#else
void log_print(int level, const char *where, const char *func, const char *fmt, ...)
{
    return;
}
#endif
