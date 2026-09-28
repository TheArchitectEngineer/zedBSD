/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Compares ways to resample, predict and estimate the velocity of touch
 * reports that come at a low or uneven rate (ws081-p001).
 *
 * A synthetic finger follows a known path.  A simulated panel scans it at a
 * rate, with jitter in the scan period, dropped reports and noise in the
 * position.  Each report is stamped the way a kernel would stamp it (the
 * panel's own scan time, the USB completion in microseconds or milliseconds,
 * or the current kernel's millisecond tick in the worker thread) and reaches
 * the consumer after a delay.  At every display frame each method gives the
 * point it would draw, and the program measures, against the true path at
 * that frame, the error, the lag, the judder of a steady motion, the error
 * across a curve, the overshoot after a stop and the steps backwards.  At a
 * lift every velocity estimator gives the fling velocity, which is compared
 * with the true one; a pause before the lift must give none.  A panel that
 * is silent while the finger stays still measures how a method starts after
 * a rest.
 *
 * The program is deterministic (fixed seeds) and prints plain tables.  The
 * design that quotes them is plan/ws081/design.md.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most reports one simulated stroke may produce. */
#define SAMPLES_MAX	1024

/* How many strokes, each with other random phases and noise, one cell averages. */
#define RUNS		64

/* How many strokes one velocity cell takes (enough for a 99th percentile). */
#define VELOCITY_RUNS	1024

/*
 * The host notices a finished transfer up to this long after it happened
 * (the xHCI interrupt moderation of 1 ms, or another device's interrupt).
 */
#define IRQ_JITTER	0.001

/* The display's frame period (60 Hz). */
#define FRAME_PERIOD	(1.0 / 60.0)

/* The consumer reads a report this long after the kernel published it. */
#define DELIVERY	0.001

/* The panel sends a report this long after it scanned. */
#define TRANSFER	0.0003

/* The number of past intervals (and delays) the period and the delay are measured over. */
#define RATE_WINDOW	8

/* The paths the synthetic finger follows. */
enum path {
	PATH_LINE,
	PATH_CIRCLE,
	PATH_STOP,
	PATH_ZIGZAG,
	PATH_FLING_FAST,
	PATH_FLING_SLOW,
	PATH_PAUSE,
	PATH_STROKE,
	PATH_FLING_EASE,
	PATH_REST_GO
};

/* Where a report's time stamp comes from. */
enum stamp {
	STAMP_SCAN,
	STAMP_COMPLETION,
	STAMP_COMPLETION_MS,
	STAMP_SCAN_MS,
	STAMP_TICK
};

/* The kinds of method that choose the point drawn at a frame. */
enum method_kind {
	KIND_HOLD,
	KIND_ANDROID,
	KIND_LERP,
	KIND_LSQ1,
	KIND_LSQ2,
	KIND_KALMAN,
	KIND_LSQ12,
	KIND_ALPHA_BETA,
	KIND_ONE_EURO
};

/*
 * One method: its kind, how far behind the frame it evaluates the stroke
 * (a fraction of the measured interval) and how far past the last report
 * it may extrapolate (a fraction of the interval plus a fixed time, never
 * more than a ceiling).
 */
struct method {
	const char *name;
	enum method_kind kind;
	double behind;
	double reach;
	double reach_extra;
	double reach_ceiling;
	double kalman_q;
	double extrapolation;
	double window_scale;
	int gap_fill;
};

/* The ways to estimate the velocity at a lift. */
enum estimator {
	ESTIMATOR_TWO_POINT,
	ESTIMATOR_LSQ1_100,
	ESTIMATOR_LSQ2_100,
	ESTIMATOR_LSQ1_ADAPTIVE,
	ESTIMATOR_LSQ2_ADAPTIVE,
	ESTIMATOR_IMPULSE,
	ESTIMATOR_ALPHA_BETA,
	ESTIMATOR_KALMAN,
	ESTIMATOR_LSQ1_SHORT,
	ESTIMATOR_LSQ2_GATED,
	ESTIMATOR_KALMAN_GATED,
	ESTIMATOR_LSQ1_SHORT_GATED,
	ESTIMATOR_LSQ2_SHORT_GATED,
	ESTIMATOR_KALMAN_NOISE_GATED,
	ESTIMATOR_COUNT
};

/*
 * The conditions of one simulated panel and kernel: how uneven the scans
 * are, how many reports are lost, how noisy the position is, how reports
 * are stamped and how finely the consumer can read its clock.
 */
struct condition {
	const char *name;
	double jitter;
	double drop;
	double noise;
	enum stamp stamp;
	double poll;
	int clock_ms;
	int silent;
};

/*
 * One report as the consumer sees it: the time it was stamped with, the
 * time the consumer can first read it, and the noisy position.
 */
struct report {
	double stamp;
	double available;
	double arrival;
	double x;
	double y;
};

/* A small deterministic random number generator (xorshift64*). */
struct rng {
	unsigned long long state;
};

/* The sums one table cell collects over its runs. */
struct metrics {
	double line_error_sum;
	double line_error_count;
	double lag_sum;
	double lag_count;
	double judder_sum;
	double judder_count;
	double circle_error_sum;
	double circle_error_count;
	double circle_radial_sum;
	double overshoot_sum;
	double overshoot_count;
	double zigzag_error_sum;
	double zigzag_error_count;
	double retreat_sum;
	double retreat_count;
};

/*
 * The simulated conditions.  "tick" is the kernel as it is now: the worker
 * thread stamps each event with the millisecond tick after a scheduling
 * delay.  "usb" stamps the USB completion in microseconds, and "scan" uses
 * the panel's own Scan Time (100 us).  "usb-ms" is "usb" as a Wayland
 * client sees it through wl_touch alone (whole milliseconds), and
 * "scan-ms" the Scan Time the same way.  "cheap" is a poor panel.  The
 * host notices each transfer up to IRQ_JITTER late; a slow poll takes one
 * report per poll (a report scanned while the last one waits goes out at
 * the next poll).
 */
static const struct condition conditions[] = {
	{"ideal", 0.0, 0.0, 0.0, STAMP_SCAN, 0.001, 0, 0},
	{"tick", 0.10, 0.0, 1.0, STAMP_TICK, 0.001, 1, 0},
	{"usb", 0.10, 0.0, 1.0, STAMP_COMPLETION, 0.001, 1, 0},
	{"usb-ms", 0.10, 0.0, 1.0, STAMP_COMPLETION_MS, 0.001, 1, 0},
	{"scan", 0.10, 0.0, 1.0, STAMP_SCAN, 0.001, 1, 0},
	{"scan-ms", 0.10, 0.0, 1.0, STAMP_SCAN_MS, 0.001, 1, 0},
	{"cheap-tick", 0.30, 0.05, 2.5, STAMP_TICK, 0.008, 1, 0},
	{"cheap-usb", 0.30, 0.05, 2.5, STAMP_COMPLETION, 0.008, 1, 0},
	{"cheap-scan", 0.30, 0.05, 2.5, STAMP_SCAN, 0.008, 1, 0},
	{"cheap-scan-ms", 0.30, 0.05, 2.5, STAMP_SCAN_MS, 0.008, 1, 0},
};

/*
 * The period the device is known to report at from its earlier strokes
 * (0: nothing known), as the library keeps it per device.  The gap table
 * sets it; a stroke's own intervals take over once it has three of them.
 */
static double device_interval;

/*
 * The noise the device is known to have (px per axis; 0: nothing known),
 * as the library keeps it per device.  The velocity table sets it to the
 * condition's noise; the gated Kalman estimator scales its rest threshold
 * and its measurement noise by it.
 */
static double device_noise;

/* The report rates the tables cover, in hertz. */
static const double rates[] = {30.0, 45.0, 60.0, 90.0, 120.0};

/*
 * The methods compared.  "p" evaluates at the frame (full prediction),
 * "b25"/"b50" a quarter or half of an interval behind it; the reach lets
 * every method get to its target at the ages a report usually has, except
 * those with an explicit ceiling ("-c20").  "eN" chooses the time behind
 * the frame from the measured interval and delay so that the method never
 * needs to extrapolate more than N ms past the last report (a late report
 * may take it half an interval further).  "lsq12" takes, of the line's and
 * the parabola's prediction, the one that goes less far (it sees a finger
 * slowing down without the parabola's noise when it does not).  "w"
 * widens the windows by 1.6 (more smoothing for a noisy panel); "a"
 * widens them only when the noise it measures (the rms residual of a
 * parabola through the last 150 ms, at least six reports) exceeds 1.5 px.
 * "g" puts a report back before a gap: a panel that said nothing while the
 * finger stayed still is taken to have been where it last said until one
 * interval before it spoke again.
 */
static const struct method methods[] = {
	{"hold", KIND_HOLD, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0},
	{"android", KIND_ANDROID, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0},
	{"lerp-T", KIND_LERP, 1.0, 0.5, 0.0, 1.0, 0.0, 0.0, 1.0, 0},
	{"lerp-e12", KIND_LERP, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 1.0, 0},
	{"lsq1-p-c20", KIND_LSQ1, 0.0, 1.0, 0.0, 0.020, 0.0, 0.0, 1.0, 0},
	{"lsq1-p", KIND_LSQ1, 0.0, 1.0, 0.005, 1.0, 0.0, 0.0, 1.0, 0},
	{"lsq1-e12", KIND_LSQ1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 1.0, 0},
	{"lsq2-p", KIND_LSQ2, 0.0, 1.0, 0.005, 1.0, 0.0, 0.0, 1.0, 0},
	{"lsq2-e12", KIND_LSQ2, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 1.0, 0},
	{"lsq12-p", KIND_LSQ12, 0.0, 1.0, 0.005, 1.0, 0.0, 0.0, 1.0, 0},
	{"lsq12-e8", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.008, 1.0, 0},
	{"lsq12-e12", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 1.0, 0},
	{"lsq12-e16", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.016, 1.0, 0},
	{"lsq12-e20", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.020, 1.0, 0},
	{"lsq12w-e12", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 1.6, 0},
	{"lsq12w-e16", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.016, 1.6, 0},
	{"lsq12a-e12", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 0.0, 0},
	{"lsq12g-e12", KIND_LSQ12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.012, 1.0, 1},
	{"kal-p", KIND_KALMAN, 0.0, 1.0, 0.005, 1.0, 1.0e6, 0.0, 1.0, 0},
	{"kal-e12", KIND_KALMAN, 0.0, 0.0, 0.0, 0.0, 1.0e6, 0.012, 1.0, 0},
	{"ab-p", KIND_ALPHA_BETA, 0.0, 1.0, 0.005, 1.0, 0.0, 0.0, 1.0, 0},
	{"1euro-p", KIND_ONE_EURO, 0.0, 1.0, 0.005, 1.0, 0.0, 0.0, 1.0, 0},
};

/* The number of methods compared. */
#define METHOD_COUNT	((int)(sizeof(methods) / sizeof(methods[0])))

/* The names printed for the estimators. */
static const char *const estimator_names[ESTIMATOR_COUNT] = {
	"2pt", "lsq1-100", "lsq2-100", "lsq1-ad", "lsq2-ad", "impulse", "ab", "kalman", "lsq1-s", "lsq2-gate", "kal-gate", "lsq1s-gate", "lsq2s-gate", "kal-ngate",
};

static double rng_uniform(struct rng *rng);
static double rng_gauss(struct rng *rng);
static void path_position(enum path path, double t, double *x, double *y);
static double path_duration(enum path path);
static int generate(const struct condition *condition, double rate, enum path path, struct rng *rng, struct report *reports);
static double measured_interval(const struct report *reports, int count);
static double median_interval(const struct report *reports, int count);
static double measured_delay(const struct report *reports, int count);
static double measured_noise(const struct report *reports, int count);
static int lsq_fit(const struct report *reports, int first, int count, int degree, double origin, double *cx, double *cy);
static int window_first(const struct report *reports, int count, double window, int minimum);
static int fill_gaps(const struct report *reports, int count, struct report *filled);
static void method_point(const struct method *method, const struct report *reports, int count, double now, double *x, double *y);
static void kalman_run(const struct report *reports, int count, double q, double *x, double *y, double *vx, double *vy);
static void alpha_beta_run(const struct report *reports, int count, double *x, double *y, double *vx, double *vy);
static void one_euro_run(const struct report *reports, int count, double *x, double *y, double *vx, double *vy);
static double estimate_velocity(enum estimator estimator, const struct report *reports, int count);
static int at_rest(const struct report *reports, int count, double interval, double threshold);
static void run_paths(const struct condition *condition, double rate, const struct method *method, struct metrics *metrics);
static void print_resampling(void);
static void print_velocity(void);
static void print_gaps(void);
static void print_rate_estimate(void);
static void print_strokes(void);

/*
 * Runs every comparison and prints its tables.
 */
int
main(
	void)
{
	/* Prints the tables in the order the design discusses them. */
	print_resampling();
	print_velocity();
	print_gaps();
	print_rate_estimate();
	print_strokes();

	/* Succeeded: every table was printed. */
	return 0;
}

/* Draws a uniform number in [0, 1). */
static double
rng_uniform(
	struct rng *rng)
{
	unsigned long long value;

	/* Advances the xorshift64* state. */
	rng->state ^= rng->state >> 12;
	rng->state ^= rng->state << 25;
	rng->state ^= rng->state >> 27;
	value = rng->state * 2685821657736338717ULL;

	/* Succeeded: the top 53 bits as a fraction. */
	return (double)(value >> 11) / 9007199254740992.0;
}

/* Draws a standard normal number (Box-Muller). */
static double
rng_gauss(
	struct rng *rng)
{
	double u;
	double v;

	/* Two uniform numbers, the first kept away from zero. */
	u = rng_uniform(rng);
	if (u < 1e-12)
		u = 1e-12;
	v = rng_uniform(rng);

	/* Succeeded: one normal number. */
	return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

/* Gives the true position of the finger on a path at a time. */
static void
path_position(
	enum path path,
	double t,
	double *x,
	double *y)
{
	double ramp;
	double tau;

	*x = 0.0;
	*y = 0.0;

	/* Chooses the path. */
	switch (path) {
	case PATH_LINE:
		/* A steady scroll at 1500 px/s. */
		*x = 1500.0 * t;
		break;
	case PATH_CIRCLE:
		/* A circle of 120 px at 1.5 turns a second (1131 px/s). */
		*x = 120.0 * cos(2.0 * M_PI * 1.5 * t);
		*y = 120.0 * sin(2.0 * M_PI * 1.5 * t);
		break;
	case PATH_STOP:
		/* 1500 px/s, then a stop within 30 ms at 472.5 px. */
		if (t < 0.3) {
			*x = 1500.0 * t;
		} else if (t < 0.33) {
			tau = t - 0.3;
			*x = 450.0 + 1500.0 * (tau - tau * tau / (2.0 * 0.03));
		} else {
			*x = 472.5;
		}
		break;
	case PATH_ZIGZAG:
		/* Back and forth over 150 px twice a second (1885 px/s at most). */
		*x = 150.0 * sin(2.0 * M_PI * 2.0 * t);
		break;
	case PATH_FLING_FAST:
	case PATH_FLING_SLOW:
		/*
		 * A flick: 50 ms at rest, then 120 ms speeding up to the release
		 * velocity with no acceleration left at the lift.
		 */
		ramp = 0.12;
		if (path == PATH_FLING_FAST)
			tau = 3000.0;
		else
			tau = 800.0;
		if (t < 0.05) {
			*x = 0.0;
		} else {
			*x = tau * ((t - 0.05) / 2.0 - sin(M_PI * (t - 0.05) / ramp) * ramp / (2.0 * M_PI));
		}
		break;
	case PATH_PAUSE:
		/* 1000 px/s for 200 ms, a stop within 20 ms, then 120 ms at rest. */
		if (t < 0.2) {
			*x = 1000.0 * t;
		} else if (t < 0.22) {
			tau = t - 0.2;
			*x = 200.0 + 1000.0 * (tau - tau * tau / (2.0 * 0.02));
		} else {
			*x = 210.0;
		}
		break;
	case PATH_FLING_EASE:
		/*
		 * The fast flick, but the finger slows from 3000 to 2400 px/s in
		 * the last 30 ms before it lifts.
		 */
		if (t < 0.05) {
			*x = 0.0;
		} else if (t < 0.17) {
			*x = 3000.0 * ((t - 0.05) / 2.0 - sin(M_PI * (t - 0.05) / 0.12) * 0.12 / (2.0 * M_PI));
		} else {
			tau = t - 0.17;
			*x = 180.0 + 3000.0 * tau - 300.0 * tau * tau / 0.03;
		}
		break;
	case PATH_REST_GO:
		/* 400 ms at rest, then 1500 px/s. */
		if (t < 0.4)
			*x = 0.0;
		else
			*x = 1500.0 * (t - 0.4);
		break;
	case PATH_STROKE:
		/* A pen-like stroke: a circle of 80 px at 2 turns a second (1005 px/s). */
		*x = 80.0 * cos(2.0 * M_PI * 2.0 * t);
		*y = 80.0 * sin(2.0 * M_PI * 2.0 * t);
		break;
	}
}

/* Gives how long the finger stays down on a path. */
static double
path_duration(
	enum path path)
{
	/* Chooses the path's length in time. */
	switch (path) {
	case PATH_LINE:
		return 0.8;
	case PATH_CIRCLE:
		return 1.0;
	case PATH_STOP:
		return 0.6;
	case PATH_ZIGZAG:
		return 1.0;
	case PATH_FLING_FAST:
	case PATH_FLING_SLOW:
		return 0.17;
	case PATH_PAUSE:
		return 0.34;
	case PATH_STROKE:
		return 1.0;
	case PATH_FLING_EASE:
		return 0.2;
	case PATH_REST_GO:
		return 0.7;
	}

	/* Unreached: every path has a length. */
	return 0.0;
}

/*
 * Simulates the reports of one stroke.
 *
 * The panel scans at the rate with the condition's jitter, loses reports
 * with its drop probability and adds Gaussian noise; the host polls the
 * endpoint at its period.  Returns the number of reports.
 */
static int
generate(
	const struct condition *condition,
	double rate,
	enum path path,
	struct rng *rng,
	struct report *reports)
{
	double period;
	double scan;
	double duration;
	double poll_phase;
	double completion;
	double last_completion;
	double handled;
	double worker;
	double latency;
	double x;
	double y;
	double true_x;
	double true_y;
	double last_true_x;
	double last_true_y;
	int count;

	/* The first scan falls anywhere within one period after the touch. */
	period = 1.0 / rate;
	duration = path_duration(path);
	scan = rng_uniform(rng) * period;
	poll_phase = rng_uniform(rng) * condition->poll;
	last_completion = -1.0;
	last_true_x = 1e9;
	last_true_y = 1e9;
	count = 0;

	/* Scans until the finger lifts. */
	while (scan <= duration && count < SAMPLES_MAX) {
		/* A lost report is scanned but never arrives. */
		path_position(path, scan, &true_x, &true_y);
		if (condition->silent && true_x == last_true_x && true_y == last_true_y) {
			/* A panel that says nothing while the finger stays still. */
		} else if (rng_uniform(rng) >= condition->drop) {
			/* The true position with the panel's noise. */
			last_true_x = true_x;
			last_true_y = true_y;
			x = true_x + condition->noise * rng_gauss(rng);
			y = true_y + condition->noise * rng_gauss(rng);

			/* The host takes the report at its next poll, one report a poll. */
			completion = ceil((scan + TRANSFER - poll_phase) / condition->poll) * condition->poll + poll_phase;
			if (completion < last_completion + condition->poll - 1e-9)
				completion = last_completion + condition->poll;
			last_completion = completion;

			/* The host notices it a little later. */
			handled = completion + IRQ_JITTER * rng_uniform(rng);

			/*
			 * The worker thread runs after a scheduling delay: mostly a
			 * fraction of a millisecond, sometimes a few (a model, not a
			 * measurement).
			 */
			latency = 0.00005 - 0.0003 * log(1.0 - rng_uniform(rng));
			if (rng_uniform(rng) < 0.05)
				latency += 0.001 + 0.003 * rng_uniform(rng);
			worker = handled + latency;

			/* Stamps the report the condition's way. */
			reports[count].x = x;
			reports[count].y = y;
			if (condition->stamp == STAMP_SCAN) {
				reports[count].stamp = floor(scan / 0.0001) * 0.0001;
				reports[count].available = handled + DELIVERY;
			} else if (condition->stamp == STAMP_SCAN_MS) {
				reports[count].stamp = floor(scan / 0.001) * 0.001;
				reports[count].available = handled + DELIVERY;
			} else if (condition->stamp == STAMP_COMPLETION) {
				reports[count].stamp = floor(handled / 0.000001) * 0.000001;
				reports[count].available = handled + DELIVERY;
			} else if (condition->stamp == STAMP_COMPLETION_MS) {
				reports[count].stamp = floor(handled / 0.001) * 0.001;
				reports[count].available = handled + DELIVERY;
			} else {
				reports[count].stamp = floor(worker / 0.001) * 0.001;
				reports[count].available = worker + DELIVERY;
			}

			/* The consumer records the arrival on its own clock. */
			reports[count].arrival = reports[count].available;
			if (condition->clock_ms)
				reports[count].arrival = floor(reports[count].available * 1000.0) / 1000.0;
			count++;
		}

		/* The next scan, one period on with the jitter. */
		scan += period * (1.0 + condition->jitter * (2.0 * rng_uniform(rng) - 1.0));
	}

	/* Succeeded: the stroke's reports. */
	return count;
}

/*
 * Measures the report period as the mean of the last intervals, leaving
 * out the gaps.
 *
 * Unlike the median it is not fooled by a slow poll's aliases (90 Hz read
 * every 8 ms gives intervals of 8 and 16 ms); a lost report lengthens it by
 * the loss rate.  An interval longer than 2.5 times the device's period
 * (or the median, when the period is not known) is a gap (a panel that said
 * nothing while the finger stayed still) and is left out.  Returns 0 with
 * fewer than two reports and no known period.
 */
static double
measured_interval(
	const struct report *reports,
	int count)
{
	double reference;
	double interval;
	double sum;
	int n;
	int i;

	/* Too few reports to measure: the device's period, if it is known. */
	if (count < 4 && device_interval > 0.0)
		return device_interval;
	if (count < 2)
		return 0.0;

	/* A gap is measured against the device's period, or the median. */
	reference = device_interval;
	if (reference <= 0.0)
		reference = median_interval(reports, count);

	/* Adds up the last intervals that are not gaps. */
	sum = 0.0;
	n = 0;
	for (i = count - 1; i > 0 && i >= count - RATE_WINDOW; i--) {
		interval = reports[i].stamp - reports[i - 1].stamp;
		if (interval > 2.5 * reference)
			continue;
		sum += interval;
		n++;
	}

	/* Succeeded: the mean interval (the reference when every one was a gap). */
	if (n == 0)
		return reference;
	return sum / (double)n;
}

/*
 * Measures the report period as the median of the last intervals.
 *
 * The median ignores a lost report (a doubled interval) and a burst (two
 * reports together), but a panel read by a slower poll than its rate
 * (90 Hz read every 8 ms: intervals of 8 and 16 ms) gives the shorter
 * alias.  Returns 0 with fewer than two reports.
 */
static double
median_interval(
	const struct report *reports,
	int count)
{
	double intervals[RATE_WINDOW];
	double swap;
	int n;
	int i;
	int j;

	/* Collects the last intervals. */
	n = 0;
	for (i = count - 1; i > 0 && n < RATE_WINDOW; i--) {
		intervals[n] = reports[i].stamp - reports[i - 1].stamp;
		n++;
	}

	/* Too few reports to measure. */
	if (n == 0)
		return 0.0;

	/* Sorts them (insertion sort: at most eight). */
	for (i = 1; i < n; i++) {
		for (j = i; j > 0 && intervals[j - 1] > intervals[j]; j--) {
			swap = intervals[j];
			intervals[j] = intervals[j - 1];
			intervals[j - 1] = swap;
		}
	}

	/* Succeeded: the middle interval. */
	return intervals[n / 2];
}

/*
 * Measures how late the consumer reads a report: the median of the last
 * reports' time from their stamp to their arrival.
 */
static double
measured_delay(
	const struct report *reports,
	int count)
{
	double delays[RATE_WINDOW];
	double swap;
	int n;
	int i;
	int j;

	/* Collects the last delays. */
	n = 0;
	for (i = count - 1; i >= 0 && n < RATE_WINDOW; i--) {
		delays[n] = reports[i].arrival - reports[i].stamp;
		n++;
	}

	/* Sorts them (insertion sort: at most eight). */
	for (i = 1; i < n; i++) {
		for (j = i; j > 0 && delays[j - 1] > delays[j]; j--) {
			swap = delays[j];
			delays[j] = delays[j - 1];
			delays[j - 1] = swap;
		}
	}

	/* Succeeded: the middle delay. */
	return delays[n / 2];
}

/*
 * Measures the panel's noise: the rms distance of the last reports from a
 * parabola fitted through them (150 ms, at least six reports), corrected
 * for the three degrees of freedom the fit takes.  Returns 0 with too few
 * reports.
 */
static double
measured_noise(
	const struct report *reports,
	int count)
{
	double cx[3];
	double cy[3];
	double t;
	double dx;
	double dy;
	double sum;
	int first;
	int k;

	/* Too few reports for a fit with residuals to spare. */
	first = window_first(reports, count, 0.15, 6);
	if (count - first < 6)
		return 0.0;

	/* The parabola through the window. */
	(void)lsq_fit(reports, first, count, 2, reports[count - 1].stamp, cx, cy);

	/* Adds up the squared distances. */
	sum = 0.0;
	for (k = first; k < count; k++) {
		t = reports[k].stamp - reports[count - 1].stamp;
		dx = reports[k].x - (cx[0] + cx[1] * t + cx[2] * t * t);
		dy = reports[k].y - (cy[0] + cy[1] * t + cy[2] * t * t);
		sum += dx * dx + dy * dy;
	}

	/* Succeeded: the noise per axis. */
	return sqrt(sum / (2.0 * (double)(count - first - 3)));
}

/*
 * Copies the last reports, putting a report back before every gap: the
 * finger is taken to have stayed where the report before the gap said
 * until one interval before the report after it.  Returns the number of
 * reports in the copy.
 */
static int
fill_gaps(
	const struct report *reports,
	int count,
	struct report *filled)
{
	double interval;
	int first;
	int n;
	int k;

	/* The period, measured without the gaps. */
	interval = measured_interval(reports, count);

	/* Copies the last 32 reports, a report put back before each gap. */
	first = count - 32;
	if (first < 0)
		first = 0;
	n = 0;
	for (k = first; k < count; k++) {
		if (k > first && reports[k].stamp - reports[k - 1].stamp > 2.5 * interval) {
			filled[n] = reports[k - 1];
			filled[n].stamp = reports[k].stamp - interval;
			n++;
		}
		filled[n] = reports[k];
		n++;
	}

	/* Succeeded: the copy. */
	return n;
}

/*
 * Finds the first report of the window before the last report: the reports
 * stamped within the window, and at least the minimum number of them.
 */
static int
window_first(
	const struct report *reports,
	int count,
	double window,
	int minimum)
{
	int first;

	/* Walks back while the reports are inside the window. */
	first = count - 1;
	while (first > 0 && reports[count - 1].stamp - reports[first - 1].stamp <= window)
		first--;

	/* Takes more reports when the window holds too few. */
	if (count - first < minimum) {
		first = count - minimum;
		if (first < 0)
			first = 0;
	}

	/* Succeeded: the index of the window's first report. */
	return first;
}

/*
 * Fits a polynomial of a degree to reports by least squares.
 *
 * The time is measured from the origin, and the coefficients of x and y
 * (constant, linear, quadratic) are written to cx and cy.  Returns the
 * degree actually fitted: lower when there are too few distinct reports.
 */
static int
lsq_fit(
	const struct report *reports,
	int first,
	int count,
	int degree,
	double origin,
	double *cx,
	double *cy)
{
	double m[3][3];
	double bx[3];
	double by[3];
	double powers[5];
	double t;
	double factor;
	double pivot;
	int size;
	int i;
	int j;
	int k;

	/* A fit needs one report more than its degree. */
	if (degree > count - first - 1)
		degree = count - first - 1;
	if (degree < 0)
		degree = 0;
	size = degree + 1;

	/* Sums the normal equations. */
	memset(m, 0, sizeof(m));
	memset(bx, 0, sizeof(bx));
	memset(by, 0, sizeof(by));
	for (k = first; k < count; k++) {
		t = reports[k].stamp - origin;
		powers[0] = 1.0;
		for (i = 1; i < 5; i++)
			powers[i] = powers[i - 1] * t;
		for (i = 0; i < size; i++) {
			for (j = 0; j < size; j++)
				m[i][j] += powers[i + j];
			bx[i] += powers[i] * reports[k].x;
			by[i] += powers[i] * reports[k].y;
		}
	}

	/* Solves them by Gaussian elimination. */
	for (i = 0; i < size; i++) {
		pivot = m[i][i];
		if (fabs(pivot) < 1e-18) {
			/* Degenerate (reports at one time): fits one degree lower. */
			return lsq_fit(reports, first, count, degree - 1, origin, cx, cy);
		}
		for (j = i + 1; j < size; j++) {
			factor = m[j][i] / pivot;
			for (k = i; k < size; k++)
				m[j][k] -= factor * m[i][k];
			bx[j] -= factor * bx[i];
			by[j] -= factor * by[i];
		}
	}

	/* Back substitution. */
	for (i = size - 1; i >= 0; i--) {
		for (j = i + 1; j < size; j++) {
			bx[i] -= m[i][j] * cx[j];
			by[i] -= m[i][j] * cy[j];
		}
		cx[i] = bx[i] / m[i][i];
		cy[i] = by[i] / m[i][i];
	}

	/* The coefficients above the degree are zero. */
	for (i = size; i < 3; i++) {
		cx[i] = 0.0;
		cy[i] = 0.0;
	}

	/* Succeeded: the degree fitted. */
	return degree;
}

/*
 * Runs an alpha-beta filter (a steady-state constant-velocity Kalman
 * filter) over the reports and gives the last state.
 */
static void
alpha_beta_run(
	const struct report *reports,
	int count,
	double *x,
	double *y,
	double *vx,
	double *vy)
{
	double dt;
	double px;
	double py;
	double rx;
	double ry;
	int k;

	/* Starts at the first report, at rest. */
	*x = reports[0].x;
	*y = reports[0].y;
	*vx = 0.0;
	*vy = 0.0;

	/* Predicts to each report and corrects by a share of the residual. */
	for (k = 1; k < count; k++) {
		dt = reports[k].stamp - reports[k - 1].stamp;
		if (dt < 0.0005)
			dt = 0.0005;
		px = *x + *vx * dt;
		py = *y + *vy * dt;
		rx = reports[k].x - px;
		ry = reports[k].y - py;
		*x = px + 0.6 * rx;
		*y = py + 0.6 * ry;
		*vx += 0.3 * rx / dt;
		*vy += 0.3 * ry / dt;
	}
}

/*
 * Runs the 1 euro filter (an adaptive low-pass filter, Casiez et al. 2012)
 * over the reports and gives the last filtered position and velocity.
 */
static void
one_euro_run(
	const struct report *reports,
	int count,
	double *x,
	double *y,
	double *vx,
	double *vy)
{
	double dt;
	double dx;
	double dy;
	double alpha_d;
	double alpha;
	double cutoff;
	double speed;
	int k;

	/* Starts at the first report, at rest. */
	*x = reports[0].x;
	*y = reports[0].y;
	*vx = 0.0;
	*vy = 0.0;

	/* Filters the derivative, then the position with a cutoff that rises with the speed. */
	for (k = 1; k < count; k++) {
		dt = reports[k].stamp - reports[k - 1].stamp;
		if (dt < 0.0005)
			dt = 0.0005;
		dx = (reports[k].x - *x) / dt;
		dy = (reports[k].y - *y) / dt;
		alpha_d = 1.0 / (1.0 + 1.0 / (2.0 * M_PI * 1.0 * dt));
		*vx += alpha_d * (dx - *vx);
		*vy += alpha_d * (dy - *vy);
		speed = sqrt(*vx * *vx + *vy * *vy);
		cutoff = 1.0 + 0.007 * speed;
		alpha = 1.0 / (1.0 + 1.0 / (2.0 * M_PI * cutoff * dt));
		*x += alpha * (reports[k].x - *x);
		*y += alpha * (reports[k].y - *y);
	}
}

/*
 * Runs a constant-velocity Kalman filter over the reports and gives the
 * last state.  The process noise is white acceleration of spectral density
 * q (px^2/s^3); the measurement noise is 2 px^2.  Unlike a fixed-gain
 * filter, it handles two reports that arrive together (a tiny interval).
 */
static void
kalman_run(
	const struct report *reports,
	int count,
	double q,
	double *x,
	double *y,
	double *vx,
	double *vy)
{
	double state[2][2];
	double p[2][2][2];
	double dt;
	double p00;
	double p01;
	double p11;
	double s;
	double k0;
	double k1;
	double residual;
	double measure;
	double measurement;
	int axis;
	int k;

	/* The measurement noise: 2 px^2, or the device's when it is larger. */
	measurement = 2.0;
	if (device_noise * device_noise > measurement)
		measurement = device_noise * device_noise;

	/* Starts each axis at the first report, at rest, with a wide velocity. */
	for (axis = 0; axis < 2; axis++) {
		state[axis][0] = axis == 0 ? reports[0].x : reports[0].y;
		state[axis][1] = 0.0;
		p[axis][0][0] = 4.0;
		p[axis][0][1] = 0.0;
		p[axis][1][0] = 0.0;
		p[axis][1][1] = 1.0e7;
	}

	/* Predicts to each report and corrects each axis. */
	for (k = 1; k < count; k++) {
		dt = reports[k].stamp - reports[k - 1].stamp;
		if (dt < 0.0)
			dt = 0.0;
		for (axis = 0; axis < 2; axis++) {
			measure = axis == 0 ? reports[k].x : reports[k].y;
			state[axis][0] += state[axis][1] * dt;
			p00 = p[axis][0][0] + dt * (2.0 * p[axis][0][1] + dt * p[axis][1][1]) + q * dt * dt * dt / 3.0;
			p01 = p[axis][0][1] + dt * p[axis][1][1] + q * dt * dt / 2.0;
			p11 = p[axis][1][1] + q * dt;
			s = p00 + measurement;
			k0 = p00 / s;
			k1 = p01 / s;
			residual = measure - state[axis][0];
			state[axis][0] += k0 * residual;
			state[axis][1] += k1 * residual;
			p[axis][0][0] = (1.0 - k0) * p00;
			p[axis][0][1] = (1.0 - k0) * p01;
			p[axis][1][0] = p[axis][0][1];
			p[axis][1][1] = p11 - k1 * p01;
		}
	}

	/* Succeeded: the filtered position and velocity. */
	*x = state[0][0];
	*y = state[1][0];
	*vx = state[0][1];
	*vy = state[1][1];
}

/*
 * Gives the point a method draws at a frame, from the reports the consumer
 * has read by then.
 */
static void
method_point(
	const struct method *method,
	const struct report *reports,
	int count,
	double now,
	double *x,
	double *y)
{
	double interval;
	double behind;
	double target;
	double limit;
	double delta;
	double fraction;
	double cx[3];
	double cy[3];
	double qx[3];
	double qy[3];
	double lx;
	double ly;
	double scale;
	double noise;
	double advance_line;
	double advance_parabola;
	struct report filled[64];
	double vx;
	double vy;
	double t;
	int first;
	int k;

	/* Without two reports every method holds the last one. */
	*x = reports[count - 1].x;
	*y = reports[count - 1].y;
	if (count < 2 || method->kind == KIND_HOLD)
		return;

	/* A method that fills gaps works on the copy with the reports put back. */
	if (method->gap_fill) {
		count = fill_gaps(reports, count, filled);
		reports = filled;
	}
	interval = measured_interval(reports, count);

	/* Chooses the time the method evaluates the stroke at, and how far it may extrapolate. */
	if (method->kind == KIND_ANDROID) {
		/* 5 ms behind the frame; at most half the last interval and 8 ms ahead. */
		target = now - 0.005;
		delta = reports[count - 1].stamp - reports[count - 2].stamp;
		limit = delta / 2.0;
		if (limit > 0.008)
			limit = 0.008;
		if (delta < 0.002 || delta > 0.020) {
			if (target > reports[count - 1].stamp)
				return;
		}
	} else if (method->extrapolation > 0.0) {
		/*
		 * Far enough behind that a report of the usual age needs no more
		 * than the allowed extrapolation; a late one may take it half an
		 * interval further.
		 */
		behind = interval + measured_delay(reports, count) - method->extrapolation;
		if (behind < 0.0)
			behind = 0.0;
		target = now - behind;
		limit = method->extrapolation + interval / 2.0;
	} else {
		target = now - method->behind * interval;
		limit = method->reach * interval + method->reach_extra;
		if (limit > method->reach_ceiling)
			limit = method->reach_ceiling;
	}
	if (target > reports[count - 1].stamp + limit)
		target = reports[count - 1].stamp + limit;

	/* Nothing is drawn from before the stroke's first report. */
	if (target < reports[0].stamp)
		target = reports[0].stamp;

	/* Computes the point. */
	switch (method->kind) {
	case KIND_ANDROID:
	case KIND_LERP:
		/* Interpolates between the two reports around the target, or extrapolates from the last two. */
		k = count - 1;
		while (k > 1 && reports[k - 1].stamp > target)
			k--;
		delta = reports[k].stamp - reports[k - 1].stamp;
		if (delta <= 0.0)
			return;
		fraction = (target - reports[k - 1].stamp) / delta;
		if (fraction < 0.0)
			fraction = 0.0;
		*x = reports[k - 1].x + fraction * (reports[k].x - reports[k - 1].x);
		*y = reports[k - 1].y + fraction * (reports[k].y - reports[k - 1].y);
		break;
	case KIND_LSQ1:
		/* A line through the reports of the last 2.5 intervals (40 ms at least). */
		delta = 2.5 * interval;
		if (delta < 0.04)
			delta = 0.04;
		delta *= method->window_scale;
		first = window_first(reports, count, delta, 2);
		(void)lsq_fit(reports, first, count, 1, reports[count - 1].stamp, cx, cy);
		t = target - reports[count - 1].stamp;
		*x = cx[0] + cx[1] * t;
		*y = cy[0] + cy[1] * t;
		break;
	case KIND_LSQ2:
		/* A parabola through the reports of the last 3.5 intervals (60 ms at least). */
		delta = 3.5 * interval;
		if (delta < 0.06)
			delta = 0.06;
		delta *= method->window_scale;
		first = window_first(reports, count, delta, 3);
		(void)lsq_fit(reports, first, count, 2, reports[count - 1].stamp, cx, cy);
		t = target - reports[count - 1].stamp;
		*x = cx[0] + cx[1] * t + cx[2] * t * t;
		*y = cy[0] + cy[1] * t + cy[2] * t * t;
		break;
	case KIND_LSQ12:
		/* A noisy panel gets wider windows when the method adapts. */
		scale = method->window_scale;
		if (scale == 0.0) {
			scale = 1.0;
			noise = measured_noise(reports, count);
			if (noise > 1.5)
				scale = 1.6;
		}

		/* The line and the parabola, each on its own window. */
		delta = 2.5 * interval;
		if (delta < 0.04)
			delta = 0.04;
		delta *= scale;
		first = window_first(reports, count, delta, 2);
		(void)lsq_fit(reports, first, count, 1, reports[count - 1].stamp, cx, cy);
		delta = 3.5 * interval;
		if (delta < 0.06)
			delta = 0.06;
		delta *= scale;
		first = window_first(reports, count, delta, 3);
		(void)lsq_fit(reports, first, count, 2, reports[count - 1].stamp, qx, qy);
		t = target - reports[count - 1].stamp;

		/* Behind the last report both are interpolations: the parabola follows a curve better. */
		*x = qx[0] + qx[1] * t + qx[2] * t * t;
		*y = qy[0] + qy[1] * t + qy[2] * t * t;
		if (t <= 0.0)
			break;

		/*
		 * Past it, the prediction that moves less far from the parabola's
		 * point at the last report, along the line's velocity, and never
		 * backwards (a parabola that turns back predicts a retreat).
		 */
		lx = qx[0] + cx[1] * t;
		ly = qy[0] + cy[1] * t;
		advance_line = (lx - qx[0]) * cx[1] + (ly - qy[0]) * cy[1];
		advance_parabola = (*x - qx[0]) * cx[1] + (*y - qy[0]) * cy[1];
		if (advance_parabola <= 0.0) {
			*x = qx[0];
			*y = qy[0];
		} else if (advance_line < advance_parabola) {
			*x = lx;
			*y = ly;
		}
		break;
	case KIND_KALMAN:
		/* The filtered state moved on by its velocity. */
		kalman_run(reports, count, method->kalman_q, x, y, &vx, &vy);
		t = target - reports[count - 1].stamp;
		*x += vx * t;
		*y += vy * t;
		break;
	case KIND_ALPHA_BETA:
		/* The filtered state moved on by its velocity. */
		alpha_beta_run(reports, count, x, y, &vx, &vy);
		t = target - reports[count - 1].stamp;
		*x += vx * t;
		*y += vy * t;
		break;
	case KIND_ONE_EURO:
		/* The filtered position moved on by the filtered velocity. */
		one_euro_run(reports, count, x, y, &vx, &vy);
		t = target - reports[count - 1].stamp;
		*x += vx * t;
		*y += vy * t;
		break;
	default:
		break;
	}
}

/* Estimates the velocity (px/s, along x) at a lift from all the stroke's reports. */
static double
estimate_velocity(
	enum estimator estimator,
	const struct report *reports,
	int count)
{
	double cx[3];
	double cy[3];
	double interval;
	double window;
	double work;
	double previous;
	double current;
	double dt;
	double x;
	double y;
	double vx;
	double vy;
	double threshold;
	int first;
	int k;

	/* A single report has no velocity. */
	if (count < 2)
		return 0.0;
	interval = measured_interval(reports, count);

	/* Chooses the estimator. */
	switch (estimator) {
	case ESTIMATOR_TWO_POINT:
		/* The last two reports. */
		dt = reports[count - 1].stamp - reports[count - 2].stamp;
		if (dt <= 0.0)
			return 0.0;
		return (reports[count - 1].x - reports[count - 2].x) / dt;
	case ESTIMATOR_LSQ1_100:
	case ESTIMATOR_LSQ1_ADAPTIVE:
		/* The slope of a line through the last 100 ms (or 3.5 intervals). */
		window = 0.1;
		if (estimator == ESTIMATOR_LSQ1_ADAPTIVE && 3.5 * interval > window)
			window = 3.5 * interval;
		first = window_first(reports, count, window, 2);
		(void)lsq_fit(reports, first, count, 1, reports[count - 1].stamp, cx, cy);
		return cx[1];
	case ESTIMATOR_LSQ2_100:
	case ESTIMATOR_LSQ2_ADAPTIVE:
		/* The slope at the last report of a parabola through the last 100 ms (or 3.5 intervals). */
		window = 0.1;
		if (estimator == ESTIMATOR_LSQ2_ADAPTIVE && 3.5 * interval > window)
			window = 3.5 * interval;
		first = window_first(reports, count, window, 2);
		(void)lsq_fit(reports, first, count, 2, reports[count - 1].stamp, cx, cy);
		return cx[1];
	case ESTIMATOR_IMPULSE:
		/*
		 * The velocity whose kinetic energy equals the work the segments
		 * of the last 100 ms did (the impulse strategy).
		 */
		first = window_first(reports, count, 0.1, 2);
		work = 0.0;
		for (k = first + 1; k < count; k++) {
			dt = reports[k].stamp - reports[k - 1].stamp;
			if (dt <= 0.0)
				continue;
			previous = work < 0.0 ? -sqrt(-2.0 * work) : sqrt(2.0 * work);
			current = (reports[k].x - reports[k - 1].x) / dt;
			work += (current - previous) * fabs(current);
			if (k == first + 1)
				work *= 0.5;
		}
		return work < 0.0 ? -sqrt(-2.0 * work) : sqrt(2.0 * work);
	case ESTIMATOR_ALPHA_BETA:
		/* The alpha-beta filter's velocity. */
		alpha_beta_run(reports, count, &x, &y, &vx, &vy);
		return vx;
	case ESTIMATOR_KALMAN:
		/* The constant-velocity Kalman filter's velocity. */
		kalman_run(reports, count, 1.0e7, &x, &y, &vx, &vy);
		return vx;
	case ESTIMATOR_LSQ1_SHORT:
		/* The slope of a line through the last 2.5 intervals (50 ms at least, three reports). */
		window = 2.5 * interval;
		if (window < 0.05)
			window = 0.05;
		first = window_first(reports, count, window, 3);
		(void)lsq_fit(reports, first, count, 1, reports[count - 1].stamp, cx, cy);
		return cx[1];
	case ESTIMATOR_KALMAN_NOISE_GATED:
		/*
		 * The Kalman filter's velocity, none at rest, the rest threshold
		 * four standard errors of the short line's slope for the device's
		 * noise (three reports over two intervals: noise * sqrt(2) / span),
		 * and never under 150 px/s.
		 */
		window = 2.5 * interval;
		if (window < 0.05)
			window = 0.05;
		threshold = 4.0 * device_noise * sqrt(2.0) / window;
		if (threshold < 150.0)
			threshold = 150.0;
		if (at_rest(reports, count, interval, threshold))
			return 0.0;
		kalman_run(reports, count, 1.0e7, &x, &y, &vx, &vy);
		return vx;
	case ESTIMATOR_KALMAN_GATED:
		/* The Kalman filter's velocity, none at rest. */
		if (at_rest(reports, count, interval, 150.0))
			return 0.0;
		kalman_run(reports, count, 1.0e7, &x, &y, &vx, &vy);
		return vx;
	case ESTIMATOR_LSQ1_SHORT_GATED:
		/* The short line's slope, none at rest. */
		if (at_rest(reports, count, interval, 150.0))
			return 0.0;
		window = 2.5 * interval;
		if (window < 0.05)
			window = 0.05;
		first = window_first(reports, count, window, 3);
		(void)lsq_fit(reports, first, count, 1, reports[count - 1].stamp, cx, cy);
		return cx[1];
	case ESTIMATOR_LSQ2_SHORT_GATED:
		/* The slope at the last report of a parabola through the last 3 intervals (60 ms, four reports at least), none at rest. */
		if (at_rest(reports, count, interval, 150.0))
			return 0.0;
		window = 3.0 * interval;
		if (window < 0.06)
			window = 0.06;
		first = window_first(reports, count, window, 4);
		(void)lsq_fit(reports, first, count, 2, reports[count - 1].stamp, cx, cy);
		return cx[1];
	case ESTIMATOR_LSQ2_GATED:
		/*
		 * The parabola's slope (as lsq2-ad), but none when a line through
		 * the last 2.5 intervals says the finger was nearly at rest: the
		 * short line sees a pause with little noise, the parabola a flick
		 * with little bias.
		 */
		if (at_rest(reports, count, interval, 150.0))
			return 0.0;
		window = 0.1;
		if (3.5 * interval > window)
			window = 3.5 * interval;
		first = window_first(reports, count, window, 2);
		(void)lsq_fit(reports, first, count, 2, reports[count - 1].stamp, cx, cy);
		return cx[1];
	default:
		break;
	}

	/* Unreached: every estimator is handled above. */
	return 0.0;
}

/*
 * Tells whether the finger was nearly at rest at the end of the stroke: a
 * line through the last 2.5 intervals (50 ms, three reports at least)
 * moves slower than the threshold.
 */
static int
at_rest(
	const struct report *reports,
	int count,
	double interval,
	double threshold)
{
	double cx[3];
	double cy[3];
	double window;
	int first;

	/* The short line. */
	window = 2.5 * interval;
	if (window < 0.05)
		window = 0.05;
	first = window_first(reports, count, window, 3);
	(void)lsq_fit(reports, first, count, 1, reports[count - 1].stamp, cx, cy);

	/* Succeeded: at rest when it is slow. */
	if (sqrt(cx[1] * cx[1] + cy[1] * cy[1]) < threshold)
		return 1;
	return 0;
}

/*
 * Runs one method over the resampling paths many times and adds up its
 * errors.
 */
static void
run_paths(
	const struct condition *condition,
	double rate,
	const struct method *method,
	struct metrics *metrics)
{
	static struct report reports[SAMPLES_MAX];
	struct rng rng;
	enum path path;
	double frame;
	double now;
	double phase;
	double x;
	double y;
	double tx;
	double ty;
	double previous_x;
	double step;
	double steps[128];
	double mean;
	double overshoot;
	double retreat;
	double last_x;
	double radius;
	int step_count;
	int count;
	int seen;
	int run;
	int i;

	/* Each path and run has its own seed, the same for every method. */
	for (path = PATH_LINE; path <= PATH_ZIGZAG; path++) {
		for (run = 0; run < RUNS; run++) {
			rng.state = 0x9e3779b97f4a7c15ULL ^ ((unsigned long long)(path + 1) << 32) ^ (unsigned long long)(run * 7919 + (int)rate);
			count = generate(condition, rate, path, &rng, reports);
			phase = rng_uniform(&rng) * FRAME_PERIOD;
			step_count = 0;
			previous_x = 0.0;
			overshoot = 0.0;
			retreat = 0.0;
			last_x = -1e9;

			/* Draws every frame while the finger is down. */
			for (frame = phase; frame <= path_duration(path); frame += FRAME_PERIOD) {
				/* The reports the consumer has read by this frame. */
				seen = 0;
				while (seen < count && reports[seen].available <= frame)
					seen++;
				if (seen == 0)
					continue;

				/* The consumer's clock may read only whole milliseconds. */
				now = frame;
				if (condition->clock_ms)
					now = floor(frame * 1000.0) / 1000.0;
				method_point(method, reports, seen, now, &x, &y);
				path_position(path, frame, &tx, &ty);

				/* The finger never goes back on the scroll and the stop: a step back is the method's. */
				if (path == PATH_LINE || path == PATH_STOP) {
					if (last_x - x > retreat)
						retreat = last_x - x;
					last_x = x;
				}

				/* Adds this frame's error to the path's measures. */
				if (path == PATH_LINE && frame >= 0.15) {
					metrics->line_error_sum += (x - tx) * (x - tx) + (y - ty) * (y - ty);
					metrics->line_error_count += 1.0;
					metrics->lag_sum += (tx - x) / 1500.0;
					metrics->lag_count += 1.0;
					if (step_count > 0 || previous_x != 0.0) {
						step = x - previous_x;
						if (step_count < 128) {
							steps[step_count] = step;
							step_count++;
						}
					}
					previous_x = x;
				} else if (path == PATH_CIRCLE && frame >= 0.15) {
					metrics->circle_error_sum += (x - tx) * (x - tx) + (y - ty) * (y - ty);
					metrics->circle_error_count += 1.0;
					radius = sqrt(x * x + y * y) - 120.0;
					metrics->circle_radial_sum += radius * radius;
				} else if (path == PATH_STOP && frame >= 0.33) {
					if (x - 472.5 > overshoot)
						overshoot = x - 472.5;
				} else if (path == PATH_ZIGZAG && frame >= 0.15) {
					metrics->zigzag_error_sum += (x - tx) * (x - tx);
					metrics->zigzag_error_count += 1.0;
				}
			}

			/* The judder of a steady scroll: how much the frame steps vary. */
			if (path == PATH_LINE && step_count > 2) {
				mean = 1500.0 * FRAME_PERIOD;
				for (i = 1; i < step_count; i++) {
					metrics->judder_sum += (steps[i] - mean) * (steps[i] - mean);
					metrics->judder_count += 1.0;
				}
			}

			/* The worst overshoot of the stop. */
			if (path == PATH_STOP) {
				metrics->overshoot_sum += overshoot;
				metrics->overshoot_count += 1.0;
			}

			/* The worst step back of the scroll and of the stop. */
			if (path == PATH_LINE || path == PATH_STOP) {
				metrics->retreat_sum += retreat;
				metrics->retreat_count += 1.0;
			}
		}
	}
}

/* Prints the resampling table: one block per condition, one row per rate and method. */
static void
print_resampling(
	void)
{
	struct metrics metrics;
	size_t c;
	size_t r;
	int m;

	printf("# Resampling at 60 Hz frames (%d strokes a cell)\n", RUNS);
	printf("# line-rms: error on a 1500 px/s scroll (px); lag (ms); judder: rms of the frame step's variation (%% of the 25 px step)\n");
	printf("# circle-rms / radial: error on a 120 px circle at 1.5 turns/s (px); overshoot: after a stop from 1500 px/s (px); zigzag-rms (px)\n");
	printf("# retreat: the worst step backwards of a stroke on the scroll and the stop, averaged (px; the finger never goes back)\n");

	/* Each condition, rate and method is one row. */
	for (c = 0; c < sizeof(conditions) / sizeof(conditions[0]); c++) {
		printf("\n## %s: jitter %.0f%%, drop %.0f%%, noise %.1f px, poll %.0f ms, clock %s\n",
		       conditions[c].name, conditions[c].jitter * 100.0, conditions[c].drop * 100.0,
		       conditions[c].noise, conditions[c].poll * 1000.0, conditions[c].clock_ms ? "ms" : "exact");
		printf("%-5s %-11s %9s %7s %8s %11s %7s %10s %11s %8s\n", "rate", "method", "line-rms", "lag", "judder",
		       "circle-rms", "radial", "overshoot", "zigzag-rms", "retreat");
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			for (m = 0; m < METHOD_COUNT; m++) {
				memset(&metrics, 0, sizeof(metrics));
				run_paths(&conditions[c], rates[r], &methods[m], &metrics);
				printf("%-5.0f %-11s %9.2f %7.2f %7.1f%% %11.2f %7.2f %10.2f %11.2f %8.2f\n", rates[r], methods[m].name,
				       sqrt(metrics.line_error_sum / metrics.line_error_count),
				       1000.0 * metrics.lag_sum / metrics.lag_count,
				       100.0 * sqrt(metrics.judder_sum / metrics.judder_count) / (1500.0 * FRAME_PERIOD),
				       sqrt(metrics.circle_error_sum / metrics.circle_error_count),
				       sqrt(metrics.circle_radial_sum / metrics.circle_error_count),
				       metrics.overshoot_sum / metrics.overshoot_count,
				       sqrt(metrics.zigzag_error_sum / metrics.zigzag_error_count),
				       metrics.retreat_sum / metrics.retreat_count);
			}
		}
	}
}

/*
 * Prints the velocity table: the error at a fast and a slow flick and at a
 * flick that slows before the lift, and the false velocity after a pause.
 */
static void
print_velocity(
	void)
{
	static struct report reports[SAMPLES_MAX];
	static double pause[VELOCITY_RUNS];
	struct rng rng;
	double fast_sum;
	double fast_square;
	double slow_sum;
	double ease_sum;
	double value;
	double swap;
	size_t c;
	size_t r;
	int e;
	int run;
	int count;
	int i;
	int j;

	printf("\n# Velocity at the lift (%d strokes a cell)\n", VELOCITY_RUNS);
	printf("# fast: flick to 3000 px/s, mean error and its rms spread (%%); slow: flick to 800 px/s; ease: flick that slows to 2400 px/s in its last 30 ms\n");
	printf("# pause: |v| after 120 ms at rest, 95th and 99th percentile and the largest (px/s; should be 0)\n");

	/* Each condition, rate and estimator is one row. */
	for (c = 1; c < sizeof(conditions) / sizeof(conditions[0]); c++) {
		printf("\n## %s\n", conditions[c].name);
		printf("%-5s %-9s %9s %8s %9s %9s %9s %9s %9s\n", "rate", "estimator", "fast", "spread", "slow", "ease",
		       "pause-p95", "pause-p99", "pause-max");
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			for (e = 0; e < ESTIMATOR_COUNT; e++) {
				fast_sum = 0.0;
				fast_square = 0.0;
				slow_sum = 0.0;
				ease_sum = 0.0;
				device_noise = conditions[c].noise;
				for (run = 0; run < VELOCITY_RUNS; run++) {
					rng.state = 0x243f6a8885a308d3ULL ^ (unsigned long long)(run * 104729 + (int)rates[r]);
					count = generate(&conditions[c], rates[r], PATH_FLING_FAST, &rng, reports);
					value = estimate_velocity((enum estimator)e, reports, count) / 3000.0 - 1.0;
					fast_sum += value;
					fast_square += value * value;
					count = generate(&conditions[c], rates[r], PATH_FLING_SLOW, &rng, reports);
					slow_sum += estimate_velocity((enum estimator)e, reports, count) / 800.0 - 1.0;
					count = generate(&conditions[c], rates[r], PATH_FLING_EASE, &rng, reports);
					ease_sum += estimate_velocity((enum estimator)e, reports, count) / 2400.0 - 1.0;
					count = generate(&conditions[c], rates[r], PATH_PAUSE, &rng, reports);
					pause[run] = fabs(estimate_velocity((enum estimator)e, reports, count));
				}
				for (i = 1; i < VELOCITY_RUNS; i++) {
					for (j = i; j > 0 && pause[j - 1] > pause[j]; j--) {
						swap = pause[j];
						pause[j] = pause[j - 1];
						pause[j - 1] = swap;
					}
				}
				device_noise = 0.0;
				printf("%-5.0f %-9s %8.1f%% %7.1f%% %8.1f%% %8.1f%% %9.0f %9.0f %9.0f\n", rates[r], estimator_names[e],
				       100.0 * fast_sum / VELOCITY_RUNS,
				       100.0 * sqrt(fast_square / VELOCITY_RUNS - (fast_sum / VELOCITY_RUNS) * (fast_sum / VELOCITY_RUNS)),
				       100.0 * slow_sum / VELOCITY_RUNS, 100.0 * ease_sum / VELOCITY_RUNS,
				       pause[(VELOCITY_RUNS * 95) / 100], pause[(VELOCITY_RUNS * 99) / 100], pause[VELOCITY_RUNS - 1]);
			}
		}
	}
}

/*
 * Prints how late a method is when a finger starts to move after a rest
 * on a panel that says nothing while the finger stays still: the mean and
 * the largest lag over the first 200 ms of the motion.
 */
static void
print_gaps(
	void)
{
	static struct report reports[SAMPLES_MAX];
	static const char *const names[] = {"lsq12-e12", "lsq12g-e12"};
	static const size_t condition_indexes[] = {2, 8};
	struct condition condition;
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
	double lag_sum;
	double lag_max;
	double lag_count;
	size_t c;
	size_t r;
	size_t n;
	int m;
	int run;
	int count;
	int seen;

	printf("\n# A start after 400 ms at rest on a panel that is silent while the finger stays still (%d strokes a cell)\n", RUNS);
	printf("%-13s %5s %-11s %9s %9s\n", "condition", "rate", "method", "lag-mean", "lag-max");

	/* Each condition, rate and method is one row. */
	for (c = 0; c < 2; c++) {
		condition = conditions[condition_indexes[c]];
		condition.silent = 1;
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			for (n = 0; n < 2; n++) {
				method = NULL;
				for (m = 0; m < METHOD_COUNT; m++) {
					if (strcmp(methods[m].name, names[n]) == 0)
						method = &methods[m];
				}
				lag_sum = 0.0;
				lag_max = 0.0;
				lag_count = 0.0;
				device_interval = 1.0 / rates[r];
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
						lag_sum += lag;
						lag_count += 1.0;
						if (lag > lag_max)
							lag_max = lag;
					}
				}
				device_interval = 0.0;
				printf("%-13s %5.0f %-11s %8.1f %8.1f\n", condition.name, rates[r], names[n],
				       1000.0 * lag_sum / lag_count, 1000.0 * lag_max);
			}
		}
	}
}

/* Prints how well the median of eight intervals measures the report rate. */
static void
print_rate_estimate(
	void)
{
	static struct report reports[SAMPLES_MAX];
	struct rng rng;
	double interval;
	double error;
	double worst;
	double sum;
	double span_error;
	double span_worst;
	double span_sum;
	size_t c;
	size_t r;
	int run;
	int count;
	int k;
	int n;

	printf("\n# Measured report period against the true period: the median of the last %d intervals, and their mean (the span over %d)\n", RATE_WINDOW, RATE_WINDOW);
	printf("%-11s %5s %11s %11s %11s %11s\n", "condition", "rate", "median-err", "median-max", "span-err", "span-max");

	/* Each condition and rate is one row. */
	for (c = 1; c < sizeof(conditions) / sizeof(conditions[0]); c++) {
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			sum = 0.0;
			worst = 0.0;
			span_sum = 0.0;
			span_worst = 0.0;
			n = 0;
			for (run = 0; run < RUNS; run++) {
				rng.state = 0x13198a2e03707344ULL ^ (unsigned long long)(run * 31 + (int)rates[r]);
				count = generate(&conditions[c], rates[r], PATH_LINE, &rng, reports);
				for (k = RATE_WINDOW + 1; k <= count; k++) {
					interval = median_interval(reports, k);
					error = fabs(interval * rates[r] - 1.0);
					sum += error;
					if (error > worst)
						worst = error;
					span_error = fabs((reports[k - 1].stamp - reports[k - 1 - RATE_WINDOW].stamp) / RATE_WINDOW * rates[r] - 1.0);
					span_sum += span_error;
					if (span_error > span_worst)
						span_worst = span_error;
					n++;
				}
			}
			printf("%-11s %5.0f %10.1f%% %10.1f%% %10.1f%% %10.1f%%\n", conditions[c].name, rates[r], 100.0 * sum / n,
			       100.0 * worst, 100.0 * span_sum / n, 100.0 * span_worst);
		}
	}
}

/*
 * Prints how far a drawn stroke strays from a true circle when it joins the
 * reports with straight lines or with a centripetal Catmull-Rom spline.
 */
static void
print_strokes(
	void)
{
	static struct report reports[SAMPLES_MAX];
	static const struct condition clean = {"clean", 0.10, 0.0, 0.0, STAMP_SCAN, 0.001, 0, 0};
	static const struct condition noisy = {"noisy", 0.10, 0.0, 1.0, STAMP_SCAN, 0.001, 0, 0};
	const struct condition *condition;
	struct rng rng;
	double p[4][2];
	double tk[4];
	double a1[2];
	double a2[2];
	double a3[2];
	double b1[2];
	double b2[2];
	double point[2];
	double u;
	double t;
	double d;
	double line_max;
	double line_sum;
	double spline_max;
	double spline_sum;
	double samples;
	double dist;
	size_t r;
	int which;
	int run;
	int count;
	int k;
	int s;
	int i;
	int j;

	printf("\n# Notes stroke: a circle of 80 px at 2 turns/s (1005 px/s), drawn between the reports\n");
	printf("%-6s %5s %11s %11s %13s %13s\n", "noise", "rate", "line-rms", "line-max", "spline-rms", "spline-max");

	/* Each noise and rate is one row. */
	for (which = 0; which < 2; which++) {
		condition = which == 0 ? &clean : &noisy;
		for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
			line_max = 0.0;
			line_sum = 0.0;
			spline_max = 0.0;
			spline_sum = 0.0;
			samples = 0.0;
			for (run = 0; run < RUNS / 4; run++) {
				rng.state = 0xa4093822299f31d0ULL ^ (unsigned long long)(run * 17 + (int)rates[r]);
				count = generate(condition, rates[r], PATH_STROKE, &rng, reports);

				/* Measures every segment with a report on each side of it. */
				for (k = 1; k + 2 < count; k++) {
					for (i = 0; i < 4; i++) {
						p[i][0] = reports[k - 1 + i].x;
						p[i][1] = reports[k - 1 + i].y;
					}
					tk[0] = 0.0;
					for (i = 1; i < 4; i++) {
						d = hypot(p[i][0] - p[i - 1][0], p[i][1] - p[i - 1][1]);
						if (d < 1e-6)
							d = 1e-6;
						tk[i] = tk[i - 1] + sqrt(d);
					}
					for (s = 1; s < 16; s++) {
						u = (double)s / 16.0;

						/* The straight line between the two middle reports. */
						point[0] = p[1][0] + u * (p[2][0] - p[1][0]);
						point[1] = p[1][1] + u * (p[2][1] - p[1][1]);
						dist = fabs(hypot(point[0], point[1]) - 80.0);
						line_sum += dist * dist;
						if (dist > line_max)
							line_max = dist;

						/* The centripetal Catmull-Rom point (Barry-Goldman). */
						t = tk[1] + u * (tk[2] - tk[1]);
						for (j = 0; j < 2; j++) {
							a1[j] = (tk[1] - t) / (tk[1] - tk[0]) * p[0][j] + (t - tk[0]) / (tk[1] - tk[0]) * p[1][j];
							a2[j] = (tk[2] - t) / (tk[2] - tk[1]) * p[1][j] + (t - tk[1]) / (tk[2] - tk[1]) * p[2][j];
							a3[j] = (tk[3] - t) / (tk[3] - tk[2]) * p[2][j] + (t - tk[2]) / (tk[3] - tk[2]) * p[3][j];
							b1[j] = (tk[2] - t) / (tk[2] - tk[0]) * a1[j] + (t - tk[0]) / (tk[2] - tk[0]) * a2[j];
							b2[j] = (tk[3] - t) / (tk[3] - tk[1]) * a2[j] + (t - tk[1]) / (tk[3] - tk[1]) * a3[j];
							point[j] = (tk[2] - t) / (tk[2] - tk[1]) * b1[j] + (t - tk[1]) / (tk[2] - tk[1]) * b2[j];
						}
						dist = fabs(hypot(point[0], point[1]) - 80.0);
						spline_sum += dist * dist;
						if (dist > spline_max)
							spline_max = dist;
						samples += 1.0;
					}
				}
			}
			printf("%-6s %5.0f %11.2f %11.2f %13.2f %13.2f\n", condition->name, rates[r], sqrt(line_sum / samples),
			       line_max, sqrt(spline_sum / samples), spline_max);
		}
	}
}
