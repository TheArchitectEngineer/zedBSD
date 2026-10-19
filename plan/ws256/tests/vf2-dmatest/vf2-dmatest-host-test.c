/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the DMA coherency procedure (vf2-dmatest-core.c).
 *
 * The model has memory, a write-back cache of 64-byte lines over the 2 MiB
 * region (every line may stay; nothing is evicted) and an uncached alias.
 * The bus master's reads and writes may each snoop the cache or not, the
 * alias's writes may or may not drop cached lines, the alias may reach
 * other memory, and a copy may fail.
 */

#include <stdio.h>
#include <string.h>

#include <drivers/platform/visionfive2/vf2-dmatest-core.h>

#define LINE		64U
#define LINES		(DRV_VF2_DMATEST_REGION / LINE)
#define WORDS		(DRV_VF2_DMATEST_REGION / 8U)

/* One model: its switches and its state. */
struct model {
	int snoop_reads;	/* device reads see dirty lines */
	int snoop_writes;	/* device writes update or drop cached lines */
	int alias_drops;	/* alias writes drop cached lines */
	int alias_elsewhere;	/* the alias reaches other memory */
	int copy_error;		/* every copy fails with this */
	uint64_t memory[WORDS];
	uint64_t other[WORDS];
	uint64_t cache[WORDS];
	unsigned char valid[LINES];
	unsigned char dirty[LINES];
	unsigned flushes_of_region;
};

static struct model m;
static int failures;

static void
write_back(size_t line)
{
	if (m.valid[line] && m.dirty[line])
		memcpy(&m.memory[line * LINE / 8U], &m.cache[line * LINE / 8U], LINE);
	m.dirty[line] = 0;
}

static void
fill_line(size_t line)
{
	if (!m.valid[line]) {
		memcpy(&m.cache[line * LINE / 8U], &m.memory[line * LINE / 8U], LINE);
		m.valid[line] = 1;
		m.dirty[line] = 0;
	}
}

static uint64_t
load(void *context, int view, size_t offset)
{
	(void)context;
	if (view == DRV_VF2_DMATEST_ALIAS) {
		if (m.alias_elsewhere)
			return m.other[offset / 8U];
		return m.memory[offset / 8U];
	}
	fill_line(offset / LINE);
	return m.cache[offset / 8U];
}

static void
store(void *context, int view, size_t offset, uint64_t value)
{
	(void)context;
	if (view == DRV_VF2_DMATEST_ALIAS) {
		if (m.alias_elsewhere) {
			m.other[offset / 8U] = value;
			return;
		}
		m.memory[offset / 8U] = value;
		if (m.alias_drops)
			m.valid[offset / LINE] = 0;
		return;
	}
	fill_line(offset / LINE);
	m.cache[offset / 8U] = value;
	m.dirty[offset / LINE] = 1;
}

static void
flush(void *context, size_t offset, size_t size)
{
	size_t line;

	(void)context;
	if (offset == 0 && size == DRV_VF2_DMATEST_REGION)
		m.flushes_of_region++;
	for (line = offset / LINE; line < (offset + size + LINE - 1U) / LINE; line++) {
		write_back(line);
		m.valid[line] = 0;
	}
}

static int
copy(void *context, size_t destination, size_t source, size_t size)
{
	size_t word;
	uint64_t value;
	size_t s;
	size_t d;

	(void)context;
	if (m.copy_error != 0)
		return m.copy_error;
	for (word = 0; word < size / 8U; word++) {
		s = source / 8U + word;
		d = destination / 8U + word;
		value = m.memory[s];
		if (m.snoop_reads && m.valid[s * 8U / LINE] && m.dirty[s * 8U / LINE])
			value = m.cache[s];
		m.memory[d] = value;
		if (m.snoop_writes && m.valid[d * 8U / LINE]) {
			m.cache[d] = value;
		}
	}
	return 0;
}

static void
run_case(const char *name, int reads, int writes, int drops, int elsewhere, int error, const char *expect[3])
{
	struct drv_vf2_dmatest_ops ops;
	struct drv_vf2_dmatest_result result;
	char text[256];
	size_t length;
	size_t line;
	int left;
	int ok;
	unsigned i;
	const char *p;

	memset(&m, 0, sizeof(m));
	m.snoop_reads = reads;
	m.snoop_writes = writes;
	m.alias_drops = drops;
	m.alias_elsewhere = elsewhere;
	m.copy_error = error;
	ops.context = NULL;
	ops.load = load;
	ops.store = store;
	ops.flush = flush;
	ops.copy = copy;
	drv_vf2_dmatest_run(&ops, &result);
	length = drv_vf2_dmatest_format(&result, "dmac", text, sizeof(text));

	ok = length < sizeof(text);
	p = text;
	for (i = 0; i < 3; i++) {
		if (strncmp(p, expect[i], strlen(expect[i])) != 0)
			ok = 0;
		p = strchr(p, '\n');
		if (p == NULL) {
			ok = 0;
			break;
		}
		p++;
	}

	/* The last flush leaves no line of the region in the cache. */
	left = 0;
	for (line = 0; line < LINES; line++)
		left += m.valid[line];
	if (left != 0 || m.flushes_of_region < 2U)
		ok = 0;

	printf("%s %s\n%s", ok ? "PASS" : "FAIL", name, text);
	if (!ok)
		failures++;
}

int
main(void)
{
	static const char *coherent[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL OK", "DMATEST dmac PASS COHERENT runs=8 bytes=65536"};
	static const char *both[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL OK", "DMATEST dmac FAIL run=0 offset=0x0 view=cached NONCOHERENT direction="};
	static const char *to_device[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL OK", "DMATEST dmac FAIL run=0 offset=0x0 view=cached NONCOHERENT direction=cpu-to-device mismatches=16"};
	static const char *to_cpu[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL OK", "DMATEST dmac FAIL run=0 offset=0x0 view=cached NONCOHERENT direction=device-to-cpu mismatches=16"};
	static const char *inconclusive_ok[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL INCONCLUSIVE", "DMATEST dmac PASS COHERENT"};
	static const char *inconclusive_bad[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL INCONCLUSIVE", "DMATEST dmac FAIL run=0 offset=0x0 view=cached NONCOHERENT direction=unclassified"};
	static const char *mismatch_ok[3] = {"DMATEST dmac ALIAS MISMATCH", "DMATEST dmac CONTROL SKIPPED", "DMATEST dmac PASS COHERENT"};
	static const char *mismatch_bad[3] = {"DMATEST dmac ALIAS MISMATCH", "DMATEST dmac CONTROL SKIPPED", "DMATEST dmac FAIL run=0 offset=0x0 view=cached NONCOHERENT direction=unclassified"};
	static const char *copy_failed[3] = {"DMATEST dmac ALIAS OK", "DMATEST dmac CONTROL OK", "DMATEST dmac FAIL copy run=0 error=60"};
	char small[16];
	struct drv_vf2_dmatest_result r;

	run_case("coherent", 1, 1, 0, 0, 0, coherent);
	run_case("noncoherent-both", 0, 0, 0, 0, 0, both);
	run_case("noncoherent-cpu-to-device", 0, 1, 0, 0, 0, to_device);
	run_case("noncoherent-device-to-cpu", 1, 0, 0, 0, 0, to_cpu);
	run_case("alias-drops-coherent", 1, 1, 1, 0, 0, inconclusive_ok);
	run_case("alias-drops-noncoherent", 1, 0, 1, 0, 0, inconclusive_bad);
	run_case("alias-elsewhere-coherent", 1, 1, 0, 1, 0, mismatch_ok);
	run_case("alias-elsewhere-noncoherent", 1, 0, 0, 1, 0, mismatch_bad);
	run_case("copy-error", 1, 1, 0, 0, 60, copy_failed);

	/* A small buffer is cut and terminated. */
	memset(&r, 0, sizeof(r));
	if (drv_vf2_dmatest_format(&r, "dmac", small, sizeof(small)) < sizeof(small) || small[15] != '\0' || strncmp(small, "DMATEST dmac AL", 15) != 0) {
		printf("FAIL format-cut\n");
		failures++;
	} else {
		printf("PASS format-cut\n");
	}

	printf("%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
	return failures != 0;
}
