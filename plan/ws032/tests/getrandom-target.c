/* WS032: getrandom(2) on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/random.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("RND %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

int main(void)
{
	unsigned char small[32], large[1000], again[1000];
	ssize_t n;
	size_t zeros, i;

	memset(small, 0, sizeof(small));
	n = getrandom(small, sizeof(small), 0);
	check("short draw fills the whole buffer", n == (ssize_t)sizeof(small));

	/* Larger than one getentropy draw, so the chunk loop is exercised. */
	memset(large, 0, sizeof(large));
	n = getrandom(large, sizeof(large), 0);
	check("draw past the 256-byte limit", n == (ssize_t)sizeof(large));

	zeros = 0;
	for (i = 0; i < sizeof(large); i++)
		if (large[i] == 0)
			zeros++;
	check("bytes are not left zeroed", zeros < sizeof(large) / 16);

	n = getrandom(again, sizeof(again), 0);
	check("a second draw differs from the first",
	      n == (ssize_t)sizeof(again) && memcmp(large, again, sizeof(again)) != 0);

	check("empty draw succeeds", getrandom(small, 0, 0) == 0);
	check("GRND_NONBLOCK never has to report a wait",
	      getrandom(small, sizeof(small), GRND_NONBLOCK) == (ssize_t)sizeof(small));
	check("GRND_RANDOM is accepted",
	      getrandom(small, sizeof(small), GRND_RANDOM) == (ssize_t)sizeof(small));
	check("GRND_INSECURE is accepted",
	      getrandom(small, sizeof(small), GRND_INSECURE) == (ssize_t)sizeof(small));

	errno = 0;
	check("an undefined flag is refused",
	      getrandom(small, sizeof(small), 0x40) == -1 && errno == EINVAL);
	errno = 0;
	check("RANDOM with INSECURE is refused",
	      getrandom(small, sizeof(small), GRND_RANDOM | GRND_INSECURE) == -1 &&
	      errno == EINVAL);

	printf("RND verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
