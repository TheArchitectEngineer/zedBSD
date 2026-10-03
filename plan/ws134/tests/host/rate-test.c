/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws134-p012: host tests of libkeiland's monitor rates
 * (userland/desktop/libkeiland/system/system-monitor-rate.c): the CPUs'
 * shares, the links' and disks' rates matched by id (not place), the mean
 * latency, a device that is new or whose counter went back has no rate,
 * and the GPUs' busy share.  The verdict goes to standard output.
 *
 *	plan/ws134/tests/host/run.sh
 */

#include "system-private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The number of failed checks. */
static int failures;

/* The samples and the frame of each test. */
static struct system_monitor_raw before;
static struct system_monitor_raw after;
static struct kl_monitor_frame frame;

static void check(int condition, const char *what);
static int near(double value, double want);
static void test_cpus(void);
static void test_links(void);
static void test_disks(void);
static void test_gpus(void);

/* Runs every test. */
int
main(void)
{
	/* Each part. */
	test_cpus();
	test_links();
	test_disks();
	test_gpus();

	/* The verdict. */
	if (failures != 0) {
		printf("monitor-rate: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check held. */
	printf("monitor-rate: PASS\n");

	/* Succeeded: the run is over. */
	return 0;
}

/* Counts and prints a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that held says so. */
	if (condition) {
		printf("ok: %s\n", what);
		return;
	}

	/* A failure is counted. */
	printf("FAILED: %s\n", what);
	failures++;
}

/* Whether a value is within a millionth of another. */
static int
near(
	double value,
	double want)
{
	double difference;

	/* Close enough. */
	difference = fabs(value - want);
	if (difference < 1e-6)
		return 1;

	/* Not. */
	return 0;
}

/* Two CPUs over 2 s at 100 Hz: one 75% busy, one idle; a CPU whose counter went back has no share. */
static void
test_cpus(void)
{
	/* The samples 2 s apart. */
	memset(&before, 0, sizeof(before));
	memset(&after, 0, sizeof(after));
	before.time_ns = 10000000000ULL;
	after.time_ns = 12000000000ULL;
	before.cpu_count = 3;
	after.cpu_count = 3;

	/* CPU 0: 150 of 200 ticks busy (100 user, 50 system). */
	before.cpu[0].user = 1000;
	before.cpu[0].system = 500;
	before.cpu[0].idle = 9000;
	after.cpu[0].user = 1100;
	after.cpu[0].system = 550;
	after.cpu[0].idle = 9050;

	/* CPU 1: all idle. */
	before.cpu[1].idle = 5000;
	after.cpu[1].idle = 5200;

	/* CPU 2: its idle went back. */
	before.cpu[2].idle = 5000;
	after.cpu[2].idle = 10;
	system_monitor_rates(&before, &after, &frame);
	check(near(frame.seconds, 2.0), "two seconds between the samples");
	check(near(frame.cpu_core[0], 0.75), "CPU 0 is 75% busy");
	check(near(frame.cpu_core[1], 0.0), "CPU 1 is idle");
	check(near(frame.cpu_core[2], 0.0), "CPU 2 whose counter went back has no share");
	check(near(frame.cpu, 150.0 / 400.0), "the whole is 150 of 400 ticks busy (CPU 2 left out)");
}

/* Links matched by id in another order; a new link and one whose counter went back have no rate. */
static void
test_links(void)
{
	/* The samples 1 s apart. */
	memset(&before, 0, sizeof(before));
	memset(&after, 0, sizeof(after));
	before.time_ns = 1000000000ULL;
	after.time_ns = 2000000000ULL;

	/* Before: links 7 and 9. */
	before.link_count = 2;
	before.link[0].id = 7;
	before.link[0].rx = 1000;
	before.link[0].tx = 100;
	before.link[1].id = 9;
	before.link[1].rx = 5000;
	before.link[1].tx = 5000;

	/* After: 9 first (gone back), 7, and a new 11. */
	after.link_count = 3;
	after.link[0].id = 9;
	after.link[0].rx = 10;
	after.link[0].tx = 6000;
	after.link[1].id = 7;
	after.link[1].rx = 3000;
	after.link[1].tx = 400;
	after.link[2].id = 11;
	after.link[2].rx = 999999;
	system_monitor_rates(&before, &after, &frame);
	check(frame.link_count == 3U, "three links");
	check(frame.link[1].id == 7U && near(frame.link[1].rx_rate, 2000.0) && near(frame.link[1].tx_rate, 300.0), "link 7 matched by id: 2000 and 300 B/s");
	check(near(frame.link[0].rx_rate, 0.0) && near(frame.link[0].tx_rate, 0.0), "link 9 whose counter went back has no rate");
	check(near(frame.link[2].rx_rate, 0.0), "the new link 11 has no rate yet");
	check(near(frame.rx_rate, 2000.0) && near(frame.tx_rate, 300.0), "the sums are link 7's");
}

/* A disk's rates and its mean latency, and the latency over two disks. */
static void
test_disks(void)
{
	/* The samples 2 s apart. */
	memset(&before, 0, sizeof(before));
	memset(&after, 0, sizeof(after));
	before.time_ns = 0;
	after.time_ns = 2000000000ULL;
	before.disk_count = 2;
	after.disk_count = 2;

	/* Disk 1: 100 reads of 4 MiB in all, 50 writes, 150 ms of time, 1 s busy. */
	before.disk[0].id = 1;
	after.disk[0].id = 1;
	after.disk[0].read_ops = 100;
	after.disk[0].write_ops = 50;
	after.disk[0].read_bytes = 4194304;
	after.disk[0].write_bytes = 1048576;
	after.disk[0].read_ns = 100000000;
	after.disk[0].write_ns = 50000000;
	after.disk[0].busy_ns = 1000000000;

	/* Disk 2: 50 reads of 250 ms in all. */
	before.disk[1].id = 2;
	after.disk[1].id = 2;
	after.disk[1].read_ops = 50;
	after.disk[1].read_ns = 250000000;
	system_monitor_rates(&before, &after, &frame);
	check(near(frame.disk[0].read_rate, 2097152.0) && near(frame.disk[0].write_rate, 524288.0), "disk 1 reads 2 MiB/s and writes 512 KiB/s");
	check(near(frame.disk[0].read_ops, 50.0) && near(frame.disk[0].write_ops, 25.0), "disk 1 does 50 and 25 operations a second");
	check(near(frame.disk[0].latency_ms, 1.0), "disk 1's mean latency is 1 ms");
	check(near(frame.disk[0].busy, 0.5), "disk 1 is busy half the time");
	check(near(frame.disk[1].latency_ms, 5.0), "disk 2's mean latency is 5 ms");
	check(near(frame.disk_latency_ms, 400.0 / 200.0), "the mean over every operation is 2 ms");
	check(near(frame.read_rate, 2097152.0), "the read sum");
}

/* A GPU's busy share over its driver's clock, and its present values. */
static void
test_gpus(void)
{
	/* One GPU, busy 300 ms of 1 s of its clock. */
	memset(&before, 0, sizeof(before));
	memset(&after, 0, sizeof(after));
	before.time_ns = 0;
	after.time_ns = 1000000000ULL;
	before.gpu_count = 1;
	after.gpu_count = 1;
	before.gpu[0].id = 1;
	before.gpu[0].time_ns = 5000000000ULL;
	before.gpu[0].busy_ns = 1000000000ULL;
	after.gpu[0].id = 1;
	after.gpu[0].time_ns = 6000000000ULL;
	after.gpu[0].busy_ns = 1300000000ULL;
	after.gpu[0].cur_mhz = 1200;
	system_monitor_rates(&before, &after, &frame);
	check(near(frame.gpu[0].busy, 0.3), "the GPU is 30% busy");
	check(frame.gpu[0].cur_mhz == 1200U, "its frequency is the present one");
}
