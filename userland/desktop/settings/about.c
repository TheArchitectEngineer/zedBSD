/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What About shows of the machine, read once when Settings starts: the
 * kernel and the architecture (uname), the processor's name (the CPU's own
 * brand string), how many processors are online, and the machine's name.
 *
 * These are POSIX interfaces and the processor's instruction, so they are
 * read here directly (plan/ws089/design.md section 6.1).  The graphics
 * device and the screen are the window's to learn (present.c, window.c);
 * the memory needs libkeiland (proposed/libkeiland-system.md) and is not
 * shown yet.
 */

#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static void about_processor(char *name, size_t size);
static void about_trim(char *text);

/*
 * Reads what About shows of the machine; a value that cannot be read is
 * left empty.
 */
void
se_about_read(
	struct se_about *about)
{
	struct utsname names;
	long cores;
	int status;

	/* Nothing known yet. */
	memset(about, 0, sizeof(*about));

	/* The kernel's name and release, and the architecture. */
	status = uname(&names);
	if (status == 0) {
		(void)snprintf(about->kernel, sizeof(about->kernel), "%s %s", names.sysname, names.release);
		(void)snprintf(about->machine, sizeof(about->machine), "%s", names.machine);
	}

	/* The processor's name. */
	about_processor(about->processor, sizeof(about->processor));

	/* The processors online. */
	cores = sysconf(_SC_NPROCESSORS_ONLN);
	if (cores > 0)
		about->cores = (unsigned)cores;

	/* The machine's name. */
	status = gethostname(about->host, sizeof(about->host) - 1U);
	if (status != 0)
		about->host[0] = '\0';
	about->host[sizeof(about->host) - 1U] = '\0';

	/* The log line the tests read. */
	se_log("ABOUT kernel=%s machine=%s cores=%u host=%s", about->kernel, about->machine, about->cores, about->host);
}

/*
 * Reads the processor's brand string with CPUID (leaves 0x80000002 to
 * 0x80000004), on x86 only; elsewhere, or when the processor has none,
 * the name stays empty.
 */
static void
about_processor(
	char *name,
	size_t size)
{
#if defined(__x86_64__) || defined(__i386__)
	uint32_t words[12];
	uint32_t leaf;
	uint32_t highest;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	unsigned index;

	/* Nothing yet. */
	name[0] = '\0';

	/* The highest extended leaf the processor answers. */
	leaf = 0x80000000U;
	__asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(0U));
	highest = eax;
	if (highest < 0x80000004U)
		return;

	/* The three leaves of the brand string, sixteen characters each. */
	for (index = 0; index < 3U; index++) {
		leaf = 0x80000002U + index;
		__asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(0U));
		words[4U * index] = eax;
		words[4U * index + 1U] = ebx;
		words[4U * index + 2U] = ecx;
		words[4U * index + 3U] = edx;
	}

	/* The string, ended within the buffer, without the padding spaces. */
	if (size > sizeof(words))
		size = sizeof(words) + 1U;
	memcpy(name, words, size - 1U);
	name[size - 1U] = '\0';
	about_trim(name);
#else
	/* No brand string on this architecture. */
	(void)size;
	name[0] = '\0';
#endif
}

/* Takes the spaces off a text's start and end, and runs of spaces inside it down to one. */
static void
about_trim(
	char *text)
{
	size_t read;
	size_t written;
	int space;

	/* Copies the text over itself, keeping one space between words. */
	written = 0;
	space = 0;
	for (read = 0; text[read] != '\0'; read++) {
		/* A space is kept only before a word that follows it. */
		if (text[read] == ' ') {
			space = 1;
			continue;
		}

		/* A word after a space gets one space, unless it starts the text. */
		if (space != 0 && written > 0) {
			text[written] = ' ';
			written++;
		}

		/* The word's character. */
		space = 0;
		text[written] = text[read];
		written++;
	}

	/* The text ends after its last word. */
	text[written] = '\0';
}
