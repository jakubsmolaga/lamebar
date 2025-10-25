#pragma once
#include "base.h"

void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);
__attribute__((noreturn))
void log_crash(const char *fmt, ...);
void log_assert(bool cond, const char *fmt, ...);
