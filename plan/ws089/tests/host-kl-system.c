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
#include <stdio.h>
#include <stdlib.h>
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

/*
 * The account (ws160-p002): with HOST_ACCOUNT_RESULT=ERRNO in the
 * environment the desktop offers it, and a password change is answered
 * with that errno (0 changed) at the next take_result; the passwords asked
 * are printed as their lengths only.
 */
static int host_account_pending;
static uint32_t host_account_request;

unsigned
kl_system_capabilities(const struct kl_system *system)
{
	(void)system;
	if (getenv("HOST_ACCOUNT_RESULT") != NULL)
		return KL_SYSTEM_HAS_ACCOUNT;
	return 0U;
}

int
kl_system_take_result(struct kl_system *system, uint32_t *request, int *error)
{
	const char *answer;

	(void)system;
	if (!host_account_pending)
		return 0;
	host_account_pending = 0;
	answer = getenv("HOST_ACCOUNT_RESULT");
	*request = host_account_request;
	*error = answer != NULL ? atoi(answer) : ENOTSUP;
	return 1;
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

/* The account (ws160-p002): offered with HOST_ACCOUNT_RESULT, answered at the next take_result. */
int
kl_system_account_set_password(struct kl_system *system, const char *current, const char *fresh, uint32_t *request)
{
	(void)system;
	if (getenv("HOST_ACCOUNT_RESULT") == NULL)
		return ENOTSUP;
	host_account_request++;
	host_account_pending = 1;
	if (request != NULL)
		*request = host_account_request;
	printf("HOSTACCOUNT set-password request=%u current_length=%zu new_length=%zu\n", host_account_request, strlen(current), strlen(fresh));
	return 0;
}

/* The administration of the accounts (ws089-p026): not offered by the stand-in. */
int
kl_system_account_administer(struct kl_system *system, const char *password, const char *operation, uint32_t *request)
{
	(void)system;
	(void)password;
	(void)operation;
	(void)request;
	return ENOTSUP;
}

/* The lock screen's PIN (ws163-p003): not offered by the stand-in. */
int
kl_system_account_set_pin(struct kl_system *system, const char *current, const char *pin, uint32_t *request)
{
	(void)system;
	(void)current;
	(void)pin;
	(void)request;
	return ENOTSUP;
}

/* No refusal's word without the administration. */
int
kl_system_account_refusal(const struct kl_system *system, uint32_t request, char *reason, size_t size)
{
	(void)system;
	(void)request;
	(void)reason;
	(void)size;
	return 0;
}

/* Remote Login (ws089-p025): not offered by the stand-in. */
void
kl_system_sharing_get_state(const struct kl_system *system, struct kl_sharing_state *state)
{
	(void)system;
	memset(state, 0, sizeof(*state));
}

int
kl_system_sharing_set_ssh(struct kl_system *system, unsigned on, uint32_t *request)
{
	(void)system;
	(void)on;
	(void)request;
	return ENOTSUP;
}

int
kl_system_sharing_query(struct kl_system *system, uint32_t *request)
{
	(void)system;
	(void)request;
	return ENOTSUP;
}
