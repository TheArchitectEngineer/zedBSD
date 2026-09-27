/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zdesktop-browser: the Web browser of the zedBSD desktop.
 *
 *   zdesktop-browser [--display=NAME] [--width=N] [--height=N] [URL]
 *   zdesktop-browser --dump=dom|style|layout|paint [--width=N] [--height=N] [--font=PATH] FILE
 *   zdesktop-browser --render|--render-gpu --output=OUT.ppm [--width=N] [--height=N] [--font=PATH] FILE
 *   zdesktop-browser --version | --help
 *
 * Without a headless mode it opens a zdesktop window on URL.  The headless
 * modes (added with the engine, one per phase) draw or dump a page, or run
 * a script, without a window; the tests use them on the host and in the
 * guest.  Every mode reports failure with a non-zero exit status and one
 * line on standard error.
 */

#include "base/base.h"
#include "page/page.h"
#include "paint/gpu.h"
#include "shell/shell.h"

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
	MAIN_MODE_RENDER,
	MAIN_MODE_RENDER_GPU
};

/*
 * What the command line asked for.
 */
struct main_options {
	enum main_mode mode;
	struct shell_options shell;
	struct text_font_paths fonts;
	const char *output;
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
	{ NULL, MAIN_MODE_WINDOW }
};

static int main_parse(int argc, char **argv, struct main_options *options);
static int main_dump(const struct main_options *options);
static int main_render(const struct main_options *options);
static int main_prepare(const struct main_options *options, const void *stack_base, struct page **page, int paint);
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
		printf("zdesktop-browser %s\n", MAIN_VERSION);
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
	case MAIN_MODE_WINDOW:
		break;
	}

	/* Opens the window and stays in it until it closes. */
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
	struct wb_buffer out;
	struct page *page;
	int error;

	int status;

	/* Loads the page, and lays it out and paints it when the dump shows that. */
	status = main_prepare(options, __builtin_frame_address(0), &page, options->mode == MAIN_MODE_DUMP_PAINT);
	if (status != 0)
		return status;

	/* Writes the dump. */
	wb_buffer_init(&out);
	if (options->mode == MAIN_MODE_DUMP_DOM) {
		error = page_dump_dom(page, &out);
	} else if (options->mode == MAIN_MODE_DUMP_STYLE) {
		error = page_dump_style(page, &out);
	} else if (options->mode == MAIN_MODE_DUMP_LAYOUT) {
		error = layout_dump(&page->layout, &out);
	} else {
		error = paint_dump(&page->paint, &out);
	}

	/* Writes the dump out. */
	if (error == 0)
		fwrite(wb_buffer_string(&out), 1, out.length, stdout);
	wb_buffer_release(&out);
	page_destroy(page);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot dump %s: %s\n", options->shell.start, strerror(error));
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
	struct paint_bitmap bitmap;
	struct page *page;
	const char *failed;
	VkResult result;
	int status;
	int error;

	/* A picture needs a file to go to. */
	if (options->output == NULL) {
		fprintf(stderr, "zdesktop-browser: --render needs --output=FILE\n");
		return 2;
	}

	/* Loads, lays out and paints the page. */
	status = main_prepare(options, __builtin_frame_address(0), &page, 1);
	if (status != 0)
		return status;

	/* The picture: the viewport's worth of the page from its top. */
	error = paint_bitmap_create(&bitmap, (int)options->shell.width, (int)options->shell.height);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot draw %s: %s\n", options->shell.start, strerror(error));
		page_destroy(page);
		return 1;
	}

	/* Draws the page with the GPU renderer, read back from an offscreen image. */
	if (options->mode == MAIN_MODE_RENDER_GPU) {
		result = paint_gpu_render(&page->paint, &page->text, 0, &bitmap, &failed);
		if (result != VK_SUCCESS) {
			fprintf(stderr, "zdesktop-browser: cannot draw %s on the GPU: %s failed (%d)\n", options->shell.start, failed, (int)result);
			paint_bitmap_release(&bitmap);
			page_destroy(page);
			return 1;
		}
	}

	/* Or with the CPU renderer. */
	if (options->mode == MAIN_MODE_RENDER) {
		error = paint_software(&page->paint, &page->text, 0, &bitmap);
		if (error != 0) {
			fprintf(stderr, "zdesktop-browser: cannot draw %s: %s\n", options->shell.start, strerror(error));
			paint_bitmap_release(&bitmap);
			page_destroy(page);
			return 1;
		}
	}

	/* Writes the picture. */
	error = paint_write_ppm(&bitmap, options->output);
	paint_bitmap_release(&bitmap);
	page_destroy(page);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot write %s: %s\n", options->output, strerror(error));
		return 1;
	}

	/* Succeeded: the picture is written. */
	return 0;
}

/*
 * Loads the page the command line names and, for every mode past the DOM
 * and style dumps, opens the fonts and lays it out; paints it when asked.
 * Reports the exit status of a failure after saying why, or 0.
 *
 * stack_base is the caller's frame: the page's heap scans the stack up to
 * it, and the caller goes on using the page after this returns.
 */
static int
main_prepare(
	const struct main_options *options,
	const void *stack_base,
	struct page **page,
	int paint)
{
	struct page *loaded;
	int layout;
	int error;

	/* Every headless mode needs a page. */
	*page = NULL;
	if (options->shell.start == NULL) {
		fprintf(stderr, "zdesktop-browser: a file to load is needed\n");
		return 2;
	}

	/* Makes the page; its heap's stack ends at the caller's frame. */
	error = page_create(&loaded, stack_base);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot make a page: %s\n", strerror(error));
		return 1;
	}

	/* Loads the file. */
	error = page_load_file(loaded, options->shell.start);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot load %s: %s\n", options->shell.start, strerror(error));
		page_destroy(loaded);
		return 1;
	}

	/* The DOM and style dumps need no layout. */
	layout = 1;
	if (options->mode == MAIN_MODE_DUMP_DOM || options->mode == MAIN_MODE_DUMP_STYLE)
		layout = 0;

	/* Lays the page out in the viewport's width. */
	if (layout) {
		error = page_open_fonts(loaded, &options->fonts);
		if (error == 0)
			error = page_layout(loaded, (int)options->shell.width, (int)options->shell.height);
		if (error != 0) {
			fprintf(stderr, "zdesktop-browser: cannot lay out %s: %s\n", options->shell.start, strerror(error));
			page_destroy(loaded);
			return 1;
		}
	}

	/* Builds the display list. */
	if (paint) {
		error = page_paint(loaded);
		if (error != 0) {
			fprintf(stderr, "zdesktop-browser: cannot paint %s: %s\n", options->shell.start, strerror(error));
			page_destroy(loaded);
			return 1;
		}
	}

	/* Succeeded: the page is ready for the mode. */
	*page = loaded;
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
				fprintf(stderr, "zdesktop-browser: unknown dump %s\n", value);
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
			fprintf(stderr, "zdesktop-browser: unknown option %s\n", argv[index]);
			return EINVAL;
		}

		/* The one word that is not an option is the page to open. */
		if (options->shell.start != NULL) {
			fprintf(stderr, "zdesktop-browser: more than one page given\n");
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
		fprintf(stderr, "zdesktop-browser: %s is not a size\n", text);
		return EINVAL;
	}

	/* Refuses a size of nothing or past what a window can be. */
	if (value == 0 || value > MAIN_MAX_SIZE) {
		fprintf(stderr, "zdesktop-browser: size %s is out of range\n", text);
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
		"usage: zdesktop-browser [--display=NAME] [--width=N] [--height=N] [URL]\n"
		"       zdesktop-browser --dump=dom|style|layout|paint [--width=N] [--height=N] [--font=PATH]\n"
		"                        [--mono-font=PATH] [--fallback-font=PATH] FILE\n"
		"       zdesktop-browser --render|--render-gpu --output=OUT.ppm [--width=N] [--height=N] [--font=PATH]\n"
		"                        [--mono-font=PATH] [--fallback-font=PATH] FILE\n"
		"       zdesktop-browser --version | --help\n");
}
