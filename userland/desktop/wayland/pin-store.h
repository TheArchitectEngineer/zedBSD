/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The store of the user's six-digit PIN (ws163-p002, plan/ws163/phase001
 * section 9): a mock that keeps the PIN's hash in the user's own
 * ~/.config/keiland/pin, which the lock screen reads.  No daemon holds or
 * checks it; the later key management replaces it.
 */

#ifndef ZWL_PIN_STORE_H
#define ZWL_PIN_STORE_H

#include <stddef.h>

/* The digits of a PIN, and the wrong PINs in a row that turn it off until a password unlock. */
#define ZWL_PIN_DIGITS		6U
#define ZWL_PIN_TRIES		5U

/* The room of the file's path and of a crypt hash, with their NULs. */
#define ZWL_PIN_PATH_MAX	1024U
#define ZWL_PIN_HASH_MAX	160U

/*
 * What the file holds: the PIN's SHA-512 crypt hash, and the wrong PINs
 * typed in a row since the last right PIN or password.
 */
struct zwl_pin_record {
	char hash[ZWL_PIN_HASH_MAX];
	unsigned failures;
};

int zwl_pin_store_path(const char *home, char *path, size_t size);
int zwl_pin_is_pin(const char *text);
int zwl_pin_store_read(const char *path, struct zwl_pin_record *record);
int zwl_pin_store_write(const char *path, const struct zwl_pin_record *record);
int zwl_pin_store_set(const char *path, const char *pin);
int zwl_pin_store_remove(const char *path);
int zwl_pin_store_usable(const char *path);
int zwl_pin_store_check(const char *path, const char *pin);
int zwl_pin_store_forgive(const char *path);

#endif
