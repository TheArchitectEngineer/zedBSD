/* WS032: the vis and base64 interfaces, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("VIS %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* Every byte must survive being encoded and read back. */
static int round_trips(int flag)
{
	char encoded[1024], decoded[300];
	char raw[256];
	int i, n;

	for (i = 0; i < 256; i++)
		raw[i] = (char)i;
	n = strvisx(encoded, raw, sizeof(raw), flag);
	if (n < 0)
		return 0;
	if ((int)strlen(encoded) != n)
		return 0;
	n = strunvis(decoded, encoded);
	if (n != 256)
		return 0;
	return memcmp(decoded, raw, 256) == 0;
}

int main(void)
{
	char out[256], back[256], *allocated;
	unsigned char bytes[48], decoded[64];
	char text[128];
	int n, i;

	/* A control character must not reach the terminal as itself. */
	check("a control character is encoded as the caret form",
	      strvis(out, "a\033[31mb", 0) == 9 &&
	      strcmp(out, "a\\^[[31mb") == 0 &&
	      strchr(out, '\033') == NULL);

	check("a high byte is encoded as the meta form",
	      strvis(out, "\351\200", 0) == 8 && strcmp(out, "\\M-i\\M^@") == 0);

	check("printable text is left alone",
	      strvis(out, "plain text", 0) == 10 &&
	      strcmp(out, "plain text") == 0);

	check("the backslash doubles",
	      strvis(out, "a\\b", 0) == 4 && strcmp(out, "a\\\\b") == 0);

	/* Tab and newline stand for themselves until they are asked for. */
	check("tab and newline are left alone by default",
	      strvis(out, "\n\t", VIS_CSTYLE) == 2 && strcmp(out, "\n\t") == 0);

	check("VIS_CSTYLE names them once they are asked for",
	      strvis(out, "\n\t", VIS_CSTYLE | VIS_NL | VIS_TAB) == 4 &&
	      strcmp(out, "\\n\\t") == 0);

	check("without VIS_CSTYLE they take the caret form",
	      strvis(out, "\n\t", VIS_NL | VIS_TAB) == 6 &&
	      strcmp(out, "\\^J\\^I") == 0);

	check("VIS_OCTAL always uses three digits",
	      strvis(out, "\n", VIS_OCTAL | VIS_NL) == 4 &&
	      strcmp(out, "\\012") == 0);

	check("VIS_WHITE encodes the spaces",
	      strvis(out, "a b", VIS_WHITE | VIS_CSTYLE) == 4 &&
	      strcmp(out, "a\\sb") == 0);

	check("VIS_SAFE keeps what cannot act on a terminal",
	      strvis(out, "ab\007", VIS_SAFE) == 3 && strcmp(out, "ab\007") == 0);

	check("every byte round-trips, plain", round_trips(0));
	check("every byte round-trips, C style", round_trips(VIS_CSTYLE));
	check("every byte round-trips, octal", round_trips(VIS_OCTAL));

	/* strnvis takes the size third, beside the string it bounds. */
	memset(out, 'x', sizeof(out));
	n = strnvis(out, "\033\033\033\033", 6, 0);
	check("strnvis reports the whole length and stops short",
	      n == 12 && strlen(out) < 6);

	check("stravis allocates what it needs",
	      stravis(&allocated, "a\nb", VIS_CSTYLE | VIS_NL) == 4 &&
	      strcmp(allocated, "a\\nb") == 0);
	free(allocated);

	check("a sequence that is not an encoding is refused",
	      strunvis(back, "a\\qb") == -1);

	/* base64. */
	check("base64 encodes the known vectors",
	      b64_ntop((const unsigned char *)"", 0, text, sizeof(text)) == 0 &&
	      b64_ntop((const unsigned char *)"f", 1, text, sizeof(text)) == 4 &&
	      strcmp(text, "Zg==") == 0);
	(void)b64_ntop((const unsigned char *)"fo", 2, text, sizeof(text));
	check("base64 pads a two-byte group", strcmp(text, "Zm8=") == 0);
	(void)b64_ntop((const unsigned char *)"foobar", 6, text, sizeof(text));
	check("base64 encodes a whole group", strcmp(text, "Zm9vYmFy") == 0);

	check("base64 decodes the known vectors",
	      b64_pton("Zg==", decoded, sizeof(decoded)) == 1 && decoded[0] == 'f');
	check("base64 decodes a whole group",
	      b64_pton("Zm9vYmFy", decoded, sizeof(decoded)) == 6 &&
	      memcmp(decoded, "foobar", 6) == 0);
	check("base64 ignores the whitespace a record is wrapped in",
	      b64_pton("Zm9v\n YmFy", decoded, sizeof(decoded)) == 6 &&
	      memcmp(decoded, "foobar", 6) == 0);

	/* Anything outside the alphabet would let two texts mean one value. */
	check("base64 refuses a character outside the alphabet",
	      b64_pton("Zm9v*mFy", decoded, sizeof(decoded)) == -1);
	check("base64 refuses padding in the middle",
	      b64_pton("Zg==Zg==", decoded, sizeof(decoded)) == -1);
	check("base64 refuses a lone character",
	      b64_pton("Z", decoded, sizeof(decoded)) == -1);
	check("base64 refuses bits that would be lost",
	      b64_pton("Zh==", decoded, sizeof(decoded)) == -1);
	check("base64 refuses a destination that is too small",
	      b64_pton("Zm9vYmFy", decoded, 3) == -1);
	check("base64 refuses to encode without room",
	      b64_ntop((const unsigned char *)"foobar", 6, text, 8) == -1);

	/* Every byte must survive base64 too. */
	for (i = 0; i < 48; i++)
		bytes[i] = (unsigned char)(i * 5 + 3);
	n = b64_ntop(bytes, sizeof(bytes), text, sizeof(text));
	check("base64 round-trips arbitrary bytes",
	      n > 0 && b64_pton(text, decoded, sizeof(decoded)) == 48 &&
	      memcmp(decoded, bytes, 48) == 0);

	printf("VIS verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
