/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor's language (language.c, WS158): the catalogs of its text
 * (libkeiland's kl_tr_*, domain "wayland") in the session's language, the
 * setting ui.language, or before a login in the system's language, the
 * one line of KEILAND_SYSCONFDIR/keiland/language ("en", "ja"); and the
 * dates of the system bar and the login screen in it.
 */

#ifndef ZWL_LANGUAGE_H
#define ZWL_LANGUAGE_H

#include <stddef.h>
#include <time.h>

struct zwl_server;

/* The domain of the compositor's own text. */
#define ZWL_LANGUAGE_DOMAIN	"wayland"

/* The system's language before a login (the login screen's), set by an administrator. */
#define ZWL_LANGUAGE_SYSTEM_PATH	KEILAND_SYSCONFDIR "/keiland/language"

/* The two forms of a date: the system bar's short one, and the login screen's long one. */
#define ZWL_LANGUAGE_DATE_SHORT	0
#define ZWL_LANGUAGE_DATE_LONG	1

/*
 * Reads the system's language for the login screen (English when the
 * file is not there).
 */
void zwl_language_system(struct zwl_server *server);

/*
 * Takes the session's language from the setting ui.language (0 English,
 * 1 Japanese): the catalogs are read and the screen drawn again.
 */
void zwl_language_set(struct zwl_server *server, int setting);

/*
 * Writes a date in the language: the short form "Mon Oct 5  14:05" with
 * the time, or the long "Monday, October 5" without it.
 */
void zwl_language_date(const struct tm *local, int form, char *out, size_t size);

#endif
