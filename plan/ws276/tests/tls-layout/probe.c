/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Thread-local variables of mixed alignment for the m68k linker to place.
 */

__thread int probe_word = 5;
__thread char probe_bytes[3] = { 1, 2, 3 };
__thread long long probe_aligned __attribute__((aligned(16))) = 7;
__thread int probe_zero;

int probe_read_word(void);
long long probe_read_aligned(void);
int probe_read_bytes(void);
int probe_read_zero(void);
void *__m68k_read_tp(void);
void _start(void);

/* Reads the initialized word. */
int
probe_read_word(
	void)
{
	return probe_word;
}

/* Reads the 16-byte-aligned double word. */
long long
probe_read_aligned(
	void)
{
	return probe_aligned;
}

/* Reads the last initialized byte. */
int
probe_read_bytes(
	void)
{
	return probe_bytes[2];
}

/* Reads the zero-filled word. */
int
probe_read_zero(
	void)
{
	return probe_zero;
}

/* Stands in for the C library's thread pointer; never run. */
void *
__m68k_read_tp(
	void)
{
	return 0;
}

/* Gives the linker an entry; never run. */
void
_start(
	void)
{
	for (;;)
		continue;
}
