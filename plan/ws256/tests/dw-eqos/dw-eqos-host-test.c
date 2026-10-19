/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the DesignWare Ethernet QoS driver, the clause-22 and
 * YT8531 PHY drivers, and the VisionFive 2's Ethernet glue, on a fake
 * controller with a fake PHY on its MDIO bus.
 *
 * The fake controller keeps its registers in memory.  An MDIO command
 * runs at once against the fake PHY; a write of the transmit tail sends
 * every descriptor the driver handed over up to the tail (unless the test
 * holds the wire); the test puts frames on the receive ring as the
 * controller would.  The service thread runs one pass at a time: its
 * sleep jumps back to the test.
 *
 *   dw-eqos-host-test driver
 *   dw-eqos-host-test glue-v13b vf2-eqos-v13b.dtb
 *   dw-eqos-host-test glue-v12a vf2-eqos-v12a.dtb
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/ethernet/dw-eqos.h>
#include <drivers/ethernet/mii-phy.h>
#include <drivers/ethernet/yt8531.h>
#include <drivers/generic/dma.h>
#include <drivers/generic/fdt.h>
#include <drivers/platform/visionfive2/vf2-crg.h>
#include <drivers/platform/visionfive2/vf2-ethernet.h>
#include <drivers/platform/visionfive2/vf2-pinctrl.h>
#include <drivers/platform/visionfive2/vf2-syscon.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/net/net-device.h>
#include <kern/net/packet-buf.h>
#include <kern/pmem.h>
#include <kern/random.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <uapi/errno.h>

/* The controller's registers the model gives meaning to. */
#define R_MAC_CONFIG	0x0000U
#define R_FILTER	0x0008U
#define R_RX_QUEUES	0x00a0U
#define R_TICKS		0x00dcU
#define R_VERSION	0x0110U
#define R_FEATURE1	0x0120U
#define R_MDIO		0x0200U
#define R_MDIO_DATA	0x0204U
#define R_ADDR_HIGH	0x0300U
#define R_ADDR_LOW	0x0304U
#define R_MMC_RX_MASK	0x070cU
#define R_MTL_TX	0x0d00U
#define R_MTL_RX	0x0d30U
#define R_DMA_MODE	0x1000U
#define R_DMA_BUS	0x1004U
#define R_CH_CONTROL	0x1100U
#define R_CH_TX		0x1104U
#define R_CH_RX		0x1108U
#define R_TX_BASE_HI	0x1110U
#define R_TX_BASE	0x1114U
#define R_RX_BASE_HI	0x1118U
#define R_RX_BASE	0x111cU
#define R_TX_TAIL	0x1120U
#define R_RX_TAIL	0x1128U
#define R_TX_LEN	0x112cU
#define R_RX_LEN	0x1130U
#define R_CH_IRQ	0x1134U
#define R_CH_STATUS	0x1160U

#define OWN		0x80000000U
#define TI		0x00000001U
#define RI		0x00000040U
#define RBU		0x00000080U
#define IRQ_RUNNING	0x0000d0c1U
#define IRQ_DRAINING	0x00005000U

/* One frame the fake controller sent. */
struct sent_frame {
	uint32_t length;
	uint32_t word2;
	uint32_t word3;
	uint8_t data[2048];
};

/* The fake PHY. */
struct fake_phy {
	unsigned address;
	int present;
	uint16_t regs[32];
	uint16_t number;
	uint16_t chip_config;
	uint16_t rgmii_config;
	uint16_t pad_drive;
	unsigned resets;
};

/* The fake controller. */
struct model {
	uint8_t registers[0x10000];
	unsigned dma_resets;
	int reset_stuck;
	unsigned tx_current;
	unsigned rx_current;
	int tx_hold;
	int tx_error_next;
	struct sent_frame sent[600];
	unsigned sent_count;
	unsigned mdio_commands;
	uint32_t last_mdio;
	struct fake_phy phy;
};

static struct model gmac;

/* The SoC's other register blocks, for the glue. */
static uint8_t sys_crg[0x10000];
static uint8_t stg_crg[0x10000];
static uint8_t aon_crg[0x10000];
static uint8_t sys_syscon[0x1000];
static uint8_t stg_syscon[0x1000];
static uint8_t aon_syscon[0x1000];
static uint8_t sys_pinctrl[0x10000];
static uint8_t aon_pinctrl[0x10000];

struct fake_block {
	uint64_t address;
	size_t size;
	uint8_t *memory;
};

static struct fake_block blocks[] = {
	{ 0x13020000ULL, sizeof(sys_crg), sys_crg },
	{ 0x10230000ULL, sizeof(stg_crg), stg_crg },
	{ 0x17000000ULL, sizeof(aon_crg), aon_crg },
	{ 0x13030000ULL, sizeof(sys_syscon), sys_syscon },
	{ 0x10240000ULL, sizeof(stg_syscon), stg_syscon },
	{ 0x17010000ULL, sizeof(aon_syscon), aon_syscon },
	{ 0x13040000ULL, sizeof(sys_pinctrl), sys_pinctrl },
	{ 0x17020000ULL, sizeof(aon_pinctrl), aon_pinctrl },
	{ 0x16030000ULL, sizeof(gmac.registers), gmac.registers }
};

/* Each generator's reset assert and status offsets, for the fake status. */
struct fake_reset {
	uint8_t *memory;
	uint32_t assert_offset;
	uint32_t status_offset;
	uint32_t registers;
};

static const struct fake_reset resets[] = {
	{ sys_crg, 0x2f8U, 0x308U, 4U },
	{ stg_crg, 0x74U, 0x78U, 1U },
	{ aon_crg, 0x38U, 0x3cU, 1U }
};

/* What the kernel's services saw. */
static uint64_t fake_now;
static uint64_t fake_ticks;
static kern_irq_handler_t irq_handler;
static void *irq_argument;
static int irq_number = -1;
static int irq_masked = 1;
static unsigned eoi_count;
static void (*thread_entry)(void *);
static void *thread_argument;
static jmp_buf service_jump;
static struct net_device *published;
static int carrier = -1;
static unsigned poll_requests;
static unsigned tx_errors;
static struct drv_dma_constraints dma_constraints;
static unsigned received_count;
static uint32_t received_length[600];
static uint8_t received_first[600];
static int random_calls;
static unsigned set_speed_calls;
static unsigned set_speed_last;

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

/* The fake PHY's registers as the MDIO bus sees them. */
static uint16_t
phy_read(
	unsigned reg)
{
	struct fake_phy *phy;

	phy = &gmac.phy;
	if (reg == 0x1eU)
		return phy->number;
	if (reg == 0x1fU) {
		if (phy->number == 0xa001U)
			return phy->chip_config;
		if (phy->number == 0xa003U)
			return phy->rgmii_config;
		if (phy->number == 0xa010U)
			return phy->pad_drive;
		return 0;
	}
	return phy->regs[reg];
}

static void
phy_write(
	unsigned reg,
	uint16_t value)
{
	struct fake_phy *phy;

	phy = &gmac.phy;
	if (reg == 0) {
		if ((value & 0x8000U) != 0)
			phy->resets++;
		phy->regs[0] = value & 0x7fffU;
		return;
	}
	if (reg == 0x1eU) {
		phy->number = value;
		return;
	}
	if (reg == 0x1fU) {
		if (phy->number == 0xa001U)
			phy->chip_config = value;
		else if (phy->number == 0xa003U)
			phy->rgmii_config = value;
		else if (phy->number == 0xa010U)
			phy->pad_drive = value;
		return;
	}
	if (reg == 1U || reg == 2U || reg == 3U || reg == 5U || reg == 10U || reg == 15U)
		return;
	phy->regs[reg] = value;
}

static struct dw_descriptor {
	uint32_t word0;
	uint32_t word1;
	uint32_t word2;
	uint32_t word3;
} *ring_at(
	uint32_t base_high,
	uint32_t base_low,
	unsigned index)
{
	uintptr_t base;

	base = ((uintptr_t)get32(gmac.registers, base_high) << 32) | get32(gmac.registers, base_low);
	return (struct dw_descriptor *)(base + index * 16U);
}

static unsigned
tail_index(
	uint32_t tail_register,
	uint32_t base_low)
{
	return (get32(gmac.registers, tail_register) - get32(gmac.registers, base_low)) / 16U;
}

/* Sends every descriptor the driver handed over, up to the tail. */
static void
model_transmit(
	void)
{
	struct dw_descriptor *descriptor;
	struct sent_frame *frame;
	unsigned count;
	unsigned tail;
	uintptr_t buffer;

	count = get32(gmac.registers, R_TX_LEN) + 1U;
	tail = tail_index(R_TX_TAIL, R_TX_BASE);
	while (gmac.tx_current != tail) {
		descriptor = ring_at(R_TX_BASE_HI, R_TX_BASE, gmac.tx_current);
		if ((descriptor->word3 & OWN) == 0)
			break;
		frame = &gmac.sent[gmac.sent_count % 600U];
		frame->length = descriptor->word3 & 0x7fffU;
		frame->word2 = descriptor->word2;
		frame->word3 = descriptor->word3;
		buffer = ((uintptr_t)descriptor->word1 << 32) | descriptor->word0;
		memcpy(frame->data, (const void *)buffer, descriptor->word2 & 0x3fffU);
		gmac.sent_count++;
		descriptor->word3 = 0x30000000U;
		if (gmac.tx_error_next) {
			descriptor->word3 |= 0x8000U;
			gmac.tx_error_next = 0;
		}
		gmac.tx_current = (gmac.tx_current + 1U) % count;
		put32(gmac.registers, R_CH_STATUS, get32(gmac.registers, R_CH_STATUS) | TI | 0x8000U);
	}
}

/* Puts one frame (with four checksum bytes) on the receive ring; 0 when no descriptor is free. */
static int
model_receive(
	const uint8_t *data,
	uint32_t length,
	int error)
{
	struct dw_descriptor *descriptor;
	unsigned count;
	uintptr_t buffer;

	count = get32(gmac.registers, R_RX_LEN) + 1U;
	descriptor = ring_at(R_RX_BASE_HI, R_RX_BASE, gmac.rx_current);
	if (gmac.rx_current == tail_index(R_RX_TAIL, R_RX_BASE) || (descriptor->word3 & OWN) == 0) {
		put32(gmac.registers, R_CH_STATUS, get32(gmac.registers, R_CH_STATUS) | RBU | 0x4000U);
		return 0;
	}
	if ((descriptor->word3 & 0x01000000U) == 0)
		return 0;
	buffer = ((uintptr_t)descriptor->word1 << 32) | descriptor->word0;
	memcpy((void *)buffer, data, length);
	memset((uint8_t *)buffer + length, 0xcc, 4);
	descriptor->word3 = 0x30000000U | (length + 4U);
	if (error)
		descriptor->word3 |= 0x8000U;
	gmac.rx_current = (gmac.rx_current + 1U) % count;
	put32(gmac.registers, R_CH_STATUS, get32(gmac.registers, R_CH_STATUS) | RI | 0x8000U);
	return 1;
}

static uint32_t
controller_read(
	uint32_t offset)
{
	return get32(gmac.registers, offset);
}

static void
controller_write(
	uint32_t offset,
	uint32_t value)
{
	unsigned phy;
	unsigned reg;

	if (offset == R_DMA_MODE) {
		if ((value & 1U) != 0) {
			gmac.dma_resets++;
			if (!gmac.reset_stuck)
				value &= ~1U;
		}
		put32(gmac.registers, offset, value);
		return;
	}
	if (offset == R_CH_STATUS) {
		put32(gmac.registers, offset, get32(gmac.registers, offset) & ~value);
		return;
	}
	if (offset == R_MTL_TX) {
		put32(gmac.registers, offset, value & ~1U);
		return;
	}
	if (offset == R_MDIO) {
		put32(gmac.registers, offset, value & ~1U);
		if ((value & 1U) == 0)
			return;
		gmac.mdio_commands++;
		gmac.last_mdio = value;
		phy = (value >> 21) & 31U;
		reg = (value >> 16) & 31U;
		if (((value >> 2) & 3U) == 3U) {
			if (!gmac.phy.present || phy != gmac.phy.address)
				put32(gmac.registers, R_MDIO_DATA, 0xffffU);
			else
				put32(gmac.registers, R_MDIO_DATA, phy_read(reg));
		} else if (((value >> 2) & 3U) == 1U) {
			if (gmac.phy.present && phy == gmac.phy.address)
				phy_write(reg, (uint16_t)get32(gmac.registers, R_MDIO_DATA));
		}
		return;
	}
	put32(gmac.registers, offset, value);
	if (offset == R_TX_TAIL && !gmac.tx_hold)
		model_transmit();
}

/* The kernel's services, as the host provides them. */

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	const uint8_t *target;

	target = (const uint8_t *)address;
	if (target >= gmac.registers && target < gmac.registers + sizeof(gmac.registers))
		return controller_read((uint32_t)(target - gmac.registers));
	return get32(target, 0);
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint8_t *target;
	unsigned index;
	uint32_t offset;

	target = (uint8_t *)address;
	if (target >= gmac.registers && target < gmac.registers + sizeof(gmac.registers)) {
		controller_write((uint32_t)(target - gmac.registers), value);
		return;
	}
	put32(target, 0, value);

	/* A reset assert register's status follows it: released is set. */
	for (index = 0; index < sizeof(resets) / sizeof(resets[0]); index++) {
		if (target < resets[index].memory + resets[index].assert_offset)
			continue;
		offset = (uint32_t)(target - resets[index].memory) - resets[index].assert_offset;
		if (offset >= resets[index].registers * 4U)
			continue;
		put32(resets[index].memory, resets[index].status_offset + offset, ~value);
	}
}

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

bool
kern_rtc_read_counter(
	uint64_t *counter,
	uint64_t *frequency_hz)
{
	fake_now += 1000U;
	*counter = fake_now;
	*frequency_hz = 1000000U;
	return true;
}

void kern_io_write_barrier(void) { }
void kern_io_read_barrier(void) { }
void spin_init(struct spinlock *lock, enum lock_rank rank, const char *name) { (void)lock; (void)rank; (void)name; }
unsigned long spin_lock_irqsave(struct spinlock *lock) { (void)lock; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled) { (void)lock; (void)enabled; }
void kern_logf(const char *format, ...) { va_list ap; va_start(ap, format); printf("  log: "); vprintf(format, ap); va_end(ap); }

int
kern_irq_register(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	irq_number = irq;
	irq_handler = handler;
	irq_argument = argument;
	return 0;
}

int kern_irq_unregister(int irq, kern_irq_handler_t handler, void *argument) { (void)irq; (void)handler; (void)argument; irq_handler = NULL; return 0; }
int kern_irq_set_mode(int irq, unsigned trigger, unsigned polarity) { (void)irq; return trigger == KERN_IRQ_TRIGGER_LEVEL && polarity == KERN_IRQ_POLARITY_HIGH ? 0 : EINVAL; }
void kern_irq_mask(int irq) { (void)irq; irq_masked = 1; }
void kern_irq_unmask(int irq) { (void)irq; irq_masked = 0; }
void kern_irq_send_eoi(kern_irq_ack_t acknowledge) { (void)acknowledge; eoi_count++; }

int
drv_dma_device_create(
	const struct drv_dma_constraints *constraints,
	struct drv_dma_device **result)
{
	dma_constraints = *constraints;
	*result = (struct drv_dma_device *)&dma_constraints;
	return 0;
}

int drv_dma_device_destroy(struct drv_dma_device *device) { (void)device; return 0; }

int
drv_dma_alloc_coherent(
	struct drv_dma_device *device,
	size_t size,
	size_t alignment,
	struct drv_dma_buffer *buffer)
{
	void *memory;

	(void)device;
	memory = aligned_alloc(alignment, (size + alignment - 1U) / alignment * alignment);
	if (memory == NULL)
		return ENOMEM;
	memset(memory, 0x5a, size);
	buffer->address = memory;
	buffer->device_address = (uintptr_t)memory;
	buffer->size = size;
	return 0;
}

void drv_dma_free_coherent(struct drv_dma_device *device, struct drv_dma_buffer *buffer) { (void)device; free(buffer->address); buffer->address = NULL; }

struct net_device *net_device_alloc(void) { return calloc(1, sizeof(struct net_device)); }
int net_device_create(struct net_device *device) { published = device; return 0; }
void net_device_destroy(struct net_device *device) { free(device); }
void net_device_ref(struct net_device *device) { (void)device; }
int net_device_set_carrier(struct net_device *device, int value) { (void)device; carrier = value; return 0; }
void net_device_schedule_poll(struct net_device *device) { (void)device; poll_requests++; }
void net_device_tx_error(struct net_device *device) { (void)device; tx_errors++; }

void
net_device_receive(
	struct net_device *device,
	struct packet_buf *packet)
{
	(void)device;
	if (received_count < 600U) {
		received_length[received_count] = (uint32_t)packet->length;
		received_first[received_count] = packet->data[0];
	}
	received_count++;
	free(packet->storage);
	free(packet);
}

struct packet_buf *
packet_buf_alloc(
	size_t headroom)
{
	struct packet_buf *packet;

	packet = calloc(1, sizeof(*packet));
	packet->storage = calloc(1, PACKET_BUF_STORAGE_SIZE);
	packet->capacity = PACKET_BUF_STORAGE_SIZE;
	packet->data = packet->storage + headroom;
	return packet;
}

void
packet_buf_free(
	struct packet_buf *packet)
{
	if (packet == NULL)
		return;
	free(packet->storage);
	free(packet);
}

void *
packet_buf_append(
	struct packet_buf *packet,
	size_t length)
{
	uint8_t *tail;

	tail = packet->data + packet->length;
	if (tail + length > packet->storage + packet->capacity)
		return NULL;
	packet->length += length;
	return tail;
}

int
kthread_create(
	void (*entry)(void *),
	void *argument,
	int priority,
	struct thread **result)
{
	static struct thread *fake;

	(void)priority;
	thread_entry = entry;
	thread_argument = argument;
	*result = (struct thread *)&fake;
	return 0;
}

void thread_start(struct thread *thread) { (void)thread; }
uint64_t sched_ticks(void) { fake_ticks += 1000U; return fake_ticks; }
void sched_sleep(uint64_t timeout_tick) { (void)timeout_tick; longjmp(service_jump, 1); }

int
kern_random_read(
	void *buffer,
	size_t length,
	unsigned flags)
{
	(void)flags;
	random_calls++;
	memset(buffer, 0x11, length);
	return 0;
}

/* The fake PHY's bus without the controller, for the generic PHY code. */
static int direct_read(void *context, unsigned phy, unsigned reg, uint16_t *data) { (void)context; (void)phy; *data = phy_read(reg); return 0; }
static int direct_write(void *context, unsigned phy, unsigned reg, uint16_t data) { (void)context; (void)phy; phy_write(reg, data); return 0; }

/* Runs one pass of the service thread: a link check and a worker wake. */
static void
run_service(
	void)
{
	if (setjmp(service_jump) == 0)
		thread_entry(thread_argument);
}

/* Takes the interrupt as the PLIC would deliver it. */
static void
take_interrupt(
	void)
{
	irq_handler(irq_number, (kern_irq_ack_t)1, irq_argument);
}

/* Sets the fake PHY's link as autonegotiation would leave it. */
static void
set_link(
	unsigned speed,
	int full)
{
	struct fake_phy *phy;

	phy = &gmac.phy;
	phy->regs[1] = 0x7949U;
	phy->regs[5] = 0;
	phy->regs[10] = 0;
	if (speed == 0)
		return;
	phy->regs[1] |= 0x0024U;
	if (speed == 1000U)
		phy->regs[10] = full ? 0x0800U : 0x0400U;
	else if (speed == 100U)
		phy->regs[5] = full ? 0x0100U : 0x0080U;
	else
		phy->regs[5] = full ? 0x0040U : 0x0020U;
}

/* Resets the fake controller and PHY to what the board leaves behind. */
static void
reset_model(
	void)
{
	memset(&gmac, 0, sizeof(gmac));
	put32(gmac.registers, R_VERSION, 0x00005052U);
	put32(gmac.registers, R_FEATURE1, (1U << 14) | (4U << 6) | 4U);
	put32(gmac.registers, R_ADDR_HIGH, 0x8000ffffU);
	put32(gmac.registers, R_ADDR_LOW, 0xffffffffU);
	gmac.phy.address = 0;
	gmac.phy.present = 1;
	gmac.phy.regs[2] = 0x4f51U;
	gmac.phy.regs[3] = 0xe91bU;
	gmac.phy.regs[15] = 0x3000U;
	gmac.phy.chip_config = 0x0020U;
	gmac.phy.rgmii_config = 0x0400U | (13U << 10) | 0x0010U | 1U;
	gmac.phy.pad_drive = 0xffffU;
	set_link(0, 0);
}

static int
board_set_speed(
	void *context,
	unsigned speed_mbps)
{
	(void)context;
	set_speed_calls++;
	set_speed_last = speed_mbps;
	return 0;
}

/* A board description in the shape of the VisionFive 2 v1.3B's. */
static void
fill_config(
	struct drv_dw_eqos_config *config)
{
	memset(config, 0, sizeof(*config));
	config->name = "en0";
	config->registers = gmac.registers;
	config->irq = 7;
	config->dma_coherent = 1;
	config->csr_clock_hz = 198000000ULL;
	config->phy_address = 0;
	config->yt8531.rx_delay = 1;
	config->yt8531.tx_delay = 1;
	config->yt8531.rx_delay_ps = 1500;
	config->yt8531.tx_delay_ps = 1500;
	config->yt8531.rx_clock_drive_ua = 3970;
	config->yt8531.rx_data_drive_ua = 2910;
	config->yt8531.tx_clock_adjust = 1;
	config->yt8531.tx_clock_inverted_100 = 1;
	config->yt8531.tx_clock_inverted_1000 = 1;
	config->tx_fifo_bytes = 2048;
	config->rx_fifo_bytes = 2048;
	config->tx_burst = 16;
	config->rx_burst = 16;
	config->burst_x8 = 0;
	config->fixed_burst = 1;
	config->threshold_mode = 1;
	config->axi_present = 1;
	config->axi_lpi = 1;
	config->axi_write_limit = 15;
	config->axi_read_limit = 15;
	config->axi_burst_lengths[0] = 256;
	config->axi_burst_lengths[1] = 128;
	config->axi_burst_lengths[2] = 64;
	config->axi_burst_lengths[3] = 32;
	config->has_mac = 1;
	config->mac[0] = 0x6c;
	config->mac[1] = 0xcf;
	config->mac[2] = 0x39;
	config->mac[3] = 0x00;
	config->mac[4] = 0x12;
	config->mac[5] = 0x34;
	config->set_speed = board_set_speed;
}

static struct packet_buf *
make_frame(
	uint32_t length,
	uint8_t first)
{
	struct packet_buf *packet;
	uint32_t index;

	packet = packet_buf_alloc(64);
	packet_buf_append(packet, length);
	for (index = 0; index < length; index++)
		packet->data[index] = (uint8_t)(first + index);
	return packet;
}

/* The driver on its own: attach, the PHY, open, link, transmit, receive, close. */
static void
check_driver(
	void)
{
	struct drv_dw_eqos_config config;
	struct drv_mii_link link;
	struct drv_mii_bus bus;
	struct packet_buf *packet;
	struct dw_descriptor *descriptor;
	uint8_t frame[1600];
	unsigned owned;
	unsigned index;
	unsigned count;
	uint32_t value;
	int error;

	/* Attach. */
	reset_model();
	fill_config(&config);
	error = drv_dw_eqos_attach(&config);
	check(error == 0 && published != NULL, "attach publishes the interface");
	if (error != 0)
		exit(1);
	check(strcmp(published->name, "en0") == 0 && published->hwaddr_len == 6U &&
	      memcmp(published->hwaddr, config.mac, 6) == 0 && published->mtu == 1500U,
	      "interface en0 with the board's MAC and MTU 1500");
	check(gmac.dma_resets == 1U, "one software reset");
	value = get32(gmac.registers, R_DMA_BUS);
	check(value == (0x80000000U | (15U << 24) | (15U << 16) | 0x800U | 0xf0U | 1U),
	      "bus mode: LPI, OSR 15/15, 40-bit addresses, bursts 256-32, fixed burst");
	check(dma_constraints.address_bits == 40U && dma_constraints.coherent == 1,
	      "DMA device: 40 address bits, coherent");
	check((get32(gmac.registers, R_CH_CONTROL) & 0x10000U) == 0, "no-pbl-x8: burst x8 off");
	check(((get32(gmac.registers, R_CH_TX) >> 16) & 0x3fU) == 16U && (get32(gmac.registers, R_CH_TX) & 0x10U) != 0,
	      "transmit burst 16, second frame");
	check(((get32(gmac.registers, R_CH_RX) >> 16) & 0x3fU) == 16U && ((get32(gmac.registers, R_CH_RX) >> 1) & 0x3fffU) == 2048U,
	      "receive burst 16, buffer 2048");
	check(get32(gmac.registers, R_MMC_RX_MASK) == 0xffffffffU, "statistics interrupts masked");
	check(irq_number == 7 && irq_handler != NULL && irq_masked, "interrupt 7 registered, masked");
	check(thread_entry != NULL, "service thread created");

	/* The PHY. */
	check(gmac.phy.resets == 1U, "PHY reset once");
	check(((gmac.last_mdio >> 8) & 7U) == 4U, "MDIO clock range 150-250 MHz for 198 MHz");
	check((gmac.phy.chip_config & 0x0100U) == 0 && ((gmac.phy.rgmii_config >> 10) & 0xfU) == 10U &&
	      (gmac.phy.rgmii_config & 0xfU) == 10U && (gmac.phy.rgmii_config & 0xf0U) == 0x10U,
	      "YT8531: 1500 ps both ways as step 10, no fixed delay, 100 Mbps delay kept");
	check(((gmac.phy.pad_drive >> 13) & 7U) == 6U && ((gmac.phy.pad_drive >> 12) & 1U) == 0 &&
	      ((gmac.phy.pad_drive >> 4) & 3U) == 3U && (gmac.phy.pad_drive & 0x0fcfU) == 0x0fcfU,
	      "YT8531 at 1.8 V: clock drive 3970 uA = 6, data 2910 uA = 3, other bits kept");
	check(gmac.phy.regs[4] == 0x01e1U && gmac.phy.regs[9] == 0x0200U && gmac.phy.regs[0] == 0x1200U,
	      "advertises 10/100 both duplexes and gigabit full, restarts autonegotiation");
	check(gmac.phy.number == 0, "extended register number put back");

	/* Link: down at first. */
	set_link(0, 0);
	run_service();
	check(carrier == -1 || carrier == 0, "no link: no carrier");

	/* Open. */
	error = published->ops->open(published);
	check(error == 0, "open");
	check(get32(gmac.registers, R_RX_LEN) == 127U && get32(gmac.registers, R_TX_LEN) == 127U, "ring lengths 128");
	check(tail_index(R_RX_TAIL, R_RX_BASE) == 127U && tail_index(R_TX_TAIL, R_TX_BASE) == 0, "receive tail at 127, transmit tail at 0");
	owned = 0;
	for (index = 0; index < 128U; index++) {
		descriptor = ring_at(R_RX_BASE_HI, R_RX_BASE, index);
		if ((descriptor->word3 & OWN) != 0 && (descriptor->word3 & 0x41000000U) == 0x41000000U)
			owned++;
	}
	check(owned == 127U, "127 receive descriptors handed over with IOC and buffer 1 valid");
	check(get32(gmac.registers, R_RX_QUEUES) == 2U, "receive queue 0 enabled");
	check(get32(gmac.registers, R_MTL_RX) == ((7U << 20) | 0x18U), "receive queue 2 KiB, threshold 128");
	check(get32(gmac.registers, R_MTL_TX) == ((7U << 16) | 0x70U | 0x8U), "transmit queue 2 KiB, threshold 512, enabled");
	check(get32(gmac.registers, R_FILTER) == 0x10U, "all multicast passes");
	check(get32(gmac.registers, R_ADDR_HIGH) == 0x3412U && get32(gmac.registers, R_ADDR_LOW) == 0x0039cf6cU, "station address");
	check(get32(gmac.registers, R_TICKS) == 197U, "1 us tick: 198 - 1");
	check((get32(gmac.registers, R_CH_RX) & 1U) != 0 && (get32(gmac.registers, R_CH_TX) & 1U) != 0, "DMA started");
	check(get32(gmac.registers, R_CH_IRQ) == IRQ_RUNNING && !irq_masked, "channel interrupts on, PLIC unmasked");
	check((get32(gmac.registers, R_MAC_CONFIG) & 3U) == 3U, "MAC transmitter and receiver on");

	/* No link: transmit refused. */
	packet = make_frame(60, 1);
	error = published->ops->transmit(published, packet);
	check(error == ENETDOWN, "no link: ENETDOWN");

	/* Link at 1000 full. */
	set_link(1000, 1);
	poll_requests = 0;
	run_service();
	value = get32(gmac.registers, R_MAC_CONFIG);
	check(carrier == 1 && (value & 0xc000U) == 0 && (value & 0x2000U) != 0 && (value & 3U) == 3U,
	      "1000 full: carrier, MAC gigabit full duplex");
	check((gmac.phy.rgmii_config & 0x4000U) != 0, "1000: PHY transmit clock inverted");
	check(set_speed_calls == 1U && set_speed_last == 1000U, "board told 1000");
	check(poll_requests == 1U && published->link_mbps == 1000U, "worker woken, link_mbps 1000");

	/* Same link again: nothing changes. */
	run_service();
	check(set_speed_calls == 1U, "same link: no change");

	/* 100 full, 10 half. */
	set_link(100, 1);
	run_service();
	value = get32(gmac.registers, R_MAC_CONFIG);
	check((value & 0xe000U) == 0xe000U && (gmac.phy.rgmii_config & 0x4000U) != 0 && set_speed_last == 100U,
	      "100 full: MII fast full duplex, inverted");
	set_link(10, 0);
	run_service();
	value = get32(gmac.registers, R_MAC_CONFIG);
	check((value & 0xe000U) == 0x8000U && (gmac.phy.rgmii_config & 0x4000U) == 0 && set_speed_last == 10U,
	      "10 half: MII half duplex, not inverted");

	/* The generic resolution on the fake PHY directly: gigabit half only when both ends say so. */
	bus.context = NULL;
	bus.read = direct_read;
	bus.write = direct_write;
	set_link(1000, 0);
	gmac.phy.regs[9] = 0x0300U;
	error = drv_mii_phy_read_link(&bus, 0, &link);
	check(error == 0 && link.speed_mbps == 1000U && !link.full_duplex, "1000 half when both advertise it");
	gmac.phy.regs[9] = 0x0200U;
	error = drv_mii_phy_read_link(&bus, 0, &link);
	check(error == 0 && link.speed_mbps == 0, "partner 1000 half, we full only: no common mode");
	set_link(100, 1);
	gmac.phy.regs[4] = 0x0061U;
	error = drv_mii_phy_read_link(&bus, 0, &link);
	check(error == 0 && link.speed_mbps == 0, "partner 100 full, we advertise 10 only: no common mode");
	gmac.phy.regs[4] = 0x01e1U;
	gmac.phy.regs[1] &= (uint16_t)~0x0020U;
	error = drv_mii_phy_read_link(&bus, 0, &link);
	check(error == 0 && link.speed_mbps == 0, "link without autonegotiation done: down");
	gmac.phy.regs[2] = 0xffffU;
	gmac.phy.regs[3] = 0xffffU;
	error = drv_mii_phy_read_identity(&bus, 0, &value);
	check(error == ENXIO, "all-ones identity: no PHY");
	gmac.phy.regs[2] = 0x4f51U;
	gmac.phy.regs[3] = 0xe91bU;

	/* Back to 1000 full. */
	set_link(1000, 1);
	run_service();

	/* Transmit a short frame. */
	gmac.sent_count = 0;
	packet = make_frame(42, 0x40);
	error = published->ops->transmit(published, packet);
	check(error == 0 && gmac.sent_count == 1U, "short frame sent");
	check(gmac.sent[0].length == 60U && (gmac.sent[0].word2 & 0x80003fffU) == (0x80000000U | 60U) &&
	      (gmac.sent[0].word3 & 0xb0000000U) == 0xb0000000U,
	      "padded to 60, IOC, first and last, owned");
	check(gmac.sent[0].data[0] == 0x40 && gmac.sent[0].data[41] == (uint8_t)(0x40 + 41) &&
	      gmac.sent[0].data[42] == 0 && gmac.sent[0].data[59] == 0,
	      "frame bytes then zero padding");
	packet = make_frame(1519, 0);
	error = published->ops->transmit(published, packet);
	check(error == EMSGSIZE, "1519 bytes: EMSGSIZE");
	packet = make_frame(1518, 0);
	error = published->ops->transmit(published, packet);
	check(error == 0 && gmac.sent[1].length == 1518U, "1518 bytes sent");

	/* Ring full while the wire is held, then freed. */
	gmac.tx_hold = 1;
	count = 0;
	for (index = 0; index < 130U; index++) {
		packet = make_frame(100, (uint8_t)index);
		error = published->ops->transmit(published, packet);
		if (error == 0)
			count++;
		else
			break;
	}
	check(count == 127U && error == ENOBUFS, "127 frames in flight, then ENOBUFS");
	gmac.tx_hold = 0;
	gmac.tx_error_next = 1;
	model_transmit();
	tx_errors = 0;
	(void)published->ops->poll_receive(published, 16);
	check(tx_errors == 1U, "a frame the MAC failed is counted once");
	packet = make_frame(100, 0x77);
	error = published->ops->transmit(published, packet);
	check(error == 0 && gmac.sent[(gmac.sent_count - 1U) % 600U].data[0] == 0x77, "ring reclaimed, sending again");

	/* Receive through the interrupt and the worker. */
	received_count = 0;
	for (index = 0; index < 1600U; index++)
		frame[index] = (uint8_t)index;
	frame[0] = 0xa0;
	model_receive(frame, 64, 0);
	frame[0] = 0xa1;
	model_receive(frame, 1514, 0);
	frame[0] = 0xa2;
	model_receive(frame, 200, 1);
	eoi_count = 0;
	poll_requests = 0;
	take_interrupt();
	check(eoi_count == 1U && poll_requests == 1U, "interrupt: EOI and worker woken");
	check(get32(gmac.registers, R_CH_IRQ) == IRQ_DRAINING && (get32(gmac.registers, R_CH_STATUS) & (RI | TI)) == 0,
	      "interrupt: status cleared, frame interrupts held off");
	count = published->ops->poll_receive(published, 16);
	check(count == 3U && received_count == 2U && received_length[0] == 64U && received_length[1] == 1514U &&
	      received_first[0] == 0xa0 && received_first[1] == 0xa1,
	      "worker: two frames without their checksum, the bad one dropped");
	check(published->rx_errors == 1U && published->rx_dropped == 1U, "bad frame counted as error and drop");
	check(get32(gmac.registers, R_CH_IRQ) == IRQ_RUNNING, "worker: interrupts back on");
	check(tail_index(R_RX_TAIL, R_RX_BASE) == 2U, "receive tail moved past the three given back (127 + 3 mod 128)");

	/* Many frames, wrapping the ring several times, with a budget. */
	received_count = 0;
	for (index = 0; index < 400U; index++) {
		frame[0] = (uint8_t)index;
		if (!model_receive(frame, 60, 0)) {
			(void)published->ops->poll_receive(published, 8);
			model_receive(frame, 60, 0);
		}
	}
	while (published->ops->poll_receive(published, 32) != 0)
		;
	value = 1;
	for (index = 0; index < 400U; index++) {
		if (received_first[index] != (uint8_t)index)
			value = 0;
	}
	check(received_count == 400U && value, "400 frames in order across ring wraps");

	/* A full ring: the controller reports no buffer, the worker refills. */
	received_count = 0;
	for (index = 0; index < 127U; index++)
		model_receive(frame, 60, 0);
	error = model_receive(frame, 60, 0);
	check(error == 0 && (get32(gmac.registers, R_CH_STATUS) & RBU) != 0, "128th frame: receive buffer unavailable");
	take_interrupt();
	while (published->ops->poll_receive(published, 64) != 0)
		;
	error = model_receive(frame, 60, 0);
	check(received_count == 127U && error == 1, "refilled: 127 delivered and the next lands");
	(void)published->ops->poll_receive(published, 64);

	/* Close. */
	published->ops->close(published);
	check(irq_masked && get32(gmac.registers, R_CH_IRQ) == 0 && (get32(gmac.registers, R_MAC_CONFIG) & 3U) == 0 &&
	      (get32(gmac.registers, R_CH_RX) & 1U) == 0 && (get32(gmac.registers, R_CH_TX) & 1U) == 0 && carrier == 0,
	      "close: interrupts off, MAC and DMA stopped, no carrier");
	packet = make_frame(60, 0);
	error = published->ops->transmit(published, packet);
	check(error == ENETDOWN, "closed: ENETDOWN");

	/* Reopen: fresh rings. */
	gmac.tx_current = 0;
	gmac.rx_current = 0;
	error = published->ops->open(published);
	check(error == 0 && tail_index(R_RX_TAIL, R_RX_BASE) == 127U && tail_index(R_TX_TAIL, R_TX_BASE) == 0 &&
	      (get32(gmac.registers, R_MAC_CONFIG) & 0x2003U) == 0x2003U && carrier == 1,
	      "reopen: fresh rings, MAC at the link, carrier");
	packet = make_frame(60, 0x55);
	error = published->ops->transmit(published, packet);
	check(error == 0 && gmac.sent[(gmac.sent_count - 1U) % 600U].data[0] == 0x55, "reopen: sends");
}

/* Attaches that fail, and a random address when nothing gives one. */
static void
check_failures(
	void)
{
	struct drv_dw_eqos_config config;
	int error;

	/* No PHY answers. */
	reset_model();
	fill_config(&config);
	gmac.phy.present = 0;
	published = NULL;
	error = drv_dw_eqos_attach(&config);
	check(error == ENODEV && published == NULL, "no PHY: ENODEV, nothing published");

	/* The reset does not finish. */
	reset_model();
	fill_config(&config);
	gmac.reset_stuck = 1;
	error = drv_dw_eqos_attach(&config);
	check(error == ETIMEDOUT && published == NULL, "reset never ends: ETIMEDOUT");

	/* No address from the board nor the controller: random, locally administered. */
	reset_model();
	fill_config(&config);
	config.has_mac = 0;
	error = drv_dw_eqos_attach(&config);
	check(error == 0 && published != NULL && random_calls == 1 && published->hwaddr[0] == 0x12 &&
	      published->hwaddr[5] == 0x11,
	      "no address: random with the local bit set and the multicast bit clear");
	error = drv_dw_eqos_attach(&config);
	check(error == EALREADY, "the same registers twice: EALREADY");
}

/* A PHY other than the YT8531 and the address the firmware left in the controller. */
static void
check_generic(
	void)
{
	struct drv_dw_eqos_config config;
	int error;

	reset_model();
	fill_config(&config);
	config.has_mac = 0;
	config.set_speed = NULL;
	put32(gmac.registers, R_ADDR_HIGH, 0x80000605U);
	put32(gmac.registers, R_ADDR_LOW, 0x04030200U);
	gmac.phy.regs[2] = 0x001cU;
	gmac.phy.regs[3] = 0xc916U;
	gmac.phy.rgmii_config = 0x1234U;
	error = drv_dw_eqos_attach(&config);
	check(error == 0 && published != NULL && published->hwaddr[0] == 0x00 && published->hwaddr[1] == 0x02 &&
	      published->hwaddr[5] == 0x06 && random_calls == 0,
	      "MAC from the controller");
	check(gmac.phy.rgmii_config == 0x1234U && gmac.phy.resets == 1U && gmac.phy.regs[0] == 0x1200U,
	      "generic PHY: reset and autonegotiation, no YT8531 registers touched");
	error = published->ops->open(published);
	set_link(100, 1);
	run_service();
	check(error == 0 && carrier == 1 && gmac.phy.rgmii_config == 0x1234U && published->link_mbps == 100U,
	      "generic PHY: 100 up, no clock inversion written");
}

/* The glue on a VisionFive 2-shaped tree. */
static void
check_glue(
	const char *path,
	int v13b)
{
	struct drv_fdt fdt;
	unsigned char *blob;
	FILE *file;
	long length;
	uint32_t value;
	int error;

	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	blob = malloc((size_t)length);
	if (blob == NULL || fread(blob, 1, (size_t)length, file) != (size_t)length) {
		perror(path);
		exit(2);
	}
	fclose(file);

	/* PLL0 1.5 GHz, PLL2 1188 MHz; bus root from PLL2, AXI config / 3, STG AXI/AHB / 2 = 198 MHz. */
	put32(sys_syscon, 0x18, (1U << 24) | (1U << 25));
	put32(sys_syscon, 0x1c, 125U);
	put32(sys_syscon, 0x20, 0U << 28);
	put32(sys_syscon, 0x24, 2U);
	put32(sys_syscon, 0x2c, 1U | (1U << 15) | (1U << 16) | (99U << 17));
	put32(sys_syscon, 0x30, 0U << 28);
	put32(sys_syscon, 0x34, 2U);
	put32(sys_crg, 5 * 4, 1U << 24);
	put32(sys_crg, 7 * 4, 3U);
	put32(sys_crg, 8 * 4, 2U);
	put32(sys_crg, 108 * 4, 10U);
	put32(aon_crg, 4 * 4, 1U);
	put32(aon_crg, 5 * 4, 0U);
	put32(aon_syscon, 0xc, 0xffffffffU);

	error = drv_fdt_open(&fdt, blob, (size_t)length);
	check(error == 0, "tree opens");
	error = drv_vf2_syscon_init(&fdt);
	check(error == 0, "system control blocks");
	error = drv_vf2_crg_init(&fdt);
	check(error == 0, "clock generators");
	error = drv_vf2_pinctrl_init(&fdt);
	check(error == 0, "pin controllers");

	reset_model();
	if (!v13b) {
		gmac.phy.chip_config = 0x0000U;
		put32(gmac.registers, R_ADDR_HIGH, 0x80000605U);
		put32(gmac.registers, R_ADDR_LOW, 0x04030200U);
	}
	error = drv_vf2_ethernet_attach(&fdt, 1);
	check(error == 0 && published != NULL, "glue: port attaches as en0");
	if (error != 0)
		return;

	value = get32(aon_syscon, 0xc);
	check(value == ((0xffffffffU & ~(7U << 18)) | (1U << 18)), "glue: AON SYSCON 0xc bits 20:18 = RGMII, rest kept");
	check((get32(aon_crg, 2 * 4) & 0x80000000U) != 0 && (get32(aon_crg, 3 * 4) & 0x80000000U) != 0 &&
	      (get32(aon_crg, 5 * 4) & 0x80000000U) != 0 && (get32(sys_crg, 109 * 4) & 0x80000000U) != 0 &&
	      (get32(sys_crg, 108 * 4) & 0x80000000U) != 0,
	      "glue: AHB, AXI, TX, PTP and GTX source gates open");
	check((get32(aon_crg, 0x38) & 3U) == 0, "glue: both resets released");
	check(irq_number == 7, "glue: macirq is PLIC source 7");
	check(((gmac.last_mdio >> 8) & 7U) == 4U, "glue: MDIO range from the 198 MHz AXI clock");
	check(dma_constraints.coherent == 1, "glue: board default coherent (the tree says nothing)");
	check(get32(gmac.registers, R_DMA_BUS) == (0x80000000U | (15U << 24) | (15U << 16) | 0x800U | 0xf0U | 1U),
	      "glue: snps,axi-config and fixed burst from the tree");

	if (v13b) {
		check(((get32(aon_crg, 5 * 4) >> 24) & 0x3fU) == 1U, "v1.3B: TX clock from RMII RTX (assigned parent)");
		check(published->hwaddr[0] == 0x6c && published->hwaddr[5] == 0x34, "v1.3B: local-mac-address");
		check(((gmac.phy.rgmii_config >> 10) & 0xfU) == 10U && (gmac.phy.rgmii_config & 0xfU) == 10U &&
		      ((gmac.phy.pad_drive >> 13) & 7U) == 6U,
		      "v1.3B: PHY delays 1500 ps, drive from the tree");
		set_link(1000, 1);
		run_service();
		check(carrier == 1 && (gmac.phy.rgmii_config & 0x4000U) != 0 && (get32(sys_crg, 108 * 4) & 0xffU) == 10U,
		      "v1.3B: 1000 up, PHY clock inverted, GTX source untouched");
	} else {
		check(((get32(aon_crg, 5 * 4) >> 24) & 0x3fU) == 0, "v1.2A: TX clock stays on the GTX source");
		check(published->hwaddr[0] == 0x00 && published->hwaddr[1] == 0x02, "v1.2A: MAC the firmware left");
		check((gmac.phy.chip_config & 0x0100U) != 0 && ((gmac.phy.rgmii_config >> 10) & 0xfU) == 0 &&
		      (gmac.phy.rgmii_config & 0xfU) == 9U && ((gmac.phy.pad_drive >> 13) & 7U) == 3U,
		      "v1.2A: rx 1900 ps as the fixed part, tx 1350 ps = step 9, default drive");
		set_link(1000, 1);
		run_service();
		check(carrier == 1 && (get32(sys_crg, 108 * 4) & 0xffU) == 12U, "v1.2A: 1000 sets GTX source 1.5 GHz / 12");
		set_link(100, 1);
		run_service();
		check(carrier == 1 && (get32(sys_crg, 108 * 4) & 0xffU) == 12U, "v1.2A: 100 is out of the divider's reach, divider kept");
	}
}

int
main(
	int argc,
	char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "driver") == 0) {
		check_driver();
	} else if (argc >= 2 && strcmp(argv[1], "failures") == 0) {
		check_failures();
	} else if (argc >= 2 && strcmp(argv[1], "generic") == 0) {
		check_generic();
	} else if (argc >= 3 && strcmp(argv[1], "glue-v13b") == 0) {
		check_glue(argv[2], 1);
	} else if (argc >= 3 && strcmp(argv[1], "glue-v12a") == 0) {
		check_glue(argv[2], 0);
	} else {
		fprintf(stderr, "usage: %s driver | failures | generic | glue-v13b dtb | glue-v12a dtb\n", argv[0]);
		return 2;
	}

	printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
	return failures == 0 ? 0 : 1;
}
