/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of zdesktop's XKB keymap (WS035 p078): the host's libxkbcommon
 * (what toolkits use) compiles the keymap zdesktop sends, the real
 * modifiers have the indices zdesktop's modifier masks assume, and evdev
 * keys give the US characters, with Shift, Caps Lock and Num Lock.  The
 * keymap file zdesktop makes holds the same text.
 *
 *   plan/ws035/tests/p078/run-host.sh
 */

#include "userland/base/zdesktop/keymap.h"

#include <xkbcommon/xkbcommon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* The count of failed checks, for the result line and the exit status. */
static unsigned failures;

static void check(int good, const char *what);
static void check_key(struct xkb_state *state, unsigned evdev, const char *want, const char *what);

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	/* A failure is counted and named. */
	if (!good) {
		failures++;
		printf("KEYMAP %s FAILED\n", what);
		return;
	}

	/* A check that held. */
	printf("KEYMAP %s ok\n", what);
}

/* Checks the keysym an evdev key gives in a state. */
static void
check_key(
	struct xkb_state *state,
	unsigned evdev,
	const char *want,
	const char *what)
{
	char name[64];
	xkb_keysym_t sym;
	int same;

	/* The keysym's name. */
	sym = xkb_state_key_get_one_sym(state, evdev + 8U);
	xkb_keysym_get_name(sym, name, sizeof(name));
	same = strcmp(name, want);
	check(same == 0, what);

	/* What it gave instead. */
	if (same != 0)
		printf("KEYMAP   evdev %u gave %s, want %s\n", evdev, name, want);
}

/* Runs the checks. */
int
main(void)
{
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	const char *text;
	uint32_t size;
	char *mapped;
	int fd;

	/* The keymap compiles. */
	text = zwl_keymap_text();
	context = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
	keymap = xkb_keymap_new_from_string(context, text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	check(keymap != NULL, "compiles");
	if (keymap == NULL)
		return 1;

	/* The real modifiers' indices match zdesktop's masks. */
	check(xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT) == 0, "Shift is 0x1");
	check(xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS) == 1, "Lock is 0x2");
	check(xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL) == 2, "Control is 0x4");
	check(xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT) == 3, "Mod1 is 0x8");
	check(xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM) == 4, "Mod2 is 0x10");
	check(xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO) == 6, "Mod4 is 0x40");

	/* Plain keys. */
	state = xkb_state_new(keymap);
	check_key(state, 30U, "a", "a");
	check_key(state, 2U, "1", "1");
	check_key(state, 28U, "Return", "Return");
	check_key(state, 57U, "space", "space");
	check_key(state, 1U, "Escape", "Escape");
	check_key(state, 103U, "Up", "Up");
	check_key(state, 59U, "F1", "F1");
	check_key(state, 125U, "Super_L", "Super_L");
	check_key(state, 56U, "Alt_L", "Alt_L");
	check_key(state, 71U, "KP_Home", "KP_Home without Num Lock");

	/* With Shift (the depressed mask zdesktop sends). */
	xkb_state_update_mask(state, 0x1U, 0U, 0U, 0U, 0U, 0U);
	check_key(state, 30U, "A", "Shift+a");
	check_key(state, 2U, "exclam", "Shift+1");
	check_key(state, 53U, "question", "Shift+slash");
	check_key(state, 15U, "ISO_Left_Tab", "Shift+Tab");

	/* With Caps Lock locked, and with Num Lock locked. */
	xkb_state_update_mask(state, 0U, 0U, 0x2U, 0U, 0U, 0U);
	check_key(state, 30U, "A", "Caps a");
	check_key(state, 2U, "1", "Caps 1");
	xkb_state_update_mask(state, 0U, 0U, 0x10U, 0U, 0U, 0U);
	check_key(state, 71U, "KP_7", "Num Lock KP7");

	/* Pressing the keys moves the modifiers as zdesktop's masks say. */
	xkb_state_unref(state);
	state = xkb_state_new(keymap);
	(void)xkb_state_update_key(state, 42U + 8U, XKB_KEY_DOWN);
	check(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) == 0x1U, "Shift key sets 0x1");
	(void)xkb_state_update_key(state, 42U + 8U, XKB_KEY_UP);
	(void)xkb_state_update_key(state, 29U + 8U, XKB_KEY_DOWN);
	check(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) == 0x4U, "Control key sets 0x4");
	(void)xkb_state_update_key(state, 29U + 8U, XKB_KEY_UP);
	(void)xkb_state_update_key(state, 56U + 8U, XKB_KEY_DOWN);
	check(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) == 0x8U, "Alt key sets 0x8");
	(void)xkb_state_update_key(state, 56U + 8U, XKB_KEY_UP);
	(void)xkb_state_update_key(state, 125U + 8U, XKB_KEY_DOWN);
	check(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) == 0x40U, "Super key sets 0x40");
	(void)xkb_state_update_key(state, 125U + 8U, XKB_KEY_UP);

	/* The keymap file holds the text, NUL and all. */
	check(zwl_keymap_open() == 0, "file made");
	fd = zwl_keymap_descriptor(&size);
	check(fd >= 0 && size == strlen(text) + 1U, "descriptor and size");
	mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	check(mapped != MAP_FAILED && memcmp(mapped, text, size) == 0, "file holds the text");

	/* The result. */
	printf("KEYMAP DONE failures=%u\n", failures);
	xkb_state_unref(state);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	/* Some check failed. */
	if (failures != 0)
		return 1;

	/* Succeeded: every check held. */
	return 0;
}
