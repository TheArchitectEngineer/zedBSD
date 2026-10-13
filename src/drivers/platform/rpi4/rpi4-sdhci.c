/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Raspberry Pi 4 BCM2711 eMMC2 SDHCI PIO driver
 *
 * A request is moved with one multiple-block command (CMD18 or CMD25) per
 * run of up to SD_RUN_BLOCKS blocks, the controller ending each run with
 * an automatic CMD12, as the SD Host Controller Simplified Specification
 * describes for a transfer without DMA.  A run that fails is moved again
 * one block at a time (BUG-295).
 */

#include "drivers/platform/rpi4/rpi4-sdhci.h"

#include <kern/disk.h>
#include <uapi/sysctl.h>
#include <kern/lock.h>

#include <uapi/errno.h>
#include "kern/klog.h"
#include "kern/device-io.h"

#define DIRECT_BASE 0xffff000000000000ULL
#define REG_BLOCK_SIZE 0x04
#define REG_BLOCK_COUNT 0x06
#define REG_ARGUMENT 0x08
#define REG_TRANSFER_MODE 0x0c
#define REG_COMMAND 0x0e
#define REG_RESPONSE0 0x10
#define REG_BUFFER 0x20
#define REG_PRESENT 0x24
#define REG_HOST_CONTROL 0x28
#define REG_POWER_CONTROL 0x29
#define REG_CLOCK_CONTROL 0x2c
#define REG_TIMEOUT_CONTROL 0x2e
#define REG_SOFTWARE_RESET 0x2f
#define REG_INT_STATUS 0x30
#define REG_INT_ENABLE 0x34
#define REG_SIGNAL_ENABLE 0x38
#define REG_AUTO_CMD_ERROR 0x3c
#define REG_CAPABILITIES 0x40

#define PRESENT_CMD_INHIBIT 0x00000001U
#define PRESENT_DATA_INHIBIT 0x00000002U
#define INT_CMD_COMPLETE 0x00000001U
#define INT_TRANSFER_COMPLETE 0x00000002U
#define INT_BUFFER_WRITE_READY 0x00000010U
#define INT_BUFFER_READ_READY 0x00000020U
#define INT_ERROR 0x00008000U
#define INT_ERROR_MASK 0xffff0000U
#define CMD_RESP_LONG 0x0001U
#define CMD_RESP_SHORT 0x0002U
#define CMD_RESP_BUSY 0x0003U
#define CMD_CRC 0x0008U
#define CMD_INDEX 0x0010U
#define CMD_DATA 0x0020U

/*
 * The bits of the transfer mode register: the block count register counts
 * the blocks of the run, the controller sends CMD12 itself after the last
 * one, the data comes from the card, and the command moves more than one
 * block.
 */
#define XFER_BLOCK_COUNT_ENABLE 0x0002U
#define XFER_AUTO_CMD12 0x0004U
#define XFER_READ 0x0010U
#define XFER_MULTI_BLOCK 0x0020U

/* The bits of the software reset register: the whole controller, or one line. */
#define SOFTWARE_RESET_ALL 0x01U
#define SOFTWARE_RESET_CMD 0x02U
#define SOFTWARE_RESET_DATA 0x04U

/* The card's state in the R1 status that CMD13 answers with. */
#define CARD_STATE_SHIFT 9
#define CARD_STATE_MASK 0x0fU
#define CARD_STATE_TRANSFER 4U
#define CARD_STATE_DATA 5U
#define CARD_STATE_RECEIVE 6U
#define CARD_STATE_PROGRAM 7U
#define CARD_READY_FOR_DATA 0x00000100U

/* How long a command, a data buffer and a read may take before it is abandoned. */
#define SD_COMMAND_SECONDS 2U

/*
 * How long a written block may keep the card busy.  A card that tidies its
 * flash in the background can hold the data line for well over the 250 to
 * 500 ms the specification names, and a block given up while the card is
 * still programming it is a block the next command collides with.
 */
#define SD_WRITE_BUSY_SECONDS 10U

/* How long the card has to come back to the transfer state after an error. */
#define SD_RECOVERY_SECONDS 10U

/* How many times one block is tried, with a recovery between the tries. */
#define SD_TRANSFER_ATTEMPTS 3U

/* How many stop commands one recovery sends to a card stuck in a transfer. */
#define SD_STOP_ATTEMPTS 3U

/* The pause between two status questions while the card is busy. */
#define SD_STATUS_POLL_MICROSECONDS 1000U

/* The bytes of one block, the only block length the driver uses. */
#define SD_BLOCK_BYTES 512U

/* The 32-bit words of one block, as the buffer port moves them. */
#define SD_BLOCK_WORDS (SD_BLOCK_BYTES / 4U)

/*
 * The most blocks one multiple-block command moves: 64 KiB, the largest
 * request the disk layer hands down (KERN_IO_BATCH_MAX), and well inside
 * the 16-bit block count register.
 */
#define SD_RUN_BLOCKS 128U

struct rpi4_sd_unit {
	volatile uint8_t *base;
	struct disk *disk;
	uint32_t rca;
	int high_capacity;

	/*
	 * The halves of the register pairs written as one word (see w16):
	 * the transfer mode until the command, the block size until the count.
	 */
	uint16_t transfer_mode;
	uint16_t block_size;

	/* One command at a time: the controller has one set of registers. */
	struct mutex lock;

	/*
	 * Set once a failed transfer could neither be recovered nor the card
	 * initialized again.  Every later transfer fails at once instead of
	 * waiting out its deadlines, so a sync or a shutdown that meets a dead
	 * card ends with an error rather than hanging.  Under the lock.
	 */
	int failed;
};

static struct rpi4_sd_unit unit;

/*
 * The interrupt status the last failed wait saw, an error or a time-out.
 * It is what a failure report shows, since the wait clears the register.
 * Under the unit lock once the disk is published.
 */
static uint32_t last_error_status;

static uint32_t r32(unsigned offset);
static uint16_t r16(unsigned offset);
static int controller_init(void);
static void w8(unsigned offset, uint8_t value);
static uint64_t counter(void);
static uint64_t frequency(void);
static uint8_t r8(unsigned offset);
static void w32(unsigned offset, uint32_t value);
static int set_clock(uint32_t target);
static void w16(unsigned offset, uint16_t value);
static int command(uint32_t index, uint32_t argument, uint16_t flags, uint32_t *response);
static int wait_bits(unsigned reg, uint32_t mask, uint32_t expected);
static int wait_interrupt(uint32_t wanted, unsigned seconds);
static int app_command(uint32_t command_index, uint32_t argument, uint16_t flags, uint32_t *response);
static int transfer_blocks(int write, uint64_t block, uint32_t count, void *buffer);
static int transfer_block_recovering(int write, uint64_t block, void *buffer);
static int transfer_run_recovering(int write, uint64_t block, uint32_t count, uint8_t *data);
static int restore_card(void);
static int recover_card(void);
static int reset_lines(uint8_t lines);
static int stop_transmission(void);
static void pause_microseconds(unsigned microseconds);
static int sd_submit(struct disk *disk, struct bio *bio);
static int sd_ioctl(struct disk *disk, unsigned long request, void *argument);

static const struct disk_ops sd_ops = {.submit = sd_submit, .ioctl = sd_ioctl};

/*
 * Implements the drv rpi4 sdhci init operation.
 */
int
drv_rpi4_sdhci_init(
	uintptr_t physical_base)
{
	int error;

	/* Maps the controller and reports what it advertises. */
	unit.base = (volatile uint8_t *)(DIRECT_BASE + physical_base);
	unit.disk = 0;
	kern_logf("sdhci: base=%p caps=%x present=%x version=%x\n",
		   (void *)physical_base, r32(REG_CAPABILITIES),
		   r32(REG_PRESENT), r16(0xfe));

	/* Checks the operation status. */
	error = controller_init();
	if (error) {
		kern_logf("sdhci: init error=%u status=%x present=%x\n",
			   (unsigned)error, r32(REG_INT_STATUS),
			   r32(REG_PRESENT));

		/* Failed. */
		return error;
	}

	unit.disk = disk_alloc();

	/* Handles the unit condition. */
	if (!unit.disk)
		return ENOMEM;
	(void)mutex_init(&unit.lock, LOCK_RANK_DISK, "sdhci");
	unit.disk->d_name[0] = 'm';
	unit.disk->d_name[1] = 'm';
	unit.disk->d_name[2] = 'c';
	unit.disk->d_name[3] = 'b';
	unit.disk->d_name[4] = 'l';
	unit.disk->d_name[5] = 'k';
	unit.disk->d_name[6] = '0';
	unit.disk->d_name[7] = '\0';
	unit.disk->d_flags = DISK_REMOVABLE;
	unit.disk->d_block_size = 512;
	unit.disk->d_block_count = 0xffffffffULL;
	unit.disk->d_max_transfer_blocks = SD_RUN_BLOCKS;
	unit.disk->d_ops = &sd_ops;
	unit.disk->d_data = &unit;

	/* hw.diskstats lists the card as an SD/MMC disk (ws134-p006). */
	disk_set_stats_kind(unit.disk, DISK_STATS_KIND_SDMMC);

	/* Checks the operation status. */
	error = disk_create(unit.disk);
	if (error) {
		unit.disk = 0;

		/* Failed. */
		return error;
	}

	kern_logf("sdhci: mmcblk0 ready (%s addressing)\n",
		   unit.high_capacity ? "block" : "byte");

	/* Succeeded. */
	return 0;
}

/*
 * Implements the drv rpi4 sdhci disk operation.
 */
struct disk *
drv_rpi4_sdhci_disk(
	void)
{
	/* Returns the computed result. */
	return unit.disk;
}

/* Supports the r32 operation. */
static uint32_t
r32(
	unsigned offset)
{
	uint32_t function_result;

	/* Obtains the hal mmio read32 result. */
	function_result = kern_mmio_read32(unit.base + offset);

	/* Returns the computed result. */
	return function_result;
}

/*
 * Reads a 16-bit register.  The BCM2711 EMMC2 (an iProc SDHCI) takes only
 * 32-bit accesses, so the word that holds it is read and the half taken out.
 */
static uint16_t
r16(
	unsigned offset)
{
	uint32_t word;
	unsigned shift;

	/* The word, and the half of it at the offset. */
	word = r32(offset & ~3U);
	shift = (offset & 2U) * 8U;
	return (uint16_t)(word >> shift);
}

/* Supports the controller init operation. */
static int
controller_init(
	void)
{
	uint32_t response = 0;
	uint64_t deadline;
	int error;

	w8(REG_SOFTWARE_RESET, 1U);
	deadline = counter() + frequency();
	/* Continue while the operation condition remains true. */
	while (r8(REG_SOFTWARE_RESET) & 1U) {
		/* Checks the counter result. */
		if (counter() >= deadline)
			return ETIMEDOUT;
	}

	w8(REG_POWER_CONTROL, 0x0fU);
	w8(REG_TIMEOUT_CONTROL, 0x0eU);
	w32(REG_INT_ENABLE, 0xffffffffU);
	w32(REG_SIGNAL_ENABLE, 0);

	/* Checks the operation status. */
	if ((error = set_clock(400000U)) != 0) {
		kern_logf("sdhci: clock identification %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	if ((error = command(0, 0, 0, 0)) != 0) {
		kern_logf("sdhci: CMD0 %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	error = command(8, 0x1aaU, CMD_RESP_SHORT | CMD_CRC | CMD_INDEX,
			&response);
	if (error) {
		kern_logf("sdhci: CMD8 error=%u irq=%x\n", (unsigned)error,
			   last_error_status);
	} else {
		kern_logf("sdhci: CMD8 response=%x\n", response);
	}

	unit.rca = 0;
	deadline = counter() + frequency() * 2U;
	do {
		/* Checks the operation status. */
		error = app_command(41, 0x40300000U, CMD_RESP_SHORT, &response);
		if (error == 0 && (response & 0x80000000U))
			break;
	} while (counter() < deadline);

	/* Checks the operation status. */
	if (error != 0 || !(response & 0x80000000U)) {
		kern_logf("sdhci: ACMD41 error=%u irq=%x ocr=%x\n",
			   (unsigned)error, last_error_status, response);

		/* Failed. */
		return ETIMEDOUT;
	}

	unit.high_capacity = (response & 0x40000000U) != 0;

	/* Checks the operation status. */
	if ((error = command(2, 0, CMD_RESP_LONG | CMD_CRC, 0)) != 0) {
		kern_logf("sdhci: CMD2 %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	if ((error = command(3, 0, CMD_RESP_SHORT | CMD_CRC | CMD_INDEX,
			     &response)) != 0) {
		kern_logf("sdhci: CMD3 %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	unit.rca = response >> 16;

	/* Handles the unit condition. */
	if (unit.rca == 0)
		return EIO;

	/* Checks the operation status. */
	if ((error = command(9, unit.rca << 16, CMD_RESP_LONG | CMD_CRC, 0)) !=
	    0) {
		kern_logf("sdhci: CMD9 %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	if ((error = command(7, unit.rca << 16,
			     CMD_RESP_BUSY | CMD_CRC | CMD_INDEX, 0)) != 0) {
		kern_logf("sdhci: CMD7 %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	if (!unit.high_capacity &&
	    (error = command(16, 512, CMD_RESP_SHORT | CMD_CRC | CMD_INDEX,
			     0)) != 0) {
		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	if ((error = set_clock(25000000U)) != 0) {
		kern_logf("sdhci: clock transfer %u\n", (unsigned)error);

		/* Failed. */
		return error;
	}

	/*
	 * ACMD6 selects the four-bit bus; keep one-bit mode if it is rejected.
	 */
	if (app_command(6, 2, CMD_RESP_SHORT | CMD_CRC | CMD_INDEX, 0) == 0)
		w8(REG_HOST_CONTROL, (uint8_t)(r8(REG_HOST_CONTROL) | 2U));

	/* Succeeded. */
	return 0;
}

/*
 * Writes an 8-bit register as a read-modify-write of the word that holds it,
 * since the controller takes only 32-bit accesses.
 */
static void
w8(
	unsigned offset,
	uint8_t value)
{
	uint32_t word;
	unsigned shift;

	/* The other bytes of the word keep what they hold. */
	word = r32(offset & ~3U);
	shift = (offset & 3U) * 8U;
	word &= ~(0xffU << shift);
	word |= (uint32_t)value << shift;
	w32(offset & ~3U, word);
}

/* Supports the counter operation. */
static uint64_t
counter(
	void)
{
	uint64_t value;

	__asm__ volatile("mrs %0,cntpct_el0" : "=r"(value));

	/* Returns the computed result. */
	return value;
}

/* Supports the frequency operation. */
static uint64_t
frequency(
	void)
{
	uint64_t value;

	__asm__ volatile("mrs %0,cntfrq_el0" : "=r"(value));

	/* Returns the computed result. */
	return value;
}

/* Reads an 8-bit register, from the word that holds it (as r16). */
static uint8_t
r8(
	unsigned offset)
{
	uint32_t word;
	unsigned shift;

	/* The word, and the byte of it at the offset. */
	word = r32(offset & ~3U);
	shift = (offset & 3U) * 8U;
	return (uint8_t)(word >> shift);
}

/* Supports the w32 operation. */
static void
w32(
	unsigned offset,
	uint32_t value)
{
	kern_mmio_write32(unit.base + offset, value);
}

/* Supports the set clock operation. */
static int
set_clock(
	uint32_t target)
{
	uint32_t caps = r32(REG_CAPABILITIES),
		 base = ((caps >> 8) & 0xffU) * 1000000U;
	uint32_t divisor = 1, encoded;
	uint16_t clock;
	uint64_t deadline;

	/* Handles the base condition. */
	if (base == 0)
		base = 100000000U;
	/* Continue while the operation condition remains true. */
	while (divisor < 0x400U && base / (2U * divisor) > target)
		divisor <<= 1;
	encoded = (divisor & 0xffU) << 8 | (divisor & 0x300U) >> 2;
	w16(REG_CLOCK_CONTROL, 0);
	w16(REG_CLOCK_CONTROL, (uint16_t)(encoded | 1U));
	deadline = counter() + frequency();
	do {
		/* Handles the clock condition. */
		clock = r16(REG_CLOCK_CONTROL);
		if (clock & 2U)
			break;
	} while (counter() < deadline);

	/* Handles the clock condition. */
	if (!(clock & 2U))
		return ETIMEDOUT;
	w16(REG_CLOCK_CONTROL, (uint16_t)(clock | 4U));

	/* Succeeded. */
	return 0;
}

/*
 * Writes a 16-bit register with 32-bit accesses only.
 *
 * Two pairs cannot be written half at a time: writing the command starts
 * it, so the transfer mode must go in the same write, and the block size is
 * written with the block count, as Linux's iProc driver does.  The first
 * half of each pair is kept until the second is written.  Any other
 * register is a read-modify-write of its word.
 */
static void
w16(
	unsigned offset,
	uint16_t value)
{
	uint32_t word;
	unsigned shift;

	/* The first halves of the pairs are kept. */
	if (offset == REG_TRANSFER_MODE) {
		unit.transfer_mode = value;
		return;
	}
	if (offset == REG_BLOCK_SIZE) {
		unit.block_size = value;
		return;
	}

	/* The command goes out with the transfer mode, which is then used up. */
	if (offset == REG_COMMAND) {
		w32(REG_TRANSFER_MODE,
		    ((uint32_t)value << 16) | unit.transfer_mode);
		unit.transfer_mode = 0;
		return;
	}

	/* The block count goes out with the block size. */
	if (offset == REG_BLOCK_COUNT) {
		w32(REG_BLOCK_SIZE, ((uint32_t)value << 16) | unit.block_size);
		return;
	}

	/* Anything else: the other half of the word keeps what it holds. */
	word = r32(offset & ~3U);
	shift = (offset & 2U) * 8U;
	word &= ~(0xffffU << shift);
	word |= (uint32_t)value << shift;
	w32(offset & ~3U, word);
}

/* Supports the command operation. */
static int
command(
	uint32_t index,
	uint32_t argument,
	uint16_t flags,
	uint32_t *response)
{
	uint32_t inhibit = PRESENT_CMD_INHIBIT;
	int error;

	/* Checks the active flags. */
	if (flags & CMD_DATA)
		inhibit |= PRESENT_DATA_INHIBIT;

	/* Checks the operation status. */
	error = wait_bits(REG_PRESENT, inhibit, 0);
	if (error)
		return error;
	w32(REG_INT_STATUS, 0xffffffffU);
	w32(REG_ARGUMENT, argument);
	w16(REG_COMMAND, (uint16_t)((index << 8) | flags));

	/* Checks the operation status. */
	error = wait_interrupt(INT_CMD_COMPLETE, SD_COMMAND_SECONDS);
	if (error)
		return error;

	/* Handles the response condition. */
	if (response)
		*response = r32(REG_RESPONSE0);
	/* Succeeded. */
	return 0;
}

/* Supports the wait bits operation. */
static int
wait_bits(
	unsigned reg,
	uint32_t mask,
	uint32_t expected)
{
	uint64_t deadline = counter() + frequency() * 2U;

	/* Continue while the operation condition remains true. */
	while ((r32(reg) & mask) != expected) {
		/* Checks the counter result. */
		if (counter() >= deadline)
			return ETIMEDOUT;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Waits for the interrupt status to show the wanted event.
 *
 * An error bit ends the wait at once.  Whatever ends the wait other than
 * the wanted event is kept in last_error_status for the failure report,
 * since the status register is cleared here or by the recovery.
 */
static int
wait_interrupt(
	uint32_t wanted,
	unsigned seconds)
{
	uint32_t status;
	uint64_t deadline;

	/* Sets the deadline the controller has to answer by. */
	deadline = counter() + frequency() * seconds;

	/* Polls the status until the event, an error, or the deadline. */
	for (;;) {
		/* An error bit ends the wait, and is acknowledged. */
		status = r32(REG_INT_STATUS);
		if ((status & (INT_ERROR | INT_ERROR_MASK)) != 0) {
			last_error_status = status;
			w32(REG_INT_STATUS, status);

			/* Failed: the controller reported an error. */
			return EIO;
		}

		/* The wanted event is acknowledged and ends the wait. */
		if ((status & wanted) != 0) {
			w32(REG_INT_STATUS, wanted);

			/* Succeeded. */
			return 0;
		}

		/* A controller that never answers is given up on at the deadline. */
		if (counter() >= deadline) {
			last_error_status = status;
			return ETIMEDOUT;
		}
	}
}

/* Supports the app command operation. */
static int
app_command(
	uint32_t command_index,
	uint32_t argument,
	uint16_t flags,
	uint32_t *response)
{
	int function_result;
	int error = command(55, unit.rca << 16,
			    CMD_RESP_SHORT | CMD_CRC | CMD_INDEX, 0);

	/* Computes the function result. */
	function_result =
		error ? error
		      : command(command_index, argument, flags, response);

	/* Returns the computed result. */
	return function_result;
}

/*
 * Moves a run of 512-byte blocks between the card and the buffer by PIO.
 *
 * One block is moved with CMD17 or CMD24.  More are moved with CMD18 or
 * CMD25: the block count register holds the length of the run, and the
 * controller sends CMD12 by itself once the last block has crossed the
 * bus.  Each block goes through the buffer port once the controller raises
 * buffer read ready or buffer write ready for it.  The run ends with one
 * wait for the transfer-complete interrupt, which the controller raises
 * only when the card has released the data line after programming the
 * last block (and, for a run, after the automatic CMD12).  Between the
 * blocks of a run the controller holds the next block back while the card
 * is busy, so the driver sends one command and waits for completion once
 * per run rather than once per block.
 */
static int
transfer_blocks(
	int write,
	uint64_t block,
	uint32_t count,
	void *buffer)
{
	uint32_t *words;
	uint32_t argument;
	uint32_t ready;
	uint32_t command_index;
	uint32_t moved;
	uint16_t mode;
	unsigned ready_seconds;
	unsigned completion_seconds;
	unsigned index;
	int error;

	/* A run of no blocks, or one longer than a command is given, is refused. */
	if (count == 0 || count > SD_RUN_BLOCKS)
		return EINVAL;

	/* A run past what a 32-bit argument can name is refused. */
	if (block > 0xffffffffULL - (count - 1U))
		return EINVAL;

	/* A high-capacity card is addressed in blocks, an older one in bytes. */
	if (unit.high_capacity) {
		argument = (uint32_t)block;
	} else {
		argument = (uint32_t)(block * SD_BLOCK_BYTES);
	}

	/* Picks the buffer event and the completion wait of the direction. */
	if (write) {
		ready = INT_BUFFER_WRITE_READY;
		mode = 0;
		completion_seconds = SD_WRITE_BUSY_SECONDS;
	} else {
		ready = INT_BUFFER_READ_READY;
		mode = XFER_READ;
		completion_seconds = SD_COMMAND_SECONDS;
	}

	/* Picks the single-block or the multiple-block command of the direction. */
	if (count == 1U && write) {
		command_index = 24U;
	} else if (count == 1U) {
		command_index = 17U;
	} else if (write) {
		command_index = 25U;
	} else {
		command_index = 18U;
	}

	/*
	 * A run's length is held by the block count register, which counts
	 * down as the blocks cross the bus, and the controller stops the
	 * card with CMD12 when it reaches zero.
	 */
	if (count != 1U) {
		mode |= XFER_MULTI_BLOCK;
		mode |= XFER_BLOCK_COUNT_ENABLE;
		mode |= XFER_AUTO_CMD12;
	}

	/* Describes the run of 512-byte blocks in the direction of the transfer. */
	w16(REG_BLOCK_SIZE, SD_BLOCK_BYTES);
	w16(REG_BLOCK_COUNT, (uint16_t)count);
	w16(REG_TRANSFER_MODE, mode);

	/* Sends the read or write command, which starts the data phase. */
	error = command(command_index, argument,
			CMD_RESP_SHORT | CMD_CRC | CMD_INDEX | CMD_DATA, 0);
	if (error != 0)
		return error;

	/* Moves each block through the buffer port once the controller is ready for it. */
	words = buffer;
	for (moved = 0; moved < count; moved++) {
		/*
		 * The first block is ready at once.  Before a later block of
		 * a write the card may still be programming the one before,
		 * and holds the data line busy while it does.
		 */
		ready_seconds = SD_COMMAND_SECONDS;
		if (write && moved != 0)
			ready_seconds = SD_WRITE_BUSY_SECONDS;

		/* Waits until the controller's buffer can take or give the block. */
		error = wait_interrupt(ready, ready_seconds);
		if (error != 0)
			return error;

		/* Moves the block through the buffer port, one word at a time. */
		for (index = 0; index < SD_BLOCK_WORDS; index++) {
			if (write) {
				w32(REG_BUFFER, words[index]);
			} else {
				words[index] = r32(REG_BUFFER);
			}
		}

		/* The next block follows in the caller's buffer. */
		words += SD_BLOCK_WORDS;
	}

	/* Waits until the run has crossed the bus and a write left the card idle. */
	error = wait_interrupt(INT_TRANSFER_COMPLETE, completion_seconds);
	if (error != 0)
		return error;

	/* Succeeded: the run is on the card or in the buffer. */
	return 0;
}

/*
 * Moves one block, recovering the controller and the card after a failure.
 *
 * Without the recovery one error leaves the inhibit bits set and every
 * later command times out, so the volume above can neither write its
 * buffers nor be synced at shutdown (BUG-288).  A block is tried again
 * after each recovery; reading or writing the same block again is
 * harmless.  A card that cannot be recovered is initialized again, and
 * one that cannot be initialized either is marked failed.
 */
static int
transfer_block_recovering(
	int write,
	uint64_t block,
	void *buffer)
{
	const char *direction;
	unsigned attempt;
	int error;
	int recovery;

	/* Names the direction in the reports. */
	if (write) {
		direction = "write";
	} else {
		direction = "read";
	}

	/* Tries the block, recovering between the tries. */
	error = EIO;
	for (attempt = 1; attempt <= SD_TRANSFER_ATTEMPTS; attempt++) {
		/* Moves the block; a success after a failure is reported as such. */
		last_error_status = 0;
		error = transfer_blocks(write, block, 1U, buffer);
		if (error == 0) {
			if (attempt > 1) {
				kern_logf("sdhci: %s of block %llu succeeded on attempt %u\n",
					  direction,
					  (unsigned long long)block,
					  attempt);
			}

			/* Succeeded: the block moved. */
			return 0;
		}

		/* Reports the failure with what the controller showed. */
		kern_logf("sdhci: %s of block %llu failed on attempt %u: error %d, interrupt status %x, present state %x\n",
			  direction,
			  (unsigned long long)block,
			  attempt,
			  error,
			  last_error_status,
			  r32(REG_PRESENT));

		/* Brings the controller and the card back to the transfer state. */
		recovery = restore_card();
		if (recovery != 0)
			return recovery;
	}

	/* Failed: every attempt failed, though the card took commands again. */
	return error;
}

/*
 * Moves a run of blocks with one command, recovering after a failure.
 *
 * A run that fails may have moved any prefix of its blocks, and which
 * prefix is not known: a write's blocks may be on the card or not, a
 * read's may be in the buffer or not.  After the controller and the card
 * are recovered, the whole run is moved again one block at a time, each
 * block with its own retries; writing or reading a block again is
 * harmless.  A run of one block is the single-block path straight away.
 */
static int
transfer_run_recovering(
	int write,
	uint64_t block,
	uint32_t count,
	uint8_t *data)
{
	const char *direction;
	uint32_t index;
	int error;
	int recovery;

	/* A run of one block has nothing to gain from the multiple-block command. */
	if (count == 1U) {
		error = transfer_block_recovering(write, block, data);
		if (error != 0)
			return error;

		/* Succeeded: the block moved. */
		return 0;
	}

	/* Names the direction in the reports. */
	if (write) {
		direction = "write";
	} else {
		direction = "read";
	}

	/* Moves the run with one command. */
	last_error_status = 0;
	error = transfer_blocks(write, block, count, data);
	if (error == 0) {
		/* Succeeded: the run moved. */
		return 0;
	}

	/* A run the driver refused never reached the controller; there is nothing to recover. */
	if (error == EINVAL)
		return EINVAL;

	/* Reports the failure with what the controller showed, the automatic CMD12 included. */
	kern_logf("sdhci: %s of %u blocks at block %llu failed: error %d, interrupt status %x, auto command error %x, present state %x; moving them one block at a time\n",
		  direction,
		  (unsigned)count,
		  (unsigned long long)block,
		  error,
		  last_error_status,
		  (unsigned)r16(REG_AUTO_CMD_ERROR),
		  r32(REG_PRESENT));

	/* Brings the controller and the card back to the transfer state. */
	recovery = restore_card();
	if (recovery != 0)
		return recovery;

	/* Moves the run again one block at a time, stopping at the first block that fails. */
	for (index = 0; index < count; index++) {
		error = transfer_block_recovering(write,
						  block + index,
						  data + (size_t)index * SD_BLOCK_BYTES);
		if (error != 0)
			return error;
	}

	/* Succeeded: every block of the run moved on its own. */
	return 0;
}

/*
 * Brings the card back to the transfer state after a failed transfer.
 *
 * The recovery is tried first; a card it cannot bring back is initialized
 * again.  A card that cannot be initialized either is marked failed, and
 * every later transfer fails at once.
 */
static int
restore_card(
	void)
{
	int recovery;

	/* Brings the controller and the card back to the transfer state. */
	recovery = recover_card();
	if (recovery != 0) {
		kern_logf("sdhci: the card did not return to the transfer state (error %d); initializing it again\n",
			  recovery);
		recovery = controller_init();
	}

	/* A card that cannot be initialized either fails its I/O from now on. */
	if (recovery != 0) {
		kern_logf("sdhci: the card could not be initialized again (error %d); failing every later transfer\n",
			  recovery);
		unit.failed = 1;
		return EIO;
	}

	/* Succeeded: the card takes the next transfer. */
	return 0;
}

/*
 * Brings the controller and the card back to a state that takes commands.
 *
 * An error interrupt or a time-out leaves the controller's command and
 * data state machines where they stopped, with their inhibit bits set.
 * Both lines are reset, and the card is asked for its state until it is
 * back in the transfer state and ready for data.  A card still sending or
 * receiving the block is told to stop; one still programming is waited
 * for.
 */
static int
recover_card(
	void)
{
	uint64_t deadline;
	uint32_t card_status;
	uint32_t card_state;
	unsigned stops;
	int error;

	/* Resets the command and the data state machines of the controller. */
	error = reset_lines(SOFTWARE_RESET_CMD | SOFTWARE_RESET_DATA);
	if (error != 0)
		return error;

	/* Asks the card for its state until it can take the next transfer. */
	stops = 0;
	deadline = counter() + frequency() * SD_RECOVERY_SECONDS;
	for (;;) {
		/* A card that does not settle by the deadline is given up on. */
		if (counter() >= deadline)
			return ETIMEDOUT;

		/* Reads the card's status; a failed question resets the command line. */
		card_status = 0;
		error = command(13, unit.rca << 16,
				CMD_RESP_SHORT | CMD_CRC | CMD_INDEX,
				&card_status);
		if (error != 0) {
			(void)reset_lines(SOFTWARE_RESET_CMD);
			pause_microseconds(SD_STATUS_POLL_MICROSECONDS);
			continue;
		}

		/* The state field says where the card's own state machine stands. */
		card_state = (card_status >> CARD_STATE_SHIFT) & CARD_STATE_MASK;

		/* A card in the transfer state and ready for data takes the next block. */
		if (card_state == CARD_STATE_TRANSFER) {
			if ((card_status & CARD_READY_FOR_DATA) != 0)
				break;
			pause_microseconds(SD_STATUS_POLL_MICROSECONDS);
			continue;
		}

		/* A card still programming the block is waited for. */
		if (card_state == CARD_STATE_PROGRAM) {
			pause_microseconds(SD_STATUS_POLL_MICROSECONDS);
			continue;
		}

		/* A card in any other state than a transfer is not ours to fix here. */
		if (card_state != CARD_STATE_DATA &&
		    card_state != CARD_STATE_RECEIVE) {
			kern_logf("sdhci: the card is in state %u after an error\n",
				  (unsigned)card_state);
			return EIO;
		}

		/* A card still in a transfer is told to stop, a bounded number of times. */
		if (stops >= SD_STOP_ATTEMPTS)
			return ETIMEDOUT;
		stops++;
		error = stop_transmission();
		if (error != 0)
			(void)reset_lines(SOFTWARE_RESET_CMD | SOFTWARE_RESET_DATA);
	}

	/* Clears what the recovery itself left in the interrupt status. */
	w32(REG_INT_STATUS, 0xffffffffU);

	/* Succeeded: the card is in the transfer state and ready for data. */
	return 0;
}

/*
 * Resets the named lines of the controller and waits for the reset to end.
 *
 * Resetting the command line clears the command inhibit; resetting the data
 * line clears the data inhibit and the buffer.  Neither touches the clock,
 * the bus width or the card.
 */
static int
reset_lines(
	uint8_t lines)
{
	uint64_t deadline;
	uint8_t pending;

	/* Starts the reset of the lines. */
	w8(REG_SOFTWARE_RESET, lines);

	/* Waits until the controller clears the reset bits it was given. */
	deadline = counter() + frequency();
	for (;;) {
		pending = (uint8_t)(r8(REG_SOFTWARE_RESET) & lines);
		if (pending == 0)
			break;

		/* A reset that never ends is reported. */
		if (counter() >= deadline) {
			kern_logf("sdhci: the reset of lines %x did not end\n",
				  (unsigned)lines);
			return ETIMEDOUT;
		}
	}

	/* Acknowledges whatever the interrupted operation had raised. */
	w32(REG_INT_STATUS, 0xffffffffU);

	/* Succeeded: the reset lines take commands again. */
	return 0;
}

/*
 * Tells the card to stop sending or receiving a block.
 *
 * CMD12 answers with R1b: the card may hold the data line busy while it
 * finishes, and the controller keeps the data inhibit set until then.
 */
static int
stop_transmission(
	void)
{
	int error;

	/* Sends the stop command. */
	error = command(12, 0, CMD_RESP_BUSY | CMD_CRC | CMD_INDEX, 0);
	if (error != 0)
		return error;

	/* Waits for the card to release the data line. */
	error = wait_bits(REG_PRESENT, PRESENT_DATA_INHIBIT, 0);
	if (error != 0)
		return error;

	/* Succeeded: the card has left the transfer. */
	return 0;
}

/* Spins for the given number of microseconds on the generic timer. */
static void
pause_microseconds(
	unsigned microseconds)
{
	uint64_t deadline;

	/* Converts the pause to timer ticks from now. */
	deadline = counter() + frequency() * microseconds / 1000000U;

	/* Spins until the timer passes the end of the pause. */
	while (counter() < deadline)
		;
}

/*
 * Serves one block request from the disk layer.
 *
 * The card is driven by PIO under the unit lock, a run of up to
 * SD_RUN_BLOCKS blocks per command.  A flush has nothing to do: the card
 * has no write cache enabled, and a written block is reported only once
 * the card has programmed it.
 */
static int
sd_submit(
	struct disk *disk,
	struct bio *bio)
{
	uint8_t *data;
	uint32_t done;
	uint32_t run;
	size_t transferred;
	int error;

	(void)disk;

	/* Picks the requests the card serves. */
	data = bio->b_data;
	error = 0;
	if (bio->b_op == BIO_FLUSH) {
		/* A flush has nothing left to write. */
		error = 0;
	} else if (bio->b_op != BIO_READ && bio->b_op != BIO_WRITE) {
		/* Discards and the rest are not served. */
		error = EOPNOTSUPP;
	} else {
		/* Moves every block of the request, stopping at the first failure. */
		mutex_lock(&unit.lock);

		/* A card marked failed is not tried again. */
		if (unit.failed)
			error = EIO;

		/*
		 * Moves the blocks in order, a run per command.  The disk
		 * layer splits at d_max_transfer_blocks, so a request is
		 * normally one run; a longer one is still served in runs.
		 */
		run = 0;
		for (done = 0;
		     done < bio->b_block_count && error == 0;
		     done += run) {
			/* The run, or what is left of the request when that is less. */
			run = bio->b_block_count - done;
			if (run > SD_RUN_BLOCKS)
				run = SD_RUN_BLOCKS;

			/* Moves the run, falling back to single blocks if it fails. */
			error = transfer_run_recovering(
				bio->b_op == BIO_WRITE,
				bio->b_mapped_block + done,
				run,
				data + (size_t)done * SD_BLOCK_BYTES);
		}

		mutex_unlock(&unit.lock);
	}

	/* Reports a failed request with what it asked for. */
	if (error != 0) {
		kern_logf("sdhci: op=%u lba=%llu count=%u error=%d\n",
			  (unsigned)bio->b_op,
			  (unsigned long long)bio->b_mapped_block,
			  bio->b_block_count,
			  error);
	}

	/* Completes the request with the bytes it moved. */
	transferred = 0;
	if (error == 0)
		transferred = (size_t)bio->b_block_count * SD_BLOCK_BYTES;
	bio_complete(bio, error, transferred);

	/* Succeeded: the request was completed, with its own error if any. */
	return 0;
}

/* Supports the sd ioctl operation. */
static int
sd_ioctl(
	struct disk *disk,
	unsigned long request,
	void *argument)
{
	(void)disk;
	(void)request;
	(void)argument;

	/* Failed. */
	return EOPNOTSUPP;
}
