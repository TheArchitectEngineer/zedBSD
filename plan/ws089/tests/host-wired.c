/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p022: the host test of the Ethernet page's editor
 * (userland/desktop/settings/wired.c compiled unchanged; the drawing is
 * settings-render's).
 *   - The check says which field is wrong, and passes good ones.
 *   - Apply sends the configuration asked (a static one's fields; DHCP's
 *     DNS servers only), waits for the answer, and a good answer closes the
 *     editor; a refusal keeps it with a message.
 *   - Typing takes digits and dots only, up to fifteen characters.
 * Prints one line a check and "host-wired: PASS" or FAIL.
 *
 *   sh plan/ws089/tests/run-host-wired.sh
 */

#include "settings.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int failures;
static struct kl_network_wired_config sent;
static int sends;
static int send_error;

static void check(const char *what, int passed);
static void set(struct se_wired *wired, int index, const char *text);
static int field_of(struct se_wired *wired);

/* The network's request, recorded (network.c in the program). */
int
se_network_configure_wired(
	struct se_app *app,
	const struct kl_network_wired_config *config)
{
	(void)app;
	sent = *config;
	sends++;
	return send_error;
}

/* The log, unused. */
void
se_log(
	const char *format,
	...)
{
	(void)format;
}

/* A field's key: the evdev codes of 1 to 0, the dot, Backspace and A, as the widgets type them. */
int
se_field_key(
	struct kl_field *field,
	const struct se_event *event)
{
	char typed;

	/* Backspace. */
	if (event->key == 14U) {
		if (field->length > 0U)
			field->text[--field->length] = '\0';
		return 1;
	}

	/* The digits, the dot and A. */
	typed = '\0';
	if (event->key >= 2U && event->key <= 10U)
		typed = (char)('1' + (event->key - 2U));
	if (event->key == 11U)
		typed = '0';
	if (event->key == 52U)
		typed = '.';
	if (event->key == 30U)
		typed = 'a';
	if (typed == '\0' || field->length + 1U >= sizeof(field->text))
		return typed != '\0';
	field->text[field->length++] = typed;
	field->text[field->length] = '\0';
	return 1;
}

/* The character a key types (libkeiland's kl_key_character for the keys the test presses; wired.c drops what is no digit or dot, ws090-p007). */
uint32_t
kl_key_character(
	uint32_t code,
	unsigned modifiers)
{
	(void)modifiers;
	if (code >= 2U && code <= 10U)
		return (uint32_t)('1' + (code - 2U));
	if (code == 11U)
		return '0';
	if (code == 52U)
		return '.';
	if (code == 30U)
		return 'a';
	return 0U;
}

/* Empties a field. */
void
se_field_clear(
	struct kl_field *field)
{
	memset(field, 0, sizeof(*field));
}

/* Prints one check's verdict. */
static void
check(
	const char *what,
	int passed)
{
	/* A pass. */
	if (passed) {
		printf("%s: ok\n", what);
		return;
	}

	/* A failure counted. */
	printf("%s: FAIL\n", what);
	failures++;
}

/* Puts a text into a field. */
static void
set(
	struct se_wired *wired,
	int index,
	const char *text)
{
	/* The text. */
	(void)snprintf(wired->fields[index].text, sizeof(wired->fields[index].text), "%s", text);
	wired->fields[index].length = strlen(wired->fields[index].text);
}

/* Gives the field the check says is wrong (0 none, else the field plus one). */
static int
field_of(
	struct se_wired *wired)
{
	char message[SE_MESSAGE];

	/* Checked. */
	return se_wired_check(wired, message, sizeof(message));
}

/*
 * Runs the checks.
 */
int
main(void)
{
	static struct se_app app;
	struct kl_network_link link;
	struct se_event event;
	struct se_wired *wired;
	int index;

	/* The editor opened on a DHCP em0 with its address and router now, and the DNS servers. */
	memset(&link, 0, sizeof(link));
	(void)snprintf(link.name, sizeof(link.name), "%s", "em0");
	(void)snprintf(link.address, sizeof(link.address), "%s", "10.0.2.15");
	(void)snprintf(link.netmask, sizeof(link.netmask), "%s", "255.255.255.0");
	(void)snprintf(link.router, sizeof(link.router), "%s", "10.0.2.2");
	link.wired_mode = KL_WIRED_DHCP;
	app.network.live = 1;
	(void)snprintf(app.network.dns[0], sizeof(app.network.dns[0]), "%s", "10.0.2.3");
	app.network.dns_count = 1;
	se_wired_edit(&app, &link);
	wired = &app.wired;
	check("opened on em0 with DHCP", strcmp(wired->interface, "em0") == 0 && wired->mode == KL_WIRED_DHCP && wired->focus == SE_WIRED_DNS1);
	check("fields start with what it has", strcmp(wired->fields[SE_WIRED_ADDRESS].text, "10.0.2.15") == 0 && strcmp(wired->fields[SE_WIRED_ROUTER].text, "10.0.2.2") == 0 &&
	    strcmp(wired->fields[SE_WIRED_DNS1].text, "10.0.2.3") == 0 && wired->fields[SE_WIRED_DNS2].length == 0U);

	/* The check: DHCP looks at the DNS servers only. */
	set(wired, SE_WIRED_ADDRESS, "garbage");
	check("dhcp ignores the address", field_of(wired) == 0);
	set(wired, SE_WIRED_DNS2, "1.1.1");
	check("dhcp checks the second server", field_of(wired) == SE_WIRED_DNS2 + 1);
	set(wired, SE_WIRED_DNS2, "1.1.1.1");
	set(wired, SE_WIRED_DNS1, "");
	check("the second without the first", field_of(wired) == SE_WIRED_DNS1 + 1);
	set(wired, SE_WIRED_DNS1, "10.0.2.3");

	/* A static address: each field in turn. */
	wired->mode = KL_WIRED_STATIC;
	check("static bad address", field_of(wired) == SE_WIRED_ADDRESS + 1);
	set(wired, SE_WIRED_ADDRESS, "192.168.7.20");
	set(wired, SE_WIRED_NETMASK, "255.0.255.0");
	check("static gap in the mask", field_of(wired) == SE_WIRED_NETMASK + 1);
	set(wired, SE_WIRED_NETMASK, "255.255.255.0");
	check("static router outside", field_of(wired) == SE_WIRED_ROUTER + 1);
	set(wired, SE_WIRED_ROUTER, "192.168.7.1");
	check("static good", field_of(wired) == 0);
	set(wired, SE_WIRED_ADDRESS, "192.168.7.255");
	check("static broadcast address", field_of(wired) == SE_WIRED_ADDRESS + 1);
	set(wired, SE_WIRED_ADDRESS, "192.168.7.20");
	set(wired, SE_WIRED_ROUTER, "");
	check("static without router", field_of(wired) == 0);
	set(wired, SE_WIRED_ROUTER, "192.168.7.1");

	/* A refused Apply keeps the editor and says why. */
	send_error = 0;
	se_wired_apply(&app);
	check("apply sends the static configuration", sends == 1 && sent.mode == KL_WIRED_STATIC && strcmp(sent.interface, "em0") == 0 &&
	    strcmp(sent.address, "192.168.7.20") == 0 && strcmp(sent.netmask, "255.255.255.0") == 0 && strcmp(sent.router, "192.168.7.1") == 0 &&
	    strcmp(sent.dns[0], "10.0.2.3") == 0 && strcmp(sent.dns[1], "1.1.1.1") == 0);
	check("apply waits", wired->asked == 1);
	se_wired_apply(&app);
	check("no second apply while waiting", sends == 1);
	se_wired_outcome(&app, EPERM);
	check("a refusal keeps the editor", wired->interface[0] != '\0' && wired->asked == 0 && wired->message_bad == 1 &&
	    strstr(wired->message, "network group") != NULL);

	/* A good answer closes it. */
	se_wired_apply(&app);
	se_wired_outcome(&app, 0);
	check("a good answer closes the editor", sends == 2 && wired->interface[0] == '\0' && wired->message_bad == 0 && strstr(wired->message, "em0") != NULL);

	/* DHCP sends the DNS servers only. */
	se_wired_edit(&app, &link);
	set(wired, SE_WIRED_DNS1, "");
	se_wired_apply(&app);
	check("dhcp sends no address", sends == 3 && sent.mode == KL_WIRED_DHCP && sent.address[0] == '\0' && sent.router[0] == '\0' && sent.dns[0][0] == '\0');
	se_wired_outcome(&app, 0);

	/* A bad field is not sent; its field takes the keyboard. */
	se_wired_edit(&app, &link);
	wired->mode = KL_WIRED_STATIC;
	set(wired, SE_WIRED_NETMASK, "255.255.255.255");
	wired->focus = SE_WIRED_DNS1;
	se_wired_apply(&app);
	check("a bad field is not sent", sends == 3 && wired->focus == SE_WIRED_NETMASK && wired->message_bad == 1);

	/* Typing: digits and dots, nothing else, no more than fifteen. */
	wired->focus = SE_WIRED_DNS2;
	set(wired, SE_WIRED_DNS2, "");
	memset(&event, 0, sizeof(event));
	event.key = 30U;
	(void)se_wired_type(wired, &event);
	check("a letter is not typed", wired->fields[SE_WIRED_DNS2].length == 0U);
	for (index = 0; index < 20; index++) {
		event.key = 2U;
		if (index % 4 == 3)
			event.key = 52U;
		(void)se_wired_type(wired, &event);
	}

	/* Fifteen, and Backspace takes one away. */
	check("no more than fifteen", wired->fields[SE_WIRED_DNS2].length == 15U);
	event.key = 14U;
	(void)se_wired_type(wired, &event);
	check("backspace", wired->fields[SE_WIRED_DNS2].length == 14U);

	/* The verdict. */
	if (failures != 0) {
		printf("host-wired: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-wired: PASS\n");
	return 0;
}
