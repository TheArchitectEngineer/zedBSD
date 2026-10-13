/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Runs the Raspberry Pi 4 SD driver on the host against a fake controller
 * and card, and checks the commands it sends (BUG-295): a run of blocks is
 * one CMD18 or CMD25 with the block count and the automatic CMD12, a single
 * block is CMD17 or CMD24, and a run that fails is recovered (CMD13, CMD12)
 * and moved again one block at a time.
 *
 * The build copies the driver with its include lines removed and its two
 * reads of the generic timer replaced by host_counter() and
 * host_frequency(), and this file includes that copy after the few kernel
 * types and functions it uses.
 */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The disk flag and the statistics kind the driver sets. */
#define DISK_REMOVABLE 0x1U
#define DISK_STATS_KIND_SDMMC 3
#define LOCK_RANK_DISK 1

/* The fake card's capacity in blocks. */
#define CARD_BLOCKS 1024U

/* The most commands one test records. */
#define LOG_MAX 512U

/* The interrupt status bit the fake sets for a data time-out. */
#define FAKE_DATA_TIMEOUT_ERROR 0x00100000U

/* The kinds of request the driver serves. */
enum bio_op {
	BIO_READ,
	BIO_WRITE,
	BIO_FLUSH,
	BIO_DISCARD
};

struct disk;
struct bio;

/* The disk operations the driver publishes. */
struct disk_ops {
	int (*submit)(struct disk *disk, struct bio *bio);
	int (*ioctl)(struct disk *disk, unsigned long request, void *argument);
};

/* The fields of a disk that the driver fills in. */
struct disk {
	char d_name[16];
	uint32_t d_flags;
	uint32_t d_block_size;
	uint64_t d_block_count;
	uint32_t d_max_transfer_blocks;
	const struct disk_ops *d_ops;
	void *d_data;
};

/* One block request, as the disk layer hands it to the driver. */
struct bio {
	enum bio_op b_op;
	void *b_data;
	uint32_t b_block_count;
	uint64_t b_mapped_block;
	int completed;
	int error;
	size_t transferred;
};

/* The driver's lock; the test runs on one thread. */
struct mutex {
	int held;
};

/* One command the fake controller saw, or the CMD12 it sent by itself. */
struct log_entry {
	uint32_t index;
	uint32_t argument;
	uint16_t mode;
	uint16_t count;
	int automatic;
};

/*
 * The fake controller's registers, the word at each offset.  The driver
 * reaches them through DIRECT_BASE, which the test cancels out.
 */
static uint32_t fake_registers[0x100U / 4U];

/* The interrupt status the fake has raised and the driver not yet cleared. */
static uint32_t fake_status;

/* The block size and count, and the transfer mode, the driver last wrote. */
static uint32_t fake_block_word;
static uint16_t fake_mode;

/* The card: its blocks, its state in the R1 status, and a pending ACMD. */
static uint8_t card_data[CARD_BLOCKS * 512U];
static uint32_t card_state;
static int card_app_command;

/* The data phase under way: direction, position, blocks done, words moved. */
static int data_active;
static int data_write;
static uint32_t data_block;
static uint32_t data_count;
static uint32_t data_done;
static uint32_t data_word;
static uint32_t data_auto_stop;
static uint32_t data_command;
static uint8_t data_fifo[512];

/* A fault to inject: the command it hits, the block of the run, how often. */
static uint32_t fault_command;
static uint32_t fault_command_other;
static uint32_t fault_block;
static unsigned fault_remaining;

/* The commands seen since the last clear. */
static struct log_entry command_log[LOG_MAX];
static unsigned command_count;

/* The fake generic timer. */
static uint64_t host_time;

/* Whether the driver's messages are printed. */
static int verbose;

/* The disk the driver allocates. */
static struct disk fake_disk;

/* How many checks failed. */
static int failures;

static uint64_t host_counter(void);
static uint64_t host_frequency(void);
struct disk *disk_alloc(void);
int disk_create(struct disk *disk);
void disk_set_stats_kind(struct disk *disk, int kind);
void bio_complete(struct bio *bio, int error, size_t transferred);
int mutex_init(struct mutex *lock, int rank, const char *name);
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
void kern_logf(const char *format, ...);
uint32_t kern_mmio_read32(const volatile void *address);
void kern_mmio_write32(volatile void *address, uint32_t value);
static void fake_command(uint32_t index, uint32_t argument);
static void fake_next_block(void);
static void fake_block_done(void);
static void fake_buffer_write(uint32_t value);
static uint32_t fake_buffer_read(void);
static void log_command(uint32_t index, uint32_t argument, uint16_t mode, uint16_t count, int automatic);
static void check(int condition, const char *what);
static void fill_pattern(uint8_t *data, uint32_t blocks, uint32_t seed);
static void reset_log(void);
static int submit(enum bio_op op, uint64_t block, uint32_t count, void *data, struct bio *bio);
static void test_write_run(void);
static void test_read_run(void);
static void test_single_block(void);
static void test_long_request(void);
static void test_write_fault(void);
static void test_read_fault(void);
static void test_persistent_fault(void);

#include "rpi4-sdhci-under-test.c"

/*
 * Runs every test and reports the result.
 */
int
main(
	int argc,
	char **argv)
{
	uintptr_t physical;
	int different;
	int error;

	/* "-v" prints the driver's own messages. */
	verbose = 0;
	different = 1;
	if (argc > 1)
		different = strcmp(argv[1], "-v");
	if (different == 0)
		verbose = 1;

	/* The physical base that DIRECT_BASE turns into the fake registers. */
	physical = (uintptr_t)fake_registers - (uintptr_t)DIRECT_BASE;

	/* Brings the driver up against the fake card. */
	error = drv_rpi4_sdhci_init(physical);
	check(error == 0, "the driver initializes the fake card");
	check(fake_disk.d_max_transfer_blocks == SD_RUN_BLOCKS,
	      "the disk takes 128-block requests");
	check(unit.high_capacity == 1, "the card is addressed in blocks");
	if (failures != 0) {
		fprintf(stderr, "rpi4-sdhci-host-test: FAIL (%d)\n", failures);
		return 1;
	}

	/* Runs the request-order and error-path tests. */
	test_write_run();
	test_read_run();
	test_single_block();
	test_long_request();
	test_write_fault();
	test_read_fault();
	test_persistent_fault();

	/* Reports the failed checks. */
	if (failures != 0) {
		fprintf(stderr, "rpi4-sdhci-host-test: FAIL (%d)\n", failures);
		return 1;
	}

	/* Reports that every check held. */
	printf("rpi4-sdhci-host-test: PASS\n");

	/* Succeeded: every check held. */
	return 0;
}

/* Advances the fake generic timer a little on every read. */
static uint64_t
host_counter(
	void)
{
	/* A thousandth of the frequency per read, so a deadline passes in a few thousand polls. */
	host_time += 1000U;

	/* Reports the time now. */
	return host_time;
}

/* Reports the fake generic timer's frequency. */
static uint64_t
host_frequency(
	void)
{
	/* One million ticks a second. */
	return 1000000U;
}

/* Hands out the one disk the driver needs. */
struct disk *
disk_alloc(
	void)
{
	/* The disk starts empty. */
	memset(&fake_disk, 0, sizeof(fake_disk));

	/* Succeeded: the disk. */
	return &fake_disk;
}

/* Publishes the disk, which the test has nothing to do for. */
int
disk_create(
	struct disk *disk)
{
	(void)disk;

	/* Succeeded. */
	return 0;
}

/* Records the statistics kind, which the test does not check. */
void
disk_set_stats_kind(
	struct disk *disk,
	int kind)
{
	(void)disk;
	(void)kind;
}

/* Records how a request ended. */
void
bio_complete(
	struct bio *bio,
	int error,
	size_t transferred)
{
	/* The request is done with this outcome. */
	bio->completed = 1;
	bio->error = error;
	bio->transferred = transferred;
}

/* Initializes the lock. */
int
mutex_init(
	struct mutex *lock,
	int rank,
	const char *name)
{
	(void)rank;
	(void)name;

	/* The lock starts free. */
	lock->held = 0;

	/* Succeeded. */
	return 0;
}

/* Takes the lock; a lock taken twice is a failure of the driver. */
void
mutex_lock(
	struct mutex *lock)
{
	/* The driver never nests the lock. */
	check(lock->held == 0, "the unit lock is not taken twice");
	lock->held = 1;
}

/* Releases the lock. */
void
mutex_unlock(
	struct mutex *lock)
{
	/* The driver releases only what it holds. */
	check(lock->held == 1, "the unit lock is released by its holder");
	lock->held = 0;
}

/* Prints a driver message when asked to. */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	/* Quiet unless "-v" was given. */
	if (!verbose)
		return;

	/* Prints the message. */
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

/* Reads a register of the fake controller. */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t offset;
	uint32_t word;

	/* The register's offset from the fake base. */
	offset = (uint32_t)((uintptr_t)address - (uintptr_t)fake_registers);

	/* The buffer port gives the next word of the block being read. */
	if (offset == REG_BUFFER)
		return fake_buffer_read();

	/* The interrupt status is what the fake raised. */
	if (offset == REG_INT_STATUS)
		return fake_status;

	/* The present state shows no inhibit: the fake is never busy between calls. */
	if (offset == REG_PRESENT)
		return 0;

	/* The clock reports itself stable once enabled, and a reset ends at once. */
	if (offset == REG_CLOCK_CONTROL) {
		word = fake_registers[offset / 4U] & 0x00ffffffU;
		if ((word & 1U) != 0)
			word |= 2U;
		return word;
	}

	/* A 100 MHz base clock. */
	if (offset == REG_CAPABILITIES)
		return 100U << 8;

	/* Anything else reads back what was written. */
	return fake_registers[offset / 4U];
}

/* Writes a register of the fake controller and acts on it. */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint32_t offset;
	uint8_t reset;

	/* The register's offset from the fake base. */
	offset = (uint32_t)((uintptr_t)address - (uintptr_t)fake_registers);

	/* Picks the register's behaviour. */
	switch (offset) {
	case REG_BUFFER:
		fake_buffer_write(value);
		return;
	case REG_INT_STATUS:
		/* The status bits are cleared by writing them. */
		fake_status &= ~value;
		return;
	case REG_BLOCK_SIZE:
		fake_block_word = value;
		return;
	case REG_TRANSFER_MODE:
		/* The transfer mode and the command go in one write, which starts the command. */
		fake_mode = (uint16_t)value;
		fake_command((value >> 24) & 0x3fU, fake_registers[REG_ARGUMENT / 4U]);
		return;
	case REG_CLOCK_CONTROL:
		/* A reset of the data line ends the data phase; the reset ends at once. */
		reset = (uint8_t)(value >> 24);
		if ((reset & (SOFTWARE_RESET_DATA | SOFTWARE_RESET_ALL)) != 0)
			data_active = 0;
		fake_registers[offset / 4U] = value & 0x00ffffffU;
		return;
	default:
		fake_registers[offset / 4U] = value;
		return;
	}
}

/* Answers one command the way an SD card in the transfer state would. */
static void
fake_command(
	uint32_t index,
	uint32_t argument)
{
	uint32_t response;
	uint32_t count;
	int application;

	/* The command follows CMD55 when it is an application command. */
	application = card_app_command;
	card_app_command = 0;
	response = 0;
	count = (fake_block_word >> 16) & 0xffffU;
	log_command(index, argument, fake_mode, (uint16_t)count, 0);

	/* Picks the card's answer. */
	switch (index) {
	case 55U:
		card_app_command = 1;
		response = 0x00000120U;
		break;
	case 41U:
		/* ACMD41: powered up, high capacity. */
		if (application)
			response = 0xc0ff8000U;
		break;
	case 3U:
		response = 0x12340000U;
		break;
	case 7U:
		/* CMD7 selects the card, which enters the transfer state. */
		card_state = CARD_STATE_TRANSFER;
		break;
	case 13U:
		/* CMD13: the state, and a card still programming finishes now. */
		response = (card_state << CARD_STATE_SHIFT) | CARD_READY_FOR_DATA;
		if (card_state == CARD_STATE_PROGRAM)
			card_state = CARD_STATE_TRANSFER;
		break;
	case 12U:
		/* CMD12 sent by the driver: the card stops and programs what it took. */
		data_active = 0;
		card_state = CARD_STATE_PROGRAM;
		break;
	case 17U:
	case 18U:
	case 24U:
	case 25U:
		/* A data command starts a data phase of one block or of the counted run. */
		data_active = 1;
		data_write = index == 24U || index == 25U;
		data_block = argument;
		data_count = 1;
		if (index == 18U || index == 25U)
			data_count = count;
		data_done = 0;
		data_word = 0;
		data_command = index;
		data_auto_stop = (fake_mode & XFER_AUTO_CMD12) != 0;
		card_state = CARD_STATE_DATA;
		if (data_write)
			card_state = CARD_STATE_RECEIVE;
		fake_status |= INT_CMD_COMPLETE;
		fake_next_block();
		fake_registers[REG_RESPONSE0 / 4U] = 0x00000900U;
		return;
	default:
		break;
	}

	/* Every other command completes at once with its response. */
	fake_registers[REG_RESPONSE0 / 4U] = response;
	fake_status |= INT_CMD_COMPLETE;
}

/* Readies the buffer for the next block of the run, or injects the fault. */
static void
fake_next_block(
	void)
{
	/* A fault aimed at this block of this command ends the data phase with an error. */
	if (fault_remaining != 0 &&
	    (data_command == fault_command || data_command == fault_command_other) &&
	    data_done == fault_block) {
		fault_remaining--;
		data_active = 0;
		fake_status |= INT_ERROR | FAKE_DATA_TIMEOUT_ERROR;
		return;
	}

	/* A read fills the buffer from the card. */
	data_word = 0;
	if (!data_write) {
		memcpy(data_fifo, card_data + (size_t)(data_block + data_done) * 512U, 512U);
		fake_status |= INT_BUFFER_READ_READY;
		return;
	}

	/* A write waits for the driver to fill the buffer. */
	fake_status |= INT_BUFFER_WRITE_READY;
}

/* Ends a block once the buffer port has moved all of it. */
static void
fake_block_done(
	void)
{
	/* A written block goes onto the card. */
	if (data_write)
		memcpy(card_data + (size_t)(data_block + data_done) * 512U, data_fifo, 512U);
	data_done++;

	/* More blocks to go: the buffer is readied for the next. */
	if (data_done < data_count) {
		fake_next_block();
		return;
	}

	/* The run is over; the controller stops it with CMD12 when told to. */
	data_active = 0;
	if (data_auto_stop)
		log_command(12U, 0, 0, 0, 1);
	card_state = CARD_STATE_TRANSFER;
	fake_status |= INT_TRANSFER_COMPLETE;
}

/* Takes one word written to the buffer port. */
static void
fake_buffer_write(
	uint32_t value)
{
	/* A word outside a write data phase is a driver error. */
	if (!data_active || !data_write) {
		check(0, "the buffer port is written only during a write");
		return;
	}

	/* The word lands in the buffer; a full buffer ends the block. */
	memcpy(data_fifo + data_word * 4U, &value, 4U);
	data_word++;
	if (data_word == 128U)
		fake_block_done();
}

/* Gives one word of the block being read from the buffer port. */
static uint32_t
fake_buffer_read(
	void)
{
	uint32_t value;

	/* A word outside a read data phase is a driver error. */
	if (!data_active || data_write) {
		check(0, "the buffer port is read only during a read");
		return 0;
	}

	/* The next word of the buffer; an empty buffer ends the block. */
	memcpy(&value, data_fifo + data_word * 4U, 4U);
	data_word++;
	if (data_word == 128U)
		fake_block_done();

	/* Reports the word. */
	return value;
}

/* Records one command in the log. */
static void
log_command(
	uint32_t index,
	uint32_t argument,
	uint16_t mode,
	uint16_t count,
	int automatic)
{
	/* A test that sends more commands than the log holds is a failure. */
	if (command_count == LOG_MAX) {
		check(0, "the command log does not overflow");
		return;
	}

	/* Appends the command. */
	command_log[command_count].index = index;
	command_log[command_count].argument = argument;
	command_log[command_count].mode = mode;
	command_log[command_count].count = count;
	command_log[command_count].automatic = automatic;
	command_count++;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;

	/* Names the failed check and counts it. */
	fprintf(stderr, "rpi4-sdhci-host-test: FAIL: %s\n", what);
	failures++;
}

/* Fills blocks with bytes that differ per block and per seed. */
static void
fill_pattern(
	uint8_t *data,
	uint32_t blocks,
	uint32_t seed)
{
	size_t index;

	/* Every byte depends on its position and the seed. */
	for (index = 0; index < (size_t)blocks * 512U; index++)
		data[index] = (uint8_t)(index * 7U + seed * 13U + index / 512U);
}

/* Forgets the commands of the previous test. */
static void
reset_log(
	void)
{
	command_count = 0;
}

/* Submits one request to the driver and checks that it completed. */
static int
submit(
	enum bio_op op,
	uint64_t block,
	uint32_t count,
	void *data,
	struct bio *bio)
{
	/* Describes the request. */
	memset(bio, 0, sizeof(*bio));
	bio->b_op = op;
	bio->b_data = data;
	bio->b_block_count = count;
	bio->b_mapped_block = block;

	/* Hands it to the driver, which completes it before returning. */
	(void)fake_disk.d_ops->submit(&fake_disk, bio);
	check(bio->completed == 1, "the request is completed");

	/* Reports the request's own outcome. */
	return bio->error;
}

/* A 128-block write is one CMD25 with the count and the automatic CMD12. */
static void
test_write_run(
	void)
{
	static uint8_t data[128U * 512U];
	struct bio bio;
	int error;

	/* Writes 128 blocks at block 100. */
	fill_pattern(data, 128U, 1U);
	reset_log();
	error = submit(BIO_WRITE, 100U, 128U, data, &bio);
	check(error == 0, "write run: the request succeeds");
	check(bio.transferred == sizeof(data), "write run: every byte is reported moved");

	/* The commands: CMD25 for the whole run, then the controller's CMD12. */
	check(command_count == 2U, "write run: two commands");
	check(command_log[0].index == 25U, "write run: CMD25");
	check(command_log[0].argument == 100U, "write run: at block 100");
	check(command_log[0].count == 128U, "write run: block count 128");
	check(command_log[0].mode == (XFER_MULTI_BLOCK | XFER_BLOCK_COUNT_ENABLE | XFER_AUTO_CMD12),
	      "write run: multiple blocks, counted, stopped by the automatic CMD12");
	check(command_log[1].index == 12U && command_log[1].automatic,
	      "write run: the controller's CMD12 ends it");

	/* The card holds what was written. */
	check(memcmp(card_data + 100U * 512U, data, sizeof(data)) == 0,
	      "write run: the card holds the data");
}

/* A 128-block read is one CMD18 with the count and the automatic CMD12. */
static void
test_read_run(
	void)
{
	static uint8_t data[128U * 512U];
	struct bio bio;
	int error;

	/* Reads back the 128 blocks at block 100. */
	memset(data, 0, sizeof(data));
	reset_log();
	error = submit(BIO_READ, 100U, 128U, data, &bio);
	check(error == 0, "read run: the request succeeds");

	/* The commands: CMD18 for the whole run, then the controller's CMD12. */
	check(command_count == 2U, "read run: two commands");
	check(command_log[0].index == 18U, "read run: CMD18");
	check(command_log[0].argument == 100U, "read run: at block 100");
	check(command_log[0].count == 128U, "read run: block count 128");
	check(command_log[0].mode == (XFER_READ | XFER_MULTI_BLOCK | XFER_BLOCK_COUNT_ENABLE | XFER_AUTO_CMD12),
	      "read run: a counted multiple-block read stopped by the automatic CMD12");
	check(command_log[1].index == 12U && command_log[1].automatic,
	      "read run: the controller's CMD12 ends it");

	/* The buffer holds what the card had. */
	check(memcmp(card_data + 100U * 512U, data, sizeof(data)) == 0,
	      "read run: the data matches the card");
}

/* A one-block request stays a single-block command without the stop. */
static void
test_single_block(
	void)
{
	static uint8_t data[512U];
	static uint8_t back[512U];
	struct bio bio;
	int error;

	/* Writes one block at block 5. */
	fill_pattern(data, 1U, 2U);
	reset_log();
	error = submit(BIO_WRITE, 5U, 1U, data, &bio);
	check(error == 0, "single write: the request succeeds");
	check(command_count == 1U, "single write: one command");
	check(command_log[0].index == 24U, "single write: CMD24");
	check(command_log[0].mode == 0, "single write: no multiple-block bits");

	/* Reads it back. */
	reset_log();
	error = submit(BIO_READ, 5U, 1U, back, &bio);
	check(error == 0, "single read: the request succeeds");
	check(command_count == 1U, "single read: one command");
	check(command_log[0].index == 17U, "single read: CMD17");
	check(command_log[0].mode == XFER_READ, "single read: only the read direction");
	check(memcmp(back, data, sizeof(data)) == 0, "single read: the data matches");

	/* A flush sends nothing to the card. */
	reset_log();
	error = submit(BIO_FLUSH, 0, 0, NULL, &bio);
	check(error == 0, "flush: the request succeeds");
	check(command_count == 0U, "flush: no command");
}

/* A request longer than one run is served as runs of at most 128 blocks. */
static void
test_long_request(
	void)
{
	static uint8_t data[200U * 512U];
	struct bio bio;
	int error;

	/* Writes 200 blocks at block 400. */
	fill_pattern(data, 200U, 3U);
	reset_log();
	error = submit(BIO_WRITE, 400U, 200U, data, &bio);
	check(error == 0, "long write: the request succeeds");

	/* The runs: 128 blocks, then the 72 left. */
	check(command_count == 4U, "long write: two runs, each with its CMD12");
	check(command_log[0].index == 25U && command_log[0].argument == 400U &&
	      command_log[0].count == 128U,
	      "long write: the first run is 128 blocks at block 400");
	check(command_log[2].index == 25U && command_log[2].argument == 528U &&
	      command_log[2].count == 72U,
	      "long write: the second run is 72 blocks at block 528");
	check(memcmp(card_data + 400U * 512U, data, sizeof(data)) == 0,
	      "long write: the card holds the data");
}

/*
 * A write run that fails at its sixth block is recovered (CMD13 finds the
 * card receiving, CMD12 stops it, CMD13 waits out the programming) and
 * written again one block at a time.
 */
static void
test_write_fault(
	void)
{
	static uint8_t data[16U * 512U];
	struct bio bio;
	unsigned index;
	unsigned first_single;
	unsigned singles;
	unsigned stop;
	int in_order;
	int error;

	/* Writes 16 blocks at block 300, failing once at block 5 of the run. */
	fill_pattern(data, 16U, 4U);
	fault_command = 25U;
	fault_command_other = 25U;
	fault_block = 5U;
	fault_remaining = 1U;
	reset_log();
	error = submit(BIO_WRITE, 300U, 16U, data, &bio);
	check(error == 0, "write fault: the request succeeds after the fallback");
	check(fault_remaining == 0, "write fault: the fault was hit");
	check(command_log[0].index == 25U && command_log[0].count == 16U,
	      "write fault: the run is tried with CMD25 first");

	/* Finds the stop the recovery sent, and the single-block writes after it. */
	stop = 0;
	first_single = 0;
	singles = 0;
	in_order = 1;
	for (index = 1; index < command_count; index++) {
		if (command_log[index].index == 12U && !command_log[index].automatic && stop == 0)
			stop = index;
		if (command_log[index].index == 25U)
			check(0, "write fault: CMD25 is not tried again");
		if (command_log[index].index == 24U) {
			if (singles == 0)
				first_single = index;
			if (command_log[index].argument != 300U + singles)
				in_order = 0;
			singles++;
		}
	}

	/* The recovery came first, then every block on its own and in order. */
	check(stop != 0, "write fault: the recovery stops the card with CMD12");
	check(command_log[1].index == 13U, "write fault: the recovery asks the card's state first");
	check(first_single > stop, "write fault: the single blocks follow the recovery");
	check(singles == 16U, "write fault: all 16 blocks are written again one at a time");
	check(in_order, "write fault: the single blocks go in order");
	check(memcmp(card_data + 300U * 512U, data, sizeof(data)) == 0,
	      "write fault: the card holds the data");
	check(unit.failed == 0, "write fault: the card is not marked failed");
}

/* A read run that fails before its first block is read again one block at a time. */
static void
test_read_fault(
	void)
{
	static uint8_t data[8U * 512U];
	struct bio bio;
	unsigned index;
	unsigned singles;
	int error;

	/* Reads 8 blocks at block 300, failing once at the first block of the run. */
	memset(data, 0, sizeof(data));
	fault_command = 18U;
	fault_command_other = 18U;
	fault_block = 0;
	fault_remaining = 1U;
	reset_log();
	error = submit(BIO_READ, 300U, 8U, data, &bio);
	check(error == 0, "read fault: the request succeeds after the fallback");
	check(fault_remaining == 0, "read fault: the fault was hit");

	/* Counts the single-block reads. */
	singles = 0;
	for (index = 0; index < command_count; index++) {
		if (command_log[index].index == 17U)
			singles++;
	}

	/* Every block was read again and the data is the card's. */
	check(singles == 8U, "read fault: all 8 blocks are read again one at a time");
	check(memcmp(card_data + 300U * 512U, data, sizeof(data)) == 0,
	      "read fault: the data matches the card");
}

/* A card that fails every write leaves the request failed after the retries of its first block. */
static void
test_persistent_fault(
	void)
{
	static uint8_t data[4U * 512U];
	struct bio bio;
	unsigned index;
	unsigned singles;
	int error;

	/* Writes 4 blocks at block 600 to a card that fails every write. */
	fill_pattern(data, 4U, 5U);
	fault_command = 25U;
	fault_command_other = 24U;
	fault_block = 0;
	fault_remaining = 1000U;
	reset_log();
	error = submit(BIO_WRITE, 600U, 4U, data, &bio);
	fault_remaining = 0;
	check(error != 0, "persistent fault: the request fails");
	check(bio.transferred == 0, "persistent fault: no bytes are reported moved");

	/* The first block is tried as often as a block is, and the rest are not. */
	singles = 0;
	for (index = 0; index < command_count; index++) {
		if (command_log[index].index == 24U) {
			check(command_log[index].argument == 600U,
			      "persistent fault: only the first block is retried");
			singles++;
		}
	}

	/* The block had its three tries, and the card still takes commands. */
	check(singles == SD_TRANSFER_ATTEMPTS, "persistent fault: the first block is tried three times");
	check(unit.failed == 0, "persistent fault: a card that recovers is not marked failed");
}
