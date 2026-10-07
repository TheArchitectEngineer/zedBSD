/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * IPP (RFC 8010, 8011; plan/ws145/design.md §5.4) over plain HTTP/1.1,
 * one request a connection: Get-Printer-Attributes (the printer's name,
 * whether it takes PDF, and the path that answers), Print-Job with the
 * document after the message, Get-Job-Attributes every five seconds until
 * the job ends, and Cancel-Job.  Version 2.0 first, 1.1 when the printer
 * says it does not take 2.0.
 */

#include "printd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The operations. */
#define IPP_PRINT_JOB		0x0002U
#define IPP_CANCEL_JOB		0x0008U
#define IPP_GET_JOB		0x0009U
#define IPP_GET_PRINTER		0x000bU

/* The delimiter tags, and the value tags written and read. */
#define IPP_OPERATION_GROUP	0x01U
#define IPP_JOB_GROUP		0x02U
#define IPP_END			0x03U
#define IPP_INTEGER		0x21U
#define IPP_ENUM		0x23U
#define IPP_TEXT_LANGUAGE	0x35U
#define IPP_NAME_LANGUAGE	0x36U
#define IPP_TEXT		0x41U
#define IPP_NAME		0x42U
#define IPP_KEYWORD		0x44U
#define IPP_URI			0x45U
#define IPP_CHARSET		0x47U
#define IPP_LANGUAGE		0x48U
#define IPP_MIME		0x49U

/* The status codes read. */
#define IPP_NOT_AUTHENTICATED	0x0402U
#define IPP_NOT_AUTHORIZED	0x0403U
#define IPP_NOT_FOUND		0x0406U
#define IPP_FORMAT		0x040aU
#define IPP_NOT_ACCEPTING	0x0506U
#define IPP_BUSY		0x0507U
#define IPP_VERSION		0x0503U

/* The job states. */
#define IPP_CANCELED		7
#define IPP_ABORTED		8
#define IPP_COMPLETED		9

/* The most of a response kept, and how long and how often a job is watched (seconds). */
#define IPP_RESPONSE_MAX	(256U * 1024U)
#define IPP_WATCH_SECONDS	(30 * 60)
#define IPP_WATCH_EVERY		5
#define IPP_BUSY_TRIES		3
#define IPP_BUSY_WAIT		30

/* A message being written. */
struct ipp_message {
	unsigned char *data;
	size_t length;
	size_t room;
	int failed;
};

/*
 * What a response said: the HTTP status, the IPP status and request-id,
 * the job's id and state (-1 for none), whether the printer lists PDF and
 * lists formats at all, and its info and model.
 */
struct ipp_answer {
	int http;
	unsigned status;
	uint32_t request;
	int job_id;
	int job_state;
	int has_pdf;
	int has_formats;
	char info[PD_NAME_MAX];
	char model[PD_NAME_MAX];
};

/* The request-ids, one after another for the daemon's life. */
static uint32_t ipp_next_request = 1;
static pthread_mutex_t ipp_lock = PTHREAD_MUTEX_INITIALIZER;

static int ipp_find_path(const char *host, unsigned port, const char *first, char *path, size_t size, struct ipp_answer *answer, int *minor, const char **detail);
static int ipp_ask(const char *host, unsigned port, const char *path, unsigned operation, int minor, int job_id, const char *job_name, struct pd_job *document, struct ipp_answer *answer, const char **detail);
static void ipp_message_put(struct ipp_message *message, const void *data, size_t size);
static void ipp_message_u16(struct ipp_message *message, unsigned value);
static void ipp_attribute(struct ipp_message *message, unsigned tag, const char *name, const void *value, size_t size);
static void ipp_text_attribute(struct ipp_message *message, unsigned tag, const char *name, const char *value);
static int ipp_exchange(const char *host, unsigned port, const char *path, const struct ipp_message *message, struct pd_job *document, unsigned char **body, size_t *body_length, int *http, const char **detail);
static int ipp_parse(const unsigned char *body, size_t length, struct ipp_answer *answer);
static int ipp_named(const char *name);
static void ipp_take_text(const unsigned char *value, size_t size, unsigned tag, char *text, size_t room);
static const char *ipp_detail(const struct ipp_answer *answer);
static uint32_t ipp_request_id(void);
static uint32_t ipp_be32(const unsigned char *bytes);

/*
 * Sends a job: the printer asked first (its path, whether it takes PDF),
 * then the document, then the job watched until it ends; its STATE lines
 * go to the backend.
 */
void
pd_ipp_job(
	struct pd_job *job)
{
	struct ipp_answer answer;
	const char *detail;
	char path[PD_PATH_MAX];
	time_t started;
	time_t now;
	int minor;
	int status;
	int tries;
	int job_id;
	int stop;
	int same;

	/* The printer, at its path. */
	status = ipp_find_path(job->host, job->port, job->path, path, sizeof(path), &answer, &minor, &detail);
	if (status != 0) {
		pd_send("STATE %lu failed %s", (unsigned long)job->job, detail);
		return;
	}

	/* A path of its own, kept by the backend. */
	same = strcmp(path, job->path);
	if (same != 0) {
		pd_send("PATH %lu %s", (unsigned long)job->job, path);
		(void)snprintf(job->path, sizeof(job->path), "%s", path);
	}

	/* A printer that lists its formats without PDF. */
	if (answer.has_formats && !answer.has_pdf) {
		pd_send("STATE %lu failed format", (unsigned long)job->job);
		return;
	}

	/* The document, again a few times while the printer is busy. */
	pd_send("STATE %lu sending", (unsigned long)job->job);
	for (tries = 0;; tries++) {
		status = ipp_ask(job->host, job->port, job->path, IPP_PRINT_JOB, minor, 0, job->title, job, &answer, &detail);
		if (status == ECANCELED) {
			pd_send("STATE %lu cancelled", (unsigned long)job->job);
			return;
		}

		/* Not sent. */
		if (status != 0) {
			pd_send("STATE %lu failed %s", (unsigned long)job->job, detail);
			return;
		}

		/* A busy printer is tried again later. */
		if (answer.status != IPP_BUSY || tries + 1 >= IPP_BUSY_TRIES)
			break;
		sleep(IPP_BUSY_WAIT);
	}

	/* Refused. */
	if (answer.http != 200 || answer.status > 0x00ffU) {
		pd_send("STATE %lu failed %s", (unsigned long)job->job, ipp_detail(&answer));
		return;
	}

	/* Taken by the printer. */
	pd_log("job %lu sent", (unsigned long)job->job);

	/* Without a job-id it cannot be watched: done, not confirmed. */
	job_id = answer.job_id;
	if (job_id <= 0) {
		pd_send("STATE %lu done unconfirmed", (unsigned long)job->job);
		return;
	}

	/* Watched from here. */
	pd_send("STATE %lu waiting", (unsigned long)job->job);

	/* Watched until it ends, asked to stop, or half an hour went. */
	started = time(NULL);
	for (;;) {
		/* Asked to stop: Cancel-Job. */
		stop = pd_cancelled(job);
		if (stop) {
			status = ipp_ask(job->host, job->port, job->path, IPP_CANCEL_JOB, minor, job_id, NULL, NULL, &answer, &detail);
			if (status == 0 && answer.http == 200 && answer.status <= 0x00ffU)
				pd_send("STATE %lu cancelled", (unsigned long)job->job);
			else
				pd_send("STATE %lu failed unconfirmed", (unsigned long)job->job);
			return;
		}

		/* Its state; a printer that does not tell it is done, not confirmed. */
		status = ipp_ask(job->host, job->port, job->path, IPP_GET_JOB, minor, job_id, NULL, NULL, &answer, &detail);
		if (status == 0 && answer.job_state < 0) {
			pd_send("STATE %lu done unconfirmed", (unsigned long)job->job);
			return;
		}

		/* Completed. */
		if (status == 0 && answer.job_state == IPP_COMPLETED) {
			pd_send("STATE %lu done", (unsigned long)job->job);
			return;
		}

		/* Cancelled at the printer. */
		if (status == 0 && answer.job_state == IPP_CANCELED) {
			pd_send("STATE %lu cancelled", (unsigned long)job->job);
			return;
		}

		/* Aborted by the printer. */
		if (status == 0 && answer.job_state == IPP_ABORTED) {
			pd_send("STATE %lu failed printer", (unsigned long)job->job);
			return;
		}

		/* Too long: done, not confirmed. */
		now = time(NULL);
		if (now - started >= IPP_WATCH_SECONDS) {
			pd_send("STATE %lu done unconfirmed", (unsigned long)job->job);
			return;
		}

		/* The next look. */
		sleep(IPP_WATCH_EVERY);
	}
}

/*
 * Answers NAME: the printer asked at the usual paths for its info or
 * model; NAMED with the path and the name, or NAMED alone.
 */
void
pd_ipp_name(
	const struct pd_name *name)
{
	struct ipp_answer answer;
	const char *detail;
	const char *text;
	char path[PD_PATH_MAX];
	int minor;
	int status;

	/* The printer at a path that answers. */
	status = ipp_find_path(name->host, name->port, "/ipp/print", path, sizeof(path), &answer, &minor, &detail);
	if (status != 0) {
		pd_send("NAMED %lu", (unsigned long)name->seq);
		return;
	}

	/* Its info, else its model, else its address. */
	text = answer.info;
	if (text[0] == '\0')
		text = answer.model;
	if (text[0] == '\0') {
		pd_send("NAMED %lu %s %s (IPP)", (unsigned long)name->seq, path, name->host);
		return;
	}

	/* Its own name. */
	pd_send("NAMED %lu %s %s", (unsigned long)name->seq, path, text);
}

/*
 * Finds the path a printer answers at: the one given, then /ipp/print,
 * /ipp and /.  Version 2.0 first, 1.1 when 2.0 is refused (*minor says
 * which).  Returns 0 with the path and the printer's answer, or an errno
 * value with the word of the failure.
 */
static int
ipp_find_path(
	const char *host,
	unsigned port,
	const char *first,
	char *path,
	size_t size,
	struct ipp_answer *answer,
	int *minor,
	const char **detail)
{
	static const char *const usual[] = { "/ipp/print", "/ipp", "/" };
	const char *tried[4];
	size_t count;
	size_t index;
	size_t seen;
	int duplicate;
	int status;

	/* The paths to try, the given one first, each once. */
	count = 0;
	tried[count] = first;
	count++;
	for (index = 0; index < 3U; index++) {
		duplicate = 0;
		for (seen = 0; seen < count; seen++)
			duplicate |= strcmp(tried[seen], usual[index]) == 0;
		if (!duplicate) {
			tried[count] = usual[index];
			count++;
		}
	}

	/* Each, until one answers. */
	*detail = "refused";
	for (index = 0; index < count; index++) {
		*minor = 0;
		status = ipp_ask(host, port, tried[index], IPP_GET_PRINTER, 0, 0, NULL, NULL, answer, detail);
		if (status == 0 && answer->status == IPP_VERSION) {
			*minor = 1;
			status = ipp_ask(host, port, tried[index], IPP_GET_PRINTER, 1, 0, NULL, NULL, answer, detail);
		}

		/* Not reached, not there, refused, or found. */
		if (status != 0)
			return status;
		if (answer->http == 404 || answer->status == IPP_NOT_FOUND)
			continue;
		if (answer->http != 200 || answer->status > 0x00ffU) {
			*detail = ipp_detail(answer);
			return EIO;
		}

		/* This path answers. */
		(void)snprintf(path, size, "%s", tried[index]);
		return 0;
	}

	/* None answered. */
	*detail = "refused";
	return ENOENT;
}

/*
 * Makes a request (its operation's attributes), sends it (with the job's
 * document after it for Print-Job) and reads the answer.  minor is 0 for
 * version 2.0, 1 for 1.1.  Returns 0 with the answer, ECANCELED, or an
 * errno value with the word of the failure.
 */
static int
ipp_ask(
	const char *host,
	unsigned port,
	const char *path,
	unsigned operation,
	int minor,
	int job_id,
	const char *job_name,
	struct pd_job *document,
	struct ipp_answer *answer,
	const char **detail)
{
	static const char *const printer_wanted[] = { "printer-state", "document-format-supported", "printer-info", "printer-make-and-model" };
	static const char *const job_wanted[] = { "job-state", "job-state-reasons" };
	struct ipp_message message;
	unsigned char *body;
	unsigned char number[4];
	char uri[256];
	size_t body_length;
	size_t index;
	uint32_t request;
	int status;

	/* The header: version, operation, request-id. */
	memset(&message, 0, sizeof(message));
	memset(answer, 0, sizeof(*answer));
	answer->job_state = -1;
	request = ipp_request_id();
	if (minor)
		ipp_message_u16(&message, 0x0101U);
	else
		ipp_message_u16(&message, 0x0200U);
	ipp_message_u16(&message, operation);
	number[0] = (unsigned char)(request >> 24);
	number[1] = (unsigned char)(request >> 16);
	number[2] = (unsigned char)(request >> 8);
	number[3] = (unsigned char)request;
	ipp_message_put(&message, number, 4U);

	/* The operation's attributes, in their order. */
	(void)snprintf(uri, sizeof(uri), "ipp://%s:%u%s", host, port, path);
	ipp_message_put(&message, "\x01", 1U);
	ipp_text_attribute(&message, IPP_CHARSET, "attributes-charset", "utf-8");
	ipp_text_attribute(&message, IPP_LANGUAGE, "attributes-natural-language", "en");
	ipp_text_attribute(&message, IPP_URI, "printer-uri", uri);
	if (job_id > 0) {
		number[0] = (unsigned char)((uint32_t)job_id >> 24);
		number[1] = (unsigned char)((uint32_t)job_id >> 16);
		number[2] = (unsigned char)((uint32_t)job_id >> 8);
		number[3] = (unsigned char)job_id;
		ipp_attribute(&message, IPP_INTEGER, "job-id", number, 4U);
	}

	/* Who asks, and the operation's own. */
	ipp_text_attribute(&message, IPP_NAME, "requesting-user-name", pd_user());
	if (operation == IPP_PRINT_JOB) {
		if (job_name == NULL || job_name[0] == '\0')
			job_name = "Document";
		ipp_text_attribute(&message, IPP_NAME, "job-name", job_name);
		ipp_text_attribute(&message, IPP_MIME, "document-format", "application/pdf");
	}

	/* The printer's attributes wanted: the name once, then the other values. */
	if (operation == IPP_GET_PRINTER) {
		ipp_text_attribute(&message, IPP_KEYWORD, "requested-attributes", printer_wanted[0]);
		for (index = 1; index < 4U; index++)
			ipp_text_attribute(&message, IPP_KEYWORD, "", printer_wanted[index]);
	}

	/* The job's attributes wanted. */
	if (operation == IPP_GET_JOB) {
		ipp_text_attribute(&message, IPP_KEYWORD, "requested-attributes", job_wanted[0]);
		ipp_text_attribute(&message, IPP_KEYWORD, "", job_wanted[1]);
	}

	/* The end of the attributes. */
	ipp_message_put(&message, "\x03", 1U);
	if (message.failed) {
		free(message.data);
		*detail = "io";
		return ENOMEM;
	}

	/* Sent and answered. */
	status = ipp_exchange(host, port, path, &message, document, &body, &body_length, &answer->http, detail);
	free(message.data);
	if (status != 0)
		return status;

	/* The answer read; a body that is not IPP is only its HTTP status. */
	if (answer->http == 200) {
		status = ipp_parse(body, body_length, answer);
		if (status != 0 || answer->request != request) {
			free(body);
			*detail = "protocol";
			return EPROTO;
		}
	}

	/* The body goes. */
	free(body);
	return 0;
}

/* Appends bytes to a message (a failure marks it failed). */
static void
ipp_message_put(
	struct ipp_message *message,
	const void *data,
	size_t size)
{
	unsigned char *grown;
	size_t room;

	/* Room. */
	if (message->failed)
		return;
	if (message->length + size > message->room) {
		room = message->room * 2U + size + 256U;
		grown = realloc(message->data, room);
		if (grown == NULL) {
			message->failed = 1;
			return;
		}

		/* Grown. */
		message->data = grown;
		message->room = room;
	}

	/* The bytes. */
	memcpy(message->data + message->length, data, size);
	message->length += size;
}

/* Appends a big-endian 16-bit number. */
static void
ipp_message_u16(
	struct ipp_message *message,
	unsigned value)
{
	unsigned char bytes[2];

	/* High byte first. */
	bytes[0] = (unsigned char)(value >> 8);
	bytes[1] = (unsigned char)value;
	ipp_message_put(message, bytes, 2U);
}

/* Appends an attribute: its tag, its name (empty for another value of the one before) and its value. */
static void
ipp_attribute(
	struct ipp_message *message,
	unsigned tag,
	const char *name,
	const void *value,
	size_t size)
{
	unsigned char byte;

	/* The tag, the name, the value. */
	byte = (unsigned char)tag;
	ipp_message_put(message, &byte, 1U);
	ipp_message_u16(message, (unsigned)strlen(name));
	ipp_message_put(message, name, strlen(name));
	ipp_message_u16(message, (unsigned)size);
	ipp_message_put(message, value, size);
}

/* Appends an attribute whose value is a text. */
static void
ipp_text_attribute(
	struct ipp_message *message,
	unsigned tag,
	const char *name,
	const char *value)
{
	/* The text's bytes. */
	ipp_attribute(message, tag, name, value, strlen(value));
}

/*
 * Sends a message by HTTP POST (with a job's document after it) and reads
 * the response's status and body (the first IPP_RESPONSE_MAX bytes).
 * Returns 0 (the body is the caller's to free), ECANCELED, or an errno
 * value with the word of the failure.
 */
static int
ipp_exchange(
	const char *host,
	unsigned port,
	const char *path,
	const struct ipp_message *message,
	struct pd_job *document,
	unsigned char **body,
	size_t *body_length,
	int *http,
	const char **detail)
{
	unsigned char *response;
	unsigned char *start;
	unsigned char *cursor;
	unsigned char *out;
	char header[512];
	uint64_t length;
	size_t got;
	size_t chunk;
	ssize_t arrived;
	long declared;
	char *end;
	char *field;
	int informational;
	int chunked;
	int version;
	int status;
	int fd;

	/* The connection. */
	*body = NULL;
	*body_length = 0;
	*http = 0;
	status = pd_connect(host, port, &fd, detail);
	if (status != 0)
		return status;

	/* The request's header and the message, then the document. */
	length = message->length;
	if (document != NULL)
		length += document->size;
	(void)snprintf(header, sizeof(header),
	    "POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
	    path, host, port, (unsigned long long)length);
	status = pd_write_all(fd, header, strlen(header));
	if (status == 0)
		status = pd_write_all(fd, message->data, message->length);
	if (status == 0 && document != NULL)
		status = pd_send_file(fd, document);
	if (status != 0) {
		(void)close(fd);
		*detail = "io";
		if (status == ETIMEDOUT || status == EAGAIN)
			*detail = "timeout";
		return status;
	}

	/* The response, until the printer closes (or as much as is kept). */
	response = malloc(IPP_RESPONSE_MAX + 1U);
	if (response == NULL) {
		(void)close(fd);
		return ENOMEM;
	}

	/* Read as it comes. */
	got = 0;
	for (;;) {
		arrived = pd_read_some(fd, response + got, IPP_RESPONSE_MAX - got);
		if (arrived <= 0)
			break;
		got += (size_t)arrived;

		/* A whole body by its length ends the reading early. */
		response[got] = '\0';
		start = (unsigned char *)strstr((char *)response, "\r\n\r\n");
		field = strstr((char *)response, "Content-Length:");
		if (field == NULL)
			field = strstr((char *)response, "content-length:");
		if (start != NULL && field != NULL && (unsigned char *)field < start) {
			declared = strtol(field + 15, NULL, 10);
			informational = strncmp((char *)response + 9, "100", 3U);
			if (declared >= 0 && got >= (size_t)(start + 4 - response) + (size_t)declared && informational != 0)
				break;
		}

		/* As much as is kept. */
		if (got == IPP_RESPONSE_MAX)
			break;
	}

	/* The connection ends. */
	(void)close(fd);
	response[got] = '\0';

	/* Past the 1xx responses to the final one. */
	cursor = response;
	for (;;) {
		start = (unsigned char *)strstr((char *)cursor, "\r\n\r\n");
		version = strncmp((char *)cursor, "HTTP/1.", 7U);
		if (start == NULL || version != 0) {
			free(response);
			*detail = "protocol";
			return EPROTO;
		}

		/* Its status: a final one ends the search. */
		*http = atoi((char *)cursor + 9);
		if (*http >= 200)
			break;
		cursor = start + 4;
	}

	/* The body: chunked, or the rest. */
	*start = '\0';
	chunked = strstr((char *)cursor, "chunked") != NULL;
	cursor = start + 4;
	got = (size_t)(response + got - cursor);
	if (chunked) {
		/* Each chunk's size line and bytes, packed together. */
		out = response;
		start = cursor;
		while (start < cursor + got) {
			chunk = (size_t)strtoul((char *)start, &end, 16);
			end = strstr(end, "\r\n");
			if (end == NULL || chunk == 0U)
				break;
			start = (unsigned char *)end + 2;
			if (start + chunk > cursor + got)
				chunk = (size_t)(cursor + got - start);
			memmove(out, start, chunk);
			out += chunk;
			start += chunk + 2U;
		}

		/* The bytes packed. */
		got = (size_t)(out - response);
	} else {
		memmove(response, cursor, got);
	}

	/* Succeeded: the body. */
	*body = response;
	*body_length = got;
	*detail = "";
	return 0;
}

/* Reads an IPP response: its status, request-id and the attributes wanted.  Returns 0 or EPROTO. */
static int
ipp_parse(
	const unsigned char *body,
	size_t length,
	struct ipp_answer *answer)
{
	char name[64];
	char last[64];
	size_t offset;
	size_t name_length;
	size_t value_length;
	unsigned tag;
	int which;
	int pdf;

	/* The header: version, status, request-id. */
	if (length < 9U)
		return EPROTO;
	answer->status = (unsigned)body[2] << 8 | body[3];
	answer->request = ipp_be32(body + 4);
	offset = 8;
	last[0] = '\0';

	/* The groups and their attributes, until the end. */
	while (offset < length) {
		tag = body[offset];
		offset++;
		if (tag == IPP_END)
			return 0;
		if (tag <= 0x0fU)
			continue;

		/* An attribute: its name (empty for another value of the last one) and its value. */
		if (offset + 2U > length)
			return EPROTO;
		name_length = (size_t)body[offset] << 8 | body[offset + 1U];
		offset += 2U;
		if (offset + name_length + 2U > length)
			return EPROTO;
		if (name_length > 0U) {
			(void)snprintf(name, sizeof(name), "%.*s", (int)name_length, (const char *)body + offset);
			(void)snprintf(last, sizeof(last), "%s", name);
		}

		/* The value's length and bytes. */
		offset += name_length;
		value_length = (size_t)body[offset] << 8 | body[offset + 1U];
		offset += 2U;
		if (offset + value_length > length)
			return EPROTO;

		/* The values wanted, by the attribute's name. */
		which = ipp_named(last);
		if (which == 1 && tag == IPP_INTEGER && value_length == 4U)
			answer->job_id = (int)ipp_be32(body + offset);
		else if (which == 2 && tag == IPP_ENUM && value_length == 4U)
			answer->job_state = (int)ipp_be32(body + offset);
		else if (which == 3 && tag == IPP_MIME) {
			answer->has_formats = 1;
			pdf = 1;
			if (value_length == 15U)
				pdf = memcmp(body + offset, "application/pdf", 15U);
			if (pdf == 0)
				answer->has_pdf = 1;
		} else if (which == 4 && answer->info[0] == '\0')
			ipp_take_text(body + offset, value_length, tag, answer->info, sizeof(answer->info));
		else if (which == 5 && answer->model[0] == '\0')
			ipp_take_text(body + offset, value_length, tag, answer->model, sizeof(answer->model));
		offset += value_length;
	}

	/* No end of the attributes. */
	return EPROTO;
}

/*
 * Tells which attribute a name is, of those read: 1 job-id, 2 job-state,
 * 3 document-format-supported, 4 printer-info, 5 printer-make-and-model,
 * 0 another.
 */
static int
ipp_named(
	const char *name)
{
	static const char *const names[] = { "job-id", "job-state", "document-format-supported", "printer-info", "printer-make-and-model" };
	size_t index;
	int same;

	/* Each name read. */
	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		same = strcmp(name, names[index]);
		if (same == 0)
			return (int)index + 1;
	}

	/* Another. */
	return 0;
}

/*
 * Takes a text value (its language left out for textWithLanguage and
 * nameWithLanguage): control characters become spaces, and it is cut at a
 * character's boundary to fit.
 */
static void
ipp_take_text(
	const unsigned char *value,
	size_t size,
	unsigned tag,
	char *text,
	size_t room)
{
	size_t skip;
	size_t index;
	size_t length;

	/* With a language: its length and bytes, then the text's length and bytes. */
	if (tag == IPP_TEXT_LANGUAGE || tag == IPP_NAME_LANGUAGE) {
		if (size < 2U)
			return;
		skip = ((size_t)value[0] << 8 | value[1]) + 2U;
		if (skip + 2U > size)
			return;
		value += skip + 2U;
		size -= skip + 2U;
	} else if (tag != IPP_TEXT && tag != IPP_NAME) {
		return;
	}

	/* As much as fits, cut where a character starts. */
	length = size;
	if (length > room - 1U) {
		length = room - 1U;
		while (length > 0U && (value[length] & 0xc0U) == 0x80U)
			length--;
	}

	/* The bytes, control characters made spaces. */
	for (index = 0; index < length; index++) {
		text[index] = (char)value[index];
		if (value[index] < 0x20U || value[index] == 0x7fU)
			text[index] = ' ';
	}

	/* Ended. */
	text[length] = '\0';
}

/* The word of a failed answer (plan/ws145/design.md §5.4). */
static const char *
ipp_detail(
	const struct ipp_answer *answer)
{
	/* HTTP's own failures. */
	if (answer->http == 401 || answer->http == 403)
		return "auth";
	if (answer->http == 426)
		return "tls";
	if (answer->http != 200)
		return "refused";

	/* IPP's. */
	if (answer->status == IPP_FORMAT)
		return "format";
	if (answer->status == IPP_NOT_AUTHENTICATED || answer->status == IPP_NOT_AUTHORIZED)
		return "auth";
	if (answer->status == IPP_BUSY)
		return "busy";
	if (answer->status == IPP_NOT_ACCEPTING)
		return "stopped";
	return "refused";
}

/* The next request-id (1 to 2^31 - 1). */
static uint32_t
ipp_request_id(void)
{
	uint32_t request;

	/* Under the lock: the threads share the count. */
	(void)pthread_mutex_lock(&ipp_lock);
	request = ipp_next_request;
	ipp_next_request++;
	if (ipp_next_request > 0x7fffffffU)
		ipp_next_request = 1;
	(void)pthread_mutex_unlock(&ipp_lock);
	return request;
}

/* Reads a big-endian 32-bit number. */
static uint32_t
ipp_be32(
	const unsigned char *bytes)
{
	uint32_t number;

	/* The most significant byte first. */
	number = (uint32_t)bytes[0] << 24;
	number |= (uint32_t)bytes[1] << 16;
	number |= (uint32_t)bytes[2] << 8;
	number |= (uint32_t)bytes[3];
	return number;
}
