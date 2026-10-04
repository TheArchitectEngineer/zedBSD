/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p012 (C1), WS131 p011: checks Settings' network requests on the
 * host against a pretend kl_system (the compositor's network) that answers
 * each request once.  A switch, a disconnect or a join asked for while
 * Settings' own request is out must wait in the slot and be sent when that
 * one is answered; a later ask replaces what waited; the scans are asked
 * for while a page lists the networks around and take no request
 * (ws089-p021); a join with a new key is one save_key request,
 * sent after the outstanding one with the key typed; a request the
 * compositor answers busy (the system bar's request was out) waits a
 * moment and goes again; a join's failures are said in words.  Built and
 * run by host-slot.sh; the last line says host-slot: PASS or FAIL.
 */

#include "settings.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* How many requests the pretend compositor remembers being sent. */
#define SLOT_SENT_MAX		16

/*
 * The pretend compositor: the request outstanding (SE_NETWORK_NONE when
 * none) and its number, the next number, the requests sent in order
 * with the network named and the key's length, and whether the scans are
 * asked for, with how often that was told.  It lives for the whole test
 * and is reset before each case.
 */
struct slot_daemon {
	unsigned outstanding;
	uint32_t outstanding_id;
	uint32_t next_id;
	unsigned sent[SLOT_SENT_MAX];
	char sent_ssid[SLOT_SENT_MAX][KL_NETWORK_SSID_MAX];
	size_t sent_key_length[SLOT_SENT_MAX];
	unsigned sent_count;
	int scanning;
	unsigned scanning_calls;
};

/* The pretend compositor of the case being run. */
static struct slot_daemon slot_daemon;

/* The system the pretend kl_system gives out (its contents are never read). */
static char slot_system;

/* How many checks failed. */
static int slot_failures;

static void slot_reset(struct se_app *app);
static void slot_finish(struct se_app *app, int error);
static void slot_sent(unsigned request, const char *ssid, const char *key, uint32_t *number);
static void slot_expect_sent(const char *name, unsigned count, const unsigned *requests);
static void slot_expect(const char *name, int condition);

/*
 * Runs the cases and prints each check's verdict and the total.
 */
int
main(void)
{
	static struct se_app app;
	static const unsigned join_then_off[] = { KL_NETWORK_JOIN, KL_NETWORK_WIFI_OFF };
	static const unsigned off_then_join[] = { KL_NETWORK_WIFI_OFF, KL_NETWORK_JOIN };
	static const unsigned disconnect_then_key[] = { KL_NETWORK_DISCONNECT, SE_NETWORK_SAVE_KEY };
	static const unsigned join_then_join[] = { KL_NETWORK_JOIN, KL_NETWORK_JOIN };
	static const unsigned join_then_disconnect[] = { KL_NETWORK_JOIN, KL_NETWORK_DISCONNECT };
	static const unsigned off_twice[] = { KL_NETWORK_WIFI_OFF, KL_NETWORK_WIFI_OFF };

	/* 1. The switch pressed during a join is sent after the join's answer. */
	slot_reset(&app);
	se_network_join(&app, "OSC Venue");
	se_network_wifi(&app, 0);
	slot_expect("1 switch waits", app.network.pending_request == KL_NETWORK_WIFI_OFF);
	slot_expect("1 no busy refusal", app.network.message_bad == 0);
	slot_finish(&app, 0);
	slot_expect_sent("1 join then off", 2U, join_then_off);

	/* 2. A saved network joined while the switch's request is out: Connecting is said at once, the join follows. */
	slot_reset(&app);
	se_network_wifi(&app, 0);
	se_network_join(&app, "Cafe Guest");
	slot_expect("2 joining said", strcmp(app.network.message, "Connecting to Cafe Guest...") == 0);
	slot_finish(&app, 0);
	slot_expect_sent("2 off then join", 2U, off_then_join);
	slot_expect("2 join names the network", strcmp(slot_daemon.sent_ssid[1], "Cafe Guest") == 0);
	slot_finish(&app, 0);
	slot_expect("2 connected", strcmp(app.network.message, "Connected to Cafe Guest.") == 0);

	/*
	 * 3. A new key during a disconnect: one save_key request after it, with
	 * the key typed (case 13: a join of another network answered meanwhile).
	 */
	slot_reset(&app);
	se_network_disconnect(&app);
	(void)snprintf(app.network.key.text, sizeof(app.network.key.text), "%s", "correct horse");
	app.network.key.length = strlen(app.network.key.text);
	(void)snprintf(app.network.key_ssid, sizeof(app.network.key_ssid), "%s", "Neighbor 5G");
	/* The form's own name, as the page passes it (T1-150: it was wiped before it was sent). */
	se_network_join_key(&app, app.network.key_ssid, app.network.key.text);
	slot_expect("3 save-key waits", app.network.pending_request == SE_NETWORK_SAVE_KEY);
	slot_expect("3 joining said", strcmp(app.network.message, "Connecting to Neighbor 5G...") == 0);
	slot_finish(&app, 0);
	slot_expect_sent("3 disconnect then save-key", 2U, disconnect_then_key);
	slot_expect("3 save-key names the network and the key", strcmp(slot_daemon.sent_ssid[1], "Neighbor 5G") == 0 && slot_daemon.sent_key_length[1] == 13U);
	slot_finish(&app, 0);
	slot_expect("3 connected", strcmp(app.network.message, "Connected to Neighbor 5G.") == 0);
	slot_expect("3 key form closed and wiped", app.network.key_ssid[0] == '\0' && app.network.key.length == 0U);

	/* 4. A second join while the first is out: the first's failure is said, then the second goes. */
	slot_reset(&app);
	se_network_join(&app, "OSC Venue");
	se_network_join(&app, "Kei Lab");
	slot_finish(&app, EIO);
	slot_expect_sent("4 join then join", 2U, join_then_join);
	slot_expect("4 second join named", strcmp(slot_daemon.sent_ssid[1], "Kei Lab") == 0);
	slot_expect("4 join under way", strcmp(app.network.join_ssid, "Kei Lab") == 0);
	slot_finish(&app, 0);
	slot_expect("4 connected", strcmp(app.network.message, "Connected to Kei Lab.") == 0);

	/*
	 * 5. ws089-p021: the scans are asked for while a page lists the networks
	 * around, once however often it polls, and no longer on another page,
	 * on Ethernet, or when the window closes; the asking takes no request.
	 */
	slot_reset(&app);
	app.page = SE_PAGE_WIFI;
	se_network_poll(&app, app.now);
	se_network_poll(&app, app.now + 300U);
	slot_expect("5 scanning asked once", slot_daemon.scanning == 1 && slot_daemon.scanning_calls == 1U);
	slot_expect("5 no request for it", slot_daemon.sent_count == 0U && app.network.request == SE_NETWORK_NONE);
	app.page = SE_PAGE_NETWORK;
	se_network_poll(&app, app.now + 600U);
	slot_expect("5 still asked on Network", slot_daemon.scanning == 1 && slot_daemon.scanning_calls == 1U);
	app.page = SE_PAGE_ETHERNET;
	se_network_poll(&app, app.now + 900U);
	slot_expect("5 no longer on Ethernet", slot_daemon.scanning == 0 && slot_daemon.scanning_calls == 2U);
	app.page = SE_PAGE_WIFI;
	se_network_poll(&app, app.now + 1200U);
	app.page = SE_PAGE_HOME;
	se_network_poll(&app, app.now + 1500U);
	slot_expect("5 no longer on Home", slot_daemon.scanning == 0 && slot_daemon.scanning_calls == 4U);
	app.page = SE_PAGE_WIFI;
	se_network_poll(&app, app.now + 1800U);
	se_network_close(&app);
	slot_expect("5 no longer once closed", slot_daemon.scanning == 0 && slot_daemon.scanning_calls == 6U);

	/* 6. Two presses during a join: the later one waits in place of the first. */
	slot_reset(&app);
	se_network_join(&app, "OSC Venue");
	se_network_wifi(&app, 0);
	se_network_disconnect(&app);
	slot_finish(&app, 0);
	slot_expect_sent("6 the later press", 2U, join_then_disconnect);

	/* 7. The compositor busy with the system bar's request: the switch waits a moment and goes again. */
	slot_reset(&app);
	se_network_wifi(&app, 0);
	slot_finish(&app, EBUSY);
	slot_expect("7 waits after busy", app.network.pending_request == KL_NETWORK_WIFI_OFF && slot_daemon.sent_count == 1U);
	slot_expect("7 no message for busy", app.network.message_bad == 0);
	app.now += 600U;
	se_network_poll(&app, app.now);
	slot_expect_sent("7 sent again", 2U, off_twice);

	/* 8. A join's failures in words. */
	slot_reset(&app);
	se_network_join(&app, "Cafe Guest");
	slot_finish(&app, ENOENT);
	slot_expect("8 no saved key", strcmp(app.network.message, "Cafe Guest has no saved key.") == 0);
	se_network_join(&app, "Cafe Guest");
	slot_finish(&app, EACCES);
	slot_expect("8 key refused", strcmp(app.network.message, "Cafe Guest did not accept the key. Check the key and try again.") == 0);
	se_network_join(&app, "Cafe Guest");
	slot_finish(&app, ENETUNREACH);
	slot_expect("8 out of reach", strcmp(app.network.message, "Cafe Guest is not in reach.") == 0);

	/* 9. A short key is refused at once, nothing sent. */
	slot_reset(&app);
	se_network_join_key(&app, "Neighbor 5G", "short");
	slot_expect("9 short key said", strcmp(app.network.message, "The key of Neighbor 5G must be 8 to 63 characters.") == 0);
	slot_expect("9 nothing sent", slot_daemon.sent_count == 0U);

	/* 10. ws089-p021: the asking is renewed every 30 seconds while shown (the compositor drops one a minute old). */
	slot_reset(&app);
	app.page = SE_PAGE_WIFI;
	se_network_poll(&app, app.now);
	app.now += 29000U;
	se_network_poll(&app, app.now);
	slot_expect("10 not renewed before 30 s", slot_daemon.scanning_calls == 1U);
	app.now += 1500U;
	se_network_poll(&app, app.now);
	slot_expect("10 renewed after 30 s", slot_daemon.scanning == 1 && slot_daemon.scanning_calls == 2U);
	app.now += 31000U;
	se_network_poll(&app, app.now);
	slot_expect("10 renewed again", slot_daemon.scanning == 1 && slot_daemon.scanning_calls == 3U);

	/*
	 * 11. BUG-183: the switch shows the position asked at once, keeps it
	 * while the state lags, and shows the state once it agrees; a failed
	 * switch goes back at once; a success whose state never comes goes back
	 * after the hold.
	 */
	slot_reset(&app);
	app.network.state.wifi = KL_WIFI_OFF;
	se_network_wifi(&app, 1);
	slot_expect("11 on at once", se_network_wifi_on(&app.network) == 1);
	slot_finish(&app, 0);
	slot_expect("11 still on while the state lags", se_network_wifi_on(&app.network) == 1 && app.network.wifi_wanted == 2);
	app.network.state.wifi = KL_WIFI_SEARCHING;
	se_network_poll(&app, app.now + 100U);
	slot_expect("11 settled when the state agrees", app.network.wifi_wanted == 0 && se_network_wifi_on(&app.network) == 1);
	app.network.state.wifi = KL_WIFI_OFF;
	se_network_wifi(&app, 1);
	slot_finish(&app, EIO);
	slot_expect("11 a failure puts it back", app.network.wifi_wanted == 0 && se_network_wifi_on(&app.network) == 0);
	se_network_wifi(&app, 1);
	slot_finish(&app, 0);
	app.now += 4100U;
	se_network_poll(&app, app.now);
	slot_expect("11 back to the state after the hold", app.network.wifi_wanted == 0 && se_network_wifi_on(&app.network) == 0);

	/*
	 * 12. BUG-186: confirming a new key closes its form at once (the key goes
	 * to the join); a key refused opens the form again, empty, with the
	 * reason; a busy answer sends the same key again.
	 */
	slot_reset(&app);
	(void)snprintf(app.network.key_ssid, sizeof(app.network.key_ssid), "%s", "Neighbor 5G");
	(void)snprintf(app.network.key.text, sizeof(app.network.key.text), "%s", "wrong horse");
	app.network.key.length = strlen(app.network.key.text);
	se_network_join_key(&app, "Neighbor 5G", app.network.key.text);
	slot_expect("12 form closed at the confirm", app.network.key_ssid[0] == '\0' && app.network.key.length == 0U);
	slot_expect("12 the key went with the join", slot_daemon.sent_count == 1U && slot_daemon.sent_key_length[0] == 11U);
	slot_finish(&app, EBUSY);
	app.now += 600U;
	se_network_poll(&app, app.now);
	slot_expect("12 busy: the same key sent again", slot_daemon.sent_count == 2U && slot_daemon.sent_key_length[1] == 11U);
	slot_finish(&app, EACCES);
	slot_expect("12 refused: the form again, empty", strcmp(app.network.key_ssid, "Neighbor 5G") == 0 && app.network.key.length == 0U);
	slot_expect("12 refused: the reason", strcmp(app.network.message, "Neighbor 5G did not accept the key. Check the key and try again.") == 0);
	slot_expect("12 the key is wiped", app.network.join_key.length == 0U && app.network.join_key.text[0] == '\0');

	/*
	 * 13. BUG-186's addendum: a new key waiting behind a join of another
	 * network keeps its key when that join is answered.
	 */
	slot_reset(&app);
	se_network_join(&app, "Cafe Guest");
	(void)snprintf(app.network.key_ssid, sizeof(app.network.key_ssid), "%s", "Neighbor 5G");
	(void)snprintf(app.network.key.text, sizeof(app.network.key.text), "%s", "correct horse");
	app.network.key.length = strlen(app.network.key.text);
	se_network_join_key(&app, "Neighbor 5G", app.network.key.text);
	slot_finish(&app, 0);
	slot_expect("13 the waiting key is sent whole", slot_daemon.sent_count == 2U && slot_daemon.sent_key_length[1] == 13U);

	/* The verdict. */
	if (slot_failures != 0) {
		printf("host-slot: FAIL (%d)\n", slot_failures);
		return 1;
	}

	/* Succeeded: every case passed. */
	printf("host-slot: PASS\n");
	return 0;
}

/* Starts a case: a fresh pretend compositor and a fresh Settings following it, on Home (no scans asked for). */
static void
slot_reset(
	struct se_app *app)
{
	/* The compositor remembers nothing; numbers start at 1. */
	memset(&slot_daemon, 0, sizeof(slot_daemon));
	slot_daemon.next_id = 1U;

	/* Settings follows it, the Wi-Fi on, nothing shown that scans by itself. */
	memset(app, 0, sizeof(*app));
	app->system = (struct kl_system *)(void *)&slot_system;
	app->page = SE_PAGE_HOME;
	app->now = 1000U;
	se_network_open(app);
	app->network.state.reachable = 1;
	app->network.state.wifi = KL_WIFI_CONNECTED;
}

/* Answers the outstanding request with an errno value, and lets Settings take the answer and poll. */
static void
slot_finish(
	struct se_app *app,
	int error)
{
	uint32_t number;

	/* The answer, taken as system.c takes it. */
	number = slot_daemon.outstanding_id;
	slot_daemon.outstanding = SE_NETWORK_NONE;
	slot_daemon.outstanding_id = 0U;
	app->now += 10U;
	(void)se_network_result(app, number, error);

	/* Settings polls. */
	app->system_changed = 0U;
	se_network_poll(app, app->now);
}

/* Records a request sent to the pretend compositor, and gives it its number. */
static void
slot_sent(
	unsigned request,
	const char *ssid,
	const char *key,
	uint32_t *number)
{
	/* The request is out, under the next number. */
	slot_daemon.outstanding = request;
	slot_daemon.outstanding_id = slot_daemon.next_id;
	slot_daemon.next_id++;
	if (number != NULL)
		*number = slot_daemon.outstanding_id;

	/* Remembered. */
	if (slot_daemon.sent_count >= SLOT_SENT_MAX)
		return;
	slot_daemon.sent[slot_daemon.sent_count] = request;
	slot_daemon.sent_ssid[slot_daemon.sent_count][0] = '\0';
	if (ssid != NULL)
		(void)snprintf(slot_daemon.sent_ssid[slot_daemon.sent_count], KL_NETWORK_SSID_MAX, "%s", ssid);
	slot_daemon.sent_key_length[slot_daemon.sent_count] = 0U;
	if (key != NULL)
		slot_daemon.sent_key_length[slot_daemon.sent_count] = strlen(key);
	slot_daemon.sent_count++;
}

/* Checks the requests sent so far, in order. */
static void
slot_expect_sent(
	const char *name,
	unsigned count,
	const unsigned *requests)
{
	unsigned index;
	int same;

	/* As many as expected, each the one expected. */
	same = 1;
	if (slot_daemon.sent_count != count)
		same = 0;
	for (index = 0; index < count && index < slot_daemon.sent_count; index++) {
		if (slot_daemon.sent[index] != requests[index])
			same = 0;
	}

	/* The verdict, with what was sent when it differs. */
	slot_expect(name, same);
	if (same != 0)
		return;
	for (index = 0; index < slot_daemon.sent_count; index++)
		printf("  sent %u: %u %s\n", index, slot_daemon.sent[index], slot_daemon.sent_ssid[index]);
}

/* Records one check's verdict. */
static void
slot_expect(
	const char *name,
	int condition)
{
	/* A failed check counts. */
	if (condition == 0) {
		printf("%s: FAIL\n", name);
		slot_failures++;
		return;
	}

	/* A passed check. */
	printf("%s: ok\n", name);
}

/* The pretend kl_system: the network offered. */
unsigned
kl_system_capabilities(
	const struct kl_system *system)
{
	(void)system;
	return KL_SYSTEM_HAS_NETWORK;
}

/* The pretend kl_system: the state never changes (the test sets it). */
void
kl_system_network_get_state(
	const struct kl_system *system,
	struct kl_network_state *state)
{
	(void)system;
	memset(state, 0, sizeof(*state));
}

/* The pretend kl_system: no networks around. */
size_t
kl_system_network_get_scan(
	const struct kl_system *system,
	struct kl_network_ap *aps,
	size_t capacity)
{
	(void)system;
	(void)aps;
	(void)capacity;
	return 0;
}

/* The pretend kl_system: a request of the daemon's, sent (the compositor answers busy, not the library). */
int
kl_system_network_request(
	struct kl_system *system,
	unsigned what,
	const char *ssid,
	uint32_t *request)
{
	(void)system;
	slot_sent(what, ssid, NULL, request);
	return 0;
}

/* The pretend kl_system: a key with its network, sent (its length checked as the library does). */
int
kl_system_network_save_key(
	struct kl_system *system,
	const char *ssid,
	const char *key,
	uint32_t *request)
{
	size_t length;

	(void)system;
	length = strlen(key);
	if (length < KL_NETWORK_KEY_MIN || length > KL_NETWORK_KEY_MAX)
		return EINVAL;
	slot_sent(SE_NETWORK_SAVE_KEY, ssid, key, request);
	return 0;
}

/* The pretend kl_system: the scans asked for or no longer (ws089-p021), remembered with how often. */
int
kl_system_network_set_scanning(
	struct kl_system *system,
	unsigned on)
{
	(void)system;
	slot_daemon.scanning = 0;
	if (on != 0U)
		slot_daemon.scanning = 1;
	slot_daemon.scanning_calls++;
	return 0;
}

/* The pretend kl_system: the details are asked for, never answered (they are not this test's). */
int
kl_system_network_query_details(
	struct kl_system *system,
	uint32_t *request)
{
	(void)system;
	*request = 1000000U;
	return 0;
}

/* The pretend kl_system: no interfaces. */
size_t
kl_system_network_get_links(
	const struct kl_system *system,
	struct kl_network_link *links,
	size_t capacity)
{
	(void)system;
	(void)links;
	(void)capacity;
	return 0;
}

/* The pretend kl_system: no DNS servers. */
size_t
kl_system_network_get_dns(
	const struct kl_system *system,
	char (*servers)[KL_NETWORK_ADDRESS_MAX],
	size_t capacity)
{
	(void)system;
	(void)servers;
	(void)capacity;
	return 0;
}

/* The pretend kl_system: no saved networks. */
size_t
kl_system_network_get_saved(
	const struct kl_system *system,
	char (*ssids)[KL_NETWORK_SSID_MAX],
	size_t capacity)
{
	(void)system;
	(void)ssids;
	(void)capacity;
	return 0;
}

/*
 * Settings' text field, wiped.
 */
void
se_field_clear(
	struct se_field *field)
{
	/* The field's bytes. */
	memset(field, 0, sizeof(*field));
}

/*
 * Settings' log, to standard error as the program's.
 */
void
se_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The line. */
	fputs("ZSETTINGS ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}
