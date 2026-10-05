/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The session where no session manager speaks to the compositor (Linux and
 * FreeBSD; libkeiland-backend since ws131-p006).
 *
 * A native system session takes the display at once, has no login screen
 * and no lock through a manager, and ends with Log Out through the
 * compositor's ordinary shutdown: every request answers ENOTSUP and the
 * tick has nothing to read.
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"

#include <errno.h>
#include <stddef.h>

/*
 * Has no hand-over to wait for.
 */
int
kl_backend_session_ready(
	struct kl_backend *backend)
{
	/* The display may be taken at once. */
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Has no manager to ask for a Log Out.
 */
int
kl_backend_session_logout(
	struct kl_backend *backend)
{
	/* The compositor ends by itself. */
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Has no login screen to log in from.
 */
int
kl_backend_session_authenticate(
	struct kl_backend *backend,
	const char *user,
	unsigned style,
	const char *secret)
{
	/* Nothing is sent, and nothing is kept. */
	(void)user;
	(void)style;
	(void)secret;
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Has no manager to unlock through.
 */
int
kl_backend_session_unlock(
	struct kl_backend *backend,
	unsigned style,
	const char *secret)
{
	/* Nothing is sent, and nothing is kept. */
	(void)style;
	(void)secret;
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Has no manager to ask for the styles.
 */
int
kl_backend_session_styles(
	struct kl_backend *backend,
	const char *user)
{
	/* Nothing is asked. */
	(void)user;
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Gives the password alone.
 */
unsigned
kl_backend_session_styles_get(
	const struct kl_backend *backend)
{
	/* No other style without a manager. */
	(void)backend;
	return KL_BACKEND_STYLE_PASSWORD;
}

/*
 * Has no manager to set a PIN through.
 */
int
kl_backend_session_set_pin(
	struct kl_backend *backend,
	const char *password,
	const char *pin)
{
	/* Nothing is sent, and nothing is kept. */
	(void)password;
	(void)pin;
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Has no manager to ask what is enrolled.
 */
int
kl_backend_session_enrolled(
	struct kl_backend *backend)
{
	/* Nothing is asked. */
	if (backend == NULL)
		return EINVAL;
	return ENOTSUP;
}

/*
 * Gives no PIN and no key.
 */
void
kl_backend_session_enrolled_get(
	const struct kl_backend *backend,
	unsigned *pin,
	unsigned *keys)
{
	/* Nothing is enrolled without a manager. */
	(void)backend;
	*pin = 0U;
	*keys = 0U;
}

/*
 * Has no refusal to tell.
 */
const char *
kl_backend_session_reason(
	const struct kl_backend *backend)
{
	/* No manager refused anything. */
	(void)backend;
	return "";
}

/*
 * Tells that no manager started the session.
 */
int
kl_backend_session_managed(
	const struct kl_backend *backend)
{
	/* Neither lock nor Log Out goes through a manager. */
	(void)backend;
	return 0;
}

/*
 * Has nothing to read.
 */
void
kl_backend_session_tick(
	struct kl_backend *backend,
	uint64_t now_ms)
{
	/* No manager sends anything. */
	(void)backend;
	(void)now_ms;
}
