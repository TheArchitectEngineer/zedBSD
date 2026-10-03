/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * WS131 p011: libkeiland's kl_system_* for Settings' host tests
 * (host-build.sh), without Wayland: no compositor offers the system, so
 * kl_system_open gives none and Settings' sound page says it is not
 * available (host-network.c fills the network pages by hand).  Test code
 * only; the program never has it.
 */

#include <keiland.h>

#include <errno.h>
#include <string.h>

struct kl_system *
kl_system_open(struct wl_display *display)
{
	(void)display;
	errno = ENOTSUP;
	return NULL;
}

void
kl_system_close(struct kl_system *system)
{
	(void)system;
}

int
kl_system_dispatch(struct kl_system *system, unsigned *changed)
{
	(void)system;
	if (changed != NULL)
		*changed = 0U;
	return 0;
}

unsigned
kl_system_capabilities(const struct kl_system *system)
{
	(void)system;
	return 0U;
}

int
kl_system_take_result(struct kl_system *system, uint32_t *request, int *error)
{
	(void)system;
	(void)request;
	(void)error;
	return 0;
}

void
kl_system_audio_get_state(const struct kl_system *system, struct kl_audio_state *state)
{
	(void)system;
	memset(state, 0, sizeof(*state));
}

int
kl_system_audio_set_volume(struct kl_system *system, unsigned left, unsigned right, unsigned muted, uint32_t *request)
{
	(void)system;
	(void)left;
	(void)right;
	(void)muted;
	(void)request;
	return ENOTSUP;
}

int
kl_system_audio_feedback(struct kl_system *system, uint32_t *request)
{
	(void)system;
	(void)request;
	return ENOTSUP;
}
