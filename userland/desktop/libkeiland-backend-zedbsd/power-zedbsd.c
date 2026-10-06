/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The power on zedBSD (ws131-p005, ws131-p027): the login screen's Shut
 * Down and Restart, and a session's Power Off and Restart (the Power Off
 * dialog, ws099-p037), through sessiond.
 *
 * sessiond takes "POWER poweroff" and "POWER reboot" on the login screen's
 * descriptor (sessiond/greeter.c) and, since ws131-p027, on a session's
 * from root or a member of wheel only (sessiond/session.c, power.c; the
 * 2026-10-06 user decision; plan/ws131/design.md, decision D12 as
 * revised): a session of another user is offered no action.  The request
 * is one short line written whole.  sessiond's answer (OK, FAIL wheel,
 * ERROR) comes back on the same descriptor as its answers to the other
 * requests, and the session (session-zedbsd.c) reads them all; a refusal
 * lets the action be asked again.
 *
 * The power source and the battery are the kernel's KERN_SYSTEM_GET_POWER
 * (ws132-p003), read on a descriptor of /dev/system of their own, so that
 * the state may be read from any thread: AC when the adapter is plugged,
 * the battery when it is not, unknown when the machine has no adapter the
 * kernel knows; the charge in percent of the first battery, -1 without one.
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/system.h>

/* The longest request line. */
#define POWER_LINE_MAX 32U

static unsigned power_actions(const struct kl_backend *backend);
static int power_administrator(void);
static void power_read(struct kl_backend_power_state *state);

/*
 * Copies the power's state: the source and the charge the kernel knows,
 * and the actions the login screen may ask for.
 */
int
kl_backend_power_get_state(
	const struct kl_backend *backend,
	struct kl_backend_power_state *state)
{
	/* A state needs a backend and somewhere to put it. */
	if (backend == NULL || state == NULL)
		return EINVAL;

	/* The source and the charge, as the kernel knows them. */
	power_read(state);

	/* The actions sessiond takes from this compositor. */
	state->actions = power_actions(backend);

	/* Succeeded: the state is filled. */
	return 0;
}

/*
 * Asks sessiond to power the machine off or restart it.
 */
int
kl_backend_power_action(
	struct kl_backend *backend,
	unsigned action)
{
	char line[POWER_LINE_MAX];
	const char *word;
	unsigned offered;
	unsigned bit;
	int error;

	/* An action needs a backend. */
	if (backend == NULL)
		return EINVAL;

	/* Only the actions sessiond takes from this compositor. */
	if (action >= 32U)
		return ENOTSUP;
	offered = power_actions(backend);
	bit = KL_BACKEND_POWER_ACTION_BIT(action);
	if ((offered & bit) == 0U)
		return ENOTSUP;

	/* One action at a time, and none while another request of sessiond's awaits its answer. */
	if (backend->power_asked != 0U || backend->session_request != KL_BACKEND_SESSION_NONE)
		return EBUSY;

	/* The request's word. */
	word = "poweroff";
	if (action == KL_BACKEND_POWER_REBOOT)
		word = "reboot";

	/* The whole line, on the login screen's descriptor or the session's (session-zedbsd.c). */
	(void)snprintf(line, sizeof(line), "POWER %s\n", word);
	error = kl_backend_session_send(backend, KL_BACKEND_SESSION_POWER, line);
	if (error != 0)
		return error;

	/* Succeeded: sessiond ends the machine; its answer comes as session_answer(KL_BACKEND_SESSION_POWER). */
	backend->power_asked = action;
	return 0;
}

/* The actions sessiond takes: power off and restart, from the login screen or a session (none without sessiond). */
static unsigned
power_actions(
	const struct kl_backend *backend)
{
	unsigned actions;
	int administrator;

	/* Without sessiond's descriptor nothing can be asked. */
	if (backend->options.greeter_descriptor < 0 && backend->options.session_descriptor < 0)
		return 0U;

	/* A session's user that is neither root nor in wheel may not (sessiond refuses it too, power.c). */
	if (backend->options.greeter_descriptor < 0) {
		administrator = power_administrator();
		if (!administrator)
			return 0U;
	}

	/* Power off and restart. */
	actions = KL_BACKEND_POWER_ACTION_BIT(KL_BACKEND_POWER_POWEROFF) |
	    KL_BACKEND_POWER_ACTION_BIT(KL_BACKEND_POWER_REBOOT);
	return actions;
}

/*
 * Reads the source and the charge from the kernel; what it does not know
 * (or a kernel without KERN_SYSTEM_GET_POWER) stays unknown.
 */
static void
power_read(
	struct kl_backend_power_state *state)
{
	struct system_power_info info;
	int descriptor;
	int result;

	/* Unknown unless the kernel says. */
	state->source = KL_BACKEND_POWER_SOURCE_UNKNOWN;
	state->percent = -1;
	state->charging = 0U;

	/* The kernel's state, on a descriptor of this call's own. */
	descriptor = open("/dev/system", O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return;
	memset(&info, 0, sizeof(info));
	result = ioctl(descriptor, KERN_SYSTEM_GET_POWER, &info);
	(void)close(descriptor);
	if (result != 0)
		return;

	/* The adapter: plugged or not. */
	if ((info.known & KERN_SYSTEM_POWER_HAS_AC) != 0U) {
		state->source = KL_BACKEND_POWER_SOURCE_BATTERY;
		if (info.ac_online != 0U)
			state->source = KL_BACKEND_POWER_SOURCE_AC;
	}

	/* The battery's charge, and whether it charges. */
	if ((info.known & KERN_SYSTEM_POWER_HAS_BATTERY) != 0U) {
		state->percent = (int)info.battery_percent;
		if (info.battery_charging != 0U)
			state->charging = 1U;
	}
}

/*
 * Tells whether the compositor's user is root or a member of wheel (its
 * primary group, or named in the group), as sessiond decides (the
 * groups file is read each time: a change shows at the next look).
 */
static int
power_administrator(void)
{
	struct passwd *user;
	struct group *wheel;
	unsigned index;
	uid_t uid;
	int same;

	/* Root may. */
	uid = getuid();
	if (uid == 0)
		return 1;

	/* The user and the wheel group, both known. */
	user = getpwuid(uid);
	if (user == NULL)
		return 0;
	wheel = getgrnam("wheel");
	if (wheel == NULL)
		return 0;

	/* Wheel as the primary group. */
	if (wheel->gr_gid == user->pw_gid)
		return 1;

	/* Or a member by name. */
	for (index = 0U; wheel->gr_mem != NULL && wheel->gr_mem[index] != NULL; index++) {
		same = strcmp(wheel->gr_mem[index], user->pw_name);
		if (same == 0)
			return 1;
	}

	/* Not in it. */
	return 0;
}
