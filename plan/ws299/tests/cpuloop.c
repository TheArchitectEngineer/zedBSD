/*
 * cpuloop: how fast one thread computes and copies, the same program on
 * zedBSD and Linux, to see whether two systems run the CPU at the same speed.
 *
 *	cpuloop [MILLIONS]
 *
 * Prints these lines: a dependent xorshift chain of MILLIONS million steps
 * (default 400; the core's clock alone, with the APIC ID and the hybrid
 * core type before and after it), the fill of directio and patwrite
 * (1 MiB of numbered 4 KiB blocks, 256 times), the same fill hot and right
 * after a 0.5 ms sleep (TSC-timed, per MiB, with how often the thread woke
 * on another CPU and on a performance core), and the C library's memcpy at
 * sizes from 8 bytes to 32 MiB (ns per call and MB/s).  Each line is "cpuloop NAME ms=T rate=R".
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MIB	(1024U * 1024U)
#define BLOCK	4096U

static volatile uint64_t sink;

/*
 * Where the thread runs now: the initial APIC ID (CPUID 1, EBX[31:24]) and,
 * on a hybrid Intel part, the core type (CPUID 0x1A, EAX[31:24]: 0x40 a
 * performance core, 0x20 an efficient one; 0 elsewhere).
 */
static void
where(unsigned *apic, unsigned *type)
{
#if defined(__x86_64__)
	unsigned a, b, c, d;

	__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0U), "c"(0U));
	*type = 0;
	if (a >= 0x1aU) {
		__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x1aU), "c"(0U));
		*type = a >> 24;
	}
	__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1U), "c"(0U));
	*apic = b >> 24;
#else
	*apic = 0;
	*type = 0;
#endif
}

static uint64_t
now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000U + (uint64_t)now.tv_nsec;
}

/* The time stamp counter (constant rate on the parts measured). */
static uint64_t
tsc(void)
{
#if defined(__x86_64__)
	unsigned lo, hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
#else
	return now_ns();
#endif
}

/* The same fill as directio.c and patwrite.c. */
static void
fill(unsigned char *buffer, size_t length, uint64_t seed, uint64_t number)
{
	uint64_t state;
	size_t at;

	/*
	 * Whole words with a copy of a constant size, which the compiler makes a
	 * store (a copy of a variable size calls the C library's memcpy for
	 * every word, which measures the library and not the I/O), then the
	 * tail.  The bytes are the same either way.
	 */
	state = seed * 0x9e3779b97f4a7c15ULL + 1U;
	for (at = 0; at + 8 <= length; at += 8) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memcpy(buffer + at, &state, 8);
	}
	if (at < length) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memcpy(buffer + at, &state, length - at);
	}
	for (at = 0; at + 8 <= length; at += BLOCK) {
		memcpy(buffer + at, &number, 8);
		number++;
	}
}

int
main(int argc, char **argv)
{
	unsigned long millions;
	unsigned char *a;
	unsigned char *b;
	uint64_t state;
	uint64_t started;
	uint64_t elapsed;
	unsigned long i;
	unsigned apic0, type0, apic1, type1;

	millions = argc > 1 ? strtoul(argv[1], NULL, 0) : 400;

	/* A dependent chain: three shifts and three xors per step. */
	state = 1;
	where(&apic0, &type0);
	started = now_ns();
	for (i = 0; i < millions * 1000000UL; i++) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
	}
	sink = state;
	elapsed = now_ns() - started;
	where(&apic1, &type1);
	printf("cpuloop chain ms=%llu rate=%.1f Msteps/s apic=%u/%u type=0x%x/0x%x\n",
	    (unsigned long long)(elapsed / 1000000U),
	    (double)millions * 1e6 / ((double)elapsed / 1e9) / 1e6, apic0, apic1, type0, type1);
	fflush(stdout);

	/* The fill the write tools do before every 1 MiB request. */
	a = malloc(64 * MIB);
	b = malloc(64 * MIB);
	if (a == NULL || b == NULL)
		return 1;
	memset(a, 1, 64 * MIB);
	memset(b, 2, 64 * MIB);
	started = now_ns();
	for (i = 0; i < 256; i++)
		fill(a + (i % 64) * MIB, MIB, 7, i * 256);
	elapsed = now_ns() - started;
	sink = a[12345];
	printf("cpuloop fill ms=%llu rate=%.1f MB/s\n", (unsigned long long)(elapsed / 1000000U),
	    256.0 * MIB / 1e6 / ((double)elapsed / 1e9));

	/*
	 * The same fill right after a short sleep, as a writer fills its next
	 * request after waiting for the device: the core may have idled deep,
	 * lost its caches or its clock, or the thread may wake on another core.
	 * Timed with the TSC (the system clock may tick in milliseconds).
	 */
	{
		uint64_t tsc0, ns0, hz, hot, cold, t, slept;
		unsigned moved, ptype, pcount, apic, type, lastapic;

		tsc0 = tsc();
		ns0 = now_ns();
		hot = 0;
		for (i = 0; i < 64; i++) {
			t = tsc();
			fill(a, MIB, 7, i * 256);
			hot += tsc() - t;
		}
		cold = 0;
		slept = 0;
		moved = 0;
		pcount = 0;
		ptype = 0;
		where(&lastapic, &type);
		for (i = 0; i < 256; i++) {
			t = tsc();
			usleep(500);
			slept += tsc() - t;
			where(&apic, &type);
			if (apic != lastapic)
				moved++;
			if (type == 0x40U)
				pcount++;
			ptype = type;
			t = tsc();
			fill(a, MIB, 7, i * 256);
			cold += tsc() - t;
			where(&lastapic, &type);
		}
		(void)ptype;
		hz = (tsc() - tsc0) * 1000000000ULL / (now_ns() - ns0 + 1U);
		printf("cpuloop fillsleep tsc_mhz=%llu slept_us=%llu hot_us=%llu after_sleep_us=%llu woke_elsewhere=%u/256 on_pcore=%u/256\n",
		    (unsigned long long)(hz / 1000000U),
		    (unsigned long long)(slept / 256U * 1000000U / (hz + 1U)),
		    (unsigned long long)(hot / 64U * 1000000U / (hz + 1U)),
		    (unsigned long long)(cold / 256U * 1000000U / (hz + 1U)), moved, pcount);
		fflush(stdout);
	}

	/*
	 * The C library's memcpy at sizes the compiler cannot see (a volatile
	 * size), hot in the cache up to 1 MiB: ns per call, and MB/s from 4 KiB.
	 */
	{
		static const size_t sizes[] = { 8, 16, 32, 64, 128, 256, 1024, 4096, 65536, 1048576, 33554432 };
		volatile size_t size;
		unsigned long calls;
		unsigned s;

		for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
			size = sizes[s];
			calls = 512UL * 1048576UL / (sizes[s] < 64 ? 64 : sizes[s]);
			if (calls > 16UL * 1048576UL)
				calls = 16UL * 1048576UL;
			if (calls < 16)
				calls = 16;
			started = now_ns();
			for (i = 0; i < calls; i++)
				memcpy(b + (sizes[s] <= 4096 ? (i % 64U) * 8U : 0), a + (sizes[s] <= 4096 ? 4096 : 0), size);
			elapsed = now_ns() - started;
			sink = b[17];
			printf("cpuloop memcpy size=%zu ns_per_call=%.2f MBps=%.0f\n", sizes[s],
			    (double)elapsed / (double)calls, (double)sizes[s] * (double)calls / 1e6 / ((double)elapsed / 1e9));
			fflush(stdout);
		}
	}
	return 0;
}
