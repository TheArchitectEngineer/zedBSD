/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Notes, the handwritten notebook (plan/ws079/design-input-notes.md
 * section 5).
 *
 *   notes [--fullscreen] [--width=W] [--height=H] [--timeout-s=S] [FILE.pdf]
 *
 * One process shows one notebook of A4 pages.  The pointer (and, once the
 * compositor offers the tablet protocol, a pen) draws strokes with the
 * pen, a translucent highlighter, or erases whole strokes; Undo and Redo
 * take changes back and make them again; pages are added and turned.
 *
 * The notebook is saved as a PDF (save.c) when asked (Ctrl+S, the toolbar,
 * the menu), on its own NOTES_AUTOSAVE_IDLE_MS (5 seconds) after the last
 * change once no stroke is being drawn, and when Notes closes.  Every
 * change is also written to a journal before it is shown (journal.c), so
 * a crash or a killed process loses nothing: the next start recovers the
 * notebook from the journal -- the journal of FILE when one is given, or
 * the most recent journal when none is.  A new notebook is saved as
 * ~/Documents/Notes/note-YYYYMMDD-HHMMSS.pdf.
 *
 * Ctrl+N adds a page after the current one (one process is one notebook,
 * so a new page is what "new" makes).  FILE is opened from the edit data
 * its PDF carries (save.c); a PDF Notes cannot edit -- another program's,
 * or one whose pages another program changed -- is left as it is and a
 * new notebook starts.  Ctrl+O has no file chooser to open another file
 * with yet, and says so.
 *
 * --timeout-s ends Notes after that many seconds as if it were closed (the
 * tests use it to bound a run).  The lines starting with "NOTES" on the
 * standard output are what the tests read.
 */

#include "app.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The window's size when the compositor leaves it to Notes. */
#define MAIN_WIDTH		1024U
#define MAIN_HEIGHT		768U

/* The font the toolbar's labels are drawn with. */
#define MAIN_FONT		"/usr/share/fonts/keiland.ttf"

/* The longest path Notes keeps. */
#define MAIN_PATH_MAX		4096U

/* The eraser's radius, in points. */
#define MAIN_ERASER_RADIUS	10.0f

/* How close two samples may be before the second is dropped, in pixels. */
#define MAIN_SAMPLE_STEP	0.25f

/* How long a status stays on the toolbar, in milliseconds. */
#define MAIN_STATUS_MS		4000U

/* The evdev codes of the keys Notes handles itself. */
#define MAIN_KEY_ESC		1U
#define MAIN_KEY_W		17U
#define MAIN_KEY_E		18U
#define MAIN_KEY_Y		21U
#define MAIN_KEY_O		24U
#define MAIN_KEY_P		25U
#define MAIN_KEY_S		31U
#define MAIN_KEY_Z		44U
#define MAIN_KEY_N		49U
#define MAIN_KEY_M		50U
#define MAIN_KEY_F11		87U
#define MAIN_KEY_PAGE_UP	104U
#define MAIN_KEY_PAGE_DOWN	109U

/* What the current contact is doing. */
#define MAIN_CONTACT_NONE	0U
#define MAIN_CONTACT_TOOLBAR	1U
#define MAIN_CONTACT_DRAW	2U
#define MAIN_CONTACT_ERASE	3U

/*
 * Everything Notes holds for the one notebook it shows.
 */
struct notes_app {
	/* The window, its drawing, the toolbar and the frame being built. */
	struct notes_window window;
	struct notes_renderer renderer;
	struct notes_ui ui;
	struct notes_frame frame;
	struct notes_view view;

	/* The notebook, its file and the name shown in the title. */
	struct notes_document document;
	char path[MAIN_PATH_MAX];
	const char *name;

	/* The page shown, the tool (its NOTES_ACTION_*), the colour's and the width's index. */
	size_t page;
	unsigned tool;
	unsigned color;
	unsigned width;

	/* The contact under way, the stroke it draws (off the page until it ends), and when it started. */
	unsigned contact;
	struct notes_stroke *live;
	uint32_t live_start;

	/* When the notebook last changed (monotonic milliseconds). */
	uint64_t changed_at;

	/* The status on the toolbar, and until when (0: none). */
	char status[160];
	uint64_t status_until;

	/* Whether the frame and the toolbar need drawing again, and the width whose buttons were logged. */
	int redraw;
	int toolbar_dirty;
	uint32_t buttons_logged;
	int drawn;

	/* When Notes ends by itself (0: never), and whether it is ending. */
	uint64_t deadline;
	int quit;

	/*
	 * The frames drawn since the last NOTES FRAMES line: how many, and the
	 * sum and the longest of the time spent building their geometry and
	 * drawing them (microseconds).  The line goes out when a contact ends,
	 * so it tells what drawing a stroke costs a frame.
	 */
	unsigned long frame_count;
	uint64_t frame_build_us;
	uint64_t frame_draw_us;
	uint64_t frame_longest_us;
};

static int app_start_document(struct notes_app *app, const char *file);
static int app_new_path(char *path, size_t size);
static int app_make_folders(const char *path);
static void app_set_title(struct notes_app *app);
static void app_status(struct notes_app *app, const char *text);
static void app_state(const struct notes_app *app, struct notes_ui_state *state);
static void app_action(struct notes_app *app, uint32_t action);
static void app_key(struct notes_app *app, const struct notes_key *key);
static void app_input(struct notes_app *app, const struct notes_input *input);
static void app_sample(struct notes_app *app, const struct notes_input *input);
static void app_end_contact(struct notes_app *app, const struct notes_input *input);
static void app_changed(struct notes_app *app);
static int app_save(struct notes_app *app, const char *reason);
static void app_draw(struct notes_app *app);
static void app_frame_report(struct notes_app *app);
static int app_timeout(const struct notes_app *app, uint64_t now);
static uint64_t app_unix_ms(void);
static uint64_t app_microseconds(void);

/*
 * Runs Notes.
 */
int
main(
	int argc,
	char **argv)
{
	static struct notes_app app;
	const char *file;
	unsigned long value;
	uint64_t now;
	uint32_t width;
	uint32_t height;
	unsigned index;
	int fullscreen;
	int is_fullscreen;
	int is_width;
	int is_height;
	int is_timeout;
	int timeout;
	int status;
	int error;
	int arg;

	/* The options and the file. */
	file = NULL;
	fullscreen = 0;
	width = MAIN_WIDTH;
	height = MAIN_HEIGHT;
	for (arg = 1; arg < argc; arg++) {
		/* Which option the argument is, when it is one. */
		is_fullscreen = strcmp(argv[arg], "--fullscreen");
		is_width = strncmp(argv[arg], "--width=", 8U);
		is_height = strncmp(argv[arg], "--height=", 9U);
		is_timeout = strncmp(argv[arg], "--timeout-s=", 12U);

		/* Each option takes its value; anything else is the file. */
		if (is_fullscreen == 0) {
			fullscreen = 1;
		} else if (is_width == 0) {
			value = strtoul(argv[arg] + 8, NULL, 10);
			if (value >= 320U && value <= 8192U)
				width = (uint32_t)value;
		} else if (is_height == 0) {
			value = strtoul(argv[arg] + 9, NULL, 10);
			if (value >= 240U && value <= 8192U)
				height = (uint32_t)value;
		} else if (is_timeout == 0) {
			value = strtoul(argv[arg] + 12, NULL, 10);
			if (value != 0U)
				app.deadline = notes_clock() + (uint64_t)value * 1000U;
		} else if (argv[arg][0] == '-') {
			fprintf(stderr, "usage: notes [--fullscreen] [--width=W] [--height=H] [--timeout-s=S] [FILE.pdf]\n");
			return 2;
		} else {
			file = argv[arg];
		}
	}

	/* The notebook: recovered from a journal, or new. */
	app.tool = NOTES_ACTION_PEN;
	app.width = 1U;
	error = app_start_document(&app, file);
	if (error != 0) {
		fprintf(stderr, "notes: cannot start a notebook: %s\n", strerror(error));
		return 1;
	}

	/* The window. */
	status = notes_window_open(&app.window, width, height, fullscreen);
	if (status != 0) {
		fprintf(stderr, "notes: cannot open a window: %s\n", strerror(errno));
		return 1;
	}

	/* Its title names the file. */
	app_set_title(&app);

	/* The menus; without the System Menu the keys still work. */
	error = notes_menu_open(&app.window);
	if (error != 0)
		printf("NOTES MENU none error=%d\n", error);

	/* The drawing. */
	error = (int)notes_renderer_open(&app.renderer, &app.window);
	if (error != (int)VK_SUCCESS) {
		fprintf(stderr, "notes: %s failed (%d)\n", app.renderer.operation, error);
		notes_renderer_close(&app.renderer);
		notes_window_close(&app.window);
		return 1;
	}

	/* The toolbar's font; the buttons work without it. */
	error = notes_ui_open(&app.ui, MAIN_FONT);
	if (error != 0)
		printf("NOTES FONT none error=%d\n", error);

	/* The page's first place, before any input needs it. */
	notes_view_layout(&app.view, app.renderer.extent.width, app.renderer.extent.height,
			  app.document.pages[0]->width, app.document.pages[0]->height);

	/* The tests' first line. */
	printf("NOTES START width=%u height=%u fullscreen=%d pages=%lu strokes=%lu path=%s\n",
	       app.window.width, app.window.height, app.window.fullscreen,
	       (unsigned long)app.document.page_count, (unsigned long)notes_document_stroke_total(&app.document), app.path);
	fflush(stdout);
	app.redraw = 1;
	app.toolbar_dirty = 1;

	/* The main loop: wait, take what arrived, save when due, draw when needed. */
	while (!app.quit) {
		/* Waits for the compositor until the next thing that is due. */
		now = notes_clock();
		timeout = app_timeout(&app, now);
		status = notes_window_dispatch(&app.window, timeout);
		if (status != 0) {
			printf("NOTES DISCONNECTED\n");
			break;
		}

		/* A new size remakes the swapchain and the toolbar. */
		if (app.window.resized) {
			app.window.resized = 0;
			error = (int)notes_renderer_resize(&app.renderer, app.window.width, app.window.height);
			if (error != (int)VK_SUCCESS) {
				fprintf(stderr, "notes: %s failed (%d)\n", app.renderer.operation, error);
				break;
			}

			/* Everything is drawn again at the new size. */
			app.redraw = 1;
			app.toolbar_dirty = 1;
		}

		/* The menus' choices. */
		for (index = 0; index < app.window.action_count; index++)
			app_action(&app, app.window.actions[index]);
		app.window.action_count = 0;

		/* The keys. */
		for (index = 0; index < app.window.key_count; index++)
			app_key(&app, &app.window.keys[index]);
		app.window.key_count = 0;

		/* The pointer's and the pen's events. */
		for (index = 0; index < app.window.input_count; index++)
			app_input(&app, &app.window.inputs[index]);
		app.window.input_count = 0;

		/* The autosave, once the notebook has been still long enough and nothing is being drawn. */
		now = notes_clock();
		if (app.document.dirty &&
		    app.contact == MAIN_CONTACT_NONE &&
		    now >= app.changed_at + NOTES_AUTOSAVE_IDLE_MS)
			(void)app_save(&app, "autosave");

		/* A status whose time is up goes. */
		if (app.status_until != 0U && now >= app.status_until) {
			app.status[0] = '\0';
			app.status_until = 0;
			app.toolbar_dirty = 1;
			app.redraw = 1;
		}

		/* The compositor's close, or the end of the run. */
		if (app.window.closed)
			app.quit = 1;
		if (app.deadline != 0U && now >= app.deadline)
			app.quit = 1;

		/* A new frame when something changed. */
		if (app.redraw && !app.quit)
			app_draw(&app);
	}

	/* A stroke still being drawn is kept, and the notebook saved when it changed. */
	if (app.live != NULL)
		app_end_contact(&app, NULL);
	if (app.document.dirty)
		(void)app_save(&app, "close");

	/* The tests' last line. */
	printf("NOTES EXIT pages=%lu strokes=%lu dirty=%d\n", (unsigned long)app.document.page_count,
	       (unsigned long)notes_document_stroke_total(&app.document), app.document.dirty);
	fflush(stdout);

	/* Everything goes; the journal stays only when the last save failed. */
	notes_ui_close(&app.ui);
	notes_frame_free(&app.frame);
	notes_renderer_close(&app.renderer);
	notes_window_close(&app.window);
	notes_journal_destroy(app.document.journal);
	app.document.journal = NULL;
	notes_document_free(&app.document);

	/* Succeeded: Notes ran to its end. */
	return 0;
}

/*
 * Starts the notebook: the given file's journal, the most recent journal
 * when no file is given, or a new notebook.
 */
static int
app_start_document(
	struct notes_app *app,
	const char *file)
{
	char journal_path[MAIN_PATH_MAX];
	char recovered_path[MAIN_PATH_MAX];
	char folder[MAIN_PATH_MAX];
	struct stat status;
	const char *slash;
	const char *cwd;
	size_t records;
	int written;
	int exists;
	int found;
	int error;

	/* The file's absolute path, when a file is given. */
	app->path[0] = '\0';
	if (file != NULL) {
		if (file[0] == '/') {
			written = snprintf(app->path, sizeof(app->path), "%s", file);
		} else {
			/* A relative path is under the current folder. */
			cwd = getcwd(folder, sizeof(folder));
			if (cwd == NULL)
				return errno;
			written = snprintf(app->path, sizeof(app->path), "%s/%s", folder, file);
		}

		/* A path that did not fit is refused. */
		if (written < 0 || (size_t)written >= sizeof(app->path))
			return ENAMETOOLONG;
	}

	/* The journal to recover: the file's, when it is there, or the most recent one. */
	found = 0;
	if (app->path[0] != '\0') {
		error = notes_journal_path(app->path, journal_path, sizeof(journal_path));
		if (error == 0) {
			exists = stat(journal_path, &status);
			if (exists == 0)
				found = 1;
		}
	} else {
		error = notes_journal_newest(journal_path, sizeof(journal_path));
		if (error == 0)
			found = 1;
	}

	/* A journal rebuilds the notebook as it was when Notes last ran. */
	if (found) {
		error = notes_journal_recover(journal_path, &app->document, recovered_path, sizeof(recovered_path), &records);
		if (error == 0) {
			memcpy(app->path, recovered_path, strlen(recovered_path) + 1U);
			printf("NOTES RECOVER records=%lu pages=%lu strokes=%lu path=%s\n", (unsigned long)records,
			       (unsigned long)app->document.page_count, (unsigned long)notes_document_stroke_total(&app->document), app->path);
			(void)snprintf(app->status, sizeof(app->status), "Recovered %lu strokes", (unsigned long)notes_document_stroke_total(&app->document));
			app->status_until = notes_clock() + MAIN_STATUS_MS;
		} else {
			printf("NOTES RECOVER failed error=%d journal=%s\n", error, journal_path);
			found = 0;
		}
	}

	/* Whether the file is there, when no journal was recovered. */
	exists = -1;
	if (!found && app->path[0] != '\0')
		exists = stat(app->path, &status);

	/* A file that is there but has no journal is opened from its edit data. */
	if (exists == 0) {
		error = notes_open_pdf(app->path, &app->document);
		if (error == 0) {
			found = 1;
			printf("NOTES OPEN pages=%lu strokes=%lu path=%s\n", (unsigned long)app->document.page_count,
			       (unsigned long)notes_document_stroke_total(&app->document), app->path);
		} else {
			/* A file Notes cannot edit is left as it is, and a new notebook starts. */
			printf("NOTES OPEN failed error=%d path=%s\n", error, app->path);
			if (error == ENOENT)
				(void)snprintf(app->status, sizeof(app->status), "Not a Notes PDF; started a new note");
			else if (error == ESTALE)
				(void)snprintf(app->status, sizeof(app->status), "Changed by another program; started a new note");
			else
				(void)snprintf(app->status, sizeof(app->status), "Cannot open the PDF; started a new note");
			app->status_until = notes_clock() + MAIN_STATUS_MS;
			app->path[0] = '\0';
		}
	}

	/* Otherwise a new notebook, at the given path or a new one. */
	if (!found) {
		error = notes_document_init(&app->document, app_unix_ms());
		if (error != 0)
			return error;
		if (app->path[0] == '\0') {
			error = app_new_path(app->path, sizeof(app->path));
			if (error != 0)
				return error;
		}
	}

	/* The journal every change goes to. */
	app->document.journal = notes_journal_create(app->path);
	if (app->document.journal == NULL)
		printf("NOTES JOURNAL none error=%d\n", errno);

	/* The name shown in the title: the file's last part. */
	slash = strrchr(app->path, '/');
	app->name = app->path;
	if (slash != NULL)
		app->name = slash + 1;

	/* Succeeded: the notebook is ready. */
	app->changed_at = notes_clock();
	return 0;
}

/* Writes the path of a new notebook: ~/Documents/Notes/note-YYYYMMDD-HHMMSS.pdf. */
static int
app_new_path(
	char *path,
	size_t size)
{
	struct tm local;
	struct tm *converted;
	const char *home;
	time_t now;
	int written;

	/* The home directory, or /tmp without one. */
	home = getenv("HOME");
	if (home == NULL || home[0] != '/')
		home = "/tmp";

	/* The time the notebook was made names it (the epoch when the local time cannot be told). */
	now = time(NULL);
	converted = localtime_r(&now, &local);
	if (converted == NULL)
		memset(&local, 0, sizeof(local));
	written = snprintf(path, size, "%s/Documents/Notes/note-%04d%02d%02d-%02d%02d%02d.pdf", home,
			   local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the path (its folder is made at the first save). */
	return 0;
}

/* Makes the folders of a file's path that are missing. */
static int
app_make_folders(
	const char *path)
{
	char folder[MAIN_PATH_MAX];
	char *slash;
	size_t length;
	int status;

	/* The path, to cut at each slash. */
	length = strlen(path);
	if (length >= sizeof(folder))
		return ENAMETOOLONG;
	memcpy(folder, path, length + 1U);

	/* Each folder from the root down to the file's; one that exists is passed. */
	for (slash = folder + 1; *slash != '\0'; slash++) {
		if (*slash != '/')
			continue;
		*slash = '\0';
		status = mkdir(folder, 0755);
		*slash = '/';
		if (status != 0 && errno != EEXIST)
			return errno;
	}

	/* Succeeded: the file's folder is there. */
	return 0;
}

/* Sets the window's title: "Notes", a dash and the file's name. */
static void
app_set_title(
	struct notes_app *app)
{
	char title[MAIN_PATH_MAX];

	/* The title with the file's name. */
	(void)snprintf(title, sizeof(title), "Notes \xe2\x80\x94 %s", app->name);
	notes_window_set_title(&app->window, title);
}

/* Shows a status on the toolbar for a while. */
static void
app_status(
	struct notes_app *app,
	const char *text)
{
	/* The text and its time. */
	(void)snprintf(app->status, sizeof(app->status), "%s", text);
	app->status_until = notes_clock() + MAIN_STATUS_MS;
	app->toolbar_dirty = 1;
	app->redraw = 1;
}

/* Gathers what the toolbar and the menus show. */
static void
app_state(
	const struct notes_app *app,
	struct notes_ui_state *state)
{
	/* The tool, colour, width, pages, history, changes and fullscreen. */
	memset(state, 0, sizeof(*state));
	state->tool = app->tool;
	state->color = app->color;
	state->width = app->width;
	state->page = app->page;
	state->page_count = app->document.page_count;
	state->dirty = app->document.dirty;

	/* Undo while a change stands, redo while one was taken back. */
	if (app->document.undo_done > 0U)
		state->can_undo = 1;
	if (app->document.undo_done < app->document.undo_count)
		state->can_redo = 1;

	/* The window's state and the status line. */
	state->fullscreen = app->window.fullscreen;
	state->status = app->status;
}

/* Carries out an action of the toolbar, a menu or a key. */
static void
app_action(
	struct notes_app *app,
	uint32_t action)
{
	size_t page;
	int error;

	/* A colour or a width chooses by its index. */
	if (action >= NOTES_ACTION_COLOR && action < NOTES_ACTION_COLOR + NOTES_COLORS) {
		app->color = action - NOTES_ACTION_COLOR;
		if (app->tool == NOTES_ACTION_ERASER)
			app->tool = NOTES_ACTION_PEN;
		app->toolbar_dirty = 1;
		app->redraw = 1;
		return;
	}

	/* A width, by its index. */
	if (action >= NOTES_ACTION_WIDTH && action < NOTES_ACTION_WIDTH + NOTES_WIDTHS) {
		app->width = action - NOTES_ACTION_WIDTH;
		app->toolbar_dirty = 1;
		app->redraw = 1;
		return;
	}

	/* The other actions. */
	switch (action) {
	case NOTES_ACTION_PEN:
	case NOTES_ACTION_HIGHLIGHTER:
	case NOTES_ACTION_ERASER:
		/* The tool. */
		app->tool = action;
		printf("NOTES TOOL %u\n", action);
		break;
	case NOTES_ACTION_UNDO:
		/* The last change is taken back, and its page shown. */
		error = notes_document_undo(&app->document, &page);
		if (error != 0)
			break;
		if (page >= app->document.page_count)
			page = app->document.page_count - 1U;
		app->page = page;
		printf("NOTES UNDO page=%lu strokes=%lu pages=%lu\n", (unsigned long)app->page,
		       (unsigned long)app->document.pages[app->page]->stroke_count, (unsigned long)app->document.page_count);
		app_changed(app);
		break;
	case NOTES_ACTION_REDO:
		/* The last change taken back is made again, and its page shown. */
		error = notes_document_redo(&app->document, &page);
		if (error != 0)
			break;
		if (page >= app->document.page_count)
			page = app->document.page_count - 1U;
		app->page = page;
		printf("NOTES REDO page=%lu strokes=%lu pages=%lu\n", (unsigned long)app->page,
		       (unsigned long)app->document.pages[app->page]->stroke_count, (unsigned long)app->document.page_count);
		app_changed(app);
		break;
	case NOTES_ACTION_PREVIOUS_PAGE:
		/* The page before, when there is one. */
		if (app->page > 0U)
			app->page--;
		printf("NOTES PAGE current=%lu count=%lu\n", (unsigned long)app->page, (unsigned long)app->document.page_count);
		break;
	case NOTES_ACTION_NEXT_PAGE:
		/* The page after, when there is one. */
		if (app->page + 1U < app->document.page_count)
			app->page++;
		printf("NOTES PAGE current=%lu count=%lu\n", (unsigned long)app->page, (unsigned long)app->document.page_count);
		break;
	case NOTES_ACTION_NEW_PAGE:
		/* A blank page after the current one, shown. */
		error = notes_document_add_page(&app->document, app->page + 1U);
		if (error != 0) {
			app_status(app, "Could not add a page");
			break;
		}

		/* The new page is shown. */
		app->page++;
		printf("NOTES PAGE current=%lu count=%lu new\n", (unsigned long)app->page, (unsigned long)app->document.page_count);
		app_changed(app);
		break;
	case NOTES_ACTION_SAVE:
		/* Saved now, even when nothing changed. */
		(void)app_save(app, "request");
		break;
	case NOTES_ACTION_OPEN:
		/* Opening another notebook needs a file chooser, which Notes does not have yet. */
		printf("NOTES OPEN chooser unsupported\n");
		app_status(app, "Opening from Notes is not available yet");
		break;
	case NOTES_ACTION_CLOSE:
		/* The main loop saves and ends. */
		app->quit = 1;
		break;
	case NOTES_ACTION_FULLSCREEN:
		/* Fullscreen on or off; the configure that follows redraws. */
		notes_window_set_fullscreen(&app->window, !app->window.fullscreen);
		break;
	case NOTES_ACTION_LEAVE_FULLSCREEN:
		/* Back to a window, when fullscreen. */
		if (app->window.fullscreen)
			notes_window_set_fullscreen(&app->window, 0);
		break;
	default:
		break;
	}

	/* The toolbar and the page show the result. */
	fflush(stdout);
	app->toolbar_dirty = 1;
	app->redraw = 1;
}

/* Turns a key the menus did not take into an action. */
static void
app_key(
	struct notes_app *app,
	const struct notes_key *key)
{
	int control;
	int shift;

	/* The modifiers that choose a shortcut. */
	control = 0;
	if ((key->modifiers & NOTES_MODIFIER_CONTROL) != 0U)
		control = 1;
	shift = 0;
	if ((key->modifiers & NOTES_MODIFIER_SHIFT) != 0U)
		shift = 1;

	/* The standard shortcuts with Control. */
	if (control) {
		switch (key->key) {
		case MAIN_KEY_S:
			app_action(app, NOTES_ACTION_SAVE);
			break;
		case MAIN_KEY_Z:
			if (shift)
				app_action(app, NOTES_ACTION_REDO);
			else
				app_action(app, NOTES_ACTION_UNDO);
			break;
		case MAIN_KEY_Y:
			app_action(app, NOTES_ACTION_REDO);
			break;
		case MAIN_KEY_N:
			app_action(app, NOTES_ACTION_NEW_PAGE);
			break;
		case MAIN_KEY_O:
			app_action(app, NOTES_ACTION_OPEN);
			break;
		case MAIN_KEY_W:
			app_action(app, NOTES_ACTION_CLOSE);
			break;
		default:
			break;
		}

		/* A key with Control is no tool key. */
		return;
	}

	/* The keys without Control. */
	switch (key->key) {
	case MAIN_KEY_PAGE_UP:
		app_action(app, NOTES_ACTION_PREVIOUS_PAGE);
		break;
	case MAIN_KEY_PAGE_DOWN:
		app_action(app, NOTES_ACTION_NEXT_PAGE);
		break;
	case MAIN_KEY_P:
		app_action(app, NOTES_ACTION_PEN);
		break;
	case MAIN_KEY_M:
		app_action(app, NOTES_ACTION_HIGHLIGHTER);
		break;
	case MAIN_KEY_E:
		app_action(app, NOTES_ACTION_ERASER);
		break;
	case MAIN_KEY_F11:
		app_action(app, NOTES_ACTION_FULLSCREEN);
		break;
	case MAIN_KEY_ESC:
		app_action(app, NOTES_ACTION_LEAVE_FULLSCREEN);
		break;
	default:
		break;
	}
}

/* Takes one input event: a press on the toolbar, a stroke, or an eraser drag. */
static void
app_input(
	struct notes_app *app,
	const struct notes_input *input)
{
	struct notes_stroke *stroke;
	uint32_t action;
	unsigned tool;
	uint32_t id;
	uint32_t color;
	float width;

	/* A motion or an end belongs to the contact under way. */
	if (input->kind == NOTES_INPUT_MOTION) {
		if (app->contact == MAIN_CONTACT_DRAW || app->contact == MAIN_CONTACT_ERASE)
			app_sample(app, input);
		return;
	}

	/* An end of contact ends it. */
	if (input->kind == NOTES_INPUT_UP) {
		app_end_contact(app, input);
		return;
	}

	/* A contact that starts while one is under way (a lost release) ends the old one first. */
	if (app->contact != MAIN_CONTACT_NONE)
		app_end_contact(app, NULL);

	/* The frame times count from the contact's start. */
	app->frame_count = 0;
	app->frame_build_us = 0;
	app->frame_draw_us = 0;
	app->frame_longest_us = 0;

	/* A press on the toolbar is its button's. */
	if (input->y < (float)NOTES_TOOLBAR_HEIGHT) {
		app->contact = MAIN_CONTACT_TOOLBAR;
		action = notes_ui_hit(&app->ui, input->x, input->y);
		if (action != NOTES_ACTION_NONE)
			app_action(app, action);
		return;
	}

	/* The eraser tool, or a pen's eraser end, erases. */
	if (app->tool == NOTES_ACTION_ERASER || input->source == NOTES_SOURCE_ERASER) {
		app->contact = MAIN_CONTACT_ERASE;
		notes_document_erase_begin(&app->document);
		app_sample(app, input);
		return;
	}

	/* Otherwise a stroke starts, in the tool's colour and width. */
	tool = NOTES_TOOL_PEN;
	if (app->tool == NOTES_ACTION_HIGHLIGHTER)
		tool = NOTES_TOOL_HIGHLIGHTER;
	color = notes_ui_color(tool, app->color);
	width = notes_ui_width(tool, app->width);
	id = app->document.next_id;
	stroke = notes_stroke_create(id, tool, color, width, app_unix_ms());
	if (stroke == NULL)
		return;

	/* The stroke's number is taken, and its first sample. */
	app->document.next_id++;
	app->live = stroke;
	app->live_start = input->time_ms;
	app->contact = MAIN_CONTACT_DRAW;
	app_sample(app, input);
}

/* Adds a sample to the stroke being drawn, or erases at it. */
static void
app_sample(
	struct notes_app *app,
	const struct notes_input *input)
{
	struct notes_point point;
	struct notes_point *last;
	size_t removed;
	float pressure;
	float dx;
	float dy;
	float step;
	int error;

	/* The place on the page, in points. */
	memset(&point, 0, sizeof(point));
	point.x = (input->x - app->view.x) / app->view.scale;
	point.y = (input->y - app->view.y) / app->view.scale;

	/* An eraser removes the strokes its circle touches. */
	if (app->contact == MAIN_CONTACT_ERASE) {
		error = notes_document_erase_at(&app->document, app->page, point.x, point.y, MAIN_ERASER_RADIUS, &removed);
		if (error == 0 && removed != 0U) {
			printf("NOTES ERASE page=%lu removed=%lu strokes=%lu\n", (unsigned long)app->page, (unsigned long)removed,
			       (unsigned long)app->document.pages[app->page]->stroke_count);
			fflush(stdout);
			app_changed(app);
		}

		/* An eraser draws no stroke. */
		return;
	}

	/* A sample too close to the last one adds nothing. */
	if (app->live->point_count != 0U) {
		last = &app->live->points[app->live->point_count - 1U];
		dx = (point.x - last->x) * app->view.scale;
		dy = (point.y - last->y) * app->view.scale;
		step = dx * dx + dy * dy;
		if (step < MAIN_SAMPLE_STEP * MAIN_SAMPLE_STEP)
			return;
	}

	/* The pressure, the tilt (in 1/100 degree) and the time since the stroke began. */
	pressure = input->pressure;
	if (!(pressure >= 0.0f))
		pressure = 0.0f;
	if (pressure > 1.0f)
		pressure = 1.0f;
	point.pressure = (uint16_t)(pressure * (float)NOTES_PRESSURE_MAX + 0.5f);
	point.tilt_x = (int16_t)(input->tilt_x * 100.0f);
	point.tilt_y = (int16_t)(input->tilt_y * 100.0f);
	point.time_ms = input->time_ms - app->live_start;
	if (input->source != NOTES_SOURCE_POINTER)
		app->live->has_tilt = 1;

	/* Succeeded or not, the stroke is drawn again with what it has. */
	(void)notes_stroke_append(app->live, &point);
	app->redraw = 1;
}

/* Ends the contact under way: a stroke goes on the page, an eraser drag becomes one change. */
static void
app_end_contact(
	struct notes_app *app,
	const struct notes_input *input)
{
	struct notes_stroke *stroke;
	unsigned lowest;
	unsigned highest;
	size_t index;
	int error;

	/* The last sample of a stroke or an eraser drag. */
	if (input != NULL &&
	    (app->contact == MAIN_CONTACT_DRAW ||
	     app->contact == MAIN_CONTACT_ERASE))
		app_sample(app, input);

	/* A finished stroke goes on top of the page. */
	if (app->contact == MAIN_CONTACT_DRAW && app->live != NULL) {
		stroke = app->live;
		app->live = NULL;
		error = notes_document_add_stroke(&app->document, app->page, stroke);
		if (error != 0) {
			notes_stroke_free(stroke);
			app_status(app, "Could not keep the stroke");
		} else {
			/* The range of the stroke's pressure, for the tests' line. */
			lowest = NOTES_PRESSURE_MAX;
			highest = 0;
			for (index = 0; index < stroke->point_count; index++) {
				if (stroke->points[index].pressure < lowest)
					lowest = stroke->points[index].pressure;
				if (stroke->points[index].pressure > highest)
					highest = stroke->points[index].pressure;
			}

			/* The tests' line. */
			printf("NOTES STROKE page=%lu id=%u tool=%u points=%lu strokes=%lu pressure=%u..%u tilt=%d\n", (unsigned long)app->page,
			       stroke->id, stroke->tool, (unsigned long)stroke->point_count, (unsigned long)app->document.pages[app->page]->stroke_count,
			       lowest, highest, stroke->has_tilt);
			fflush(stdout);
			app_changed(app);
		}
	}

	/* An eraser drag's removals are one change from now on. */
	if (app->contact == MAIN_CONTACT_ERASE)
		notes_document_erase_end(&app->document);

	/* The frames the contact took, for the tests' line. */
	app_frame_report(app);

	/* No contact is under way. */
	app->contact = MAIN_CONTACT_NONE;
	app->redraw = 1;
}

/* Notes a change of the notebook: the autosave waits from now, and the toolbar shows it. */
static void
app_changed(
	struct notes_app *app)
{
	/* The time of the change. */
	app->changed_at = notes_clock();
	app->toolbar_dirty = 1;
	app->redraw = 1;
}

/*
 * Saves the notebook as its PDF, and removes the journal the save makes
 * unnecessary.  Returns 0, or an errno value.
 */
static int
app_save(
	struct notes_app *app,
	const char *reason)
{
	char text[160];
	size_t bytes;
	int error;

	/* The file's folder, then the file. */
	bytes = 0;
	error = app_make_folders(app->path);
	if (error == 0)
		error = notes_save_pdf(&app->document, app->path, &bytes);

	/* A failure is shown and logged, and the journal keeps the changes. */
	if (error != 0) {
		printf("NOTES SAVE failed reason=%s error=%d path=%s\n", reason, error, app->path);
		fflush(stdout);
		(void)snprintf(text, sizeof(text), "Could not save: %s", strerror(error));
		app_status(app, text);
		return error;
	}

	/* The journal is no longer needed; the next change starts another. */
	if (app->document.journal != NULL)
		(void)notes_journal_discard(app->document.journal);

	/* The file is among the recent ones. */
	(void)keiland_recent_add(app->path, "notes");

	/* The tests' line and the status. */
	printf("NOTES SAVE reason=%s pages=%lu strokes=%lu bytes=%lu path=%s\n", reason, (unsigned long)app->document.page_count,
	       (unsigned long)notes_document_stroke_total(&app->document), (unsigned long)bytes, app->path);
	fflush(stdout);
	app_status(app, "Saved");

	/* Succeeded: the file is the notebook as it stands. */
	return 0;
}

/* Builds and draws a frame: the desk, the page and its strokes, the stroke being drawn, the toolbar. */
static void
app_draw(
	struct notes_app *app)
{
	struct notes_ui_state state;
	struct notes_view view;
	struct notes_page *page;
	unsigned char *pixels;
	uint64_t started;
	uint64_t built;
	uint64_t finished;
	size_t pitch;
	size_t index;
	int error;

	/* When the frame starts, for the frame times. */
	started = app_microseconds();

	/* The page's place; a new place (and the first) is logged for the tests. */
	page = app->document.pages[app->page];
	notes_view_layout(&view, app->renderer.extent.width, app->renderer.extent.height, page->width, page->height);
	if (!app->drawn ||
	    view.x != app->view.x ||
	    view.y != app->view.y ||
	    view.scale != app->view.scale) {
		printf("NOTES LAYOUT window=%ux%u page=%d,%d,%d,%d scale=%.4f\n", app->renderer.extent.width, app->renderer.extent.height,
		       (int)view.x, (int)view.y, (int)(page->width * view.scale), (int)(page->height * view.scale), (double)view.scale);
		fflush(stdout);
	}

	/* The place the input is measured against. */
	app->view = view;

	/* The toolbar, drawn again when its state changed; the menus show the same state. */
	if (app->toolbar_dirty) {
		app_state(app, &state);
		notes_renderer_toolbar(&app->renderer, &pixels, &pitch);
		notes_ui_draw(&app->ui, pixels, pitch, app->renderer.extent.width, NOTES_TOOLBAR_HEIGHT, &state);
		notes_menu_refresh(&app->window, &state);
		app->toolbar_dirty = 0;

		/* The buttons' places, logged for the tests once per width. */
		if (app->buttons_logged != app->renderer.extent.width) {
			app->buttons_logged = app->renderer.extent.width;
			printf("NOTES BUTTONS");
			for (index = 0; index < app->ui.button_count; index++) {
				printf(" %u:%d,%d,%d,%d", app->ui.buttons[index].action, app->ui.buttons[index].x, app->ui.buttons[index].y,
				       app->ui.buttons[index].width, app->ui.buttons[index].height);
			}

			/* The line ends. */
			printf("\n");
			fflush(stdout);
		}
	}

	/* The page with a soft shadow. */
	notes_frame_begin(&app->frame);
	notes_frame_rect(&app->frame, view.x + 2.0f, view.y + 3.0f, page->width * view.scale, page->height * view.scale, 0x0000002eU);
	notes_frame_rect(&app->frame, view.x, view.y, page->width * view.scale, page->height * view.scale, 0xffffffffU);

	/* The strokes, bottom first, clipped to the page. */
	notes_frame_clip(&app->frame, 1, view.x, view.y, page->width * view.scale, page->height * view.scale);
	for (index = 0; index < page->stroke_count; index++) {
		error = notes_stroke_outline(page->strokes[index]);
		if (error != 0)
			continue;
		notes_frame_polygon(&app->frame, page->strokes[index]->outline, page->strokes[index]->outline_count, &view, page->strokes[index]->color);
	}

	/* The stroke being drawn, on top. */
	if (app->live != NULL && app->live->point_count != 0U) {
		error = notes_stroke_outline(app->live);
		if (error == 0)
			notes_frame_polygon(&app->frame, app->live->outline, app->live->outline_count, &view, app->live->color);
	}

	/* The toolbar across the top. */
	notes_frame_clip(&app->frame, 0, 0.0f, 0.0f, 0.0f, 0.0f);
	notes_frame_texture(&app->frame, 0.0f, 0.0f, (float)app->renderer.extent.width, (float)NOTES_TOOLBAR_HEIGHT);

	/* The frame; a swapchain out of date is remade and drawn next time. */
	if (app->frame.error != 0) {
		printf("NOTES DRAW out of memory\n");
		return;
	}

	/* Draws it, after the time the geometry took. */
	built = app_microseconds();
	error = (int)notes_renderer_draw(&app->renderer, &app->frame);
	if (error == (int)VK_ERROR_OUT_OF_DATE_KHR) {
		app->window.resized = 1;
		return;
	}

	/* Any other failure ends Notes. */
	if (error != (int)VK_SUCCESS) {
		fprintf(stderr, "notes: %s failed (%d)\n", app->renderer.operation, error);
		app->quit = 1;
		return;
	}

	/* The frame's times join the ones the next NOTES FRAMES line reports. */
	finished = app_microseconds();
	app->frame_count++;
	app->frame_build_us += built - started;
	app->frame_draw_us += finished - built;
	if (finished - started > app->frame_longest_us)
		app->frame_longest_us = finished - started;

	/* Succeeded: the frame is shown (the first is logged for the tests). */
	if (!app->drawn) {
		printf("NOTES FRAME first draws=%lu\n", (unsigned long)app->frame.draw_count);
		fflush(stdout);
	}

	/* Nothing waits to be drawn. */
	app->drawn = 1;
	app->redraw = 0;
}

/*
 * Logs the frames drawn since the last report -- their count, the average
 * time their geometry and their drawing took, and the longest frame -- and
 * starts counting again.
 */
static void
app_frame_report(
	struct notes_app *app)
{
	unsigned long build_average;
	unsigned long draw_average;

	/* A contact that drew no frame has nothing to report. */
	if (app->frame_count == 0U)
		return;

	/* The averages, in microseconds. */
	build_average = (unsigned long)(app->frame_build_us / app->frame_count);
	draw_average = (unsigned long)(app->frame_draw_us / app->frame_count);

	/* The tests' line. */
	printf("NOTES FRAMES count=%lu strokes=%lu build_us=%lu draw_us=%lu frame_us=%lu longest_us=%lu\n", app->frame_count,
	       (unsigned long)app->document.pages[app->page]->stroke_count, build_average, draw_average,
	       build_average + draw_average, (unsigned long)app->frame_longest_us);
	fflush(stdout);

	/* The next report counts from here. */
	app->frame_count = 0;
	app->frame_build_us = 0;
	app->frame_draw_us = 0;
	app->frame_longest_us = 0;
}

/* Tells how long the main loop may wait, in milliseconds (-1: until the compositor speaks). */
static int
app_timeout(
	const struct notes_app *app,
	uint64_t now)
{
	uint64_t due;
	uint64_t wait;

	/* The next thing due: the autosave, the status's end, the end of the run. */
	due = 0;
	if (app->document.dirty && app->contact == MAIN_CONTACT_NONE)
		due = app->changed_at + NOTES_AUTOSAVE_IDLE_MS;
	if (app->status_until != 0U &&
	    (due == 0U ||
	     app->status_until < due))
		due = app->status_until;
	if (app->deadline != 0U &&
	    (due == 0U ||
	     app->deadline < due))
		due = app->deadline;

	/* A frame waiting to be drawn is due now. */
	if (app->redraw)
		return 0;

	/* Nothing is due: wait for the compositor. */
	if (due == 0U)
		return -1;

	/* Something past due runs now. */
	if (due <= now)
		return 0;

	/* Reports the wait, within an int. */
	wait = due - now;
	if (wait > 1000000U)
		wait = 1000000U;
	return (int)wait;
}

/* Returns the time of day in UNIX milliseconds. */
static uint64_t
app_unix_ms(void)
{
	struct timespec now;
	int status;

	/* The real-time clock. */
	status = clock_gettime(CLOCK_REALTIME, &now);
	if (status != 0)
		return 0U;

	/* Reports it in milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Returns the monotonic time in microseconds. */
static uint64_t
app_microseconds(void)
{
	struct timespec now;
	int status;

	/* The monotonic clock. */
	status = clock_gettime(CLOCK_MONOTONIC, &now);
	if (status != 0)
		return 0U;

	/* Reports it in microseconds. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}
