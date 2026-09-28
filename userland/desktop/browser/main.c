/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * browser: the Web browser of the zedBSD desktop.
 *
 *   browser [--display=NAME] [--width=N] [--height=N] [--ca-file=PEM] [URL]
 *   browser --dump=dom|style|layout|paint [--width=N] [--height=N] [--font=PATH] FILE
 *   browser --run [--width=N] [--height=N] FILE
 *   browser --dump=ast [--module] [--strict] FILE.js
 *   browser --js [--strict] FILE.js
 *   browser --dump=code [--strict] FILE.js
 *   browser --render|--render-gpu --output=OUT.ppm [--width=N] [--height=N] [--font=PATH] FILE
 *   browser --version | --help
 *
 * Without a headless mode it opens a zdesktop window on URL, or on the
 * start page the package installs (MAIN_START_PAGE).  The headless
 * modes (added with the engine, one per phase) draw or dump a page, or run
 * a script, without a window; the tests use them on the host and in the
 * guest.  The modes that load a page do it through a view (view/view.h),
 * as the window does: the page's scripts run, its timers run on a virtual
 * clock up to MAIN_SETTLE_BUDGET (browser_view_settle), and the page is
 * drawn with the CPU (browser_view_draw_pixels) or dumped
 * (browser_view_dump).  --run writes the page's console to standard output
 * (the other modes write it to standard error).  Every mode reports
 * failure with a non-zero exit status and one line on standard error.
 *
 * --async fetches the page and its images without blocking, through the
 * view's loader, and waits for them; without it they are read at once.
 * --ca-file (in any mode that loads a page) trusts the CA certificates of
 * a PEM file besides the system's roots for https (the tests' own CA).
 */

#include "base/base.h"
#include "js/js.h"
#include "vm/bytecode.h"
#include "paint/gpu.h"
#include "shell/shell.h"
#include "view/view.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The version the program reports. */
#define MAIN_VERSION		"0.1 (ws074)"

/* The window size used unless told otherwise, in pixels. */
#define MAIN_DEFAULT_WIDTH	1024U
#define MAIN_DEFAULT_HEIGHT	768U

/* The largest window size accepted on the command line, in pixels. */
#define MAIN_MAX_SIZE		16384UL

/* The page the window opens when the command line names none. */
#define MAIN_START_PAGE		"/usr/share/browser/start.html"

/*
 * How long the headless modes let a page's timers run, in virtual
 * milliseconds (the budget the Chromium references are made with).
 */
#define MAIN_SETTLE_BUDGET	5000.0

/* The most live bytes a script run by --js may keep in its heap. */
#define MAIN_SCRIPT_HEAP_LIMIT	((size_t)1024U * 1024U * 1024U)

/*
 * What the program was asked to do.
 */
enum main_mode {
	MAIN_MODE_WINDOW,
	MAIN_MODE_VERSION,
	MAIN_MODE_HELP,
	MAIN_MODE_DUMP_DOM,
	MAIN_MODE_DUMP_STYLE,
	MAIN_MODE_DUMP_LAYOUT,
	MAIN_MODE_DUMP_PAINT,
	MAIN_MODE_DUMP_AST,
	MAIN_MODE_RUN_JS,
	MAIN_MODE_DUMP_CODE,
	MAIN_MODE_RENDER,
	MAIN_MODE_RENDER_GPU,
	MAIN_MODE_RUN_PAGE
};

/*
 * What the command line asked for.
 */
struct main_options {
	enum main_mode mode;
	struct shell_options shell;
	struct text_font_paths fonts;
	const char *output;
	unsigned parse;
	int async;
};

/*
 * The page of a headless mode: the command line, the view that holds the
 * page, and whether the load failed (the load callback said why).  It
 * lives in the mode's function for as long as the view.
 */
struct main_page {
	const struct main_options *options;
	struct browser_view *view;
	int failed;
};

/*
 * One headless dump: the name --dump= takes and the mode it selects.
 */
struct main_dump_name {
	const char *name;
	enum main_mode mode;
};

/*
 * The dumps --dump= knows, ending with a NULL name.  The table is constant
 * for the life of the program.
 */
static const struct main_dump_name main_dumps[] = {
	{ "dom", MAIN_MODE_DUMP_DOM },
	{ "style", MAIN_MODE_DUMP_STYLE },
	{ "layout", MAIN_MODE_DUMP_LAYOUT },
	{ "paint", MAIN_MODE_DUMP_PAINT },
	{ "ast", MAIN_MODE_DUMP_AST },
	{ "code", MAIN_MODE_DUMP_CODE },
	{ NULL, MAIN_MODE_WINDOW }
};

static int main_parse(int argc, char **argv, struct main_options *options);
static int main_dump(const struct main_options *options);
static int main_render(const struct main_options *options);
static int main_dump_ast(const struct main_options *options);
static int main_run_js(const struct main_options *options);
static int main_run_page(const struct main_options *options);
static int main_read_script(const char *path, struct wb_units *units);
static int main_dump_code(struct vm_realm *realm, const struct wb_units *units, unsigned how, struct js_syntax_error *error);
static int main_dump_unit(const struct vm_code *code, struct wb_buffer *out);
static int main_open(const struct main_options *options, const void *stack_base, unsigned flags, struct main_page *page);
static void main_loaded(void *context, struct browser_view *view, enum browser_load_state state, const char *url, int error, const char *reason);
static void main_console_out(void *context, struct browser_view *view, int level, const char *text, size_t length);
static void main_console_err(void *context, struct browser_view *view, int level, const char *text, size_t length);
static int main_draw_gpu(struct browser_view *view, uint32_t *pixels, unsigned width, unsigned height);
static int main_write_ppm(const char *path, const uint32_t *pixels, unsigned width, unsigned height);
static int main_parse_size(const char *text, unsigned *size);
static const char *main_value(const char *argument, const char *name);
static void main_usage(FILE *stream);

/*
 * Runs the mode the command line names.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	int error;
	int status;

	/* Reads the command line. */
	error = main_parse(argc, argv, &options);
	if (error != 0) {
		main_usage(stderr);
		return 2;
	}

	/* Runs the mode. */
	switch (options.mode) {
	case MAIN_MODE_VERSION:
		printf("browser %s\n", MAIN_VERSION);
		return 0;
	case MAIN_MODE_HELP:
		main_usage(stdout);
		return 0;
	case MAIN_MODE_DUMP_DOM:
	case MAIN_MODE_DUMP_STYLE:
	case MAIN_MODE_DUMP_LAYOUT:
	case MAIN_MODE_DUMP_PAINT:
		status = main_dump(&options);
		return status;
	case MAIN_MODE_RENDER:
	case MAIN_MODE_RENDER_GPU:
		status = main_render(&options);
		return status;
	case MAIN_MODE_DUMP_AST:
		status = main_dump_ast(&options);
		return status;
	case MAIN_MODE_RUN_JS:
	case MAIN_MODE_DUMP_CODE:
		status = main_run_js(&options);
		return status;
	case MAIN_MODE_RUN_PAGE:
		status = main_run_page(&options);
		return status;
	case MAIN_MODE_WINDOW:
		break;
	}

	/* Opens the window (on the start page when no page is named) and stays in it until it closes. */
	if (options.shell.start == NULL)
		options.shell.start = MAIN_START_PAGE;
	options.shell.fonts = &options.fonts;
	status = shell_run(&options.shell);
	if (status != 0)
		return status;

	/* Succeeded: the window was closed. */
	return 0;
}

/* Loads the page the command line names and writes one of its dumps to standard output. */
static int
main_dump(
	const struct main_options *options)
{
	struct main_page page;
	enum browser_dump kind;
	unsigned flags;
	size_t length;
	char *text;
	int status;
	int error;

	/* The dump the mode names; the layout and the display list need the page laid out with its images. */
	flags = BROWSER_SETTLE_LAYOUT;
	if (options->mode == MAIN_MODE_DUMP_DOM) {
		kind = BROWSER_DUMP_DOM;
		flags = 0;
	} else if (options->mode == MAIN_MODE_DUMP_STYLE) {
		kind = BROWSER_DUMP_STYLE;
		flags = 0;
	} else if (options->mode == MAIN_MODE_DUMP_LAYOUT) {
		kind = BROWSER_DUMP_LAYOUT;
	} else {
		kind = BROWSER_DUMP_PAINT;
	}

	/* Loads the page and lets it settle. */
	status = main_open(options, __builtin_frame_address(0), flags, &page);
	if (status != 0)
		return status;

	/* The dump, written out. */
	error = browser_view_dump(page.view, kind, &text, &length);
	if (error == 0)
		fwrite(text, 1, length, stdout);
	free(text);
	browser_view_destroy(page.view);
	if (error != 0) {
		fprintf(stderr, "browser: cannot dump %s: %s\n", options->shell.start, strerror(error));
		return 1;
	}

	/* Succeeded: the dump is written. */
	return 0;
}

/* Draws the page the command line names with the CPU or the GPU renderer and writes it as a PPM file. */
static int
main_render(
	const struct main_options *options)
{
	struct main_page page;
	uint32_t *pixels;
	unsigned width;
	unsigned height;
	int status;
	int error;

	/* A picture needs a file to go to. */
	if (options->output == NULL) {
		fprintf(stderr, "browser: --render needs --output=FILE\n");
		return 2;
	}

	/* Loads the page and lets it settle, laid out with its images. */
	status = main_open(options, __builtin_frame_address(0), BROWSER_SETTLE_LAYOUT, &page);
	if (status != 0)
		return status;

	/* The picture: the viewport's worth of the page from its top. */
	width = options->shell.width;
	height = options->shell.height;
	pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	if (pixels == NULL) {
		fprintf(stderr, "browser: cannot draw %s: %s\n", options->shell.start, strerror(ENOMEM));
		browser_view_destroy(page.view);
		return 1;
	}

	/* Draws the page with the GPU renderer (which says why it failed), or with the CPU renderer. */
	if (options->mode == MAIN_MODE_RENDER_GPU) {
		error = main_draw_gpu(page.view, pixels, width, height);
	} else {
		error = browser_view_draw_pixels(page.view, pixels, width, height, (size_t)width * sizeof(uint32_t));
		if (error != 0)
			fprintf(stderr, "browser: cannot draw %s: %s\n", options->shell.start, strerror(error));
	}

	/* The view is no longer needed; a picture that could not be drawn is not written. */
	browser_view_destroy(page.view);
	if (error != 0) {
		free(pixels);
		return 1;
	}

	/* Writes the picture. */
	error = main_write_ppm(options->output, pixels, width, height);
	free(pixels);
	if (error != 0) {
		fprintf(stderr, "browser: cannot write %s: %s\n", options->output, strerror(error));
		return 1;
	}

	/* Succeeded: the picture is written. */
	return 0;
}

/* Parses the script the command line names and writes its syntax tree, or its syntax error (exit status 1). */
static int
main_dump_ast(
	const struct main_options *options)
{
	struct wb_buffer out;
	struct wb_units units;
	struct js_program program;
	struct js_syntax_error error;
	int status;

	/* A dump needs a file. */
	if (options->shell.start == NULL) {
		fprintf(stderr, "browser: a file to parse is needed\n");
		return 2;
	}

	/* The file, as UTF-16. */
	status = main_read_script(options->shell.start, &units);
	if (status != 0)
		return 1;

	/* The parse. */
	status = js_parse(units.data, units.length, options->parse, &program, &error);
	if (status == EINVAL) {
		printf("SyntaxError: %s:%u:%u: %s\n", options->shell.start, error.line, error.column, error.message);
		wb_units_release(&units);
		return 1;
	}

	/* Any other failure of the parse. */
	if (status != 0) {
		fprintf(stderr, "browser: cannot parse %s: %s\n", options->shell.start, strerror(status));
		wb_units_release(&units);
		return 1;
	}

	/* The tree. */
	wb_buffer_init(&out);
	status = js_dump(program.root, &out);
	if (status == 0)
		fwrite(wb_buffer_string(&out), 1, out.length, stdout);
	wb_buffer_release(&out);
	js_program_release(&program);
	wb_units_release(&units);
	if (status != 0)
		return 1;

	/* Succeeded: the tree is written. */
	return 0;
}

/*
 * Runs the script the command line names in a realm of its own with
 * print; reports a syntax error, what is not supported, or an uncaught
 * exception on standard error with exit status 1.
 */
static int
main_run_js(
	const struct main_options *options)
{
	struct wb_units units;
	struct wb_buffer text;
	struct js_syntax_error error;
	struct vm_heap *heap;
	struct vm_realm *realm;
	vm_value completion;
	int status;
	int exit_status;

	/* A run needs a file. */
	if (options->shell.start == NULL) {
		fprintf(stderr, "browser: a script to run is needed\n");
		return 2;
	}

	/* The file, as UTF-16. */
	status = main_read_script(options->shell.start, &units);
	if (status != 0)
		return 1;

	/* The heap (its stack ends at this frame) and the realm with print. */
	status = vm_heap_create(&heap, MAIN_SCRIPT_HEAP_LIMIT);
	if (status != 0) {
		fprintf(stderr, "browser: cannot make a heap: %s\n", strerror(status));
		wb_units_release(&units);
		return 1;
	}

	/* The stack the collector scans ends at this frame. */
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	status = vm_realm_create(heap, &realm);
	if (status == 0)
		status = js_install_builtins(realm);
	if (status == 0)
		status = js_define_print(realm);
	if (status != 0) {
		fprintf(stderr, "browser: cannot make a realm: %s\n", strerror(status));
		vm_heap_destroy(heap);
		wb_units_release(&units);
		return 1;
	}

	/* The run, or the dump of the code. */
	if (options->mode == MAIN_MODE_DUMP_CODE) {
		status = main_dump_code(realm, &units, options->parse, &error);
	} else {
		status = js_run_script(realm, units.data, units.length, options->parse, &completion, &error);
	}

	/* The source is no longer needed. */
	wb_units_release(&units);

	/* Why it did not run to its end. */
	exit_status = 0;
	if (status == EINVAL && error.unsupported) {
		fprintf(stderr, "browser: %s:%u:%u: %s\n", options->shell.start, error.line, error.column, error.message);
		exit_status = 1;
	} else if (status == EINVAL) {
		fprintf(stderr, "SyntaxError: %s:%u:%u: %s\n", options->shell.start, error.line, error.column, error.message);
		exit_status = 1;
	} else if (status == VM_THROWN) {
		wb_buffer_init(&text);
		status = js_exception_text(realm, realm->exception, &text);
		if (status == 0)
			fprintf(stderr, "Uncaught %s\n", wb_buffer_string(&text));
		wb_buffer_release(&text);
		exit_status = 1;
	} else if (status != 0) {
		fprintf(stderr, "browser: cannot run %s: %s\n", options->shell.start, strerror(status));
		exit_status = 1;
	}

	/* The realm and its heap are no longer needed. */
	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	if (exit_status != 0)
		return exit_status;

	/* Succeeded: the script ran to its end. */
	return 0;
}

/* Loads a page, runs its scripts and timers, and writes its console to standard output. */
static int
main_run_page(
	const struct main_options *options)
{
	struct main_page page;
	int status;

	/* The page, loaded and settled with its console on standard output. */
	status = main_open(options, __builtin_frame_address(0), 0, &page);
	if (status != 0)
		return status;

	/* The page is no longer needed. */
	browser_view_destroy(page.view);

	/* Succeeded: the page ran. */
	return 0;
}

/* Compiles a script and writes its code units (the program's, then each function's inside it). */
static int
main_dump_code(
	struct vm_realm *realm,
	const struct wb_units *units,
	unsigned how,
	struct js_syntax_error *error)
{
	struct js_program program;
	struct vm_function *function;
	struct wb_buffer out;
	int status;

	/* The tree, then the code. */
	status = js_parse(units->data, units->length, how, &program, error);
	if (status != 0)
		return status;
	status = js_compile(realm, &program, &function, error);
	js_program_release(&program);
	if (status != 0)
		return status;

	/* The units as text. */
	wb_buffer_init(&out);
	status = main_dump_unit(function->code, &out);
	if (status == 0)
		fwrite(wb_buffer_string(&out), 1, out.length, stdout);
	wb_buffer_release(&out);
	if (status != 0)
		return status;

	/* Succeeded: the code is written. */
	return 0;
}

/* Writes a code unit, then the code units among its constants. */
static int
main_dump_unit(
	const struct vm_code *code,
	struct wb_buffer *out)
{
	struct vm_cell *cell;
	uint32_t index;
	int is_cell;
	int status;

	/* The unit's name and instructions. */
	status = wb_buffer_append_string(out, "\n== ");
	if (status == 0)
		status = vm_string_to_utf8(code->name, out);
	if (status == 0)
		status = wb_buffer_append_string(out, "\n");
	if (status == 0)
		status = vm_code_dump(code, out);
	if (status != 0)
		return status;

	/* The functions made inside it. */
	for (index = 0; index < code->constant_count; index++) {
		is_cell = vm_value_is_cell(code->constants[index]);
		if (!is_cell)
			continue;
		cell = vm_value_as_cell(code->constants[index]);
		if (cell->type != &vm_code_type)
			continue;
		status = main_dump_unit((const struct vm_code *)cell, out);
		if (status != 0)
			return status;
	}

	/* Succeeded: the units are written. */
	return 0;
}

/* Reads a script file as UTF-16; says why on standard error when it cannot. */
static int
main_read_script(
	const char *path,
	struct wb_units *units)
{
	struct wb_buffer bytes;
	int status;

	/* The bytes, then their UTF-16. */
	wb_buffer_init(&bytes);
	wb_units_init(units);
	status = wb_file_read(path, &bytes);
	if (status == 0)
		status = wb_utf8_to_units((const unsigned char *)bytes.data, bytes.length, units);
	wb_buffer_release(&bytes);
	if (status != 0) {
		fprintf(stderr, "browser: cannot read %s: %s\n", path, strerror(status));
		wb_units_release(units);
		return status;
	}

	/* Succeeded: the script's characters. */
	return 0;
}

/*
 * Makes the view of a headless mode and loads the page the command line
 * names into it, then lets it settle: its timers run on the virtual clock
 * and, with BROWSER_SETTLE_LAYOUT in flags, it is laid out with its images.
 * Reports the exit status of a failure after saying why, or 0 with the
 * view in page->view.
 *
 * stack_base is the caller's frame: the pages' heaps scan the stack up to
 * it, and the caller goes on using the view after this returns.
 */
static int
main_open(
	const struct main_options *options,
	const void *stack_base,
	unsigned flags,
	struct main_page *page)
{
	struct browser_callbacks callbacks;
	struct browser_view_options view_options;
	int error;

	/* Every headless mode needs a page. */
	memset(page, 0, sizeof(*page));
	page->options = options;
	if (options->shell.start == NULL) {
		fprintf(stderr, "browser: a file to load is needed\n");
		return 2;
	}

	/* The callbacks: a failed load is said, and the console goes to standard error (--run's to standard output). */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.load = main_loaded;
	callbacks.console = main_console_err;
	if (options->mode == MAIN_MODE_RUN_PAGE)
		callbacks.console = main_console_out;
	callbacks.context = page;

	/* The view at the viewport's size, reading at once or, with --async, fetching the page and its images in the background. */
	memset(&view_options, 0, sizeof(view_options));
	view_options.fonts = &options->fonts;
	view_options.callbacks = &callbacks;
	view_options.stack_base = stack_base;
	view_options.width = options->shell.width;
	view_options.height = options->shell.height;
	view_options.fetch = BROWSER_FETCH_AT_ONCE;
	if (options->async)
		view_options.fetch = BROWSER_FETCH_BACKGROUND;
	error = browser_view_create(&view_options, &page->view);
	if (error != 0) {
		fprintf(stderr, "browser: cannot make a view: %s\n", strerror(error));
		return 1;
	}

	/* The page, running its scripts (a load that failed was said by the callback). */
	error = browser_view_load(page->view, options->shell.start);
	if (error != 0) {
		if (!page->failed)
			fprintf(stderr, "browser: cannot load %s: %s\n", options->shell.start, strerror(error));
		browser_view_destroy(page->view);
		return 1;
	}

	/* The page settled: fetched, its timers run, and laid out with its images when the mode needs that. */
	error = browser_view_settle(page->view, MAIN_SETTLE_BUDGET, flags);
	if (error != 0) {
		if (!page->failed)
			fprintf(stderr, "browser: cannot run %s: %s\n", options->shell.start, strerror(error));
		browser_view_destroy(page->view);
		return 1;
	}

	/* Succeeded: the page is ready for the mode. */
	return 0;
}

/* The view's load callback: a load that failed, and why (the TLS reason for an https page). */
static void
main_loaded(
	void *context,
	struct browser_view *view,
	enum browser_load_state state,
	const char *url,
	int error,
	const char *reason)
{
	struct main_page *page;

	UNUSED_PARAMETER(view);
	UNUSED_PARAMETER(url);

	/* Only a failure is said. */
	if (state != BROWSER_LOAD_FAILED)
		return;

	/* The line, by the location the command line gave. */
	page = context;
	fprintf(stderr, "browser: cannot load %s: %s", page->options->shell.start, strerror(error));
	if (reason[0] != '\0')
		fprintf(stderr, " (TLS: %s)", reason);
	fputc('\n', stderr);

	/* The mode ends without saying it again. */
	page->failed = 1;
}

/* The view's console callback of --run: the line as it is, on standard output. */
static void
main_console_out(
	void *context,
	struct browser_view *view,
	int level,
	const char *text,
	size_t length)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	UNUSED_PARAMETER(level);

	/* The line. */
	fwrite(text, 1, length, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

/* The view's console callback of the other modes: the line on standard error, marked as the console's. */
static void
main_console_err(
	void *context,
	struct browser_view *view,
	int level,
	const char *text,
	size_t length)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	UNUSED_PARAMETER(level);

	/* The line. */
	fprintf(stderr, "console: %.*s\n", (int)length, text);
}

/*
 * Draws the view with the GPU renderer into an offscreen image of its own
 * device and reads it back into pixels (width by height, packed rows);
 * says why on standard error when it cannot.
 */
static int
main_draw_gpu(
	struct browser_view *view,
	uint32_t *pixels,
	unsigned width,
	unsigned height)
{
	const struct paint_list *list;
	struct text_system *text;
	struct paint_bitmap bitmap;
	layout_unit scroll_y;
	const char *failed;
	VkResult result;
	int error;

	/* What to draw. */
	error = browser_view_display(view, &list, &text, &scroll_y);
	if (error != 0) {
		fprintf(stderr, "browser: cannot draw on the GPU: %s\n", strerror(error));
		return error;
	}

	/* The picture, drawn from the page's top. */
	bitmap.pixels = pixels;
	bitmap.width = (int)width;
	bitmap.height = (int)height;
	result = paint_gpu_render(list, text, 0, &bitmap, &failed);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "browser: cannot draw on the GPU: %s failed (%d)\n", failed, (int)result);
		return EIO;
	}

	/* Succeeded: the pixels hold the picture. */
	return 0;
}

/* Writes pixels (0xAARRGGBB, packed rows) as a binary PPM file. */
static int
main_write_ppm(
	const char *path,
	const uint32_t *pixels,
	unsigned width,
	unsigned height)
{
	unsigned char sample[3];
	size_t count;
	size_t index;
	size_t written;
	FILE *file;
	int failed;
	int closed;
	int error;

	/* The file. */
	file = fopen(path, "wb");
	if (file == NULL)
		return errno;

	/* The header: the magic, the size and the largest sample. */
	fprintf(file, "P6\n%u %u\n255\n", width, height);

	/* Each pixel's red, green and blue, while the writes go through. */
	count = (size_t)width * (size_t)height;
	for (index = 0; index < count; index++) {
		sample[0] = (unsigned char)(pixels[index] >> 16);
		sample[1] = (unsigned char)(pixels[index] >> 8);
		sample[2] = (unsigned char)pixels[index];
		written = fwrite(sample, 1, sizeof(sample), file);
		if (written != sizeof(sample))
			break;
	}

	/* A write that failed, or a close that did. */
	error = 0;
	failed = ferror(file);
	if (failed)
		error = EIO;
	closed = fclose(file);
	if (closed != 0 && error == 0)
		error = errno;
	if (error != 0)
		return error;

	/* Succeeded: the file holds the picture. */
	return 0;
}

/* Reads the command line into options; returns EINVAL for a word it does not know. */
static int
main_parse(
	int argc,
	char **argv,
	struct main_options *options)
{
	const char *value;
	int differs;
	int index;
	int dump;
	int error;

	/* Starts from the window mode with the default size. */
	memset(options, 0, sizeof(*options));
	options->mode = MAIN_MODE_WINDOW;
	options->shell.width = MAIN_DEFAULT_WIDTH;
	options->shell.height = MAIN_DEFAULT_HEIGHT;
	options->fonts.sans = TEXT_DEFAULT_SANS;
	options->fonts.mono = TEXT_DEFAULT_MONO;
	options->fonts.fallback = TEXT_DEFAULT_FALLBACK;

	/* Takes each word in turn. */
	for (index = 1; index < argc; index++) {
		/* The informational modes. */
		differs = strcmp(argv[index], "--version");
		if (differs == 0) {
			options->mode = MAIN_MODE_VERSION;
			continue;
		}

		/* The help. */
		differs = strcmp(argv[index], "--help");
		if (differs == 0) {
			options->mode = MAIN_MODE_HELP;
			continue;
		}

		/* Running a page for its console. */
		differs = strcmp(argv[index], "--run");
		if (differs == 0) {
			options->mode = MAIN_MODE_RUN_PAGE;
			continue;
		}

		/* The headless dumps of a page. */
		value = main_value(argv[index], "--dump=");
		if (value != NULL) {
			/* Finds the dump's name among the known ones. */
			for (dump = 0; main_dumps[dump].name != NULL; dump++) {
				differs = strcmp(value, main_dumps[dump].name);
				if (differs == 0)
					break;
			}

			/* A name that is not known is refused. */
			if (main_dumps[dump].name == NULL) {
				fprintf(stderr, "browser: unknown dump %s\n", value);
				return EINVAL;
			}

			/* The dump's mode. */
			options->mode = main_dumps[dump].mode;
			continue;
		}

		/* The headless drawing of a page. */
		differs = strcmp(argv[index], "--render");
		if (differs == 0) {
			options->mode = MAIN_MODE_RENDER;
			continue;
		}

		/* The same with the GPU renderer. */
		differs = strcmp(argv[index], "--render-gpu");
		if (differs == 0) {
			options->mode = MAIN_MODE_RENDER_GPU;
			continue;
		}

		/* The headless run of a script. */
		differs = strcmp(argv[index], "--js");
		if (differs == 0) {
			options->mode = MAIN_MODE_RUN_JS;
			continue;
		}

		/* How a script is parsed: as a module, or as strict code. */
		differs = strcmp(argv[index], "--module");
		if (differs == 0) {
			options->parse |= JS_PARSE_MODULE;
			continue;
		}

		/* As strict code. */
		differs = strcmp(argv[index], "--strict");
		if (differs == 0) {
			options->parse |= JS_PARSE_STRICT;
			continue;
		}

		/* The headless modes fetch the page and its images with the asynchronous loader. */
		differs = strcmp(argv[index], "--async");
		if (differs == 0) {
			options->async = 1;
			continue;
		}

		/* The file a drawing goes to. */
		value = main_value(argv[index], "--output=");
		if (value != NULL) {
			options->output = value;
			continue;
		}

		/* The fonts. */
		value = main_value(argv[index], "--font=");
		if (value != NULL) {
			options->fonts.sans = value;
			continue;
		}

		/* The monospace font. */
		value = main_value(argv[index], "--mono-font=");
		if (value != NULL) {
			options->fonts.mono = value;
			continue;
		}

		/* The fallback font. */
		value = main_value(argv[index], "--fallback-font=");
		if (value != NULL) {
			options->fonts.fallback = value;
			continue;
		}

		/* A CA file for https besides the system's roots. */
		value = main_value(argv[index], "--ca-file=");
		if (value != NULL) {
			error = browser_add_ca_file(value);
			if (error != 0)
				return error;

			continue;
		}

		/* The Wayland display to connect to. */
		value = main_value(argv[index], "--display=");
		if (value != NULL) {
			options->shell.display = value;
			continue;
		}

		/* The window's width. */
		value = main_value(argv[index], "--width=");
		if (value != NULL) {
			error = main_parse_size(value, &options->shell.width);
			if (error != 0)
				return error;

			continue;
		}

		/* The window's height. */
		value = main_value(argv[index], "--height=");
		if (value != NULL) {
			error = main_parse_size(value, &options->shell.height);
			if (error != 0)
				return error;

			continue;
		}

		/* An unknown option is refused. */
		if (argv[index][0] == '-') {
			fprintf(stderr, "browser: unknown option %s\n", argv[index]);
			return EINVAL;
		}

		/* The one word that is not an option is the page to open. */
		if (options->shell.start != NULL) {
			fprintf(stderr, "browser: more than one page given\n");
			return EINVAL;
		}

		/* Remembers the page. */
		options->shell.start = argv[index];
	}

	/* Succeeded: options holds the request. */
	return 0;
}

/* Reads a window size in pixels. */
static int
main_parse_size(
	const char *text,
	unsigned *size)
{
	unsigned long value;
	char *end;

	/* Reads the decimal number, which must be the whole word. */
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 ||
	    end == text ||
	    *end != '\0') {
		fprintf(stderr, "browser: %s is not a size\n", text);
		return EINVAL;
	}

	/* Refuses a size of nothing or past what a window can be. */
	if (value == 0 || value > MAIN_MAX_SIZE) {
		fprintf(stderr, "browser: size %s is out of range\n", text);
		return EINVAL;
	}

	/* Succeeded: the size fits. */
	*size = (unsigned)value;
	return 0;
}

/* Finds the value after name= in an argument, or NULL when the argument is another option. */
static const char *
main_value(
	const char *argument,
	const char *name)
{
	size_t length;
	int differs;

	/* Compares the option's name with the start of the argument. */
	length = strlen(name);
	differs = strncmp(argument, name, length);
	if (differs != 0)
		return NULL;

	/* Reports what follows the name. */
	return argument + length;
}

/* Prints how to run the program. */
static void
main_usage(
	FILE *stream)
{
	/* Lists the forms of the command line. */
	fprintf(stream,
		"usage: browser [--display=NAME] [--width=N] [--height=N] [--ca-file=PEM] [URL]\n"
		"       browser --dump=dom|style|layout|paint [--width=N] [--height=N] [--font=PATH]\n"
		"                        [--mono-font=PATH] [--fallback-font=PATH] FILE\n"
		"       browser --run [--width=N] [--height=N] FILE\n"
		"       browser --render|--render-gpu --output=OUT.ppm [--width=N] [--height=N] [--font=PATH] [--async]\n"
		"                        [--mono-font=PATH] [--fallback-font=PATH] FILE\n"
		"       browser --dump=ast [--module] [--strict] FILE.js\n"
		"       browser --js [--strict] FILE.js\n"
		"       browser --dump=code [--strict] FILE.js\n"
		"       browser --version | --help\n");
}
