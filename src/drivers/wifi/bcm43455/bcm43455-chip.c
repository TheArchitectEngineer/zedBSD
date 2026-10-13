/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BCM43455: the backplane, the enumeration ROM, the firmware download and
 * the start of the frame function.
 *
 * The chip's cores sit on an AXI backplane.  The host reaches it through
 * function 1: three window registers hold an address's high bits and the
 * low fifteen go into the function 1 address.  Each core has a wrapper
 * whose reset and I/O control registers start, stop and halt it.  The
 * sequence of register operations and every value are recorded with their
 * public sources in plan/ws206/phase004/values.md; no code of another
 * driver was copied.
 */

#include "drivers/wifi/bcm43455/bcm43455-chip.h"
#include "drivers/wifi/bcm43455/bcm43455-bytes.h"
#include "drivers/wifi/bcm43455/bcm43455-hw.h"
#include "drivers/wifi/bcm43455/bcm43455-firmware.h"
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <uapi/errno.h>

/* How long the slow (ALP) clock may take to come, in milliseconds. */
#define ALP_WAIT_MS			50U

/* How long the fast (HT) clock may take once the firmware runs. */
#define HT_WAIT_MS			3000U

/* How long function 2 may take to report ready once the firmware runs. */
#define FRAMES_READY_MS			3000U

/* How many times a core's reset is let go before it is given up. */
#define RESET_RELEASE_TRIES		50U

/* How many bytes at each end of a download are read back. */
#define VERIFY_BYTES			64U

/* The most RAM a 43455 can have; a bank table that adds up to more is garbled. */
#define RAM_SIZE_MAX			(2U * 1024U * 1024U)

static int set_window(struct bcm43455_chip *chip, uint32_t address);
static int transfer_memory(struct bcm43455_chip *chip, uint32_t address, uint8_t *data, const uint8_t *source, size_t length, int write);
static int start_backplane_clock(struct bcm43455_chip *chip);
static int scan_erom(struct bcm43455_chip *chip, uint32_t erom);
static void keep_core(struct bcm43455_chip *chip, const struct bcm43455_core *core);
static int read_ram_size(struct bcm43455_chip *chip);
static int disable_core(struct bcm43455_chip *chip, const struct bcm43455_core *core, uint32_t prereset, uint32_t reset);
static int reset_core(struct bcm43455_chip *chip, const struct bcm43455_core *core, uint32_t prereset, uint32_t reset, uint32_t postreset);
static int halt_cores(struct bcm43455_chip *chip);
static int verify_memory(struct bcm43455_chip *chip, uint32_t address, const uint8_t *expected, size_t length);
static int wait_clock(struct bcm43455_chip *chip, uint8_t mask, unsigned timeout_ms);

/*
 * Starts the backplane clock, identifies the chip and finds its cores.
 */
int
bcm43455_chip_attach(
	struct bcm43455_chip *chip,
	const struct bcm43455_bus_ops *ops,
	void *context)
{
	uint32_t identity;
	uint32_t erom;
	unsigned bus;
	int error;

	/* Refuses a missing chip or bus. */
	if (chip == NULL || ops == NULL)
		return EINVAL;

	/* Starts from a chip that knows nothing. */
	kern_memset(chip, 0, sizeof(*chip));
	chip->ops = ops;
	chip->context = context;
	chip->ram_base = BCM43455_RAM_BASE;

	/* Runs the backplane on its slow clock, which needs no firmware. */
	error = start_backplane_clock(chip);
	if (error != 0)
		return error;

	/* Reads the chip id register of the chip common core. */
	error = bcm43455_chip_read32(chip, BCM43455_CHIPCOMMON_BASE + BCM43455_CHIPCOMMON_ID, &identity);
	if (error != 0)
		return error;
	chip->id = (uint16_t)(identity & BCM43455_CHIP_ID_MASK);
	chip->revision = (identity >> BCM43455_CHIP_REVISION_SHIFT) & BCM43455_CHIP_REVISION_MASK;
	bus = (identity >> BCM43455_CHIP_BUS_SHIFT) & BCM43455_CHIP_BUS_MASK;
	kern_logf("bcm43455: chip %04x revision %u bus type %u\n", (unsigned)chip->id, chip->revision, bus);

	/* Refuses any chip but a 43455 on an AXI backplane. */
	if (chip->id != BCM43455_CHIP_ID ||
	    chip->revision < BCM43455_CHIP_REVISION_MIN ||
	    bus != BCM43455_CHIP_BUS_AXI)
		return ENODEV;

	/* Reads the capabilities, logged for the save-restore question (R8). */
	error = bcm43455_chip_read32(chip, BCM43455_CHIPCOMMON_BASE + BCM43455_CHIPCOMMON_CAPABILITY, &chip->capability);
	if (error != 0)
		return error;
	error = bcm43455_chip_read32(chip, BCM43455_CHIPCOMMON_BASE + BCM43455_CHIPCOMMON_CAPABILITY_EXT, &chip->capability_ext);
	if (error != 0)
		return error;
	kern_logf("bcm43455: capabilities %08x, extended %08x\n", chip->capability, chip->capability_ext);

	/* Finds the cores in the enumeration ROM. */
	error = bcm43455_chip_read32(chip, BCM43455_CHIPCOMMON_BASE + BCM43455_CHIPCOMMON_EROM, &erom);
	if (error != 0)
		return error;
	error = scan_erom(chip, erom);
	if (error != 0)
		return error;

	/* Measures the RAM the ARM core runs from. */
	error = read_ram_size(chip);
	if (error != 0)
		return error;
	kern_logf(
		"bcm43455: SDIO core %08x, ARM CR4 %08x (wrapper %08x), D11 wrapper %08x, RAM %06x + %06x\n",
		chip->sdio.base,
		chip->cr4.base,
		chip->cr4.wrapper,
		chip->d11.wrapper,
		chip->ram_base,
		chip->ram_size);

	/* Succeeded: the chip's cores and RAM are known. */
	return 0;
}

/*
 * Reads one 32-bit backplane register.
 */
int
bcm43455_chip_read32(
	struct bcm43455_chip *chip,
	uint32_t address,
	uint32_t *value)
{
	uint8_t bytes[4];
	uint32_t offset;
	int error;

	/* Points the window at the register. */
	error = set_window(chip, address);
	if (error != 0)
		return error;

	/* Reads the four bytes through function 1. */
	offset = (address & BCM43455_WINDOW_OFFSET_MASK) | BCM43455_WINDOW_ACCESS_32;
	error = chip->ops->read(chip->context, BCM43455_FUNCTION_BACKPLANE, offset, bytes, sizeof(bytes));
	if (error != 0)
		return error;

	/* Succeeded: the register's value. */
	*value = bcm43455_get_le32(bytes);
	return 0;
}

/*
 * Writes one 32-bit backplane register.
 */
int
bcm43455_chip_write32(
	struct bcm43455_chip *chip,
	uint32_t address,
	uint32_t value)
{
	uint8_t bytes[4];
	uint32_t offset;
	int error;

	/* Points the window at the register. */
	error = set_window(chip, address);
	if (error != 0)
		return error;

	/* Writes the four bytes through function 1. */
	bcm43455_put_le32(bytes, value);
	offset = (address & BCM43455_WINDOW_OFFSET_MASK) | BCM43455_WINDOW_ACCESS_32;
	error = chip->ops->write(chip->context, BCM43455_FUNCTION_BACKPLANE, offset, bytes, sizeof(bytes));
	if (error != 0)
		return error;

	/* Succeeded: the register holds the value. */
	return 0;
}

/*
 * Writes backplane memory.
 */
int
bcm43455_chip_write_memory(
	struct bcm43455_chip *chip,
	uint32_t address,
	const uint8_t *data,
	size_t length)
{
	int error;

	/* Moves the bytes through the bounce buffer. */
	error = transfer_memory(chip, address, NULL, data, length, 1);
	if (error != 0)
		return error;

	/* Succeeded: the memory holds the bytes. */
	return 0;
}

/*
 * Reads backplane memory.
 */
int
bcm43455_chip_read_memory(
	struct bcm43455_chip *chip,
	uint32_t address,
	uint8_t *data,
	size_t length)
{
	int error;

	/* Moves the bytes straight into the caller's buffer. */
	error = transfer_memory(chip, address, data, NULL, length, 0);
	if (error != 0)
		return error;

	/* Succeeded: data holds the memory's bytes. */
	return 0;
}

/*
 * Writes the firmware and the NVRAM and starts the ARM core.
 */
int
bcm43455_chip_download(
	struct bcm43455_chip *chip,
	const uint8_t *firmware,
	size_t firmware_length,
	const uint8_t *nvram,
	size_t nvram_length)
{
	uint32_t nvram_address;
	uint32_t vector;
	uint32_t halted;
	int error;

	/* Refuses images that are not whole words or that RAM cannot hold. */
	if (firmware == NULL || nvram == NULL)
		return EINVAL;
	if (firmware_length < VERIFY_BYTES || (firmware_length % 4U) != 0)
		return EINVAL;
	error = bcm43455_nvram_place(chip->ram_base, chip->ram_size, firmware_length, nvram_length, &nvram_address);
	if (error != 0)
		return error;

	/* Stops the radio and halts the ARM core, so nothing runs from RAM while it changes. */
	error = halt_cores(chip);
	if (error != 0)
		return error;

	/* Writes the firmware at the bottom of RAM and reads both of its ends back. */
	error = bcm43455_chip_write_memory(chip, chip->ram_base, firmware, firmware_length);
	if (error != 0)
		return error;
	error = verify_memory(chip, chip->ram_base, firmware, VERIFY_BYTES);
	if (error != 0)
		return error;
	error = verify_memory(
		chip,
		chip->ram_base + (uint32_t)(firmware_length - VERIFY_BYTES),
		firmware + firmware_length - VERIFY_BYTES,
		VERIFY_BYTES);
	if (error != 0)
		return error;

	/* Writes the NVRAM at the top of RAM, where the firmware looks for it, and reads it back. */
	error = bcm43455_chip_write_memory(chip, nvram_address, nvram, nvram_length);
	if (error != 0)
		return error;
	error = verify_memory(chip, nvram_address, nvram, nvram_length);
	if (error != 0)
		return error;
	kern_logf(
		"bcm43455: firmware %u bytes at %06x, NVRAM %u bytes at %06x\n",
		(unsigned)firmware_length,
		chip->ram_base,
		(unsigned)nvram_length,
		nvram_address);

	/* Clears the SDIO core's interrupt state left from before. */
	error = bcm43455_chip_write32(chip, chip->sdio.base + BCM43455_SDIO_INT_STATUS, 0xffffffffU);
	if (error != 0)
		return error;

	/* Puts the firmware's reset vector, its first word, where the ARM core starts. */
	vector = bcm43455_get_le32(firmware);
	error = bcm43455_chip_write32(chip, 0, vector);
	if (error != 0)
		return error;

	/* Lets the ARM core out of reset without the halt: the firmware starts. */
	halted = BCM43455_CR4_IO_CPU_HALT;
	error = reset_core(chip, &chip->cr4, halted, 0, 0);
	if (error != 0)
		return error;

	/* Succeeded: the firmware runs. */
	return 0;
}

/*
 * Brings the frame function up behind the running firmware.
 */
int
bcm43455_chip_start(
	struct bcm43455_chip *chip)
{
	uint8_t enable;
	int error;

	/* Tells the firmware which protocol version the host speaks. */
	error = bcm43455_chip_write32(
		chip,
		chip->sdio.base + BCM43455_SDIO_TO_CHIP_DATA,
		(uint32_t)BCM43455_SDPCM_PROTOCOL << BCM43455_TO_CHIP_VERSION_SHIFT);
	if (error != 0)
		return error;

	/* Asks for the fast clock, which the firmware brings up once it runs. */
	error = chip->ops->write_byte(chip->context, BCM43455_FUNCTION_BACKPLANE, BCM43455_F1_CHIP_CLOCK, BCM43455_CLOCK_HT_REQUEST);
	if (error != 0)
		return error;
	error = wait_clock(chip, BCM43455_CLOCK_HT_AVAILABLE, HT_WAIT_MS);
	if (error != 0) {
		kern_logf("bcm43455: fast clock not available (%d)\n", error);
		return error;
	}

	/*
	 * Holds the fast clock on.  Without it the chip may drop to its slow
	 * clock between frames, which would need the save-restore handshake
	 * this driver does not do (R8).
	 */
	error = chip->ops->write_byte(
		chip->context,
		BCM43455_FUNCTION_BACKPLANE,
		BCM43455_F1_CHIP_CLOCK,
		(uint8_t)(BCM43455_CLOCK_HT_REQUEST | BCM43455_CLOCK_FORCE_HT));
	if (error != 0)
		return error;

	/* Enables the frame function, which reports ready once the firmware serves it. */
	error = chip->ops->enable_function(chip->context, BCM43455_FUNCTION_FRAMES, FRAMES_READY_MS);
	if (error != 0) {
		kern_logf("bcm43455: frame function not ready (%d)\n", error);
		return error;
	}

	/* Lets the to-host mailbox and the chip's activity raise the SDIO core's interrupt. */
	error = bcm43455_chip_write32(
		chip,
		chip->sdio.base + BCM43455_SDIO_HOST_INT_MASK,
		BCM43455_INT_MAILBOX_MASK | BCM43455_INT_CHIP_ACTIVE);
	if (error != 0)
		return error;

	/* Sets the read FIFO's watermark. */
	error = chip->ops->write_byte(chip->context, BCM43455_FUNCTION_BACKPLANE, BCM43455_F1_WATERMARK, BCM43455_F1_WATERMARK_VALUE);
	if (error != 0)
		return error;

	/* Lets both functions interrupt the host. */
	enable = (uint8_t)(BCM43455_CCCR_INTERRUPT_MASTER | BCM43455_CCCR_INTERRUPT_F1 | BCM43455_CCCR_INTERRUPT_F2);
	error = chip->ops->write_byte(chip->context, BCM43455_FUNCTION_COMMON, BCM43455_CCCR_INTERRUPT_ENABLE, enable);
	if (error != 0)
		return error;

	/* Leaves the window where the frames are moved. */
	error = bcm43455_chip_frame_window(chip);
	if (error != 0)
		return error;

	/* Succeeded: the frame function carries frames. */
	return 0;
}

/*
 * Points the window at the chip common core.
 */
int
bcm43455_chip_frame_window(
	struct bcm43455_chip *chip)
{
	int error;

	/* The frame function's address lies in the chip common core's window. */
	error = set_window(chip, BCM43455_CHIPCOMMON_BASE);
	if (error != 0)
		return error;

	/* Succeeded: frames may move. */
	return 0;
}

/*
 * Points the backplane window at the 32 KB that hold address, writing
 * only when it moves.
 */
static int
set_window(
	struct bcm43455_chip *chip,
	uint32_t address)
{
	uint32_t window;
	int error;

	/* Nothing to do while the window already covers the address. */
	window = address & ~(uint32_t)BCM43455_WINDOW_OFFSET_MASK;
	if (chip->window_valid != 0 && chip->window == window)
		return 0;

	/*
	 * A window half written points nowhere known, so it stays invalid
	 * until all three registers are written.
	 */
	chip->window_valid = 0;

	/* Writes bit 15 of the address, then bits 16 to 23, then 24 to 31. */
	error = chip->ops->write_byte(
		chip->context,
		BCM43455_FUNCTION_BACKPLANE,
		BCM43455_F1_WINDOW_LOW,
		(uint8_t)((window >> 8) & BCM43455_WINDOW_LOW_MASK));
	if (error != 0)
		return error;
	error = chip->ops->write_byte(chip->context, BCM43455_FUNCTION_BACKPLANE, BCM43455_F1_WINDOW_MID, (uint8_t)((window >> 16) & 0xffU));
	if (error != 0)
		return error;
	error = chip->ops->write_byte(chip->context, BCM43455_FUNCTION_BACKPLANE, BCM43455_F1_WINDOW_HIGH, (uint8_t)(window >> 24));
	if (error != 0)
		return error;

	/* Succeeded: the window covers the address. */
	chip->window = window;
	chip->window_valid = 1;
	return 0;
}

/*
 * Moves backplane memory in pieces that stay inside one window and fit
 * the bus: byte mode up to a block, whole blocks beyond.  A write takes
 * its bytes from source through the bounce buffer; a read puts them in
 * data.
 */
static int
transfer_memory(
	struct bcm43455_chip *chip,
	uint32_t address,
	uint8_t *data,
	const uint8_t *source,
	size_t length,
	int write)
{
	size_t done;
	size_t piece;
	size_t room;
	uint32_t offset;
	unsigned block;
	int error;

	/* Refuses a length that is not whole words. */
	if ((length % 4U) != 0)
		return EINVAL;

	/* Moves the memory one piece at a time. */
	block = chip->ops->block_size(chip->context, BCM43455_FUNCTION_BACKPLANE);
	done = 0;
	while (done < length) {
		/* Points the window at the piece's start. */
		error = set_window(chip, address + (uint32_t)done);
		if (error != 0)
			return error;

		/* Takes no more than the window holds past the start, nor than the bounce buffer. */
		offset = (address + (uint32_t)done) & BCM43455_WINDOW_OFFSET_MASK;
		room = (size_t)(BCM43455_WINDOW_SIZE - offset);
		piece = length - done;
		if (piece > room)
			piece = room;
		if (piece > BCM43455_CHIP_TRANSFER_MAX)
			piece = BCM43455_CHIP_TRANSFER_MAX;

		/* A piece longer than a block is cut to whole blocks. */
		if (block != 0 && piece > block)
			piece -= piece % block;

		/* Moves the piece. */
		offset |= BCM43455_WINDOW_ACCESS_32;
		if (write) {
			kern_memcpy(chip->bounce, source + done, piece);
			error = chip->ops->write(chip->context, BCM43455_FUNCTION_BACKPLANE, offset, chip->bounce, piece);
		} else {
			error = chip->ops->read(chip->context, BCM43455_FUNCTION_BACKPLANE, offset, data + done, piece);
		}
		if (error != 0)
			return error;

		/* Moves past the piece. */
		done += piece;
	}

	/* A write's bounce buffer may have held part of the NVRAM; it keeps nothing. */
	if (write)
		kern_memset(chip->bounce, 0, sizeof(chip->bounce));

	/* Succeeded: the memory moved. */
	return 0;
}

/*
 * Asks for the backplane's slow clock, waits for it, and then keeps it on
 * by force; also drops the SDIO lines' internal pull-ups, which the board
 * provides.
 */
static int
start_backplane_clock(
	struct bcm43455_chip *chip)
{
	int error;

	/* Asks for the slow clock without the chip's own clock requests. */
	error = chip->ops->write_byte(
		chip->context,
		BCM43455_FUNCTION_BACKPLANE,
		BCM43455_F1_CHIP_CLOCK,
		(uint8_t)(BCM43455_CLOCK_HW_REQUEST_OFF | BCM43455_CLOCK_ALP_REQUEST));
	if (error != 0)
		return error;

	/* Waits for it. */
	error = wait_clock(chip, BCM43455_CLOCK_ALP_AVAILABLE, ALP_WAIT_MS);
	if (error != 0) {
		kern_logf("bcm43455: backplane clock not available (%d)\n", error);
		return error;
	}

	/* Keeps the slow clock on. */
	error = chip->ops->write_byte(
		chip->context,
		BCM43455_FUNCTION_BACKPLANE,
		BCM43455_F1_CHIP_CLOCK,
		(uint8_t)(BCM43455_CLOCK_HW_REQUEST_OFF | BCM43455_CLOCK_FORCE_ALP));
	if (error != 0)
		return error;

	/* Drops the internal pull-ups. */
	error = chip->ops->write_byte(chip->context, BCM43455_FUNCTION_BACKPLANE, BCM43455_F1_PULL_UP, 0);
	if (error != 0)
		return error;

	/* Succeeded: the backplane runs. */
	return 0;
}

/*
 * Walks the enumeration ROM and keeps the cores the driver needs.
 *
 * A component takes two words; master port descriptors are skipped; each
 * address descriptor may be followed by a high address word and a size
 * word.  A core's base is its first slave port 0 address, its wrapper the
 * first master wrapper (for a core with master ports) or slave wrapper.
 */
static int
scan_erom(
	struct bcm43455_chip *chip,
	uint32_t erom)
{
	struct bcm43455_core core;
	uint32_t word;
	uint32_t extra;
	unsigned words;
	unsigned type;
	unsigned kind;
	unsigned port;
	unsigned size;
	unsigned master_wraps;
	unsigned slave_wraps;
	unsigned have_core;
	int error;

	/* Reads descriptor after descriptor until the end mark. */
	kern_memset(&core, 0, sizeof(core));
	have_core = 0;
	master_wraps = 0;
	slave_wraps = 0;
	for (words = 0; words < BCM43455_EROM_WORDS_MAX; words++) {
		/* Reads the next descriptor. */
		error = bcm43455_chip_read32(chip, erom + words * 4U, &word);
		if (error != 0)
			return error;

		/* The end mark ends the table, and the last core with it. */
		type = word & BCM43455_EROM_TYPE_MASK;
		if (type == BCM43455_EROM_END) {
			if (have_core)
				keep_core(chip, &core);
			break;
		}

		/* A component starts a core: its id here, its revision and port counts in the next word. */
		if (type == BCM43455_EROM_COMPONENT) {
			if (have_core)
				keep_core(chip, &core);
			kern_memset(&core, 0, sizeof(core));
			core.id = (uint16_t)((word >> BCM43455_EROM_PART_SHIFT) & BCM43455_EROM_PART_MASK);
			words++;
			error = bcm43455_chip_read32(chip, erom + words * 4U, &word);
			if (error != 0)
				return error;
			core.revision = (uint8_t)((word >> BCM43455_EROM_REVISION_SHIFT) & BCM43455_EROM_REVISION_MASK);
			master_wraps = (word >> BCM43455_EROM_MASTER_WRAPS_SHIFT) & BCM43455_EROM_COUNT_MASK;
			slave_wraps = (word >> BCM43455_EROM_SLAVE_WRAPS_SHIFT) & BCM43455_EROM_COUNT_MASK;
			have_core = 1;
			continue;
		}

		/* Anything but an address descriptor (a master port) says nothing the driver needs. */
		if ((word & BCM43455_EROM_TYPE_SHORT_MASK) != BCM43455_EROM_ADDRESS)
			continue;

		/* Skips the high address word of a wide address. */
		if ((word & BCM43455_EROM_WIDE) != 0)
			words++;

		/* Skips the size words of a described size, one or two. */
		size = (word >> BCM43455_EROM_SIZE_SHIFT) & BCM43455_EROM_SIZE_MASK;
		if (size == BCM43455_EROM_SIZE_DESCRIBED) {
			words++;
			error = bcm43455_chip_read32(chip, erom + words * 4U, &extra);
			if (error != 0)
				return error;
			if ((extra & BCM43455_EROM_WIDE) != 0)
				words++;
		}

		/* An address outside a core belongs to nothing. */
		if (!have_core)
			continue;

		/* Keeps the core's first slave port 0 address as its registers. */
		kind = (word >> BCM43455_EROM_KIND_SHIFT) & BCM43455_EROM_KIND_MASK;
		port = (word >> BCM43455_EROM_PORT_SHIFT) & BCM43455_EROM_PORT_MASK;
		if (kind == BCM43455_EROM_KIND_SLAVE &&
		    port == 0 &&
		    core.base == 0)
			core.base = word & BCM43455_EROM_BASE_MASK;

		/* Keeps the core's first wrapper of the kind its ports call for. */
		if (core.wrapper == 0) {
			if (kind == BCM43455_EROM_KIND_MASTER_WRAP && master_wraps != 0)
				core.wrapper = word & BCM43455_EROM_BASE_MASK;
			else if (kind == BCM43455_EROM_KIND_SLAVE_WRAP &&
			         master_wraps == 0 &&
			         slave_wraps != 0)
				core.wrapper = word & BCM43455_EROM_BASE_MASK;
		}
	}

	/* Refuses a table that does not describe every core the driver drives. */
	if (chip->sdio.base == 0) {
		kern_logf("bcm43455: no SDIO device core in the enumeration ROM\n");
		return EIO;
	}
	if (chip->cr4.base == 0 || chip->cr4.wrapper == 0) {
		kern_logf("bcm43455: no ARM CR4 core in the enumeration ROM\n");
		return EIO;
	}
	if (chip->d11.wrapper == 0) {
		kern_logf("bcm43455: no D11 core in the enumeration ROM\n");
		return EIO;
	}

	/* Succeeded: the cores are known. */
	return 0;
}

/* Keeps a core the driver needs; the first of each kind wins. */
static void
keep_core(
	struct bcm43455_chip *chip,
	const struct bcm43455_core *core)
{
	struct bcm43455_core *slot;

	/* Finds where the driver keeps a core of this id. */
	slot = NULL;
	if (core->id == BCM43455_CORE_CHIPCOMMON)
		slot = &chip->chipcommon;
	else if (core->id == BCM43455_CORE_SDIO_DEVICE)
		slot = &chip->sdio;
	else if (core->id == BCM43455_CORE_ARM_CR4)
		slot = &chip->cr4;
	else if (core->id == BCM43455_CORE_D11)
		slot = &chip->d11;

	/* Keeps only the first core of a needed kind. */
	if (slot == NULL)
		return;
	if (slot->id != 0)
		return;
	*slot = *core;
}

/*
 * Adds up the ARM CR4's RAM banks.
 */
static int
read_ram_size(
	struct bcm43455_chip *chip)
{
	uint32_t capability;
	uint32_t info;
	uint32_t unit;
	uint32_t total;
	unsigned banks;
	unsigned bank;
	int error;

	/* The capability register counts the banks of both kinds. */
	error = bcm43455_chip_read32(chip, chip->cr4.base + BCM43455_CR4_CAPABILITY, &capability);
	if (error != 0)
		return error;
	banks = capability & BCM43455_CR4_BANKS_A_MASK;
	banks += (capability >> BCM43455_CR4_BANKS_B_SHIFT) & BCM43455_CR4_BANKS_B_MASK;

	/* Selects each bank and adds its size. */
	total = 0;
	for (bank = 0; bank < banks; bank++) {
		/* Selects the bank and reads what it holds. */
		error = bcm43455_chip_write32(chip, chip->cr4.base + BCM43455_CR4_BANK_INDEX, bank);
		if (error != 0)
			return error;
		error = bcm43455_chip_read32(chip, chip->cr4.base + BCM43455_CR4_BANK_INFO, &info);
		if (error != 0)
			return error;

		/* A bank counts blocks of 8 KB, or of 1 KB when it says so. */
		unit = BCM43455_CR4_BANK_UNIT;
		if ((info & BCM43455_CR4_BANK_1K) != 0)
			unit = BCM43455_CR4_BANK_UNIT / 8U;
		total += ((info & BCM43455_CR4_BANK_SIZE_MASK) + 1U) * unit;
	}

	/* Refuses a table without RAM or with more than the chip can have. */
	if (total == 0 || total > RAM_SIZE_MAX) {
		kern_logf("bcm43455: RAM bank table unusable (%u banks, %u bytes)\n", banks, total);
		return EIO;
	}

	/* Succeeded: the RAM's size. */
	chip->ram_size = total;
	return 0;
}

/*
 * Puts a core into reset with the given I/O control before and during it.
 * A core already in reset only gets its in-reset control.
 */
static int
disable_core(
	struct bcm43455_chip *chip,
	const struct bcm43455_core *core,
	uint32_t prereset,
	uint32_t reset)
{
	uint32_t control;
	uint32_t readback;
	int error;

	/* Reads whether the core is in reset already. */
	error = bcm43455_chip_read32(chip, core->wrapper + BCM43455_WRAP_RESET_CONTROL, &control);
	if (error != 0)
		return error;

	/* A running core is given its pre-reset control, then put into reset. */
	if ((control & BCM43455_RESET_CONTROL_RESET) == 0) {
		/* Sets the pre-reset control with the clock forced on, and reads it back to post the write. */
		error = bcm43455_chip_write32(
			chip,
			core->wrapper + BCM43455_WRAP_IO_CONTROL,
			prereset | BCM43455_IO_CONTROL_FORCE_GATE | BCM43455_IO_CONTROL_CLOCK);
		if (error != 0)
			return error;
		error = bcm43455_chip_read32(chip, core->wrapper + BCM43455_WRAP_IO_CONTROL, &readback);
		if (error != 0)
			return error;

		/* Asserts reset and lets it take hold. */
		error = bcm43455_chip_write32(chip, core->wrapper + BCM43455_WRAP_RESET_CONTROL, BCM43455_RESET_CONTROL_RESET);
		if (error != 0)
			return error;
		error = chip->ops->sleep(chip->context, 1);
		if (error != 0)
			return error;
	}

	/* Sets the in-reset control with the clock forced on, and reads it back. */
	error = bcm43455_chip_write32(
		chip,
		core->wrapper + BCM43455_WRAP_IO_CONTROL,
		reset | BCM43455_IO_CONTROL_FORCE_GATE | BCM43455_IO_CONTROL_CLOCK);
	if (error != 0)
		return error;
	error = bcm43455_chip_read32(chip, core->wrapper + BCM43455_WRAP_IO_CONTROL, &readback);
	if (error != 0)
		return error;

	/* Succeeded: the core is in reset. */
	return 0;
}

/*
 * Resets a core from whatever state it is in and lets it run with the
 * post-reset I/O control.
 */
static int
reset_core(
	struct bcm43455_chip *chip,
	const struct bcm43455_core *core,
	uint32_t prereset,
	uint32_t reset,
	uint32_t postreset)
{
	uint32_t control;
	uint32_t readback;
	unsigned tries;
	int error;

	/* Puts the core into reset first, which works from any state. */
	error = disable_core(chip, core, prereset, reset);
	if (error != 0)
		return error;

	/* Lets go of reset until the core reports it released. */
	for (tries = 0;
	     ;
	     tries++) {
		/* Reads whether the reset still holds. */
		error = bcm43455_chip_read32(chip, core->wrapper + BCM43455_WRAP_RESET_CONTROL, &control);
		if (error != 0)
			return error;
		if ((control & BCM43455_RESET_CONTROL_RESET) == 0)
			break;

		/* A core that never comes out of reset is given up on. */
		if (tries == RESET_RELEASE_TRIES) {
			kern_logf("bcm43455: core %03x stays in reset\n", (unsigned)core->id);
			return EIO;
		}

		/* Releases the reset and gives it a moment. */
		error = bcm43455_chip_write32(chip, core->wrapper + BCM43455_WRAP_RESET_CONTROL, 0);
		if (error != 0)
			return error;
		error = chip->ops->sleep(chip->context, 1);
		if (error != 0)
			return error;
	}

	/* Sets the running control with the clock on and the forced gate off, and reads it back. */
	error = bcm43455_chip_write32(chip, core->wrapper + BCM43455_WRAP_IO_CONTROL, postreset | BCM43455_IO_CONTROL_CLOCK);
	if (error != 0)
		return error;
	error = bcm43455_chip_read32(chip, core->wrapper + BCM43455_WRAP_IO_CONTROL, &readback);
	if (error != 0)
		return error;

	/* Succeeded: the core runs. */
	return 0;
}

/*
 * Resets the radio core with its PHY held, and leaves the ARM core out of
 * reset but halted, so the RAM can be written.
 */
static int
halt_cores(
	struct bcm43455_chip *chip)
{
	uint32_t control;
	uint32_t prereset;
	int error;

	/* Resets the radio core and keeps its PHY clocked. */
	error = reset_core(
		chip,
		&chip->d11,
		BCM43455_D11_IO_PHY_RESET | BCM43455_D11_IO_PHY_CLOCK,
		BCM43455_D11_IO_PHY_CLOCK,
		BCM43455_D11_IO_PHY_CLOCK);
	if (error != 0)
		return error;

	/* Resets the ARM core, keeping only its halt bit across, and leaves it halted. */
	error = bcm43455_chip_read32(chip, chip->cr4.wrapper + BCM43455_WRAP_IO_CONTROL, &control);
	if (error != 0)
		return error;
	prereset = control & BCM43455_CR4_IO_CPU_HALT;
	error = reset_core(chip, &chip->cr4, prereset, BCM43455_CR4_IO_CPU_HALT, BCM43455_CR4_IO_CPU_HALT);
	if (error != 0)
		return error;

	/* Succeeded: nothing runs from RAM. */
	return 0;
}

/*
 * Reads memory back, a piece at a time through the bounce buffer, and
 * compares it with what was written.
 */
static int
verify_memory(
	struct bcm43455_chip *chip,
	uint32_t address,
	const uint8_t *expected,
	size_t length)
{
	size_t done;
	size_t piece;
	int same;
	int error;

	/* Compares the range one bounce buffer at a time. */
	done = 0;
	while (done < length) {
		/* Reads the next piece back. */
		piece = length - done;
		if (piece > sizeof(chip->bounce))
			piece = sizeof(chip->bounce);
		error = bcm43455_chip_read_memory(chip, address + (uint32_t)done, chip->bounce, piece);
		if (error != 0) {
			kern_memset(chip->bounce, 0, sizeof(chip->bounce));
			return error;
		}

		/* Compares it, then clears the copy, which may hold NVRAM settings. */
		same = kern_memcmp(chip->bounce, expected + done, piece);
		kern_memset(chip->bounce, 0, sizeof(chip->bounce));
		if (same != 0) {
			kern_logf("bcm43455: RAM at %06x does not read back what was written\n", address + (uint32_t)done);
			return EIO;
		}

		/* Moves past the piece. */
		done += piece;
	}

	/* Succeeded: the memory holds what was written. */
	return 0;
}

/*
 * Waits for a bit of the chip clock register in one-millisecond steps.
 */
static int
wait_clock(
	struct bcm43455_chip *chip,
	uint8_t mask,
	unsigned timeout_ms)
{
	uint8_t clock;
	unsigned waited;
	int error;

	/* Reads the register until the bit is set or the time is spent. */
	for (waited = 0;
	     ;
	     waited++) {
		/* Reads the clock state. */
		error = chip->ops->read_byte(chip->context, BCM43455_FUNCTION_BACKPLANE, BCM43455_F1_CHIP_CLOCK, &clock);
		if (error != 0)
			return error;
		if ((clock & mask) != 0)
			break;

		/* The clock did not come in time. */
		if (waited == timeout_ms)
			return ETIMEDOUT;

		/* Waits a millisecond. */
		error = chip->ops->sleep(chip->context, 1);
		if (error != 0)
			return error;
	}

	/* Succeeded: the clock is there. */
	return 0;
}
