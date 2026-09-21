/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef LIBC_SYS_TIME_H
#define LIBC_SYS_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/types.h>
#include <time.h>

struct timeval {
	time_t tv_sec;
	suseconds_t tv_usec;
};

struct itimerval { struct timeval it_interval, it_value; };

/*
 * Arithmetic on a time of day.  These are macros rather than calls because
 * they are used in loops that poll, where the comparison is the work.
 */
#define timerclear(t)	((t)->tv_sec = (t)->tv_usec = 0)
#define timerisset(t)	((t)->tv_sec != 0 || (t)->tv_usec != 0)
#define timercmp(a, b, op)						\
	(((a)->tv_sec == (b)->tv_sec) ?					\
	 ((a)->tv_usec op (b)->tv_usec) : ((a)->tv_sec op (b)->tv_sec))
#define timeradd(a, b, out) do {					\
	(out)->tv_sec = (a)->tv_sec + (b)->tv_sec;			\
	(out)->tv_usec = (a)->tv_usec + (b)->tv_usec;			\
	if ((out)->tv_usec >= 1000000) {				\
		(out)->tv_sec++;					\
		(out)->tv_usec -= 1000000;				\
	}								\
} while (0)
#define timersub(a, b, out) do {					\
	(out)->tv_sec = (a)->tv_sec - (b)->tv_sec;			\
	(out)->tv_usec = (a)->tv_usec - (b)->tv_usec;			\
	if ((out)->tv_usec < 0) {					\
		(out)->tv_sec--;					\
		(out)->tv_usec += 1000000;				\
	}								\
} while (0)
#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2

int utimes(const char *, const struct timeval [2]);
#if __ZEDBSD_LEGACY_VISIBLE
int gettimeofday(struct timeval *, void *);
int getitimer(int, struct itimerval *);
int setitimer(int, const struct itimerval *, struct itimerval *);
#endif

#ifdef __cplusplus
}
#endif

#endif
