/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libkeiland's touch motion (ws081-p003,
 * userland/desktop/libkeiland/motion.c, plan/ws081/design.md section 7).
 *
 * It includes the design's comparison program (motion-compare.c) to reuse
 * its simulated panels, paths and measures, and draws with the library as
 * one more method.  The library must measure as well as the design's
 * chosen method on the same reports, give exact points on exact motion,
 * estimate flick velocities as the design's estimator does, and handle the
 * awkward inputs: gaps, bursts, reports going back, a slow poll's aliases,
 * and panels whose Scan Time drifts, wraps, restarts, stands still or has
 * the wrong unit.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>

#define main motion_compare_main
#include "motion-compare.c"
#undef main

#include <keiland.h>

/* A microsecond count the simulated times are offset by, so that none is zero. */
#define TIME_BASE	1000000U

/* The number of checks run, and of those that failed. */
static unsigned checks;
static unsigned failures;

/* The library's objects the adapter draws with, and the stroke it has fed them. */
static struct kl_motion_device *adapter_device;
static struct kl_motion *adapter_motion;
static const struct report *adapter_reports;
static double adapter_first_stamp;
static int adapter_fed;

static void check(int passed, const char *format, ...);
static uint64_t microseconds(double seconds);
static void adapter_start(void);
static void adapter_stop(void);
static void library_point(const struct report *reports, int count, double now, double *x, double *y);
static const struct method *method_named(const char *name);
static void measures(const struct metrics *metrics, double *judder, double *lag, double *overshoot, double *retreat);
static void test_exact(void);
static void test_edges(void);
static void test_resampling(void);
static void test_gaps(void);
static void test_velocity(void);
static void test_rate(void);
static void test_noise(void);
static void test_clock(void);

/*
 * Runs every test and reports the result.
 */
int
main(
	void)
{
	/* The library's own checks first, then against the design's measures. */
	test_exact();
	test_edges();
	test_rate();
	test_clock();
	test_noise();
	test_resampling();
	test_gaps();
	test_velocity();

	/* Reports the outcome. */
	if (failures != 0) {
		printf("host-motion: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}
	printf("host-motion: ok (%u checks)\n", checks);
	return 0;
}

/* Counts one check and prints it when it fails. */
static void
check(
	int passed,
	const char *format,
	...)
{
	va_list arguments;

	checks++;
	if (passed)
		return;
	failures++;
	printf("FAIL: ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* Turns a simulated time in seconds into the library's microseconds. */
static uint64_t
microseconds(
	double seconds)
{
	return (uint64_t)llround(seconds * 1.0e6) + TIME_BASE;
}

/* Gives the adapter a fresh device and motion. */
static void
adapter_start(
	void)
{
	adapter_device = kl_motion_device_create();
	adapter_motion = kl_motion_create(adapter_device);
	adapter_reports = NULL;
	adapter_first_stamp = -1.0;
	adapter_fed = 0;
}

/* Ends the adapter's stroke and frees its objects. */
static void
adapter_stop(
	void)
{
	if (adapter_fed > 0)
		kl_motion_end(adapter_motion);
	kl_motion_destroy(adapter_motion);
	kl_motion_device_destroy(adapter_device);
	adapter_motion = NULL;
	adapter_device = NULL;
}

/*
 * Draws with the library: feeds it the reports it has not seen yet and
 * asks for the point.  Another stroke (fewer reports, or another first
 * report) ends the last one, which teaches the device, as a lift would.
 */
static void
library_point(
	const struct report *reports,
	int count,
	double now,
	double *x,
	double *y)
{
	int error;

	/* A new stroke. */
	if (reports != adapter_reports || count < adapter_fed || reports[0].stamp != adapter_first_stamp) {
		if (adapter_fed > 0)
			kl_motion_end(adapter_motion);
		kl_motion_begin(adapter_motion);
		adapter_reports = reports;
		adapter_first_stamp = reports[0].stamp;
		adapter_fed = 0;
	}

	/* The reports it has not seen. */
	while (adapter_fed < count) {
		error = kl_motion_add(adapter_motion, microseconds(reports[adapter_fed].stamp),
					   microseconds(reports[adapter_fed].arrival), reports[adapter_fed].x,
					   reports[adapter_fed].y);
		check(error == 0, "adapter add error %d", error);
		adapter_fed++;
	}

	/* The point at the frame. */
	error = kl_motion_point(adapter_motion, microseconds(now), KL_MOTION_EXTRAPOLATION_CONTENT, x, y);
	check(error == 0, "adapter point error %d", error);
}

/* Finds one of the comparison's methods by name. */
static const struct method *
method_named(
	const char *name)
{
	int m;

	for (m = 0; m < METHOD_COUNT; m++) {
		if (strcmp(methods[m].name, name) == 0)
			return &methods[m];
	}
	printf("no method %s\n", name);
	exit(2);
}

/* Turns a cell's sums into the measures the tables print. */
static void
measures(
	const struct metrics *metrics,
	double *judder,
	double *lag,
	double *overshoot,
	double *retreat)
{
	*judder = 100.0 * sqrt(metrics->judder_sum / metrics->judder_count) / (1500.0 * FRAME_PERIOD);
	*lag = 1000.0 * metrics->lag_sum / metrics->lag_count;
	*overshoot = metrics->overshoot_sum / metrics->overshoot_count;
	*retreat = metrics->retreat_sum / metrics->retreat_count;
}

/*
 * Exact motion gives exact points: a steady line is drawn on the line at
 * the time the design's formula says, and a decelerating parabola is drawn
 * on the parabola both behind and past the last report.
 */
static void
test_exact(
	void)
{
	struct kl_motion_device *device;
	struct kl_motion *motion;
	uint64_t stamp;
	uint64_t now;
	double t;
	double x;
	double y;
	double expected;
	int k;
	int error;

	device = kl_motion_device_create();
	motion = kl_motion_create(device);
	check(device != NULL && motion != NULL, "create");

	/* A steady line: 1500 px/s along x, 700 along y, every 16 ms, read 2 ms later. */
	kl_motion_begin(motion);
	for (k = 0; k < 12; k++) {
		stamp = TIME_BASE + (uint64_t)k * 16000U;
		t = (double)(stamp - TIME_BASE) / 1.0e6;
		error = kl_motion_add(motion, stamp, stamp + 2000U, 1500.0 * t, 700.0 * t);
		check(error == 0, "line add %d", error);
	}

	/*
	 * Behind is 16 + 2 - 12 = 6 ms: at 10 ms after the last report the
	 * point is the line at 4 ms after it.
	 */
	now = stamp + 10000U;
	error = kl_motion_point(motion, now, KL_MOTION_EXTRAPOLATION_CONTENT, &x, &y);
	t = (double)(stamp + 4000U - TIME_BASE) / 1.0e6;
	check(error == 0 && fabs(x - 1500.0 * t) < 1e-6 && fabs(y - 700.0 * t) < 1e-6,
	      "line point %.9f,%.9f wanted %.9f,%.9f", x, y, 1500.0 * t, 700.0 * t);

	/* Far past the last report the point stops at the extrapolation limit (12 + 8 ms). */
	error = kl_motion_point(motion, stamp + 200000U, KL_MOTION_EXTRAPOLATION_CONTENT, &x, &y);
	t = (double)(stamp + 20000U - TIME_BASE) / 1.0e6;
	check(error == 0 && fabs(x - 1500.0 * t) < 1e-6, "line limit %.9f wanted %.9f", x, 1500.0 * t);

	/* A finger slowing down: x = 2000 t - 3000 t^2 (it stops at t = 1/3 s). */
	kl_motion_begin(motion);
	for (k = 0; k < 12; k++) {
		stamp = TIME_BASE + (uint64_t)k * 16000U;
		t = (double)(stamp - TIME_BASE) / 1.0e6;
		error = kl_motion_add(motion, stamp, stamp + 2000U, 2000.0 * t - 3000.0 * t * t, 0.0);
		check(error == 0, "parabola add %d", error);
	}

	/* Behind the last report (4 ms after it minus 6: 2 ms before it) the parabola is exact. */
	error = kl_motion_point(motion, stamp + 4000U, KL_MOTION_EXTRAPOLATION_CONTENT, &x, &y);
	t = (double)(stamp - 2000U - TIME_BASE) / 1.0e6;
	expected = 2000.0 * t - 3000.0 * t * t;
	check(error == 0 && fabs(x - expected) < 1e-6, "parabola behind %.9f wanted %.9f", x, expected);

	/* Past it, the slowing parabola goes less far than the line and is chosen: exact again. */
	error = kl_motion_point(motion, stamp + 14000U, KL_MOTION_EXTRAPOLATION_CONTENT, &x, &y);
	t = (double)(stamp + 8000U - TIME_BASE) / 1.0e6;
	expected = 2000.0 * t - 3000.0 * t * t;
	check(error == 0 && fabs(x - expected) < 1e-6, "parabola past %.9f wanted %.9f", x, expected);

	kl_motion_destroy(motion);
	kl_motion_device_destroy(device);
}

/*
 * The awkward inputs: no report, one report, a report going back, reports
 * at one time, a second of silence, a missing object, and the time behind
 * the frame moving slowly once settled.
 */
static void
test_edges(
	void)
{
	struct kl_motion_device *device;
	struct kl_motion *motion;
	uint64_t stamp;
	double x;
	double y;
	double first_x;
	double vx;
	double vy;
	int k;
	int error;

	device = kl_motion_device_create();
	motion = kl_motion_create(device);

	/* Nothing to draw before a report; a single report is drawn where it is. */
	kl_motion_begin(motion);
	error = kl_motion_point(motion, TIME_BASE, 12000U, &x, &y);
	check(error == ENOENT, "no report gives ENOENT, not %d", error);
	(void)kl_motion_add(motion, TIME_BASE, TIME_BASE + 1000U, 10.0, 20.0);
	error = kl_motion_point(motion, TIME_BASE + 50000U, 12000U, &x, &y);
	check(error == 0 && x == 10.0 && y == 20.0, "one report %.3f,%.3f", x, y);
	error = kl_motion_velocity(motion, TIME_BASE + 5000U, &vx, &vy);
	check(error == 0 && vx == 0.0 && vy == 0.0, "one report has no velocity");

	/* A report going back is refused. */
	error = kl_motion_add(motion, TIME_BASE - 1U, TIME_BASE, 0.0, 0.0);
	check(error == EINVAL, "report going back gives EINVAL, not %d", error);

	/* Reports at one time (a burst) give a finite point. */
	for (k = 0; k < 4; k++)
		(void)kl_motion_add(motion, TIME_BASE + 8000U, TIME_BASE + 9000U, 10.0 + k, 20.0);
	error = kl_motion_point(motion, TIME_BASE + 20000U, 12000U, &x, &y);
	check(error == 0 && isfinite(x) && isfinite(y), "burst point %.3f,%.3f", x, y);
	error = kl_motion_velocity(motion, TIME_BASE + 20000U, &vx, &vy);
	check(error == 0 && isfinite(vx) && isfinite(vy), "burst velocity %.3f,%.3f", vx, vy);

	/* After a second of silence the older reports are forgotten. */
	(void)kl_motion_add(motion, TIME_BASE + 1500000U, TIME_BASE + 1501000U, 500.0, 600.0);
	error = kl_motion_point(motion, TIME_BASE + 1520000U, 12000U, &x, &y);
	check(error == 0 && x == 500.0 && y == 600.0, "silence forgets %.3f,%.3f", x, y);

	/* Missing objects are refused. */
	check(kl_motion_add(NULL, 0, 0, 0.0, 0.0) == EINVAL, "add without motion");
	check(kl_motion_point(NULL, 0, 0, &x, &y) == EINVAL, "point without motion");
	check(kl_motion_velocity(motion, 0, NULL, &vy) == EINVAL, "velocity without result");
	check(kl_motion_create(NULL) == NULL, "motion without device");

	/*
	 * Once settled, the time behind the frame moves at most 0.5 ms a call:
	 * a steady line whose delay jumps from 2 to 12 ms (behind 6 → 16 ms).
	 */
	kl_motion_begin(motion);
	for (k = 0; k < 10; k++) {
		stamp = TIME_BASE + (uint64_t)k * 16000U;
		(void)kl_motion_add(motion, stamp, stamp + 2000U, 1.5 * (double)(stamp - TIME_BASE) / 1000.0, 0.0);
	}
	(void)kl_motion_point(motion, stamp + 8000U, 12000U, &first_x, &y);
	for (k = 10; k < 20; k++) {
		stamp = TIME_BASE + (uint64_t)k * 16000U;
		(void)kl_motion_add(motion, stamp, stamp + 12000U, 1.5 * (double)(stamp - TIME_BASE) / 1000.0, 0.0);
	}
	(void)kl_motion_point(motion, stamp + 8000U, 12000U, &first_x, &y);
	(void)kl_motion_point(motion, stamp + 8000U, 12000U, &x, &y);

	/*
	 * The line is 1.5 px a millisecond: drawn 8 ms after the last report,
	 * the first call is 6.5 ms behind (1.5 ms past the report), the second
	 * 7 ms behind (1 ms past it).
	 */
	check(fabs(first_x - 1.5 * ((double)(stamp - TIME_BASE) / 1000.0 + 1.5)) < 1e-6, "behind first step %.6f px",
	      first_x - 1.5 * (double)(stamp - TIME_BASE) / 1000.0);
	check(fabs(x - 1.5 * ((double)(stamp - TIME_BASE) / 1000.0 + 1.0)) < 1e-6, "behind second step %.6f px",
	      x - 1.5 * (double)(stamp - TIME_BASE) / 1000.0);

	kl_motion_destroy(motion);
	kl_motion_device_destroy(device);
}

/*
 * The period is the mean of the intervals without gaps: a 90 Hz panel
 * read every 8 ms (intervals of 8 and 16 ms) measures 11.1 ms, and 5% of
 * lost reports lengthen it by about 5%.
 */
static void
test_rate(
	void)
{
	static const struct condition alias = {"alias", 0.0, 0.0, 0.0, STAMP_COMPLETION, 0.008, 1, 0};
	static const struct condition lossy = {"lossy", 0.10, 0.05, 1.0, STAMP_COMPLETION, 0.001, 1, 0};
	static struct report reports[SAMPLES_MAX];
	struct kl_motion_device *device;
	struct kl_motion *motion;
	struct rng rng;
	double interval;
	int count;
	int k;
	int run;

	/* The slow poll's alias. */
	device = kl_motion_device_create();
	motion = kl_motion_create(device);
	rng.state = 0x1234567ULL;
	count = generate(&alias, 90.0, PATH_LINE, &rng, reports);
	kl_motion_begin(motion);
	for (k = 0; k < count; k++)
		(void)kl_motion_add(motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
	kl_motion_end(motion);
	interval = (double)kl_motion_device_interval(device);
	check(fabs(interval / 11111.0 - 1.0) < 0.15, "90 Hz read every 8 ms measures %.0f us", interval);
	kl_motion_destroy(motion);
	kl_motion_device_destroy(device);

	/* Lost reports, over many strokes. */
	device = kl_motion_device_create();
	motion = kl_motion_create(device);
	for (run = 0; run < 16; run++) {
		rng.state = 0x7654321ULL + (unsigned long long)run;
		count = generate(&lossy, 60.0, PATH_LINE, &rng, reports);
		kl_motion_begin(motion);
		for (k = 0; k < count; k++)
			(void)kl_motion_add(motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
		kl_motion_end(motion);
	}
	interval = (double)kl_motion_device_interval(device);
	check(interval / 16667.0 - 1.0 > -0.03 && interval / 16667.0 - 1.0 < 0.10, "60 Hz with 5%% lost measures %.0f us", interval);
	kl_motion_destroy(motion);
	kl_motion_device_destroy(device);
}

/*
 * The Scan Time mapping: a true panel maps onto its scan times plus the
 * least delay, through drift, a wrap and a restart; a Scan Time that
 * stands still or runs ten times too fast is not trusted.
 */
static void
test_clock(
	void)
{
	struct kl_motion_device *device;
	struct rng rng;
	uint64_t host;
	uint64_t stamp;
	uint64_t last;
	uint64_t scan_host;
	uint32_t raw;
	double scan;
	double rate;
	double worst;
	int bad_order;
	int bad_future;
	int k;
	int variant;
	int error;

	/* 0: a true clock, 1: 200 ppm fast, 2: 200 ppm slow, 3: a wrap, 4: a restart after a pause. */
	for (variant = 0; variant < 5; variant++) {
		device = kl_motion_device_create();
		rng.state = 0xabcdef12ULL + (unsigned long long)variant;
		rate = 1.0;
		if (variant == 1)
			rate = 1.0002;
		if (variant == 2)
			rate = 0.9998;
		worst = 0.0;
		bad_order = 0;
		bad_future = 0;
		last = 0;
		for (k = 0; k < 600; k++) {
			/* 60 Hz scans; after 300 a pause of 1.2 s in the restart variant. */
			scan = (double)k / 60.0;
			if (variant == 4 && k >= 300)
				scan += 1.2;

			/* The panel's clock, and the host time 0.3 to 1.3 ms after the scan. */
			raw = (uint32_t)llround(scan * 1.0e6 * rate);
			if (variant == 3)
				raw += 0xffffffffU - 2000000U;
			if (variant == 4 && k >= 300)
				raw = (uint32_t)llround((scan - 5.0) * 1.0e6);
			scan_host = TIME_BASE + (uint64_t)llround(scan * 1.0e6);
			host = scan_host + 300U + (uint64_t)llround(1000.0 * rng_uniform(&rng));
			error = kl_motion_device_time(device, host, raw, &stamp);
			check(error == 0, "clock error %d", error);

			/* Never later than the host, never back. */
			if (stamp > host)
				bad_future++;
			if (stamp < last)
				bad_order++;
			last = stamp;

			/* After the first second (trusted), close to the scan plus the least delay. */
			if (k >= 60 && (variant != 4 || k >= 360)) {
				if (fabs((double)stamp - (double)(scan_host + 300U)) > worst)
					worst = fabs((double)stamp - (double)(scan_host + 300U));
			}
		}
		check(bad_future == 0 && bad_order == 0, "clock variant %d: %d later than host, %d back", variant, bad_future, bad_order);
		check(worst < 400.0, "clock variant %d: %.0f us from the scan", variant, worst);
		kl_motion_device_destroy(device);
	}

	/* 0: a Scan Time standing still, 1: one ten times too fast: the host time is used. */
	for (variant = 0; variant < 2; variant++) {
		device = kl_motion_device_create();
		worst = 0.0;
		for (k = 0; k < 300; k++) {
			host = TIME_BASE + (uint64_t)k * 16667U + 700U;
			raw = 12345U;
			if (variant == 1)
				raw = (uint32_t)k * 166670U;
			(void)kl_motion_device_time(device, host, raw, &stamp);
			if (fabs((double)stamp - (double)host) > worst)
				worst = fabs((double)stamp - (double)host);
		}
		check(worst == 0.0, "untrusted Scan Time variant %d strays %.0f us from the host", variant, worst);
		kl_motion_device_destroy(device);
	}
}

/*
 * The device's noise: after sixteen strokes of mixed paths (a scroll, a
 * circle, a stop, back and forth) it is within 30% of the panel's, and
 * the stops and turns do not make a clean panel look noisy.
 */
static void
test_noise(
	void)
{
	static struct report reports[SAMPLES_MAX];
	static const double levels[] = {0.0, 1.0, 2.5};
	struct condition condition;
	struct kl_motion_device *device;
	struct kl_motion *motion;
	struct rng rng;
	double noise;
	size_t l;
	int run;
	int count;
	int k;

	for (l = 0; l < sizeof(levels) / sizeof(levels[0]); l++) {
		condition = conditions[2];
		condition.noise = levels[l];
		device = kl_motion_device_create();
		motion = kl_motion_create(device);
		for (run = 0; run < 16; run++) {
			rng.state = 0x5eed0000ULL + (unsigned long long)(run * 31 + (int)l);
			count = generate(&condition, 60.0, (enum path)(PATH_LINE + run % 4), &rng, reports);
			kl_motion_begin(motion);
			for (k = 0; k < count; k++)
				(void)kl_motion_add(motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
			kl_motion_end(motion);
		}
		noise = kl_motion_device_noise(device);
		if (levels[l] == 0.0) {
			check(noise < 0.5, "clean panel measures noise %.3f px", noise);
		} else {
			check(fabs(noise / levels[l] - 1.0) < 0.3, "panel of %.1f px measures %.3f px", levels[l], noise);
		}
		printf("noise: panel %.1f px, measured %.3f px\n", levels[l], noise);
		kl_motion_destroy(motion);
		kl_motion_device_destroy(device);
	}
}

/*
 * The library against the design's chosen method on the same reports:
 * judder, overshoot and retreat no worse than 1 point, 0.5 px and 0.5 px,
 * and no more than 0.5 ms later.  The noisy panel is compared with the
 * widened windows the library chooses once it has measured the noise; the
 * device learns the panel from sixteen earlier strokes first, as a device
 * in use has.
 */
static void
test_resampling(
	void)
{
	static const size_t indexes[] = {0, 2, 8};
	static const char *const references[] = {"lsq12g-e12", "lsq12g-e12", "lsq12w-e12"};
	static struct report reports[SAMPLES_MAX];
	struct method library;
	struct metrics mine;
	struct metrics theirs;
	struct rng rng;
	double judder[2];
	double lag[2];
	double overshoot[2];
	double retreat[2];
	size_t c;
	size_t r;
	int run;
	int count;
	int k;

	memset(&library, 0, sizeof(library));
	library.name = "library";
	library.kind = KIND_EXTERNAL;
	library.window_scale = 1.0;
	external_point = library_point;

	printf("%-11s %5s %17s %15s %19s %17s\n", "condition", "rate", "judder lib/ref", "lag lib/ref", "overshoot lib/ref", "retreat lib/ref");
	for (c = 0; c < sizeof(indexes) / sizeof(indexes[0]); c++) {
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			memset(&mine, 0, sizeof(mine));
			memset(&theirs, 0, sizeof(theirs));
			adapter_start();
			for (run = 0; run < 16; run++) {
				rng.state = 0xfeedULL + (unsigned long long)run;
				count = generate(&conditions[indexes[c]], rates[r], (enum path)(PATH_LINE + run % 4), &rng, reports);
				kl_motion_begin(adapter_motion);
				for (k = 0; k < count; k++)
					(void)kl_motion_add(adapter_motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
				kl_motion_end(adapter_motion);
			}
			run_paths(&conditions[indexes[c]], rates[r], &library, &mine);
			adapter_stop();
			run_paths(&conditions[indexes[c]], rates[r], method_named(references[c]), &theirs);
			measures(&mine, &judder[0], &lag[0], &overshoot[0], &retreat[0]);
			measures(&theirs, &judder[1], &lag[1], &overshoot[1], &retreat[1]);
			printf("%-11s %5.0f %7.2f%% /%6.2f%% %6.2f / %6.2f %8.2f / %8.2f %7.2f / %7.2f\n", conditions[indexes[c]].name,
			       rates[r], judder[0], judder[1], lag[0], lag[1], overshoot[0], overshoot[1], retreat[0], retreat[1]);
			check(judder[0] <= judder[1] + 1.0, "%s %.0f Hz judder %.2f against %.2f", conditions[indexes[c]].name, rates[r], judder[0], judder[1]);
			check(lag[0] <= lag[1] + 0.5, "%s %.0f Hz lag %.2f against %.2f", conditions[indexes[c]].name, rates[r], lag[0], lag[1]);
			check(overshoot[0] <= overshoot[1] + 0.5, "%s %.0f Hz overshoot %.2f against %.2f", conditions[indexes[c]].name, rates[r], overshoot[0], overshoot[1]);
			check(retreat[0] <= retreat[1] + 0.5, "%s %.0f Hz retreat %.2f against %.2f", conditions[indexes[c]].name, rates[r], retreat[0], retreat[1]);
		}
	}
	external_point = NULL;
}

/*
 * A start after a rest on a panel that is silent while the finger stays
 * still: the library, which knows the device's period from earlier
 * strokes, starts no later than the design's gap-filling method with the
 * period given.
 */
static void
test_gaps(
	void)
{
	static struct report reports[SAMPLES_MAX];
	static const size_t indexes[] = {2, 8};
	struct condition condition;
	struct method library;
	const struct method *reference;
	const struct method *method;
	struct rng rng;
	double frame;
	double now;
	double phase;
	double x;
	double y;
	double tx;
	double ty;
	double lag;
	double lag_sum[2];
	double lag_max[2];
	double lag_count[2];
	size_t c;
	size_t r;
	int which;
	int run;
	int count;
	int seen;
	int k;

	memset(&library, 0, sizeof(library));
	library.name = "library";
	library.kind = KIND_EXTERNAL;
	library.window_scale = 1.0;
	reference = method_named("lsq12g-e12");
	external_point = library_point;

	for (c = 0; c < sizeof(indexes) / sizeof(indexes[0]); c++) {
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			condition = conditions[indexes[c]];
			for (which = 0; which < 2; which++) {
				lag_sum[which] = 0.0;
				lag_max[which] = 0.0;
				lag_count[which] = 0.0;
			}

			/* The library learns the device's period from a few ordinary strokes. */
			adapter_start();
			for (run = 0; run < 4; run++) {
				rng.state = 0x600dULL + (unsigned long long)run;
				count = generate(&condition, rates[r], PATH_LINE, &rng, reports);
				kl_motion_begin(adapter_motion);
				for (k = 0; k < count; k++)
					(void)kl_motion_add(adapter_motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
				kl_motion_end(adapter_motion);
			}

			/* Then the silent rests. */
			condition.silent = 1;
			device_interval = 1.0 / rates[r];
			for (which = 0; which < 2; which++) {
				method = &library;
				if (which == 1)
					method = reference;
				for (run = 0; run < RUNS; run++) {
					rng.state = 0x452821e638d01377ULL ^ (unsigned long long)(run * 6151 + (int)rates[r]);
					count = generate(&condition, rates[r], PATH_REST_GO, &rng, reports);
					phase = rng_uniform(&rng) * FRAME_PERIOD;
					for (frame = phase; frame <= path_duration(PATH_REST_GO); frame += FRAME_PERIOD) {
						seen = 0;
						while (seen < count && reports[seen].available <= frame)
							seen++;
						if (seen == 0 || frame < 0.4 || frame > 0.6)
							continue;
						now = floor(frame * 1000.0) / 1000.0;
						method_point(method, reports, seen, now, &x, &y);
						path_position(PATH_REST_GO, frame, &tx, &ty);
						lag = (tx - x) / 1500.0;
						lag_sum[which] += lag;
						lag_count[which] += 1.0;
						if (lag > lag_max[which])
							lag_max[which] = lag;
					}
				}
			}
			device_interval = 0.0;
			adapter_stop();

			printf("gap %-11s %5.0f lag lib %.1f/%.1f ms, ref %.1f/%.1f ms\n", condition.name, rates[r],
			       1000.0 * lag_sum[0] / lag_count[0], 1000.0 * lag_max[0], 1000.0 * lag_sum[1] / lag_count[1],
			       1000.0 * lag_max[1]);
			check(lag_sum[0] / lag_count[0] <= lag_sum[1] / lag_count[1] + 0.001, "%s %.0f Hz start lag %.1f against %.1f ms",
			      condition.name, rates[r], 1000.0 * lag_sum[0] / lag_count[0], 1000.0 * lag_sum[1] / lag_count[1]);
			check(lag_max[0] <= lag_max[1] + 0.005, "%s %.0f Hz start lag max %.1f against %.1f ms", condition.name,
			      rates[r], 1000.0 * lag_max[0], 1000.0 * lag_max[1]);
		}
	}
	external_point = NULL;
}

/*
 * The flick velocity against the design's estimator (Kalman with the
 * noise-scaled rest gate) on the same reports: the three flicks' mean
 * errors within 2 points, and a pause before the lift giving no fling
 * (nothing at the 99th percentile on the ordinary panel, under the 300
 * px/s fling threshold on the poor one).
 */
static void
test_velocity(
	void)
{
	static struct report reports[SAMPLES_MAX];
	static double pause[VELOCITY_RUNS];
	static const size_t indexes[] = {2, 8};
	static const enum path flicks[] = {PATH_FLING_FAST, PATH_FLING_SLOW, PATH_FLING_EASE};
	static const double truths[] = {3000.0, 800.0, 2400.0};
	struct kl_motion_device *device;
	struct kl_motion *motion;
	struct rng rng;
	double mine[3];
	double theirs[3];
	double vx;
	double vy;
	double swap;
	size_t c;
	size_t r;
	size_t f;
	int run;
	int count;
	int k;
	int i;
	int j;

	for (c = 0; c < sizeof(indexes) / sizeof(indexes[0]); c++) {
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			/* The device learns the panel from sixteen ordinary strokes first. */
			device = kl_motion_device_create();
			motion = kl_motion_create(device);
			for (run = 0; run < 16; run++) {
				rng.state = 0xfeedULL + (unsigned long long)run;
				count = generate(&conditions[indexes[c]], rates[r], (enum path)(PATH_LINE + run % 4), &rng, reports);
				kl_motion_begin(motion);
				for (k = 0; k < count; k++)
					(void)kl_motion_add(motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
				kl_motion_end(motion);
			}

			/* The flicks and the pause, the same reports for both. */
			for (f = 0; f < 3; f++) {
				mine[f] = 0.0;
				theirs[f] = 0.0;
			}
			device_noise = conditions[indexes[c]].noise;
			for (run = 0; run < VELOCITY_RUNS; run++) {
				rng.state = 0x243f6a8885a308d3ULL ^ (unsigned long long)(run * 104729 + (int)rates[r]);
				for (f = 0; f < 3; f++) {
					count = generate(&conditions[indexes[c]], rates[r], flicks[f], &rng, reports);
					kl_motion_begin(motion);
					for (k = 0; k < count; k++)
						(void)kl_motion_add(motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
					(void)kl_motion_velocity(motion, microseconds(path_duration(flicks[f])), &vx, &vy);
					kl_motion_begin(motion);
					mine[f] += vx / truths[f] - 1.0;
					theirs[f] += estimate_velocity(ESTIMATOR_KALMAN_NOISE_GATED, reports, count) / truths[f] - 1.0;
				}
				count = generate(&conditions[indexes[c]], rates[r], PATH_PAUSE, &rng, reports);
				kl_motion_begin(motion);
				for (k = 0; k < count; k++)
					(void)kl_motion_add(motion, microseconds(reports[k].stamp), microseconds(reports[k].arrival), reports[k].x, reports[k].y);
				(void)kl_motion_velocity(motion, microseconds(path_duration(PATH_PAUSE)), &vx, &vy);
				kl_motion_begin(motion);
				pause[run] = sqrt(vx * vx + vy * vy);
			}
			device_noise = 0.0;

			/* The 99th percentile of the pause. */
			for (i = 1; i < VELOCITY_RUNS; i++) {
				for (j = i; j > 0 && pause[j - 1] > pause[j]; j--) {
					swap = pause[j];
					pause[j] = pause[j - 1];
					pause[j - 1] = swap;
				}
			}

			printf("velocity %-11s %5.0f lib %+.1f%% %+.1f%% %+.1f%% ref %+.1f%% %+.1f%% %+.1f%% pause p99 %.0f\n",
			       conditions[indexes[c]].name, rates[r], 100.0 * mine[0] / VELOCITY_RUNS, 100.0 * mine[1] / VELOCITY_RUNS,
			       100.0 * mine[2] / VELOCITY_RUNS, 100.0 * theirs[0] / VELOCITY_RUNS, 100.0 * theirs[1] / VELOCITY_RUNS,
			       100.0 * theirs[2] / VELOCITY_RUNS, pause[(VELOCITY_RUNS * 99) / 100]);
			for (f = 0; f < 3; f++) {
				check(fabs(mine[f] - theirs[f]) / VELOCITY_RUNS < 0.02, "%s %.0f Hz flick %zu error %.1f%% against %.1f%%",
				      conditions[indexes[c]].name, rates[r], f, 100.0 * mine[f] / VELOCITY_RUNS,
				      100.0 * theirs[f] / VELOCITY_RUNS);
			}
			if (conditions[indexes[c]].noise <= 1.0) {
				check(pause[(VELOCITY_RUNS * 99) / 100] == 0.0, "%s %.0f Hz pause p99 %.0f px/s", conditions[indexes[c]].name,
				      rates[r], pause[(VELOCITY_RUNS * 99) / 100]);
			} else {
				check(pause[(VELOCITY_RUNS * 99) / 100] < 300.0, "%s %.0f Hz pause p99 %.0f px/s", conditions[indexes[c]].name,
				      rates[r], pause[(VELOCITY_RUNS * 99) / 100]);
			}
			kl_motion_destroy(motion);
			kl_motion_device_destroy(device);
		}
	}
}
