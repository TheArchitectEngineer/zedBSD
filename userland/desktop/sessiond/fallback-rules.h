/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * When the graphical login has failed to start and the text console's
 * login takes over (fallback-rules.c, ws239-p001; BUG-122 for the count).
 * Nothing here reads a file or starts a program, so the host tests run it
 * alone.
 */

#ifndef SESSIOND_FALLBACK_RULES_H
#define SESSIOND_FALLBACK_RULES_H

/* How many greeters in a row may fail before the console's login takes over (BUG-122: six, about half a minute). */
#define SESSIOND_GREETER_FAILURES	6U

/* A greeter that ends with an error within this many seconds of its start has failed. */
#define SESSIOND_GREETER_QUICK_SECONDS	20L

/* How long a greeter may take to say READY (to take the display) before it is ended as failed, in seconds. */
#define SESSIOND_GREETER_START_SECONDS	60L

/*
 * How long greeters failing in a row may keep the screen from the console,
 * in milliseconds, counted from the start of the first of them: two that
 * hang without READY (about two minutes) reach it, not six (six minutes).
 */
#define SESSIOND_FAILURE_STREAK_MS	90000LL

int sessiond_greeter_failed(int ready, int status, long seconds);
int sessiond_console_due(unsigned failures, long long streak_ms);

#endif
