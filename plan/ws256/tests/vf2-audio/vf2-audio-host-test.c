/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the VisionFive 2's sound path on a fake register model:
 * the AXI DMA controller's bring-up, its memory copies (descriptor chains
 * split at the block size, the common width) and its cyclic transfer to
 * the PWM audio converter (configuration words, descriptor control words,
 * the request line, the interrupt's restart on each period, the period
 * reports and the position), the converter's control word and pins, and
 * the sample clock from PLL2.
 *
 * The model runs one descriptor chain of every enabled channel each time
 * the driver sleeps or the test steps it: it checks the descriptor's
 * valid mark, moves the bytes (to memory, or into a log of the words the
 * converter received), clears the valid mark as the hardware writes the
 * descriptor back, follows the link, and at a last-marked descriptor
 * stops the channel and raises transfer-done.  DMA addresses are host
 * pointers.
 *
 *   vf2-audio-host-test vf2-audio.dtb
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/audio/audio.h>
#include <drivers/dma/dw-axi-dmac.h>
#include <drivers/generic/dma.h>
#include <drivers/generic/fdt.h>
#include <drivers/platform/visionfive2/vf2-crg.h>
#include <drivers/platform/visionfive2/vf2-dmac.h>
#include <drivers/platform/visionfive2/vf2-pinctrl.h>
#include <drivers/platform/visionfive2/vf2-pwmdac.h>
#include <drivers/platform/visionfive2/vf2-syscon.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/audio.h>
#include <uapi/errno.h>

struct fake_block {
	uint64_t address;
	size_t size;
	uint8_t *memory;
};

static uint8_t sys_crg[0x10000];
static uint8_t stg_crg[0x10000];
static uint8_t aon_crg[0x10000];
static uint8_t sys_syscon[0x1000];
static uint8_t sys_pinctrl[0x10000];
static uint8_t dmac[0x10000];
static uint8_t pwmdac[0x1000];

static struct fake_block blocks[] = {
	{ 0x13020000ULL, sizeof(sys_crg), sys_crg },
	{ 0x10230000ULL, sizeof(stg_crg), stg_crg },
	{ 0x17000000ULL, sizeof(aon_crg), aon_crg },
	{ 0x13030000ULL, sizeof(sys_syscon), sys_syscon },
	{ 0x13040000ULL, sizeof(sys_pinctrl), sys_pinctrl },
	{ 0x16050000ULL, sizeof(dmac), dmac },
	{ 0x100b0000ULL, sizeof(pwmdac), pwmdac }
};

#define PWMDAC_PHYS	0x100b0000ULL
#define CH(c, off)	(0x100U + (c) * 0x100U + (off))

/* The words the converter's data register received, in order. */
static uint32_t device_log[65536];
static unsigned device_count;
static int device_bad_address;

/* The registered interrupt handler. */
static kern_irq_handler_t irq_handler;
static void *irq_argument;
static int irq_number = -1;

/* The registered audio backend. */
static const struct drv_audio_ops *audio_ops;
static void *audio_private;
static struct drv_dma_device *audio_dma;
static unsigned audio_interrupts;
static int pwmdac_writes_after_enable;

static int failures;

static void
check(
	int condition,
	const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static uint32_t
get32(
	const uint8_t *memory,
	uint32_t offset)
{
	uint32_t value;

	memcpy(&value, memory + offset, 4);
	return value;
}

static void
put32(
	uint8_t *memory,
	uint32_t offset,
	uint32_t value)
{
	memcpy(memory + offset, &value, 4);
}

/* Runs the descriptor chain of one enabled channel until it stops. */
static void
run_channel(
	unsigned c)
{
	uint64_t pointer;
	uint8_t *descriptor;
	uint64_t source;
	uint64_t destination;
	uint64_t next;
	uint32_t block;
	uint32_t control_low;
	uint32_t control_high;
	uint32_t flow;
	unsigned width;
	unsigned count;
	unsigned index;

	for (count = 0; count < 64; count++) {
		pointer = (uint64_t)get32(dmac, CH(c, 0x28)) | ((uint64_t)get32(dmac, CH(c, 0x2c)) << 32);
		descriptor = (uint8_t *)(uintptr_t)pointer;
		memcpy(&control_high, descriptor + 36, 4);
		if ((control_high & 0x80000000U) == 0) {
			/* Invalid descriptor: an error, the channel stops. */
			put32(dmac, CH(c, 0x88), get32(dmac, CH(c, 0x88)) | 0x2000U);
			put32(dmac, 0x18, get32(dmac, 0x18) & ~(1U << c));
			return;
		}
		memcpy(&source, descriptor + 0, 8);
		memcpy(&destination, descriptor + 8, 8);
		memcpy(&block, descriptor + 16, 4);
		memcpy(&next, descriptor + 24, 8);
		memcpy(&control_low, descriptor + 32, 4);
		width = (control_low >> 8) & 7U;
		flow = get32(dmac, CH(c, 0x24)) & 7U;
		if (flow == 0) {
			memcpy((void *)(uintptr_t)destination, (void *)(uintptr_t)source, (size_t)(block + 1U) << width);
		} else {
			if (destination != PWMDAC_PHYS)
				device_bad_address = 1;
			for (index = 0; index <= block; index++) {
				uint32_t word;

				memcpy(&word, (void *)(uintptr_t)(source + (uint64_t)index * 4U), 4);
				if (device_count < sizeof(device_log) / sizeof(device_log[0]))
					device_log[device_count++] = word;
			}
		}

		/* Writes back: valid cleared; the pointer follows the link. */
		control_high &= ~0x80000000U;
		memcpy(descriptor + 36, &control_high, 4);
		put32(dmac, CH(c, 0x28), (uint32_t)next);
		put32(dmac, CH(c, 0x2c), (uint32_t)(next >> 32));
		if ((control_high & 0x40000000U) != 0)
			break;
	}

	/* End of chain: the channel stops and reports transfer-done. */
	put32(dmac, 0x18, get32(dmac, 0x18) & ~(1U << c));
	if ((get32(dmac, CH(c, 0x80)) & 2U) != 0)
		put32(dmac, CH(c, 0x88), get32(dmac, CH(c, 0x88)) | 2U);
}

/* Runs every enabled channel once and raises the interrupt if signalled. */
static void
model_step(
	void)
{
	unsigned c;
	int raise;

	raise = 0;
	for (c = 0; c < 4; c++) {
		if ((get32(dmac, 0x18) & (1U << c)) != 0)
			run_channel(c);
		if ((get32(dmac, CH(c, 0x88)) & get32(dmac, CH(c, 0x90))) != 0)
			raise = 1;
	}
	if (raise && irq_handler != NULL)
		irq_handler(irq_number, (kern_irq_ack_t)1, irq_argument);
}

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t value;

	memcpy(&value, (const void *)address, 4);
	return value;
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint8_t *target;
	uint32_t offset;
	uint32_t old;
	unsigned c;

	target = (uint8_t *)address;

	/* The DMA controller's channel enable and interrupt clear registers. */
	if (target >= dmac && target < dmac + sizeof(dmac)) {
		offset = (uint32_t)(target - dmac);
		if (offset == 0x18) {
			old = get32(dmac, 0x18);
			for (c = 0; c < 8; c++) {
				if ((value & (1U << (c + 8))) == 0)
					continue;
				old &= ~(1U << c);
				old |= value & (1U << c);
			}
			put32(dmac, 0x18, old);
			return;
		}
		if (offset >= 0x100 && (offset & 0xffU) == 0x98) {
			c = (offset - 0x100) / 0x100;
			put32(dmac, CH(c, 0x88), get32(dmac, CH(c, 0x88)) & ~value);
			return;
		}
	}

	/* A reset assert register's status follows it. */
	if (target >= sys_crg + 0x2f8 && target < sys_crg + 0x2f8 + 16)
		put32(sys_crg, 0x308 + (uint32_t)(target - (sys_crg + 0x2f8)), ~value);
	if (target == stg_crg + 0x74)
		put32(stg_crg, 0x78, ~value);
	if (target == pwmdac + 4 && (value & 1U) != 0)
		pwmdac_writes_after_enable++;
	memcpy(target, &value, 4);
}

uint64_t kern_mmio_read64(const volatile void *address) { uint64_t v; memcpy(&v, (const void *)address, 8); return v; }
void kern_mmio_write64(volatile void *address, uint64_t value) { memcpy((void *)address, &value, 8); }
void kern_io_write_barrier(void) { }
void kern_usleep_range(unsigned min_us, unsigned max_us) { (void)min_us; (void)max_us; model_step(); }

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	unsigned index;

	(void)attributes;
	for (index = 0; index < sizeof(blocks) / sizeof(blocks[0]); index++) {
		if (blocks[index].address == address && size <= blocks[index].size) {
			*mapped = blocks[index].memory;
			return 0;
		}
	}
	printf("unexpected map of %llx\n", (unsigned long long)address);
	return ENOMEM;
}

int kern_device_unmap(void *mapped, size_t size) { (void)mapped; (void)size; return 0; }
void spin_init(struct spinlock *lock, enum lock_rank rank, const char *name) { (void)lock; (void)rank; (void)name; }
unsigned long spin_lock_irqsave(struct spinlock *lock) { (void)lock; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled) { (void)lock; (void)enabled; }
void kern_logf(const char *format, ...) { va_list ap; va_start(ap, format); printf("  log: "); vprintf(format, ap); va_end(ap); }
void *kern_calloc(size_t count, size_t size) { return calloc(count, size); }
void kern_free(void *pointer) { free(pointer); }

int kern_irq_register(int irq, kern_irq_handler_t handler, void *argument) { irq_number = irq; irq_handler = handler; irq_argument = argument; return 0; }
int kern_irq_set_mode(int irq, unsigned trigger, unsigned polarity) { (void)irq; (void)trigger; (void)polarity; return 0; }
void kern_irq_mask(int irq) { (void)irq; }
void kern_irq_unmask(int irq) { (void)irq; }
static unsigned eoi_count;
void kern_irq_send_eoi(kern_irq_ack_t acknowledge) { (void)acknowledge; eoi_count++; }

struct drv_dma_device { int coherent; };
static struct drv_dma_device the_dma;
static int dma_created;

int
drv_dma_device_create(
	const struct drv_dma_constraints *constraints,
	struct drv_dma_device **result)
{
	the_dma.coherent = constraints->coherent;
	dma_created++;
	*result = &the_dma;
	return 0;
}

int
drv_dma_alloc_coherent(
	struct drv_dma_device *device,
	size_t size,
	size_t alignment,
	struct drv_dma_buffer *buffer)
{
	void *memory;

	(void)device;
	if (posix_memalign(&memory, alignment < 64 ? 64 : alignment, size) != 0)
		return ENOMEM;
	memset(buffer, 0, sizeof(*buffer));
	buffer->address = memory;
	buffer->device_address = (uint64_t)(uintptr_t)memory;
	buffer->size = size;
	return 0;
}

int
drv_audio_register(
	const struct drv_audio_ops *ops,
	void *private_data,
	struct drv_dma_device *dma,
	struct drv_audio_device **device)
{
	audio_ops = ops;
	audio_private = private_data;
	audio_dma = dma;
	*device = (struct drv_audio_device *)(uintptr_t)0x1234;
	return 0;
}

void
drv_audio_interrupt(
	struct drv_audio_device *device,
	int capture)
{
	if (device == (struct drv_audio_device *)(uintptr_t)0x1234 && capture == 0)
		audio_interrupts++;
}

static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length);
	if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
		perror(path);
		exit(2);
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

/* PLL2 fractional at 1228.8 MHz (24 MHz x 51.2), audio root / 2, APB and STG buses. */
static void
set_registers(
	void)
{
	put32(sys_syscon, 0x2c, 1U | (51U << 17));
	put32(sys_syscon, 0x30, 0x333333U | (0U << 28));
	put32(sys_syscon, 0x34, 1U);
	put32(sys_crg, 5 * 4, 1U << 24);
	put32(sys_crg, 7 * 4, 3U);
	put32(sys_crg, 8 * 4, 2U);
	put32(sys_crg, 11 * 4, 4U);
	put32(sys_crg, 16 * 4, 2U);
	put32(sys_crg, 158 * 4, 12U);
	/* Every reset line held, as after power-on. */
	memset(sys_crg + 0x2f8, 0xff, 16);
	memset(sys_crg + 0x308, 0x00, 16);
	put32(stg_crg, 0x74, 0xffffffffU);
	put32(stg_crg, 0x78, 0U);
	/* The firmware left channel 2 enabled. */
	put32(dmac, 0x18, 1U << 2);
}

static void
check_copy(
	struct drv_dw_axi_dmac *controller)
{
	struct drv_dw_axi_dmac_channel *channel;
	uint8_t *source;
	uint8_t *destination;
	size_t index;
	int error;

	error = drv_dw_axi_dmac_channel_get(controller, &channel);
	check(error == 0, "copy: a second channel is free");
	source = malloc(1U << 21);
	destination = malloc(1U << 21);
	for (index = 0; index < (1U << 21); index++)
		source[index] = (uint8_t)(index * 7U + 3U);

	/* 8-byte aligned, 1 MiB: two blocks of 65536 beats of 8 bytes. */
	memset(destination, 0, 1U << 21);
	error = drv_dw_axi_dmac_copy(channel, (uint64_t)(uintptr_t)destination, (uint64_t)(uintptr_t)source, 1U << 20);
	check(error == 0, "copy 1 MiB: ends");
	check(memcmp(destination, source, 1U << 20) == 0 && destination[1U << 20] == 0, "copy 1 MiB: bytes equal, nothing past the end");
	check(get32(dmac, CH(1, 0x24)) == ((1U << 20) | 0U), "copy: config high = memory flow, priority 1 (new layout)");
	check(get32(dmac, CH(1, 0x20)) == 0xfU, "copy: config low = descriptor lists, no request line");

	/* Odd size: byte beats, 65536 bytes a block, two descriptors. */
	memset(destination, 0, 1U << 21);
	error = drv_dw_axi_dmac_copy(channel, (uint64_t)(uintptr_t)destination + 1U, (uint64_t)(uintptr_t)source + 3U, 100003U);
	check(error == 0, "copy odd: ends");
	check(memcmp(destination + 1, source + 3, 100003U) == 0 && destination[0] == 0 && destination[100004] == 0, "copy odd: bytes equal, edges untouched");

	/* Too long for the chain: 32 blocks of 512 KiB is the limit. */
	error = drv_dw_axi_dmac_copy(channel, 0x1000U, 0x2000U, (size_t)33U << 19);
	check(error == E2BIG, "copy longer than 32 blocks: E2BIG");

	check(get32(dmac, CH(1, 0x80)) == 0, "copy: status enable cleared afterwards");
	drv_dw_axi_dmac_channel_put(channel);
	free(source);
	free(destination);
}

static void
check_audio(
	void)
{
	struct audio_format format;
	struct drv_dma_buffer ring;
	uint32_t *words;
	uint8_t *descriptor;
	uint32_t value;
	unsigned index;
	unsigned step;
	int ok;
	int error;

	check(audio_ops != NULL && audio_ops->playback == 1U && audio_ops->capture == 0U, "audio: one playback backend registered");
	check(audio_dma == &the_dma, "audio: ring from the DMA controller's owner");
	check(audio_ops->format_count == 1U && audio_ops->formats[0].rate == 48000U && audio_ops->formats[0].channels == 2U &&
	      audio_ops->formats[0].format == KERN_AUDIO_FORMAT_S16_LE, "audio: S16_LE stereo 48 kHz");
	check(get32(pwmdac, 4) == 0x4008U, "PWMDAC: configured silent (centre, signed, disabled)");

	/* The ring of 8 fragments of 4096 bytes. */
	error = drv_dma_alloc_coherent(audio_dma, 8U * 4096U, 64U, &ring);
	check(error == 0, "audio: ring allocated");
	words = ring.address;
	for (index = 0; index < 8U * 1024U; index++)
		words[index] = 0xa5000000U | index;

	format.format = KERN_AUDIO_FORMAT_S16_LE;
	format.channels = 2U;
	format.rate = 44100U;
	format.reserved = 0;
	error = audio_ops->prepare(audio_private, 0, &format, &ring, 4096U, 8U);
	check(error == EINVAL, "prepare 44.1 kHz: EINVAL");
	format.rate = 48000U;
	error = audio_ops->prepare(audio_private, 1, &format, &ring, 4096U, 8U);
	check(error == ENOTSUP, "prepare capture: ENOTSUP");
	error = audio_ops->prepare(audio_private, 0, &format, &ring, 512U * 1024U, 8U);
	check(error == E2BIG, "prepare 512 KiB periods: longer than a block, E2BIG");
	error = audio_ops->prepare(audio_private, 0, &format, &ring, 4096U, 8U);
	check(error == 0, "prepare 8 x 4096: ok");

	/* The cyclic channel's configuration and descriptors. */
	check(get32(dmac, CH(0, 0x20)) == (0xfU | (22U << 11)), "cyclic: config low = lists, request line 22 at bit 11");
	check(get32(dmac, CH(0, 0x24)) == (1U | (0U << 20)), "cyclic: config high = memory to device, hardware handshakes, priority 0");
	ok = 1;
	error = audio_ops->start(audio_private, 0);
	check(error == 0, "start: ok");
	check(get32(pwmdac, 4) == 0x4009U, "PWMDAC: enabled on start");
	check((get32(dmac, 0x18) & 1U) != 0, "start: channel 0 enabled");
	descriptor = (uint8_t *)(uintptr_t)((uint64_t)get32(dmac, CH(0, 0x28)) | ((uint64_t)get32(dmac, CH(0, 0x2c)) << 32));
	for (index = 0; index < 8U; index++) {
		uint64_t source;
		uint64_t destination;
		uint64_t next;
		uint32_t block;
		uint32_t control_low;
		uint32_t control_high;

		memcpy(&source, descriptor + index * 64U + 0, 8);
		memcpy(&destination, descriptor + index * 64U + 8, 8);
		memcpy(&block, descriptor + index * 64U + 16, 4);
		memcpy(&next, descriptor + index * 64U + 24, 8);
		memcpy(&control_low, descriptor + index * 64U + 32, 4);
		memcpy(&control_high, descriptor + index * 64U + 36, 4);
		if (source != ring.device_address + index * 4096U)
			ok = 0;
		if (destination != PWMDAC_PHYS || block != 1023U)
			ok = 0;
		if (next != (uint64_t)(uintptr_t)(descriptor + ((index + 1U) % 8U) * 64U))
			ok = 0;
		if (control_low != 0x45240U || control_high != 0xc00f87c0U)
			ok = 0;
	}
	check(ok, "cyclic: 8 descriptors: period sources, data register, 1024 beats, ring links, control 0x45240/0xc00f87c0");
	check(audio_ops->position(audio_private, 0) == 0U, "position 0 before the first period ends");

	/* Two and a half laps. */
	ok = 1;
	for (step = 0; step < 20U; step++) {
		model_step();
		if (audio_ops->position(audio_private, 0) != ((step + 1U) % 8U) * 4096U)
			ok = 0;
	}
	check(ok, "position follows each finished period");
	check(audio_interrupts == 20U, "one framework interrupt a period (20)");
	check(eoi_count == 20U, "every interrupt retired");
	check(device_count == 20U * 1024U && !device_bad_address, "the converter received 20 periods at its data register");
	ok = 1;
	for (index = 0; index < device_count; index++) {
		if (device_log[index] != (0xa5000000U | (index % (8U * 1024U))))
			ok = 0;
	}
	check(ok, "frames arrive in ring order, wrapping");
	check((get32(dmac, 0x18) & 1U) != 0, "channel still enabled (restarted) after 20 periods");

	audio_ops->stop(audio_private, 0);
	check(get32(pwmdac, 4) == 0x4008U, "PWMDAC: disabled on stop");
	check((get32(dmac, 0x18) & 1U) == 0, "stop: channel 0 disabled");
	check(get32(dmac, CH(0, 0x90)) == 0U, "stop: interrupt signal off");
	value = device_count;
	model_step();
	check(device_count == value && audio_interrupts == 20U, "nothing moves after stop");

	/* Restart from the first period. */
	error = audio_ops->start(audio_private, 0);
	check(error == 0, "restart: ok");
	device_count = 0;
	model_step();
	check(device_count == 1024U && device_log[0] == 0xa5000000U, "restart begins at the first period with descriptors valid again");
	audio_ops->stop(audio_private, 0);
	free(ring.address);
}

int
main(
	int argc,
	char **argv)
{
	struct drv_dw_axi_dmac *controller;
	struct drv_dw_axi_dmac_channel *channel;
	struct drv_vf2_clock clock;
	struct drv_fdt fdt;
	unsigned char *blob;
	uint64_t rate;
	size_t size;
	int error;

	if (argc != 2) {
		fprintf(stderr, "usage: vf2-audio-host-test vf2-audio.dtb\n");
		return 2;
	}
	blob = read_file(argv[1], &size);
	error = drv_fdt_open(&fdt, blob, size);
	check(error == 0, "tree opens");
	set_registers();
	check(drv_vf2_syscon_init(&fdt) == 0, "syscon init");
	check(drv_vf2_crg_init(&fdt) == 0, "crg init");
	check(drv_vf2_pinctrl_init(&fdt) == 0, "pinctrl init");

	error = drv_vf2_pwmdac_attach(&fdt, 1);
	check(error == 0, "PWMDAC attaches");
	check(drv_vf2_pwmdac_attach(&fdt, 1) == EALREADY, "second attach: EALREADY");

	/* DMA controller bring-up. */
	check((get32(stg_crg, 27 * 4) & 0x80000000U) != 0 && (get32(stg_crg, 28 * 4) & 0x80000000U) != 0, "DMAC: core and config clocks open");
	check((get32(stg_crg, 0x74) & ((1U << 5) | (1U << 6))) == 0, "DMAC: both resets released");
	check(get32(dmac, 0x10) == 3U, "DMAC: enabled with interrupts");
	check((get32(dmac, 0x18) & 0xffU) == 0, "DMAC: firmware's channel 2 stopped");
	check(irq_number == 73 && irq_handler != NULL, "DMAC: interrupt 73 registered");
	check(dma_created == 1 && the_dma.coherent == 1, "DMAC: one DMA owner, coherent by the board default");

	/* Converter bring-up. */
	check((get32(sys_crg, 157 * 4) & 0x80000000U) != 0 && (get32(sys_crg, 158 * 4) & 0x80000000U) != 0, "PWMDAC: apb and core clocks open");
	check((get32(sys_crg, 0x2f8 + 12) & 1U) == 0, "PWMDAC: reset 96 released");
	check((get32(sys_crg, 158 * 4) & 0xffffffU) == 50U, "PWMDAC: core divider 50 (614.4 MHz / 50 = 12.288 MHz)");
	clock.crg = DRV_VF2_CRG_SYS;
	clock.index = 158;
	error = drv_vf2_clock_get_rate(&clock, &rate);
	check(error == 0 && rate / 256U >= 47999U && rate / 256U <= 48000U, "PWMDAC: 48 kHz sample rate from PLL2 1228.8 MHz");
	{
		/* GPIO 33 and 34: output selects 28 and 29 (dout registers at 0x40, a byte a pin). */
		uint32_t dout;

		dout = get32(sys_pinctrl, 0x40 + (33U / 4U) * 4U);
		check(((dout >> ((33U % 4U) * 8U)) & 0x7fU) == 28U && ((dout >> ((34U % 4U) * 8U)) & 0x7fU) == 29U,
		      "pins: GPIO 33 left (28), GPIO 34 right (29)");
	}

	check_audio();

	/* Memory copies on another channel (the audio holds channel 0). */
	error = drv_vf2_dmac_get(&fdt, 0, 1, &controller);
	check(error == ENOTSUP, "a second controller node: ENOTSUP");
	controller = NULL;
	{
		uint32_t node;

		error = drv_fdt_find_compatible(&fdt, "starfive,jh7110-axi-dma", DRV_FDT_NO_NODE, &node);
		check(error == 0, "DMAC node found");
		error = drv_vf2_dmac_get(&fdt, node, 1, &controller);
		check(error == 0 && controller != NULL, "the same controller handed out again");
	}
	check_copy(controller);
	error = drv_dw_axi_dmac_channel_get(controller, &channel);
	check(error == 0, "channel 1 free again after put");

	printf("%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
	return failures == 0 ? 0 : 1;
}
