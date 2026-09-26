/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zdesktop-terminal: a VT100 terminal in a Wayland window, drawn with Vulkan.
 *
 * It runs a shell (or a command) on a pseudo-terminal, shows what it writes
 * and sends it the keys typed.  The window follows the size the compositor
 * gives it, and the shell is told the grid's new size.  The terminal ends
 * when the shell exits or the window is closed.
 *
 * Its menus (menu.c) are drawn by zdesktop; what they choose is carried out
 * here: a new window (another terminal), closing, the selection and the
 * terminal's own clipboard (zdesktop has no clipboard between clients
 * yet), the font's size, fullscreen, keys for the shell, clearing and
 * resetting the screen, and a line about the terminal.
 *
 * Every outcome is one line on standard output: ZTERM DONE on a normal end
 * (with the reason), ZTERM FAILED naming what failed otherwise.
 */

#include "terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* The font the terminal uses unless told otherwise, and its size in pixels. */
#define MAIN_FONT		"/usr/share/fonts/zdesktop-mono.ttf"
#define MAIN_FONT_PIXELS	16U

/* The grid the window opens with. */
#define MAIN_COLUMNS		90U
#define MAIN_ROWS		28U

/* The shell run when no command is given. */
#define MAIN_SHELL		"/bin/sh"

/* How many redraws in a row may find the swapchain out of date before the terminal gives up. */
#define MAIN_STALE_LIMIT	8U

/* The program started for a new window when the terminal was not run by a path. */
#define MAIN_PROGRAM		"/bin/zdesktop-terminal"

/* The most text the clipboard holds: every cell as four UTF-8 bytes, and a line break per row. */
#define MAIN_CLIPBOARD_MAX	(TERMINAL_MAX_COLUMNS * TERMINAL_MAX_ROWS * 4U + TERMINAL_MAX_ROWS)

/* What Help > About Terminal writes on the screen. */
#define MAIN_ABOUT		"\r\n\033[1mTerminal\033[0m: a VT100 terminal for zdesktop (zedBSD), drawn with Vulkan.\r\n" \
				"Its menus are drawn by zdesktop's System Menu (xdg_toplevel_menu_v1, through libzdesktop).\r\n"

/*
 * What the command line asked for.
 */
struct main_options {
	const char *program;
	const char *display;
	const char *font;
	const char *command;
	const char *token;
	unsigned pixels;
	unsigned columns;
	unsigned rows;
	unsigned timeout;
};

/*
 * How the run is going: the shell, and what the error line names when
 * something fails.
 */
struct main_run {
	/* The shell's process and its pseudo-terminal (-1 when not started). */
	pid_t child;
	int master;

	/* The grid's size, as last told to the shell, and the font's size in pixels. */
	unsigned columns;
	unsigned rows;
	unsigned pixels;

	/* What failed last, the Vulkan result of it, and why the run ended normally. */
	const char *operation;
	VkResult result;
	const char *reason;
};

/*
 * The terminal's parts, for the whole run.  They are file-scope because the
 * grid alone is too large for the stack.
 */
static struct terminal_screen main_screen;
static struct terminal_font main_font;
static struct terminal_window main_window;
static struct terminal_renderer main_renderer;

/*
 * The terminal's clipboard (Edit > Copy) and its length, and the text being
 * pasted into the shell (Edit > Paste) with how much of it was written.
 * zdesktop has no clipboard between clients yet, so the clipboard is the
 * terminal's own and lives as long as it.
 */
static char main_clipboard[MAIN_CLIPBOARD_MAX];
static size_t main_clipboard_length;
static char main_paste[MAIN_CLIPBOARD_MAX];
static size_t main_paste_length;
static size_t main_paste_written;

static int main_parse(int argc, char **argv, struct main_options *options);
static const char *main_value(const char *argument, const char *name);
static int main_number(const char *text, unsigned maximum, unsigned *value);
static int main_start(const struct main_options *options, struct main_run *run);
static int main_loop(const struct main_options *options, struct main_run *run);
static int main_resize(const struct main_options *options, struct main_run *run);
static pid_t main_spawn(const struct main_options *options, int *master);
static void main_grid(unsigned width, unsigned height, unsigned *columns, unsigned *rows);
static void main_tell_size(int master, unsigned columns, unsigned rows);
static int main_read_shell(int master);
static int main_write_shell(int master);
static int main_menu_actions(const struct main_options *options, struct main_run *run);
static void main_menu_state(const struct main_run *run, struct terminal_menu_state *state);
static void main_new_window(const struct main_options *options, const struct main_run *run);
static int main_zoom(const struct main_options *options, struct main_run *run, unsigned pixels);
static void main_copy(void);
static void main_start_paste(void);

/*
 * Runs the terminal.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	struct main_run run;
	int status;
	int exit_status;

	/* The command line. */
	status = main_parse(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: zdesktop-terminal [--display=NAME] [--font=PATH] [--font-size=PIXELS] [--columns=N] [--rows=N] [--command=COMMAND] [--token=NAME] [--timeout-s=N]\n");
		return 2;
	}

	/* A write to a shell that has gone must not kill the terminal. */
	(void)signal(SIGPIPE, SIG_IGN);
	memset(&run, 0, sizeof(run));
	run.child = -1;
	run.master = -1;
	run.operation = "none";
	run.result = VK_SUCCESS;
	run.reason = "closed";

	/* The font, the window, the drawing and the shell; then the terminal runs until it ends. */
	status = main_start(&options, &run);
	if (status == 0)
		status = main_loop(&options, &run);

	/* One line says how the run ended. */
	exit_status = 0;
	if (status == 0) {
		printf("ZTERM DONE run=%s reason=%s\n", options.token, run.reason);
	} else {
		printf("ZTERM FAILED run=%s operation=%s result=%d errno=%d\n", options.token, run.operation, (int)run.result, errno);
		exit_status = 1;
	}

	/* The line reaches whoever reads the log before the terminal goes. */
	fflush(stdout);

	/* The shell, if it still runs, is hung up on and reaped. */
	if (run.master >= 0)
		(void)close(run.master);
	if (run.child > 0) {
		(void)kill(run.child, SIGHUP);
		(void)waitpid(run.child, &status, 0);
	}

	/* The drawing before the window it draws into, then the font. */
	terminal_renderer_close(&main_renderer);
	terminal_window_close(&main_window);
	terminal_font_close(&main_font);

	/* Reports how the terminal ended. */
	return exit_status;
}

/* Reads the command line into the options; returns nonzero for a malformed one. */
static int
main_parse(
	int argc,
	char **argv,
	struct main_options *options)
{
	const char *value;
	const char *slash;
	int index;
	int status;

	/* The defaults; a new window runs the program the way this one was run. */
	memset(options, 0, sizeof(*options));
	options->program = MAIN_PROGRAM;
	slash = NULL;
	if (argc > 0 && argv[0] != NULL)
		slash = strchr(argv[0], '/');
	if (slash != NULL)
		options->program = argv[0];
	options->display = NULL;
	options->font = MAIN_FONT;
	options->command = NULL;
	options->token = "term";
	options->pixels = MAIN_FONT_PIXELS;
	options->columns = MAIN_COLUMNS;
	options->rows = MAIN_ROWS;
	options->timeout = 0U;

	/* Each option in turn; the first name that matches takes it. */
	for (index = 1; index < argc; index++) {
		/* The compositor's socket. */
		value = main_value(argv[index], "--display=");
		if (value != NULL) {
			options->display = value;
			continue;
		}

		/* The font file. */
		value = main_value(argv[index], "--font=");
		if (value != NULL) {
			options->font = value;
			continue;
		}

		/* The font's size in pixels. */
		value = main_value(argv[index], "--font-size=");
		if (value != NULL) {
			status = main_number(value, 96U, &options->pixels);
			if (status != 0)
				return -1;
			continue;
		}

		/* The grid the window opens with. */
		value = main_value(argv[index], "--columns=");
		if (value != NULL) {
			status = main_number(value, TERMINAL_MAX_COLUMNS, &options->columns);
			if (status != 0)
				return -1;
			continue;
		}

		/* The rows the window opens with. */
		value = main_value(argv[index], "--rows=");
		if (value != NULL) {
			status = main_number(value, TERMINAL_MAX_ROWS, &options->rows);
			if (status != 0)
				return -1;
			continue;
		}

		/* A command run by the shell in place of an interactive shell. */
		value = main_value(argv[index], "--command=");
		if (value != NULL) {
			options->command = value;
			continue;
		}

		/* The name of the run in the log lines. */
		value = main_value(argv[index], "--token=");
		if (value != NULL) {
			options->token = value;
			continue;
		}

		/* A deadline in seconds, 0 for none. */
		value = main_value(argv[index], "--timeout-s=");
		if (value != NULL) {
			status = main_number(value, 86400U, &options->timeout);
			if (status != 0)
				return -1;
			continue;
		}

		/* An unknown option refuses the command line. */
		return -1;
	}

	/* A font too small, or an empty grid, is refused. */
	if (options->pixels < 6U || options->columns == 0U || options->rows == 0U)
		return -1;

	/* Succeeded: the options. */
	return 0;
}

/* Returns what follows an option's name in an argument, or NULL when the argument is another option. */
static const char *
main_value(
	const char *argument,
	const char *name)
{
	size_t length;
	int differs;

	/* The argument must start with the name, equals sign included. */
	length = strlen(name);
	differs = strncmp(argument, name, length);
	if (differs != 0)
		return NULL;

	/* Reports the value after the name. */
	return argument + length;
}

/* Opens the font, the window and the drawing, sizes the grid and starts the shell; returns nonzero on failure. */
static int
main_start(
	const struct main_options *options,
	struct main_run *run)
{
	struct terminal_menu_state state;
	int status;

	/* The font, which sets the cell's size. */
	run->operation = "terminal_font_open";
	status = terminal_font_open(&main_font, options->font, options->pixels);
	if (status != 0) {
		errno = status;
		return -1;
	}

	/* The window, sized for the grid asked for. */
	run->operation = "terminal_window_open";
	status = terminal_window_open(&main_window, options->display,
				      options->columns * main_font.cell_width + 2U * TERMINAL_PADDING,
				      options->rows * main_font.cell_height + 2U * TERMINAL_PADDING);
	if (status != 0)
		return -1;

	/* The drawing, at the size the compositor gave. */
	run->result = terminal_renderer_open(&main_renderer, &main_window, &main_font);
	run->operation = main_renderer.operation;
	if (run->result != VK_SUCCESS)
		return -1;

	/* The grid that fits the window. */
	run->pixels = options->pixels;
	main_grid(main_renderer.extent.width, main_renderer.extent.height, &run->columns, &run->rows);
	terminal_screen_init(&main_screen, run->columns, run->rows);

	/* The menus, which zdesktop draws (none from a compositor without the System Menu). */
	main_menu_state(run, &state);
	run->operation = "terminal_menu_open";
	status = terminal_menu_open(&main_window, &state);
	if (status != 0)
		return -1;

	/* The shell on a pseudo-terminal of that size. */
	run->operation = "forkpty";
	run->child = main_spawn(options, &run->master);
	if (run->child < 0)
		return -1;

	/* The shell learns the grid's size. */
	main_tell_size(run->master, run->columns, run->rows);

	/* Succeeded: the terminal is up. */
	printf("ZTERM START run=%s columns=%u rows=%u cell=%ux%u window=%ux%u\n",
	       options->token, run->columns, run->rows, main_font.cell_width, main_font.cell_height,
	       main_renderer.extent.width, main_renderer.extent.height);
	fflush(stdout);
	return 0;
}

/* Runs until the shell ends or the window is closed; returns nonzero on a failure. */
static int
main_loop(
	const struct main_options *options,
	struct main_run *run)
{
	struct terminal_menu_state state;
	uint64_t started;
	uint64_t now;
	unsigned stale;
	int status;
	int ready;
	int timeout;

	/* One round per event: wait, read the shell, send the keys, redraw what changed. */
	started = terminal_clock();
	stale = 0U;
	for (;;) {
		/* Waits for the compositor or the shell, or until the held key repeats. */
		timeout = 1000;
		now = terminal_clock();
		if (main_window.repeat_key != 0U) {
			timeout = 0;
			if (main_window.repeat_at > now)
				timeout = (int)(main_window.repeat_at - now);
		}

		/* Runs the compositor's events and learns whether the shell wrote. */
		run->operation = "terminal_window_dispatch";
		status = terminal_window_dispatch(&main_window, run->master, timeout, &ready);
		if (status != 0)
			return -1;

		/* The compositor closing the window ends the terminal. */
		if (main_window.closed) {
			run->reason = "closed";
			return 0;
		}

		/* The menus' choices are carried out; Close Window ends the terminal. */
		status = main_menu_actions(options, run);
		if (status < 0)
			return -1;
		if (status > 0) {
			run->reason = "menu-close";
			return 0;
		}

		/* What the shell wrote goes on the grid; its end ends the terminal. */
		if (ready) {
			status = main_read_shell(run->master);
			if (status != 0) {
				run->reason = "shell-exited";
				return 0;
			}
		}

		/* The held key repeats; a key typed ends the selection. */
		terminal_window_repeat(&main_window, terminal_clock());
		if (main_window.input_length != 0U && main_screen.selected) {
			main_screen.selected = 0;
			main_screen.changed = 1;
		}

		/* The keys typed and the text pasted go to the shell. */
		status = main_write_shell(run->master);
		if (status != 0) {
			run->reason = "shell-exited";
			return 0;
		}

		/* The deadline, when one was given, ends the terminal. */
		now = terminal_clock();
		if (options->timeout != 0U && now - started >= (uint64_t)options->timeout * 1000U) {
			run->reason = "timeout";
			return 0;
		}

		/* A new size remakes the swapchain and the grid, and tells the shell. */
		if (main_window.resized) {
			status = main_resize(options, run);
			if (status != 0)
				return -1;
		}

		/* The menus show the terminal's state (only a change is sent). */
		main_menu_state(run, &state);
		terminal_menu_refresh(&main_window, &state);

		/* Nothing changed: nothing to draw. */
		if (!main_screen.changed)
			continue;

		/* Draws the grid; a swapchain out of date is remade and drawn again next round. */
		run->result = terminal_renderer_draw(&main_renderer, &main_screen, &main_font);
		run->operation = main_renderer.operation;
		if (run->result == VK_ERROR_OUT_OF_DATE_KHR) {
			stale++;
			if (stale > MAIN_STALE_LIMIT)
				return -1;
			main_window.resized = 1;
			continue;
		}

		/* Any other failure ends the terminal. */
		if (run->result != VK_SUCCESS)
			return -1;

		/* The grid on the window is up to date. */
		stale = 0U;
		main_screen.changed = 0;
	}
}

/* Remakes the swapchain at the window's new size, fits the grid to it and tells the shell; returns nonzero on failure. */
static int
main_resize(
	const struct main_options *options,
	struct main_run *run)
{
	/* The swapchain at the new size. */
	main_window.resized = 0;
	run->result = terminal_renderer_resize(&main_renderer, main_window.width, main_window.height);
	run->operation = main_renderer.operation;
	if (run->result != VK_SUCCESS)
		return -1;

	/* The grid that fits it, told to the shell. */
	main_grid(main_renderer.extent.width, main_renderer.extent.height, &run->columns, &run->rows);
	terminal_screen_resize(&main_screen, run->columns, run->rows);
	main_tell_size(run->master, run->columns, run->rows);
	main_screen.changed = 1;

	/* Succeeded: the next frame is drawn at the new size. */
	printf("ZTERM RESIZE run=%s columns=%u rows=%u window=%ux%u\n", options->token, run->columns, run->rows, main_renderer.extent.width, main_renderer.extent.height);
	fflush(stdout);
	return 0;
}

/* Reads a decimal number no larger than a maximum; returns nonzero when it is not one. */
static int
main_number(
	const char *text,
	unsigned maximum,
	unsigned *value)
{
	unsigned long parsed;
	char *end;

	/* A number with nothing after it. */
	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0')
		return -1;

	/* Too large a value is refused. */
	if (parsed > maximum)
		return -1;

	/* Succeeded: the value. */
	*value = (unsigned)parsed;
	return 0;
}

/* Starts the shell (or the command) on a new pseudo-terminal; returns its process, or -1. */
static pid_t
main_spawn(
	const struct main_options *options,
	int *master)
{
	char *arguments[4];
	pid_t child;
	int flags;
	int status;

	/* The pseudo-terminal and the child whose controlling terminal it is. */
	child = forkpty(master, NULL, NULL, NULL);
	if (child < 0)
		return -1;

	/* The child becomes the shell, with the terminal's type in its environment. */
	if (child == 0) {
		(void)setenv("TERM", "xterm", 1);
		arguments[0] = MAIN_SHELL;
		arguments[1] = NULL;
		if (options->command != NULL) {
			arguments[1] = "-c";
			arguments[2] = (char *)options->command;
			arguments[3] = NULL;
		}

		/* Only a failed exec comes back. */
		(void)execv(MAIN_SHELL, arguments);
		_exit(127);
	}

	/* The terminal reads the shell without blocking, a burst at a time. */
	flags = fcntl(*master, F_GETFL);
	if (flags >= 0) {
		status = fcntl(*master, F_SETFL, flags | O_NONBLOCK);
		(void)status;
	}

	/* Succeeded: the shell runs. */
	return child;
}

/* Works out how many cells fit in the window, inside the padding. */
static void
main_grid(
	unsigned width,
	unsigned height,
	unsigned *columns,
	unsigned *rows)
{
	/* The window less the padding on both sides, in whole cells, at least one of each. */
	*columns = 1U;
	*rows = 1U;
	if (width > 2U * TERMINAL_PADDING + main_font.cell_width)
		*columns = (width - 2U * TERMINAL_PADDING) / main_font.cell_width;
	if (height > 2U * TERMINAL_PADDING + main_font.cell_height)
		*rows = (height - 2U * TERMINAL_PADDING) / main_font.cell_height;
	if (*columns > TERMINAL_MAX_COLUMNS)
		*columns = TERMINAL_MAX_COLUMNS;
	if (*rows > TERMINAL_MAX_ROWS)
		*rows = TERMINAL_MAX_ROWS;
}

/* Tells the shell the grid's size (it gets SIGWINCH). */
static void
main_tell_size(
	int master,
	unsigned columns,
	unsigned rows)
{
	struct winsize size;
	int status;

	/* The size in cells; the pixels are not given. */
	memset(&size, 0, sizeof(size));
	size.ws_col = (unsigned short)columns;
	size.ws_row = (unsigned short)rows;
	status = ioctl(master, TIOCSWINSZ, &size);
	(void)status;
}

/* Reads what the shell wrote and puts it on the grid; returns nonzero once the shell has gone. */
static int
main_read_shell(
	int master)
{
	unsigned char buffer[8192];
	ssize_t count;
	int rounds;

	/* Reads bursts until nothing is left, at most a few before the grid is drawn again. */
	for (rounds = 0; rounds < 16; rounds++) {
		count = read(master, buffer, sizeof(buffer));
		if (count > 0) {
			terminal_screen_write(&main_screen, buffer, (size_t)count);
			continue;
		}

		/* Nothing more for now. */
		if (count < 0 && (errno == EAGAIN || errno == EINTR))
			return 0;

		/* End of file, or EIO once the last user of the terminal has gone: the shell is gone. */
		return 1;
	}

	/* Succeeded: more may follow, read on the next round. */
	return 0;
}

/* Writes the keys typed to the shell; returns nonzero once the shell has gone. */
static int
main_write_shell(
	int master)
{
	ssize_t count;

	/* Text being pasted goes first, as much as the terminal takes. */
	if (main_paste_written < main_paste_length) {
		count = write(master, main_paste + main_paste_written, main_paste_length - main_paste_written);
		if (count < 0 && errno != EAGAIN && errno != EINTR)
			return 1;
		if (count > 0)
			main_paste_written += (size_t)count;
	}

	/* Nothing typed. */
	if (main_window.input_length == 0U)
		return 0;

	/* The bytes typed, all at once; a full terminal keeps the rest for the next round. */
	count = write(master, main_window.input, main_window.input_length);
	if (count < 0) {
		/* A full pseudo-terminal is tried again later; any other failure means the shell is gone. */
		if (errno == EAGAIN || errno == EINTR)
			return 0;
		return 1;
	}

	/* What was written leaves the buffer. */
	memmove(main_window.input, main_window.input + count, main_window.input_length - (size_t)count);
	main_window.input_length -= (size_t)count;

	/* Succeeded: the keys went to the shell. */
	return 0;
}

/*
 * Carries out the actions the menus chose.  Returns 1 when one closes the
 * terminal, -1 when one failed, 0 otherwise.
 */
static int
main_menu_actions(
	const struct main_options *options,
	struct main_run *run)
{
	uint32_t action;
	int status;

	/* Each action in the order it was chosen. */
	for (;;) {
		action = terminal_menu_take(&main_window);
		if (action == TERMINAL_ACTION_NONE)
			break;

		/* What the action does. */
		status = 0;
		switch (action) {
		case TERMINAL_ACTION_NEW_WINDOW:
			main_new_window(options, run);
			break;
		case TERMINAL_ACTION_CLOSE:
			/* The terminal ends. */
			return 1;
		case TERMINAL_ACTION_COPY:
			main_copy();
			break;
		case TERMINAL_ACTION_PASTE:
			main_start_paste();
			break;
		case TERMINAL_ACTION_SELECT_ALL:
			/* The whole screen is selected until a key is typed. */
			main_screen.selected = 1;
			main_screen.changed = 1;
			break;
		case TERMINAL_ACTION_ZOOM_IN:
			status = main_zoom(options, run, run->pixels + TERMINAL_PIXELS_STEP);
			break;
		case TERMINAL_ACTION_ZOOM_OUT:
			status = main_zoom(options, run, run->pixels - TERMINAL_PIXELS_STEP);
			break;
		case TERMINAL_ACTION_ZOOM_NORMAL:
			status = main_zoom(options, run, options->pixels);
			break;
		case TERMINAL_ACTION_SIZE_SMALL:
			status = main_zoom(options, run, TERMINAL_PIXELS_SMALL);
			break;
		case TERMINAL_ACTION_SIZE_MEDIUM:
			status = main_zoom(options, run, TERMINAL_PIXELS_MEDIUM);
			break;
		case TERMINAL_ACTION_SIZE_LARGE:
			status = main_zoom(options, run, TERMINAL_PIXELS_LARGE);
			break;
		case TERMINAL_ACTION_SIZE_HUGE:
			status = main_zoom(options, run, TERMINAL_PIXELS_HUGE);
			break;
		case TERMINAL_ACTION_FULLSCREEN:
			/* The compositor's configure says whether it happened. */
			if (main_window.fullscreen)
				terminal_window_set_fullscreen(&main_window, 0);
			else
				terminal_window_set_fullscreen(&main_window, 1);
			break;
		case TERMINAL_ACTION_INTERRUPT:
			/* ^C, as if typed. */
			terminal_window_type(&main_window, "\003", 1U);
			break;
		case TERMINAL_ACTION_END_OF_FILE:
			/* ^D, as if typed. */
			terminal_window_type(&main_window, "\004", 1U);
			break;
		case TERMINAL_ACTION_CLEAR:
			/* The cursor home and the screen erased. */
			terminal_screen_write(&main_screen, (const unsigned char *)"\033[H\033[2J", 7U);
			break;
		case TERMINAL_ACTION_RESET:
			/* The screen as it started, at its size. */
			terminal_screen_init(&main_screen, run->columns, run->rows);
			break;
		case TERMINAL_ACTION_ABOUT:
			/* A line about the terminal on the screen (there are no dialogs). */
			terminal_screen_write(&main_screen, (const unsigned char *)MAIN_ABOUT, sizeof(MAIN_ABOUT) - 1U);
			break;
		default:
			/* An action this terminal does not have. */
			break;
		}

		/* A failed action ends the terminal. */
		if (status != 0)
			return -1;
		printf("ZTERM ACTION run=%s action=%u\n", options->token, action);
		fflush(stdout);
	}

	/* Succeeded: every action was carried out. */
	return 0;
}

/* Describes the terminal's state for the menus. */
static void
main_menu_state(
	const struct main_run *run,
	struct terminal_menu_state *state)
{
	/* The selection, the clipboard, the font's size, and fullscreen. */
	memset(state, 0, sizeof(*state));
	state->selection = main_screen.selected;
	state->clipboard = 0;
	if (main_clipboard_length != 0U)
		state->clipboard = 1;
	state->pixels = run->pixels;
	state->fullscreen = main_window.fullscreen;
}

/*
 * Starts another terminal for a new window, the way this one was run.  It
 * is started through a child that ends at once, so that it is nobody's
 * child to wait for.
 */
static void
main_new_window(
	const struct main_options *options,
	const struct main_run *run)
{
	char token[64];
	char display[128];
	char *arguments[4];
	pid_t child;
	pid_t grandchild;
	int status;

	/* The child, which starts the terminal and ends. */
	child = fork();
	if (child < 0)
		return;
	if (child == 0) {
		/* The terminal's process, which keeps nothing of this terminal's. */
		grandchild = fork();
		if (grandchild != 0)
			_exit(0);
		if (run->master >= 0)
			(void)close(run->master);

		/* The same program and display, its log lines named after this run's. */
		(void)snprintf(token, sizeof(token), "--token=%s-new", options->token);
		arguments[0] = (char *)options->program;
		arguments[1] = token;
		arguments[2] = NULL;
		if (options->display != NULL) {
			(void)snprintf(display, sizeof(display), "--display=%s", options->display);
			arguments[2] = display;
			arguments[3] = NULL;
		}

		/* Only a failed exec comes back. */
		(void)execv(options->program, arguments);
		_exit(127);
	}

	/* The child ends at once. */
	(void)waitpid(child, &status, 0);
}

/*
 * Draws the text at another size (within TERMINAL_PIXELS_MIN and _MAX):
 * the grid that fits the window is made again and told to the shell.
 * Returns 0, or -1 when the font cannot be drawn at all any more.
 */
static int
main_zoom(
	const struct main_options *options,
	struct main_run *run,
	unsigned pixels)
{
	int error;

	/* The size stays within the bounds, and an unchanged size does nothing. */
	if (pixels < TERMINAL_PIXELS_MIN)
		pixels = TERMINAL_PIXELS_MIN;
	if (pixels > TERMINAL_PIXELS_MAX)
		pixels = TERMINAL_PIXELS_MAX;
	if (pixels == run->pixels)
		return 0;

	/* The font at the new size; a size it cannot give keeps the old one. */
	error = terminal_font_resize(&main_font, pixels);
	if (error == ENOMEM) {
		run->operation = "terminal_font_resize";
		errno = error;
		return -1;
	}

	/* A size the font cannot be measured at is not taken. */
	if (error != 0)
		return 0;
	run->pixels = pixels;

	/* The grid that fits the window at the new size, told to the shell. */
	main_grid(main_renderer.extent.width, main_renderer.extent.height, &run->columns, &run->rows);
	terminal_screen_resize(&main_screen, run->columns, run->rows);
	main_tell_size(run->master, run->columns, run->rows);
	main_screen.changed = 1;

	/* Succeeded: the next frame draws at the new size. */
	printf("ZTERM ZOOM run=%s pixels=%u columns=%u rows=%u\n", options->token, pixels, run->columns, run->rows);
	fflush(stdout);
	return 0;
}

/* Copies the screen's text to the clipboard (Edit > Copy, with the whole screen selected). */
static void
main_copy(void)
{
	/* Nothing is copied without a selection. */
	if (!main_screen.selected)
		return;

	/* The text, as it is on the screen now. */
	main_clipboard_length = terminal_screen_text(&main_screen, main_clipboard, sizeof(main_clipboard));
	printf("ZTERM COPY bytes=%lu\n", (unsigned long)main_clipboard_length);
	fflush(stdout);
}

/*
 * Starts pasting the clipboard into the shell (Edit > Paste): line breaks
 * become carriage returns, as the Enter key sends them.
 */
static void
main_start_paste(void)
{
	size_t index;

	/* The clipboard's text, line breaks turned into what Enter sends. */
	for (index = 0; index < main_clipboard_length; index++) {
		main_paste[index] = main_clipboard[index];
		if (main_paste[index] == '\n')
			main_paste[index] = '\r';
	}

	/* The main loop writes it as the shell takes it. */
	main_paste_length = main_clipboard_length;
	main_paste_written = 0;
	printf("ZTERM PASTE bytes=%lu\n", (unsigned long)main_paste_length);
	fflush(stdout);
}
