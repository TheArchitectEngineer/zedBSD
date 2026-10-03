/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rtld dlopen check of BUG-083 (rtld-dlopen.sh runs it in the guest).
 * The program needs /usr/lib/libws074probe.so (rtld-probe.c) as DT_NEEDED.
 * It checks that:
 *  1. dlopen of a bare name finds a package library in /usr/lib
 *     (libcrypto.so of the OpenSSL package) and dlsym finds its functions;
 *  2. dlopen of the DT_NEEDED library from /usr/lib gives the copy already
 *     loaded (the same function address and the same counter);
 *  3. an absolute path outside /lib is still refused, and a name that is
 *     nowhere fails.
 * Each check prints "pass NAME" or "FAIL NAME"; the last line is
 * "rtld-dlopen N/M", and the exit status is 0 when all pass.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int probe_bump(void);
int probe_count(void);

static int check(const char *name, int ok, int *passed, int *total);

/* Runs the checks. */
int
main(void)
{
	int (*count)(void);
	void *crypto;
	void *probe;
	void *absolute;
	void *missing;
	void *version;
	void *address;
	int passed;
	int total;
	int ok;

	/* 1. A package library by its bare name. */
	passed = 0;
	total = 0;
	crypto = dlopen("libcrypto.so", RTLD_NOW);
	check("usr-lib-open", crypto != NULL, &passed, &total);
	version = NULL;
	if (crypto != NULL)
		version = dlsym(crypto, "OpenSSL_version_num");
	check("usr-lib-symbol", version != NULL, &passed, &total);

	/* 2. The DT_NEEDED library from /usr/lib: the copy already loaded. */
	probe_bump();
	probe_bump();
	probe = dlopen("libws074probe.so", RTLD_NOW);
	check("needed-open", probe != NULL, &passed, &total);
	address = NULL;
	if (probe != NULL)
		address = dlsym(probe, "probe_count");
	ok = address != NULL && address == (void *)probe_count;
	check("needed-same-function", ok, &passed, &total);
	ok = 0;
	if (address != NULL) {
		memcpy(&count, &address, sizeof(count));
		ok = count() == 2;
	}

	/* The counter the program's calls moved is the one dlsym reads. */
	check("needed-same-state", ok, &passed, &total);

	/* 3. An absolute path outside /lib, and a name that is nowhere. */
	absolute = dlopen("/usr/lib/libcrypto.so", RTLD_NOW);
	check("absolute-refused", absolute == NULL, &passed, &total);
	missing = dlopen("libws074-none.so", RTLD_NOW);
	check("missing-refused", missing == NULL, &passed, &total);

	/* The summary. */
	printf("rtld-dlopen %d/%d\n", passed, total);
	if (passed != total)
		return 1;
	return 0;
}

/* Prints one check's result and counts it. */
static int
check(
	const char *name,
	int ok,
	int *passed,
	int *total)
{
	/* The line, then the counts. */
	if (ok)
		printf("pass %s\n", name);
	else
		printf("FAIL %s: %s\n", name, dlerror());
	(*total)++;
	if (ok)
		(*passed)++;
	return ok;
}
