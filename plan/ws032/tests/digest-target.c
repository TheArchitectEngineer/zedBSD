/* WS032: the digest interfaces, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <md5.h>
#include <sha1.h>
#include <sha2.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *what, const char *got, const char *want)
{
	int ok = strcmp(got, want) == 0;

	printf("DIG %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok) {
		printf("DIG info got  %s\n", got);
		printf("DIG info want %s\n", want);
		failures++;
	}
}

static void flag(const char *what, int ok)
{
	printf("DIG %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

int main(void)
{
	char out[SHA512_DIGEST_STRING_LENGTH];
	char other[SHA512_DIGEST_STRING_LENGTH];
	static const char alphabet[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	static const char fifty_six[] =
	    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	SHA1_CTX sha1;
	SHA2_CTX sha2;
	MD5_CTX md5;
	size_t i;

	/* The vectors RFC 1321 lists. */
	check("md5 of nothing",
	      MD5Data((const uint8_t *)"", 0, out),
	      "d41d8cd98f00b204e9800998ecf8427e");
	check("md5 of abc",
	      MD5Data((const uint8_t *)"abc", 3, out),
	      "900150983cd24fb0d6963f7d28e17f72");
	check("md5 of a message that fills a block and more",
	      MD5Data((const uint8_t *)alphabet, sizeof(alphabet) - 1, out),
	      "d174ab98d277d9f5a5611c2c9f419d9f");

	/* The vectors FIPS 180-4 lists for SHA-1. */
	check("sha1 of nothing",
	      SHA1Data((const uint8_t *)"", 0, out),
	      "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	check("sha1 of abc",
	      SHA1Data((const uint8_t *)"abc", 3, out),
	      "a9993e364706816aba3e25717850c26c9cd0d89d");
	check("sha1 of a message that spans two blocks",
	      SHA1Data((const uint8_t *)fifty_six, sizeof(fifty_six) - 1, out),
	      "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

	/* A million characters, which is many blocks and a long length. */
	SHA1Init(&sha1);
	for (i = 0; i < 1000U; i++) {
		static uint8_t chunk[1000];

		memset(chunk, 'a', sizeof(chunk));
		SHA1Update(&sha1, chunk, sizeof(chunk));
	}
	check("sha1 of a million characters", SHA1End(&sha1, out),
	      "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

	/* SHA-2. */
	check("sha256 of nothing",
	      SHA256Data((const uint8_t *)"", 0, out),
	      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	check("sha256 of abc",
	      SHA256Data((const uint8_t *)"abc", 3, out),
	      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	check("sha256 of a message that spans two blocks",
	      SHA256Data((const uint8_t *)fifty_six, sizeof(fifty_six) - 1, out),
	      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	check("sha384 of nothing",
	      SHA384Data((const uint8_t *)"", 0, out),
	      "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1"
	      "da274edebfe76f65fbd51ad2f14898b95b");
	check("sha384 of abc",
	      SHA384Data((const uint8_t *)"abc", 3, out),
	      "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5b"
	      "ed8086072ba1e7cc2358baeca134c825a7");

	check("sha512 of nothing",
	      SHA512Data((const uint8_t *)"", 0, out),
	      "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9"
	      "ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927"
	      "da3e");
	check("sha512 of abc",
	      SHA512Data((const uint8_t *)"abc", 3, out),
	      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d3"
	      "9a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54c"
	      "a49f");

	/*
	 * Feeding the same message in uneven pieces must give the same
	 * answer: that is what the part-filled block has to get right.
	 */
	SHA256Init(&sha2);
	for (i = 0; i < sizeof(fifty_six) - 1; i += 7U) {
		size_t piece = sizeof(fifty_six) - 1 - i;

		if (piece > 7U)
			piece = 7U;
		SHA256Update(&sha2, (const uint8_t *)fifty_six + i, piece);
	}
	(void)SHA256End(&sha2, other);
	check("sha256 does not care how the message is divided", other,
	      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	SHA512Init(&sha2);
	for (i = 0; i < sizeof(alphabet) - 1; i += 5U) {
		size_t piece = sizeof(alphabet) - 1 - i;

		if (piece > 5U)
			piece = 5U;
		SHA512Update(&sha2, (const uint8_t *)alphabet + i, piece);
	}
	(void)SHA512End(&sha2, other);
	(void)SHA512Data((const uint8_t *)alphabet, sizeof(alphabet) - 1, out);
	check("sha512 does not care how the message is divided", other, out);

	MD5Init(&md5);
	for (i = 0; i < sizeof(alphabet) - 1; i += 3U) {
		size_t piece = sizeof(alphabet) - 1 - i;

		if (piece > 3U)
			piece = 3U;
		MD5Update(&md5, (const uint8_t *)alphabet + i, piece);
	}
	(void)MD5End(&md5, other);
	check("md5 does not care how the message is divided", other,
	      "d174ab98d277d9f5a5611c2c9f419d9f");

	/* A shorter digest must not be a prefix of the longer one. */
	(void)SHA384Data((const uint8_t *)"abc", 3, out);
	(void)SHA512Data((const uint8_t *)"abc", 3, other);
	flag("sha384 is not a prefix of sha512",
	     strncmp(out, other, SHA384_DIGEST_LENGTH * 2) != 0);

	/* A message exactly one block long exercises the extra pad block. */
	{
		uint8_t block[64];

		memset(block, 'x', sizeof(block));
		(void)SHA256Data(block, sizeof(block), out);
		SHA256Init(&sha2);
		SHA256Update(&sha2, block, 32U);
		SHA256Update(&sha2, block + 32U, 32U);
		(void)SHA256End(&sha2, other);
		flag("a message of exactly one block pads into another",
		     strcmp(out, other) == 0 && strlen(out) == 64U);
	}

	printf("DIG verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
