/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The store of the user's six-digit PIN (pin-store.h; ws163-p002, the mock
 * of plan/ws163/phase001 section 9).
 *
 * ~/.config/keiland/pin is the user's own (mode 0600, the home 0700) and
 * holds two lines:
 *
 *   hash $6$rounds=20000$SALT$...   the PIN's SHA-512 crypt hash
 *   failures N                      the wrong PINs typed in a row
 *
 * The PIN is never written.  A PIN is six digits, a million of them, so the
 * hash keeps nothing from whoever can read the file: the file's mode and
 * the limit of ZWL_PIN_TRIES wrong PINs in a row are the guard, and the
 * count is in the file so that a restart does not reset it.  A new file is
 * written beside the old one, flushed and renamed over it.
 *
 * crypt() is POSIX: zedBSD's libc has it, Linux and FreeBSD link libcrypt,
 * so the compositor keeps the same store on all three.
 */

#include "pin-store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The file's place under the home. */
#define PIN_FILE		".config/keiland/pin"

/* The SHA-512 crypt rounds of a new hash, and the characters of its salt. */
#define PIN_ROUNDS		20000U
#define PIN_SALT_LENGTH		16U

/* The largest file read: two short lines. */
#define PIN_FILE_MAX		512U

/*
 * The 64 characters a crypt salt is made of, one for each six bits of the
 * random bytes.
 */
static const char pin_salt_alphabet[] =
	"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static int pin_parse(const char *text, size_t length, struct zwl_pin_record *record);
static int pin_parse_line(const char *line, size_t length, struct zwl_pin_record *record, unsigned *seen);
static int pin_hash(const char *pin, const char *setting, char *hash, size_t size);
static int pin_salt(char *salt, size_t size);
static int pin_same(const char *left, const char *right);
static void pin_mkdir(const char *path);
static int pin_write_all(int descriptor, const char *text, size_t length);

/*
 * Names the PIN file under a home.  Returns 0, ENOENT for a home that is
 * missing or not absolute, or ENAMETOOLONG.
 */
int
zwl_pin_store_path(
	const char *home,
	char *path,
	size_t size)
{
	int written;

	/* Only an absolute home has a file. */
	if (home == NULL || home[0] != '/')
		return ENOENT;

	/* The file under it. */
	written = snprintf(path, size, "%s/%s", home, PIN_FILE);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the path is named. */
	return 0;
}

/*
 * Tells whether text is a PIN: exactly ZWL_PIN_DIGITS decimal digits.
 * A password is at least eight characters (WS160), so six digits are a
 * PIN's try and never a password (H5).
 */
int
zwl_pin_is_pin(
	const char *text)
{
	size_t index;

	/* Nothing is no PIN. */
	if (text == NULL)
		return 0;

	/* Every one of the digits is a digit. */
	for (index = 0; index < ZWL_PIN_DIGITS; index++) {
		if (text[index] < '0' || text[index] > '9')
			return 0;
	}

	/* And nothing follows them. */
	if (text[ZWL_PIN_DIGITS] != '\0')
		return 0;

	/* It is a PIN. */
	return 1;
}

/*
 * Reads the PIN file.  Returns 0 with the record, ENOENT when there is no
 * PIN, EINVAL for a file that is not a PIN file, or the errno value of the
 * reading.
 */
int
zwl_pin_store_read(
	const char *path,
	struct zwl_pin_record *record)
{
	char text[PIN_FILE_MAX];
	ssize_t count;
	size_t length;
	int descriptor;
	int error;

	/* Opens the file itself, not what a link would point at. */
	memset(record, 0, sizeof(*record));
	descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (descriptor < 0)
		return errno;

	/* Reads the whole file; one that does not fit is not a PIN file. */
	error = 0;
	length = 0;
	for (;;) {
		/* Reads what is left of the room; an interruption is tried again. */
		count = read(descriptor, text + length, sizeof(text) - length);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			error = errno;
			break;
		}

		/* The end of the file. */
		if (count == 0)
			break;

		/* The bytes read; a full room is too much. */
		length += (size_t)count;
		if (length == sizeof(text)) {
			error = EINVAL;
			break;
		}
	}

	/* The file is closed either way. */
	(void)close(descriptor);
	if (error != 0)
		return error;

	/* Takes the lines apart. */
	error = pin_parse(text, length, record);
	if (error != 0) {
		memset(record, 0, sizeof(*record));
		return error;
	}

	/* Succeeded: the record is the file's. */
	return 0;
}

/*
 * Writes the PIN file: its folder made when missing (the user's alone), a
 * new file beside the old one, flushed and renamed over it.  Returns 0 or
 * an errno value; a failure leaves the old file as it was.
 */
int
zwl_pin_store_write(
	const char *path,
	const struct zwl_pin_record *record)
{
	char temporary[ZWL_PIN_PATH_MAX + 8U];
	char text[PIN_FILE_MAX];
	size_t text_length;
	int descriptor;
	int written;
	int status;
	int error;

	/* The two lines. */
	written = snprintf(text, sizeof(text), "hash %s\nfailures %u\n", record->hash, record->failures);
	if (written < 0 || (size_t)written >= sizeof(text))
		return EINVAL;
	text_length = (size_t)written;

	/* The folder, and a new file of the user's alone beside the file (mkstemp makes it 0600). */
	pin_mkdir(path);
	written = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
	if (written < 0 || (size_t)written >= sizeof(temporary))
		return ENAMETOOLONG;
	descriptor = mkstemp(temporary);
	if (descriptor < 0)
		return errno;

	/* Writes the text and flushes it to the disk before it takes the file's name. */
	error = pin_write_all(descriptor, text, text_length);
	if (error == 0) {
		status = fsync(descriptor);
		if (status != 0)
			error = errno;
	}

	/* The file is closed either way; a closing that fails is a failed write. */
	status = close(descriptor);
	if (error == 0 && status != 0)
		error = EIO;

	/* A failed write leaves the old file. */
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

	/* Renames the new file over the file, which replaces it at once. */
	status = rename(temporary, path);
	if (status != 0) {
		error = errno;
		(void)unlink(temporary);
		return error;
	}

	/* Succeeded: the file holds the record. */
	return 0;
}

/*
 * Sets the PIN (a new one, or in place of the old one): its hash with a
 * new salt, and no wrong PINs.  Returns 0, EINVAL for a pin that is not
 * six digits, or an errno value.
 */
int
zwl_pin_store_set(
	const char *path,
	const char *pin)
{
	struct zwl_pin_record record;
	char setting[64];
	char salt[PIN_SALT_LENGTH + 1U];
	int is_pin;
	int error;

	/* Only six digits are a PIN. */
	is_pin = zwl_pin_is_pin(pin);
	if (!is_pin)
		return EINVAL;

	/* A new salt. */
	error = pin_salt(salt, sizeof(salt));
	if (error != 0)
		return error;

	/* The hash, with no wrong PINs. */
	memset(&record, 0, sizeof(record));
	(void)snprintf(setting, sizeof(setting), "$6$rounds=%u$%s$", PIN_ROUNDS, salt);
	error = pin_hash(pin, setting, record.hash, sizeof(record.hash));
	if (error != 0)
		return error;

	/* Writes the file. */
	error = zwl_pin_store_write(path, &record);
	if (error != 0)
		return error;

	/* Succeeded: the PIN is set. */
	return 0;
}

/*
 * Removes the PIN.  Returns 0 (also when there was none) or the errno
 * value of the removal.
 */
int
zwl_pin_store_remove(
	const char *path)
{
	int status;

	/* The file goes; a file already gone is no PIN either. */
	status = unlink(path);
	if (status != 0 && errno != ENOENT)
		return errno;

	/* Succeeded: there is no PIN. */
	return 0;
}

/*
 * Tells whether the PIN can be typed: there is one, and fewer than
 * ZWL_PIN_TRIES wrong ones were typed in a row.
 */
int
zwl_pin_store_usable(
	const char *path)
{
	struct zwl_pin_record record;
	int error;

	/* The file. */
	error = zwl_pin_store_read(path, &record);
	if (error != 0)
		return 0;

	/* Too many wrong PINs turn it off until a password unlocks. */
	if (record.failures >= ZWL_PIN_TRIES)
		return 0;

	/* It can be typed. */
	return 1;
}

/*
 * Checks a typed PIN.  Returns 0 when it is right (the wrong ones in a row
 * are forgotten), EACCES when it is wrong (counted in the file), EPERM when
 * the PIN is off after ZWL_PIN_TRIES wrong ones (nothing is checked),
 * ENOENT when there is no PIN, EINVAL when pin is not six digits, or an
 * errno value.
 */
int
zwl_pin_store_check(
	const char *path,
	const char *pin)
{
	struct zwl_pin_record record;
	char hash[ZWL_PIN_HASH_MAX];
	int is_pin;
	int same;
	int error;

	/* Only six digits are a PIN's try. */
	is_pin = zwl_pin_is_pin(pin);
	if (!is_pin)
		return EINVAL;

	/* The file. */
	error = zwl_pin_store_read(path, &record);
	if (error != 0)
		return error;

	/* A PIN turned off is not tried at all. */
	if (record.failures >= ZWL_PIN_TRIES)
		return EPERM;

	/* The typed PIN's hash with the file's salt. */
	error = pin_hash(pin, record.hash, hash, sizeof(hash));
	if (error != 0)
		return error;

	/* The right PIN: the wrong ones before it are forgotten. */
	same = pin_same(hash, record.hash);
	if (same) {
		if (record.failures != 0U) {
			record.failures = 0U;
			(void)zwl_pin_store_write(path, &record);
		}

		/* Succeeded: the PIN is right. */
		return 0;
	}

	/* A wrong one is counted, so a restart does not give more tries. */
	record.failures++;
	error = zwl_pin_store_write(path, &record);
	if (error != 0)
		return error;

	/* Reports the wrong PIN. */
	return EACCES;
}

/*
 * Forgets the wrong PINs in a row (the password unlocked): a PIN turned
 * off can be typed again.  Returns 0 (also when there is no PIN or nothing
 * to forget) or an errno value.
 */
int
zwl_pin_store_forgive(
	const char *path)
{
	struct zwl_pin_record record;
	int error;

	/* The file; no PIN has nothing to forget. */
	error = zwl_pin_store_read(path, &record);
	if (error != 0)
		return 0;
	if (record.failures == 0U)
		return 0;

	/* Writes the file with none. */
	record.failures = 0U;
	error = zwl_pin_store_write(path, &record);
	if (error != 0)
		return error;

	/* Succeeded: the PIN can be typed again. */
	return 0;
}

/* Takes the file's lines apart into a record; returns 0 or EINVAL. */
static int
pin_parse(
	const char *text,
	size_t length,
	struct zwl_pin_record *record)
{
	const char *line;
	const char *end;
	size_t line_length;
	unsigned seen;
	int error;

	/* Each line, ended by a line end or by the end of the text. */
	seen = 0U;
	line = text;
	while (line < text + length) {
		/* The line's end. */
		end = memchr(line, '\n', (size_t)(text + length - line));
		if (end == NULL)
			end = text + length;
		line_length = (size_t)(end - line);

		/* The line's item. */
		error = pin_parse_line(line, line_length, record, &seen);
		if (error != 0)
			return error;

		/* The next line. */
		line = end + 1;
	}

	/* A file without the hash is not a PIN file. */
	if ((seen & 1U) == 0U)
		return EINVAL;

	/* Succeeded: the record is whole. */
	return 0;
}

/*
 * Takes one line: "hash H" (seen bit 1) or "failures N" (seen bit 2); an
 * empty line is nothing.  Returns 0 or EINVAL.
 */
static int
pin_parse_line(
	const char *line,
	size_t length,
	struct zwl_pin_record *record,
	unsigned *seen)
{
	unsigned long failures;
	char number[16];
	char *after;
	int match;

	/* An empty line. */
	if (length == 0U)
		return 0;

	/* The hash: a SHA-512 crypt string that fits. */
	match = strncmp(line, "hash ", 5U);
	if (match == 0 && length > 5U) {
		if (length - 5U >= sizeof(record->hash))
			return EINVAL;
		memcpy(record->hash, line + 5, length - 5U);
		record->hash[length - 5U] = '\0';
		match = strncmp(record->hash, "$6$", 3U);
		if (match != 0)
			return EINVAL;
		*seen |= 1U;
		return 0;
	}

	/* The wrong PINs in a row: a decimal number. */
	match = strncmp(line, "failures ", 9U);
	if (match == 0 && length > 9U && length - 9U < sizeof(number)) {
		memcpy(number, line + 9, length - 9U);
		number[length - 9U] = '\0';
		errno = 0;
		failures = strtoul(number, &after, 10);
		if (errno != 0 || *after != '\0' || after == number)
			return EINVAL;
		record->failures = ZWL_PIN_TRIES;
		if (failures < ZWL_PIN_TRIES)
			record->failures = (unsigned)failures;
		*seen |= 2U;
		return 0;
	}

	/* Anything else is not a PIN file's. */
	return EINVAL;
}

/* Hashes a PIN with crypt under a setting (a salt, or a whole hash); returns 0, or EIO when crypt cannot. */
static int
pin_hash(
	const char *pin,
	const char *setting,
	char *hash,
	size_t size)
{
	const char *made;
	size_t length;
	int match;

	/* SHA-512 crypt; anything else is a crypt that cannot. */
	made = crypt(pin, setting);
	if (made == NULL)
		return EIO;
	match = strncmp(made, "$6$", 3U);
	if (match != 0)
		return EIO;

	/* The hash, when it fits. */
	length = strlen(made);
	if (length >= size)
		return EIO;
	memcpy(hash, made, length + 1U);

	/* Succeeded: the hash is made. */
	return 0;
}

/* Makes a new crypt salt from the system's random bytes; returns 0 or an errno value. */
static int
pin_salt(
	char *salt,
	size_t size)
{
	unsigned char bytes[PIN_SALT_LENGTH];
	ssize_t count;
	size_t done;
	size_t index;
	int descriptor;
	int error;

	/* The random bytes. */
	descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return errno;

	/* Every one of them; an interruption is tried again. */
	error = 0;
	done = 0;
	while (done < sizeof(bytes)) {
		count = read(descriptor, bytes + done, sizeof(bytes) - done);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0) {
			error = EIO;
			break;
		}

		/* The bytes read. */
		done += (size_t)count;
	}

	/* The device is closed either way. */
	(void)close(descriptor);
	if (error != 0)
		return error;

	/* One salt character for six bits of each byte. */
	if (size < PIN_SALT_LENGTH + 1U)
		return EINVAL;
	for (index = 0; index < PIN_SALT_LENGTH; index++)
		salt[index] = pin_salt_alphabet[bytes[index] & 63U];
	salt[PIN_SALT_LENGTH] = '\0';

	/* Succeeded: the salt is made. */
	return 0;
}

/*
 * Compares two hashes in a time that does not depend on where they first
 * differ.  Returns 1 when they are the same.
 */
static int
pin_same(
	const char *left,
	const char *right)
{
	size_t left_length;
	size_t right_length;
	size_t index;
	unsigned char difference;

	/* Hashes of different lengths differ. */
	left_length = strlen(left);
	right_length = strlen(right);
	if (left_length != right_length)
		return 0;

	/* Every byte goes into the difference. */
	difference = 0U;
	for (index = 0; index < left_length; index++)
		difference |= (unsigned char)(left[index] ^ right[index]);

	/* Any difference is another hash. */
	if (difference != 0U)
		return 0;

	/* No difference at all: the same hash. */
	return 1;
}

/* Makes the folders above the file that are missing (as mkdir -p), for the user alone. */
static void
pin_mkdir(
	const char *path)
{
	char partial[ZWL_PIN_PATH_MAX];
	size_t length;
	size_t index;

	/* A path that does not fit makes nothing (the write then fails). */
	length = strlen(path);
	if (length >= sizeof(partial))
		return;
	memcpy(partial, path, length + 1U);

	/* Makes each folder up to a slash after the first; one that exists is left alone. */
	for (index = 1; index < length; index++) {
		if (partial[index] != '/')
			continue;
		partial[index] = '\0';
		(void)mkdir(partial, 0700);
		partial[index] = '/';
	}
}

/* Writes every byte of a text; returns 0 or an errno value. */
static int
pin_write_all(
	int descriptor,
	const char *text,
	size_t length)
{
	ssize_t count;
	size_t done;

	/* Writes what is left; an interruption is tried again. */
	done = 0;
	while (done < length) {
		count = write(descriptor, text + done, length - done);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}

		/* The bytes written. */
		done += (size_t)count;
	}

	/* Succeeded: the text is written. */
	return 0;
}
