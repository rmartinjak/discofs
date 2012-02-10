#ifndef FS2GO_LOG_H
#define FS2GO_LOG_H

#include "funcs.h"

enum log_levels {
	LOG_NONE,
	LOG_ERROR,
	LOG_INFO,
	LOG_VERBOSE,
	LOG_FSOP,
	LOG_DEBUG
};

void log_init(int level, const char *path);
void log_destroy();
void log_error(const char *s);
void log_print(int level, const char *fmt, ...);

#define WHERE __FILE__ ":" STR(__LINE__) "\t"

#define PERROR(msg) log_error(WHERE msg)
#define ERROR(...) log_print(LOG_ERROR, WHERE __VA_ARGS__)
#define INFO(...) log_print(LOG_INFO, WHERE __VA_ARGS__)
#define VERBOSE(...) log_print(LOG_VERBOSE, WHERE __VA_ARGS__)
#define FSOP(...) log_print(LOG_FSOP, WHERE __VA_ARGS__)
#define DEBUG(...) log_print(LOG_DEBUG, WHERE __VA_ARGS__)
#define DEBUGTIME(msg) { struct timespec debug_tp; clock_gettime(CLOCK_REALTIME, &debug_tp); DEBUG(msg ": %ld, %ld\n", debug_tp.tv_sec, debug_tp.tv_nsec); }
#define TIMEDCALL(call) { \
			struct timespec before, after; long long td; \
			clock_gettime(CLOCK_REALTIME, &before); \
			call; \
			clock_gettime(CLOCK_REALTIME, &after); \
			td = (after.tv_sec - before.tv_sec) * 1000000000 + after.tv_nsec - before.tv_nsec; \
			DEBUG(STR(call) ": %lld\n", td); \
			}

#endif
