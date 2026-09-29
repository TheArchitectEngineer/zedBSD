/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-051 probe: prints where the dynamic loader placed every loaded
 * object.  Linked with the same needed libraries, in the same order, as
 * sshd-auth and sshd-session (libutil, libcrypto, libc), it shows which
 * library a user fault address of those programs falls in.
 */

#include <link.h>
#include <stdio.h>

static int print_object(struct dl_phdr_info *information, size_t size, void *context);

/*
 * Prints the load address and extent of every loaded object.
 */
int
main(
	void)
{
	/* Walks the loaded objects in load order. */
	(void)dl_iterate_phdr(print_object, NULL);

	/* Succeeded: every object was printed. */
	return 0;
}

/* Prints one loaded object's base and the range its segments cover. */
static int
print_object(
	struct dl_phdr_info *information,
	size_t size,
	void *context)
{
	unsigned long low, high, end;
	int i;

	(void)size;
	(void)context;

	/* Finds the lowest and highest address the loadable segments cover. */
	low = ~0UL;
	high = 0;
	for (i = 0; i < information->dlpi_phnum; i++) {
		if (information->dlpi_phdr[i].p_type != PT_LOAD)
			continue;
		if (information->dlpi_phdr[i].p_vaddr < low)
			low = information->dlpi_phdr[i].p_vaddr;
		end = information->dlpi_phdr[i].p_vaddr + information->dlpi_phdr[i].p_memsz;
		if (end > high)
			high = end;
	}

	/* Reports the object with its address range. */
	printf("%#lx-%#lx base %#lx %s\n",
	       (unsigned long)information->dlpi_addr + low,
	       (unsigned long)information->dlpi_addr + high,
	       (unsigned long)information->dlpi_addr,
	       information->dlpi_name);

	/* Continues with the next object. */
	return 0;
}
