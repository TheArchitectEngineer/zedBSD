/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws140: the dynamic loader past its old fixed limits.
 *
 * The program is linked with forty libraries (libmany00.so to
 * libmany39.so), more than the sixteen dependencies and close to the
 * thirty-two objects the loader once had room for, and then opens three
 * hubs of forty leaf libraries each, which takes the process past 160
 * objects.  It looks symbols up in the whole process and through a hub's
 * graph of 41 objects, closes and reopens, and then loads the three
 * objects U3 is about: a library with 300 TLSDESC relocations, one whose
 * name is longer than 64 bytes, and one with more than sixteen program
 * headers.  build-many.sh builds the libraries and this program, and
 * rtld-many.sh runs it in the guest with LD_LIBRARY_PATH set to where the
 * libraries are.
 *
 * Each step prints "RTLD-MANY step=<name> ok", or "RTLD-MANY step=<name>
 * FAIL <detail>" and exits with 1.  The last line is "RTLD-MANY: PASS".
 */

#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The libraries the program is linked with, and the leaves of a hub. */
#define MANY_DIRECT 40U
#define MANY_HUBS 3U
#define MANY_LEAVES_PER_HUB 40U

/* The TLS variables of libtlsdesc.so (build-many.sh), each its own TLSDESC. */
#define MANY_TLSDESC 300L

/* The library with a long name, and the value its function returns. */
#define MANY_LONG_NAME \
	"libmany-long-name-past-the-old-sixty-four-byte-limit-of-the-loader-ws140.so"
#define MANY_LONG_VALUE 77

/* The library with extra program headers, and its function's value. */
#define MANY_PHDR_NAME "libphdr.so"
#define MANY_PHDR_VALUE 55

/* What count_callback is asked to count or find. */
struct many_count {
	unsigned objects;
	unsigned phdr_found;
	unsigned phdr_phnum;
	unsigned phdr_dynamic;
};

int many_value_00(void);
int many_value_39(void);

int main(void);
static void step_ok(const char *step);
static void step_fail(const char *step, const char *detail);
static void count_objects(struct many_count *count);
static int count_callback(struct dl_phdr_info *information, size_t size, void *argument);
static int call_symbol(void *handle, const char *name, int *value);
static void check_global(void);
static void check_graph(void **hubs);
static void check_tlsdesc(void);
static void check_long_name(void);
static void check_phdr(const char *step);

/*
 * Runs every step of the test in order.
 */
int
main(
	void)
{
	struct many_count count;
	char hub_name[sizeof("libhub0.so")];
	void *hubs[MANY_HUBS];
	unsigned startup_objects;
	unsigned i;
	int value;
	int found;

	/* Reached main: every dependency was loaded and relocated. */
	step_ok("startup");

	/* Calls the first and the last direct dependency. */
	value = many_value_00();
	if (value != 1)
		step_fail("direct", "many_value_00 is wrong");

	/* The last, past the 32 objects the loader once had room for. */
	value = many_value_39();
	if (value != 40)
		step_fail("direct", "many_value_39 is wrong");

	/* Both answered. */
	step_ok("direct");

	/* Finds every direct dependency's function in the whole process. */
	check_global();
	step_ok("global");

	/* Counts the objects loaded at startup: the program, 40, libc.so and ld.so. */
	count_objects(&count);
	startup_objects = count.objects;
	printf("RTLD-MANY count-startup=%u\n", startup_objects);
	if (startup_objects < MANY_DIRECT + 3U)
		step_fail("count-startup", "too few objects");

	/* Enough objects. */
	step_ok("count-startup");

	/* Opens the three hubs, which load 120 leaves. */
	for (i = 0; i < MANY_HUBS; i++) {
		/* Opens one hub. */
		snprintf(hub_name, sizeof(hub_name), "libhub%u.so", i);
		hubs[i] = dlopen(hub_name, RTLD_NOW);
		if (hubs[i] == NULL)
			step_fail("hubs", dlerror());
	}

	/* Counts the hubs and their leaves beside the startup objects. */
	count_objects(&count);
	printf("RTLD-MANY count-hubs=%u\n", count.objects);
	if (count.objects < startup_objects + MANY_HUBS * (MANY_LEAVES_PER_HUB + 1U))
		step_fail("hubs", "too few objects");

	/* Every hub and leaf is loaded. */
	step_ok("hubs");

	/* Looks symbols up through the hubs' graphs. */
	check_graph(hubs);
	step_ok("graph");

	/* Closes the hubs, which unloads them and their leaves. */
	for (i = 0; i < MANY_HUBS; i++) {
		/* Closes one hub. */
		value = dlclose(hubs[i]);
		if (value != 0)
			step_fail("close", dlerror());
	}

	/* Back to the objects loaded at startup. */
	count_objects(&count);
	if (count.objects != startup_objects)
		step_fail("close", "objects left after the hubs closed");

	/* Everything the hubs brought is gone. */
	step_ok("close");

	/* Opens a hub again, into the slots the others left. */
	hubs[2] = dlopen("libhub2.so", RTLD_NOW);
	if (hubs[2] == NULL)
		step_fail("reopen", dlerror());

	/* Calls a leaf through it. */
	found = call_symbol(hubs[2], "leaf_value_100", &value);
	if (!found || value != 101)
		step_fail("reopen", "leaf_value_100 is missing or wrong");

	/* Closes it. */
	value = dlclose(hubs[2]);
	if (value != 0)
		step_fail("reopen", dlerror());

	/* The slots were used again. */
	step_ok("reopen");

	/* The three objects past the other fixed limits (U3). */
	check_tlsdesc();
	step_ok("tlsdesc");
	check_long_name();
	step_ok("long-name");
	check_phdr("phdr");
	step_ok("phdr");
	check_phdr("phdr-reopen");
	step_ok("phdr-reopen");

	/* Back to the objects loaded at startup once more. */
	count_objects(&count);
	if (count.objects != startup_objects)
		step_fail("end", "objects left at the end");

	/* Succeeded: every step passed. */
	printf("RTLD-MANY: PASS\n");
	return 0;
}

/*
 * Reports that a step passed.
 */
static void
step_ok(
	const char *step)
{
	/* The line rtld-many.sh collects. */
	printf("RTLD-MANY step=%s ok\n", step);
}

/*
 * Reports that a step failed and ends the program.
 */
static void
step_fail(
	const char *step,
	const char *detail)
{
	/* A NULL detail is a dlerror() with nothing to say. */
	if (detail == NULL)
		detail = "(no detail)";

	/* The line rtld-many.sh collects, then the failure exit. */
	printf("RTLD-MANY step=%s FAIL %s\n", step, detail);
	exit(1);
}

/*
 * Counts the loaded objects and finds the one with extra program headers.
 */
static void
count_objects(
	struct many_count *count)
{
	/* Walks every loaded object. */
	memset(count, 0, sizeof(*count));
	dl_iterate_phdr(count_callback, count);
}

/*
 * Counts one object, and records the headers of libphdr.so.
 */
static int
count_callback(
	struct dl_phdr_info *information,
	size_t size,
	void *argument)
{
	struct many_count *count;
	const char *name;
	size_t name_length;
	size_t suffix_length;
	unsigned i;
	int compared;

	/* One more object; the size of the record is not needed. */
	(void)size;
	count = argument;
	count->objects++;

	/* Only libphdr.so is looked at further. */
	name = information->dlpi_name;
	if (name == NULL)
		return 0;

	/* Compares the end of the path with the library's name. */
	name_length = strlen(name);
	suffix_length = strlen(MANY_PHDR_NAME);
	if (name_length < suffix_length)
		return 0;

	/* The path ends with the name. */
	compared = strcmp(name + name_length - suffix_length, MANY_PHDR_NAME);
	if (compared != 0)
		return 0;

	/* Records its count of headers and whether its dynamic one is among them. */
	count->phdr_found = 1;
	count->phdr_phnum = information->dlpi_phnum;
	for (i = 0; i < information->dlpi_phnum; i++) {
		/* The header of its dynamic section. */
		if (information->dlpi_phdr[i].p_type == PT_DYNAMIC)
			count->phdr_dynamic = 1;
	}

	/* Goes on to the next object. */
	return 0;
}

/*
 * Looks a function up through a handle and calls it.  Returns 1 with its
 * value in *value when the symbol is found, 0 otherwise.
 */
static int
call_symbol(
	void *handle,
	const char *name,
	int *value)
{
	int (*function)(void);
	void *symbol;

	/* The function, when the handle's graph has it. */
	symbol = dlsym(handle, name);
	if (symbol == NULL)
		return 0;

	/* Calls it. */
	function = (int (*)(void))symbol;
	*value = function();

	/* Succeeded: the value is in *value. */
	return 1;
}

/*
 * Finds every direct dependency's function through the process's handle.
 */
static void
check_global(
	void)
{
	char name[sizeof("many_value_00")];
	void *process;
	unsigned i;
	int value;
	int found;

	/* The handle of the whole process. */
	process = dlopen(NULL, RTLD_NOW);
	if (process == NULL)
		step_fail("global", dlerror());

	/* Each library's function returns its number plus one. */
	for (i = 0; i < MANY_DIRECT; i++) {
		/* Looks up and calls one. */
		snprintf(name, sizeof(name), "many_value_%02u", i);
		found = call_symbol(process, name, &value);
		if (!found || value != (int)i + 1)
			step_fail("global", name);
	}
}

/*
 * Looks leaves up through the hubs' graphs: found in their own hub only.
 */
static void
check_graph(
	void **hubs)
{
	void *symbol;
	int value;
	int found;

	/* The last leaf of the first hub, the 41st object of its graph. */
	found = call_symbol(hubs[0], "leaf_value_039", &value);
	if (!found || value != 40)
		step_fail("graph", "leaf_value_039 in hub0 is missing or wrong");

	/* The last leaf of the last hub. */
	found = call_symbol(hubs[2], "leaf_value_119", &value);
	if (!found || value != 120)
		step_fail("graph", "leaf_value_119 in hub2 is missing or wrong");

	/* A leaf of another hub is not in the first hub's graph. */
	symbol = dlsym(hubs[0], "leaf_value_119");
	if (symbol != NULL)
		step_fail("graph", "leaf_value_119 found through hub0");
}

/*
 * Opens the library with 300 TLSDESC relocations, reads and writes its
 * variables, and opens it again after closing it to see them fresh.
 */
static void
check_tlsdesc(
	void)
{
	long (*sum)(void);
	void (*add)(void);
	void *library;
	void *symbol;
	long expected;
	long total;
	unsigned round;
	int closed;

	/* The variables start at 1 to 300: their sum. */
	expected = MANY_TLSDESC * (MANY_TLSDESC + 1L) / 2L;

	/* Twice: the second time after the first copy was unloaded. */
	for (round = 0; round < 2U; round++) {
		/* Opens the library, relocating its TLSDESC entries. */
		library = dlopen("libtlsdesc.so", RTLD_NOW);
		if (library == NULL)
			step_fail("tlsdesc", dlerror());

		/* Its two functions. */
		symbol = dlsym(library, "tlsdesc_sum");
		if (symbol == NULL)
			step_fail("tlsdesc", dlerror());

		/* The sum, then the function that adds one to each variable. */
		sum = (long (*)(void))symbol;
		symbol = dlsym(library, "tlsdesc_add");
		if (symbol == NULL)
			step_fail("tlsdesc", dlerror());

		/* Reads the initial values through every descriptor. */
		add = (void (*)(void))symbol;
		total = sum();
		printf("RTLD-MANY tlsdesc round=%u sum=%ld\n", round, total);
		if (total != expected)
			step_fail("tlsdesc", "initial sum is wrong");

		/* Writes each variable through its descriptor and reads it back. */
		add();
		total = sum();
		if (total != expected + MANY_TLSDESC)
			step_fail("tlsdesc", "sum after the writes is wrong");

		/* Unloads it, with its pages of TLSDESC arguments. */
		closed = dlclose(library);
		if (closed != 0)
			step_fail("tlsdesc", dlerror());
	}
}

/*
 * Opens the library whose name is longer than 64 bytes.
 */
static void
check_long_name(
	void)
{
	void *library;
	int value;
	int found;
	int closed;

	/* Opens it by its long name. */
	library = dlopen(MANY_LONG_NAME, RTLD_NOW);
	if (library == NULL)
		step_fail("long-name", dlerror());

	/* Calls its function. */
	found = call_symbol(library, "long_value", &value);
	if (!found || value != MANY_LONG_VALUE)
		step_fail("long-name", "long_value is missing or wrong");

	/* Closes it. */
	closed = dlclose(library);
	if (closed != 0)
		step_fail("long-name", dlerror());
}

/*
 * Opens the library with more than sixteen program headers and sees them
 * through dl_iterate_phdr.  Called twice: the second time it reuses the
 * program table the first copy left.
 */
static void
check_phdr(
	const char *step)
{
	struct many_count count;
	void *library;
	int value;
	int found;
	int closed;

	/* Opens it. */
	library = dlopen(MANY_PHDR_NAME, RTLD_NOW);
	if (library == NULL)
		step_fail(step, dlerror());

	/* Calls its function. */
	found = call_symbol(library, "phdr_value", &value);
	if (!found || value != MANY_PHDR_VALUE)
		step_fail(step, "phdr_value is missing or wrong");

	/* Its headers as dl_iterate_phdr reports them. */
	count_objects(&count);
	printf("RTLD-MANY %s phnum=%u\n", step, count.phdr_phnum);
	if (!count.phdr_found || count.phdr_phnum <= 16U || !count.phdr_dynamic)
		step_fail(step, "program headers are missing or short");

	/* Closes it. */
	closed = dlclose(library);
	if (closed != 0)
		step_fail(step, dlerror());
}
