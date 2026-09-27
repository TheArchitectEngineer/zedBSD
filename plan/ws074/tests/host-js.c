/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p025: the batch driver of the JavaScript engine for the test262
 * runner (run-test262.py): runs many tests in one process, each in a heap
 * and a realm of its own.
 *
 *   host-js HARNESS_DIR MANIFEST
 *
 * Each line of MANIFEST is "PATH<TAB>MODE<TAB>HARNESS" where MODE is
 * "sloppy" or "strict" and HARNESS the harness files to run before the
 * test, separated by commas (empty for a raw test).  Strict mode puts
 * "use strict" before everything.  Each result is one line,
 * "PATH<TAB>MODE<TAB>RESULT", where RESULT is "ok" (with " async-complete"
 * or " async-failure" when the test printed the asynchronous tests'
 * marks), "syntax L:C MESSAGE", "unsupported MESSAGE", "throw TEXT" or
 * "error MESSAGE".  print is defined and its output kept for the marks.
 */

#include "js/js.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The most live bytes a test's heap may keep. */
#define HOST_JS_HEAP_LIMIT	((size_t)256U * 1024U * 1024U)

/* The longest line of the manifest. */
#define HOST_JS_LINE_MAX	8192

/* The longest exception text reported. */
#define HOST_JS_TEXT_MAX	300

/*
 * What the running test printed.
 *
 * print appends to it; it is emptied before each test.  The driver runs
 * one test at a time, so one buffer serves them all.
 */
static struct wb_buffer host_js_printed;

static int host_js_run(const char *harness_dir, const char *path, const char *mode, const char *harness, const void *stack_base);
static int host_js_source(const char *harness_dir, const char *path, int strict, const char *harness, struct wb_units *units);
static int host_js_append_file(const char *path, struct wb_buffer *bytes);
static void host_js_report(struct vm_realm *realm, int status, const struct js_syntax_error *error, struct wb_buffer *out);
static int host_js_print(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int host_js_define_print(struct vm_realm *realm);

/*
 * Runs each test the manifest lists and writes its result.
 */
int
main(
	int argc,
	char **argv)
{
	char line[HOST_JS_LINE_MAX];
	char *mode;
	char *harness;
	char *end;
	char *read;
	FILE *manifest;
	int status;

	/* The harness's directory and the manifest. */
	if (argc < 3) {
		fprintf(stderr, "usage: host-js HARNESS_DIR MANIFEST\n");
		return 2;
	}

	/* The manifest. */
	manifest = fopen(argv[2], "r");
	if (manifest == NULL) {
		perror(argv[2]);
		return 2;
	}

	/* Each line: the path, the mode and the harness files. */
	wb_buffer_init(&host_js_printed);
	for (;;) {
		read = fgets(line, sizeof(line), manifest);
		if (read == NULL)
			break;
		end = strchr(line, '\n');
		if (end != NULL)
			*end = '\0';
		mode = strchr(line, '\t');
		if (mode == NULL)
			continue;
		*mode = '\0';
		mode++;
		harness = strchr(mode, '\t');
		if (harness == NULL)
			continue;
		*harness = '\0';
		harness++;

		/* The test (this frame is the bottom of the stack the collector scans). */
		status = host_js_run(argv[1], line, mode, harness, __builtin_frame_address(0));
		if (status != 0)
			printf("%s\t%s\terror %s\n", line, mode, strerror(status));
		fflush(stdout);
	}

	/* The manifest is done. */
	fclose(manifest);
	wb_buffer_release(&host_js_printed);

	/* Succeeded: every test has its line. */
	return 0;
}

/* Runs one test in a heap and a realm of its own and writes its result. */
static int
host_js_run(
	const char *harness_dir,
	const char *path,
	const char *mode,
	const char *harness,
	const void *stack_base)
{
	struct wb_units units;
	struct wb_buffer out;
	struct js_syntax_error error;
	struct vm_heap *heap;
	struct vm_realm *realm;
	vm_value completion;
	int strict;
	int differs;
	int status;

	/* The source: the harness, then the test. */
	strict = 0;
	differs = strcmp(mode, "strict");
	if (differs == 0)
		strict = 1;
	status = host_js_source(harness_dir, path, strict, harness, &units);
	if (status != 0)
		return status;

	/* The heap and the realm, with print. */
	status = vm_heap_create(&heap, HOST_JS_HEAP_LIMIT);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* The collector scans the stack up to the driver's frame. */
	vm_heap_set_stack_base(heap, stack_base);
	status = vm_realm_create(heap, &realm);
	if (status == 0)
		status = host_js_define_print(realm);
	if (status != 0) {
		vm_heap_destroy(heap);
		wb_units_release(&units);
		return status;
	}

	/* The run, and its result. */
	wb_buffer_clear(&host_js_printed);
	status = js_run_script(realm, units.data, units.length, 0, &completion, &error);
	wb_buffer_init(&out);
	host_js_report(realm, status, &error, &out);
	printf("%s\t%s\t%s\n", path, mode, wb_buffer_string(&out));

	/* The test's heap and source are no longer needed. */
	wb_buffer_release(&out);
	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	wb_units_release(&units);

	/* Succeeded: the result is written. */
	return 0;
}

/* Makes a test's source: "use strict" for strict mode, the harness files, then the test. */
static int
host_js_source(
	const char *harness_dir,
	const char *path,
	int strict,
	const char *harness,
	struct wb_units *units)
{
	struct wb_buffer bytes;
	char name[1024];
	const char *start;
	const char *comma;
	size_t length;
	int status;

	/* Strict mode's directive first. */
	wb_buffer_init(&bytes);
	status = 0;
	if (strict)
		status = wb_buffer_append_string(&bytes, "\"use strict\";\n");

	/* Each harness file in order. */
	start = harness;
	while (status == 0 && *start != '\0') {
		comma = strchr(start, ',');
		length = strlen(start);
		if (comma != NULL)
			length = (size_t)(comma - start);
		snprintf(name, sizeof(name), "%s/%.*s", harness_dir, (int)length, start);
		status = host_js_append_file(name, &bytes);
		start += length;
		if (*start == ',')
			start++;
	}

	/* Then the test, as UTF-16. */
	if (status == 0)
		status = host_js_append_file(path, &bytes);
	wb_units_init(units);
	if (status == 0)
		status = wb_utf8_to_units((const unsigned char *)bytes.data, bytes.length, units);
	wb_buffer_release(&bytes);
	if (status != 0) {
		wb_units_release(units);
		return status;
	}

	/* Succeeded: the source. */
	return 0;
}

/* Appends a file's bytes and a line break. */
static int
host_js_append_file(
	const char *path,
	struct wb_buffer *bytes)
{
	struct wb_buffer file;
	int status;

	/* The file. */
	wb_buffer_init(&file);
	status = wb_file_read(path, &file);
	if (status == 0)
		status = wb_buffer_append(bytes, file.data, file.length);
	if (status == 0)
		status = wb_buffer_append_string(bytes, "\n");
	wb_buffer_release(&file);
	if (status != 0)
		return status;

	/* Succeeded: the file is appended. */
	return 0;
}

/* Writes a run's result: ok (with the asynchronous marks), a syntax error, what is not supported, or the exception. */
static void
host_js_report(
	struct vm_realm *realm,
	int status,
	const struct js_syntax_error *error,
	struct wb_buffer *out)
{
	struct wb_buffer text;
	const char *printed;
	const char *mark;
	const char *message;
	size_t length;

	/* Ran to its end: ok, with what it printed of the asynchronous tests. */
	printed = wb_buffer_string(&host_js_printed);
	if (status == 0) {
		wb_buffer_append_string(out, "ok");
		mark = strstr(printed, "Test262:AsyncTestComplete");
		if (mark != NULL)
			wb_buffer_append_string(out, " async-complete");
		mark = strstr(printed, "Test262:AsyncTestFailure");
		if (mark != NULL)
			wb_buffer_append_string(out, " async-failure");
		return;
	}

	/* The compiler's refusal, or a syntax error. */
	if (status == EINVAL && error->unsupported) {
		wb_buffer_printf(out, "unsupported %s", error->message);
		return;
	}

	/* A syntax error. */
	if (status == EINVAL) {
		wb_buffer_printf(out, "syntax %u:%u %s", error->line, error->column, error->message);
		return;
	}

	/* Any other failure of the engine. */
	if (status != VM_THROWN) {
		wb_buffer_printf(out, "error %s", strerror(status));
		return;
	}

	/* The exception's text, on one line and not too long. */
	wb_buffer_init(&text);
	js_exception_text(realm, realm->exception, &text);
	message = wb_buffer_string(&text);
	length = strcspn(message, "\r\n");
	if (length > HOST_JS_TEXT_MAX)
		length = HOST_JS_TEXT_MAX;
	wb_buffer_printf(out, "throw %.*s", (int)length, message);
	wb_buffer_release(&text);
}

/* Keeps the arguments as one printed line (print). */
static int
host_js_print(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_string *string;
	unsigned index;
	int status;

	UNUSED_PARAMETER(this_value);

	/* Each argument's string, a space between them, then the line's end. */
	*result = VM_VALUE_UNDEFINED;
	for (index = 0; index < count; index++) {
		status = vm_to_string(realm, args[index], &string);
		if (status != 0)
			return status;
		if (index > 0)
			wb_buffer_append_string(&host_js_printed, " ");
		vm_string_to_utf8(string, &host_js_printed);
	}

	/* The line's end. */
	wb_buffer_append_string(&host_js_printed, "\n");

	/* Succeeded: print returns undefined. */
	return 0;
}

/* Defines the driver's print on a realm's global object. */
static int
host_js_define_print(
	struct vm_realm *realm)
{
	struct vm_function *function;
	vm_value key;
	int status;

	/* The native function. */
	function = vm_function_create_native(realm, "print", 1, host_js_print);
	if (function == NULL)
		return ENOMEM;

	/* The global property. */
	key = vm_key_from_ascii(realm->heap, "print");
	if (key == VM_VALUE_EMPTY)
		return ENOMEM;
	status = vm_object_define(realm->heap, realm->global, key, vm_value_cell(function),
	    VM_PROPERTY_WRITABLE | VM_PROPERTY_CONFIGURABLE);
	if (status != 0)
		return status;

	/* Succeeded: print is defined. */
	return 0;
}
