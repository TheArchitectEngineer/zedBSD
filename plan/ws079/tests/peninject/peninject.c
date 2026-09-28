/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * peninject: replays a pen script through /dev/input-inject (test builds
 * with CONFIG_INPUT_TEST_INJECT=y only).  Run as root.
 *
 * Usage: peninject [SCRIPT]   (standard input without SCRIPT)
 *
 * One command per line; '#' starts a comment.
 *   size W H                 declare the pen with ABS_X 0..W, ABS_Y 0..H
 *                            (first command; default 32767 32767)
 *   tool pen|rubber          choose the tool for the next down
 *   down X Y P [TX TY]       bring the tool in and touch with pressure P
 *   move X Y P [TX TY]       move while touching or hovering
 *   ramp FROM TO STEPS MS    step the pressure FROM..TO, MS between steps
 *   button stylus|stylus2 0|1
 *   up                       lift and take the tool out of range
 *   wait MS                  sleep
 * The pen stays open until the script ends; `hold MS` keeps it that long
 * afterwards so a reader can still query it.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <uapi/input-inject.h>
#include <uapi/input.h>

#define PENINJECT_LINE_MAX	256
#define PENINJECT_BATCH_MAX	INPUT_INJECT_EVENTS_MAX

/* The pen as the script has left it. */
struct pen_state {
	int fd;
	int declared;
	int tool;
	int in_range;
	int x, y, pressure, tilt_x, tilt_y;
	struct input_event batch[PENINJECT_BATCH_MAX];
	unsigned count;
};

static int pen_declare(struct pen_state *pen, int width, int height);
static void pen_add(struct pen_state *pen, int type, int code, int value);
static int pen_flush(struct pen_state *pen);
static int pen_position(struct pen_state *pen, int argc, int *values);
static void sleep_ms(long milliseconds);
static int run_line(struct pen_state *pen, char *line, unsigned number);

int
main(
	int argc,
	char **argv)
{
	struct pen_state pen;
	char line[PENINJECT_LINE_MAX];
	FILE *script;
	unsigned number;

	/* Opens the script and the injector. */
	script = stdin;
	if (argc > 2) {
		fprintf(stderr, "usage: peninject [SCRIPT]\n");
		return 2;
	}
	if (argc == 2) {
		script = fopen(argv[1], "r");
		if (script == NULL) {
			perror(argv[1]);
			return 1;
		}
	}
	memset(&pen, 0, sizeof(pen));
	pen.tool = BTN_TOOL_PEN;
	pen.fd = open("/dev/input-inject", O_WRONLY);
	if (pen.fd < 0) {
		perror("/dev/input-inject");
		return 1;
	}

	/* Runs the script line by line. */
	number = 0;
	while (fgets(line, sizeof(line), script) != NULL) {
		number++;
		if (run_line(&pen, line, number) != 0)
			return 1;
	}

	/* Declares a default pen for an empty script, then closes. */
	if (!pen.declared && pen_declare(&pen, 32767, 32767) != 0)
		return 1;
	close(pen.fd);
	return 0;
}

/* Runs one script line. */
static int
run_line(
	struct pen_state *pen,
	char *line,
	unsigned number)
{
	char word[16], name[16];
	int values[5];
	int count, i, from, to, steps;
	long milliseconds;
	char *hash;

	/* Drops comments and blank lines. */
	hash = strchr(line, '#');
	if (hash != NULL)
		*hash = '\0';
	if (sscanf(line, "%15s", word) != 1)
		return 0;

	/* Declares the pen with the script's size. */
	if (strcmp(word, "size") == 0) {
		if (pen->declared ||
		    sscanf(line, "%*s %d %d", &values[0], &values[1]) != 2)
			goto bad;
		return pen_declare(pen, values[0], values[1]);
	}
	if (!pen->declared && pen_declare(pen, 32767, 32767) != 0)
		return -1;

	/* Chooses the tool for the next approach. */
	if (strcmp(word, "tool") == 0) {
		if (sscanf(line, "%*s %15s", name) != 1 || pen->in_range)
			goto bad;
		if (strcmp(name, "pen") == 0)
			pen->tool = BTN_TOOL_PEN;
		else if (strcmp(name, "rubber") == 0)
			pen->tool = BTN_TOOL_RUBBER;
		else
			goto bad;
		return 0;
	}

	/* Brings the tool in, sets the axes and touches. */
	if (strcmp(word, "down") == 0 || strcmp(word, "move") == 0) {
		count = sscanf(line, "%*s %d %d %d %d %d", &values[0],
			       &values[1], &values[2], &values[3], &values[4]);
		if (count != 3 && count != 5)
			goto bad;
		if (!pen->in_range) {
			pen_add(pen, EV_KEY, pen->tool, 1);
			pen->in_range = 1;
		}
		if (pen_position(pen, count, values) != 0)
			goto bad;
		if (strcmp(word, "down") == 0)
			pen_add(pen, EV_KEY, BTN_TOUCH, 1);
		pen_add(pen, EV_SYN, SYN_REPORT, 0);
		return pen_flush(pen);
	}

	/* Steps the pressure at the current position. */
	if (strcmp(word, "ramp") == 0) {
		if (sscanf(line, "%*s %d %d %d %ld", &from, &to, &steps,
			   &milliseconds) != 4 || steps < 1 || steps > 100000 ||
		    milliseconds < 0 || !pen->in_range)
			goto bad;
		for (i = 0; i <= steps; i++) {
			pen->pressure = from + (int)((long)(to - from) * i / steps);
			pen_add(pen, EV_ABS, ABS_PRESSURE, pen->pressure);
			pen_add(pen, EV_SYN, SYN_REPORT, 0);
			if (pen_flush(pen) != 0)
				return -1;
			sleep_ms(milliseconds);
		}
		return 0;
	}

	/* Presses or releases a barrel button. */
	if (strcmp(word, "button") == 0) {
		if (sscanf(line, "%*s %15s %d", name, &values[0]) != 2)
			goto bad;
		if (strcmp(name, "stylus") == 0)
			pen_add(pen, EV_KEY, BTN_STYLUS, values[0]);
		else if (strcmp(name, "stylus2") == 0)
			pen_add(pen, EV_KEY, BTN_STYLUS2, values[0]);
		else
			goto bad;
		pen_add(pen, EV_SYN, SYN_REPORT, 0);
		return pen_flush(pen);
	}

	/* Lifts: pressure 0, touch 0, then the tool out of range. */
	if (strcmp(word, "up") == 0) {
		pen->pressure = 0;
		pen_add(pen, EV_ABS, ABS_PRESSURE, 0);
		pen_add(pen, EV_KEY, BTN_TOUCH, 0);
		pen_add(pen, EV_KEY, pen->tool, 0);
		pen_add(pen, EV_SYN, SYN_REPORT, 0);
		pen->in_range = 0;
		return pen_flush(pen);
	}

	/* Sleeps between commands, or keeps the pen before closing. */
	if (strcmp(word, "wait") == 0 || strcmp(word, "hold") == 0) {
		if (sscanf(line, "%*s %ld", &milliseconds) != 1 ||
		    milliseconds < 0)
			goto bad;
		sleep_ms(milliseconds);
		return 0;
	}

bad:
	/* Reports the line that could not be run. */
	fprintf(stderr, "peninject: line %u: bad command\n", number);
	return -1;
}

/* Declares the pen through the injector's setup record. */
static int
pen_declare(
	struct pen_state *pen,
	int width,
	int height)
{
	struct input_inject_setup setup;

	/* Writes the setup record. */
	memset(&setup, 0, sizeof(setup));
	setup.magic = INPUT_INJECT_MAGIC;
	setup.kind = INPUT_INJECT_KIND_PEN;
	setup.x_max = width;
	setup.y_max = height;
	if (write(pen->fd, &setup, sizeof(setup)) != (ssize_t)sizeof(setup)) {
		perror("peninject: declare");
		return -1;
	}
	pen->declared = 1;
	return 0;
}

/* Sets the position, pressure and tilt from a down or move line. */
static int
pen_position(
	struct pen_state *pen,
	int argc,
	int *values)
{
	/* Takes the values the line gave. */
	pen->x = values[0];
	pen->y = values[1];
	pen->pressure = values[2];
	if (argc == 5) {
		pen->tilt_x = values[3];
		pen->tilt_y = values[4];
	}

	/* Queues the axes; the kernel refuses values out of range. */
	pen_add(pen, EV_ABS, ABS_X, pen->x);
	pen_add(pen, EV_ABS, ABS_Y, pen->y);
	pen_add(pen, EV_ABS, ABS_PRESSURE, pen->pressure);
	pen_add(pen, EV_ABS, ABS_TILT_X, pen->tilt_x);
	pen_add(pen, EV_ABS, ABS_TILT_Y, pen->tilt_y);
	return 0;
}

/* Queues one event for the next write. */
static void
pen_add(
	struct pen_state *pen,
	int type,
	int code,
	int value)
{
	struct input_event *event;

	/* Keeps the batch within the injector's limit. */
	if (pen->count == PENINJECT_BATCH_MAX)
		return;
	event = &pen->batch[pen->count++];
	memset(event, 0, sizeof(*event));
	event->type = (uint16_t)type;
	event->code = (uint16_t)code;
	event->value = value;
}

/* Writes the queued events as one batch. */
static int
pen_flush(
	struct pen_state *pen)
{
	size_t size;

	/* Writes the batch; the kernel takes all of it or none. */
	size = pen->count * sizeof(pen->batch[0]);
	pen->count = 0;
	if (size == 0)
		return 0;
	if (write(pen->fd, pen->batch, size) != (ssize_t)size) {
		perror("peninject: write");
		return -1;
	}
	return 0;
}

/* Sleeps for a number of milliseconds. */
static void
sleep_ms(
	long milliseconds)
{
	struct timespec delay;

	/* Sleeps through interruptions. */
	delay.tv_sec = milliseconds / 1000;
	delay.tv_nsec = (milliseconds % 1000) * 1000000L;
	while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
		;
}
