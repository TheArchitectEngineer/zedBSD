/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amd64 HAL's early console framebuffer window: how the
 * system page table records it, gives it to the kernel's console once
 * kernel_putc is published, and removes it with a global TLB shootdown,
 * and what stays as before (dynamic device views, the fixed windows).
 *
 * The real src/hal/amd64/space.c is compiled into this file, so its static
 * state (the window, the system directories, the shootdown pool) is read
 * directly.  The real framebuffer-map.c builds the window's entries, and
 * the real bsp-pcat/cons.c (with its output fixture) is the early console
 * whose pointer the removal clears.  Two simulated CPUs: CPU 0 sends and
 * has CR4.PGE set, CPU 1 receives each shootdown through the vector stub
 * and has no PGE.  GS points at CPU 0's per-CPU record, so the GS-relative
 * loads of space.c read it.
 *
 * usage: hal-fbwindow-host-test SCENE   (1..7, see main())
 */

#include <asm/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "src/hal/amd64/space.c"
#include "drivers/platform/pcat/graphics/splash.h"
#include "drivers/platform/pcat/graphics/vgafont.h"

#define TEST_CPUS 2U
#define TEST_PGE ((uint64_t)1U << 7)
#define TEST_CR4_CPU0 ((uint64_t)0x6b0U)
#define TEST_CR4_CPU1 ((uint64_t)0x620U)
#define TEST_EDGE0 ((uint64_t)0x7000U)
#define TEST_EDGE1 ((uint64_t)0x8000U)
#define TEST_MMIO_BASE ((uintptr_t)0xffffffffc0000000ULL)
#define TEST_PIXELS (640U * 400U)
#define TEST_TABLE_PAGES 8U
#define TEST_TABLE_PHYSICAL ((uint64_t)0x100000000ULL)
#define TEST_REQUESTS 16U
#define TEST_UC (HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_NOCACHE)

/* What one simulated CPU did to its translation caches. */
struct test_cpu_record {
	unsigned cr4_reads;
	unsigned cr4_writes;
	uint64_t cr4_written[4];
	unsigned cr3_reloads;
};

/* One shootdown request as it was delivered to the remote CPU. */
struct test_request {
	hal_space_t space;
	void *vaddr;
	size_t size;
	unsigned global;
};

/* The early console's output fixture in cons.c (KERN_CONSOLE_OUTPUT_TEST). */
void pcat_console_output_test_reset(uint32_t *pixels, size_t pixel_count);
int pcat_console_output_test_state(unsigned *row, unsigned *column);

/* The HAL's kernel console hook, which the kernel publishes. */
void (*kernel_putc)(int c);

/* The kernel's font, which the early console's fixture never draws here. */
const uint8_t drv_pcat_vgafont16[PCAT_VGAFONT_GLYPHS * PCAT_VGAFONT_HEIGHT];

/* The linker's kernel extent, which the paths under test never read. */
char __kernel_virt_start[1];
char __kernel_virt_end[1];
char __kernel_text_phys_start[1];
char __kernel_text_phys_end[1];
char __kernel_rodata_phys_start[1];
char __kernel_rodata_phys_end[1];

/* The simulated CPUs: their records, CR4, and which one is flushing now. */
static struct amd64_percpu test_percpu[TEST_CPUS];
static struct test_cpu_record test_cpu[TEST_CPUS];
static uint64_t test_cr4[TEST_CPUS];
static unsigned test_flushing_cpu;

/* The shootdown requests delivered since the last reset. */
static struct test_request test_requests[TEST_REQUESTS];
static unsigned test_request_count;

/* The simulated interrupt flag of the sending CPU. */
static int test_irq_enabled = 1;

/* RAM pages handed out as page tables, by fake physical address. */
static uint64_t test_tables[TEST_TABLE_PAGES][512] __attribute__((aligned(4096)));
static unsigned test_tables_used;

/* The early console's framebuffer and the characters the kernel's console got. */
static uint32_t *test_pixels;
static unsigned test_kernel_chars;

static int failures;
static int checks;

#define CHECK(condition)							\
	do {									\
		checks++;							\
		if (!(condition)) {						\
			fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__,	\
			    #condition);					\
			failures++;						\
		}								\
	} while (0)

/*
 * Stubs of the HAL's other parts.
 */

uint64_t
asm_get_cr4(
	void)
{
	test_cpu[test_flushing_cpu].cr4_reads++;
	return test_cr4[test_flushing_cpu];
}

void
asm_load_cr4(
	uint64_t value)
{
	struct test_cpu_record *record;

	record = &test_cpu[test_flushing_cpu];
	if (record->cr4_writes < 4U)
		record->cr4_written[record->cr4_writes] = value;
	record->cr4_writes++;
	test_cr4[test_flushing_cpu] = value;
}

void
asm_flush_tlb(
	void)
{
	test_cpu[test_flushing_cpu].cr3_reloads++;
}

void
asm_load_cr3(
	uintptr_t address)
{
	(void)address;
	fprintf(stderr, "unexpected CR3 load\n");
	abort();
}

uint64_t
asm_read_msr(
	uint32_t msr)
{
	(void)msr;
	abort();
}

void
asm_write_msr(
	uint32_t msr,
	uint64_t value)
{
	(void)msr;
	(void)value;
	abort();
}

void
asm_outb(
	uint16_t port,
	uint8_t data)
{
	(void)port;
	(void)data;
}

bool
hal_irq_disable(
	void)
{
	bool enabled;

	enabled = test_irq_enabled != 0;
	test_irq_enabled = 0;
	return enabled;
}

void
hal_irq_enable(
	void)
{
	test_irq_enabled = 1;
}

void
hal_irq_send_eoi(
	hal_irq_ack_t acknowledge)
{
	(void)acknowledge;
	abort();
}

void
hal_irq_mask(
	int irq_num)
{
	(void)irq_num;
}

unsigned
hal_cpu_count(
	void)
{
	return TEST_CPUS;
}

hal_cpu_id_t
hal_cpu_current(
	void)
{
	return 0;
}

void
hal_cpu_ready_mask(
	struct hal_cpu_mask *result)
{
	hal_cpu_mask_zero(result);
	hal_cpu_mask_set(result, 0);
	hal_cpu_mask_set(result, 1);
}

struct amd64_percpu *
amd64_percpu_get(
	hal_cpu_id_t cpu)
{
	return &test_percpu[cpu];
}

struct amd64_percpu *
amd64_percpu_current(
	void)
{
	return &test_percpu[0];
}

uint32_t
amd64_smp_apic_id(
	hal_cpu_id_t cpu)
{
	return cpu;
}

/*
 * Delivers a shootdown vector: records the request the target finds active
 * and lets the target service it, as its interrupt handler would.
 */
int
amd64_lapic_send_vector(
	uint32_t apic_id,
	uint8_t vector)
{
	struct amd64_shootdown_request *request;
	unsigned slot;

	if (vector != AMD64_VECTOR_TLB || apic_id >= TEST_CPUS || apic_id == 0) {
		fprintf(stderr, "unexpected vector %u to %u\n", vector, apic_id);
		abort();
	}
	for (slot = 0; slot < AMD64_SHOOTDOWN_REQUESTS; slot++) {
		request = &shootdowns[slot];
		if (request->active != 1U || (request->pending & ((uint64_t)1U << apic_id)) == 0)
			continue;
		if (test_request_count < TEST_REQUESTS) {
			test_requests[test_request_count].space = request->space;
			test_requests[test_request_count].vaddr = request->vaddr;
			test_requests[test_request_count].size = request->size;
			test_requests[test_request_count].global = request->global;
		}
		test_request_count++;
	}
	test_flushing_cpu = apic_id;
	service_shootdowns(apic_id);
	test_flushing_cpu = 0;
	return HAL_OK;
}

int
hal_pmem_alloc(
	size_t req_size,
	size_t req_align,
	hal_physaddr_t *block)
{
	if (req_size != PAGE_SIZE || req_align != PAGE_SIZE || test_tables_used == TEST_TABLE_PAGES)
		abort();
	*block = TEST_TABLE_PHYSICAL + (uint64_t)test_tables_used * PAGE_SIZE;
	test_tables_used++;
	return HAL_OK;
}

void *
hal_pmem_to_kernel(
	hal_physaddr_t paddr)
{
	uint64_t index;

	index = (paddr - TEST_TABLE_PHYSICAL) / PAGE_SIZE;
	if (paddr < TEST_TABLE_PHYSICAL || index >= test_tables_used)
		abort();
	return test_tables[index];
}

int
hal_pmem_free(
	hal_physaddr_t *block,
	size_t size)
{
	(void)block;
	(void)size;
	abort();
}

void *
hal_memset(
	void *s,
	int c,
	size_t n)
{
	return memset(s, c, n);
}

void *
kernel_alloc(
	size_t size)
{
	return malloc(size);
}

void
kernel_free(
	void *p)
{
	free(p);
}

void
hal_fatal(
	const char *file,
	int line,
	const char *s)
{
	fprintf(stderr, "HAL_FATAL %s:%d: %s\n", file, line, s);
	abort();
}

int
hal_printf(
	const char *format,
	...)
{
	(void)format;
	return 0;
}

int
hal_puts(
	const char *s)
{
	(void)s;
	return 0;
}

void *
hal_get_arch_handoff(
	const char *name)
{
	(void)name;
	return NULL;
}

const struct zbl6_framebuffer *
bsp_boot_framebuffer(
	void)
{
	return NULL;
}

int
amd64_ram_lookup(
	const struct amd64_ram_builder *builder,
	uint64_t physical,
	uint64_t *entry)
{
	(void)builder;
	(void)physical;
	(void)entry;
	return 0;
}

void
drv_pcat_splash_start(
	volatile uint32_t *pixels,
	unsigned width,
	unsigned height,
	unsigned stride,
	int rgbx)
{
	(void)pixels;
	(void)width;
	(void)height;
	(void)stride;
	(void)rgbx;
}

void
drv_pcat_splash_step(
	void)
{
}

/* The parts of the boot path the scenes never reach. */
void amd64_acpi_window_init(struct amd64_acpi_window *window) { (void)window; abort(); }
int amd64_acpi_window_reserve(struct amd64_acpi_window *window, paddr_t physical, size_t size, unsigned *first_slot, size_t *page_offset, unsigned *new_first_slot, unsigned *new_slot_count) { (void)window; (void)physical; (void)size; (void)first_slot; (void)page_offset; (void)new_first_slot; (void)new_slot_count; abort(); }
int amd64_early_reservation(uint32_t index, uint64_t *physical, uint64_t *size) { (void)index; (void)physical; (void)size; abort(); }
int amd64_early_table_page(uint64_t *physical, int mapped) { (void)physical; (void)mapped; abort(); }
hal_irq_ack_t amd64_irq_ack_begin(uint32_t vector, int irq) { (void)vector; (void)irq; abort(); }
const struct amd64_kernel_image *amd64_kernel_image(void) { abort(); }
int amd64_kernel_image_owns(uint64_t physical) { (void)physical; abort(); }
enum amd64_ram_result amd64_ram_map(struct amd64_ram_builder *builder, uint64_t physical, uint64_t size, uint64_t flags) { (void)builder; (void)physical; (void)size; (void)flags; abort(); }
int bsp_mem_attributes(uint32_t index, uint64_t *attributes) { (void)index; (void)attributes; abort(); }
uint32_t bsp_memory_source(void) { abort(); }
int bsp_mem_range(uint32_t index, uint64_t *base, uint64_t *size, uint32_t *type) { (void)index; (void)base; (void)size; (void)type; abort(); }
uint32_t bsp_mem_range_count(void) { abort(); }
int bsp_physical_range_mappable(uint64_t physical, size_t size) { (void)physical; (void)size; abort(); }

/*
 * The fixture.
 */

static void
test_kernel_putc(
	int c)
{
	(void)c;
	test_kernel_chars++;
}

/* Forgets what the simulated CPUs did and the requests they got. */
static void
reset_records(
	void)
{
	memset(test_cpu, 0, sizeof(test_cpu));
	memset(test_requests, 0, sizeof(test_requests));
	test_request_count = 0;
	test_cr4[0] = TEST_CR4_CPU0;
	test_cr4[1] = TEST_CR4_CPU1;
}

/* Reports whether no CPU flushed anything and no request was sent. */
static int
nothing_flushed(
	void)
{
	unsigned cpu;

	if (test_request_count != 0)
		return 0;
	for (cpu = 0; cpu < TEST_CPUS; cpu++) {
		if (test_cpu[cpu].cr4_reads != 0 || test_cpu[cpu].cr4_writes != 0 || test_cpu[cpu].cr3_reloads != 0)
			return 0;
	}
	return 1;
}

/* Reports whether the HAL left interrupts and its registry lock as they were. */
static int
hal_quiet(
	void)
{
	if (test_irq_enabled != 1)
		return 0;
	if (space_registry_lock != 0)
		return 0;
	return 1;
}

/*
 * Builds the system page table's framebuffer window the way the boot path
 * does (framebuffer-map.c, then the record), with the fixed low MMIO and
 * APIC entries beside it, and resets the early console onto a RAM
 * framebuffer and kernel_putc to NULL.
 */
static void
setup(
	uint64_t physical,
	uint64_t size)
{
	uint64_t tables[2];
	unsigned index;
	int mapped;

	memset(system_mmio_pd, 0, sizeof(system_mmio_pd));
	memset(system_framebuffer_edges, 0, sizeof(system_framebuffer_edges));
	memset(&fb_window, 0, sizeof(fb_window));
	for (index = 0; index < 8U; index++)
		system_mmio_pd[index] = (0xf0000000ULL + (uint64_t)index * 0x200000ULL) | 0x8000000000000193ULL;
	system_mmio_pd[8] = 0xfee00000ULL | 0x8000000000000193ULL;
	system_mmio_pd[9] = 0xfec00000ULL | 0x8000000000000193ULL;
	acpi_physical_max = ((uint64_t)1U << 46) - 1U;

	tables[0] = TEST_EDGE0;
	tables[1] = TEST_EDGE1;
	mapped = amd64_framebuffer_map(&system_mmio_pd[AMD64_FRAMEBUFFER_PD_FIRST],
	    AMD64_FRAMEBUFFER_PD_COUNT, system_framebuffer_edges, tables,
	    physical, size, acpi_physical_max);
	if (!mapped) {
		fprintf(stderr, "framebuffer map refused %#llx+%#llx\n",
		    (unsigned long long)physical, (unsigned long long)size);
		abort();
	}
	fb_window_hold(physical, size);

	pcat_console_output_test_reset(test_pixels, TEST_PIXELS);
	kernel_putc = NULL;
	test_kernel_chars = 0;
	reset_records();
}

/* Translates a kernel address of the low MMIO directory through the window's entries. */
static int
translate(
	uintptr_t address,
	uint64_t *physical)
{
	uint64_t entry;
	uint64_t table;
	uint64_t *edge;
	unsigned index;

	index = (unsigned)((address - TEST_MMIO_BASE) / 0x200000U);
	if (address < TEST_MMIO_BASE || index >= 512U)
		return 0;
	entry = system_mmio_pd[index];
	if ((entry & 1U) == 0)
		return 0;
	if ((entry & 0x80U) != 0) {
		*physical = (entry & 0x000fffffffe00000ULL) + (address & 0x1fffffU);
		return 1;
	}
	table = entry & 0x000ffffffffff000ULL;
	if (table == TEST_EDGE0) {
		edge = system_framebuffer_edges[0];
	} else if (table == TEST_EDGE1) {
		edge = system_framebuffer_edges[1];
	} else {
		return 0;
	}
	entry = edge[(address >> 12) & 511U];
	if ((entry & 1U) == 0)
		return 0;
	*physical = (entry & 0x000ffffffffff000ULL) + (address & 0xfffU);
	return 1;
}

/* Reports whether an address lies in the dynamic device window. */
static int
in_device_window(
	void *address)
{
	uintptr_t value;

	value = (uintptr_t)address;
	if (value < (uintptr_t)AMD64_DEVICE_WINDOW_BASE)
		return 0;
	if (value - (uintptr_t)AMD64_DEVICE_WINDOW_BASE >= (uintptr_t)AMD64_DEVICE_PAGE_COUNT * PAGE_SIZE)
		return 0;
	return 1;
}

/* Maps a request that must take the ordinary path (a dynamic view), and releases it. */
static void
check_ordinary_map(
	hal_physaddr_t physical,
	size_t size,
	uint32_t attr,
	enum amd64_fb_window_state state)
{
	void *address;
	int error;

	address = NULL;
	error = hal_space_map_device(physical, size, attr, &address);
	CHECK(error == HAL_OK);
	CHECK(address != (void *)fb_window.address);
	CHECK(in_device_window(address));
	CHECK(fb_window.state == state);
	CHECK(hal_quiet());
	if (error != HAL_OK)
		return;
	error = hal_space_unmap_device(address, size);
	CHECK(error == HAL_OK);
	CHECK(fb_window.state == state);
	CHECK(hal_quiet());
}

/* Reports whether exactly one ordinary system shootdown was flushed by both CPUs. */
static int
one_ordinary_shootdown(
	void)
{
	if (test_request_count != 1)
		return 0;
	if (test_requests[0].global != 0U || test_requests[0].space != HAL_SPACE_SYS)
		return 0;
	if (test_cpu[0].cr4_reads != 0 || test_cpu[0].cr4_writes != 0 || test_cpu[0].cr3_reloads != 1)
		return 0;
	if (test_cpu[1].cr4_reads != 0 || test_cpu[1].cr4_writes != 0 || test_cpu[1].cr3_reloads != 1)
		return 0;
	return 1;
}

/*
 * The scenes.
 */

/* 1: the recorded window matches the handoff and the entries the boot path built. */
static void
scene_hold(
	void)
{
	static const uint64_t geometry[][2] = {
		{ 0xc0000000ULL, 0x300000ULL },
		{ 0xc0101000ULL, 0x1d4c00ULL },
		{ 0xe0000000ULL, 0x200000ULL },
		{ 0x801ff000ULL, 0x2000ULL },
		{ 0x4000000000ULL, 0x7e9000ULL },
	};
	uint64_t physical;
	uint64_t aligned;
	uint64_t end;
	unsigned count;
	unsigned index;
	unsigned present;
	unsigned g;

	for (g = 0; g < sizeof(geometry) / sizeof(geometry[0]); g++) {
		setup(geometry[g][0], geometry[g][1]);
		aligned = geometry[g][0] & ~0x1fffffULL;
		end = (geometry[g][0] + geometry[g][1] + 4095U) & ~4095ULL;
		count = (unsigned)((end - aligned + 0x1fffffU) / 0x200000U);

		CHECK(fb_window.state == AMD64_FB_WINDOW_HELD);
		CHECK(fb_window.physical == geometry[g][0]);
		CHECK(fb_window.size == geometry[g][1]);
		CHECK(fb_window.address == ZBL6_FRAMEBUFFER_VIRTUAL_BASE + (geometry[g][0] & 0x1fffffU));
		CHECK(fb_window.pd_count == count);

		/* The first and the last byte translate to the framebuffer's. */
		physical = 0;
		CHECK(translate(fb_window.address, &physical) && physical == geometry[g][0]);
		physical = 0;
		CHECK(translate(fb_window.address + fb_window.size - 1U, &physical) &&
		    physical == geometry[g][0] + geometry[g][1] - 1U);

		/* The window takes exactly pd_count directory entries from the first. */
		present = 0;
		for (index = AMD64_FRAMEBUFFER_PD_FIRST; index < 512U; index++) {
			if ((system_mmio_pd[index] & 1U) != 0)
				present++;
		}
		CHECK(present == count);
		for (index = 0; index < count; index++)
			CHECK((system_mmio_pd[AMD64_FRAMEBUFFER_PD_FIRST + index] & 1U) != 0);
	}
}

/* 2: the window goes only to the exact uncached request after kernel_putc, once. */
static void
scene_give(
	void)
{
	const hal_physaddr_t physical = 0xc0101000ULL;
	const size_t size = 0x1d5000U;
	void *address;
	int error;

	setup(physical, size);

	/* While the early console prints, the exact request takes the ordinary path. */
	check_ordinary_map(physical, size, TEST_UC, AMD64_FB_WINDOW_HELD);

	/* The kernel prints from here. */
	kernel_putc = test_kernel_putc;

	/* A different size, attribute or base takes the ordinary path. */
	check_ordinary_map(physical, size - PAGE_SIZE, TEST_UC, AMD64_FB_WINDOW_HELD);
	check_ordinary_map(physical, size + PAGE_SIZE, TEST_UC, AMD64_FB_WINDOW_HELD);
	check_ordinary_map(physical, size, HAL_SPACE_READ | HAL_SPACE_WRITE, AMD64_FB_WINDOW_HELD);
	check_ordinary_map(physical, size, HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_WC, AMD64_FB_WINDOW_HELD);
	check_ordinary_map(physical, size, HAL_SPACE_READ | HAL_SPACE_NOCACHE, AMD64_FB_WINDOW_HELD);
	check_ordinary_map(physical, size, TEST_UC | HAL_SPACE_DEVICE, AMD64_FB_WINDOW_HELD);
	check_ordinary_map(physical + PAGE_SIZE, size, TEST_UC, AMD64_FB_WINDOW_HELD);

	/* The exact uncached request gets the window, without a shootdown. */
	reset_records();
	address = NULL;
	error = hal_space_map_device(physical, size, TEST_UC, &address);
	CHECK(error == HAL_OK);
	CHECK(address == (void *)fb_window.address);
	CHECK((uintptr_t)address == ZBL6_FRAMEBUFFER_VIRTUAL_BASE + 0x101000U);
	CHECK(fb_window.state == AMD64_FB_WINDOW_GIVEN);
	CHECK(nothing_flushed());
	CHECK(hal_quiet());

	/* A second exact request takes the ordinary path. */
	check_ordinary_map(physical, size, TEST_UC, AMD64_FB_WINDOW_GIVEN);
}

/* 3: the given window's release clears its entries, the early console, and every TLB. */
static void
scene_remove(
	void)
{
	const hal_physaddr_t physical = 0xc0101000ULL;
	const size_t size = 0x3d4c00U;
	uint64_t before_low[16];
	uint64_t before_high[512 - 16];
	void *address;
	unsigned index;
	unsigned slot;
	unsigned zero;
	unsigned count;
	uint32_t checksum;
	uint32_t after;
	int error;

	setup(physical, size);
	count = fb_window.pd_count;
	CHECK(count == 3U);
	memcpy(before_low, system_mmio_pd, sizeof(before_low));
	CHECK(pcat_console_output_test_state(NULL, NULL) == 1);

	kernel_putc = test_kernel_putc;
	address = NULL;
	error = hal_space_map_device(physical, size, TEST_UC, &address);
	CHECK(error == HAL_OK && address == (void *)fb_window.address);

	/* A release of a different size is not of the window, and changes nothing. */
	memcpy(before_high, &system_mmio_pd[16], sizeof(before_high));
	reset_records();
	error = hal_space_unmap_device(address, size - 1U);
	CHECK(error == HAL_ERR_INVALID);
	CHECK(fb_window.state == AMD64_FB_WINDOW_GIVEN);
	CHECK(memcmp(before_high, &system_mmio_pd[16], sizeof(before_high)) == 0);
	CHECK(nothing_flushed());
	CHECK(hal_quiet());

	/* The release of the window removes it. */
	reset_records();
	error = hal_space_unmap_device(address, size);
	CHECK(error == HAL_OK);
	CHECK(fb_window.state == AMD64_FB_WINDOW_REMOVED);
	CHECK(hal_quiet());

	/* Its directory entries and both edge tables are empty; the others stay. */
	for (index = 0; index < count; index++)
		CHECK(system_mmio_pd[AMD64_FRAMEBUFFER_PD_FIRST + index] == 0);
	zero = 1;
	for (index = 0; index < 2U; index++) {
		for (slot = 0; slot < 512U; slot++) {
			if (system_framebuffer_edges[index][slot] != 0)
				zero = 0;
		}
	}
	CHECK(zero);
	CHECK(memcmp(before_low, system_mmio_pd, sizeof(before_low)) == 0);
	for (index = 0; index < 512U; index++) {
		if (index >= AMD64_FRAMEBUFFER_PD_FIRST && index < AMD64_FRAMEBUFFER_PD_FIRST + count)
			continue;
		if (index < 16U)
			continue;
		CHECK(system_mmio_pd[index] == 0);
	}

	/* The early console forgot the framebuffer. */
	CHECK(pcat_console_output_test_state(NULL, NULL) == 0);

	/* Exactly one global request: CPU 0 (PGE) toggled PGE, CPU 1 (no PGE) reloaded CR3. */
	CHECK(test_request_count == 1);
	CHECK(test_requests[0].global == 1U);
	CHECK(test_requests[0].space == HAL_SPACE_SYS);
	CHECK(test_requests[0].vaddr == NULL && test_requests[0].size == 0);
	CHECK(test_cpu[0].cr4_reads == 1);
	CHECK(test_cpu[0].cr4_writes == 2);
	CHECK(test_cpu[0].cr4_written[0] == (TEST_CR4_CPU0 & ~TEST_PGE));
	CHECK(test_cpu[0].cr4_written[1] == TEST_CR4_CPU0);
	CHECK(test_cpu[0].cr3_reloads == 0);
	CHECK(test_cpu[1].cr4_reads == 1);
	CHECK(test_cpu[1].cr4_writes == 0);
	CHECK(test_cpu[1].cr3_reloads == 1);
	CHECK(test_cr4[0] == TEST_CR4_CPU0);

	/* Characters still go to the kernel and never reach the framebuffer. */
	checksum = 0;
	for (index = 0; index < TEST_PIXELS; index++)
		checksum = checksum * 31U + test_pixels[index];
	hal_putc('A');
	hal_putc('\n');
	CHECK(test_kernel_chars == 2);
	after = 0;
	for (index = 0; index < TEST_PIXELS; index++)
		after = after * 31U + test_pixels[index];
	CHECK(after == checksum);

	/* A second release finds nothing, and flushes nothing. */
	reset_records();
	error = hal_space_unmap_device(address, size);
	CHECK(error == HAL_ERR_INVALID);
	CHECK(fb_window.state == AMD64_FB_WINDOW_REMOVED);
	CHECK(nothing_flushed());
	CHECK(hal_quiet());
}

/* 4: a dynamic view's release keeps the ordinary, non-global shootdown. */
static void
scene_dynamic(
	void)
{
	void *address;
	int error;

	setup(0xc0000000ULL, 0x300000ULL);
	kernel_putc = test_kernel_putc;

	/* A BAR elsewhere maps to a dynamic view through one ordinary shootdown. */
	reset_records();
	address = NULL;
	error = hal_space_map_device(0xe0000000ULL, 0x10000U, TEST_UC, &address);
	CHECK(error == HAL_OK);
	CHECK(in_device_window(address));
	CHECK(one_ordinary_shootdown());
	CHECK(hal_quiet());

	/* Its release is one ordinary shootdown: no CR4 access on either CPU. */
	reset_records();
	error = hal_space_unmap_device(address, 0x10000U);
	CHECK(error == HAL_OK);
	CHECK(one_ordinary_shootdown());
	CHECK(fb_window.state == AMD64_FB_WINDOW_HELD);
	CHECK(hal_quiet());

	/* The same while the window is given. */
	error = hal_space_map_device(0xc0000000ULL, 0x300000U, TEST_UC, &address);
	CHECK(error == HAL_OK && fb_window.state == AMD64_FB_WINDOW_GIVEN);
	error = hal_space_map_device(0xe0000000ULL, 0x10000U, TEST_UC, &address);
	CHECK(error == HAL_OK);
	reset_records();
	error = hal_space_unmap_device(address, 0x10000U);
	CHECK(error == HAL_OK);
	CHECK(one_ordinary_shootdown());
	CHECK(fb_window.state == AMD64_FB_WINDOW_GIVEN);
	CHECK(hal_quiet());
}

/* 5: a framebuffer meeting a fixed window keeps its window: busy, nothing changes. */
static void
scene_fixed(
	void)
{
	static const uint64_t geometry[][2] = {
		{ 0xf0200000ULL, 0x400000ULL },
		{ 0xefe00000ULL, 0x400000ULL },
		{ 0xf0e00000ULL, 0x400000ULL },
	};
	uint64_t directory[512];
	uint64_t edges[2][512];
	void *address;
	unsigned g;
	int error;

	for (g = 0; g < sizeof(geometry) / sizeof(geometry[0]); g++) {
		setup(geometry[g][0], geometry[g][1]);
		kernel_putc = test_kernel_putc;
		address = NULL;
		error = hal_space_map_device(geometry[g][0], geometry[g][1], TEST_UC, &address);
		CHECK(error == HAL_OK);
		CHECK(address == (void *)fb_window.address);
		CHECK(fb_window.state == AMD64_FB_WINDOW_GIVEN);
		memcpy(directory, system_mmio_pd, sizeof(directory));
		memcpy(edges, system_framebuffer_edges, sizeof(edges));

		/* Twice: each release is refused the same way. */
		reset_records();
		error = hal_space_unmap_device(address, geometry[g][1]);
		CHECK(error == HAL_ERR_BUSY);
		error = hal_space_unmap_device(address, geometry[g][1]);
		CHECK(error == HAL_ERR_BUSY);
		CHECK(fb_window.state == AMD64_FB_WINDOW_GIVEN);
		CHECK(memcmp(directory, system_mmio_pd, sizeof(directory)) == 0);
		CHECK(memcmp(edges, system_framebuffer_edges, sizeof(edges)) == 0);
		CHECK(nothing_flushed());
		CHECK(pcat_console_output_test_state(NULL, NULL) == 1);
		CHECK(hal_quiet());
	}

	/* The overlap test at the edges of every fixed window. */
	CHECK(fb_window_meets_fixed(0x9f000U, 0x1000U) == 0);
	CHECK(fb_window_meets_fixed(0x9f000U, 0x1001U) == 1);
	CHECK(fb_window_meets_fixed(0xfffffU, 0x1U) == 1);
	CHECK(fb_window_meets_fixed(0x100000U, 0x1000U) == 0);
	CHECK(fb_window_meets_fixed(0xeff00000U, 0x100000U) == 0);
	CHECK(fb_window_meets_fixed(0xeff00000U, 0x100001U) == 1);
	CHECK(fb_window_meets_fixed(0xf1000000U, 0x1000U) == 0);
	CHECK(fb_window_meets_fixed(0xfebff000U, 0x1000U) == 0);
	CHECK(fb_window_meets_fixed(0xfebff000U, 0x2000U) == 1);
	CHECK(fb_window_meets_fixed(0xfef00000U, 0x1000U) == 1);
	CHECK(fb_window_meets_fixed(0xff000000U, 0x1000U) == 0);
	CHECK(fb_window_meets_fixed(0xc0000000U, 0x30000000U) == 0);
	CHECK(fb_window_meets_fixed(0xc0000000U, 0x30000001U) == 1);
}

/* 6: after the window is removed, the same request takes the ordinary path. */
static void
scene_after(
	void)
{
	const hal_physaddr_t physical = 0xc0000000ULL;
	const size_t size = 0x300000U;
	void *address;
	int error;

	setup(physical, size);
	kernel_putc = test_kernel_putc;
	error = hal_space_map_device(physical, size, TEST_UC, &address);
	CHECK(error == HAL_OK && address == (void *)fb_window.address);
	error = hal_space_unmap_device(address, size);
	CHECK(error == HAL_OK && fb_window.state == AMD64_FB_WINDOW_REMOVED);

	/* A dynamic view, through one ordinary shootdown each way. */
	reset_records();
	address = NULL;
	error = hal_space_map_device(physical, size, TEST_UC, &address);
	CHECK(error == HAL_OK);
	CHECK(in_device_window(address));
	CHECK(one_ordinary_shootdown());
	reset_records();
	error = hal_space_unmap_device(address, size);
	CHECK(error == HAL_OK);
	CHECK(one_ordinary_shootdown());
	CHECK(fb_window.state == AMD64_FB_WINDOW_REMOVED);
	CHECK(hal_quiet());
}

/* 7: the global flush toggles PGE when set and reloads CR3 otherwise. */
static void
scene_flush(
	void)
{
	/* PGE set: cleared, then the same value back, and no CR3 reload. */
	reset_records();
	flush_tlb_global();
	CHECK(test_cpu[0].cr4_reads == 1);
	CHECK(test_cpu[0].cr4_writes == 2);
	CHECK(test_cpu[0].cr4_written[0] == (TEST_CR4_CPU0 & ~TEST_PGE));
	CHECK(test_cpu[0].cr4_written[1] == TEST_CR4_CPU0);
	CHECK(test_cpu[0].cr3_reloads == 0);

	/* PGE clear: CR4 untouched, CR3 reloaded. */
	reset_records();
	test_cr4[0] = TEST_CR4_CPU1;
	flush_tlb_global();
	CHECK(test_cpu[0].cr4_reads == 1);
	CHECK(test_cpu[0].cr4_writes == 0);
	CHECK(test_cpu[0].cr3_reloads == 1);
	CHECK(test_cr4[0] == TEST_CR4_CPU1);

	/* The request flush: global goes through CR4, ordinary does not read it. */
	reset_records();
	flush_request_range(HAL_SPACE_SYS, NULL, 0, 1U);
	CHECK(test_cpu[0].cr4_reads == 1 && test_cpu[0].cr4_writes == 2);
	CHECK(test_cpu[0].cr3_reloads == 0);
	reset_records();
	flush_request_range(HAL_SPACE_SYS, NULL, 0, 0U);
	CHECK(test_cpu[0].cr4_reads == 0 && test_cpu[0].cr4_writes == 0);
	CHECK(test_cpu[0].cr3_reloads == 1);
	reset_records();
	flush_request_range(HAL_SPACE_SYS, (void *)0xffffffffc2000000ULL, PAGE_SIZE, 1U);
	CHECK(test_cpu[0].cr4_reads == 1 && test_cpu[0].cr4_writes == 2);
	CHECK(test_cpu[0].cr3_reloads == 0);
}

int
main(
	int argc,
	char **argv)
{
	long error;
	int scene;

	if (argc != 2) {
		fprintf(stderr, "usage: %s SCENE\n", argv[0]);
		return 2;
	}
	scene = atoi(argv[1]);

	/* GS points at CPU 0's record, as the HAL's per-CPU selection does. */
	test_percpu[0].self = &test_percpu[0];
	test_percpu[1].self = &test_percpu[1];
	error = syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)&test_percpu[0]);
	if (error != 0) {
		perror("arch_prctl");
		return 2;
	}
	test_pixels = calloc(TEST_PIXELS, sizeof(*test_pixels));
	if (test_pixels == NULL)
		return 2;

	switch (scene) {
	case 1:
		scene_hold();
		break;
	case 2:
		scene_give();
		break;
	case 3:
		scene_remove();
		break;
	case 4:
		scene_dynamic();
		break;
	case 5:
		scene_fixed();
		break;
	case 6:
		scene_after();
		break;
	case 7:
		scene_flush();
		break;
	default:
		fprintf(stderr, "unknown scene %d\n", scene);
		return 2;
	}
	free(test_pixels);
	if (failures != 0) {
		printf("FAIL scene %d (%d of %d checks)\n", scene, failures, checks);
		return 1;
	}
	printf("PASS scene %d (%d checks)\n", scene, checks);
	return 0;
}
