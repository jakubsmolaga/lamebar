#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

static void
logv(const char *level, const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", level);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

void log_info(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	logv("INFO", fmt, args);
	va_end(args);
}

void log_debug(const char *fmt, ...) {
#ifndef NDEBUG
	va_list args;
	va_start(args, fmt);
	logv("DEBUG", fmt, args);
	va_end(args);
#endif
}

__attribute__((noreturn))
void log_crash(const char *fmt, ...) {
	if (errno != 0) perror("SYSTEM ERROR");
	va_list args;
	va_start(args, fmt);
	logv("ERROR", fmt, args);
	va_end(args);
	exit(1);
}

void log_assert(bool cond, const char *fmt, ...) {
	if (!cond) {
		va_list args;
		va_start(args, fmt);
		log_crash(fmt, args);
	}
}

