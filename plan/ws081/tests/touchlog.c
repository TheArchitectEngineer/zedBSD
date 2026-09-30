/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws081-p016: records a touch screen's reports for a while and says how
 * regular they were (from plan/ws084/tests/evlat.c).  Each report (a
 * SYN_REPORT) is one line of the raw log: when it arrived (CLOCK_MONOTONIC),
 * the kernel's stamp, the screen's own scan time (MSC_TIMESTAMP, -1 for a
 * screen without it) and how many fingers were down.  At the end it prints
 * the summary: the reports while fingers were down, their rate, the
 * intervals (median, 95th percentile, longest) by the scan time and by the
 * arrival, and the gaps (an interval longer than 1.5 times the median: a
 * report lost or late).
 *
 *   touchlog [--device=/dev/input/eventN] [--seconds=N] [--raw=PATH]
 *
 * Without --device it takes the first device with multitouch positions.
 * It stops after the seconds (default 15) or at SIGINT or SIGTERM.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <uapi/input.h>

/* The most reports one run keeps, the most fingers followed, and the input devices looked at. */
#define LOG_REPORTS		100000
#define LOG_SLOTS		16
#define LOG_DEVICES		32

/* One report: when it arrived and was stamped (microseconds), its scan time (-1 without), and the fingers down after it. */
struct report {
	int64_t arrival_us;
	int64_t stamp_us;
	int64_t scan_us;
	int fingers;
};

/* The reports of the run. */
static struct report log_reports[LOG_REPORTS];
static size_t log_count;

/* Set by SIGINT or SIGTERM. */
static volatile sig_atomic_t log_stop;

static int64_t log_now_us(void);
static void log_signal(int number);
static int log_find(char *path, size_t size);
static int log_is_touch(int descriptor);
static void log_summary(const char *device);
static void log_intervals(const char *what, int64_t *intervals, size_t count, int64_t *median);
static int log_compare(const void *left, const void *right);

/*
 * Runs the record and prints the summary.
 */
int
main(
	int argc,
	char **argv)
{
	struct input_event events[64];
	struct pollfd entry;
	char path[64];
	const char *device;
	const char *raw_path;
	FILE *raw;
	int64_t started;
	int64_t now;
	int64_t scan;
	int slots[LOG_SLOTS];
	int slot;
	int fingers;
	int seconds;
	int descriptor;
	int index;
	int ready;
	int found;
	int match;
	ssize_t got;

	/* The options. */
	device = NULL;
	raw_path = NULL;
	seconds = 15;
	for (index = 1; index < argc; index++) {
		/* The device. */
		match = strncmp(argv[index], "--device=", 9);
		if (match == 0) {
			device = argv[index] + 9;
			continue;
		}

		/* The time to record. */
		match = strncmp(argv[index], "--seconds=", 10);
		if (match == 0) {
			seconds = atoi(argv[index] + 10);
			continue;
		}

		/* The raw log. */
		match = strncmp(argv[index], "--raw=", 6);
		if (match == 0) {
			raw_path = argv[index] + 6;
			continue;
		}

		/* Anything else. */
		fprintf(stderr, "usage: touchlog [--device=/dev/input/eventN] [--seconds=N] [--raw=PATH]\n");
		return 2;
	}

	/* The touch screen: the one named, or the first with multitouch positions. */
	if (device == NULL) {
		found = log_find(path, sizeof(path));
		if (!found) {
			fprintf(stderr, "touchlog: no touch screen\n");
			return 1;
		}

		/* The one found. */
		device = path;
	}

	/* Opened. */
	descriptor = open(device, O_RDONLY);
	if (descriptor < 0) {
		fprintf(stderr, "touchlog: %s: %s\n", device, strerror(errno));
		return 1;
	}

	/* The raw log, when asked for. */
	raw = NULL;
	if (raw_path != NULL) {
		raw = fopen(raw_path, "w");
		if (raw == NULL) {
			fprintf(stderr, "touchlog: %s: %s\n", raw_path, strerror(errno));
			close(descriptor);
			return 1;
		}

		/* Its header. */
		fprintf(raw, "# arrival_us stamp_us scan_us fingers\n");
	}

	/* Stops at the end of the time or at a signal. */
	signal(SIGINT, log_signal);
	signal(SIGTERM, log_signal);
	printf("touchlog: recording %s for %d seconds; drag a finger now\n", device, seconds);
	fflush(stdout);

	/* Each event until the end: a report closes at SYN_REPORT. */
	for (slot = 0; slot < LOG_SLOTS; slot++)
		slots[slot] = -1;
	slot = 0;
	scan = -1;
	started = log_now_us();
	for (;;) {
		/* Until a signal or the end of the time. */
		now = log_now_us();
		if (log_stop || now - started >= (int64_t)seconds * 1000000)
			break;

		/* The events that came. */
		entry.fd = descriptor;
		entry.events = POLLIN;
		entry.revents = 0;
		ready = poll(&entry, 1, 100);
		if (ready <= 0)
			continue;
		got = read(descriptor, events, sizeof(events));
		if (got <= 0)
			break;

		/* The events read. */
		for (index = 0; index < (int)(got / (ssize_t)sizeof(events[0])); index++) {
			/* The slot and the finger in it. */
			if (events[index].type == EV_ABS && events[index].code == ABS_MT_SLOT && events[index].value >= 0 && events[index].value < LOG_SLOTS)
				slot = events[index].value;
			if (events[index].type == EV_ABS && events[index].code == ABS_MT_TRACKING_ID)
				slots[slot] = events[index].value;

			/* The screen's scan time of this report. */
			if (events[index].type == EV_MSC && events[index].code == MSC_TIMESTAMP)
				scan = (int64_t)(uint32_t)events[index].value;

			/* The report ends: kept with the fingers down after it. */
			if (events[index].type != EV_SYN || events[index].code != SYN_REPORT)
				continue;
			fingers = 0;
			for (found = 0; found < LOG_SLOTS; found++) {
				if (slots[found] >= 0)
					fingers++;
			}

			/* Kept while there is room. */
			if (log_count < LOG_REPORTS) {
				log_reports[log_count].arrival_us = log_now_us();
				log_reports[log_count].stamp_us = (int64_t)events[index].time.tv_sec * 1000000 + events[index].time.tv_usec;
				log_reports[log_count].scan_us = scan;
				log_reports[log_count].fingers = fingers;
				if (raw != NULL)
					fprintf(raw, "%lld %lld %lld %d\n", (long long)log_reports[log_count].arrival_us, (long long)log_reports[log_count].stamp_us, (long long)scan, fingers);
				log_count++;
			}

			/* The next report's scan time is its own. */
			scan = -1;
		}
	}

	/* The summary. */
	close(descriptor);
	if (raw != NULL)
		fclose(raw);
	log_summary(device);
	return 0;
}

/* Reports the monotonic clock in microseconds. */
static int64_t
log_now_us(void)
{
	struct timespec now;

	/* The clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

/* Asks the record to stop. */
static void
log_signal(
	int number)
{
	/* At the next event or poll. */
	(void)number;
	log_stop = 1;
}

/* Finds the first input device with multitouch positions; 1 with its path. */
static int
log_find(
	char *path,
	size_t size)
{
	int descriptor;
	int index;
	int touch;

	/* Each event node in turn. */
	for (index = 0; index < LOG_DEVICES; index++) {
		snprintf(path, size, "/dev/input/event%d", index);
		descriptor = open(path, O_RDONLY);
		if (descriptor < 0)
			continue;
		touch = log_is_touch(descriptor);
		close(descriptor);
		if (touch)
			return 1;
	}

	/* None. */
	return 0;
}

/* Tells whether a device reports multitouch positions. */
static int
log_is_touch(
	int descriptor)
{
	unsigned char bits[(ABS_MAX + 8) / 8];
	int error;

	/* Its absolute axes. */
	memset(bits, 0, sizeof(bits));
	error = ioctl(descriptor, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
	if (error < 0)
		return 0;

	/* The multitouch X position among them. */
	if ((bits[ABS_MT_POSITION_X / 8] & (1U << (ABS_MT_POSITION_X % 8))) != 0)
		return 1;
	return 0;
}

/* Prints the summary of the reports while fingers were down. */
static void
log_summary(
	const char *device)
{
	static int64_t by_scan[LOG_REPORTS];
	static int64_t by_arrival[LOG_REPORTS];
	int64_t median;
	int64_t first;
	int64_t last;
	size_t scans;
	size_t arrivals;
	size_t touching;
	size_t gaps;
	size_t index;
	double seconds;
	double rate;

	/* The intervals between reports that both had fingers down (a report with none ends a stroke). */
	scans = 0;
	arrivals = 0;
	touching = 0;
	first = -1;
	last = -1;
	for (index = 0; index < log_count; index++) {
		if (log_reports[index].fingers == 0)
			continue;
		touching++;
		if (first < 0)
			first = log_reports[index].arrival_us;
		last = log_reports[index].arrival_us;
		if (index == 0 || log_reports[index - 1].fingers == 0)
			continue;
		by_arrival[arrivals++] = log_reports[index].arrival_us - log_reports[index - 1].arrival_us;
		if (log_reports[index].scan_us >= 0 && log_reports[index - 1].scan_us >= 0 && log_reports[index].scan_us > log_reports[index - 1].scan_us)
			by_scan[scans++] = log_reports[index].scan_us - log_reports[index - 1].scan_us;
	}

	/* The reports and their rate. */
	printf("SUMMARY device=%s reports=%lu touching=%lu\n", device, (unsigned long)log_count, (unsigned long)touching);
	seconds = 0.0;
	if (last > first)
		seconds = (double)(last - first) / 1000000.0;
	rate = 0.0;
	if (seconds > 0.0)
		rate = (double)(touching - 1U) / seconds;
	printf("SUMMARY touching_seconds=%.2f rate_hz=%.1f\n", seconds, rate);

	/* The intervals by the screen's scan time (what the screen sent) and by the arrival (what reached Kei). */
	median = 0;
	log_intervals("scan", by_scan, scans, &median);
	gaps = 0;
	for (index = 0; index < scans && median > 0; index++) {
		if (by_scan[index] * 2 > median * 3)
			gaps++;
	}

	/* The gaps by the scan time. */
	if (scans > 0)
		printf("SUMMARY scan_gaps=%lu (intervals over 1.5 x the median)\n", (unsigned long)gaps);
	log_intervals("arrival", by_arrival, arrivals, &median);
	gaps = 0;
	for (index = 0; index < arrivals && median > 0; index++) {
		if (by_arrival[index] * 2 > median * 3)
			gaps++;
	}

	/* The gaps by the arrival. */
	printf("SUMMARY arrival_gaps=%lu (intervals over 1.5 x the median)\n", (unsigned long)gaps);
}

/* Prints the median, the 95th percentile and the longest of some intervals (sorted in place), and gives the median. */
static void
log_intervals(
	const char *what,
	int64_t *intervals,
	size_t count,
	int64_t *median)
{
	/* None: nothing to say. */
	if (count == 0) {
		printf("SUMMARY %s_intervals=0\n", what);
		*median = 0;
		return;
	}

	/* Sorted: the median, the 95th percentile and the longest, in milliseconds. */
	qsort(intervals, count, sizeof(intervals[0]), log_compare);
	*median = intervals[count / 2];
	printf("SUMMARY %s_intervals=%lu median_ms=%.2f p95_ms=%.2f max_ms=%.2f\n", what, (unsigned long)count,
	       (double)intervals[count / 2] / 1000.0, (double)intervals[count * 95 / 100] / 1000.0, (double)intervals[count - 1] / 1000.0);
}

/* Orders two intervals. */
static int
log_compare(
	const void *left,
	const void *right)
{
	int64_t a;
	int64_t b;

	/* By their length. */
	a = *(const int64_t *)left;
	b = *(const int64_t *)right;
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}
