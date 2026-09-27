/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p015: the batch driver of browser's URL parser for
 * run-url-tests.py.  Reads commands from standard input, one a line, each
 * field in hexadecimal UTF-8 (so tabs and line feeds in URLs survive):
 *
 *   url HEX-INPUT HEX-BASE|-    parses the input against the base
 *   data HEX-INPUT              processes a data: URL
 *
 * and writes one line for each: "failure", or "ok" and the fields in
 * hexadecimal (a URL's href, origin, protocol, username, password, host,
 * hostname, port, pathname, search and hash; a data: URL's MIME type and
 * body).
 */

#include "net/net.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int unhex(const char *text, struct wb_buffer *out);
static void put_hex(const struct wb_buffer *buffer);
static void run_url(const char *input, const char *base);
static void run_data(const char *input);

int
main(void)
{
	static char line[1 << 20];
	char *command;
	char *first;
	char *second;
	char *tab;

	/* Each command: its fields between tabs. */
	while (fgets(line, sizeof(line), stdin) != NULL) {
		line[strcspn(line, "\n")] = '\0';
		command = line;
		first = NULL;
		second = NULL;
		tab = strchr(command, '\t');
		if (tab != NULL) {
			*tab = '\0';
			first = tab + 1;
			tab = strchr(first, '\t');
		}
		if (tab != NULL) {
			*tab = '\0';
			second = tab + 1;
		}
		if (first == NULL) {
			printf("failure\n");
			continue;
		}
		if (strcmp(command, "url") == 0)
			run_url(first, second);
		else
			run_data(first);
		fflush(stdout);
	}
	return 0;
}

/* Decodes a hexadecimal field ("-" and the empty field are empty). */
static int
unhex(
	const char *text,
	struct wb_buffer *out)
{
	unsigned value;
	size_t index;
	size_t length;

	/* Two digits a byte. */
	length = strlen(text);
	if (strcmp(text, "-") == 0)
		return 0;
	for (index = 0; index + 1U < length; index += 2U) {
		if (sscanf(text + index, "%2x", &value) != 1)
			return EINVAL;
		wb_buffer_append_byte(out, (unsigned char)value);
	}
	return 0;
}

/* Writes a tab and a buffer in hexadecimal ("-" for an empty one, so no field is empty). */
static void
put_hex(
	const struct wb_buffer *buffer)
{
	size_t index;

	/* The bytes. */
	printf("\t");
	if (buffer->length == 0)
		printf("-");
	for (index = 0; index < buffer->length; index++)
		printf("%02x", buffer->data[index]);
}

/* Parses a URL against a base and writes its parts. */
static void
run_url(
	const char *input,
	const char *base_text)
{
	struct wb_buffer text;
	struct wb_buffer base_buffer;
	struct wb_buffer part;
	struct net_url base;
	struct net_url url;
	int has_base;
	int part_index;
	int error;

	/* The base first, when there is one; a base that fails fails the test. */
	wb_buffer_init(&text);
	wb_buffer_init(&base_buffer);
	unhex(input, &text);
	has_base = 0;
	if (base_text != NULL && strcmp(base_text, "-") != 0) {
		unhex(base_text, &base_buffer);
		error = net_url_parse(wb_buffer_string(&base_buffer), base_buffer.length, NULL, &base);
		if (error != 0) {
			printf("failure\n");
			wb_buffer_release(&text);
			wb_buffer_release(&base_buffer);
			return;
		}
		has_base = 1;
	}

	/* The URL. */
	error = net_url_parse(wb_buffer_string(&text), text.length, has_base ? &base : NULL, &url);
	if (has_base)
		net_url_release(&base);
	wb_buffer_release(&text);
	wb_buffer_release(&base_buffer);
	if (error != 0) {
		printf("failure\n");
		return;
	}

	/* Its parts in the order of the enum. */
	printf("ok");
	for (part_index = NET_URL_HREF; part_index <= NET_URL_HASH; part_index++) {
		wb_buffer_init(&part);
		net_url_component(&url, part_index, &part);
		put_hex(&part);
		wb_buffer_release(&part);
	}
	printf("\n");
	net_url_release(&url);
}

/* Processes a data: URL and writes its MIME type and body. */
static void
run_data(
	const char *input)
{
	struct wb_buffer text;
	struct net_url url;
	struct net_data data;
	int error;

	/* The URL, then its data. */
	wb_buffer_init(&text);
	unhex(input, &text);
	error = net_url_parse(wb_buffer_string(&text), text.length, NULL, &url);
	wb_buffer_release(&text);
	if (error != 0) {
		printf("failure\n");
		return;
	}
	error = net_data_parse(&url, &data);
	net_url_release(&url);
	if (error != 0) {
		printf("failure\n");
		return;
	}

	/* The MIME type and the body. */
	printf("ok");
	put_hex(&data.mime);
	put_hex(&data.body);
	printf("\n");
	net_data_release(&data);
}
