/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * IPv6 fragment reassembly (RFC 8200 section 4.5).
 *
 * The input hands every packet with a fragment header here.  An atomic
 * fragment (offset 0, no more fragments) goes on at once, on its own
 * (RFC 6946).  Any other is kept, as the packet it came in, in the record
 * of its datagram: the source, the destination, the identification and
 * the device it came in on.  When the record holds the whole of the
 * fragmentable part, the pieces are copied into one packet after the
 * offset-zero fragment's unfragmentable part, without the fragment header,
 * and the input walks on from there.
 *
 * A record lasts 60 seconds from its first fragment.  When it runs out
 * with the offset-zero fragment kept, that fragment is quoted in a Time
 * Exceeded (fragment reassembly time exceeded).  Overlapping fragments
 * throw the whole datagram away, and the record stays, empty and dead,
 * until it runs out, so that the datagram's later fragments are thrown
 * away too (RFC 5722); an exact duplicate is the one exception, dropped
 * alone.  A first fragment whose header chain is cut short is refused
 * with a Parameter Problem (RFC 7112).
 *
 * A datagram is reassembled only as large as one packet holds; the
 * records, the fragments a record keeps and the fragments kept in all are
 * bounded, and the oldest record makes way when one of them is reached.
 *
 * One lock covers the records.  Nothing is freed, sent or referenced
 * while it is held: what leaves a record is chained and freed after.
 */

#include "ipv6.h"

#include "kern/net/net-device.h"
#include "kern/net/packet-buf.h"
#include "kern/lock.h"
#include "wire.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

/* The fragment header's length, and its offset and more-fragments bits. */
#define IPV6_FRAGMENT_HEADER_LENGTH	8U
#define IPV6_FRAGMENT_OFFSET_MASK	0xfff8U
#define IPV6_FRAGMENT_MORE		0x0001U

/* The largest payload length a reassembled packet may have. */
#define IPV6_FRAGMENT_PAYLOAD_MAX	65535U

/* Where the Payload Length is in the IPv6 header, and the Fragment Offset in the fragment header. */
#define IPV6_FRAGMENT_PAYLOAD_FIELD	4U
#define IPV6_FRAGMENT_OFFSET_FIELD	2U

/* How long a datagram's fragments are waited for (RFC 8200 section 4.5). */
#define IPV6_FRAGMENT_TIMEOUT_MS	60000U

/* The Time Exceeded code for a reassembly that ran out (RFC 4443 section 3.3). */
#define IPV6_FRAGMENT_TIME_EXCEEDED_CODE	1U

/* The smallest headers of the upper layers a first fragment must hold whole (RFC 7112 section 5). */
#define IPV6_FRAGMENT_UDP_HEADER	8U
#define IPV6_FRAGMENT_TCP_HEADER	20U
#define IPV6_FRAGMENT_ICMPV6_HEADER	4U
#define IPV6_FRAGMENT_PROTOCOL_TCP	6U
#define IPV6_FRAGMENT_PROTOCOL_UDP	17U

/*
 * The bounds: the records, the fragments one record keeps, and the
 * fragments kept in all (an eighth of the standard packets).  A 64-bit
 * machine, with jumbo packets, reassembles up to 9 kilobytes; a 32-bit
 * board's small pool reassembles what two standard packets carry.
 */
#if PACKET_BUF_JUMBO_COUNT != 0
#define IPV6_FRAGMENT_DATAGRAMS		16U
#define IPV6_FRAGMENT_PIECES		64U
#define IPV6_FRAGMENT_HELD		128U
#else
#define IPV6_FRAGMENT_DATAGRAMS		2U
#define IPV6_FRAGMENT_PIECES		4U
#define IPV6_FRAGMENT_HELD		4U
#endif

/* The largest reassembled packet, its IPv6 header included: what one packet holds after the headroom. */
#define IPV6_FRAGMENT_LARGEST		(PACKET_BUF_MAX_STORAGE_SIZE - PACKET_BUF_DEFAULT_HEADROOM)

/* What a fragment's arrival came to. */
#define IPV6_FRAGMENT_KEPT		1U	/* kept in its record, the datagram not yet whole */
#define IPV6_FRAGMENT_DROPPED		2U	/* not kept: a duplicate, or its datagram is dead */
#define IPV6_FRAGMENT_WHOLE		3U	/* kept, and its datagram is whole */

/*
 * One fragment a record keeps: the packet it came in (its data the
 * fragment's data, its storage still holding the IPv6 header at
 * l3_offset), and where its data goes in the fragmentable part.
 */
struct ipv6_fragment_piece {
	struct packet_buf *packet;
	unsigned offset;
	unsigned length;
};

/*
 * One datagram being reassembled, or a dead one whose fragments are
 * thrown away until it runs out.  The key is the device, the addresses
 * and the identification.  Its pieces are in the order of their offsets
 * and never overlap.  The end is known once the last fragment came; the
 * offset-zero fragment gives the unfragmentable part's length, the next
 * header after it, the place of the field that named the fragment header,
 * and whether it may be quoted in a Time Exceeded.
 */
struct ipv6_fragment_datagram {
	int used;
	int dead;
	struct net_device *device;
	struct in6_addr source;
	struct in6_addr destination;
	uint32_t identification;
	uint64_t expire_ms;
	int end_known;
	unsigned end;
	unsigned received;
	int first_known;
	unsigned unfragmentable;
	uint8_t first_next_header;
	size_t first_field;
	int first_quotable;
	unsigned count;
	struct ipv6_fragment_piece pieces[IPV6_FRAGMENT_PIECES];
};

/*
 * Guards the records and the count of fragments kept.  Taken by the
 * input and the timer, never with the IPv6 lock held.
 */
static struct spinlock ipv6_fragment_lock;

/*
 * The records.  A record with used 0 is free; a dead one keeps its key
 * and no pieces.  Emptied at boot, filled by the input, emptied by the
 * timer, by a datagram made whole, by a record making way, and by the
 * device going away.
 */
static struct ipv6_fragment_datagram ipv6_fragment_datagrams[IPV6_FRAGMENT_DATAGRAMS];

/*
 * The fragments kept in all the records: the sum of their counts, kept
 * at most IPV6_FRAGMENT_HELD so that the packet pool keeps the rest.
 */
static unsigned ipv6_fragment_held;

static int ipv6_fragment_chain(const struct packet_buf *packet, size_t start, uint8_t next_header);
static struct ipv6_fragment_datagram *ipv6_fragment_find_locked(const struct net_device *device, const struct in6_addr *source, const struct in6_addr *destination, uint32_t identification);
static struct ipv6_fragment_datagram *ipv6_fragment_take_locked(struct packet_buf **release);
static unsigned ipv6_fragment_place_locked(struct ipv6_fragment_datagram *datagram, struct packet_buf *packet, unsigned offset, int more, size_t start, size_t field, uint8_t next_header, int quotable, struct packet_buf **release);
static int ipv6_fragment_room_locked(const struct ipv6_fragment_datagram *keep, struct packet_buf **release);
static void ipv6_fragment_kill_locked(struct ipv6_fragment_datagram *datagram, struct packet_buf **release);
static void ipv6_fragment_empty_locked(struct ipv6_fragment_datagram *datagram, struct packet_buf **release);
static struct packet_buf *ipv6_fragment_assemble(struct ipv6_fragment_datagram *whole);
static void ipv6_fragment_pieces_free(struct ipv6_fragment_datagram *whole);
static void ipv6_fragment_release(struct packet_buf *release);

/*
 * Initializes the reassembly: no records, nothing kept.
 */
void
ipv6_fragment_init(
	void)
{
	/* The lock, and empty records. */
	spin_init(&ipv6_fragment_lock, LOCK_RANK_NETWORK, "IPv6 fragments");
	kern_memset(ipv6_fragment_datagrams, 0, sizeof(ipv6_fragment_datagrams));
	ipv6_fragment_held = 0U;
}

/*
 * Takes a packet whose fragment header is at *offset, named by the byte
 * at *field (the packet's data its IPv6 header, at l3_offset).  Returns 0
 * with *packet the packet to walk on with, *next_header the header at the
 * fragmentable part's start, *offset where it is and *field the byte that
 * names it: the same packet past its fragment header for an atomic
 * fragment, or the reassembled datagram.  Otherwise the packet was
 * consumed (kept, dropped or answered with an error) and an errno value
 * says so: EINPROGRESS when kept.
 */
int
ipv6_fragment_input(
	struct packet_buf **packet,
	uint8_t *next_header,
	size_t *offset,
	size_t *field)
{
	const struct ipv6_fragment_wire *fragment;
	const struct ipv6_wire *header;
	struct ipv6_fragment_datagram *datagram;
	struct ipv6_fragment_datagram whole;
	struct packet_buf *arrived;
	struct packet_buf *assembled;
	struct packet_buf *release;
	struct in6_addr source;
	struct in6_addr destination;
	unsigned long irq;
	uint32_t identification;
	uint16_t offset_flags;
	uint8_t fragment_next;
	unsigned fragment_offset;
	unsigned outcome;
	size_t start;
	size_t data_start;
	size_t length;
	void *pulled;
	int quotable;
	int more;
	int error;

	arrived = *packet;
	start = *offset;

	/* The fragment header whole. */
	if (start + IPV6_FRAGMENT_HEADER_LENGTH > arrived->length) {
		packet_buf_free(arrived);
		return EINVAL;
	}

	/* Its fields, and the fragment's data after it (the reserved bits are not looked at). */
	fragment = (const struct ipv6_fragment_wire *)(arrived->data + start);
	offset_flags = wire_get16(fragment->offset_flags);
	fragment_offset = offset_flags & IPV6_FRAGMENT_OFFSET_MASK;
	more = 0;
	if ((offset_flags & IPV6_FRAGMENT_MORE) != 0U)
		more = 1;
	fragment_next = fragment->next_header;
	identification = wire_get32(fragment->identification);
	data_start = start + IPV6_FRAGMENT_HEADER_LENGTH;
	length = arrived->length - data_start;

	/* An atomic fragment is the whole packet: it goes on alone, past its fragment header (RFC 6946). */
	if (fragment_offset == 0U && !more) {
		*next_header = fragment_next;
		*field = start;
		*offset = data_start;
		return 0;
	}

	/* A fragment that is not the last is a multiple of eight bytes long: a Parameter Problem at the Payload Length. */
	if (more && (length % 8U) != 0U) {
		icmp6_error(arrived, ICMP6_PARAMETER_PROBLEM, ICMP6_PARAMETER_HEADER, IPV6_FRAGMENT_PAYLOAD_FIELD);
		return EINVAL;
	}

	/* A fragment that would make the payload longer than 65535 bytes: a Parameter Problem at its Fragment Offset. */
	if ((start - IPV6_HEADER_LENGTH) + fragment_offset + length > IPV6_FRAGMENT_PAYLOAD_MAX) {
		icmp6_error(arrived, ICMP6_PARAMETER_PROBLEM, ICMP6_PARAMETER_HEADER, (uint32_t)(start + IPV6_FRAGMENT_OFFSET_FIELD));
		return EINVAL;
	}

	/* A fragment with no data that is not the last carries nothing. */
	if (more && length == 0U) {
		packet_buf_free(arrived);
		return EINVAL;
	}

	/* The first fragment holds the whole header chain: refused with a Parameter Problem (RFC 7112), or dropped when it nests another fragment header. */
	if (fragment_offset == 0U) {
		error = ipv6_fragment_chain(arrived, data_start, fragment_next);
		if (error == EINVAL) {
			icmp6_error(arrived, ICMP6_PARAMETER_PROBLEM, ICMP6_PARAMETER_FIRST_FRAGMENT, 0U);
			return EINVAL;
		}
		if (error != 0) {
			packet_buf_free(arrived);
			return EINVAL;
		}
	}

	/* A first fragment that starts an ICMPv6 error message is never quoted in another error (RFC 4443 section 2.4). */
	quotable = 1;
	if (fragment_offset == 0U && fragment_next == IPV6_NEXT_ICMPV6 && arrived->data[data_start] < 128U)
		quotable = 0;

	/* The key's addresses. */
	header = (const struct ipv6_wire *)arrived->data;
	kern_memcpy(source.s6_addr, header->source, 16U);
	kern_memcpy(destination.s6_addr, header->destination, 16U);

	/* The packet's data made the fragment's data; the IPv6 header stays in its storage. */
	pulled = packet_buf_pull(arrived, data_start);
	if (pulled == NULL) {
		packet_buf_free(arrived);
		return EINVAL;
	}

	/*
	 * The fragment placed in its datagram's record, made when it is the
	 * first to come; a whole datagram's record is copied out and emptied.
	 */
	release = NULL;
	irq = spin_lock_irqsave(&ipv6_fragment_lock);

	datagram = ipv6_fragment_find_locked(arrived->device, &source, &destination, identification);
	if (datagram == NULL) {
		datagram = ipv6_fragment_take_locked(&release);
		datagram->used = 1;
		datagram->device = arrived->device;
		datagram->source = source;
		datagram->destination = destination;
		datagram->identification = identification;
		datagram->expire_ms = ipv6_now_ms() + IPV6_FRAGMENT_TIMEOUT_MS;
	}
	outcome = IPV6_FRAGMENT_DROPPED;
	if (!datagram->dead)
		outcome = ipv6_fragment_place_locked(datagram, arrived, fragment_offset, more, start, *field, fragment_next, quotable, &release);
	if (outcome == IPV6_FRAGMENT_WHOLE) {
		kern_memcpy(&whole, datagram, sizeof(whole));
		ipv6_fragment_held -= datagram->count;
		kern_memset(datagram, 0, sizeof(*datagram));
	}

	spin_unlock_irqrestore(&ipv6_fragment_lock, irq);

	/* What left the records, freed now that the lock is let go. */
	ipv6_fragment_release(release);

	/* A fragment not kept. */
	if (outcome == IPV6_FRAGMENT_DROPPED) {
		packet_buf_free(arrived);
		return EINVAL;
	}

	/* A fragment kept, its datagram still waiting for others. */
	if (outcome == IPV6_FRAGMENT_KEPT)
		return EINPROGRESS;

	/* The whole datagram in one packet; its pieces are freed. */
	assembled = ipv6_fragment_assemble(&whole);
	if (assembled == NULL)
		return ENOBUFS;

	/* Succeeded: the reassembled packet, walked on from its fragmentable part. */
	*packet = assembled;
	*next_header = whole.first_next_header;
	*field = whole.first_field;
	*offset = whole.unfragmentable;
	return 0;
}

/*
 * Runs the reassembly's timer: each record whose time is up is emptied,
 * and a live one that kept its offset-zero fragment answers its source
 * with a Time Exceeded quoting that fragment.
 */
void
ipv6_fragment_timer_run(
	uint64_t now_ms)
{
	struct ipv6_fragment_datagram *datagram;
	struct packet_buf *exceeded[IPV6_FRAGMENT_DATAGRAMS];
	struct packet_buf *release;
	unsigned long irq;
	unsigned exceeded_count;
	unsigned index;
	unsigned piece;

	/* The records that ran out emptied, each live one's quotable offset-zero fragment kept back for its answer. */
	release = NULL;
	exceeded_count = 0U;
	irq = spin_lock_irqsave(&ipv6_fragment_lock);

	for (index = 0U; index < IPV6_FRAGMENT_DATAGRAMS; index++) {
		datagram = &ipv6_fragment_datagrams[index];
		if (!datagram->used || datagram->expire_ms > now_ms)
			continue;

		/* The offset-zero fragment, when kept, is the first piece. */
		piece = 0U;
		if (!datagram->dead && datagram->first_known && datagram->first_quotable) {
			exceeded[exceeded_count] = datagram->pieces[0].packet;
			exceeded_count++;
			piece = 1U;
		}

		/* The other pieces go with the record. */
		while (piece < datagram->count) {
			datagram->pieces[piece].packet->next = release;
			release = datagram->pieces[piece].packet;
			piece++;
		}

		ipv6_fragment_held -= datagram->count;
		kern_memset(datagram, 0, sizeof(*datagram));
	}

	spin_unlock_irqrestore(&ipv6_fragment_lock, irq);

	/* The pieces freed, and each Time Exceeded sent (it consumes its fragment). */
	ipv6_fragment_release(release);
	for (index = 0U; index < exceeded_count; index++)
		icmp6_error(exceeded[index], ICMP6_TIME_EXCEEDED, IPV6_FRAGMENT_TIME_EXCEEDED_CODE, 0U);
}

/*
 * Gives the time in milliseconds since boot at which the first record
 * runs out (0 for none).
 */
uint64_t
ipv6_fragment_deadline(
	void)
{
	unsigned long irq;
	unsigned index;
	uint64_t deadline;

	/* The earliest time of the records in use. */
	deadline = 0U;
	irq = spin_lock_irqsave(&ipv6_fragment_lock);

	for (index = 0U; index < IPV6_FRAGMENT_DATAGRAMS; index++) {
		if (!ipv6_fragment_datagrams[index].used)
			continue;
		if (deadline == 0U || ipv6_fragment_datagrams[index].expire_ms < deadline)
			deadline = ipv6_fragment_datagrams[index].expire_ms;
	}

	spin_unlock_irqrestore(&ipv6_fragment_lock, irq);

	/* Succeeded: the earliest, or none. */
	return deadline;
}

/*
 * Forgets the datagrams that came in on a device going away, and frees
 * their fragments (and the device references they hold).
 */
void
ipv6_fragment_purge_device(
	struct net_device *device)
{
	struct packet_buf *release;
	unsigned long irq;
	unsigned index;

	/* The device's records emptied. */
	release = NULL;
	irq = spin_lock_irqsave(&ipv6_fragment_lock);

	for (index = 0U; index < IPV6_FRAGMENT_DATAGRAMS; index++) {
		if (!ipv6_fragment_datagrams[index].used || ipv6_fragment_datagrams[index].device != device)
			continue;
		ipv6_fragment_empty_locked(&ipv6_fragment_datagrams[index], &release);
	}

	spin_unlock_irqrestore(&ipv6_fragment_lock, irq);

	/* Their fragments freed. */
	ipv6_fragment_release(release);
}

/*
 * Tells whether a first fragment holds its whole header chain, from the
 * next header after the fragment header at start: every extension header
 * whole, and the upper layer's smallest header (RFC 7112 section 5).
 * Returns 0, EINVAL when the chain is cut short, or ELOOP when it holds
 * another fragment header.
 */
static int
ipv6_fragment_chain(
	const struct packet_buf *packet,
	size_t start,
	uint8_t next_header)
{
	const struct ipv6_extension_wire *extension;
	size_t position;
	size_t length;
	size_t needed;

	/* Each extension header in turn, until the upper layer. */
	position = start;
	while (next_header == IPV6_NEXT_HOPOPTS ||
	    next_header == IPV6_NEXT_ROUTING ||
	    next_header == IPV6_NEXT_DSTOPTS ||
	    next_header == IPV6_NEXT_FRAGMENT) {
		/* A fragment header inside a fragment. */
		if (next_header == IPV6_NEXT_FRAGMENT)
			return ELOOP;

		/* The header's start in the fragment. */
		if (position + sizeof(*extension) > packet->length)
			return EINVAL;

		/* The header whole in the fragment. */
		extension = (const struct ipv6_extension_wire *)(packet->data + position);
		length = ((size_t)extension->length + 1U) * 8U;
		if (position + length > packet->length)
			return EINVAL;

		/* The next one. */
		next_header = extension->next_header;
		position += length;
	}

	/* The upper layer's smallest header. */
	needed = 0U;
	if (next_header == IPV6_FRAGMENT_PROTOCOL_UDP)
		needed = IPV6_FRAGMENT_UDP_HEADER;
	else if (next_header == IPV6_FRAGMENT_PROTOCOL_TCP)
		needed = IPV6_FRAGMENT_TCP_HEADER;
	else if (next_header == IPV6_NEXT_ICMPV6)
		needed = IPV6_FRAGMENT_ICMPV6_HEADER;

	/* The upper layer's header whole in the fragment. */
	if (position + needed > packet->length)
		return EINVAL;

	/* Succeeded: the chain is whole. */
	return 0;
}

/* Finds the record of a datagram by its key (NULL for none). */
static struct ipv6_fragment_datagram *
ipv6_fragment_find_locked(
	const struct net_device *device,
	const struct in6_addr *source,
	const struct in6_addr *destination,
	uint32_t identification)
{
	struct ipv6_fragment_datagram *datagram;
	unsigned index;
	int same;

	/* Each record in use, by its device, identification and addresses. */
	for (index = 0U; index < IPV6_FRAGMENT_DATAGRAMS; index++) {
		datagram = &ipv6_fragment_datagrams[index];
		if (!datagram->used ||
		    datagram->device != device ||
		    datagram->identification != identification)
			continue;
		same = in6_equal(&datagram->source, source);
		if (!same)
			continue;
		same = in6_equal(&datagram->destination, destination);
		if (same)
			return datagram;
	}

	/* None has the key. */
	return NULL;
}

/*
 * Takes a free record, emptying one to make way when none is: the
 * oldest dead one, else the oldest live one.  Its fragments are chained
 * on *release.
 */
static struct ipv6_fragment_datagram *
ipv6_fragment_take_locked(
	struct packet_buf **release)
{
	struct ipv6_fragment_datagram *datagram;
	struct ipv6_fragment_datagram *oldest_dead;
	struct ipv6_fragment_datagram *oldest_live;
	unsigned index;

	/* A free record, or the oldest of each kind. */
	oldest_dead = NULL;
	oldest_live = NULL;
	for (index = 0U; index < IPV6_FRAGMENT_DATAGRAMS; index++) {
		datagram = &ipv6_fragment_datagrams[index];
		if (!datagram->used)
			return datagram;
		if (datagram->dead) {
			if (oldest_dead == NULL || datagram->expire_ms < oldest_dead->expire_ms)
				oldest_dead = datagram;
		} else {
			if (oldest_live == NULL || datagram->expire_ms < oldest_live->expire_ms)
				oldest_live = datagram;
		}
	}

	/* A dead record makes way before a live one. */
	datagram = oldest_dead;
	if (datagram == NULL)
		datagram = oldest_live;
	ipv6_fragment_empty_locked(datagram, release);

	/* Succeeded: the record made free. */
	return datagram;
}

/*
 * Places a fragment (its packet's data the fragment's data, start the
 * place of its fragment header) in its datagram's live record: kept,
 * dropped as an exact duplicate, or, on an overlap, a contradiction or a
 * bound reached, the whole datagram killed and the fragment dropped.
 * What leaves the records is chained on *release.  Returns
 * IPV6_FRAGMENT_KEPT, IPV6_FRAGMENT_DROPPED or IPV6_FRAGMENT_WHOLE.
 */
static unsigned
ipv6_fragment_place_locked(
	struct ipv6_fragment_datagram *datagram,
	struct packet_buf *packet,
	unsigned offset,
	int more,
	size_t start,
	size_t field,
	uint8_t next_header,
	int quotable,
	struct packet_buf **release)
{
	struct ipv6_fragment_piece *neighbor;
	struct ipv6_fragment_piece *last;
	unsigned length;
	unsigned end;
	unsigned furthest;
	unsigned position;
	unsigned index;
	size_t unfragmentable;
	int same;
	int room;

	length = (unsigned)packet->length;
	end = offset + length;

	/* The furthest any fragment reaches, this one's included: the last piece reaches furthest of those kept. */
	furthest = end;
	if (datagram->count != 0U) {
		last = &datagram->pieces[datagram->count - 1U];
		if (last->offset + last->length > furthest)
			furthest = last->offset + last->length;
	}

	/* The reassembled packet must fit one packet: its unfragmentable part (40 bytes at least until the first fragment says) and the furthest end. */
	unfragmentable = IPV6_HEADER_LENGTH;
	if (datagram->first_known)
		unfragmentable = datagram->unfragmentable;
	if (offset == 0U)
		unfragmentable = start;
	if (unfragmentable + furthest > IPV6_FRAGMENT_LARGEST) {
		ipv6_fragment_kill_locked(datagram, release);
		return IPV6_FRAGMENT_DROPPED;
	}

	/* The last fragment fixes the end: another end, or a fragment beyond it, contradicts it. */
	if (!more) {
		if (datagram->end_known && end != datagram->end) {
			ipv6_fragment_kill_locked(datagram, release);
			return IPV6_FRAGMENT_DROPPED;
		}
		if (furthest > end) {
			ipv6_fragment_kill_locked(datagram, release);
			return IPV6_FRAGMENT_DROPPED;
		}
	} else if (datagram->end_known && end > datagram->end) {
		ipv6_fragment_kill_locked(datagram, release);
		return IPV6_FRAGMENT_DROPPED;
	}

	/* Where it goes: before the first piece that starts at or after it. */
	position = 0U;
	while (position < datagram->count && datagram->pieces[position].offset < offset)
		position++;

	/* A piece at the same place and of the same length: an exact duplicate is dropped alone, another is an overlap. */
	if (position < datagram->count &&
	    datagram->pieces[position].offset == offset &&
	    datagram->pieces[position].length == length) {
		same = kern_memcmp(datagram->pieces[position].packet->data, packet->data, length);
		if (same == 0)
			return IPV6_FRAGMENT_DROPPED;
		ipv6_fragment_kill_locked(datagram, release);
		return IPV6_FRAGMENT_DROPPED;
	}

	/* The piece before it must end where it starts or earlier (RFC 5722). */
	if (position > 0U) {
		neighbor = &datagram->pieces[position - 1U];
		if (neighbor->offset + neighbor->length > offset) {
			ipv6_fragment_kill_locked(datagram, release);
			return IPV6_FRAGMENT_DROPPED;
		}
	}

	/* The piece after it must start where it ends or later (RFC 5722). */
	if (position < datagram->count) {
		neighbor = &datagram->pieces[position];
		if (end > neighbor->offset) {
			ipv6_fragment_kill_locked(datagram, release);
			return IPV6_FRAGMENT_DROPPED;
		}
	}

	/* A datagram cut in more pieces than a record keeps. */
	if (datagram->count >= IPV6_FRAGMENT_PIECES) {
		ipv6_fragment_kill_locked(datagram, release);
		return IPV6_FRAGMENT_DROPPED;
	}

	/* Room among the fragments kept in all, made by older datagrams. */
	room = ipv6_fragment_room_locked(datagram, release);
	if (!room) {
		ipv6_fragment_kill_locked(datagram, release);
		return IPV6_FRAGMENT_DROPPED;
	}

	/* The pieces after it moved up one. */
	for (index = datagram->count; index > position; index--)
		datagram->pieces[index] = datagram->pieces[index - 1U];

	/*
	 * The piece kept: it counts among the fragments kept in all, and its
	 * bytes toward the whole.
	 */
	datagram->pieces[position].packet = packet;
	datagram->pieces[position].offset = offset;
	datagram->pieces[position].length = length;
	datagram->count++;
	datagram->received += length;
	ipv6_fragment_held++;

	/* The offset-zero fragment gives the unfragmentable part and the next header after it. */
	if (offset == 0U) {
		datagram->first_known = 1;
		datagram->unfragmentable = (unsigned)start;
		datagram->first_next_header = next_header;
		datagram->first_field = field;
		datagram->first_quotable = quotable;
	}

	/* The last fragment gives the end. */
	if (!more) {
		datagram->end_known = 1;
		datagram->end = end;
	}

	/*
	 * Whole when the end and the start are known and the bytes kept add
	 * up to the end: the pieces never overlap, so no gap is left.
	 */
	if (datagram->end_known && datagram->first_known && datagram->received == datagram->end)
		return IPV6_FRAGMENT_WHOLE;

	/* Succeeded: kept, the datagram still waiting. */
	return IPV6_FRAGMENT_KEPT;
}

/*
 * Makes room for one more fragment among those kept in all, emptying the
 * oldest live records other than keep.  Returns 1 when there is room.
 */
static int
ipv6_fragment_room_locked(
	const struct ipv6_fragment_datagram *keep,
	struct packet_buf **release)
{
	struct ipv6_fragment_datagram *datagram;
	struct ipv6_fragment_datagram *oldest;
	unsigned index;

	/* The oldest other record with fragments made way, until there is room. */
	while (ipv6_fragment_held >= IPV6_FRAGMENT_HELD) {
		oldest = NULL;
		for (index = 0U; index < IPV6_FRAGMENT_DATAGRAMS; index++) {
			datagram = &ipv6_fragment_datagrams[index];
			if (!datagram->used || datagram == keep || datagram->count == 0U)
				continue;
			if (oldest == NULL || datagram->expire_ms < oldest->expire_ms)
				oldest = datagram;
		}

		/* Only this datagram keeps fragments: no room. */
		if (oldest == NULL)
			return 0;

		ipv6_fragment_empty_locked(oldest, release);
	}

	/* Succeeded: room for one more. */
	return 1;
}

/*
 * Kills a datagram: its fragments are chained on *release, and its
 * record stays, dead, until it runs out, so that its later fragments
 * are dropped too (RFC 5722).
 */
static void
ipv6_fragment_kill_locked(
	struct ipv6_fragment_datagram *datagram,
	struct packet_buf **release)
{
	unsigned index;

	/* The pieces chained to be freed. */
	for (index = 0U; index < datagram->count; index++) {
		datagram->pieces[index].packet->next = *release;
		*release = datagram->pieces[index].packet;
	}

	/*
	 * Dead: no pieces, no start or end known, nothing to answer when it
	 * runs out; the key and the time stay.
	 */
	ipv6_fragment_held -= datagram->count;
	datagram->dead = 1;
	datagram->count = 0U;
	datagram->received = 0U;
	datagram->end_known = 0;
	datagram->end = 0U;
	datagram->first_known = 0;
}

/* Empties a record, chaining its fragments on *release. */
static void
ipv6_fragment_empty_locked(
	struct ipv6_fragment_datagram *datagram,
	struct packet_buf **release)
{
	unsigned index;

	/* The pieces chained to be freed. */
	for (index = 0U; index < datagram->count; index++) {
		datagram->pieces[index].packet->next = *release;
		*release = datagram->pieces[index].packet;
	}

	/* The record free, its pieces no longer counted. */
	ipv6_fragment_held -= datagram->count;
	kern_memset(datagram, 0, sizeof(*datagram));
}

/*
 * Copies a whole datagram's pieces into one packet: the offset-zero
 * fragment's IPv6 header and unfragmentable part, with its Payload Length
 * worked out again and the field that named the fragment header naming
 * the next header instead (RFC 8200 section 4.5), then the fragmentable
 * part.  The pieces are freed.  Returns the packet, or NULL when none was
 * free.
 */
static struct packet_buf *
ipv6_fragment_assemble(
	struct ipv6_fragment_datagram *whole)
{
	struct ipv6_wire *header;
	struct packet_buf *first;
	struct packet_buf *packet;
	uint8_t *data;
	size_t total;
	unsigned index;
	int error;

	/* The packet for the whole: the unfragmentable part and the end. */
	first = whole->pieces[0].packet;
	total = (size_t)whole->unfragmentable + whole->end;
	packet = packet_buf_alloc_for(PACKET_BUF_DEFAULT_HEADROOM, total, 0U, &error);
	if (packet == NULL) {
		ipv6_fragment_pieces_free(whole);
		return NULL;
	}

	/* Room in it for the whole. */
	data = packet_buf_append(packet, total);
	if (data == NULL) {
		packet_buf_free(packet);
		ipv6_fragment_pieces_free(whole);
		return NULL;
	}

	/* The headers from the offset-zero fragment, with the payload's new length and the fragment header's next header. */
	kern_memcpy(data, first->storage + first->l3_offset, whole->unfragmentable);
	header = (struct ipv6_wire *)data;
	wire_put16(header->payload_length, (uint16_t)(total - IPV6_HEADER_LENGTH));
	data[whole->first_field] = whole->first_next_header;

	/* Each piece in its place in the fragmentable part. */
	for (index = 0U; index < whole->count; index++) {
		kern_memcpy(
			data + whole->unfragmentable + whole->pieces[index].offset,
			whole->pieces[index].packet->data,
			whole->pieces[index].length);
	}

	/* Where its header is, and the device it came in on (with a reference of its own). */
	packet->l3_offset = (uint16_t)(packet->data - packet->storage);
	packet->l3_length = (uint16_t)total;
	packet->protocol = first->protocol;
	packet->flags = first->flags;
	packet->device = first->device;
	if (packet->device != NULL)
		net_device_ref(packet->device);
	packet->source_length = first->source_length;
	kern_memcpy(packet->source_address, first->source_address, sizeof(packet->source_address));

	/* The pieces freed. */
	ipv6_fragment_pieces_free(whole);

	/* Succeeded: the reassembled packet. */
	return packet;
}

/* Frees the pieces of a record copied out of the records. */
static void
ipv6_fragment_pieces_free(
	struct ipv6_fragment_datagram *whole)
{
	struct packet_buf *release;
	unsigned index;

	/* The pieces chained, then freed. */
	release = NULL;
	for (index = 0U; index < whole->count; index++) {
		whole->pieces[index].packet->next = release;
		release = whole->pieces[index].packet;
	}
	ipv6_fragment_release(release);
}

/* Frees a chain of packets that left the records. */
static void
ipv6_fragment_release(
	struct packet_buf *release)
{
	struct packet_buf *next;

	/* Each in turn, its link cleared first. */
	while (release != NULL) {
		next = release->next;
		release->next = NULL;
		packet_buf_free(release);
		release = next;
	}
}
