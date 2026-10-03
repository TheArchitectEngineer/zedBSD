/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * keiland-settings: a client of libkeiland's settings for the tests of
 * WS135 (ws135-p003).
 *
 *   keiland-settings [--app=NAME] [--timeout-ms=N] COMMAND ...
 *
 * The commands run in order: "get KEY", "dump" (every key), "set KEY
 * VALUE", "reset KEY" (each waits up to the timeout for its result) and
 * "watch PREFIX" (the changes of the keys starting with PREFIX until the
 * timeout).  Each line starts with KEILAND-SETTINGS:
 *
 *   KEILAND-SETTINGS value key=K value=V flags=F | error=E   (E: 0 or the errno's name)
 *   KEILAND-SETTINGS result request=R error=E
 *   KEILAND-SETTINGS change key=K value=V flags=F   (V is - when none)
 *   KEILAND-SETTINGS done status=S
 */

#include <keiland.h>

#include <wayland-client.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The keys "dump" prints, the desktop's in the table's order. */
static const char *const probe_keys[] = {
	"wallpaper",
	"window.opacity",
	"pointer.speed",
	"pointer.natural",
	"keyboard.repeat.rate",
	"keyboard.repeat.delay",
	"sound.volume",
	"sound.muted",
	"sound.available",
	"terminal.ambiguous-wide"
};

static void probe_get(struct kl_settings *settings, const char *key);
static int probe_wait(struct wl_display *display, struct kl_settings *settings, uint32_t request, int timeout_ms);
static int probe_round(struct wl_display *display, struct kl_settings *settings, int timeout_ms);
static void probe_change(void *data, const char *key, const char *value, unsigned flags);
static unsigned long long probe_clock_ms(void);
static const char *probe_error(int error);

int
main(
	int argc,
	char **argv)
{
	struct wl_display *display;
	struct kl_settings *settings;
	const char *app;
	unsigned long long until;
	uint32_t request;
	size_t index;
	int timeout_ms;
	int status;
	int error;
	int arg;

	setvbuf(stdout, NULL, _IOLBF, 0);

	/* The options. */
	app = NULL;
	timeout_ms = 3000;
	arg = 1;
	while (arg < argc && strncmp(argv[arg], "--", 2) == 0) {
		if (strncmp(argv[arg], "--app=", 6) == 0)
			app = argv[arg] + 6;
		else if (strncmp(argv[arg], "--timeout-ms=", 13) == 0)
			timeout_ms = atoi(argv[arg] + 13);
		arg++;
	}

	/* The display and the settings. */
	display = wl_display_connect(NULL);
	if (display == NULL) {
		printf("KEILAND-SETTINGS failed step=connect errno=%d\n", errno);
		return 1;
	}
	settings = kl_settings_open(display, app);
	if (settings == NULL) {
		printf("KEILAND-SETTINGS failed step=open errno=%d\n", errno);
		return 1;
	}
	printf("KEILAND-SETTINGS open app=%s\n", app != NULL ? app : "-");

	/* Each command in turn. */
	status = 0;
	while (arg < argc) {
		if (strcmp(argv[arg], "get") == 0 && arg + 1 < argc) {
			probe_get(settings, argv[arg + 1]);
			arg += 2;
		} else if (strcmp(argv[arg], "dump") == 0) {
			for (index = 0; index < sizeof(probe_keys) / sizeof(probe_keys[0]); index++)
				probe_get(settings, probe_keys[index]);
			arg += 1;
		} else if (strcmp(argv[arg], "set") == 0 && arg + 2 < argc) {
			error = kl_settings_set(settings, argv[arg + 1], argv[arg + 2], &request);
			printf("KEILAND-SETTINGS set key=%s value=%s error=%s\n", argv[arg + 1], argv[arg + 2], probe_error(error));
			if (error == 0)
				status |= probe_wait(display, settings, request, timeout_ms);
			arg += 3;
		} else if (strcmp(argv[arg], "reset") == 0 && arg + 1 < argc) {
			error = kl_settings_reset(settings, argv[arg + 1], &request);
			printf("KEILAND-SETTINGS reset key=%s error=%s\n", argv[arg + 1], probe_error(error));
			if (error == 0)
				status |= probe_wait(display, settings, request, timeout_ms);
			arg += 2;
		} else if (strcmp(argv[arg], "watch") == 0 && arg + 1 < argc) {
			error = kl_settings_watch(settings, argv[arg + 1], probe_change, NULL, NULL);
			printf("KEILAND-SETTINGS watch prefix=%s error=%s\n", argv[arg + 1], probe_error(error));
			until = probe_clock_ms() + (unsigned long long)timeout_ms;
			while (probe_clock_ms() < until) {
				error = probe_round(display, settings, (int)(until - probe_clock_ms()));
				if (error != 0)
					break;
			}
			arg += 2;
		} else {
			printf("KEILAND-SETTINGS failed step=usage word=%s\n", argv[arg]);
			status = 1;
			break;
		}
	}

	/* The end. */
	printf("KEILAND-SETTINGS done status=%d\n", status);
	kl_settings_close(settings);
	wl_display_disconnect(display);
	return status;
}

/* Prints a key's value, or why there is none. */
static void
probe_get(
	struct kl_settings *settings,
	const char *key)
{
	char value[KL_SETTINGS_VALUE_MAX];
	unsigned flags;
	int error;

	/* The value and its flags. */
	flags = 0;
	error = kl_settings_get(settings, key, value, sizeof(value), &flags);
	if (error != 0) {
		printf("KEILAND-SETTINGS value key=%s error=%s\n", key, probe_error(error));
		return;
	}
	printf("KEILAND-SETTINGS value key=%s value=%s flags=%u\n", key, value, flags);
}

/* Waits for a request's result (watching nothing meanwhile); returns 0, or 1 when it does not come. */
static int
probe_wait(
	struct wl_display *display,
	struct kl_settings *settings,
	uint32_t request,
	int timeout_ms)
{
	unsigned long long until;
	uint32_t finished;
	int taken;
	int error;

	/* Rounds until the result. */
	(void)wl_display_flush(display);
	until = probe_clock_ms() + (unsigned long long)timeout_ms;
	for (;;) {
		taken = kl_settings_take_result(settings, &finished, &error);
		if (taken && finished == request) {
			printf("KEILAND-SETTINGS result request=%u error=%s\n", finished, probe_error(error));
			return 0;
		}
		if (taken)
			continue;
		if (probe_clock_ms() >= until)
			break;
		error = probe_round(display, settings, (int)(until - probe_clock_ms()));
		if (error != 0)
			break;
	}

	/* No answer. */
	printf("KEILAND-SETTINGS result request=%u missing\n", request);
	return 1;
}

/* Reads the display's events for up to timeout_ms, then dispatches the settings; returns 0 or -1 when the display went. */
static int
probe_round(
	struct wl_display *display,
	struct kl_settings *settings,
	int timeout_ms)
{
	struct pollfd descriptor;
	int status;

	/* The events read before, then a wait for more. */
	while (wl_display_prepare_read(display) != 0)
		(void)wl_display_dispatch_pending(display);
	(void)wl_display_flush(display);
	descriptor.fd = wl_display_get_fd(display);
	descriptor.events = POLLIN;
	descriptor.revents = 0;
	status = poll(&descriptor, 1, timeout_ms);
	if (status > 0) {
		status = wl_display_read_events(display);
	} else {
		wl_display_cancel_read(display);
	}
	if (status < 0)
		return -1;

	/* The application's queue, then the settings'. */
	(void)wl_display_dispatch_pending(display);
	status = kl_settings_dispatch(settings);
	if (status != 0)
		printf("KEILAND-SETTINGS dispatch error=%d\n", status);
	return 0;
}

/* Prints a change a watch heard. */
static void
probe_change(
	void *data,
	const char *key,
	const char *value,
	unsigned flags)
{
	(void)data;
	printf("KEILAND-SETTINGS change key=%s value=%s flags=%u\n", key, value != NULL ? value : "-", flags);
}

/* A steady clock in milliseconds. */
static unsigned long long
probe_clock_ms(void)
{
	struct timespec now;

	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL;
}

/* Names an errno value the settings give, so that the tests read the same on every system. */
static const char *
probe_error(
	int error)
{
	switch (error) {
	case 0:
		return "0";
	case ENOENT:
		return "ENOENT";
	case EINVAL:
		return "EINVAL";
	case EPERM:
		return "EPERM";
	case ENOTSUP:
		return "ENOTSUP";
	case EBUSY:
		return "EBUSY";
	case ENODEV:
		return "ENODEV";
	case EAGAIN:
		return "EAGAIN";
	case ERANGE:
		return "ERANGE";
	case EIO:
		return "EIO";
	case EPIPE:
		return "EPIPE";
	default:
		break;
	}
	return "other";
}
