/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Linux build's library contract check (WS131 p011): the version, and
 * the desktop's system reached only through the compositor (kl_system_*),
 * which without a display gives nothing.
 */

#include <keiland.h>

#include <wayland-client.h>

#include <errno.h>
#include <stdio.h>

/*
 * Checks the library version, that the system needs a display, and, when a
 * compositor runs, that it either offers Keiland's system extension or is
 * refused as another desktop.
 */
int
main(
	void)
{
	struct wl_display *display;
	struct kl_system *system;
	unsigned version;
	unsigned capabilities;

	/* The desktop contract of WS131 p011 (22), or a later one that only added to it (23: WS134 p012's monitor). */
	version = keiland_version();
	if (version < 22U)
		return 1;

	/* No system without a display. */
	system = kl_system_open(NULL);
	if (system != NULL || errno != EINVAL)
		return 1;

	/* A compositor that runs: Keiland offers the system, another desktop is refused with ENOTSUP. */
	display = wl_display_connect(NULL);
	if (display != NULL) {
		system = kl_system_open(display);
		if (system == NULL && errno != ENOTSUP) {
			wl_display_disconnect(display);
			return 1;
		}

		/* What Keiland offers, said for the record. */
		if (system != NULL) {
			capabilities = kl_system_capabilities(system);
			printf("lib-smoke: capabilities=0x%x\n", capabilities);
			kl_system_close(system);
		}

		/* The display goes. */
		wl_display_disconnect(display);
	}

	/* Publishes the checked build-time library contract. */
	(void)puts("lib-smoke: PASS");

	/* Succeeded: the library contract is usable. */
	return 0;
}
