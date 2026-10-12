#!/bin/sh
# Host test of the arm64 stall watch's judgement (ws208-p007, T1-555).
# time_since() and cpu_is_stopped() are cut out of src/hal/arm64/watch.c and
# compiled against a fake per-CPU record with the fields they read:
#  - a heartbeat stamped after the judging CPU's now (another CPU took its
#    tick between the judge's counter read and its look) is fresh, not a
#    wrapped age of 2^64 ticks (the false "cpu1 STOPPED tick -295147905179s");
#  - a CPU that is not up, has not taken its first tick, or is parked is
#    not stopped;
#  - a heartbeat within the threshold is not a stop, one at or past it is
#    (the true stop the real Pi showed stays detected);
#  - time_since() of an older time is the plain difference.
# Usage: plan/ws208/tests/watch-judge-host-test.sh [build-dir]  (from the tree root)
# Last line: watch-judge-host-test: PASS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws208-p007-host}
mkdir -p "$out"

# Cuts a function (its return-type line and its body) out of a source file.
cut_function() {
	awk -v name="$2" '$0 ~ "^"name"\\(" {p=1; print previous} p{print} p&&/^}/{exit} {previous=$0}' "$1"
}

{
	cut_function src/hal/arm64/watch.c time_since
	cut_function src/hal/arm64/watch.c cpu_is_stopped
} > "$out/watch-judge.inc"

cat > "$out/watch-judge-main.c" <<'EOT'
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The fields of the arm64 per-CPU record that the judgement reads. */
struct arm64_percpu {
	volatile unsigned ready;
	uint64_t watch_beat_time;
	unsigned watch_parked;
};

static uint64_t time_since(uint64_t now, uint64_t then);
static int cpu_is_stopped(struct arm64_percpu *cpu, uint64_t now, uint64_t threshold);

#include "watch-judge.inc"

static int failures;

static void
expect(const char *what, uint64_t got, uint64_t want)
{
	if (got == want)
		return;
	printf("FAIL %s: got %llu want %llu\n", what, (unsigned long long)got, (unsigned long long)want);
	failures++;
}

int
main(void)
{
	/* 62.5 MHz as QEMU raspi4b reports, 2 s threshold. */
	const uint64_t frequency = 62500000U;
	const uint64_t threshold = 2U * frequency;
	const uint64_t now = 22U * frequency + 788U * (frequency / 1000U);
	struct arm64_percpu cpu;

	/* time_since: older, equal, newer, zero. */
	expect("age of an older time", time_since(now, now - 5U), 5U);
	expect("age of now", time_since(now, now), 0U);
	expect("age of a later time", time_since(now, now + 40000U), 0U);
	expect("age of time zero", time_since(now, 0U), now);

	/* The T1-555 race: another CPU stamped its tick just after now. */
	cpu.ready = 1;
	cpu.watch_parked = 0;
	cpu.watch_beat_time = now + 40000U;
	expect("beat after now is not a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 0U);

	/* Before the CPU is up. */
	cpu.ready = 0;
	cpu.watch_beat_time = now - 10U * frequency;
	expect("CPU not up is not a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 0U);

	/* Up, but no tick yet. */
	cpu.ready = 1;
	cpu.watch_beat_time = 0;
	expect("CPU before its first tick is not a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 0U);

	/* Parked on purpose with an old heartbeat. */
	cpu.watch_beat_time = now - 10U * frequency;
	cpu.watch_parked = 1;
	expect("parked CPU is not a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 0U);

	/* Recent heartbeat. */
	cpu.watch_parked = 0;
	cpu.watch_beat_time = now - frequency / 1000U;
	expect("1 ms old beat is not a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 0U);
	cpu.watch_beat_time = now - threshold + 1U;
	expect("beat just inside the threshold is not a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 0U);

	/* A true stop. */
	cpu.watch_beat_time = now - threshold;
	expect("beat at the threshold is a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 1U);
	cpu.watch_beat_time = now - 10U * frequency;
	expect("10 s old beat is a stop", (uint64_t)cpu_is_stopped(&cpu, now, threshold), 1U);

	if (failures != 0) {
		printf("watch-judge-host-test: FAIL (%d)\n", failures);
		return 1;
	}
	printf("watch-judge-host-test: PASS\n");
	return 0;
}
EOT

cc -std=c99 -Wall -Wextra -Werror -o "$out/watch-judge" "$out/watch-judge-main.c"
"$out/watch-judge"
