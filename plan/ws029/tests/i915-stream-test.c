/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the native command stream validator with well-formed and
 * malformed streams.
 */

#include "i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"
#include "../../../src/drivers/gpu/i915/gem.c"
#include "../../../src/drivers/gpu/i915/irq.c"
#include "../../../src/drivers/gpu/i915/engine.c"
#include "../../../src/drivers/gpu/i915/lrc.c"
#include "../../../src/drivers/gpu/i915/request.c"
#include "../../../src/drivers/gpu/i915/i915.c"

/* One stream buffer large enough for the largest accepted stream. */
static uint8_t stream[I915_STREAM_HEADER_BYTES + I915_STREAM_MAX_RELOCATIONS * I915_STREAM_RELOCATION_BYTES + I915_STREAM_MAX_DWORDS * 4U + 16U];

static uint32_t build(uint32_t engine, uint32_t relocations, uint32_t dwords, uint32_t flags, uint32_t last);
static void put32(uint8_t *at, uint32_t value);
static void test_valid(void);
static void test_invalid(void);

int
main(void)
{
	test_valid();
	test_invalid();
	printf("i915 stream host test PASS\n");
	return 0;
}

/* Stores one little-endian word. */
static void
put32(
	uint8_t *at,
	uint32_t value)
{
	memcpy(at, &value, 4U);
}

/* Builds a stream with the given shape and returns its byte count. */
static uint32_t
build(
	uint32_t engine,
	uint32_t relocations,
	uint32_t dwords,
	uint32_t flags,
	uint32_t last)
{
	uint32_t bytes;
	uint32_t index;
	uint8_t *batch;

	memset(stream, 0, sizeof(stream));
	put32(stream, I915_STREAM_MAGIC);
	put32(stream + 4U, I915_STREAM_VERSION);
	put32(stream + 8U, engine);
	put32(stream + 12U, relocations);
	put32(stream + 16U, dwords);
	put32(stream + 20U, flags);
	for (index = 0U; index < relocations; index++) {
		put32(stream + I915_STREAM_HEADER_BYTES + index * I915_STREAM_RELOCATION_BYTES, index * 2U);
		memset(stream + I915_STREAM_HEADER_BYTES + index * I915_STREAM_RELOCATION_BYTES + 8U, 0x11, 8U);
	}
	batch = stream + I915_STREAM_HEADER_BYTES + relocations * I915_STREAM_RELOCATION_BYTES;
	for (index = 0U; index < dwords; index++)
		put32(batch + index * 4U, MI_NOOP);
	if (dwords != 0U)
		put32(batch + (dwords - 1U) * 4U, last);
	bytes = I915_STREAM_HEADER_BYTES + relocations * I915_STREAM_RELOCATION_BYTES + dwords * 4U;
	return bytes;
}

/* Well-formed streams for both engines report their relocation and batch locations. */
static void
test_valid(void)
{
	struct i915_stream parsed;
	uint32_t bytes;
	int error;

	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	error = drv_i915_stream_parse(stream, bytes, &parsed);
	assert(error == 0);
	assert(parsed.engine == I915_STREAM_ENGINE_BCS0);
	assert(parsed.relocation_count == 0U && parsed.batch_dwords == 1U);
	assert(parsed.batch == (const uint32_t *)(stream + I915_STREAM_HEADER_BYTES));

	bytes = build(I915_STREAM_ENGINE_RCS0, 3U, 8U, 0U, MI_BATCH_BUFFER_END);
	error = drv_i915_stream_parse(stream, bytes, &parsed);
	assert(error == 0);
	assert(parsed.engine == I915_STREAM_ENGINE_RCS0);
	assert(parsed.relocation_count == 3U && parsed.batch_dwords == 8U);
	assert(parsed.relocations == stream + I915_STREAM_HEADER_BYTES);
	assert(parsed.batch == (const uint32_t *)(stream + I915_STREAM_HEADER_BYTES + 3U * I915_STREAM_RELOCATION_BYTES));

	/* The largest shape is accepted exactly. */
	bytes = build(I915_STREAM_ENGINE_BCS0, I915_STREAM_MAX_RELOCATIONS, I915_STREAM_MAX_DWORDS, 0U, MI_BATCH_BUFFER_END);
	put32(stream + I915_STREAM_HEADER_BYTES + (I915_STREAM_MAX_RELOCATIONS - 1U) * I915_STREAM_RELOCATION_BYTES, I915_STREAM_MAX_DWORDS - 2U);
	error = drv_i915_stream_parse(stream, bytes, &parsed);
	assert(error == 0);
}

/* Every malformed field is refused with EINVAL and reports nothing. */
static void
test_invalid(void)
{
	struct i915_stream parsed;
	uint32_t bytes;
	int error;

	/* Too short for a header. */
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	error = drv_i915_stream_parse(stream, I915_STREAM_HEADER_BYTES - 1U, &parsed);
	assert(error == EINVAL);
	assert(parsed.batch == NULL);

	/* Wrong magic, version, engine and flags. */
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	put32(stream, 0x31394957U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	put32(stream + 4U, 2U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(2U, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 1U, MI_BATCH_BUFFER_END);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	put32(stream + 28U, 1U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);

	/* Counts outside their bounds. */
	bytes = build(I915_STREAM_ENGINE_BCS0, I915_STREAM_MAX_RELOCATIONS + 1U, 1U, 0U, MI_BATCH_BUFFER_END);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 0U, 0U, MI_BATCH_BUFFER_END);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 1U, 0U, MI_BATCH_BUFFER_END);
	put32(stream + 16U, I915_STREAM_MAX_DWORDS + 1U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);

	/* Byte count disagreeing with the header, in both directions. */
	bytes = build(I915_STREAM_ENGINE_BCS0, 1U, 4U, 0U, MI_BATCH_BUFFER_END);
	assert(drv_i915_stream_parse(stream, bytes + 4U, &parsed) == EINVAL);
	assert(drv_i915_stream_parse(stream, bytes - 4U, &parsed) == EINVAL);

	/* A batch that does not end, and a relocation past the end or with reserved bits. */
	bytes = build(I915_STREAM_ENGINE_BCS0, 0U, 2U, 0U, MI_NOOP);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 1U, 4U, 0U, MI_BATCH_BUFFER_END);
	put32(stream + I915_STREAM_HEADER_BYTES, 3U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 1U, 4U, 0U, MI_BATCH_BUFFER_END);
	put32(stream + I915_STREAM_HEADER_BYTES + 4U, 1U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == EINVAL);
	bytes = build(I915_STREAM_ENGINE_BCS0, 1U, 4U, 0U, MI_BATCH_BUFFER_END);
	put32(stream + I915_STREAM_HEADER_BYTES, 2U);
	assert(drv_i915_stream_parse(stream, bytes, &parsed) == 0);
}
