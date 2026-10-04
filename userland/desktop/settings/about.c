/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What About shows of the machine, read once when Settings starts: the
 * system's version (PRETTY_NAME of /etc/os-release, or of
 * /usr/lib/os-release when /etc has none; ws089-p027), the kernel and the
 * architecture (uname), the processor's name (the CPU's own brand
 * string), how many processors are online, and the machine's name.
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
static void about_unquote(char *value);
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

	/* The system's version, from /etc/os-release or the system's own copy. */
	status = se_about_pretty_name("/etc/os-release", about->system, sizeof(about->system));
	if (status != 0)
		(void)se_about_pretty_name("/usr/lib/os-release", about->system, sizeof(about->system));

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
	se_log("ABOUT system=%s kernel=%s machine=%s cores=%u host=%s", about->system, about->kernel, about->machine,
	       about->cores, about->host);
}

/*
 * Reads PRETTY_NAME of an os-release file (os-release(5): KEY=value lines,
 * the value maybe in double or single quotes, with backslash escapes in
 * double quotes, "#" starting a comment).  Returns 0 with the name, cut
 * short to fit, or -1 when the file cannot be read or has no PRETTY_NAME
 * (the name is then empty).
 */
int
se_about_pretty_name(
	const char *path,
	char *name,
	size_t size)
{
	char line[256];
	FILE *file;
	char *read;
	char *value;
	size_t length;
	int same;
	int found;

	/* Nothing yet. */
	name[0] = '\0';

	/* The file. */
	file = fopen(path, "r");
	if (file == NULL)
		return -1;

	/* Each line, until PRETTY_NAME. */
	found = -1;
	for (;;) {
		/* The next line; the file's end ends the search. */
		read = fgets(line, sizeof(line), file);
		if (read == NULL)
			break;

		/* The line without its end. */
		length = strcspn(line, "\r\n");
		line[length] = '\0';

		/* Only PRETTY_NAME= at the line's start counts. */
		same = strncmp(line, "PRETTY_NAME=", 12U);
		if (same != 0)
			continue;

		/* Its value, without the quotes. */
		value = line + 12;
		about_unquote(value);
		(void)snprintf(name, size, "%s", value);
		found = 0;
		break;
	}

	/* The file is no longer needed. */
	(void)fclose(file);

	/* An empty name counts as none. */
	if (found == 0 && name[0] == '\0')
		found = -1;

	/* Succeeded when the name was found. */
	return found;
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

/*
 * Takes the quotes off an os-release value in place: within double quotes
 * a backslash keeps the character after it (\", \\, \$, \`); within
 * single quotes everything is kept; a value without quotes ends at the
 * first space.
 */
static void
about_unquote(
	char *value)
{
	size_t read;
	size_t written;
	char quote;

	/* A value without quotes: up to its first space. */
	quote = value[0];
	if (quote != '"' && quote != '\'') {
		written = strcspn(value, " \t");
		value[written] = '\0';
		return;
	}

	/* Inside the quotes, up to the closing one. */
	written = 0;
	for (read = 1; value[read] != '\0' && value[read] != quote; read++) {
		/* A backslash in double quotes keeps the next character. */
		if (quote == '"' && value[read] == '\\' && value[read + 1U] != '\0')
			read++;

		/* The character. */
		value[written] = value[read];
		written++;
	}

	/* The value ends where it was copied to. */
	value[written] = '\0';
}
