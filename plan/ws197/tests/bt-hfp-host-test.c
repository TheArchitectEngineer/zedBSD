/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of bluetoothd's HFP hands-free unit (ws197-p006,
 * plan/ws197/phase006/phase.md): a scripted audio gateway answers on a
 * fake DLC.  The AT lines' reading, the SLC's order, an incoming call
 * (RING, +CLIP, +CIEV, AT+CLCC) answered and hung up, a DTMF key, a dial,
 * an unanswered command, the owner's refusals, an AG without the enhanced
 * call status (the indicators' guess), and the link's end during a call.
 * Run by bt-phone-host-test.sh.
 */

#include "userland/base/bluetoothd/hfp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The DLC the AG opens to the HF channel (DLCI = channel * 2). */
#define TEST_DLCI	(BTD_HFP_HF_CHANNEL * 2U)

/*
 * The fake daemon: the clock, what the HF wrote on the DLC, the SDP
 * queries and DLCs asked for, the answers to tokens and the events, all
 * kept as text.
 */
struct test_world {
	uint64_t now;
	int wanted;
	char written[4096];
	size_t written_length;
	unsigned sdp_queries;
	unsigned opens;
	unsigned closes;
	char answers[4096];
	char emits[8192];
};

/* The world of the test, and the HF under test. */
static struct test_world world;
static struct btd_hfp hfp;

/* The checks made and failed. */
static unsigned checks;
static unsigned failures;

static void check(int condition, const char *what);
static uint64_t hook_clock(void *context);
static int hook_wanted(void *context);
static int hook_sdp(void *context, uint16_t uuid);
static int hook_open(void *context, unsigned channel);
static int hook_write(void *context, unsigned dlci, const uint8_t *data, size_t length, size_t *written);
static void hook_close(void *context, unsigned dlci);
static void hook_answer(void *context, uint64_t token, const char *line, const uint8_t *bytes, size_t length);
static void hook_emit(void *context, const char *line, const uint8_t *bytes, size_t length);
static void start(void);
static void ag(const char *text);
static int took(const char *command);
static int emitted(const char *text);
static int answered(const char *text);
static void slc(const char *brsf, int three_way);
static void test_at(void);
static void test_call(void);
static void test_refusals(void);
static void test_guess(void);
static void test_lost(void);
static void collect(void *context, const char *line);

/* The lines collect saw. */
static char collected[8][64];
static unsigned collected_count;

/*
 * Runs every part and reports the checks.
 */
int
main(
	void)
{
	/* Each part, the output unbuffered so that a crash keeps what came before. */
	setvbuf(stdout, NULL, _IONBF, 0);
	test_at();
	test_call();
	test_refusals();
	test_guess();
	test_lost();

	/* The verdict. */
	printf("bt-hfp-host-test: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;

	/* Succeeded: every check held. */
	return 0;
}

/* Counts a check, and reports one that failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more. */
	checks++;
	if (!condition) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* The fake clock. */
static uint64_t
hook_clock(
	void *context)
{
	(void)context;
	return world.now;
}

/* Whether the owner wants calls. */
static int
hook_wanted(
	void *context)
{
	(void)context;
	return world.wanted;
}

/* An SDP query asked for, counted. */
static int
hook_sdp(
	void *context,
	uint16_t uuid)
{
	(void)context;
	check(uuid == BTD_SDP_UUID_AG, "sdp: the AG's class");
	world.sdp_queries++;
	return 0;
}

/* A DLC asked for, counted. */
static int
hook_open(
	void *context,
	unsigned channel)
{
	(void)context;
	(void)channel;
	world.opens++;
	return 0;
}

/* What the HF writes on the DLC, kept whole. */
static int
hook_write(
	void *context,
	unsigned dlci,
	const uint8_t *data,
	size_t length,
	size_t *written)
{
	/* The fake keeps no context; the DLC is the SLC's. */
	(void)context;
	check(dlci == TEST_DLCI, "write: the SLC's DLC");
	if (world.written_length + length >= sizeof(world.written))
		return ENOSPC;
	memcpy(world.written + world.written_length, data, length);
	world.written_length += length;
	world.written[world.written_length] = '\0';
	*written = length;
	return 0;
}

/* A DLC closed, counted. */
static void
hook_close(
	void *context,
	unsigned dlci)
{
	(void)context;
	(void)dlci;
	world.closes++;
}

/* An answer to a token, kept as "token: line". */
static void
hook_answer(
	void *context,
	uint64_t token,
	const char *line,
	const uint8_t *bytes,
	size_t length)
{
	size_t used;

	/* Kept after the others. */
	(void)context;
	(void)bytes;
	(void)length;
	used = strlen(world.answers);
	(void)snprintf(world.answers + used, sizeof(world.answers) - used, "%llu: %s\n", (unsigned long long)token, line);
}

/* An event's line, kept. */
static void
hook_emit(
	void *context,
	const char *line,
	const uint8_t *bytes,
	size_t length)
{
	size_t used;

	/* Kept after the others. */
	(void)context;
	(void)bytes;
	(void)length;
	used = strlen(world.emits);
	(void)snprintf(world.emits + used, sizeof(world.emits) - used, "%s\n", line);
}

/* A new world and HF, the link ready and calls wanted. */
static void
start(
	void)
{
	struct btd_hfp_hooks hooks;

	/* The world. */
	memset(&world, 0, sizeof(world));
	world.now = 1000U;
	world.wanted = 1;

	/* The HF with the fake hooks. */
	memset(&hooks, 0, sizeof(hooks));
	hooks.clock = hook_clock;
	hooks.wanted = hook_wanted;
	hooks.sdp_query = hook_sdp;
	hooks.dlc_open = hook_open;
	hooks.dlc_write = hook_write;
	hooks.dlc_close = hook_close;
	hooks.answer = hook_answer;
	hooks.emit = hook_emit;
	btd_hfp_init(&hfp, &hooks);
	btd_hfp_ready(&hfp);
}

/* The AG sends text (its own CR LF in it). */
static void
ag(
	const char *text)
{
	btd_hfp_data(&hfp, TEST_DLCI, (const uint8_t *)text, strlen(text));
}

/* Tells whether the HF wrote a command (with its CR) since the last call, and forgets what it wrote. */
static int
took(
	const char *command)
{
	char want[128];
	int same;

	/* Nothing written is asked for by an empty command. */
	want[0] = '\0';
	if (command[0] != '\0')
		(void)snprintf(want, sizeof(want), "%s\r", command);
	same = strcmp(world.written, want);
	if (same != 0)
		printf("  wrote \"%s\", wanted \"%s\"\n", world.written, command);
	world.written[0] = '\0';
	world.written_length = 0U;
	return same == 0;
}

/* Tells whether an event was emitted (a part of its line). */
static int
emitted(
	const char *text)
{
	return strstr(world.emits, text) != NULL;
}

/* Tells whether an answer was given (a part of the answers). */
static int
answered(
	const char *text)
{
	return strstr(world.answers, text) != NULL;
}

/* Makes the SLC on the AG's DLC: +BRSF's answer given, the indicators of a usual phone. */
static void
slc(
	const char *brsf,
	int three_way)
{
	/* The AG's DLC to the HF channel, taken. */
	check(btd_hfp_accept(&hfp, BTD_HFP_HF_CHANNEL) == 1, "slc: the HF channel accepted");
	check(btd_hfp_accept(&hfp, 16U) == 0, "slc: another channel not");
	btd_hfp_opened(&hfp, TEST_DLCI, BTD_HFP_HF_CHANNEL, 0);
	check(took("AT+BRSF=62"), "slc: AT+BRSF first");
	ag(brsf);
	check(took("AT+CIND=?"), "slc: AT+CIND=? next");
	ag("\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\n\r\nOK\r\n");
	check(took("AT+CIND?"), "slc: AT+CIND? next");
	ag("\r\n+CIND: 1,0,0,0,4,0,3\r\n\r\nOK\r\n");
	check(took("AT+CMER=3,0,0,1"), "slc: AT+CMER next");
	ag("\r\nOK\r\n");
	if (three_way) {
		check(took("AT+CHLD=?"), "slc: AT+CHLD=? with 3-way");
		ag("\r\n+CHLD: (0,1,2,3)\r\n\r\nOK\r\n");
	}

	/* Ready, and the commands after it. */
	check(btd_hfp_state_word(&hfp)[0] == 'r', "slc: ready");
	check(emitted("PHONE HF state=ready why=- service=1 signal=4 battery=3 roam=0"), "slc: the HF line told");
	check(took("AT+CLIP=1"), "slc: AT+CLIP=1 after");
	ag("\r\nOK\r\n");
	if (three_way) {
		check(took("AT+CCWA=1"), "slc: AT+CCWA=1 with 3-way");
		ag("\r\nERROR\r\n");
	}

	/* The errors' numbers asked for. */
	check(took("AT+CMEE=1"), "slc: AT+CMEE=1 after (a refused CCWA changes nothing)");
	ag("\r\nOK\r\n");
}

/* The lines' reading and the results' parts. */
static void
test_at(
	void)
{
	static const char overlong_start[] = "+CLIP: \"";
	struct btd_at_input input;
	char names[BTD_AT_INDICATORS_MAX][BTD_AT_NAME_MAX];
	char field[64];
	char big[400];
	const char *rest;
	size_t count;
	long value;
	int error;

	/* Lines at CR and LF, empty ones skipped, a NUL dropped. */
	collected_count = 0U;
	btd_at_input_init(&input);
	btd_at_input(&input, (const uint8_t *)"\r\nOK\r\n\r\nRI", 10U, collect, NULL);
	btd_at_input(&input, (const uint8_t *)"NG\r\n+VGS:\0 7\n", 13U, collect, NULL);
	check(collected_count == 3U && strcmp(collected[0], "OK") == 0 && strcmp(collected[1], "RING") == 0 && strcmp(collected[2], "+VGS: 7") == 0, "at: lines");

	/* A line past the room dropped whole, the next one kept. */
	memset(big, 'x', sizeof(big));
	memcpy(big, overlong_start, sizeof(overlong_start) - 1U);
	collected_count = 0U;
	btd_at_input(&input, (const uint8_t *)big, sizeof(big), collect, NULL);
	btd_at_input(&input, (const uint8_t *)"\r\nOK\r\n", 6U, collect, NULL);
	check(collected_count == 1U && strcmp(collected[0], "OK") == 0 && input.dropped == 1U, "at: an overlong line dropped");

	/* A result's name and fields: quotes keep commas, empty fields, numbers. */
	check(btd_at_result("+CLIP: \"+81 90,1\",145,,,\"Taro\"", "+CLIP", &rest) == 1, "at: +CLIP");
	check(btd_at_result("+CLIPX: 1", "+CLIP", &rest) == 0, "at: not +CLIP");
	error = btd_at_field(rest, 0U, field, sizeof(field));
	check(error == 0 && strcmp(field, "+81 90,1") == 0, "at: a quoted field with a comma");
	error = btd_at_field(rest, 2U, field, sizeof(field));
	check(error == 0 && field[0] == '\0', "at: an empty field");
	error = btd_at_field(rest, 4U, field, sizeof(field));
	check(error == 0 && strcmp(field, "Taro") == 0, "at: the fifth field");
	error = btd_at_field(rest, 5U, field, sizeof(field));
	check(error == ENOENT, "at: no sixth field");
	error = btd_at_number(rest, 1U, &value);
	check(error == 0 && value == 145L, "at: a number");
	error = btd_at_number(rest, 0U, &value);
	check(error == EINVAL, "at: not a number");
	error = btd_at_field("\"abc\"", 0U, field, 3U);
	check(error == ENOSPC && field[0] == '\0', "at: a field too long");

	/* The indicators' names in order, lowercased; a group without a name refused. */
	error = btd_at_indicators("(\"Service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3))", names, BTD_AT_INDICATORS_MAX, &count);
	check(error == 0 && count == 3U && strcmp(names[0], "service") == 0 && strcmp(names[2], "callsetup") == 0, "at: indicators");
	error = btd_at_indicators("((0,1))", names, BTD_AT_INDICATORS_MAX, &count);
	check(error == EINVAL, "at: an indicator without a name");
}

/* Keeps a line the input gave. */
static void
collect(
	void *context,
	const char *line)
{
	(void)context;
	if (collected_count < 8U) {
		(void)snprintf(collected[collected_count], sizeof(collected[0]), "%s", line);
		collected_count++;
	}
}

/* An incoming call answered, a key, hung up; a dial; an unanswered command. */
static void
test_call(
	void)
{
	const char *refused;
	const char *ended;

	/* Started: the SDP query for the AG; the AG's own DLC comes first. */
	start();
	check(world.sdp_queries == 1U, "call: the SDP query asked");
	slc("\r\n+BRSF: 871\r\n\r\nOK\r\n", 1);
	check(took("AT+CLCC"), "call: AT+CLCC after the SLC");
	ag("\r\nOK\r\n");

	/* A call rings: the setup indicator, RING and +CLIP, then the list. */
	world.emits[0] = '\0';
	ag("\r\n+CIEV: 3,1\r\n");
	check(emitted("PHONE CALL index=0 state=incoming dir=in number=\"\" name=\"\""), "call: ringing told at once");
	check(took("AT+CLCC"), "call: the list asked for");
	ag("\r\nRING\r\n\r\n+CLIP: \"+819012345678\",145,,,\"Taro\"\r\n");
	check(emitted("number=\"+819012345678\" name=\"Taro\""), "call: its number and name");
	ag("\r\n+CLCC: 1,1,4,0,0,\"+819012345678\",145\r\n\r\nOK\r\n");
	check(emitted("PHONE CALL index=1 state=incoming dir=in number=\"+819012345678\" name=\"Taro\""), "call: its index from the list, its name kept");

	/* Answered: ATA, then the call on. */
	refused = btd_hfp_ask(&hfp, 7U, BTD_HFP_ASK_ANSWER, NULL);
	check(refused == NULL && took("ATA"), "call: ATA");
	ag("\r\nOK\r\n");
	check(answered("7: DONE"), "call: answered");
	world.emits[0] = '\0';
	ag("\r\n+CIEV: 2,1\r\n");
	check(took("AT+CLCC"), "call: listed again");
	ag("\r\n+CIEV: 3,0\r\n");
	ag("\r\n+CLCC: 1,1,0,0,0,\"+819012345678\",145\r\n\r\nOK\r\n");
	check(emitted("PHONE CALL index=1 state=active dir=in"), "call: on");
	check(took("AT+CLCC"), "call: one more list after the change during the first (only one waits)");
	ag("\r\n+CLCC: 1,1,0,0,0,\"+819012345678\",145\r\n\r\nOK\r\n");
	check(took(""), "call: then none");

	/* A key, then hung up: the call ends once. */
	refused = btd_hfp_ask(&hfp, 8U, BTD_HFP_ASK_DTMF, "5");
	check(refused == NULL && took("AT+VTS=5"), "call: a key");
	ag("\r\nOK\r\n");
	refused = btd_hfp_ask(&hfp, 9U, BTD_HFP_ASK_HANGUP, NULL);
	check(refused == NULL && took("AT+CHUP"), "call: hung up");
	ag("\r\nOK\r\n");
	world.emits[0] = '\0';
	ag("\r\n+CIEV: 2,0\r\n");
	check(emitted("PHONE CALL index=1 state=ended"), "call: ended at once");
	check(took("AT+CLCC"), "call: listed after the end");
	ag("\r\nOK\r\n");
	ended = strstr(world.emits, "state=ended");
	check(ended != NULL && strstr(ended + 1, "state=ended") == NULL, "call: ended told once");

	/* A dial, alerting, then the list names it. */
	refused = btd_hfp_ask(&hfp, 10U, BTD_HFP_ASK_DIAL, "+8190");
	check(refused == NULL && took("ATD+8190;"), "dial: ATD");
	ag("\r\nOK\r\n");
	world.emits[0] = '\0';
	ag("\r\n+CIEV: 3,2\r\n");
	check(emitted("state=dialing dir=out"), "dial: dialing");
	check(took("AT+CLCC"), "dial: listed");
	ag("\r\n+CLCC: 1,0,3,0,0,\"+8190\",129\r\n\r\nOK\r\n");
	check(emitted("PHONE CALL index=1 state=alerting dir=out number=\"+8190\""), "dial: alerting, its number");

	/* An unanswered command: its timeout answered. */
	refused = btd_hfp_ask(&hfp, 11U, BTD_HFP_ASK_VOLUME, "9");
	check(refused == NULL && took("AT+VGS=9"), "volume: AT+VGS");
	world.now += BTD_HFP_COMMAND_MS;
	btd_hfp_tick(&hfp, world.now);
	check(answered("11: ERROR timeout\n11: DONE"), "volume: timed out");
	check(emitted("speaker=9"), "volume: told");
}

/* The owner's requests refused at once. */
static void
test_refusals(
	void)
{
	const char *refused;

	/* Before the SLC: not ready. */
	start();
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_DIAL, "123");
	check(refused != NULL && strcmp(refused, "not-ready") == 0, "refusal: not ready");

	/* After it (an AG without 3-way or voice): the arguments and what is not there. */
	slc("\r\n+BRSF: 0\r\n\r\nOK\r\n", 0);
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_DIAL, "12a");
	check(refused != NULL && strcmp(refused, "argument") == 0, "refusal: a number not dialable");
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_ANSWER, NULL);
	check(refused != NULL && strcmp(refused, "no-call") == 0, "refusal: nothing rings");
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_HOLD, "1");
	check(refused != NULL && strcmp(refused, "no-hold") == 0, "refusal: no 3-way");
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_VOICE, "1");
	check(refused != NULL && strcmp(refused, "no-voice") == 0, "refusal: no voice recognition");
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_VOLUME, "16");
	check(refused != NULL && strcmp(refused, "argument") == 0, "refusal: volume 16");
	refused = btd_hfp_ask(&hfp, 1U, BTD_HFP_ASK_DTMF, "5");
	check(refused != NULL && strcmp(refused, "no-call") == 0, "refusal: a key without a call");

	/* An AG that refuses the SLC: failed, tried again after the wait. */
	start();
	btd_hfp_opened(&hfp, TEST_DLCI, BTD_HFP_HF_CHANNEL, 0);
	(void)took("AT+BRSF=62");
	ag("\r\nERROR\r\n");
	check(emitted("PHONE HF state=failed why=slc"), "refusal: the SLC refused");
	world.now += BTD_HFP_RETRY_FIRST_MS;
	btd_hfp_tick(&hfp, world.now);
	check(world.sdp_queries == 2U, "refusal: tried again after the wait");
}

/* An AG without the enhanced call status: the indicators' guess. */
static void
test_guess(
	void)
{
	/* The SLC without 3-way or call status: no CHLD=?, no list. */
	start();
	slc("\r\n+BRSF: 0\r\n\r\nOK\r\n", 0);

	/* Ringing, its number, on, ended. */
	world.emits[0] = '\0';
	ag("\r\n+CIEV: 3,1\r\n\r\nRING\r\n\r\n+CLIP: \"0312345678\",129\r\n");
	check(emitted("state=incoming dir=in number=\"0312345678\""), "guess: ringing with its number");
	ag("\r\n+CIEV: 2,1\r\n\r\n+CIEV: 3,0\r\n");
	check(emitted("state=active dir=in number=\"0312345678\""), "guess: on");
	ag("\r\n+CIEV: 2,0\r\n");
	check(emitted("state=ended"), "guess: ended");
	check(took(""), "guess: no list asked for");
}

/* The link ends during a call: the call ends, a waiting request is answered as lost. */
static void
test_lost(
	void)
{
	const char *refused;

	/* A call on (found by the first list). */
	start();
	slc("\r\n+BRSF: 64\r\n\r\nOK\r\n", 0);
	check(took("AT+CLCC"), "lost: the first list");
	ag("\r\n+CLCC: 2,0,0,0,0,\"110\",129\r\n\r\nOK\r\n");
	check(emitted("PHONE CALL index=2 state=active dir=out number=\"110\""), "lost: a call on before the SLC");

	/* A request waiting, then the link's end. */
	refused = btd_hfp_ask(&hfp, 5U, BTD_HFP_ASK_HANGUP, NULL);
	check(refused == NULL, "lost: hang up asked");
	world.emits[0] = '\0';
	btd_hfp_ended(&hfp);
	check(answered("5: ERROR lost\n5: DONE"), "lost: the request answered");
	check(emitted("PHONE CALL index=2 state=ended") && emitted("PHONE HF state=off"), "lost: the call ended, the HF off");
}
