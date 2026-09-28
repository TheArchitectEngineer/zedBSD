/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * touchinject: drives the test touch screen of /dev/input-inject and reads
 * it back (WS079 p012).
 *
 * The injector exists only in kernels built with CONFIG_INPUT_TEST_INJECT=y,
 * and only root may open it.  Its touch screen takes frames of fingers and
 * runs them through the USB touch screen's state machine, so its evdev node
 * speaks multitouch protocol B as a USB touch screen does.
 *
 *   touchinject [SCRIPT]   replays a touch script (standard input without SCRIPT)
 *   touchinject -c         checks the injector's refusals of touch setups and frames
 *   touchinject -d MS      waits for the test touch screen's evdev node and prints
 *                          its name, its axes and every event for MS milliseconds
 *
 * A script has one line per frame; '#' starts a comment.  A line holds one
 * command, or finger commands separated by ';', which make one frame
 * together:
 *   size W H [N]          declares the screen with fingers in 0..W and 0..H and
 *                         N fingers per report (the first command; default
 *                         32767 32767 2: three or more fingers make the split
 *                         reports of a "hybrid" USB touch screen)
 *   down ID X Y           finger ID touches at X, Y
 *   move ID X Y           finger ID moves to X, Y
 *   up ID                 finger ID lifts
 *   swipe DX DY STEPS MS  moves every touching finger by DX, DY in STEPS frames,
 *                         MS between them
 *   wait MS / hold MS     sleeps
 * Every finger that touches is in every frame; a finger that lifts is in its
 * last frame with its tip up.  The screen stays declared until the program
 * ends.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <uapi/input-inject.h>
#include <uapi/input.h>

/* The longest script line, and the longest command in it. */
#define TOUCHINJECT_LINE_MAX	256
#define TOUCHINJECT_WORD_MAX	16

/* The size and the fingers per report the screen is declared with when a script does not say. */
#define TOUCHINJECT_DEFAULT_SIZE	32767
#define TOUCHINJECT_DEFAULT_PER_REPORT	2

/* The injector's node. */
#define TOUCHINJECT_NODE	"/dev/input-inject"

/* The directory of the evdev nodes, and the name the test touch screen's node reports. */
#define TOUCHINJECT_INPUT_DIRECTORY	"/dev/input"
#define TOUCHINJECT_SCREEN_NAME		"Test touchscreen (input-inject)"

/* The commands of a script. */
enum touch_command {
	COMMAND_NONE,
	COMMAND_SIZE,
	COMMAND_DOWN,
	COMMAND_MOVE,
	COMMAND_UP,
	COMMAND_SWIPE,
	COMMAND_WAIT,
};

/*
 * One script word and the command it names.
 *
 * The table of them is constant for the program's life.
 */
struct touch_command_word {
	const char *word;
	enum touch_command command;
};

/*
 * One finger the script holds.
 *
 * A finger is used from its down to the frame after its up; lifting marks a
 * finger whose next frame is its last, with its tip up.
 */
struct touch_finger {
	int used;
	int lifting;
	int contact_id;
	int x;
	int y;
};

/*
 * The screen as a replayed script has left it.
 *
 * One instance lives for one replay.
 */
struct touch_screen {
	int fd;
	int declared;
	int width;
	int height;
	struct touch_finger fingers[INPUT_INJECT_TOUCH_CONTACTS];
};

/*
 * The counts of one refusal check.
 *
 * One instance lives for the check; every case adds to one of the counts.
 */
struct check_result {
	unsigned passed;
	unsigned failed;
};

/*
 * One evdev code and the name the dump prints it by.
 *
 * The table of them is constant for the program's life.
 */
struct code_name {
	int type;
	int code;
	const char *name;
};

/*
 * The words a script's commands are written with.
 *
 * wait and hold are the same command; hold reads better at a script's end.
 */
static const struct touch_command_word touch_commands[] = {
	{ "size", COMMAND_SIZE },
	{ "down", COMMAND_DOWN },
	{ "move", COMMAND_MOVE },
	{ "up", COMMAND_UP },
	{ "swipe", COMMAND_SWIPE },
	{ "wait", COMMAND_WAIT },
	{ "hold", COMMAND_WAIT },
};

/*
 * The names the dump prints the touch screen's events by.
 *
 * The axes are listed first, in the order the dump prints their ranges.
 */
static const struct code_name code_names[] = {
	{ EV_ABS, ABS_X, "ABS_X" },
	{ EV_ABS, ABS_Y, "ABS_Y" },
	{ EV_ABS, ABS_MT_SLOT, "ABS_MT_SLOT" },
	{ EV_ABS, ABS_MT_TRACKING_ID, "ABS_MT_TRACKING_ID" },
	{ EV_ABS, ABS_MT_POSITION_X, "ABS_MT_POSITION_X" },
	{ EV_ABS, ABS_MT_POSITION_Y, "ABS_MT_POSITION_Y" },
	{ EV_KEY, BTN_TOUCH, "BTN_TOUCH" },
	{ EV_SYN, SYN_REPORT, "SYN_REPORT" },
};

/* The number of axes at the head of code_names. */
#define TOUCHINJECT_AXIS_COUNT	6U

static int replay(const char *path);
static int run_line(struct touch_screen *screen, char *line, unsigned number);
static int run_part(struct touch_screen *screen, char *part, int *frame);
static enum touch_command command_of(const char *word);
static int run_size(struct touch_screen *screen, const char *part);
static int run_finger(struct touch_screen *screen, enum touch_command command, const char *part);
static int run_swipe(struct touch_screen *screen, const char *part);
static int run_wait(const char *part);
static struct touch_finger * finger_of(struct touch_screen *screen, int contact_id);
static int screen_declare(struct touch_screen *screen, int width, int height, int per_report);
static int screen_frame(struct touch_screen *screen);
static void sleep_ms(long milliseconds);
static int check(void);
static void check_expect(struct check_result *result, const char *name, ssize_t written, int error, int expected);
static ssize_t check_setup(int fd, unsigned kind, unsigned per_report, unsigned reserved);
static ssize_t check_frame(int fd, unsigned count, int contact_id, int tip, int x, unsigned reserved);
static int dump(long milliseconds);
static int dump_find(char *path, size_t size);
static void dump_axes(int fd);
static const char *code_name_of(int type, int code);
static long long now_ms(void);

/*
 * Replays a script, checks the refusals, or dumps the touch screen, as asked.
 */
int
main(
	int argc,
	char **argv)
{
	int status;
	int same;
	long milliseconds;

	/* -c checks the refusals. */
	same = 1;
	if (argc == 2)
		same = strcmp(argv[1], "-c");
	if (same == 0) {
		status = check();
		return status;
	}

	/* -d MS dumps the touch screen's node. */
	same = 1;
	if (argc == 3)
		same = strcmp(argv[1], "-d");
	if (same == 0) {
		milliseconds = strtol(argv[2], NULL, 10);
		status = dump(milliseconds);
		return status;
	}

	/* More than one argument is not a replay. */
	if (argc > 2) {
		fprintf(stderr, "usage: touchinject [SCRIPT] | -c | -d MS\n");
		return 2;
	}

	/* Replays the named script, or standard input. */
	if (argc == 2) {
		status = replay(argv[1]);
	} else {
		status = replay(NULL);
	}

	/* Succeeded or failed as the replay did. */
	return status;
}

/* Replays one script through the injector. */
static int
replay(
	const char *path)
{
	struct touch_screen screen;
	char line[TOUCHINJECT_LINE_MAX];
	FILE *script;
	char *read_line;
	unsigned number;
	int error;

	/* Opens the script, or reads standard input. */
	script = stdin;
	if (path != NULL) {
		script = fopen(path, "r");
		if (script == NULL) {
			perror(path);
			return 1;
		}
	}

	/* Opens the injector; the screen is declared by the first command. */
	memset(&screen, 0, sizeof(screen));
	screen.fd = open(TOUCHINJECT_NODE, O_WRONLY);
	if (screen.fd < 0) {
		perror(TOUCHINJECT_NODE);
		return 1;
	}

	/* Runs the script line by line. */
	number = 0;
	while (1) {
		/* The end of the script ends the replay. */
		read_line = fgets(line, sizeof(line), script);
		if (read_line == NULL)
			break;

		/* A bad line stops the replay. */
		number++;
		error = run_line(&screen, line, number);
		if (error != 0)
			return 1;
	}

	/* Declares a default screen for a script without commands. */
	if (!screen.declared) {
		error = screen_declare(&screen, TOUCHINJECT_DEFAULT_SIZE, TOUCHINJECT_DEFAULT_SIZE, TOUCHINJECT_DEFAULT_PER_REPORT);
		if (error != 0)
			return 1;
	}

	/* Closing the injector removes the screen. */
	close(screen.fd);

	/* Succeeded: every line was written. */
	return 0;
}

/*
 * Runs one script line: its commands in order, then, when a finger command
 * changed the fingers, one frame.
 */
static int
run_line(
	struct touch_screen *screen,
	char *line,
	unsigned number)
{
	char *hash;
	char *part;
	char *rest;
	int frame;
	int error;

	/* Drops a comment. */
	hash = strchr(line, '#');
	if (hash != NULL)
		*hash = '\0';

	/* Runs each ';'-separated part; a bad part is reported with its line. */
	frame = 0;
	rest = line;
	while (rest != NULL) {
		/* Cuts the next part out of the line. */
		part = rest;
		rest = strchr(part, ';');
		if (rest != NULL) {
			*rest = '\0';
			rest++;
		}

		/* A bad command stops the replay. */
		error = run_part(screen, part, &frame);
		if (error < 0) {
			fprintf(stderr, "touchinject: line %u: bad command\n", number);
			return -1;
		}

		/* A write the kernel refused stops the replay. */
		if (error > 0)
			return -1;
	}

	/* The finger commands of the line make one frame. */
	if (frame) {
		error = screen_frame(screen);
		if (error != 0)
			return -1;
	}

	/* Succeeded: the line was run. */
	return 0;
}

/*
 * Runs one command of a line.  Sets *frame when the command changed the
 * fingers.  Returns 0, -1 for a bad command, or 1 for a refused write.
 */
static int
run_part(
	struct touch_screen *screen,
	char *part,
	int *frame)
{
	enum touch_command command;
	char word[TOUCHINJECT_WORD_MAX];
	int count;
	int error;

	/* A blank part does nothing. */
	count = sscanf(part, "%15s", word);
	if (count != 1)
		return 0;

	/* Any first command but size declares the default screen. */
	command = command_of(word);
	if (!screen->declared && command != COMMAND_SIZE) {
		error = screen_declare(screen, TOUCHINJECT_DEFAULT_SIZE, TOUCHINJECT_DEFAULT_SIZE, TOUCHINJECT_DEFAULT_PER_REPORT);
		if (error != 0)
			return 1;
	}

	/* Runs the command. */
	switch (command) {
	case COMMAND_SIZE:
		error = run_size(screen, part);
		break;
	case COMMAND_DOWN:
	case COMMAND_MOVE:
	case COMMAND_UP:
		error = run_finger(screen, command, part);
		if (error == 0)
			*frame = 1;
		break;
	case COMMAND_SWIPE:
		error = run_swipe(screen, part);
		break;
	case COMMAND_WAIT:
		error = run_wait(part);
		break;
	default:
		error = -1;
		break;
	}

	/* Reports the command's outcome. */
	if (error != 0)
		return error;

	/* Succeeded: the command was run. */
	return 0;
}

/* Finds the command a script word names (COMMAND_NONE for none). */
static enum touch_command
command_of(
	const char *word)
{
	unsigned index;
	int same;

	/* Compares the word with every command's. */
	for (index = 0; index < sizeof(touch_commands) / sizeof(touch_commands[0]); index++) {
		/* The command whose word it is. */
		same = strcmp(word, touch_commands[index].word);
		if (same == 0)
			return touch_commands[index].command;
	}

	/* The word names no command. */
	return COMMAND_NONE;
}

/* Declares the screen's size and fingers per report; only as the first command. */
static int
run_size(
	struct touch_screen *screen,
	const char *part)
{
	int width;
	int height;
	int per_report;
	int count;
	int error;

	/* A screen that is declared already cannot change. */
	if (screen->declared)
		return -1;

	/* The size, and the fingers per report when given. */
	per_report = TOUCHINJECT_DEFAULT_PER_REPORT;
	count = sscanf(part, "%*s %d %d %d", &width, &height, &per_report);
	if (count < 2)
		return -1;

	/* Declares the screen. */
	error = screen_declare(screen, width, height, per_report);
	if (error != 0)
		return 1;

	/* Succeeded: the screen exists. */
	return 0;
}

/* Puts a finger down, moves it, or marks it to lift in the next frame. */
static int
run_finger(
	struct touch_screen *screen,
	enum touch_command command,
	const char *part)
{
	struct touch_finger *finger;
	int contact_id;
	int x;
	int y;
	int count;

	/* A lift names only the finger. */
	if (command == COMMAND_UP) {
		count = sscanf(part, "%*s %d", &contact_id);
		if (count != 1)
			return -1;

		/* Only a finger that touches can lift. */
		finger = finger_of(screen, contact_id);
		if (finger == NULL)
			return -1;
		finger->lifting = 1;
		return 0;
	}

	/* A touch or a move names the finger and where it is. */
	count = sscanf(part, "%*s %d %d %d", &contact_id, &x, &y);
	if (count != 3)
		return -1;

	/* A move needs the finger down. */
	finger = finger_of(screen, contact_id);
	if (command == COMMAND_MOVE && finger == NULL)
		return -1;

	/* A new finger takes a free place (the kernel refuses a bad identifier). */
	if (finger == NULL) {
		finger = finger_of(screen, -1);
		if (finger == NULL)
			return -1;
		finger->used = 1;
		finger->lifting = 0;
		finger->contact_id = contact_id;
	}

	/* The finger is where the command says. */
	finger->x = x;
	finger->y = y;

	/* Succeeded: the finger is in the next frame. */
	return 0;
}

/* Moves every touching finger by DX, DY in STEPS frames, MS between them. */
static int
run_swipe(
	struct touch_screen *screen,
	const char *part)
{
	int start_x[INPUT_INJECT_TOUCH_CONTACTS];
	int start_y[INPUT_INJECT_TOUCH_CONTACTS];
	struct touch_finger *finger;
	long milliseconds;
	unsigned index;
	int dx;
	int dy;
	int steps;
	int step;
	int count;
	int error;

	/* The way, the frames and the time between them. */
	count = sscanf(part, "%*s %d %d %d %ld", &dx, &dy, &steps, &milliseconds);
	if (count != 4 || steps < 1 || milliseconds < 0)
		return -1;

	/* Where each finger starts. */
	for (index = 0; index < INPUT_INJECT_TOUCH_CONTACTS; index++) {
		start_x[index] = screen->fingers[index].x;
		start_y[index] = screen->fingers[index].y;
	}

	/* Each step is one frame, a part of the way further. */
	for (step = 1; step <= steps; step++) {
		for (index = 0; index < INPUT_INJECT_TOUCH_CONTACTS; index++) {
			finger = &screen->fingers[index];
			if (!finger->used || finger->lifting)
				continue;
			finger->x = start_x[index] + dx * step / steps;
			finger->y = start_y[index] + dy * step / steps;
		}

		/* Writes the step's frame. */
		error = screen_frame(screen);
		if (error != 0)
			return 1;
		sleep_ms(milliseconds);
	}

	/* Succeeded: every step was written. */
	return 0;
}

/* Sleeps for the time a wait or hold command gives. */
static int
run_wait(
	const char *part)
{
	long milliseconds;
	int count;

	/* A negative or missing time is refused. */
	count = sscanf(part, "%*s %ld", &milliseconds);
	if (count != 1 || milliseconds < 0)
		return -1;

	/* Succeeded: the time has passed. */
	sleep_ms(milliseconds);
	return 0;
}

/* Finds the used finger with a contact identifier, or a free place for -1; NULL for none. */
static struct touch_finger *
finger_of(
	struct touch_screen *screen,
	int contact_id)
{
	struct touch_finger *finger;
	unsigned index;

	/* Looks at every place. */
	for (index = 0; index < INPUT_INJECT_TOUCH_CONTACTS; index++) {
		finger = &screen->fingers[index];

		/* A free place, when one is asked for. */
		if (contact_id < 0) {
			if (!finger->used)
				return finger;
			continue;
		}

		/* The finger with that identifier. */
		if (finger->used && finger->contact_id == contact_id)
			return finger;
	}

	/* Nothing matches. */
	return NULL;
}

/* Declares the touch screen: its area and the fingers per report. */
static int
screen_declare(
	struct touch_screen *screen,
	int width,
	int height,
	int per_report)
{
	struct input_inject_setup setup;
	ssize_t written;

	/* Describes the screen: its kind, its area and its reports. */
	memset(&setup, 0, sizeof(setup));
	setup.magic = INPUT_INJECT_MAGIC;
	setup.kind = INPUT_INJECT_KIND_TOUCH;
	setup.x_max = width;
	setup.y_max = height;
	setup.report_contacts = (uint32_t)per_report;

	/* Writes the setup record, which registers the screen. */
	written = write(screen->fd, &setup, sizeof(setup));
	if (written != (ssize_t)sizeof(setup)) {
		perror("touchinject: declare");
		return -1;
	}

	/* Succeeded: the screen exists until the injector is closed. */
	screen->declared = 1;
	screen->width = width;
	screen->height = height;
	return 0;
}

/*
 * Writes one frame: every finger that touches, and every finger that lifts
 * with its tip up (which then leaves the script).
 */
static int
screen_frame(
	struct touch_screen *screen)
{
	struct input_inject_touch_frame frame;
	struct input_inject_contact *contact;
	struct touch_finger *finger;
	ssize_t written;
	unsigned index;

	/* Collects the used fingers in their places' order. */
	memset(&frame, 0, sizeof(frame));
	for (index = 0; index < INPUT_INJECT_TOUCH_CONTACTS; index++) {
		finger = &screen->fingers[index];
		if (!finger->used)
			continue;
		contact = &frame.contacts[frame.count];
		contact->contact_id = finger->contact_id;
		contact->tip = !finger->lifting;
		contact->x = finger->x;
		contact->y = finger->y;
		frame.count++;
	}

	/* Writes the frame; the kernel takes it whole or refuses it. */
	written = write(screen->fd, &frame, sizeof(frame));
	if (written != (ssize_t)sizeof(frame)) {
		perror("touchinject: frame");
		return -1;
	}

	/* A finger that lifted in this frame leaves the script. */
	for (index = 0; index < INPUT_INJECT_TOUCH_CONTACTS; index++) {
		finger = &screen->fingers[index];
		if (finger->used && finger->lifting)
			finger->used = 0;
	}

	/* Succeeded: the frame is written. */
	return 0;
}

/* Sleeps for a number of milliseconds. */
static void
sleep_ms(
	long milliseconds)
{
	struct timespec pause;

	/* Splits the time into seconds and nanoseconds. */
	pause.tv_sec = milliseconds / 1000;
	pause.tv_nsec = (milliseconds % 1000) * 1000000L;

	/* Sleeps; an early wake is of no matter to a test script. */
	(void)nanosleep(&pause, NULL);
}

/*
 * Checks that the injector refuses bad touch setups and bad frames, and
 * takes good ones.
 */
static int
check(void)
{
	struct check_result result;
	struct input_event event;
	ssize_t written;
	int fd;

	/* Nothing has been checked yet. */
	memset(&result, 0, sizeof(result));

	/* Opens the injector as root. */
	fd = open(TOUCHINJECT_NODE, O_WRONLY);
	if (fd < 0) {
		perror(TOUCHINJECT_NODE);
		return 1;
	}

	/* A touch screen of no finger per report, or of more than a frame holds, is refused. */
	written = check_setup(fd, INPUT_INJECT_KIND_TOUCH, 0, 0);
	check_expect(&result, "setup-no-finger", written, errno, EINVAL);
	written = check_setup(fd, INPUT_INJECT_KIND_TOUCH, INPUT_INJECT_TOUCH_CONTACTS + 1U, 0);
	check_expect(&result, "setup-eleven-fingers", written, errno, EINVAL);

	/* The reserved word, a pen with fingers and an unknown kind are refused. */
	written = check_setup(fd, INPUT_INJECT_KIND_TOUCH, 2, 1);
	check_expect(&result, "setup-reserved", written, errno, EINVAL);
	written = check_setup(fd, INPUT_INJECT_KIND_PEN, 2, 0);
	check_expect(&result, "setup-pen-with-fingers", written, errno, EINVAL);
	written = check_setup(fd, 3, 2, 0);
	check_expect(&result, "setup-unknown-kind", written, errno, EINVAL);

	/* A good setup declares the touch screen. */
	written = check_setup(fd, INPUT_INJECT_KIND_TOUCH, 2, 0);
	check_expect(&result, "setup-good", written, errno, 0);

	/* A pen's event is not a frame. */
	memset(&event, 0, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	errno = 0;
	written = write(fd, &event, sizeof(event));
	check_expect(&result, "frame-pen-event", written, errno, EINVAL);

	/* Too many fingers, a tip of 2, an identifier past 255, a finger off the screen, the reserved word. */
	written = check_frame(fd, INPUT_INJECT_TOUCH_CONTACTS + 1U, 1, 1, 10, 0);
	check_expect(&result, "frame-eleven-fingers", written, errno, EINVAL);
	written = check_frame(fd, 1, 1, 2, 10, 0);
	check_expect(&result, "frame-tip-2", written, errno, EINVAL);
	written = check_frame(fd, 1, INPUT_INJECT_CONTACT_ID_MAX + 1, 1, 10, 0);
	check_expect(&result, "frame-id-256", written, errno, EINVAL);
	written = check_frame(fd, 1, 1, 1, 101, 0);
	check_expect(&result, "frame-off-screen", written, errno, EINVAL);
	written = check_frame(fd, 1, 1, 1, 10, 1);
	check_expect(&result, "frame-reserved", written, errno, EINVAL);

	/* A good frame, and a frame of no finger, are still taken. */
	written = check_frame(fd, 1, 1, 1, 10, 0);
	check_expect(&result, "frame-good", written, errno, 0);
	written = check_frame(fd, 0, 0, 0, 0, 0);
	check_expect(&result, "frame-empty", written, errno, 0);

	/* Closing removes the screen. */
	close(fd);

	/* Reports the totals. */
	printf("TOUCHCHECK result=%s passed=%u failed=%u\n", result.failed == 0 ? "ok" : "FAIL", result.passed, result.failed);
	if (result.failed != 0)
		return 1;

	/* Succeeded: every refusal was as expected. */
	return 0;
}

/*
 * Records one case: a write that should have failed with the expected
 * error, or succeeded when the expected error is 0.
 */
static void
check_expect(
	struct check_result *result,
	const char *name,
	ssize_t written,
	int error,
	int expected)
{
	int got;

	/* A call that did not fail reports no error. */
	got = 0;
	if (written < 0)
		got = error;

	/* Counts and prints the case. */
	if (got == expected) {
		result->passed++;
		printf("TOUCHCHECK case=%s expect=%d got=%d ok\n", name, expected, got);
	} else {
		result->failed++;
		printf("TOUCHCHECK case=%s expect=%d got=%d FAIL\n", name, expected, got);
	}
}

/* Writes one setup record of an area of 100 x 100. */
static ssize_t
check_setup(
	int fd,
	unsigned kind,
	unsigned per_report,
	unsigned reserved)
{
	struct input_inject_setup setup;
	ssize_t written;

	/* The record as asked. */
	memset(&setup, 0, sizeof(setup));
	setup.magic = INPUT_INJECT_MAGIC;
	setup.kind = kind;
	setup.x_max = 100;
	setup.y_max = 100;
	setup.report_contacts = per_report;
	setup.reserved = reserved;

	/* Writes it; the kernel takes it or refuses it. */
	errno = 0;
	written = write(fd, &setup, sizeof(setup));

	/* Succeeded or refused, as the kernel answered. */
	return written;
}

/* Writes one frame of count fingers, all alike but for their identifiers. */
static ssize_t
check_frame(
	int fd,
	unsigned count,
	int contact_id,
	int tip,
	int x,
	unsigned reserved)
{
	struct input_inject_touch_frame frame;
	ssize_t written;
	unsigned index;

	/* The frame as asked (a count past the array names only its fingers). */
	memset(&frame, 0, sizeof(frame));
	frame.count = count;
	frame.reserved = reserved;
	for (index = 0; index < count && index < INPUT_INJECT_TOUCH_CONTACTS; index++) {
		frame.contacts[index].contact_id = contact_id + (int)index;
		frame.contacts[index].tip = tip;
		frame.contacts[index].x = x;
		frame.contacts[index].y = 10;
	}

	/* Writes it; the kernel takes it whole or refuses it. */
	errno = 0;
	written = write(fd, &frame, sizeof(frame));

	/* Succeeded or refused, as the kernel answered. */
	return written;
}

/*
 * Waits up to MS milliseconds for the test touch screen's evdev node, then
 * prints its name, its axes and every event it reports until MS
 * milliseconds have passed from the start or the screen goes away.
 */
static int
dump(
	long milliseconds)
{
	struct input_event events[32];
	struct pollfd poller;
	char path[64];
	long long deadline;
	long long now;
	ssize_t bytes;
	size_t count;
	size_t index;
	unsigned long total;
	int found;
	int ready;
	int fd;

	/* Waits for the node, which appears when the screen is declared. */
	deadline = now_ms() + milliseconds;
	found = 0;
	while (1) {
		/* A node with the screen's name ends the wait. */
		found = dump_find(path, sizeof(path));
		if (found)
			break;

		/* Gives up when the time is over. */
		now = now_ms();
		if (now >= deadline)
			break;
		sleep_ms(20);
	}

	/* Without the node there is nothing to read. */
	if (!found) {
		printf("TOUCHDUMP end events=0 reason=no-node\n");
		return 1;
	}

	/* Opens the node and prints what it is. */
	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror(path);
		return 1;
	}

	/* The node's path, name and axes. */
	printf("TOUCHDUMP node=%s name=%s\n", path, TOUCHINJECT_SCREEN_NAME);
	dump_axes(fd);
	fflush(stdout);

	/* Prints every event until the time is over or the screen goes away. */
	total = 0;
	while (1) {
		/* Waits for events for the rest of the time. */
		now = now_ms();
		if (now >= deadline) {
			printf("TOUCHDUMP end events=%lu reason=time\n", total);
			break;
		}

		/* Polls the node for the rest of the time. */
		poller.fd = fd;
		poller.events = POLLIN;
		poller.revents = 0;
		ready = poll(&poller, 1, (int)(deadline - now));
		if (ready <= 0)
			continue;

		/* Reads the events that are ready. */
		bytes = read(fd, events, sizeof(events));
		if (bytes < 0 && errno == EAGAIN)
			continue;
		if (bytes <= 0) {
			printf("TOUCHDUMP end events=%lu reason=gone errno=%d\n", total, bytes < 0 ? errno : 0);
			break;
		}

		/* Prints each event by its name and value. */
		count = (size_t)bytes / sizeof(events[0]);
		for (index = 0; index < count; index++) {
			printf("TOUCHDUMP event %s %d\n", code_name_of(events[index].type, events[index].code), events[index].value);
			total++;
		}

		/* The lines leave at once, for a reader of the output file. */
		fflush(stdout);
	}

	/* The node is no longer read. */
	close(fd);

	/* Succeeded: the events were printed. */
	return 0;
}

/* Finds the evdev node whose name is the test touch screen's; reports whether it did. */
static int
dump_find(
	char *path,
	size_t size)
{
	DIR *directory;
	struct dirent *entry;
	char name[64];
	int found;
	int fd;
	int length;
	int same;

	/* Without the directory there is no node. */
	directory = opendir(TOUCHINJECT_INPUT_DIRECTORY);
	if (directory == NULL)
		return 0;

	/* Asks every eventN node for its name. */
	found = 0;
	while (!found) {
		/* The end of the directory ends the search. */
		entry = readdir(directory);
		if (entry == NULL)
			break;

		/* Only eventN nodes speak evdev. */
		same = strncmp(entry->d_name, "event", 5);
		if (same != 0)
			continue;

		/* Opens the node to ask for its name. */
		length = snprintf(path, size, "%s/%s", TOUCHINJECT_INPUT_DIRECTORY, entry->d_name);
		if (length < 0 || (size_t)length >= size)
			continue;
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;

		/* The test touch screen's name ends the search. */
		memset(name, 0, sizeof(name));
		length = ioctl(fd, EVIOCGNAME(sizeof(name) - 1U), name);
		close(fd);
		if (length < 0)
			continue;
		same = strcmp(name, TOUCHINJECT_SCREEN_NAME);
		if (same == 0)
			found = 1;
	}

	/* The directory stream is no longer needed. */
	closedir(directory);

	/* Succeeded: whether the node was found (its path is in path). */
	return found;
}

/* Prints the range and resolution of the touch screen's six axes. */
static void
dump_axes(
	int fd)
{
	struct input_absinfo axis;
	unsigned index;
	int error;

	/* Asks the node for each axis. */
	for (index = 0; index < TOUCHINJECT_AXIS_COUNT; index++) {
		/* An axis that cannot be read is printed as missing. */
		memset(&axis, 0, sizeof(axis));
		error = ioctl(fd, EVIOCGABS(code_names[index].code), &axis);
		if (error < 0) {
			printf("TOUCHDUMP abs %s missing errno=%d\n", code_names[index].name, errno);
			continue;
		}

		/* The axis's range and resolution. */
		printf("TOUCHDUMP abs %s min=%d max=%d resolution=%d\n", code_names[index].name, axis.minimum, axis.maximum, axis.resolution);
	}
}

/* Tells the name of an event's code, or "UNKNOWN" for one the screen does not declare. */
static const char *
code_name_of(
	int type,
	int code)
{
	unsigned index;

	/* Looks the code up among the screen's. */
	for (index = 0; index < sizeof(code_names) / sizeof(code_names[0]); index++) {
		if (code_names[index].type == type && code_names[index].code == code)
			return code_names[index].name;
	}

	/* The screen does not declare it. */
	return "UNKNOWN";
}

/* Reports the monotonic time in milliseconds. */
static long long
now_ms(void)
{
	struct timespec now;

	/* Reads the monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Succeeded: the time in milliseconds. */
	return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000L;
}
