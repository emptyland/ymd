#ifndef YMD_JIFFIES_H
#define YMD_JIFFIES_H

#include "builtin.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

#define ymd_jiffy() ((unsigned long long)GetTickCount())

#elif defined(__linux__)
#include "sys/time"

static YMD_INLINE unsigned long long ymd_jiffy() {
	struct timeval jiffx;
	gettimeofday(&jiffx, NULL);
	return jiffx->tv_sec * 1000ULL + jiffx->tv_usec / 1000ULL;
}

#endif

#endif // YMD_JIFFIES_H