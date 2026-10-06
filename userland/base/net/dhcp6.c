/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DHCPv6 messages of `dhcpc -6` (ws130-p007): a client's messages
 * built and a server's read (RFC 8415, with RFC 3646's DNS options).
 */

#include "userland/base/net/dhcp6.h"

#include <string.h>

/* The options a client writes or reads (RFC 8415 section 21, RFC 3646). */
#define DHCP6_OPTION_CLIENTID	1U
#define DHCP6_OPTION_SERVERID	2U
#define DHCP6_OPTION_IA_NA	3U
#define DHCP6_OPTION_IAADDR	5U
#define DHCP6_OPTION_ORO	6U
#define DHCP6_OPTION_PREFERENCE	7U
#define DHCP6_OPTION_ELAPSED	8U
#define DHCP6_OPTION_STATUS	13U
#define DHCP6_OPTION_DNS	23U
#define DHCP6_OPTION_DOMAINS	24U
#define DHCP6_OPTION_REFRESH	32U
#define DHCP6_OPTION_SOL_MAX_RT	82U
#define DHCP6_OPTION_INF_MAX_RT	83U

/* The fixed parts: a message's type and transaction, an option's code and length, an IA_NA's and an IAADDR's. */
#define DHCP6_HEADER		4U
#define DHCP6_OPTION_HEADER	4U
#define DHCP6_IA_NA_FIXED	12U
#define DHCP6_IAADDR_FIXED	24U

/* A transaction identifier has 24 bits. */
#define DHCP6_XID_MASK		0x00ffffffU

/* The longest DNS label. */
#define DHCP6_LABEL_MAX		63U

/* A message being written: its buffer, its room and how much is used; a write past the room marks it failed. */
struct dhcp6_writer {
	uint8_t *buffer;
	size_t capacity;
	size_t used;
	int failed;
};

static void dhcp6_put(struct dhcp6_writer *writer, const void *bytes, size_t length);
static void dhcp6_put16(struct dhcp6_writer *writer, unsigned value);
static void dhcp6_put32(struct dhcp6_writer *writer, uint32_t value);
static void dhcp6_option(struct dhcp6_writer *writer, unsigned code, size_t length);
static uint16_t dhcp6_read16(const uint8_t *bytes);
static uint32_t dhcp6_read32(const uint8_t *bytes);
static int dhcp6_option_at(const uint8_t *options, size_t length, size_t offset, unsigned *code, size_t *size);
static int dhcp6_ia_na(const uint8_t *option, size_t length, struct dhcp6_reply *reply);
static int dhcp6_iaaddr(const uint8_t *option, size_t length, struct dhcp6_reply *reply);
static void dhcp6_dns(const uint8_t *option, size_t length, struct dhcp6_reply *reply);
static void dhcp6_domains(const uint8_t *option, size_t length, struct dhcp6_reply *reply);
static int dhcp6_label_valid(const uint8_t *label, size_t length);

/*
 * Builds a client's message: the client's identifier, the server's for a
 * Request or a Renew, an IA_NA for any but an Information-Request, the
 * options asked for, and the elapsed time.  Returns 0, or -1 when the
 * buffer is too small or the request lacks what its type needs.
 */
int
dhcp6_build(
	uint8_t *buffer,
	size_t capacity,
	size_t *length,
	const struct dhcp6_request *request)
{
	struct dhcp6_writer writer;
	unsigned requested[4];
	unsigned count;
	unsigned index;
	int needs_server;

	/* A type a client sends, with the identifiers it needs. */
	needs_server = request->type == DHCP6_REQUEST || request->type == DHCP6_RENEW;
	if (request->type != DHCP6_SOLICIT && request->type != DHCP6_INFORMATION && !needs_server)
		return -1;
	if (request->client == NULL || (needs_server && request->server == NULL))
		return -1;

	/* The type and the transaction. */
	writer.buffer = buffer;
	writer.capacity = capacity;
	writer.used = 0;
	writer.failed = 0;
	dhcp6_put32(&writer, (uint32_t)request->type << 24 | (request->xid & DHCP6_XID_MASK));

	/* The identifiers. */
	dhcp6_option(&writer, DHCP6_OPTION_CLIENTID, request->client->length);
	dhcp6_put(&writer, request->client->bytes, request->client->length);
	if (needs_server) {
		dhcp6_option(&writer, DHCP6_OPTION_SERVERID, request->server->length);
		dhcp6_put(&writer, request->server->bytes, request->server->length);
	}

	/* The IA_NA, with its address when one is asked for (times 0: the server's choice). */
	if (request->with_ia && request->type != DHCP6_INFORMATION) {
		dhcp6_option(&writer, DHCP6_OPTION_IA_NA,
		    DHCP6_IA_NA_FIXED + (request->with_address ? DHCP6_OPTION_HEADER + DHCP6_IAADDR_FIXED : 0U));
		dhcp6_put32(&writer, request->iaid);
		dhcp6_put32(&writer, 0U);
		dhcp6_put32(&writer, 0U);
		if (request->with_address) {
			dhcp6_option(&writer, DHCP6_OPTION_IAADDR, DHCP6_IAADDR_FIXED);
			dhcp6_put(&writer, request->address.s6_addr, sizeof(request->address.s6_addr));
			dhcp6_put32(&writer, 0U);
			dhcp6_put32(&writer, 0U);
		}
	}

	/* The options asked for: the DNS's, and the times RFC 8415 says each type asks for. */
	count = 0;
	requested[count++] = DHCP6_OPTION_DNS;
	requested[count++] = DHCP6_OPTION_DOMAINS;
	if (request->type == DHCP6_INFORMATION) {
		requested[count++] = DHCP6_OPTION_REFRESH;
		requested[count++] = DHCP6_OPTION_INF_MAX_RT;
	} else {
		requested[count++] = DHCP6_OPTION_SOL_MAX_RT;
	}
	dhcp6_option(&writer, DHCP6_OPTION_ORO, count * 2U);
	for (index = 0; index < count; index++)
		dhcp6_put16(&writer, requested[index]);

	/* The elapsed time. */
	dhcp6_option(&writer, DHCP6_OPTION_ELAPSED, 2U);
	dhcp6_put16(&writer, request->elapsed);
	if (writer.failed)
		return -1;

	/* Succeeded. */
	*length = writer.used;
	return 0;
}

/*
 * Reads a server's Advertise or Reply to the transaction xid.  With a
 * client identifier, the message must carry the same one.  Returns 0, or
 * -1 when it is not an answer to this client, an Advertise has no server
 * identifier, or an option runs past its end.
 */
int
dhcp6_parse(
	const uint8_t *message,
	size_t length,
	uint32_t xid,
	const struct dhcp6_duid *client,
	struct dhcp6_reply *reply)
{
	const uint8_t *options;
	const uint8_t *data;
	size_t remaining;
	size_t offset;
	size_t size;
	unsigned code;
	int client_seen;
	int status;

	/* An Advertise or a Reply to this transaction. */
	memset(reply, 0, sizeof(*reply));
	reply->status = DHCP6_STATUS_NONE;
	reply->ia_status = DHCP6_STATUS_NONE;
	if (length < DHCP6_HEADER)
		return -1;
	reply->type = message[0];
	if (reply->type != DHCP6_ADVERTISE && reply->type != DHCP6_REPLY)
		return -1;
	if ((dhcp6_read32(message) & DHCP6_XID_MASK) != (xid & DHCP6_XID_MASK))
		return -1;

	/* Each option. */
	client_seen = 0;
	options = message + DHCP6_HEADER;
	remaining = length - DHCP6_HEADER;
	offset = 0;
	while (offset < remaining) {
		status = dhcp6_option_at(options, remaining, offset, &code, &size);
		if (status != 0)
			return -1;
		data = options + offset + DHCP6_OPTION_HEADER;
		offset += DHCP6_OPTION_HEADER + size;

		/* The ones a client acts on; others are passed over. */
		if (code == DHCP6_OPTION_CLIENTID) {
			client_seen = client == NULL || (size == client->length && memcmp(data, client->bytes, size) == 0);
			if (!client_seen)
				return -1;
		} else if (code == DHCP6_OPTION_SERVERID) {
			if (size == 0U || size > DHCP6_DUID_MAX)
				return -1;
			memcpy(reply->server.bytes, data, size);
			reply->server.length = size;
		} else if (code == DHCP6_OPTION_PREFERENCE && size == 1U) {
			reply->preference = data[0];
		} else if (code == DHCP6_OPTION_STATUS && size >= 2U) {
			reply->status = dhcp6_read16(data);
		} else if (code == DHCP6_OPTION_IA_NA && !reply->has_ia) {
			status = dhcp6_ia_na(data, size, reply);
			if (status != 0)
				return -1;
		} else if (code == DHCP6_OPTION_DNS) {
			dhcp6_dns(data, size, reply);
		} else if (code == DHCP6_OPTION_DOMAINS) {
			dhcp6_domains(data, size, reply);
		} else if (code == DHCP6_OPTION_REFRESH && size == 4U) {
			reply->refresh = dhcp6_read32(data);
		}
	}

	/*
	 * The client's identifier when one was sent, and a server's on an
	 * Advertise (a Request names it).  A Reply without the server's is
	 * taken: QEMU's user network sends none to an Information-Request.
	 */
	if (client != NULL && !client_seen)
		return -1;
	if (reply->type == DHCP6_ADVERTISE && reply->server.length == 0U)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Makes a DUID-UUID (RFC 6355) from 16 random bytes, marked as a version 4 (random) UUID. */
void
dhcp6_duid_uuid(
	const uint8_t *random,
	struct dhcp6_duid *duid)
{
	/* The type, then the UUID with its version and variant bits. */
	duid->bytes[0] = 0;
	duid->bytes[1] = DHCP6_DUID_UUID;
	memcpy(duid->bytes + 2, random, 16U);
	duid->bytes[2 + 6] = (uint8_t)((duid->bytes[2 + 6] & 0x0fU) | 0x40U);
	duid->bytes[2 + 8] = (uint8_t)((duid->bytes[2 + 8] & 0x3fU) | 0x80U);
	duid->length = DHCP6_DUID_UUID_LENGTH;
}

/*
 * Gives an interface's IAID: FNV-1a over its name, the same on every boot
 * and without its MAC address (an interface's index can change).
 */
uint32_t
dhcp6_iaid(
	const char *interface)
{
	uint32_t hash;
	const char *letter;

	/* Each byte of the name. */
	hash = 2166136261U;
	for (letter = interface; *letter != '\0'; letter++) {
		hash ^= (uint8_t)*letter;
		hash *= 16777619U;
	}

	/* Succeeded. */
	return hash;
}

/* Writes bytes, or marks the message failed when they do not fit. */
static void
dhcp6_put(
	struct dhcp6_writer *writer,
	const void *bytes,
	size_t length)
{
	/* Room for them. */
	if (writer->failed || length > writer->capacity - writer->used) {
		writer->failed = 1;
		return;
	}

	/* Succeeded. */
	memcpy(writer->buffer + writer->used, bytes, length);
	writer->used += length;
}

/* Writes a 16-bit number in network order. */
static void
dhcp6_put16(
	struct dhcp6_writer *writer,
	unsigned value)
{
	uint8_t bytes[2];

	/* The high byte first. */
	bytes[0] = (uint8_t)(value >> 8);
	bytes[1] = (uint8_t)value;
	dhcp6_put(writer, bytes, sizeof(bytes));
}

/* Writes a 32-bit number in network order. */
static void
dhcp6_put32(
	struct dhcp6_writer *writer,
	uint32_t value)
{
	uint8_t bytes[4];

	/* The high byte first. */
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
	dhcp6_put(writer, bytes, sizeof(bytes));
}

/* Writes an option's code and length. */
static void
dhcp6_option(
	struct dhcp6_writer *writer,
	unsigned code,
	size_t length)
{
	/* A length an option can have. */
	if (length > 0xffffU) {
		writer->failed = 1;
		return;
	}

	/* Succeeded. */
	dhcp6_put16(writer, code);
	dhcp6_put16(writer, (unsigned)length);
}

/* Reads a 16-bit number in network order. */
static uint16_t
dhcp6_read16(
	const uint8_t *bytes)
{
	/* The high byte first. */
	return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
}

/* Reads a 32-bit number in network order. */
static uint32_t
dhcp6_read32(
	const uint8_t *bytes)
{
	/* The high byte first. */
	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

/* Reads the code and length of the option at offset; 0, or -1 when it runs past the end. */
static int
dhcp6_option_at(
	const uint8_t *options,
	size_t length,
	size_t offset,
	unsigned *code,
	size_t *size)
{
	/* Its header, then its data, within the options. */
	if (length - offset < DHCP6_OPTION_HEADER)
		return -1;
	*code = dhcp6_read16(options + offset);
	*size = dhcp6_read16(options + offset + 2);
	if (*size > length - offset - DHCP6_OPTION_HEADER)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Reads an IA_NA: its identifier and times, its status, and its first usable address; 0, or -1 when malformed. */
static int
dhcp6_ia_na(
	const uint8_t *option,
	size_t length,
	struct dhcp6_reply *reply)
{
	const uint8_t *options;
	const uint8_t *data;
	size_t remaining;
	size_t offset;
	size_t size;
	unsigned code;
	int status;

	/* Its fixed part. */
	if (length < DHCP6_IA_NA_FIXED)
		return -1;
	reply->has_ia = 1;
	reply->iaid = dhcp6_read32(option);
	reply->t1 = dhcp6_read32(option + 4);
	reply->t2 = dhcp6_read32(option + 8);

	/* Its options. */
	options = option + DHCP6_IA_NA_FIXED;
	remaining = length - DHCP6_IA_NA_FIXED;
	offset = 0;
	while (offset < remaining) {
		status = dhcp6_option_at(options, remaining, offset, &code, &size);
		if (status != 0)
			return -1;
		data = options + offset + DHCP6_OPTION_HEADER;
		offset += DHCP6_OPTION_HEADER + size;
		if (code == DHCP6_OPTION_STATUS && size >= 2U) {
			reply->ia_status = dhcp6_read16(data);
		} else if (code == DHCP6_OPTION_IAADDR && !reply->has_address) {
			status = dhcp6_iaaddr(data, size, reply);
			if (status != 0)
				return -1;
		}
	}

	/* Succeeded. */
	return 0;
}

/* Reads an IA_NA's address and its lifetimes; one whose preferred lifetime passes its valid one is not taken. */
static int
dhcp6_iaaddr(
	const uint8_t *option,
	size_t length,
	struct dhcp6_reply *reply)
{
	uint32_t preferred;
	uint32_t valid;

	/* Its fixed part. */
	if (length < DHCP6_IAADDR_FIXED)
		return -1;
	preferred = dhcp6_read32(option + 16);
	valid = dhcp6_read32(option + 20);

	/* Lifetimes that make sense (RFC 8415 section 21.6). */
	if (preferred > valid)
		return 0;

	/* Succeeded: kept. */
	reply->has_address = 1;
	memcpy(reply->address.s6_addr, option, 16U);
	reply->preferred = preferred;
	reply->valid = valid;
	return 0;
}

/* Keeps the DNS servers of a DNS Recursive Name Server option, three at most. */
static void
dhcp6_dns(
	const uint8_t *option,
	size_t length,
	struct dhcp6_reply *reply)
{
	size_t offset;

	/* Each address of 16 bytes. */
	for (offset = 0; offset + 16U <= length; offset += 16U) {
		if (reply->dns_count == DHCP6_DNS_MAX)
			return;
		memcpy(reply->dns[reply->dns_count++].s6_addr, option + offset, 16U);
	}
}

/* Keeps a Domain Search List option's names, separated by spaces, as far as they fit. */
static void
dhcp6_domains(
	const uint8_t *option,
	size_t length,
	struct dhcp6_reply *reply)
{
	size_t offset;
	size_t used;
	unsigned label;

	/* The names in DNS's label form; a zero label ends each. */
	used = strlen(reply->search);
	offset = 0;
	while (offset < length) {
		label = option[offset++];

		/* The end of a name: a space before the next. */
		if (label == 0U) {
			if (used != 0U && reply->search[used - 1U] != ' ' && used + 1U < DHCP6_SEARCH_MAX)
				reply->search[used++] = ' ';
			continue;
		}
		if (label > DHCP6_LABEL_MAX || offset + label > length || used + label + 2U >= DHCP6_SEARCH_MAX)
			break;
		if (!dhcp6_label_valid(option + offset, label))
			break;

		/* A label, after a dot when the name has one already. */
		if (used != 0U && reply->search[used - 1U] != ' ')
			reply->search[used++] = '.';
		memcpy(reply->search + used, option + offset, label);
		used += label;
		offset += label;
	}

	/* Succeeded: no space at the end. */
	while (used != 0U && reply->search[used - 1U] == ' ')
		used--;
	reply->search[used] = '\0';
}

/* Tells whether a label is a host name's: letters, digits, '-' and '_' only (nothing else reaches resolv.conf). */
static int
dhcp6_label_valid(
	const uint8_t *label,
	size_t length)
{
	size_t index;
	uint8_t letter;

	/* Each byte. */
	for (index = 0; index < length; index++) {
		letter = label[index];
		if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9'))
			continue;
		if (letter == '-' || letter == '_')
			continue;
		return 0;
	}

	/* Succeeded. */
	return 1;
}
