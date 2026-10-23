/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The armv7 HAL's checks of itself at boot.
 *
 * Each check prints one PASS line, or a FAIL line with the number of the
 * step that failed and stops the boot: the run-time ABI's helpers with the
 * registers they return in, the exclusive load and store on the memory the
 * kernel uses, and the user address spaces with the access and dirty
 * state the HAL records through faults.
 */

#include <hal/hal.h>
#include "asm.h"
#include "defs.h"
#include "percpu.h"
#include "selftest.h"
#include "space.h"

/* The additions the exclusive check makes, and the retries one may take. */
#define SELFTEST_ATOMIC_ROUNDS		10000U
#define SELFTEST_ATOMIC_RETRY_LIMIT	16U

/* The user address the paging check maps in both of its spaces. */
#define SELFTEST_USER_ADDRESS		0x10000000U

/* The words the paging check's two pages start with, and the word it stores. */
#define SELFTEST_PATTERN_A		0x0a0a5a5aU
#define SELFTEST_PATTERN_B		0x0b0b5b5bU
#define SELFTEST_PATTERN_STORED		0x13572468U
#define SELFTEST_PATTERN_REFUSED	0xdeadbeefU

/* The bytes the memory helpers are checked on. */
#define SELFTEST_BYTES			16U

/*
 * The run-time ABI's helpers, by the prototypes the calling convention
 * gives their registers: a division with remainder returns the quotient
 * in r0 and the remainder in r1 as one 64-bit value, and the 64-bit ones
 * are called through the shims of aeabi.S, which store r0 to r3.
 */
uint32_t __aeabi_uidiv(uint32_t numerator, uint32_t denominator);
uint64_t __aeabi_uidivmod(uint32_t numerator, uint32_t denominator);
int32_t __aeabi_idiv(int32_t numerator, int32_t denominator);
uint64_t __aeabi_idivmod(int32_t numerator, int32_t denominator);
void __aeabi_memcpy(void *destination, const void *source, size_t count);
void __aeabi_memmove(void *destination, const void *source, size_t count);
void __aeabi_memset(void *destination, size_t count, int value);
void __aeabi_memclr(void *destination, size_t count);
void armv7_aeabi_call_uldivmod(uint64_t numerator, uint64_t denominator, uint32_t *registers);
void armv7_aeabi_call_ldivmod(int64_t numerator, int64_t denominator, uint32_t *registers);

/*
 * The word the exclusive check adds to, and the 64-bit one.  Used once by
 * the boot processor.
 */
static volatile uint32_t atomic_word;
static uint64_t atomic_doubleword;

static void aeabi_fail(unsigned step);
static int check_unsigned_division(volatile uint32_t numerator, volatile uint32_t denominator, uint32_t quotient, uint32_t remainder);
static int check_signed_division(volatile int32_t numerator, volatile int32_t denominator, int32_t quotient, int32_t remainder);
static int check_long_division(uint64_t numerator, uint64_t denominator, uint64_t quotient, uint64_t remainder, int is_signed);
static int check_memory_helpers(void);
static uint32_t exclusive_add_once(volatile uint32_t *word);
static void paging_fail(unsigned step);
static uint32_t user_read(uintptr_t address);
static void user_write(uintptr_t address, uint32_t value);
static int store_is_refused(uintptr_t address, uint32_t value);
static int page_state(hal_space_t space, uintptr_t address, uint32_t *flags);

/*
 * Checks the run-time ABI's helpers through the registers they return in,
 * and the memory helpers' argument orders.
 */
void
armv7_aeabi_selftest(
	void)
{
	int passed;

	/* The 32-bit unsigned divisions: an ordinary one, all ones by one, and a remainder in the high half. */
	passed = check_unsigned_division(100U, 7U, 14U, 2U);
	if (!passed)
		aeabi_fail(1);
	passed = check_unsigned_division(0xffffffffU, 1U, 0xffffffffU, 0U);
	if (!passed)
		aeabi_fail(2);
	passed = check_unsigned_division(0xffffffffU, 0x10000U, 0xffffU, 0xffffU);
	if (!passed)
		aeabi_fail(3);

	/* The 32-bit signed divisions round toward zero, with the dividend's sign on the remainder. */
	passed = check_signed_division(-7, 2, -3, -1);
	if (!passed)
		aeabi_fail(4);
	passed = check_signed_division(7, -2, -3, 1);
	if (!passed)
		aeabi_fail(5);
	passed = check_signed_division((int32_t)0x80000000U, -1, (int32_t)0x80000000U, 0);
	if (!passed)
		aeabi_fail(6);

	/* The 64-bit divisions, quotient in r0:r1 and remainder in r2:r3. */
	passed = check_long_division(0x8000000000000005ULL, 0x100000000ULL, 0x80000000ULL, 5ULL, 0);
	if (!passed)
		aeabi_fail(7);
	passed = check_long_division(0xffffffffffffffffULL, 3ULL, 0x5555555555555555ULL, 0ULL, 0);
	if (!passed)
		aeabi_fail(8);
	passed = check_long_division((uint64_t)-1000000000007LL, 1000000ULL, (uint64_t)-1000000LL, (uint64_t)-7LL, 1);
	if (!passed)
		aeabi_fail(9);

	/* The memory helpers, memset's count before its value among them. */
	passed = check_memory_helpers();
	if (!passed)
		aeabi_fail(10);

	hal_puts("ARMV7 AEABI PASS\n");
}

/*
 * Checks the exclusive load and store: a word of normal shareable memory
 * is added to many times, each addition succeeding within a few retries,
 * and a 64-bit atomic addition, which the compiler makes of the
 * doubleword exclusives, gives the right sum.
 */
void
armv7_atomic_selftest(
	void)
{
	uint32_t round;
	uint32_t retries;
	uint32_t most_retries;
	uint32_t failed;
	uint64_t sum;

	/* Adds one SELFTEST_ATOMIC_ROUNDS times, counting each addition's retries. */
	atomic_word = 0;
	most_retries = 0;
	for (round = 0; round < SELFTEST_ATOMIC_ROUNDS; round++) {
		retries = 0;
		failed = exclusive_add_once(&atomic_word);
		while (failed != 0 && retries < SELFTEST_ATOMIC_RETRY_LIMIT) {
			retries++;
			failed = exclusive_add_once(&atomic_word);
		}
		if (failed != 0) {
			hal_printf("ARMV7 ATOMIC FAIL 1 round=%u\n", round);
			HAL_FATAL("armv7 exclusive store never succeeds");
		}
		if (retries > most_retries)
			most_retries = retries;
	}

	/* Every addition landed once. */
	if (atomic_word != SELFTEST_ATOMIC_ROUNDS) {
		hal_printf("ARMV7 ATOMIC FAIL 2 word=%u\n", atomic_word);
		HAL_FATAL("armv7 exclusive additions lost");
	}

	/* A 64-bit addition carries into the high word. */
	atomic_doubleword = 0xffffffffULL;
	(void)__atomic_fetch_add(&atomic_doubleword, 1ULL, __ATOMIC_SEQ_CST);
	sum = __atomic_load_n(&atomic_doubleword, __ATOMIC_SEQ_CST);
	if (sum != 0x100000000ULL) {
		hal_printf("ARMV7 ATOMIC FAIL 3 sum=%llx\n", sum);
		HAL_FATAL("armv7 64-bit atomic addition wrong");
	}

	hal_printf("ARMV7 ATOMIC PASS retries=%u\n", most_retries);
}

/*
 * Checks the user address spaces from the kernel: a page's first access
 * and first store are recorded by the faults they take, a store to a page
 * made read-only is the kernel's fault, and two spaces map the same
 * address to different memory under their own identifiers.
 */
void
armv7_paging_selftest(
	void)
{
	hal_physaddr_t page_a;
	hal_physaddr_t page_b;
	hal_space_t space_a;
	hal_space_t space_b;
	volatile uint32_t *direct_a;
	volatile uint32_t *direct_b;
	uint32_t flags;
	uint32_t value;
	uint32_t asid_a;
	uint32_t asid_b;
	int error;
	int refused;

	/* Two pages, each with its own pattern. */
	error = hal_pmem_alloc(ARMV7_PAGE_SIZE, ARMV7_PAGE_SIZE, &page_a);
	if (error != HAL_OK)
		paging_fail(1);
	error = hal_pmem_alloc(ARMV7_PAGE_SIZE, ARMV7_PAGE_SIZE, &page_b);
	if (error != HAL_OK)
		paging_fail(1);
	direct_a = hal_pmem_to_kernel(page_a);
	direct_b = hal_pmem_to_kernel(page_b);
	direct_a[0] = SELFTEST_PATTERN_A;
	direct_b[0] = SELFTEST_PATTERN_B;

	/* Two spaces, each with its page at the same user address. */
	space_a = hal_space_create();
	space_b = hal_space_create();
	if (space_a == NULL || space_b == NULL)
		paging_fail(2);
	error = hal_space_map(space_a, (void *)SELFTEST_USER_ADDRESS, page_a, ARMV7_PAGE_SIZE, HAL_SPACE_READ | HAL_SPACE_WRITE);
	if (error != HAL_OK)
		paging_fail(2);
	error = hal_space_map(space_b, (void *)SELFTEST_USER_ADDRESS, page_b, ARMV7_PAGE_SIZE, HAL_SPACE_READ | HAL_SPACE_WRITE);
	if (error != HAL_OK)
		paging_fail(2);

	/* A page just mapped is neither accessed nor dirty. */
	error = page_state(space_a, SELFTEST_USER_ADDRESS, &flags);
	if (error != HAL_OK || flags != HAL_SPACE_PAGE_PRESENT)
		paging_fail(3);

	/* The first read takes the access flag fault and reads the page. */
	hal_space_switch(space_a);
	value = user_read(SELFTEST_USER_ADDRESS);
	if (value != SELFTEST_PATTERN_A)
		paging_fail(4);
	error = page_state(space_a, SELFTEST_USER_ADDRESS, &flags);
	if (error != HAL_OK || flags != (HAL_SPACE_PAGE_PRESENT | HAL_SPACE_PAGE_ACCESSED))
		paging_fail(4);

	/* The first store takes the permission fault, and reaches RAM. */
	user_write(SELFTEST_USER_ADDRESS, SELFTEST_PATTERN_STORED);
	error = page_state(space_a, SELFTEST_USER_ADDRESS, &flags);
	if (error != HAL_OK || (flags & HAL_SPACE_PAGE_DIRTY) == 0)
		paging_fail(5);
	if (direct_a[0] != SELFTEST_PATTERN_STORED)
		paging_fail(5);

	/* Taking the write away reports the page dirty, and the next store is the kernel's fault. */
	error = hal_space_prot_query(space_a, (void *)SELFTEST_USER_ADDRESS, ARMV7_PAGE_SIZE, HAL_SPACE_READ, &flags);
	if (error != HAL_OK || (flags & HAL_SPACE_PAGE_DIRTY) == 0)
		paging_fail(6);
	refused = store_is_refused(SELFTEST_USER_ADDRESS, SELFTEST_PATTERN_REFUSED);
	if (!refused)
		paging_fail(7);
	if (direct_a[0] != SELFTEST_PATTERN_STORED)
		paging_fail(7);

	/* The other space's identifier and memory at the same address, then the first's again. */
	hal_space_switch(space_b);
	value = user_read(SELFTEST_USER_ADDRESS);
	if (value != SELFTEST_PATTERN_B)
		paging_fail(8);
	asid_a = armv7_space_asid(space_a);
	asid_b = armv7_space_asid(space_b);
	if (asid_a == 0 || asid_b == 0 || asid_a == asid_b)
		paging_fail(8);
	hal_space_switch(space_a);
	value = user_read(SELFTEST_USER_ADDRESS);
	if (value != SELFTEST_PATTERN_STORED)
		paging_fail(8);

	/* Leaves the spaces and gives everything back. */
	hal_space_switch(HAL_SPACE_SYS);
	error = hal_space_unmap(space_a, (void *)SELFTEST_USER_ADDRESS, ARMV7_PAGE_SIZE);
	if (error != HAL_OK)
		paging_fail(9);
	error = hal_space_unmap(space_b, (void *)SELFTEST_USER_ADDRESS, ARMV7_PAGE_SIZE);
	if (error != HAL_OK)
		paging_fail(9);
	hal_space_destroy(space_a);
	hal_space_destroy(space_b);
	(void)hal_pmem_free(&page_a, ARMV7_PAGE_SIZE);
	(void)hal_pmem_free(&page_b, ARMV7_PAGE_SIZE);

	hal_printf("ARMV7 PAGING PASS asid=%u,%u\n", asid_a, asid_b);
}

/* Reports a failed step of the run-time ABI check and stops. */
static void
aeabi_fail(
	unsigned step)
{
	/* The step, then the stop. */
	hal_printf("ARMV7 AEABI FAIL %u\n", step);
	HAL_FATAL("armv7 run-time ABI helper wrong");
}

/* Checks one 32-bit unsigned division in both of its forms. */
static int
check_unsigned_division(
	volatile uint32_t numerator,
	volatile uint32_t denominator,
	uint32_t quotient,
	uint32_t remainder)
{
	uint64_t both;
	uint32_t alone;

	/* The quotient alone. */
	alone = __aeabi_uidiv(numerator, denominator);
	if (alone != quotient)
		return 0;

	/* The quotient in r0 and the remainder in r1. */
	both = __aeabi_uidivmod(numerator, denominator);
	if ((uint32_t)both != quotient)
		return 0;
	if ((uint32_t)(both >> 32) != remainder)
		return 0;

	/* Succeeded: both forms are right. */
	return 1;
}

/* Checks one 32-bit signed division in both of its forms. */
static int
check_signed_division(
	volatile int32_t numerator,
	volatile int32_t denominator,
	int32_t quotient,
	int32_t remainder)
{
	uint64_t both;
	int32_t alone;

	/* The quotient alone. */
	alone = __aeabi_idiv(numerator, denominator);
	if (alone != quotient)
		return 0;

	/* The quotient in r0 and the remainder in r1. */
	both = __aeabi_idivmod(numerator, denominator);
	if ((int32_t)(uint32_t)both != quotient)
		return 0;
	if ((int32_t)(uint32_t)(both >> 32) != remainder)
		return 0;

	/* Succeeded: both forms are right. */
	return 1;
}

/* Checks one 64-bit division through the registers it returns in. */
static int
check_long_division(
	uint64_t numerator,
	uint64_t denominator,
	uint64_t quotient,
	uint64_t remainder,
	int is_signed)
{
	uint32_t registers[4];

	/* r0:r1 and r2:r3 as the helper left them. */
	if (is_signed) {
		armv7_aeabi_call_ldivmod((int64_t)numerator, (int64_t)denominator, registers);
	} else {
		armv7_aeabi_call_uldivmod(numerator, denominator, registers);
	}

	/* The quotient in r0:r1. */
	if (registers[0] != (uint32_t)quotient || registers[1] != (uint32_t)(quotient >> 32))
		return 0;

	/* The remainder in r2:r3. */
	if (registers[2] != (uint32_t)remainder || registers[3] != (uint32_t)(remainder >> 32))
		return 0;

	/* Succeeded: both results in their registers. */
	return 1;
}

/*
 * Checks the memory helpers: a copy, an overlapping move, a fill whose
 * count comes before its value, and a clear, each leaving the next byte
 * alone.
 */
static int
check_memory_helpers(
	void)
{
	uint8_t source[SELFTEST_BYTES];
	uint8_t buffer[SELFTEST_BYTES];
	unsigned index;

	/* A known pattern to copy. */
	for (index = 0; index < SELFTEST_BYTES; index++) {
		source[index] = (uint8_t)(index + 1U);
		buffer[index] = 0;
	}

	/* A copy of eight bytes. */
	__aeabi_memcpy(buffer, source, 8U);
	if (buffer[0] != 1U || buffer[7] != 8U || buffer[8] != 0U)
		return 0;

	/* A move of the copy two bytes up, over itself. */
	__aeabi_memmove(buffer + 2, buffer, 8U);
	if (buffer[2] != 1U || buffer[9] != 8U || buffer[10] != 0U)
		return 0;

	/* A fill of four bytes: the count, then the value. */
	__aeabi_memset(buffer, 4U, 0x5a);
	if (buffer[0] != 0x5aU || buffer[3] != 0x5aU || buffer[4] != 3U)
		return 0;

	/* A clear of three bytes. */
	__aeabi_memclr(buffer, 3U);
	if (buffer[0] != 0U || buffer[2] != 0U || buffer[3] != 0x5aU)
		return 0;

	/* Succeeded: every helper takes its arguments in its own order. */
	return 1;
}

/*
 * Adds one to a word with one exclusive load and store, and reports the
 * store's status: zero when it succeeded.
 */
static uint32_t
exclusive_add_once(
	volatile uint32_t *word)
{
	uint32_t value;
	uint32_t failed;

	/* The load, the addition and the store that succeeds only if nothing came between. */
	__asm__ volatile("ldrex %0, [%2]\n\t"
			 "add %0, %0, #1\n\t"
			 "strex %1, %0, [%2]"
			 : "=&r"(value), "=&r"(failed)
			 : "r"(word)
			 : "memory");

	/* Succeeded: the store's status. */
	return failed;
}

/* Reports a failed step of the paging check and stops. */
static void
paging_fail(
	unsigned step)
{
	/* The step, then the stop. */
	hal_printf("ARMV7 PAGING FAIL %u\n", step);
	HAL_FATAL("armv7 address space check failed");
}

/* Reads a word at a user address of the selected space. */
static uint32_t
user_read(
	uintptr_t address)
{
	uint32_t value;

	/* One load the fault handling may run again. */
	value = *(volatile uint32_t *)address;

	/* Succeeded: the word. */
	return value;
}

/* Writes a word at a user address of the selected space. */
static void
user_write(
	uintptr_t address,
	uint32_t value)
{
	/* One store the fault handling may run again. */
	*(volatile uint32_t *)address = value;
}

/*
 * Stores a word at a user address of the selected space, expecting the
 * kernel's fault, and reports whether it came: a permission fault of a
 * store, which the abort handling records and steps over.
 */
static int
store_is_refused(
	uintptr_t address,
	uint32_t value)
{
	struct armv7_percpu *cpu;
	unsigned status;

	/* Arms the abort handling for this address alone. */
	cpu = armv7_percpu_current();
	cpu->selftest_fault_taken = 0;
	cpu->selftest_fault_status = 0;
	cpu->selftest_fault_address = address;
	cpu->selftest_fault_armed = 1;

	/* One store instruction, which the abort handling steps over. */
	__asm__ volatile("str %0, [%1]" : : "r"(value), "r"(address) : "memory");

	/* Disarms the handling whether or not the fault came. */
	cpu->selftest_fault_armed = 0;
	if (!cpu->selftest_fault_taken)
		return 0;

	/* The fault must be a permission fault of a store on a page. */
	status = (cpu->selftest_fault_status & 0xfU) | ((cpu->selftest_fault_status >> 6) & 0x10U);
	if (status != ARMV7_FS_PERMISSION_PAGE)
		return 0;
	if ((cpu->selftest_fault_status & ARMV7_DFSR_WNR) == 0)
		return 0;

	/* Succeeded: the store was refused as the kernel's fault. */
	return 1;
}

/* Reads a user page's recorded state. */
static int
page_state(
	hal_space_t space,
	uintptr_t address,
	uint32_t *flags)
{
	int error;

	/* The space's record of the page. */
	error = hal_space_query(space, (void *)address, flags);
	if (error != HAL_OK)
		return error;

	/* Succeeded: the flags are filled. */
	return HAL_OK;
}
