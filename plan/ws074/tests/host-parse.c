/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p024: the batch driver of the JavaScript parser for the test262
 * runner (run-test262-parse.py): parses many files in one process.
 *
 *   host-parse MANIFEST
 *
 * Each line of MANIFEST is "PATH<TAB>MODE" where MODE is "sloppy",
 * "strict" or "module".  Each result is one line: "PATH<TAB>MODE<TAB>ok"
 * or "PATH<TAB>MODE<TAB>error LINE:COLUMN MESSAGE".
 */

#include "js/js.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int
main(
	int argc,
	char **argv)
{
	struct wb_buffer bytes;
	struct wb_units units;
	struct js_program program;
	struct js_syntax_error error;
	char line[4096];
	char *tab;
	char *mode;
	unsigned how;
	FILE *manifest;
	int status;

	if (argc < 2) {
		fprintf(stderr, "usage: host-parse MANIFEST\n");
		return 2;
	}
	manifest = fopen(argv[1], "r");
	if (manifest == NULL) {
		perror(argv[1]);
		return 2;
	}
	while (fgets(line, sizeof(line), manifest) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		tab = strchr(line, '\t');
		if (tab == NULL)
			continue;
		*tab = '\0';
		mode = tab + 1;
		how = 0;
		if (strcmp(mode, "strict") == 0)
			how = JS_PARSE_STRICT;
		if (strcmp(mode, "module") == 0)
			how = JS_PARSE_MODULE;

		wb_buffer_init(&bytes);
		wb_units_init(&units);
		status = wb_file_read(line, &bytes);
		if (status == 0)
			status = wb_utf8_to_units((const unsigned char *)bytes.data, bytes.length, &units);
		wb_buffer_release(&bytes);
		if (status != 0) {
			printf("%s\t%s\tunreadable %s\n", line, mode, strerror(status));
			wb_units_release(&units);
			continue;
		}
		status = js_parse(units.data, units.length, how, &program, &error);
		if (status == 0) {
			printf("%s\t%s\tok\n", line, mode);
			js_program_release(&program);
		} else if (status == EINVAL) {
			printf("%s\t%s\terror %u:%u %s\n", line, mode, error.line, error.column, error.message);
		} else {
			printf("%s\t%s\tfailed %s\n", line, mode, strerror(status));
		}
		wb_units_release(&units);
		fflush(stdout);
	}
	fclose(manifest);
	return 0;
}
