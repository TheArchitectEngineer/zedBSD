/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The characters of Text Editor's keys: evdev key codes to the characters
 * they type (Terminal's keys.c).
 *
 * zdesktop forwards evdev codes with no keymap, so the editor carries its
 * own layout: the US one, as Terminal and the console.  Japanese input
 * comes through an input method later (WS095).
 */

#include "textedit.h"

/* How many codes the character tables cover (up to the space bar). */
#define KEYS_TABLE_SIZE	58U

/*
 * The character each key types without shift, by evdev code; 0 for a key
 * that types none.  The US layout.
 */
static const char keys_plain[KEYS_TABLE_SIZE] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
	'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

/* The character each key types with shift, by evdev code. */
static const char keys_shifted[KEYS_TABLE_SIZE] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
	'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

/*
 * Reports the character a key types with the modifiers held, or 0 for a
 * key that types none (a key with Control or Alt types none either).
 */
uint32_t
te_key_character(
	uint32_t key,
	uint32_t modifiers)
{
	char character;

	/* Control and Alt make a key a command, not a character. */
	if ((modifiers & (TE_MOD_CTRL | TE_MOD_ALT | TE_MOD_SUPER)) != 0U)
		return 0U;

	/* Only the keys of the tables type characters. */
	if (key >= KEYS_TABLE_SIZE)
		return 0U;

	/* The character, shifted or not. */
	character = keys_plain[key];
	if ((modifiers & TE_MOD_SHIFT) != 0U)
		character = keys_shifted[key];

	/* Succeeded: the character, or 0. */
	return (uint32_t)(unsigned char)character;
}
