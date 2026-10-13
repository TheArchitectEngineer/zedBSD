/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The TCP socket implementation.
 *
 * A socket may have several segments outstanding at once, held oldest first
 * in a fixed ring.  How many is bounded by that ring and by the window the
 * peer advertises, so a sender does not have to wait a round trip for each
 * segment.  An acknowledgement retires every segment it covers.  The network
 * worker's timer resends the oldest outstanding segment with exponential
 * backoff until it is acknowledged or the attempt is abandoned; the ones
 * behind it follow once it is taken.
 *
 * A segment the device or the packet pool refuses stays in the ring unsent
 * (ws220-p003), with every segment queued after it, and the writer goes on:
 * the network worker sends them in order on its next pass after the device
 * has room (its transmit completion runs one) or when the pool may have
 * packets again.  A pure acknowledgement refused the same way is counted
 * and sent again with them.
 *
 * Listeners hold half-open children until their handshake completes and then
 * queue them for accept.  Payload is delivered in order only; anything else
 * is dropped and re-acknowledged.
 *
 * An AF_INET6 socket (ws130-p003) speaks IPv6 to an IPv6 peer, and IPv4 to
 * an IPv4-mapped one; a listener on [::] that is not IPV6_V6ONLY takes the
 * IPv4 connections for its port too, whose children are IPv4-mapped.
 */

#include "kern/net/tcp-socket.h"
#include "kern/net/byteorder.h"
#include "kern/net/net-device.h"
#include "kern/net/packet-buf.h"
#include "kern/net/route.h"
#include "kern/clock.h"
#include "kern/kmem.h"
#include "kern/poll.h"
#include "kern/sched.h"
#include "kern/signal.h"
#include "kern/process.h"
#include "kern/thread.h"
#include "internal.h"
#include "ipv6.h"
#include "wire.h"
#include <kern/kcrt.h>

#include <uapi/netinet.h>
#include <uapi/errno.h>
#include <uapi/sysctl.h>

#define TCP_FIN 0x01U
#define TCP_SYN 0x02U
#define TCP_RST 0x04U
#define TCP_PSH 0x08U
#define TCP_ACK 0x10U
#define TCP_MSS 1024U

/*
 * The largest segment this end takes, which the SYN offers (BUG-222): an
 * Ethernet frame's 1500 bytes less the IP and TCP headers.  It fits one
 * packet's storage with its headers.  The segments this end sends stay
 * TCP_MSS, which every interface it is used on carries.
 */
#define TCP_RECEIVE_MSS 1460U

/* The same over IPv6, whose header is 20 bytes larger (ws130-p003). */
#define TCP_RECEIVE_MSS6 1440U

/* How a listener matches a segment: not, on the wildcard address, or on the exact one. */
#define TCP_MATCH_NONE		0
#define TCP_MATCH_WILDCARD	1
#define TCP_MATCH_EXACT		2

/*
 * The addresses of a segment that came in: IPv4 ones (family AF_INET), or
 * IPv6 ones with the interface it came in on (ws130-p003).
 */
struct tcp_segment_addresses {
	int family;
	uint32_t source;
	uint32_t destination;
	struct in6_addr source6;
	struct in6_addr destination6;
	unsigned ifindex;
};

/*
 * How many packets a socket's receive queue may hold.  The window is a
 * full segment for each one still free, so this is what bounds a transfer
 * over a link with any delay (BUG-222: the common limit of 8 was an 8 KB
 * window, about 1 MB/s on a LAN).  44 segments are the 64 KB the queue's
 * bytes allow, and the larger pool of the 64-bit machines has room for
 * them; the small pool of the 32-bit boards keeps the common limit.
 */
#if PACKET_BUF_POOL_COUNT >= 256U
#define TCP_RECEIVE_PACKETS 44U
#else
#define TCP_RECEIVE_PACKETS SOCKET_RECEIVE_MESSAGES_MAX
#endif
/*
 * How many segments ahead of a gap a connection keeps (BUG-222): the
 * larger pool of the 64-bit machines has room for TCP_REORDER_MAX of them,
 * the small pool of the 32-bit boards for a few.
 */
#if PACKET_BUF_POOL_COUNT >= 256U
#define TCP_REORDER_LIMIT TCP_REORDER_MAX
#else
#define TCP_REORDER_LIMIT 4U
#endif
#define TCP_EPHEMERAL_FIRST 49152U
/* The initial retransmission timeout of RFC 6298, one second. */
#define TCP_INITIAL_RTO KERN_MS_TO_TICKS(1000U)

/*
 * The least term the round-trip variation adds to the smoothed round-trip
 * time in the retransmission timeout (ws220-p004): the timeout is
 * SRTT + max(G, 4 RTTVAR, 200 ms), always 200 ms beyond the round trip,
 * as Linux has it, so a peer that delays its acknowledgement up to 200 ms
 * does not look lost.  RFC 6298 section 2.4 asks for at least a second;
 * the user chose 200 ms (2026-10-13).
 */
#define TCP_RTO_MIN_TERM_US 200000U

/*
 * The timeout a connection starts its data with when its SYN had to be
 * resent and no round trip was measured (RFC 6298 section 5.7).
 */
#define TCP_SYN_RETRIED_RTO KERN_MS_TO_TICKS(3000U)

/* The duplicate acknowledgements in a row that start a fast retransmit (RFC 5681 section 3.2). */
#define TCP_DUPLICATE_ACKS_FAST 3U

/*
 * What an immediate resend (retransmit_immediate) is for (ws220-p005): a
 * loss (a fast retransmit or a partial acknowledgement's), whose resend
 * stops any timing (review-1 M2), or a probe the reopened window now takes,
 * which stops only its own.
 */
#define TCP_IMMEDIATE_LOSS 1U
#define TCP_IMMEDIATE_PROBE 2U

/* The longest round-trip sample taken, so the sums in microseconds stay within 32 bits. */
#define TCP_RTT_SAMPLE_MAX_US 60000000U
/* How long closing a connection waits for what it sent to be taken. */
#define TCP_CLOSE_DRAIN_MS 10000U

/* What a closing connection waits for. */
enum tcp_drain {
	/* A free entry in the retransmission ring, for the FIN. */
	TCP_DRAIN_ROOM,
	/* Every byte of data acknowledged; a FIN may still be outstanding. */
	TCP_DRAIN_DATA,
	/* Everything acknowledged, the FIN included. */
	TCP_DRAIN_ALL,
};
/*
 * The pause before sending again when buffers are short: a connect whose
 * SYN found no packet for its copy; a writer whose copy found none; the
 * output retry of segments the packet pool refused, or a device that
 * refused again on a retry that sent nothing; and a timer's resend the
 * device refused again.
 */
#define TCP_SYN_RETRY_MS 250U
#define TCP_SEND_RETRY_MS 10U

/*
 * When the output retry runs at the latest after a full device refused a
 * segment (ws220-p003).  The device's transmit completion runs the network
 * worker sooner; this is for a device that runs it for nothing.
 */
#define TCP_OUTPUT_RETRY_DEVICE_MS 1U

/*
 * The most pure acknowledgements of one receive_next kept to be sent again:
 * three duplicates are what a peer's fast retransmit waits for.
 */
#define TCP_ACK_PENDING_MAX 3U

/*
 * The longest the retransmission timeout grows to as it doubles for each
 * resend: RFC 6298 section 2.5 allows a maximum of at least 60 seconds.
 */
#define TCP_RETRANSMIT_TIMEOUT_MAX KERN_MS_TO_TICKS(60000U)

/*
 * How long the timer goes on resending the oldest segment before the
 * attempt is given up (RFC 1122 section 4.2.3.5, R2), counted from the
 * first timeout after the peer last answered.  R2 is at least 100 seconds
 * for data and at least 3 minutes for a SYN; a time rather than a count of
 * resends, so it holds whatever the timeouts are (ws220-p002).
 */
#define TCP_GIVE_UP_DATA_MS 100000U
#define TCP_GIVE_UP_SYN_MS 180000U

/*
 * A test build may start every connection's sequence numbers this many
 * bytes before they wrap (ws220-p002), so that a transfer of a few times
 * that many bytes crosses the wrap every time instead of one time in
 * four for a gigabyte.  0, the default, chooses them as usual.
 */
#ifndef CONFIG_TCP_ISN_WRAP_BEFORE
#define CONFIG_TCP_ISN_WRAP_BEFORE 0U
#endif

/* How a reset that came in is taken (RFC 5961 section 3.2). */
enum tcp_reset_check {
	/* Its sequence number is not within the window: it is ignored. */
	TCP_RESET_IGNORE,
	/* It is within the window but not the next expected: a challenge acknowledgement answers it. */
	TCP_RESET_CHALLENGE,
	/* It is exactly the next sequence number expected: the connection is reset. */
	TCP_RESET_ACCEPT,
};

/*
 * What became of a segment handed to the output, and how it is sent again
 * (ws220-p003).  Only DEVICE and WAIT keep a segment unsent for the output
 * retry; LOST and HARD leave it sent, to the retransmission timer.
 */
enum tcp_refusal {
	/* The device took it. */
	TCP_REFUSAL_NONE,
	/* A full device queue: on the network worker's next pass, which the device's transmit completion runs. */
	TCP_REFUSAL_DEVICE,
	/* An empty packet pool: after TCP_SEND_RETRY_MS. */
	TCP_REFUSAL_WAIT,
	/*
	 * A neighbour still being resolved (IPv4 asked ARP for it): lost as on
	 * the wire, so the timer's backoff and R2 bound how often ARP is asked
	 * again and when a peer that never answers is given up (review-1 G1).
	 */
	TCP_REFUSAL_LOST,
	/* An error that will not pass (no route, a device gone, too large). */
	TCP_REFUSAL_HARD,
};

/*
 * Idle probing for SO_KEEPALIVE, in clock ticks.  The defaults are the
 * traditional ones: probe after two hours of silence, then every 75 seconds,
 * and give up after nine unanswered probes.  A build may shorten them so the
 * behaviour can be observed in a test that does not run for hours.
 */
#ifndef CONFIG_TCP_KEEPALIVE_IDLE_MS
#define CONFIG_TCP_KEEPALIVE_IDLE_MS 7200000U
#endif
#ifndef CONFIG_TCP_KEEPALIVE_INTERVAL_MS
#define CONFIG_TCP_KEEPALIVE_INTERVAL_MS 75000U
#endif
#ifndef CONFIG_TCP_KEEPALIVE_COUNT
#define CONFIG_TCP_KEEPALIVE_COUNT 9U
#endif

#define TCP_KEEPALIVE_IDLE \
	((uint64_t)CONFIG_TCP_KEEPALIVE_IDLE_MS * KERN_CLOCK_HZ / 1000U)
#define TCP_KEEPALIVE_INTERVAL \
	((uint64_t)CONFIG_TCP_KEEPALIVE_INTERVAL_MS * KERN_CLOCK_HZ / 1000U)
#define TCP_KEEPALIVE_COUNT ((unsigned)CONFIG_TCP_KEEPALIVE_COUNT)
#define TCP_LISTEN_BACKLOG_MAX 16U

/*
 * The number of sockets the retransmission timer snapshots on its own
 * stack.  More registered sockets are snapshotted into an allocated array
 * sized to them, so the timer reaches every socket (BUG-107).
 */
#define TCP_TIMER_STACK_SNAPSHOT 32U

struct tcp_endpoint {
	struct tcp_socket tcp;
	struct tcp_endpoint *next;
};

static struct tcp_endpoint *tcp_sockets;
static uint16_t next_ephemeral;
static struct spinlock tcp_registry_lock;

static struct tcp_endpoint * tcp_endpoint(struct socket *socket);
static void tcp_forget_peer(struct tcp_endpoint *endpoint);
static void tcp_listener_remove(struct tcp_endpoint *child);
static void tcp_listener_established(struct tcp_endpoint *child);
static int tcp_port_in_use(const struct tcp_endpoint *candidate, int strict);
static int tcp_allocate_port_locked(struct tcp_endpoint *endpoint);
static int tcp_route(struct tcp_endpoint *endpoint, struct net_device **device, uint32_t *source);
static int tcp_send_segment_at(struct tcp_endpoint *endpoint, uint32_t sequence, uint8_t flags, const void *data, size_t length);
static int tcp_output_segment(struct tcp_endpoint *endpoint, uint32_t sequence, uint8_t flags, const void *data, size_t length);
static void tcp_stats_fill_locked(const struct tcp_endpoint *endpoint, uint64_t now, int superuser, struct tcp_stats_entry *entry);
static int tcp_allocate_port(struct tcp_endpoint *endpoint);
static enum tcp_refusal tcp_refusal_of(int error);
static uint64_t tcp_refusal_delay(enum tcp_refusal refusal);
static int tcp_refusal_retries(enum tcp_refusal refusal);
static unsigned tcp_sent_count_locked(const struct tcp_endpoint *endpoint);
static uint32_t tcp_sent_end_locked(const struct tcp_endpoint *endpoint);
static void tcp_mark_sent_locked(struct tcp_endpoint *endpoint, uint64_t now);
static void tcp_unmark_sent_locked(struct tcp_endpoint *endpoint, const struct packet_buf *packet);
static void tcp_output_refused_locked(struct tcp_endpoint *endpoint, enum tcp_refusal refusal, int progressed, uint64_t now);
static void tcp_output_retry_settle_locked(struct tcp_endpoint *endpoint);
static int tcp_output_retry_due_locked(const struct tcp_endpoint *endpoint, uint64_t now);
static void tcp_output_retry(struct tcp_endpoint *endpoint, uint8_t *payload, uint64_t now);
static void tcp_send_unsent(struct tcp_endpoint *endpoint, uint8_t *payload);
static void tcp_send_ack_pending(struct tcp_endpoint *endpoint);
static int tcp_send_ack(struct tcp_endpoint *endpoint, int duplicate);
static int tcp_sequence_before(uint32_t left, uint32_t right);
static int tcp_sequence_at_or_before(uint32_t left, uint32_t right);
static uint32_t tcp_initial_sequence(uint32_t chosen);
static uint64_t tcp_backoff_timeout(uint64_t rto, unsigned count);
static uint64_t tcp_rto_locked(const struct tcp_endpoint *endpoint);
static void tcp_rtt_sample_locked(struct tcp_endpoint *endpoint, uint64_t now);
static void tcp_rtt_forget_locked(struct tcp_endpoint *endpoint);
static void tcp_rtt_handshake_locked(struct tcp_endpoint *endpoint, uint32_t acknowledgement, uint64_t now);
static int tcp_retransmit_given_up_locked(struct tcp_endpoint *endpoint, uint64_t now);
static enum tcp_reset_check tcp_reset_check_locked(const struct tcp_endpoint *endpoint, uint32_t sequence);
static int tcp_send_window_update_allowed_locked(const struct tcp_endpoint *endpoint, uint32_t sequence, uint32_t acknowledgement);
static int tcp_probe_fits_locked(const struct tcp_endpoint *endpoint);
static int tcp_oldest_fits_window_locked(const struct tcp_endpoint *endpoint);
struct tcp_discard {
	struct packet_buf *packets[CONFIG_TCP_SEND_QUEUE_MAX];
	unsigned count;
};

static void tcp_retransmit_reset(struct tcp_endpoint *endpoint, struct tcp_discard *discard);
static void tcp_discard_free(struct tcp_discard *discard);
static uint32_t tcp_in_flight(const struct tcp_endpoint *endpoint);
static void tcp_retire_acknowledged(struct tcp_endpoint *endpoint, uint32_t acknowledgement, struct tcp_discard *discard);
static void tcp_retransmit_clear(struct tcp_endpoint *endpoint);
static void tcp_reorder_keep(struct tcp_endpoint *endpoint, struct packet_buf **packet, uint32_t sequence);
static void tcp_reorder_drain(struct tcp_endpoint *endpoint, int discard);
static void tcp_reorder_free(struct tcp_endpoint *endpoint);
static void tcp_connect_cancel_locked(struct tcp_endpoint *endpoint, uint32_t generation, struct tcp_discard *discard);
static int tcp_send_reliable(struct tcp_endpoint *endpoint, uint8_t flags, const void *data, size_t length);
static int tcp_bind(struct socket *socket, const struct sockaddr *address, socklen_t length);
static int tcp_listen(struct socket *socket, int backlog);
static int tcp_accept(struct socket *socket, struct socket **result, struct sockaddr *address, socklen_t *length, unsigned io_flags);
static int tcp_connect(struct socket *socket, const struct sockaddr *address, socklen_t length, unsigned io_flags);
static ssize_t tcp_sendto(struct socket *socket, const void *buffer, size_t length, int flags, const struct sockaddr *address, socklen_t address_length);
static ssize_t tcp_recvfrom(struct socket *socket, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_length);
static uint16_t tcp_receive_window(struct tcp_endpoint *endpoint);
static int tcp_reset_error_locked(const struct tcp_endpoint *endpoint, uint8_t flags, uint32_t acknowledgement);
static void tcp_send_reset(const struct tcp_segment_addresses *addresses, const struct tcp_wire *segment, size_t payload_length);
static void tcp_send_reset6(const struct tcp_segment_addresses *addresses, struct packet_buf *packet);
static int tcp_shutdown(struct socket *socket, int how);
static int tcp_getsockname(struct socket *socket, struct sockaddr *address, socklen_t *length);
static int tcp_getpeername(struct socket *socket, struct sockaddr *address, socklen_t *length);
static void tcp_close(struct socket *socket);
static void tcp_close_drain(struct tcp_endpoint *endpoint, enum tcp_drain until);
static void tcp_endpoint_close(struct socket *socket);
static int tcp_poll(struct socket *socket, short events, short *revents);
static int tcp_setsockopt(struct socket *socket, int level, int option, const void *value, socklen_t length);
static int tcp_getsockopt(struct socket *socket, int level, int option, void *value, socklen_t *length);
static void tcp_keepalive_changed(struct socket *socket);
static void tcp_keepalive_arm_locked(struct tcp_endpoint *endpoint);
static struct tcp_endpoint * tcp_lookup(const struct tcp_segment_addresses *addresses, uint16_t source_port, uint16_t destination_port);
static int tcp_connection_matches(const struct tcp_endpoint *endpoint, const struct tcp_segment_addresses *addresses, uint16_t source_port, uint16_t destination_port);
static int tcp_listener_matches(const struct tcp_endpoint *endpoint, const struct tcp_segment_addresses *addresses, uint16_t destination_port);
static struct tcp_endpoint * tcp_passive_syn(struct tcp_endpoint *listener, const struct tcp_segment_addresses *addresses, uint16_t source_port, uint32_t sequence);
static void tcp_child_addresses(struct tcp_endpoint *child, const struct tcp_endpoint *listener, const struct tcp_segment_addresses *addresses);
static int tcp_input(struct packet_buf *packet, uint32_t source, uint32_t destination);
static int tcp6_input(struct packet_buf *packet, const struct in6_addr *source, const struct in6_addr *destination);
static int tcp_segment_input(struct packet_buf *packet, const struct tcp_segment_addresses *addresses);
static int tcp6_route(struct tcp_endpoint *endpoint, struct net_device **device, struct in6_addr *source);

static const struct socket_ops tcp_ops = {
	.bind = tcp_bind,
	.connect = tcp_connect,
	.listen = tcp_listen,
	.accept = tcp_accept,
	.sendto = tcp_sendto,
	.recvfrom = tcp_recvfrom,
	.shutdown = tcp_shutdown,
	.getsockname = tcp_getsockname,
	.getpeername = tcp_getpeername,
	.ioctl = inet_socket_ioctl,
	.poll = tcp_poll,
	.close = tcp_close,
	.endpoint_close = tcp_endpoint_close,
	.setsockopt = tcp_setsockopt,
	.getsockopt = tcp_getsockopt,
	.keepalive_changed = tcp_keepalive_changed,
};

/*
 * Creates a closed TCP socket and registers it.
 */
int
tcp_socket_create(
	int protocol,
	struct socket **result)
{
	struct tcp_endpoint *endpoint;
	unsigned long irq;

	/* Rejects a missing result or a protocol other than TCP. */
	if (result == NULL || (protocol != 0 && protocol != IPPROTO_TCP))
		return EPROTONOSUPPORT;

	/* Allocates the endpoint and links it into the registry. */
	endpoint = kern_calloc(1, sizeof(*endpoint));
	if (endpoint == NULL)
		return ENOMEM;
	inet_socket_object_init(&endpoint->tcp.inet, SOCK_STREAM, IPPROTO_TCP,
	    &tcp_ops);
	endpoint->tcp.state = TCP_CLOSED;

	/* Its receive queue takes the packets the window it offers counts (BUG-222). */
	endpoint->tcp.inet.socket.receive_packet_limit = TCP_RECEIVE_PACKETS;
	irq = spin_lock_irqsave(&tcp_registry_lock);

	endpoint->next = tcp_sockets;
	tcp_sockets = endpoint;

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	*result = &endpoint->tcp.inet.socket;

	/* Reports the created socket. */
	return 0;
}

/*
 * Initializes the TCP registry and registers the input handler.
 */
int
tcp_init(
	void)
{
	int error;

	tcp_sockets = NULL;
	next_ephemeral = TCP_EPHEMERAL_FIRST;
	spin_init(&tcp_registry_lock, LOCK_RANK_SOCKET_REGISTRY,
	    "TCP socket registry");

	/* Reports why the registration failed. */
	error = ipv4_protocol_register(IPPROTO_TCP, tcp_input);
	if (error != 0)
		return error;

	/* And from IPv6 (ws130-p003). */
	error = ipv6_protocol_register(IPPROTO_TCP, tcp6_input);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Decides what an expired idle probe means for one socket.
 *
 * The caller holds the socket lock.  Returns 1 when a probe should be sent,
 * -1 when the peer has failed to answer every probe allowed, and 0 when the
 * socket no longer needs one.
 */
static int
tcp_keepalive_expire_locked(
	struct tcp_endpoint *endpoint,
	uint64_t now)
{
	/* Stops probing a connection that is no longer established. */
	if (endpoint->tcp.inet.socket.keepalive == 0 ||
	    endpoint->tcp.state != TCP_ESTABLISHED) {
		endpoint->tcp.keepalive_deadline = 0;
		endpoint->tcp.keepalive_probes = 0;
		return 0;
	}

	/* Declares the peer unreachable once every probe went unanswered. */
	if (endpoint->tcp.keepalive_probes >= TCP_KEEPALIVE_COUNT) {
		endpoint->tcp.keepalive_deadline = 0;
		endpoint->tcp.keepalive_probes = 0;
		endpoint->tcp.state = TCP_CLOSED;
		endpoint->tcp.active_connect_generation = 0;
		endpoint->tcp.connect_wait_deadline = 0;
		return -1;
	}

	/* Schedules the next probe and asks for this one. */
	endpoint->tcp.keepalive_probes++;
	endpoint->tcp.keepalive_deadline = now + TCP_KEEPALIVE_INTERVAL;
	return 1;
}

/*
 * Takes an ICMPv6 error about a segment this host sent (ws130-p003): a
 * refusal (port unreachable) ends a connect still sending its SYN, as a
 * reset would; the other errors are soft (RFC 1122 section 4.2.3.9) and
 * leave the connection to its timers.
 */
void
tcp6_error(
	const struct in6_addr *source,
	const struct in6_addr *destination,
	uint16_t source_port,
	uint16_t destination_port,
	int error)
{
	struct tcp_segment_addresses addresses;
	struct tcp_endpoint *endpoint;
	struct tcp_discard retransmit;
	unsigned long socket_irq;
	int refused;

	/* Only a refusal is a hard error. */
	if (error != ECONNREFUSED)
		return;

	/* The connection the segment went from, found as its answer would be. */
	kern_memset(&addresses, 0, sizeof(addresses));
	addresses.family = AF_INET6;
	addresses.source6 = *destination;
	addresses.destination6 = *source;
	endpoint = tcp_lookup(&addresses, destination_port, source_port);
	if (endpoint == NULL)
		return;

	/* A connect still sending its SYN is refused. */
	retransmit.count = 0;
	socket_irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);

	refused = 0;
	if (endpoint->tcp.state == TCP_SYN_SENT) {
		tcp_retransmit_reset(endpoint, &retransmit);
		endpoint->tcp.state = TCP_CLOSED;
		endpoint->tcp.active_connect_generation = 0;
		endpoint->tcp.connect_wait_deadline = 0;
		tcp_forget_peer(endpoint);
		refused = 1;
	}

	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

	/* The connect hears of it. */
	tcp_discard_free(&retransmit);
	if (refused) {
		socket_set_error(&endpoint->tcp.inet.socket, ECONNREFUSED);
		socket_wake_connect(&endpoint->tcp.inet.socket);
	}
	socket_release(&endpoint->tcp.inet.socket);
}

/*
 * Retransmits expired segments, abandoning attempts that failed too often.
 *
 * Each pass also sends what the device or the packet pool refused before,
 * once its retry is due (ws220-p003); the network worker runs a pass after
 * each device's transmit completion as well as at its deadlines.
 */
void
tcp_timer_run(
	void)
{
	struct tcp_endpoint *endpoint;
	struct tcp_endpoint *stack_snapshot[TCP_TIMER_STACK_SNAPSHOT];
	struct tcp_endpoint **snapshot;
	struct tcp_endpoint **allocated;
	unsigned registered;
	unsigned capacity;
	unsigned count;
	unsigned index;
	int referenced;
	unsigned long irq;
	uint64_t now;
	struct socket *socket;
	struct tcp_discard discard;
	struct tcp_pending *oldest;
	uint8_t payload[TCP_MSS];
	uint8_t flags;
	uint32_t sequence;
	size_t length;
	uint64_t delay;
	enum tcp_refusal refusal;
	unsigned sent;
	unsigned serial;
	unsigned exponent;
	unsigned immediate;
	uint64_t rto;
	int retries;
	int still_oldest;
	int given_up;
	int error;
	int failed;
	int probe;

	count = 0;
	registered = 0;
	now = sched_ticks();

	/* Holds the registry still while its sockets are counted. */
	irq = spin_lock_irqsave(&tcp_registry_lock);

	/* Counts the registered sockets, which sizes the snapshot. */
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next)
		registered++;

	/* Lets sockets be created and closed again while the snapshot is sized. */
	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/*
	 * Uses the stack snapshot, or an allocated one when there are more
	 * sockets than it holds.  Without memory the stack snapshot serves
	 * the first sockets now and the others on a later pass.
	 */
	snapshot = stack_snapshot;
	capacity = TCP_TIMER_STACK_SNAPSHOT;
	allocated = NULL;
	if (registered > TCP_TIMER_STACK_SNAPSHOT)
		allocated = kern_calloc(registered, sizeof(*allocated));

	/* Switches to the allocated snapshot when there is one. */
	if (allocated != NULL) {
		snapshot = allocated;
		capacity = registered;
	}

	/*
	 * Snapshots the referenced sockets so no registry lock is held during
	 * I/O.  A socket created since the count waits for the next pass.
	 */
	irq = spin_lock_irqsave(&tcp_registry_lock);

	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next) {
		/* Leaves a socket past the snapshot for the next pass. */
		if (count >= capacity)
			break;

		/* Skips a socket whose closing has begun. */
		referenced = socket_tryref(&endpoint->tcp.inet.socket);
		if (!referenced)
			continue;

		/* Keeps the referenced socket in the snapshot. */
		snapshot[count] = endpoint;
		count++;
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Handles each socket whose retransmission deadline passed. */
	for (index = 0; index < count; index++) {
		discard.count = 0;
		endpoint = snapshot[index];
		socket = &endpoint->tcp.inet.socket;

		/* Sends the segments and acknowledgements refused before, when their retry is due. */
		tcp_output_retry(endpoint, payload, now);

		/*
		 * Only a segment handed to the device has a retransmission
		 * deadline; the unsent ones wait for the output retry.
		 */
		irq = spin_lock_irqsave(&socket->lock);
		sent = tcp_sent_count_locked(endpoint);
		if (sent == 0 ||
		    endpoint->tcp.retransmit_deadline > now) {
			/*
			 * With nothing to retransmit, an armed idle probe is
			 * the other reason this socket may need attention.
			 */
			probe = 0;
			if (endpoint->tcp.send_count == 0 &&
			    endpoint->tcp.keepalive_deadline != 0 &&
			    endpoint->tcp.keepalive_deadline <= now)
				probe = tcp_keepalive_expire_locked(endpoint, now);
			spin_unlock_irqrestore(&socket->lock, irq);

			/* A peer that answered no probe at all is gone. */
			if (probe < 0) {
				tcp_forget_peer(endpoint);
				socket_set_error(socket, ETIMEDOUT);
				socket_wake_connect(socket);
				socket_wake_receive(socket);
				socket_release(socket);
				continue;
			}

			/*
			 * The probe is an acknowledgement for the last byte
			 * already sent, which the peer answers without
			 * delivering anything to its reader.
			 */
			if (probe > 0)
				(void)tcp_send_segment_at(endpoint,
				    endpoint->tcp.send_next - 1U, TCP_ACK,
				    NULL, 0);
			socket_release(socket);
			continue;
		}

		/*
		 * A segment resent for R2 without an answer from the peer
		 * abandons the attempt (RFC 1122 section 4.2.3.5).
		 */
		given_up = tcp_retransmit_given_up_locked(endpoint, now);
		if (given_up) {
			tcp_retransmit_reset(endpoint, &discard);
			endpoint->tcp.state = TCP_CLOSED;
			endpoint->tcp.active_connect_generation = 0;
			endpoint->tcp.connect_wait_deadline = 0;
			tcp_forget_peer(endpoint);
			spin_unlock_irqrestore(&socket->lock, irq);
			tcp_discard_free(&discard);
			socket_set_error(socket, ETIMEDOUT);
			if (endpoint->tcp.listener != NULL) {
				tcp_listener_remove(endpoint);
				/* Queue ownership. */
				socket_release(socket);
				/* Timer snapshot ownership. */
				socket_release(socket);
				continue;
			}

			socket_wake_connect(socket);
			socket_wake_receive(socket);
			socket_release(socket);
			continue;
		}

		/*
		 * The oldest outstanding segment is the one a timeout
		 * resends; anything after it follows once the peer
		 * acknowledges this one.  It is copied out so the send can
		 * happen with the lock released.
		 */
		oldest = &endpoint->tcp.send_queue[endpoint->tcp.send_first];
		sequence = oldest->sequence;
		flags = oldest->flags;
		length = oldest->packet->length;
		if (length > sizeof(payload)) {
			tcp_retransmit_reset(endpoint, &discard);
			endpoint->tcp.state = TCP_CLOSED;
			endpoint->tcp.active_connect_generation = 0;
			endpoint->tcp.connect_wait_deadline = 0;
			tcp_forget_peer(endpoint);
			spin_unlock_irqrestore(&socket->lock, irq);
			tcp_discard_free(&discard);
			socket_set_error(socket, EIO);
			socket_release(socket);
			continue;
		}

		if (length != 0)
			kern_memcpy(payload, oldest->packet->data, length);

		/*
		 * Counts the resend of the oldest segment, and backs the timeout
		 * off exponentially, up to the longest, when it really expired:
		 * an immediate resend (recovery, a reopened window) does not
		 * double it (ws220-p004 review-1 G1).
		 */
		endpoint->tcp.retransmit_count++;
		endpoint->tcp.retransmit_serial++;
		serial = endpoint->tcp.retransmit_serial;
		immediate = endpoint->tcp.retransmit_immediate;
		endpoint->tcp.retransmit_immediate = 0;
		if (!immediate)
			endpoint->tcp.rto_backoff++;
		exponent = endpoint->tcp.rto_backoff;

		/*
		 * A timeout that really expired starts a recovery up to what was
		 * sent by then (RFC 6582 section 4): partial acknowledgements
		 * have the next hole resent at once, segments sent after it are
		 * not resent (they give the sample that ends the backoff,
		 * ws220-p004 review-1 G1), and the duplicates the resends bring
		 * start no fast retransmit.
		 */
		if (!immediate) {
			endpoint->tcp.recover = tcp_sent_end_locked(endpoint);
			endpoint->tcp.recovery = 1;
			endpoint->tcp.recovery_by_timeout = 1;
			endpoint->tcp.duplicate_acks = 0;
			endpoint->tcp.dupack_guard = 0;
		}

		/* The next deadline, the timeout doubled as many times as it is backed off. */
		rto = tcp_rto_locked(endpoint);
		delay = tcp_backoff_timeout(rto, exponent);
		endpoint->tcp.retransmit_deadline = now + delay;

		/*
		 * Karn's rule (RFC 6298 section 3): an acknowledgement of a
		 * segment resent may answer either sending, so the segment
		 * resent is timed no longer.  A timeout that really expired stops
		 * any timing, as BSD does; an immediate resend (recovery, a
		 * reopened window) stops it only when it resends the segment
		 * timed, so a later segment's sample still ends the backoff
		 * (ws220-p004 review-1 G1: reopened windows came every round trip
		 * from a peer like this one).  Since ws220-p005 the immediate
		 * resends of a loss (a fast retransmit, a partial acknowledgement)
		 * stop any timing too (p005 review-1 M2: the segment timed behind
		 * a hole would give a sample of the recovery, not of the round
		 * trip); a probe's stops only its own.  A SYN resent
		 * gives the handshake no sample, and the data three seconds
		 * (section 5.7).
		 */
		if (immediate != TCP_IMMEDIATE_PROBE) {
			endpoint->tcp.rtt_timing = 0;
		} else if (endpoint->tcp.rtt_timing != 0 && endpoint->tcp.rtt_sequence == sequence + oldest->advance) {
			endpoint->tcp.rtt_timing = 0;
		}

		/* A SYN (or SYN-ACK) resent. */
		if (endpoint->tcp.state == TCP_SYN_SENT || endpoint->tcp.state == TCP_SYN_RECEIVED)
			endpoint->tcp.syn_timed_out = 1;

		/*
		 * A resend into a window that cannot take it probes the window
		 * (RFC 9293 section 3.8.6.1, ws220-p005): it is sent again at
		 * once when the window opens, rather than at the backed-off
		 * timeout.
		 */
		oldest->probe = 0;
		if (length != 0 &&
		    sequence + oldest->advance - endpoint->tcp.send_unacknowledged > (uint32_t)endpoint->tcp.peer_window)
			oldest->probe = 1;
		spin_unlock_irqrestore(&socket->lock, irq);

		/* Resends the oldest segment. */
		error = tcp_send_segment_at(endpoint, sequence, flags, payload, length);
		refusal = tcp_refusal_of(error);

		/*
		 * A resend the device took is counted for net.tcpstats.  One the
		 * device or the pool refused did not happen: the backoff is taken
		 * back and the resend tried again soon (ws220-p003), rather than
		 * after the doubled timeout; soon is TCP_SEND_RETRY_MS once it has
		 * been refused before (review-1 M2), so a device that never
		 * completes is not tried every tick.  One lost for an unresolved
		 * neighbour keeps the backoff, as one lost on the wire.
		 */
		irq = spin_lock_irqsave(&socket->lock);

		if (error == 0) {
			endpoint->tcp.counters.retransmit_segments++;
			endpoint->tcp.counters.retransmit_bytes += length;
		}

		/* A refused resend of the segment still oldest, whose backoff no one has moved since. */
		sent = tcp_sent_count_locked(endpoint);
		retries = tcp_refusal_retries(refusal);
		still_oldest = 0;
		if (retries &&
		    sent != 0 &&
		    endpoint->tcp.send_queue[endpoint->tcp.send_first].sequence == sequence &&
		    endpoint->tcp.retransmit_serial == serial)
			still_oldest = 1;
		if (still_oldest) {
			delay = tcp_refusal_delay(refusal);
			if (endpoint->tcp.retransmit_refused != 0)
				delay = tcp_refusal_delay(TCP_REFUSAL_WAIT);
			if (endpoint->tcp.retransmit_count != 0)
				endpoint->tcp.retransmit_count--;
			endpoint->tcp.retransmit_deadline = now + delay;

			/* The doubling taken back too, unless a sample has set it again meanwhile; an immediate one stays so. */
			if (!immediate && endpoint->tcp.rto_backoff == exponent)
				endpoint->tcp.rto_backoff--;
			endpoint->tcp.retransmit_immediate = immediate;

			/* The next refused resend knows this one was refused too. */
			endpoint->tcp.retransmit_refused = 1;
		}

		/* A resend that went, or was lost as on the wire, ends the run of refusals. */
		if (!retries)
			endpoint->tcp.retransmit_refused = 0;

		spin_unlock_irqrestore(&socket->lock, irq);

		/* A hard send error abandons the attempt the segment belonged to. */
		if (refusal == TCP_REFUSAL_HARD) {
			failed = 0;
			irq = spin_lock_irqsave(&socket->lock);
			sent = tcp_sent_count_locked(endpoint);
			if (sent != 0 &&
			    endpoint->tcp.send_queue[endpoint->tcp.send_first]
				.sequence == sequence) {
				tcp_retransmit_reset(endpoint, &discard);
				endpoint->tcp.state = TCP_CLOSED;
				endpoint->tcp.active_connect_generation = 0;
				endpoint->tcp.connect_wait_deadline = 0;
				tcp_forget_peer(endpoint);
				failed = 1;
			}

			spin_unlock_irqrestore(&socket->lock, irq);
			tcp_discard_free(&discard);
			if (!failed) {
				socket_release(socket);
				continue;
			}

			socket_set_error(socket, error);
			if (endpoint->tcp.listener != NULL) {
				tcp_listener_remove(endpoint);
				/* Queue ownership. */
				socket_release(socket);
				/* Timer snapshot ownership. */
				socket_release(socket);
				continue;
			}

			socket_wake_connect(socket);
			socket_release(socket);
			continue;
		}

		socket_release(socket);
	}

	/* Frees the allocated snapshot; the references were all released above. */
	if (allocated != NULL)
		kern_free(allocated);
}

/*
 * Reports the earliest retransmission deadline, or zero without one.
 *
 * A socket's deadline is its oldest sent segment's retransmission, or with
 * nothing queued its idle probe, and its output retry when that comes
 * first (ws220-p003): unsent segments and pending acknowledgements have no
 * retransmission deadline of their own.
 */
uint64_t
tcp_timer_next_deadline(
	void)
{
	struct tcp_endpoint *endpoint;
	uint64_t deadline;
	unsigned long irq;
	uint64_t candidate;
	uint64_t retry;
	unsigned long socket_irq;
	unsigned sent;

	deadline = 0;
	irq = spin_lock_irqsave(&tcp_registry_lock);

	/* Takes the minimum over every socket with a segment outstanding. */
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next) {
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);

		/* The oldest sent segment's retransmission, or with nothing queued the idle probe. */
		sent = tcp_sent_count_locked(endpoint);
		candidate = 0;
		if (sent != 0)
			candidate = endpoint->tcp.retransmit_deadline;
		else if (endpoint->tcp.send_count == 0)
			candidate = endpoint->tcp.keepalive_deadline;

		/*
		 * The output retry's deadline, unless a thread is sending the
		 * unsent entries: the retry is not due then, and a deadline
		 * already passed would keep the worker from sleeping until that
		 * thread is done.  It wakes the worker when it is.
		 */
		retry = 0;
		if (endpoint->tcp.send_flushing == 0)
			retry = endpoint->tcp.output_retry_deadline;

		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

		/* The output retry, when it is due before the socket's other deadline. */
		if (retry != 0 && (candidate == 0 || retry < candidate))
			candidate = retry;

		/* The earliest so far. */
		if (candidate != 0 && (deadline == 0 || candidate < deadline))
			deadline = candidate;
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Reports the earliest deadline. */
	return deadline;
}

/*
 * Copies net.tcpstats: the header and each registered TCP socket.
 *
 * The registry lock is held across the count and the copy, so the entries
 * written are the ones counted; each socket is read under its own lock,
 * taken after the registry's as the timer takes them.  Without an output,
 * or with one shorter than needed (ENOMEM), only the length is reported.
 * A reader that is not the superuser is given every socket without its
 * sequence numbers.
 */
int
tcp_stats_copy(
	void *output,
	size_t capacity,
	size_t *needed,
	int superuser)
{
	struct tcp_stats_header header;
	struct tcp_stats_entry entry;
	struct packet_buf_pool_usage usage;
	struct tcp_endpoint *endpoint;
	uint8_t *place;
	unsigned long irq;
	unsigned long socket_irq;
	uint32_t count;
	uint64_t now;
	int error;

	/* The moment of the reading and the packet pool's use at it. */
	now = sched_ticks();
	packet_buf_pool_usage(&usage);

	/* The sockets, counted and copied with the registry held still. */
	irq = spin_lock_irqsave(&tcp_registry_lock);

	/* Counts them, which sizes the value. */
	count = 0;
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next)
		count++;

	/* The length, and nothing written without a buffer or with one too small. */
	*needed = sizeof(header) + (size_t)count * sizeof(entry);
	error = 0;
	if (output != NULL && capacity < *needed)
		error = ENOMEM;
	if (output == NULL || error != 0) {
		spin_unlock_irqrestore(&tcp_registry_lock, irq);
		return error;
	}

	/* The header. */
	kern_memset(&header, 0, sizeof(header));
	header.version = TCP_STATS_VERSION;
	header.struct_size = sizeof(header);
	header.element_size = sizeof(entry);
	header.count = count;
	header.packets_total = usage.total;
	header.packets_used = usage.used;
	header.packets_used_max = usage.used_max;
	header.packet_alloc_failures = usage.alloc_failures;
	header.time_ms = kern_ticks_to_ms(now);
	place = output;
	kern_memcpy(place, &header, sizeof(header));
	place += sizeof(header);

	/* Each socket after it, as one moment of its own state. */
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next) {
		socket_irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);
		tcp_stats_fill_locked(endpoint, now, superuser, &entry);
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

		kern_memcpy(place, &entry, sizeof(entry));
		place += sizeof(entry);
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Succeeded: the header and every socket are in the output. */
	return 0;
}

/*
 * Arms or disarms the idle probe for one socket.
 *
 * The caller holds the socket lock.  Only an established connection probes:
 * there is nothing to keep alive before it exists or after it is gone.
 */
static void
tcp_keepalive_arm_locked(
	struct tcp_endpoint *endpoint)
{
	endpoint->tcp.keepalive_probes = 0;

	/* Disarms when the option is off or the connection is not up. */
	if (endpoint->tcp.inet.socket.keepalive == 0 ||
	    endpoint->tcp.state != TCP_ESTABLISHED) {
		endpoint->tcp.keepalive_deadline = 0;
		return;
	}

	/* Restarts the idle period. */
	endpoint->tcp.keepalive_deadline = sched_ticks() + TCP_KEEPALIVE_IDLE;
}

/*
 * Reacts to SO_KEEPALIVE being set or cleared on an existing socket.
 */
static void
tcp_keepalive_changed(
	struct socket *socket)
{
	struct tcp_endpoint *endpoint;
	unsigned long irq;

	endpoint = tcp_endpoint(socket);

	/* Handles a socket that is not a TCP endpoint. */
	if (endpoint == NULL)
		return;
	irq = spin_lock_irqsave(&socket->lock);
	tcp_keepalive_arm_locked(endpoint);
	spin_unlock_irqrestore(&socket->lock, irq);
}

/*
 * Sets one option at the TCP protocol level.
 */
static int
tcp_setsockopt(
	struct socket *socket,
	int level,
	int option,
	const void *value,
	socklen_t length)
{
	struct tcp_endpoint *endpoint;
	unsigned long irq;
	int enabled;
	int error;

	/* IPv6's options (IPV6_V6ONLY) are the internet socket layer's (ws130-p003). */
	if (socket != NULL && level == IPPROTO_IPV6) {
		endpoint = tcp_endpoint(socket);
		error = inet_socket_setsockopt(&endpoint->tcp.inet, level, option, value, length);
		if (error != 0)
			return error;
		return 0;
	}

	/* Only this protocol's own level is handled here. */
	if (socket == NULL || level != IPPROTO_TCP)
		return ENOPROTOOPT;
	if (option != TCP_NODELAY)
		return ENOPROTOOPT;
	if (value == NULL || length != sizeof(enabled))
		return EINVAL;
	kern_memcpy(&enabled, value, sizeof(enabled));

	endpoint = tcp_endpoint(socket);

	/* Handles a socket that is not a TCP endpoint. */
	if (endpoint == NULL)
		return ENOPROTOOPT;

	/*
	 * The stored value is what getsockopt reports.  This implementation
	 * sends each write as its own segment as soon as the previous one is
	 * acknowledged, so it never accumulates small writes; clearing the
	 * option therefore does not introduce the delay the name refers to.
	 */
	irq = spin_lock_irqsave(&socket->lock);
	endpoint->tcp.nodelay = enabled != 0;
	spin_unlock_irqrestore(&socket->lock, irq);

	/* Reports successful completion. */
	return 0;
}

/*
 * Reads one option at the TCP protocol level.
 */
static int
tcp_getsockopt(
	struct socket *socket,
	int level,
	int option,
	void *value,
	socklen_t *length)
{
	struct tcp_endpoint *endpoint;
	unsigned long irq;
	int enabled;
	int error;

	/* IPv6's options (IPV6_V6ONLY) are the internet socket layer's (ws130-p003). */
	if (socket != NULL && level == IPPROTO_IPV6) {
		endpoint = tcp_endpoint(socket);
		error = inet_socket_getsockopt(&endpoint->tcp.inet, level, option, value, length);
		if (error != 0)
			return error;
		return 0;
	}

	/* Only this protocol's own level is handled here. */
	if (socket == NULL || level != IPPROTO_TCP)
		return ENOPROTOOPT;
	if (option != TCP_NODELAY)
		return ENOPROTOOPT;
	if (value == NULL || length == NULL || *length < sizeof(enabled))
		return EINVAL;

	endpoint = tcp_endpoint(socket);

	/* Handles a socket that is not a TCP endpoint. */
	if (endpoint == NULL)
		return ENOPROTOOPT;
	irq = spin_lock_irqsave(&socket->lock);
	enabled = endpoint->tcp.nodelay != 0;
	spin_unlock_irqrestore(&socket->lock, irq);
	kern_memcpy(value, &enabled, sizeof(enabled));
	*length = sizeof(enabled);

	/* Reports successful completion. */
	return 0;
}

/* Converts a socket to its endpoint. */
static struct tcp_endpoint *
tcp_endpoint(
	struct socket *socket)
{
	return (struct tcp_endpoint *)socket;
}

/* Clears the peer address of an endpoint. */
static void
tcp_forget_peer(
	struct tcp_endpoint *endpoint)
{
	endpoint->tcp.inet.remote_address = 0;
	kern_memset(&endpoint->tcp.inet.remote6, 0, sizeof(endpoint->tcp.inet.remote6));
	endpoint->tcp.inet.scope6 = 0;
	endpoint->tcp.inet.remote_port = 0;
	endpoint->tcp.inet.inet_flags &= ~INET_SOCKET_CONNECTED;
}

/* Removes a child from its listener's half-open or accept queue. */
static void
tcp_listener_remove(
	struct tcp_endpoint *child)
{
	struct tcp_endpoint *listener;
	struct tcp_socket **link;
	struct tcp_socket *previous;
	unsigned long irq;

	previous = NULL;

	/* Ignores a child without a listener. */
	if (child == NULL || child->tcp.listener == NULL)
		return;
	listener = (struct tcp_endpoint *)child->tcp.listener;
	irq = spin_lock_irqsave(&listener->tcp.inet.socket.lock);

	/* Searches the half-open queue first. */
	for (link = &listener->tcp.half_open_head; *link != NULL;
	     link = &(*link)->queue_next) {
		if (*link == &child->tcp) {
			*link = child->tcp.queue_next;
			if (listener->tcp.half_open_count != 0)
				listener->tcp.half_open_count--;
			child->tcp.queue_next = NULL;
			child->tcp.listener = NULL;
			spin_unlock_irqrestore(&listener->tcp.inet.socket.lock, irq);
			return;
		}
	}

	/* Then the accept queue, fixing its tail. */
	for (link = &listener->tcp.accept_head; *link != NULL;
	     link = &(*link)->queue_next) {
		if (*link == &child->tcp) {
			*link = child->tcp.queue_next;
			if (listener->tcp.accept_tail == &child->tcp)
				listener->tcp.accept_tail = previous;
			if (listener->tcp.accept_count != 0)
				listener->tcp.accept_count--;
			child->tcp.queue_next = NULL;
			child->tcp.listener = NULL;
			spin_unlock_irqrestore(&listener->tcp.inet.socket.lock, irq);
			return;
		}

		previous = *link;
	}

	spin_unlock_irqrestore(&listener->tcp.inet.socket.lock, irq);
}

/* Moves a child whose handshake completed to its listener's accept queue. */
static void
tcp_listener_established(
	struct tcp_endpoint *child)
{
	struct tcp_endpoint *listener;
	struct tcp_socket **link;
	unsigned long irq;

	/* Ignores a child without a listener. */
	if (child == NULL || child->tcp.listener == NULL)
		return;
	listener = (struct tcp_endpoint *)child->tcp.listener;
	irq = spin_lock_irqsave(&listener->tcp.inet.socket.lock);

	/* Unlinks the child from the half-open queue. */
	for (link = &listener->tcp.half_open_head; *link != NULL;
	     link = &(*link)->queue_next) {
		if (*link == &child->tcp) {
			*link = child->tcp.queue_next;
			if (listener->tcp.half_open_count != 0)
				listener->tcp.half_open_count--;
			break;
		}
	}

	/* Appends it to the accept queue and wakes the acceptors. */
	child->tcp.queue_next = NULL;
	if (listener->tcp.accept_tail != NULL)
		listener->tcp.accept_tail->queue_next = &child->tcp;
	else
		listener->tcp.accept_head = &child->tcp;
	listener->tcp.accept_tail = &child->tcp;
	listener->tcp.accept_count++;
	waitq_wake_all(&listener->tcp.inet.socket.accept_waitq);
	poll_notify();

	spin_unlock_irqrestore(&listener->tcp.inet.socket.lock, irq);
}

/* Tests whether another socket conflicts with a candidate's local address. */
static int
tcp_port_in_use(
	const struct tcp_endpoint *candidate,
	int strict)
{
	const struct tcp_endpoint *endpoint;
	int conflict;

	/* A strict check ignores address reuse. */
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next) {
		if (endpoint == candidate)
			continue;
		if (strict)
			conflict = inet_socket_local_conflict(&endpoint->tcp.inet, 0,
			    &candidate->tcp.inet, 0);
		else
			conflict = inet_socket_local_conflict(&endpoint->tcp.inet,
			    endpoint->tcp.inet.bind_reuse_address,
			    &candidate->tcp.inet,
			    candidate->tcp.inet.bind_reuse_address);
		if (conflict)
			return 1;
	}

	return 0;
}

/* Assigns a free ephemeral port; the caller holds the registry lock. */
static int
tcp_allocate_port_locked(
	struct tcp_endpoint *endpoint)
{
	unsigned attempts;
	uint16_t port;

	/* Tries successive ephemeral ports, wrapping within the range. */
	for (attempts = 0; attempts < 16384U; attempts++) {
		port = next_ephemeral;
		next_ephemeral++;
		if (next_ephemeral < TCP_EPHEMERAL_FIRST)
			next_ephemeral = TCP_EPHEMERAL_FIRST;
		endpoint->tcp.inet.local_port = port;
		if (!tcp_port_in_use(endpoint, 1)) {
			endpoint->tcp.inet.inet_flags |= INET_SOCKET_BOUND;
			return 0;
		}

		endpoint->tcp.inet.local_port = 0;
	}

	return EADDRINUSE;
}

/* Finds the output device and source address for an endpoint's peer. */
static int
tcp_route(
	struct tcp_endpoint *endpoint,
	struct net_device **device,
	uint32_t *source)
{
	struct net_route route;
	struct net_device *output;
	int have_route;
	int error;

	/* A bound interface wins over the route's device. */
	have_route = 0;
	if (route_lookup_ref(endpoint->tcp.inet.remote_address, &route) == 0)
		have_route = 1;
	if (endpoint->tcp.inet.ifindex != 0)
		output = net_device_find_by_index_ref(endpoint->tcp.inet.ifindex);
	else if (have_route)
		output = route.device;
	else
		output = NULL;
	if (endpoint->tcp.inet.ifindex == 0 && output != NULL)
		route.device = NULL;
	if (have_route)
		route_release(&route);
	if (output == NULL)
		return ENETUNREACH;

	/* Takes the device's address as the source and remembers the interface. */
	error = inet_interface_address(output, source, NULL, NULL);
	if (error != 0) {
		net_device_release(output);
		return error;
	}

	endpoint->tcp.inet.ifindex = output->ifindex;
	if (endpoint->tcp.inet.local_address == 0)
		endpoint->tcp.inet.local_address = *source;
	*device = output;
	return 0;
}

/*
 * Finds the output device (NULL: the route's) and the source address for
 * an IPv6 endpoint's peer (ws130-p003): the bound interface, or the one a
 * link-local peer is on, and the bound address or the one chosen (RFC 6724),
 * which the endpoint then keeps.
 */
static int
tcp6_route(
	struct tcp_endpoint *endpoint,
	struct net_device **device,
	struct in6_addr *source)
{
	struct net_device *output;
	struct in6_addr chosen;
	unsigned ifindex;
	int linklocal;
	int unspecified;
	int error;

	/* The interface asked for. */
	ifindex = endpoint->tcp.inet.ifindex;
	linklocal = in6_is_linklocal(&endpoint->tcp.inet.remote6);
	if (ifindex == 0 && linklocal)
		ifindex = endpoint->tcp.inet.scope6;
	output = NULL;
	if (ifindex != 0) {
		output = net_device_find_by_index_ref(ifindex);
		if (output == NULL)
			return ENETUNREACH;
	}

	/* The way to the peer, and the source it would take. */
	error = ipv6_route_source(output, &endpoint->tcp.inet.remote6, &chosen, NULL);
	if (error != 0) {
		if (output != NULL)
			net_device_release(output);
		return error;
	}

	/* The endpoint keeps the first source, as its name and its connection's. */
	unspecified = in6_is_unspecified(&endpoint->tcp.inet.local6);
	if (unspecified)
		endpoint->tcp.inet.local6 = chosen;
	*source = endpoint->tcp.inet.local6;
	*device = output;

	/* Succeeded: the device (or NULL) and the source. */
	return 0;
}

/*
 * Transmits one segment with an explicit sequence number and counts it.
 *
 * Every segment this end sends goes through here, so the counts of
 * net.tcpstats see each one: sent, or failed on its way to the device (an
 * acknowledgement or a resend that is then lost, as on the wire).  The
 * caller holds no socket lock, which the counting takes.
 */
static int
tcp_send_segment_at(
	struct tcp_endpoint *endpoint,
	uint32_t sequence,
	uint8_t flags,
	const void *data,
	size_t length)
{
	unsigned long irq;
	int error;

	/* Builds and hands the segment to the device. */
	error = tcp_output_segment(endpoint, sequence, flags, data, length);

	/* Counts the segment and, when it went, a closed window it told of. */
	irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);

	if (error != 0) {
		endpoint->tcp.counters.segment_send_failures++;
	} else {
		endpoint->tcp.counters.segments_out++;
		if (endpoint->tcp.advertised_window == 0)
			endpoint->tcp.counters.zero_windows_out++;
	}

	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, irq);

	/* Reports why the segment did not go. */
	if (error != 0)
		return error;

	/* Succeeded: the device has the segment. */
	return 0;
}

/*
 * Fills one net.tcpstats entry from a socket, under its lock.
 *
 * now is the tick of the reading, which the time left of the
 * retransmission deadline is measured from.  The sequence numbers are given
 * to the superuser only (ws220-p002): with them and the addresses, a
 * process could put data or a reset into another user's connection.
 */
static void
tcp_stats_fill_locked(
	const struct tcp_endpoint *endpoint,
	uint64_t now,
	int superuser,
	struct tcp_stats_entry *entry)
{
	const struct tcp_socket *tcp;
	const struct socket *socket;
	const struct tcp_counters *counters;
	uint32_t sent_end;
	unsigned sent;
	uint64_t rto;
	uint64_t timeout;
	int ipv6;

	/* The parts of the endpoint read, and an entry with every field not kept 0. */
	tcp = &endpoint->tcp;
	socket = &tcp->inet.socket;
	counters = &tcp->counters;
	kern_memset(entry, 0, sizeof(*entry));

	/* What has been handed to the device, which unsent segments are not part of (ws220-p003). */
	sent_end = tcp_sent_end_locked(endpoint);
	sent = tcp_sent_count_locked(endpoint);

	/* The addresses: IPv6 for a socket that speaks it, else IPv4 in network order. */
	ipv6 = inet_socket_speaks_ipv6(&tcp->inet);
	if (ipv6) {
		entry->family = TCP_STATS_FAMILY_INET6;
		kern_memcpy(entry->local_address, tcp->inet.local6.s6_addr, 16U);
		kern_memcpy(entry->remote_address, tcp->inet.remote6.s6_addr, 16U);
	} else {
		entry->family = TCP_STATS_FAMILY_INET;
		wire_put32(entry->local_address, tcp->inet.local_address);
		wire_put32(entry->remote_address, tcp->inet.remote_address);
	}

	/* The state, the ports, and the parts of TCP this stack keeps (none yet beyond the base). */
	entry->state = (uint32_t)tcp->state;
	entry->features = 0;
	entry->local_port = tcp->inet.local_port;
	entry->remote_port = tcp->inet.remote_port;

	/*
	 * The sequence numbers, for the superuser only; TCP_STATS_HAVE_SEQUENCE
	 * tells a reader they are given, so their 0 otherwise means "not given".
	 */
	if (superuser) {
		entry->features |= TCP_STATS_HAVE_SEQUENCE;
		entry->send_unacknowledged = tcp->send_unacknowledged;
		entry->send_next = sent_end;
		entry->receive_next = tcp->receive_next;
	}

	/* The send side: the peer's window, and the ring that holds what is in flight and what waits unsent. */
	entry->send_window = tcp->peer_window;
	entry->in_flight_bytes = sent_end - tcp->send_unacknowledged;
	entry->send_segments = tcp->send_count;
	entry->send_ring = TCP_SEND_QUEUE_MAX;
	entry->send_mss = TCP_MSS;

	/*
	 * The round-trip estimate (ws220-p004; srtt_us and rttvar_us mean
	 * nothing while rtt_samples is 0), and the retransmission timer: how
	 * many times its timeout is doubled (timeouts since the last valid
	 * sample), the timeout it waits now (up to 60 seconds), and what is
	 * left of it.
	 */
	entry->features |= TCP_STATS_HAVE_RTT;
	entry->srtt_us = tcp->srtt_us;
	entry->rttvar_us = tcp->rttvar_us;
	rto = tcp_rto_locked(endpoint);
	timeout = tcp_backoff_timeout(rto, tcp->rto_backoff);
	entry->rtt_samples = tcp->rtt_samples;
	entry->backoff = tcp->rto_backoff;
	entry->rto_ms = (uint32_t)kern_ticks_to_ms(timeout);

	/* The time left of a deadline still ahead; one due or none is 0. */
	if (sent != 0 && tcp->retransmit_deadline > now)
		entry->retransmit_in_ms = (uint32_t)kern_ticks_to_ms(tcp->retransmit_deadline - now);

	/* The receive side: what the peer may send, and what waits for the reader. */
	entry->receive_window = tcp->advertised_window;
	entry->reorder_segments = tcp->reorder_count;
	entry->receive_packets = (uint32_t)socket->receive_packets;
	entry->receive_bytes = (uint32_t)socket->receive_bytes;
	entry->receive_limit_bytes = (uint32_t)socket->receive_hiwat_bytes;

	/* The segment size the SYN offered, smaller over IPv6 for its larger header. */
	if (ipv6) {
		entry->receive_mss = TCP_RECEIVE_MSS6;
	} else {
		entry->receive_mss = TCP_RECEIVE_MSS;
	}

	/* The counts since the socket was created. */
	entry->segments_out = counters->segments_out;
	entry->segments_in = counters->segments_in;
	entry->data_segments_out = counters->data_segments_out;
	entry->data_bytes_out = counters->data_bytes_out;
	entry->data_bytes_in = counters->data_bytes_in;
	entry->retransmit_segments = counters->retransmit_segments;
	entry->retransmit_bytes = counters->retransmit_bytes;
	entry->immediate_resends = counters->immediate_resends;
	entry->send_refused = counters->send_refused;
	entry->send_buffer_sleeps = counters->send_buffer_sleeps;
	entry->segment_send_failures = counters->segment_send_failures;
	entry->acks_in = counters->acks_in;
	entry->duplicate_acks_in = counters->duplicate_acks_in;
	entry->acks_outside = counters->acks_outside;
	entry->zero_windows_in = counters->zero_windows_in;
	entry->zero_windows_out = counters->zero_windows_out;
	entry->window_updates_out = counters->window_updates_out;
	entry->old_segments_in = counters->old_segments_in;
	entry->out_of_order_in = counters->out_of_order_in;
	entry->out_of_order_kept = counters->out_of_order_kept;
	entry->receive_refused = counters->receive_refused;

	/* Fast retransmits (ws220-p005). */
	entry->features |= TCP_STATS_HAVE_FAST_RETRANSMIT;
	entry->fast_retransmits = counters->fast_retransmits;
}

/* Builds and transmits one segment with an explicit sequence number. */
static int
tcp_output_segment(
	struct tcp_endpoint *endpoint,
	uint32_t sequence,
	uint8_t flags,
	const void *data,
	size_t length)
{
	struct net_device *device;
	struct packet_buf *packet;
	struct tcp_wire *tcp;
	struct in6_addr source6;
	uint32_t source;
	uint16_t checksum;
	uint16_t window;
	uint16_t mss;
	uint8_t *option;
	size_t options;
	void *payload;
	int ipv6;
	int error;

	/*
	 * A SYN carries the maximum segment size this end takes: one receive
	 * slot, TCP_RECEIVE_MSS bytes.  The receive window counts a full segment
	 * per free slot, so a peer that sent larger segments would find it
	 * closing faster than it opens; without the option a peer assumes its
	 * own size.
	 */
	options = 0;
	if ((flags & TCP_SYN) != 0)
		options = 4U;

	/* Routes the segment: over IPv6 to an IPv6 peer (ws130-p003), else over IPv4. */
	source = 0;
	mss = TCP_RECEIVE_MSS;
	ipv6 = inet_socket_speaks_ipv6(&endpoint->tcp.inet);
	if (ipv6) {
		error = tcp6_route(endpoint, &device, &source6);
		mss = TCP_RECEIVE_MSS6;
	} else {
		error = tcp_route(endpoint, &device, &source);
	}
	if (error != 0)
		return error;

	/*
	 * Allocates its buffer.  An empty pool is ENOMEM, which tells it from
	 * a full device queue (ENOBUFS from the driver): the device says when
	 * it has room again, the pool does not (ws220-p003).
	 */
	packet = packet_buf_alloc(PACKET_BUF_DEFAULT_HEADROOM);
	if (packet == NULL) {
		if (device != NULL)
			net_device_release(device);
		return ENOMEM;
	}

	/* A segment too large for one packet's storage is never sent (the callers keep within TCP_MSS). */
	tcp = packet_buf_append(packet, sizeof(*tcp) + options);
	payload = packet_buf_append(packet, length);
	if (tcp == NULL || payload == NULL) {
		packet_buf_free(packet);
		if (device != NULL)
			net_device_release(device);
		return EMSGSIZE;
	}

	/* Fills the header with the current receive state and the free space. */
	kern_memset(tcp, 0, sizeof(*tcp));
	if (length != 0)
		kern_memcpy(payload, data, length);
	wire_put16(tcp->source, endpoint->tcp.inet.local_port);
	wire_put16(tcp->destination, endpoint->tcp.inet.remote_port);
	wire_put32(tcp->sequence, sequence);
	wire_put32(tcp->acknowledgement, endpoint->tcp.receive_next);
	tcp->data_offset = (uint8_t)((5U + options / 4U) << 4);
	tcp->flags = flags;

	/* Writes the MSS option (kind 2, length 4) after the fixed header. */
	if (options != 0) {
		option = (uint8_t *)(tcp + 1);
		option[0] = 2U;
		option[1] = 4U;
		option[2] = (uint8_t)(mss >> 8);
		option[3] = (uint8_t)(mss & 0xffU);
	}

	window = tcp_receive_window(endpoint);
	endpoint->tcp.advertised_window = window;
	wire_put16(tcp->window, window);

	/* Hands the segment to IPv6, with the checksum over its pseudo-header. */
	if (ipv6) {
		checksum = net_checksum_pseudo6(source6.s6_addr, endpoint->tcp.inet.remote6.s6_addr, IPPROTO_TCP,
		    packet->data, packet->length);
		wire_put16(tcp->checksum, checksum);
		error = ipv6_output(device, &source6, &endpoint->tcp.inet.remote6, IPPROTO_TCP, 0U, packet);
		if (device != NULL)
			net_device_release(device);
		if (error != 0)
			return error;
		return 0;
	}

	/* Hands the segment to IPv4. */
	checksum = net_checksum_pseudo(source, endpoint->tcp.inet.remote_address,
	    IPPROTO_TCP, packet->data, packet->length);
	wire_put16(tcp->checksum, checksum);
	error = ipv4_output(device, endpoint->tcp.inet.remote_address,
	    IPPROTO_TCP, packet);
	net_device_release(device);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Assigns a free ephemeral port under the registry lock. */
static int
tcp_allocate_port(
	struct tcp_endpoint *endpoint)
{
	unsigned long irq;
	int error;

	irq = spin_lock_irqsave(&tcp_registry_lock);

	error = tcp_allocate_port_locked(endpoint);

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Takes the retransmission record out of an endpoint for the caller to free.
 *
 * The unsent entries go with it, and so do the pending acknowledgements and
 * the output retry: a connection reset or given up owes the peer nothing, and
 * one just established has none yet.  send_flushing stays with the thread
 * that set it, which finds the ring emptied when it comes back.
 */
static void
tcp_retransmit_reset(
	struct tcp_endpoint *endpoint,
	struct tcp_discard *discard)
{
	unsigned index;
	unsigned slot;

	/* Hands every outstanding payload to the caller to free unlocked. */
	discard->count = 0;
	for (index = 0; index < endpoint->tcp.send_count; index++) {
		slot = (endpoint->tcp.send_first + index) % TCP_SEND_QUEUE_MAX;
		discard->packets[discard->count++] =
		    endpoint->tcp.send_queue[slot].packet;
		endpoint->tcp.send_queue[slot].packet = NULL;
	}
	endpoint->tcp.send_first = 0;
	endpoint->tcp.send_count = 0;
	endpoint->tcp.send_unsent = 0;
	endpoint->tcp.retransmit_deadline = 0;
	endpoint->tcp.retransmit_count = 0;
	endpoint->tcp.retransmit_since = 0;
	endpoint->tcp.retransmit_refused = 0;
	endpoint->tcp.retransmit_immediate = 0;
	endpoint->tcp.recovery = 0;
	endpoint->tcp.recovery_by_timeout = 0;
	endpoint->tcp.dupack_guard = 0;
	endpoint->tcp.duplicate_acks = 0;

	/* No segment is left to time; the estimate and its backoff stay for the connection. */
	endpoint->tcp.rtt_timing = 0;

	/* Nothing is owed the device any more. */
	endpoint->tcp.ack_pending = 0;
	tcp_output_retry_settle_locked(endpoint);
}

/* Frees the payloads a reset handed over, outside any lock. */
static void
tcp_discard_free(
	struct tcp_discard *discard)
{
	unsigned index;

	/* Process each remaining element. */
	for (index = 0; index < discard->count; index++)
		packet_buf_free(discard->packets[index]);
	discard->count = 0;
}

/*
 * Tells whether one sequence number comes before another.
 *
 * Sequence numbers are compared modulo 2^32 (RFC 9293 section 3.4): left
 * comes first when it lies in the half of the space before right, which
 * stays true where the numbers wrap from 2^32 - 1 to 0.
 */
static int
tcp_sequence_before(
	uint32_t left,
	uint32_t right)
{
	int32_t distance;

	/* The signed distance from right to left: negative when left comes first. */
	distance = (int32_t)(left - right);

	/* left lies in the half of the space before right. */
	if (distance < 0)
		return 1;

	/* Succeeded: left is right itself or comes after it. */
	return 0;
}

/* Tells whether one sequence number is another or comes before it, modulo 2^32. */
static int
tcp_sequence_at_or_before(
	uint32_t left,
	uint32_t right)
{
	int before;

	/* The same number. */
	if (left == right)
		return 1;

	/* An earlier one. */
	before = tcp_sequence_before(left, right);
	if (before)
		return 1;

	/* Succeeded: left comes after right. */
	return 0;
}

/*
 * Gives the initial sequence number of a new connection.
 *
 * chosen is the number the caller picked; a build with
 * CONFIG_TCP_ISN_WRAP_BEFORE puts the first data byte that many bytes
 * before the wrap instead, the SYN taking the number before it.
 */
static uint32_t
tcp_initial_sequence(
	uint32_t chosen)
{
	uint32_t wrap_before;

	/* The ordinary build keeps the number the caller picked. */
	wrap_before = (uint32_t)CONFIG_TCP_ISN_WRAP_BEFORE;
	if (wrap_before == 0U)
		return chosen;

	/* Succeeded: the SYN's number, one before the first data byte. */
	return 0U - wrap_before - 1U;
}

/*
 * Gives the retransmission timeout after a number of resends.
 *
 * The connection's timeout (rto, in ticks; see tcp_rto_locked) doubles for
 * each resend (RFC 6298 section 5.5) and stops at
 * TCP_RETRANSMIT_TIMEOUT_MAX.
 */
static uint64_t
tcp_backoff_timeout(
	uint64_t rto,
	unsigned count)
{
	uint64_t timeout;
	unsigned index;

	/* Doubles the connection's timeout once per resend, until it reaches the longest. */
	timeout = rto;
	for (index = 0; index < count; index++) {
		timeout *= 2U;
		if (timeout >= TCP_RETRANSMIT_TIMEOUT_MAX)
			break;
	}

	/* A doubling past the longest timeout is held at it. */
	if (timeout > TCP_RETRANSMIT_TIMEOUT_MAX)
		timeout = TCP_RETRANSMIT_TIMEOUT_MAX;

	/* Succeeded: the timeout the next deadline waits. */
	return timeout;
}

/*
 * Gives a connection's retransmission timeout before any backoff, in ticks,
 * under the socket lock: the one its round-trip estimate gives, or the
 * initial second before the first sample (RFC 6298 section 2.1).
 */
static uint64_t
tcp_rto_locked(
	const struct tcp_endpoint *endpoint)
{
	/* No sample yet. */
	if (endpoint->tcp.rto == 0)
		return TCP_INITIAL_RTO;

	/* Succeeded: the estimate's timeout. */
	return endpoint->tcp.rto;
}

/*
 * Takes the round-trip sample of the segment being timed, which an
 * acknowledgement has just covered, under the socket lock (RFC 6298
 * section 2, ws220-p004).
 *
 * The sample is the ticks since the segment was handed to the device, in
 * microseconds (0 when it came back within the tick).  The first sets
 * SRTT to it and RTTVAR to half of it; each later one moves RTTVAR a
 * quarter and SRTT an eighth of the way.  The timeout is then SRTT +
 * max(G, 4 RTTVAR, TCP_RTO_MIN_TERM_US), G being one tick, rounded up to a
 * tick and at most TCP_RETRANSMIT_TIMEOUT_MAX.
 */
static void
tcp_rtt_sample_locked(
	struct tcp_endpoint *endpoint,
	uint64_t now)
{
	uint64_t ticks;
	uint64_t sample;
	uint64_t variation;
	uint64_t difference;
	uint64_t term;
	uint64_t granularity;
	uint64_t timeout_us;
	uint64_t timeout;

	/*
	 * The segment is timed no longer; the next one sent is.  A valid
	 * sample ends the backoff (RFC 6298 section 5 note).
	 */
	endpoint->tcp.rtt_timing = 0;
	endpoint->tcp.rto_backoff = 0;

	/* The sample in microseconds, bounded so the sums below stay small. */
	ticks = now - endpoint->tcp.rtt_start;
	sample = ticks * 1000000U / KERN_CLOCK_HZ;
	if (sample > TCP_RTT_SAMPLE_MAX_US)
		sample = TCP_RTT_SAMPLE_MAX_US;

	/* The first sample (RFC 6298 section 2.2), or a later one (section 2.3). */
	if (endpoint->tcp.rtt_samples == 0) {
		endpoint->tcp.srtt_us = (uint32_t)sample;
		endpoint->tcp.rttvar_us = (uint32_t)(sample / 2U);
	} else {
		/* |SRTT - R|, taken before SRTT moves. */
		if (endpoint->tcp.srtt_us > sample) {
			difference = endpoint->tcp.srtt_us - sample;
		} else {
			difference = sample - endpoint->tcp.srtt_us;
		}

		/* RTTVAR a quarter of the way to the difference, then SRTT an eighth of the way to the sample. */
		variation = ((uint64_t)endpoint->tcp.rttvar_us * 3U + difference) / 4U;
		endpoint->tcp.rttvar_us = (uint32_t)variation;
		endpoint->tcp.srtt_us = (uint32_t)(((uint64_t)endpoint->tcp.srtt_us * 7U + sample) / 8U);
	}

	/* One more sample, the count held at its largest. */
	if (endpoint->tcp.rtt_samples != UINT32_MAX)
		endpoint->tcp.rtt_samples++;

	/* The term added to SRTT: the largest of the clock's tick, 4 RTTVAR and the least term. */
	granularity = 1000000U / KERN_CLOCK_HZ;
	term = (uint64_t)endpoint->tcp.rttvar_us * 4U;
	if (term < granularity)
		term = granularity;
	if (term < TCP_RTO_MIN_TERM_US)
		term = TCP_RTO_MIN_TERM_US;
	timeout_us = endpoint->tcp.srtt_us + term;

	/*
	 * In ticks, rounded up, and one tick more: a deadline counted from
	 * the tick a segment went in can expire up to a tick before the
	 * timeout has really passed (ws220-p004 review-1 M4, a peer whose
	 * delayed acknowledgement comes just at the least term).  No longer
	 * than the longest timeout.
	 */
	timeout = (timeout_us * KERN_CLOCK_HZ + 999999U) / 1000000U + 1U;
	if (timeout > TCP_RETRANSMIT_TIMEOUT_MAX)
		timeout = TCP_RETRANSMIT_TIMEOUT_MAX;
	endpoint->tcp.rto = timeout;
}

/*
 * Forgets a connection's round-trip estimate, under the socket lock: a new
 * connect measures its own, starting from the initial second.
 */
static void
tcp_rtt_forget_locked(
	struct tcp_endpoint *endpoint)
{
	/* No sample, no timeout of its own, no backoff, nothing timed. */
	endpoint->tcp.rtt_samples = 0;
	endpoint->tcp.srtt_us = 0;
	endpoint->tcp.rttvar_us = 0;
	endpoint->tcp.rto = 0;
	endpoint->tcp.rto_backoff = 0;
	endpoint->tcp.rtt_timing = 0;
	endpoint->tcp.syn_timed_out = 0;
	endpoint->tcp.recovery = 0;
	endpoint->tcp.recovery_by_timeout = 0;
	endpoint->tcp.dupack_guard = 0;
	endpoint->tcp.duplicate_acks = 0;
}

/*
 * Takes what the handshake measured, under the socket lock, as an
 * acknowledgement of this end's SYN (or SYN-ACK) completes it, before the
 * ring is emptied (ws220-p004).
 *
 * The SYN timed and never resent gives the first sample.  One whose timer
 * expired, or whose SYN-ACK was sent again for a repeated SYN, gives none
 * (Karn's rule), and the data then starts with a timeout of three seconds
 * rather than the initial one (RFC 6298 section 5.7).  Either way the
 * backoff of the handshake ends: the data has its own timeout.  The caller
 * calls this before tcp_retransmit_reset() empties the ring.
 */
static void
tcp_rtt_handshake_locked(
	struct tcp_endpoint *endpoint,
	uint32_t acknowledgement,
	uint64_t now)
{
	int covered;

	/* The SYN timed, never resent, and acknowledged now. */
	covered = 0;
	if (endpoint->tcp.rtt_timing != 0 && endpoint->tcp.syn_timed_out == 0)
		covered = tcp_sequence_at_or_before(endpoint->tcp.rtt_sequence, acknowledgement);
	if (covered)
		tcp_rtt_sample_locked(endpoint, now);

	/* A SYN resent, without a sample. */
	if (endpoint->tcp.syn_timed_out != 0 && endpoint->tcp.rtt_samples == 0)
		endpoint->tcp.rto = TCP_SYN_RETRIED_RTO;

	/* The data starts without the handshake's backoff. */
	endpoint->tcp.rto_backoff = 0;
	endpoint->tcp.syn_timed_out = 0;
	endpoint->tcp.rtt_timing = 0;
}

/*
 * Tells whether the timer has resent the oldest segment for long enough to
 * give the attempt up, under the socket lock.
 *
 * The first timeout since the peer last answered starts the count; R2 runs
 * from it, three minutes while the connection is still being set up (a
 * SYN is what is resent) and 100 seconds after (RFC 1122 section 4.2.3.5).
 */
static int
tcp_retransmit_given_up_locked(
	struct tcp_endpoint *endpoint,
	uint64_t now)
{
	uint64_t limit;

	/* The first timeout since the peer answered starts the time R2 counts. */
	if (endpoint->tcp.retransmit_since == 0) {
		endpoint->tcp.retransmit_since = now;
		return 0;
	}

	/* A connection being set up resends its SYN for longer than one that carries data. */
	if (endpoint->tcp.state == TCP_SYN_SENT ||
	    endpoint->tcp.state == TCP_SYN_RECEIVED) {
		limit = KERN_MS_TO_TICKS(TCP_GIVE_UP_SYN_MS);
	} else {
		limit = KERN_MS_TO_TICKS(TCP_GIVE_UP_DATA_MS);
	}

	/* R2 has not passed yet: the segment is resent again. */
	if (now - endpoint->tcp.retransmit_since < limit)
		return 0;

	/* Succeeded: R2 has passed without an answer, so the attempt is given up. */
	return 1;
}

/*
 * Tells how a reset of a connection past its SYN is taken, under the socket
 * lock (RFC 9293 section 3.10.7.4, RFC 5961 section 3.2).
 *
 * A reset whose sequence number is the next one expected resets the
 * connection.  One elsewhere within the window this end advertised is
 * answered by an acknowledgement, which a peer that really reset answers
 * with a reset of the right number; a blind reset from someone who does
 * not know the number ends nothing.  Any other is ignored.
 */
static enum tcp_reset_check
tcp_reset_check_locked(
	const struct tcp_endpoint *endpoint,
	uint32_t sequence)
{
	uint32_t offset;

	/* The next sequence number expected resets the connection. */
	if (sequence == endpoint->tcp.receive_next)
		return TCP_RESET_ACCEPT;

	/* Elsewhere in the window this end last advertised: the peer is asked again. */
	offset = sequence - endpoint->tcp.receive_next;
	if (offset < (uint32_t)endpoint->tcp.advertised_window)
		return TCP_RESET_CHALLENGE;

	/* Succeeded: outside the window, the reset is not this connection's. */
	return TCP_RESET_IGNORE;
}

/*
 * Tells whether an acceptable acknowledgement may set the peer's window,
 * under the socket lock (RFC 9293 section 3.10.7.4).
 *
 * It may when its segment is newer than the one the window came from:
 * a later sequence number (SND.WL1), or the same one and an
 * acknowledgement at least as far (SND.WL2).  The caller has checked that
 * the acknowledgement lies within what was sent.
 */
static int
tcp_send_window_update_allowed_locked(
	const struct tcp_endpoint *endpoint,
	uint32_t sequence,
	uint32_t acknowledgement)
{
	int newer;

	/* A segment from later in the peer's sequence space. */
	newer = tcp_sequence_before(endpoint->tcp.send_window_sequence, sequence);
	if (newer)
		return 1;

	/* An older segment never sets the window. */
	if (sequence != endpoint->tcp.send_window_sequence)
		return 0;

	/* The same segment number with an acknowledgement as far or farther. */
	newer = tcp_sequence_at_or_before(endpoint->tcp.send_window_acknowledgement, acknowledgement);
	if (newer)
		return 1;

	/* Succeeded: the window came from a newer acknowledgement, which stands. */
	return 0;
}

/*
 * Tells whether the oldest segment sent went out as a probe of a window
 * that could not take it and now fits the window the peer advertises,
 * under the socket lock (ws220-p005).
 */
static int
tcp_probe_fits_locked(
	const struct tcp_endpoint *endpoint)
{
	const struct tcp_pending *oldest;
	uint32_t end;

	/* No probe: an ordinary segment, which a reopened window says nothing of. */
	oldest = &endpoint->tcp.send_queue[endpoint->tcp.send_first];
	if (oldest->probe == 0)
		return 0;

	/* The window does not reach its end yet: the timer probes again. */
	end = oldest->sequence + oldest->advance - endpoint->tcp.send_unacknowledged;
	if (end > (uint32_t)endpoint->tcp.peer_window)
		return 0;

	/* Succeeded: the probe fits now. */
	return 1;
}

/*
 * Tells whether the oldest segment sent fits the window the peer
 * advertises, under the socket lock (ws220-p005 review-1 G1): while it does
 * not, the peer's acknowledgements answer a closed window.
 */
static int
tcp_oldest_fits_window_locked(
	const struct tcp_endpoint *endpoint)
{
	const struct tcp_pending *oldest;
	uint32_t end;

	/* The oldest segment's end, from SND.UNA. */
	oldest = &endpoint->tcp.send_queue[endpoint->tcp.send_first];
	end = oldest->sequence + oldest->advance - endpoint->tcp.send_unacknowledged;

	/* Beyond the window. */
	if (end > (uint32_t)endpoint->tcp.peer_window)
		return 0;

	/* Succeeded: it fits. */
	return 1;
}

/* Reports the sequence space the outstanding segments occupy. */
static uint32_t
tcp_in_flight(
	const struct tcp_endpoint *endpoint)
{
	/* Returns the computed result. */
	return endpoint->tcp.send_next - endpoint->tcp.send_unacknowledged;
}

/*
 * Retires every segment the acknowledgement covers.
 *
 * The caller holds the socket lock and frees the returned payloads once it
 * has released it.  The deadline restarts for whatever is still outstanding.
 */
static void
tcp_retire_acknowledged(
	struct tcp_endpoint *endpoint,
	uint32_t acknowledgement,
	struct tcp_discard *discard)
{
	struct tcp_pending *entry;
	uint32_t end;
	unsigned sent;
	uint64_t now;
	uint64_t rto;
	uint64_t timeout;
	int progressed = 0;
	int recovering;
	int probe_answered = 0;
	int beyond;
	int covered;
	int before;

	/*
	 * Takes the covered entries off the front of the ring.  Only an entry
	 * handed to the device can be covered (an acknowledgement past what was
	 * sent is refused before this), so the unsent ones are never taken.
	 */
	for (;;) {
		/* Stops when no entry sent is left. */
		sent = tcp_sent_count_locked(endpoint);
		if (sent == 0)
			break;

		/* The oldest entry, and the sequence number after it. */
		entry = &endpoint->tcp.send_queue[endpoint->tcp.send_first];
		end = entry->sequence + entry->advance;

		/*
		 * Stops at the first segment the peer has not taken, compared
		 * modulo 2^32 so that it stays correct where the numbers wrap.
		 */
		before = tcp_sequence_before(acknowledgement, end);
		if (before)
			break;

		/* A probe answered ends the backoff of the closed window (review-1 M3). */
		if (entry->probe != 0)
			probe_answered = 1;

		/* Hands its copy to the caller to free, and takes it off the ring. */
		discard->packets[discard->count++] = entry->packet;
		entry->packet = NULL;
		endpoint->tcp.send_first =
		    (endpoint->tcp.send_first + 1U) % TCP_SEND_QUEUE_MAX;
		endpoint->tcp.send_count--;
		progressed = 1;
	}

	/*
	 * An acknowledgement that takes nothing (a duplicate, which a peer
	 * sends for each segment after a gap, or the answer of a peer whose
	 * window is closed) leaves the timer as it is (RFC 6298 section 5.3):
	 * restarting it would put the resend of the lost segment off for as
	 * long as duplicates come.  The peer answered, so the attempts start
	 * over: a connection is not given up while its peer answers.  The
	 * backoff stays (ws220-p004 review-1 G2): the resends to a closed
	 * window keep doubling apart, and only a sample ends the backoff.
	 */
	if (!progressed) {
		endpoint->tcp.retransmit_count = 0;
		endpoint->tcp.retransmit_since = 0;
		endpoint->tcp.retransmit_refused = 0;
		return;
	}

	/*
	 * The timer follows whatever is now the oldest segment sent; with
	 * none, an unsent one starts it when it goes (ws220-p003).  In a
	 * recovery (RFC 6582 section 3.2), an acknowledgement short of recover
	 * is a partial one: the next hole is resent at once rather than after
	 * another timeout; one that reaches recover ends the recovery.  The
	 * duplicates in a row start over (ws220-p005).
	 */
	recovering = 0;
	if (endpoint->tcp.recovery != 0)
		recovering = tcp_sequence_before(acknowledgement, endpoint->tcp.recover);

	/*
	 * A recovery a timeout started that ends exactly at recover holds back
	 * the duplicates of recover (review-1 M1); one beyond it lets them go.
	 */
	if (endpoint->tcp.dupack_guard != 0) {
		beyond = tcp_sequence_before(endpoint->tcp.dupack_guard_sequence, acknowledgement);
		if (beyond)
			endpoint->tcp.dupack_guard = 0;
	}

	/* The recovery ends here; one a timeout started, exactly at recover, guards its duplicates. */
	if (endpoint->tcp.recovery != 0 && !recovering) {
		if (endpoint->tcp.recovery_by_timeout != 0 && acknowledgement == endpoint->tcp.recover) {
			endpoint->tcp.dupack_guard = 1;
			endpoint->tcp.dupack_guard_sequence = acknowledgement;
		}

		/* Out of the recovery: recover means nothing until the next one (review-1 G2). */
		endpoint->tcp.recovery = 0;
		endpoint->tcp.recovery_by_timeout = 0;
	}

	/* The backoff of a closed window ends when its probe is answered. */
	if (probe_answered)
		endpoint->tcp.rto_backoff = 0;
	endpoint->tcp.duplicate_acks = 0;
	endpoint->tcp.retransmit_count = 0;
	endpoint->tcp.retransmit_since = 0;
	endpoint->tcp.retransmit_refused = 0;
	sent = tcp_sent_count_locked(endpoint);
	now = sched_ticks();

	/* An acknowledgement that covers the segment being timed gives a round-trip sample (ws220-p004). */
	covered = 0;
	if (endpoint->tcp.rtt_timing != 0)
		covered = tcp_sequence_at_or_before(endpoint->tcp.rtt_sequence, acknowledgement);
	if (covered)
		tcp_rtt_sample_locked(endpoint, now);
	rto = tcp_rto_locked(endpoint);

	/*
	 * No timer without a segment in flight, at once when recovering (an
	 * immediate resend, which does not double the timeout), else the
	 * timeout from now, backed off until a valid sample (Karn).  The
	 * sample above comes first, so the timeout is the new one.
	 */
	timeout = tcp_backoff_timeout(rto, endpoint->tcp.rto_backoff);
	endpoint->tcp.retransmit_immediate = 0;
	if (sent == 0) {
		endpoint->tcp.retransmit_deadline = 0;
	} else if (recovering) {
		endpoint->tcp.retransmit_deadline = now;
		endpoint->tcp.retransmit_immediate = TCP_IMMEDIATE_LOSS;
		endpoint->tcp.counters.immediate_resends++;
	} else {
		endpoint->tcp.retransmit_deadline = now + timeout;
	}
}

/* Drops the retransmission record and wakes senders. */
static void
tcp_retransmit_clear(
	struct tcp_endpoint *endpoint)
{
	struct socket *socket;
	struct tcp_discard packet;
	unsigned long irq;

	/* Takes the queued segment out under the socket lock. */
	socket = &endpoint->tcp.inet.socket;
	irq = spin_lock_irqsave(&socket->lock);

	tcp_retransmit_reset(endpoint, &packet);

	spin_unlock_irqrestore(&socket->lock, irq);

	/* Frees them and lets a blocked sender continue. */
	tcp_discard_free(&packet);
	socket_wake_send(socket);
}

/* Abandons an active connect of a given generation; the caller holds the socket lock. */
static void
tcp_connect_cancel_locked(
	struct tcp_endpoint *endpoint,
	uint32_t generation,
	struct tcp_discard *discard)
{
	struct socket *socket;

	socket = &endpoint->tcp.inet.socket;
	discard->count = 0;

	/*
	 * The generation check prevents a waiter from cancelling a later
	 * attempt which reused the same socket.
	 */
	if (endpoint->tcp.state != TCP_SYN_SENT ||
	    endpoint->tcp.active_connect_generation != generation)
		return;

	/* Closes the socket and wakes everyone waiting on the attempt. */
	tcp_retransmit_reset(endpoint, discard);
	endpoint->tcp.state = TCP_CLOSED;
	endpoint->tcp.active_connect_generation = 0;
	endpoint->tcp.connect_wait_deadline = 0;
	tcp_forget_peer(endpoint);
	waitq_wake_all(&socket->connect_waitq);
	waitq_wake_all(&socket->send_waitq);
	poll_notify();
}

/*
 * Queues a segment that is retransmitted until acknowledged, and sends it.
 *
 * The segment joins the ring unsent.  When nothing queued before it is
 * still unsent and no other thread is sending, this thread sends it at
 * once from the caller's data; otherwise it waits its turn, sent by the
 * thread sending the ones before it or by the output retry already set for
 * them (ws220-p003).  One the device or the pool refuses stays in the ring
 * for the output retry, and the call succeeds: the ring holds the data.
 * One lost for an unresolved neighbour stays sent, as one lost on the
 * wire, for the retransmission timer.  Only a segment that failed for good
 * (no route) while still the last in the ring is taken back, and its error
 * reported; one with others queued after it stays, as a segment lost on
 * the wire, since taking it back would make its writer send its bytes
 * twice.
 */
static int
tcp_send_reliable(
	struct tcp_endpoint *endpoint,
	uint8_t flags,
	const void *data,
	size_t length)
{
	struct packet_buf *copy;
	void *payload;
	uint32_t sequence;
	uint32_t advance;
	uint32_t window;
	uint64_t now;
	unsigned long irq;
	unsigned slot;
	unsigned sent;
	enum tcp_refusal refusal;
	int control;
	int owner;
	int retries;
	int error;
	struct tcp_discard discard;

	/* Keeps a copy of the payload for retransmission. */
	copy = packet_buf_alloc(0);
	if (copy == NULL)
		return ENOBUFS;
	payload = packet_buf_append(copy, length);
	if (payload == NULL) {
		packet_buf_free(copy);
		return ENOBUFS;
	}

	if (length != 0)
		kern_memcpy(payload, data, length);

	/* SYN and FIN each consume one sequence number. */
	advance = (uint32_t)length;
	if ((flags & (TCP_SYN | TCP_FIN)) != 0)
		advance++;

	/*
	 * The segment joins the queued ones if both the ring and the peer's
	 * advertised window have room for it; the unsent ones count, being
	 * promised to the peer already.  A control segment is always
	 * admitted: refusing a SYN or a FIN for want of window would stall
	 * the connection itself rather than the data on it.
	 */
	irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);

	control = (flags & (TCP_SYN | TCP_FIN)) != 0;
	if (endpoint->tcp.send_count >= TCP_SEND_QUEUE_MAX) {
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, irq);
		packet_buf_free(copy);
		return EAGAIN;
	}
	if (!control && length != 0) {
		window = endpoint->tcp.peer_window;

		/* An unknown or closed window still allows one segment. */
		if (window == 0)
			window = (uint32_t)TCP_MSS;
		if (tcp_in_flight(endpoint) != 0 &&
		    tcp_in_flight(endpoint) + (uint32_t)length > window) {
			spin_unlock_irqrestore(
			    &endpoint->tcp.inet.socket.lock, irq);
			packet_buf_free(copy);
			return EAGAIN;
		}
	}

	/* The entry, at the end of the ring and unsent. */
	sequence = endpoint->tcp.send_next;
	slot = (endpoint->tcp.send_first + endpoint->tcp.send_count) %
	    TCP_SEND_QUEUE_MAX;
	endpoint->tcp.send_queue[slot].packet = copy;
	endpoint->tcp.send_queue[slot].sequence = sequence;
	endpoint->tcp.send_queue[slot].advance = advance;
	endpoint->tcp.send_queue[slot].length = (uint16_t)length;
	endpoint->tcp.send_queue[slot].flags = flags;
	endpoint->tcp.send_count++;
	endpoint->tcp.send_unsent++;
	endpoint->tcp.send_next += advance;

	/* Data queued for its first transmission is counted for net.tcpstats. */
	if (length != 0) {
		endpoint->tcp.counters.data_segments_out++;
		endpoint->tcp.counters.data_bytes_out += length;
	}

	/*
	 * This thread sends the entry itself when it is the only unsent one
	 * and no other thread is sending: then it keeps its place in the
	 * stream.  send_flushing makes this thread the only one to move the
	 * end of what was sent until it gives it up below.
	 */
	now = sched_ticks();
	owner = 0;
	if (endpoint->tcp.send_unsent == 1U && endpoint->tcp.send_flushing == 0) {
		endpoint->tcp.send_flushing = 1;
		tcp_mark_sent_locked(endpoint, now);
		owner = 1;
	}

	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, irq);

	/* Succeeded: the entry waits behind the unsent ones, for whoever sends them. */
	if (!owner)
		return 0;

	/*
	 * Publish the retransmission record before output.  A fast ACK may
	 * then safely retire it, and the timer can never observe an unowned
	 * pointer.
	 */
	error = tcp_send_segment_at(endpoint, sequence, flags, data, length);
	refusal = tcp_refusal_of(error);
	retries = tcp_refusal_retries(refusal);

	/* Settles what became of the entry, and gives the sending up. */
	discard.count = 0;
	irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);

	if (retries) {
		/*
		 * A segment the device or the pool had no room for is unsent
		 * again, ahead of any queued after it, and the output retry
		 * sends it (a SYN or a FIN too, which no one else would try
		 * again).  net.tcpstats counts the data ones.
		 */
		tcp_unmark_sent_locked(endpoint, copy);
		if (length != 0)
			endpoint->tcp.counters.send_refused++;
		tcp_output_refused_locked(endpoint, refusal, 1, now);
		error = 0;
	} else if (refusal == TCP_REFUSAL_LOST) {
		/* Lost for an unresolved neighbour: the timer resends it, and R2 ends a peer that never answers. */
		error = 0;
	} else if (refusal == TCP_REFUSAL_HARD) {
		/*
		 * A segment that failed for good is taken back while it is the
		 * last in the ring (a connect's SYN without a route reports the
		 * error); with others queued after it, it stays.
		 */
		slot = (endpoint->tcp.send_first + endpoint->tcp.send_count -
		    1U) % TCP_SEND_QUEUE_MAX;
		if (endpoint->tcp.send_unsent == 0 &&
		    endpoint->tcp.send_count != 0 &&
		    endpoint->tcp.send_queue[slot].packet == copy) {
			discard.packets[discard.count++] = copy;
			endpoint->tcp.send_queue[slot].packet = NULL;
			endpoint->tcp.send_count--;
			endpoint->tcp.send_next = sequence;

			/* A data segment taken back was not queued for net.tcpstats after all: its writer sends the bytes again. */
			if (length != 0) {
				endpoint->tcp.counters.data_segments_out--;
				endpoint->tcp.counters.data_bytes_out -= length;
			}

			/* A segment taken back is not timed (ws220-p004 review-1 M1); the next one sent is. */
			if (endpoint->tcp.rtt_timing != 0 && endpoint->tcp.rtt_sequence == sequence + advance)
				endpoint->tcp.rtt_timing = 0;

			/* Nothing in flight is timed. */
			sent = tcp_sent_count_locked(endpoint);
			if (sent == 0) {
				endpoint->tcp.retransmit_deadline = 0;
				endpoint->tcp.retransmit_count = 0;
				endpoint->tcp.retransmit_since = 0;
				endpoint->tcp.retransmit_refused = 0;
				endpoint->tcp.retransmit_immediate = 0;
			}
		} else {
			error = 0;
		}
	}

	/*
	 * Entries other threads queued while this one was sending are left to
	 * the network worker's next pass, which has a buffer to copy their
	 * payloads into.
	 */
	if (!retries && endpoint->tcp.send_unsent != 0) {
		endpoint->tcp.output_retry_on_pass = 1;
		endpoint->tcp.output_retry_deadline = now;
	}

	/* send_flushing given up: the next sender is whoever finds the first unsent entry. */
	endpoint->tcp.send_flushing = 0;
	tcp_output_retry_settle_locked(endpoint);

	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, irq);

	/* Frees the copy taken back, outside the lock. */
	tcp_discard_free(&discard);

	/*
	 * The worker learns of the new deadlines: the retransmission's, or the
	 * output retry's, which it did not count while this thread was sending.
	 */
	net_worker_wakeup();

	/* Reports a segment taken back for an error that will not pass. */
	if (error != 0)
		return error;

	/* Succeeded: the segment is on its way, or waits in the ring. */
	return 0;
}

/*
 * Tells how a segment the stack could not hand to the device is sent
 * again, from the error its output reported (ws220-p003).
 */
static enum tcp_refusal
tcp_refusal_of(
	int error)
{
	/* The device took it. */
	if (error == 0)
		return TCP_REFUSAL_NONE;

	/* A full device queue, which the device's transmit completion empties (ECM, NCM, GENET). */
	if (error == ENOBUFS || error == EBUSY)
		return TCP_REFUSAL_DEVICE;

	/* An empty packet pool, which tells no one when it has packets again. */
	if (error == ENOMEM)
		return TCP_REFUSAL_WAIT;

	/* A neighbour still being resolved: the segment is lost, as on the wire. */
	if (error == EAGAIN)
		return TCP_REFUSAL_LOST;

	/* Any other error does not pass by waiting. */
	return TCP_REFUSAL_HARD;
}

/* Tells whether a segment so refused stays unsent for the output retry. */
static int
tcp_refusal_retries(
	enum tcp_refusal refusal)
{
	/* A full device. */
	if (refusal == TCP_REFUSAL_DEVICE)
		return 1;

	/* An empty pool. */
	if (refusal == TCP_REFUSAL_WAIT)
		return 1;

	/* Succeeded: sent, lost as on the wire, or failed for good, it is the timer's. */
	return 0;
}

/* Gives the ticks after which a refused output is tried again at the latest. */
static uint64_t
tcp_refusal_delay(
	enum tcp_refusal refusal)
{
	uint64_t delay;

	/*
	 * A full device runs the network worker when it has room again, so
	 * its delay only covers a device that does not; the pool tells no
	 * one, so the retry waits rather than spinning.
	 */
	if (refusal == TCP_REFUSAL_DEVICE) {
		delay = kern_ms_to_ticks(TCP_OUTPUT_RETRY_DEVICE_MS);
	} else {
		delay = kern_ms_to_ticks(TCP_SEND_RETRY_MS);
	}

	/* At least one tick, so the retry comes after the pass that set it. */
	if (delay == 0)
		delay = 1;

	/* Succeeded: the delay. */
	return delay;
}

/* Counts the ring's entries handed to the device, under the socket lock. */
static unsigned
tcp_sent_count_locked(
	const struct tcp_endpoint *endpoint)
{
	/* The unsent entries are the last ones of the ring. */
	return endpoint->tcp.send_count - endpoint->tcp.send_unsent;
}

/*
 * Gives the end of what was handed to the device (RFC 9293's SND.NXT),
 * under the socket lock: the first unsent entry's sequence number, or the
 * end of what was queued when every entry was sent.
 */
static uint32_t
tcp_sent_end_locked(
	const struct tcp_endpoint *endpoint)
{
	unsigned slot;

	/* Everything queued was sent. */
	if (endpoint->tcp.send_unsent == 0)
		return endpoint->tcp.send_next;

	/* The first unsent entry, after the ones sent. */
	slot = (endpoint->tcp.send_first + endpoint->tcp.send_count -
	    endpoint->tcp.send_unsent) % TCP_SEND_QUEUE_MAX;

	/* Succeeded: what was sent ends where it starts. */
	return endpoint->tcp.send_queue[slot].sequence;
}

/*
 * Marks the first unsent entry as handed to the device, under the socket
 * lock; only the thread that set send_flushing calls it.
 *
 * An entry sent with none in flight before it is the oldest one in flight,
 * so it starts the retransmission timer: an entry that has not left has no
 * timeout to wait, and its first sending is not counted as a resend.
 */
static void
tcp_mark_sent_locked(
	struct tcp_endpoint *endpoint,
	uint64_t now)
{
	struct tcp_pending *entry;
	unsigned sent;
	unsigned slot;
	uint64_t rto;
	uint64_t timeout;

	/* What was in flight before it, and the entry itself, the first unsent one. */
	sent = tcp_sent_count_locked(endpoint);
	slot = (endpoint->tcp.send_first + sent) % TCP_SEND_QUEUE_MAX;
	entry = &endpoint->tcp.send_queue[slot];

	/* It is no longer owed to the device: the end of what was sent moves past it. */
	endpoint->tcp.send_unsent--;

	/* The oldest segment in flight starts the timer afresh, with the connection's timeout, still backed off until a sample. */
	if (sent == 0) {
		rto = tcp_rto_locked(endpoint);
		timeout = tcp_backoff_timeout(rto, endpoint->tcp.rto_backoff);
		endpoint->tcp.retransmit_count = 0;
		endpoint->tcp.retransmit_since = 0;
		endpoint->tcp.retransmit_refused = 0;
		endpoint->tcp.retransmit_immediate = 0;
		endpoint->tcp.retransmit_deadline = now + timeout;
	}

	/*
	 * A segment that goes beyond the window the peer last advertised (a
	 * closed or too small window lets one segment go) is a probe of that
	 * window (ws220-p005): the peer refuses it unless the window has
	 * opened, and it is sent again at once when it does.
	 */
	entry->probe = 0;
	if (entry->length != 0 &&
	    entry->sequence + entry->advance - endpoint->tcp.send_unacknowledged > (uint32_t)endpoint->tcp.peer_window)
		entry->probe = 1;

	/* With no segment being timed, this one is: one sample a round trip (ws220-p004). */
	if (endpoint->tcp.rtt_timing == 0) {
		endpoint->tcp.rtt_timing = 1;
		endpoint->tcp.rtt_sequence = entry->sequence + entry->advance;
		endpoint->tcp.rtt_start = now;
	}
}

/*
 * Takes back the mark of an entry the device or the pool refused, under the
 * socket lock: it is unsent again, first of the unsent entries.
 *
 * packet is the entry's copy, which tells it from another entry put in the
 * same place.  Only the last entry sent can be the caller's, since only the
 * thread that set send_flushing marks entries; when an acknowledgement or a
 * reset has taken it out of the ring meanwhile, nothing changes.
 */
static void
tcp_unmark_sent_locked(
	struct tcp_endpoint *endpoint,
	const struct packet_buf *packet)
{
	uint32_t sent_end;
	int inside;
	uint32_t end;
	unsigned sent;
	unsigned slot;

	/* No entry sent is left: a reset emptied the ring, or an acknowledgement took the entry. */
	sent = tcp_sent_count_locked(endpoint);
	if (sent == 0)
		return;

	/* The last entry sent, which another packet shows is not the caller's. */
	slot = (endpoint->tcp.send_first + sent - 1U) % TCP_SEND_QUEUE_MAX;
	if (endpoint->tcp.send_queue[slot].packet != packet)
		return;

	/* It is owed to the device again, ahead of every entry queued after it. */
	endpoint->tcp.send_unsent++;

	/*
	 * A recovery reaches no further than what was sent (review-1 L3): a
	 * segment first sent after this is no hole a partial acknowledgement
	 * should resend.
	 */
	sent_end = tcp_sent_end_locked(endpoint);
	if (endpoint->tcp.recovery != 0) {
		inside = tcp_sequence_before(sent_end, endpoint->tcp.recover);
		if (inside)
			endpoint->tcp.recover = sent_end;
	}

	/* A segment that did not go is not timed; the next one sent is. */
	end = endpoint->tcp.send_queue[slot].sequence + endpoint->tcp.send_queue[slot].advance;
	if (endpoint->tcp.rtt_timing != 0 && endpoint->tcp.rtt_sequence == end)
		endpoint->tcp.rtt_timing = 0;

	/* With nothing left in flight, nothing is timed. */
	if (sent == 1U) {
		endpoint->tcp.retransmit_deadline = 0;
		endpoint->tcp.retransmit_count = 0;
		endpoint->tcp.retransmit_since = 0;
		endpoint->tcp.retransmit_refused = 0;
		endpoint->tcp.retransmit_immediate = 0;
	}
}

/*
 * Sets the output retry after the device or the pool refused a segment,
 * under the socket lock, keeping a sooner retry already set.
 *
 * A full device is tried again on the network worker's next pass, which
 * its transmit completion runs, and after TCP_OUTPUT_RETRY_DEVICE_MS at the
 * latest; when a retry of the worker sent nothing at all (progressed is 0),
 * the latest is TCP_SEND_RETRY_MS instead, so a device that never completes
 * is not tried every tick.  The pool and an unresolved neighbour are tried
 * again after TCP_SEND_RETRY_MS only.
 */
static void
tcp_output_refused_locked(
	struct tcp_endpoint *endpoint,
	enum tcp_refusal refusal,
	int progressed,
	uint64_t now)
{
	uint64_t delay;
	uint64_t deadline;

	/*
	 * A full device: the next pass, and the latest after it.  The pool:
	 * only after the wait.  The retry is one for the socket, so a pass
	 * already asked for and an earlier deadline already set stand
	 * (review-1 L1): the retry serves every refused segment and
	 * acknowledgement at the soonest any of them asked for.
	 */
	if (refusal == TCP_REFUSAL_DEVICE) {
		delay = tcp_refusal_delay(TCP_REFUSAL_DEVICE);
		if (!progressed)
			delay = tcp_refusal_delay(TCP_REFUSAL_WAIT);
		endpoint->tcp.output_retry_on_pass = 1;
	} else {
		delay = tcp_refusal_delay(TCP_REFUSAL_WAIT);
	}

	/* The earlier of the deadline set and this one. */
	deadline = now + delay;
	if (endpoint->tcp.output_retry_deadline == 0 ||
	    deadline < endpoint->tcp.output_retry_deadline)
		endpoint->tcp.output_retry_deadline = deadline;
}

/*
 * Clears the output retry once nothing waits for it, under the socket lock:
 * no unsent entry and no pending acknowledgement.
 */
static void
tcp_output_retry_settle_locked(
	struct tcp_endpoint *endpoint)
{
	/* Unsent entries still wait. */
	if (endpoint->tcp.send_unsent != 0)
		return;

	/* Pending acknowledgements still wait. */
	if (endpoint->tcp.ack_pending != 0)
		return;

	/* Nothing does: the worker has no retry of this socket to wake for. */
	endpoint->tcp.output_retry_on_pass = 0;
	endpoint->tcp.output_retry_deadline = 0;
}

/*
 * Tells whether the output retry of a socket is due now, under the socket
 * lock: something waits for it, no thread is sending the unsent entries
 * already, and the pass or the deadline it waits for has come.
 */
static int
tcp_output_retry_due_locked(
	const struct tcp_endpoint *endpoint,
	uint64_t now)
{
	/* Nothing waits. */
	if (endpoint->tcp.send_unsent == 0 && endpoint->tcp.ack_pending == 0)
		return 0;

	/* A thread is sending the unsent entries; the retry stays for after it. */
	if (endpoint->tcp.send_flushing != 0)
		return 0;

	/* A full device refused, and this is a pass after it. */
	if (endpoint->tcp.output_retry_on_pass != 0)
		return 1;

	/* The deadline has come. */
	if (endpoint->tcp.output_retry_deadline != 0 &&
	    endpoint->tcp.output_retry_deadline <= now)
		return 1;

	/* Succeeded: the retry is not due yet. */
	return 0;
}

/*
 * Sends again what the device or the pool refused, once its retry is due:
 * the pending acknowledgements, then the unsent entries in order.
 *
 * The network worker calls it for each socket on each pass, with its own
 * buffer for the payloads.  It does not wake the worker when the device
 * refuses again: the device's completion or the deadline brings the next
 * try, so a full device is not tried in a loop.
 */
static void
tcp_output_retry(
	struct tcp_endpoint *endpoint,
	uint8_t *payload,
	uint64_t now)
{
	struct socket *socket;
	unsigned long irq;
	int due;
	int owner;

	/* The socket whose lock guards the ring and the retry. */
	socket = &endpoint->tcp.inet.socket;
	owner = 0;

	/*
	 * Takes the retry when it is due, and with unsent entries the sending
	 * of them, under one hold of the lock: a writer queueing meanwhile
	 * leaves its entry to this pass.  A refusal sets the retry again.  A
	 * retry nothing waits for any more is cleared first, so its deadline
	 * cannot keep the worker awake.
	 */
	irq = spin_lock_irqsave(&socket->lock);

	tcp_output_retry_settle_locked(endpoint);
	due = tcp_output_retry_due_locked(endpoint, now);
	if (due) {
		endpoint->tcp.output_retry_on_pass = 0;
		endpoint->tcp.output_retry_deadline = 0;
		if (endpoint->tcp.send_unsent != 0) {
			endpoint->tcp.send_flushing = 1;
			owner = 1;
		}
	}

	spin_unlock_irqrestore(&socket->lock, irq);

	/* The retry is not due. */
	if (!due)
		return;

	/* The acknowledgements first: following a data segment, they would not count as duplicates. */
	tcp_send_ack_pending(endpoint);

	/* Then the unsent entries, oldest first. */
	if (owner)
		tcp_send_unsent(endpoint, payload);
}

/*
 * Hands the unsent entries to the device in order, until none is left or
 * the device or the pool refuses one; the caller has set send_flushing,
 * which this gives up.
 *
 * Each entry is marked sent and its payload copied into payload (TCP_MSS
 * bytes) under the socket lock, then handed to the device with the lock
 * released: a reset may free the ring's copies meanwhile.  Finding none
 * left and giving up send_flushing happen under one hold of the lock, so an
 * entry a writer queues meanwhile is never left without a sender.
 */
static void
tcp_send_unsent(
	struct tcp_endpoint *endpoint,
	uint8_t *payload)
{
	struct socket *socket;
	struct tcp_pending *entry;
	struct packet_buf *packet;
	unsigned long irq;
	unsigned slot;
	uint32_t sequence;
	size_t length;
	uint8_t flags;
	uint64_t now;
	enum tcp_refusal refusal;
	int progressed;
	int retries;
	int error;

	/* The socket whose lock guards the ring and the retry. */
	socket = &endpoint->tcp.inet.socket;
	progressed = 0;

	/* Sends each unsent entry in turn, the lock held between them. */
	irq = spin_lock_irqsave(&socket->lock);

	for (;;) {
		/* Stops when every entry has gone. */
		if (endpoint->tcp.send_unsent == 0)
			break;

		/* The first unsent entry, marked sent and copied out. */
		slot = (endpoint->tcp.send_first + endpoint->tcp.send_count -
		    endpoint->tcp.send_unsent) % TCP_SEND_QUEUE_MAX;
		entry = &endpoint->tcp.send_queue[slot];
		packet = entry->packet;
		sequence = entry->sequence;
		flags = entry->flags;
		length = packet->length;
		now = sched_ticks();
		tcp_mark_sent_locked(endpoint, now);

		/*
		 * A payload larger than a segment is never queued (a write
		 * takes at most TCP_MSS bytes); one that were would be left
		 * marked and unsent, for the timer to abandon its connection.
		 */
		if (length > TCP_MSS) {
			progressed = 1;
			continue;
		}

		/* Its payload, which the ring's copy may not outlive once the lock is released. */
		if (length != 0)
			kern_memcpy(payload, packet->data, length);

		spin_unlock_irqrestore(&socket->lock, irq);

		/* Hands it to the device. */
		error = tcp_send_segment_at(endpoint, sequence, flags, payload, length);
		refusal = tcp_refusal_of(error);

		/*
		 * A refused entry is unsent again, and the retry set for it
		 * and those after it; net.tcpstats counts the data ones.
		 */
		retries = tcp_refusal_retries(refusal);
		irq = spin_lock_irqsave(&socket->lock);
		if (retries) {
			tcp_unmark_sent_locked(endpoint, packet);
			if (length != 0)
				endpoint->tcp.counters.send_refused++;
			tcp_output_refused_locked(endpoint, refusal, progressed, now);
			break;
		}

		/*
		 * One that went, or was lost for an unresolved neighbour, or
		 * failed for good, is the timer's now.  Each of the others lost
		 * the same way asks ARP once more, at most a ring of them; the
		 * timer's backoff spaces the asking after that.
		 */
		progressed = 1;
	}

	/* Gives the sending up with nothing more to do now. */
	endpoint->tcp.send_flushing = 0;
	tcp_output_retry_settle_locked(endpoint);

	spin_unlock_irqrestore(&socket->lock, irq);
}

/*
 * Sends the pure acknowledgements the device or the pool refused before.
 *
 * While receive_next stands they are duplicates, sent as many as were
 * refused (at most TCP_ACK_PENDING_MAX), each of which the peer counts
 * towards its fast retransmit; once it has moved, one acknowledges all of
 * them.  Called by the output retry of the network worker, it leaves a
 * refusal to the next retry rather than waking the worker.
 */
static void
tcp_send_ack_pending(
	struct tcp_endpoint *endpoint)
{
	struct socket *socket;
	unsigned long irq;
	uint32_t sequence;
	uint32_t receive_next;
	unsigned count;
	unsigned sent;
	unsigned total;
	uint64_t now;
	enum tcp_refusal refusal;
	int progressed;
	int retries;
	int error;

	/* The socket whose lock guards the ring and the retry. */
	socket = &endpoint->tcp.inet.socket;

	/* Takes the pending acknowledgements, and the sequence number they carry. */
	irq = spin_lock_irqsave(&socket->lock);

	count = endpoint->tcp.ack_pending;
	receive_next = endpoint->tcp.ack_pending_receive_next;
	if (count != 0 && receive_next != endpoint->tcp.receive_next) {
		count = 1;
		receive_next = endpoint->tcp.receive_next;
	}

	/* They are this call's now, sent at the end of what was sent. */
	endpoint->tcp.ack_pending = 0;
	sequence = tcp_sent_end_locked(endpoint);

	spin_unlock_irqrestore(&socket->lock, irq);

	/* None was pending. */
	if (count == 0)
		return;

	/* Sends them one by one until the device or the pool refuses one; one lost otherwise is lost as on the wire. */
	refusal = TCP_REFUSAL_NONE;
	retries = 0;
	for (sent = 0; sent < count; sent++) {
		error = tcp_send_segment_at(endpoint, sequence, TCP_ACK, NULL, 0);
		refusal = tcp_refusal_of(error);
		retries = tcp_refusal_retries(refusal);
		if (retries)
			break;
	}

	/* Every one went, or is lost for good. */
	if (!retries)
		return;

	/*
	 * The ones not sent are pending again, joined to any refused
	 * meanwhile of the same receive_next; a newer receive_next refused
	 * meanwhile stands for them.
	 */
	now = sched_ticks();
	irq = spin_lock_irqsave(&socket->lock);

	/* A connection closed meanwhile owes the peer nothing (review-1 L4). */
	if (endpoint->tcp.state == TCP_CLOSED) {
		spin_unlock_irqrestore(&socket->lock, irq);
		return;
	}

	/* The ones not sent join the pending ones, or stand for them. */
	if (endpoint->tcp.ack_pending == 0) {
		endpoint->tcp.ack_pending = count - sent;
		endpoint->tcp.ack_pending_receive_next = receive_next;
	} else if (endpoint->tcp.ack_pending_receive_next == receive_next) {
		total = endpoint->tcp.ack_pending + count - sent;
		if (total > TCP_ACK_PENDING_MAX)
			total = TCP_ACK_PENDING_MAX;
		endpoint->tcp.ack_pending = total;
	}

	/* The retry for them, later when this retry sent nothing at all. */
	progressed = 0;
	if (sent != 0)
		progressed = 1;
	tcp_output_refused_locked(endpoint, refusal, progressed, now);

	spin_unlock_irqrestore(&socket->lock, irq);
}

/*
 * Sends a pure acknowledgement of what this end has received, with the
 * window as it now stands.
 *
 * Its sequence number is the end of what was sent (RFC 9293's SND.NXT): a
 * peer whose window is closed takes a segment at that number only.  One
 * the device or the pool refuses is kept to be sent again by the output
 * retry (ws220-p003), and the network worker woken to wait for it.
 * duplicate is 1 for the acknowledgement of a segment that came ahead of a
 * gap, which a peer's fast retransmit counts: up to TCP_ACK_PENDING_MAX of
 * those are kept for one receive_next.  Any other (a window update, a
 * challenge, an answer to an old segment) is kept as one, since repeating
 * it would look like duplicates to the peer (review-1 M1).  Once
 * receive_next has moved, one says all of them; one that goes with a newer
 * receive_next than the pending ones says all of them too, and they are
 * dropped.  A connection closed meanwhile keeps none (review-1 L4).
 */
static int
tcp_send_ack(
	struct tcp_endpoint *endpoint,
	int duplicate)
{
	struct socket *socket;
	unsigned long irq;
	uint32_t sequence;
	uint64_t now;
	enum tcp_refusal refusal;
	int retries;
	int error;

	/* The socket whose lock guards the ring and the retry. */
	socket = &endpoint->tcp.inet.socket;

	/* The end of what was sent. */
	irq = spin_lock_irqsave(&socket->lock);

	sequence = tcp_sent_end_locked(endpoint);

	spin_unlock_irqrestore(&socket->lock, irq);

	/* Hands it to the device. */
	error = tcp_send_segment_at(endpoint, sequence, TCP_ACK, NULL, 0);
	refusal = tcp_refusal_of(error);
	retries = tcp_refusal_retries(refusal);

	/* A connection closed meanwhile owes the peer nothing. */
	now = sched_ticks();
	irq = spin_lock_irqsave(&socket->lock);

	if (endpoint->tcp.state == TCP_CLOSED)
		retries = 0;

	/* Keeps a refused one for the output retry, or drops the pending ones a newer one covers. */
	if (retries) {
		if (endpoint->tcp.ack_pending != 0 &&
		    endpoint->tcp.ack_pending_receive_next == endpoint->tcp.receive_next) {
			if (duplicate && endpoint->tcp.ack_pending < TCP_ACK_PENDING_MAX)
				endpoint->tcp.ack_pending++;
		} else {
			endpoint->tcp.ack_pending = 1;
			endpoint->tcp.ack_pending_receive_next = endpoint->tcp.receive_next;
		}

		/* The retry for it. */
		tcp_output_refused_locked(endpoint, refusal, 1, now);
	} else if (error == 0 &&
	    endpoint->tcp.ack_pending != 0 &&
	    endpoint->tcp.ack_pending_receive_next != endpoint->tcp.receive_next) {
		endpoint->tcp.ack_pending = 0;
		tcp_output_retry_settle_locked(endpoint);
	}

	spin_unlock_irqrestore(&socket->lock, irq);

	/* A refused one: the worker learns of the retry's deadline. */
	if (retries) {
		net_worker_wakeup();
		return error;
	}

	/* Reports an acknowledgement lost for good. */
	if (error != 0)
		return error;

	/* Succeeded: the device has it. */
	return 0;
}

/* Binds a socket to a local address and port. */
static int
tcp_bind(
	struct socket *socket,
	const struct sockaddr *address,
	socklen_t length)
{
	struct tcp_endpoint *endpoint;
	int error;
	unsigned long irq;
	unsigned long socket_irq;

	/* A socket binds only once. */
	endpoint = tcp_endpoint(socket);
	if ((endpoint->tcp.inet.inet_flags & INET_SOCKET_BOUND) != 0)
		return EINVAL;

	/* Records the reuse option in force at bind time. */
	socket_irq = spin_lock_irqsave(&socket->lock);

	endpoint->tcp.inet.bind_reuse_address = socket->reuse_address;

	spin_unlock_irqrestore(&socket->lock, socket_irq);

	error = inet_socket_bind(&endpoint->tcp.inet, address, length);
	if (error != 0)
		return error;

	/* Allocates a port for zero, or checks the requested one. */
	irq = spin_lock_irqsave(&tcp_registry_lock);

	if (endpoint->tcp.inet.local_port == 0)
		error = tcp_allocate_port_locked(endpoint);
	else if (tcp_port_in_use(endpoint, 0))
		error = EADDRINUSE;
	if (error != 0) {
		endpoint->tcp.inet.local_address = 0;
		kern_memset(&endpoint->tcp.inet.local6, 0, sizeof(endpoint->tcp.inet.local6));
		endpoint->tcp.inet.mapped = 0;
		endpoint->tcp.inet.local_port = 0;
		endpoint->tcp.inet.ifindex = 0;
		endpoint->tcp.inet.bind_reuse_address = 0;
		endpoint->tcp.inet.inet_flags &= ~INET_SOCKET_BOUND;
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Reports why the bind failed. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Puts a bound socket into the listening state. */
static int
tcp_listen(
	struct socket *socket,
	int backlog)
{
	struct tcp_endpoint *endpoint;
	struct tcp_endpoint *other;
	unsigned long irq;
	int conflict;

	endpoint = tcp_endpoint(socket);
	conflict = 0;

	/* Listening again is harmless; listening on a connection is not. */
	if (endpoint->tcp.state != TCP_CLOSED) {
		if (endpoint->tcp.state == TCP_LISTEN)
			return 0;
		return EISCONN;
	}

	if ((endpoint->tcp.inet.inet_flags & INET_SOCKET_BOUND) == 0 ||
	    endpoint->tcp.inet.local_port == 0)
		return EDESTADDRREQ;

	/* Clamps the backlog. */
	if (backlog < 0)
		backlog = 0;
	if ((unsigned)backlog > TCP_LISTEN_BACKLOG_MAX)
		backlog = TCP_LISTEN_BACKLOG_MAX;

	/* No other listener may cover the same local address. */
	irq = spin_lock_irqsave(&tcp_registry_lock);

	for (other = tcp_sockets; other != NULL; other = other->next) {
		if (other != endpoint &&
		    other->tcp.state == TCP_LISTEN &&
		    inet_socket_local_conflict(&other->tcp.inet, 0,
		    &endpoint->tcp.inet, 0)) {
			conflict = 1;
			break;
		}
	}

	if (!conflict) {
		if (backlog == 0)
			endpoint->tcp.listen_backlog = 1U;
		else
			endpoint->tcp.listen_backlog = (unsigned)backlog;
		endpoint->tcp.state = TCP_LISTEN;
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Reports the listen result. */
	if (conflict)
		return EADDRINUSE;
	return 0;
}

/* Takes the next established child off a listener. */
static int
tcp_accept(
	struct socket *socket,
	struct socket **result,
	struct sockaddr *address,
	socklen_t *length,
	unsigned io_flags)
{
	struct tcp_endpoint *listener;
	struct tcp_socket *accepted;
	struct thread *thread;
	unsigned long irq;
	int error;
	uint64_t sequence;

	listener = tcp_endpoint(socket);
	thread = thread_current();

	/* Rejects a missing result or a socket that is not listening. */
	if (result == NULL)
		return EINVAL;
	irq = spin_lock_irqsave(&socket->lock);

	if (listener->tcp.state != TCP_LISTEN) {
		spin_unlock_irqrestore(&socket->lock, irq);
		return EINVAL;
	}

	/* Waits for a child unless the accept must not block. */
	while (listener->tcp.accept_head == NULL) {
		if ((io_flags & SOCKET_IO_NONBLOCK) != 0 || thread == NULL) {
			spin_unlock_irqrestore(&socket->lock, irq);
			return EAGAIN;
		}

		if (signal_pending_unblocked(thread)) {
			spin_unlock_irqrestore(&socket->lock, irq);
			return EINTR;
		}

		sequence = waitq_sequence(&socket->accept_waitq);
		error = waitq_sleep(&socket->accept_waitq, &socket->lock, sequence,
		    0, WAITQ_INTERRUPTIBLE);
		if (error == EINTR) {
			spin_unlock_irqrestore(&socket->lock, irq);
			return EINTR;
		}

		if (socket->error != 0) {
			error = socket->error;
			socket->error = 0;
			spin_unlock_irqrestore(&socket->lock, irq);
			return error;
		}
	}

	/* Reports the peer and dequeues the child. */
	accepted = listener->tcp.accept_head;
	if (address != NULL && length != NULL) {
		error = inet_socket_getpeername(&accepted->inet, address, length);
		if (error != 0) {
			spin_unlock_irqrestore(&socket->lock, irq);
			return error;
		}
	}

	listener->tcp.accept_head = accepted->queue_next;
	if (listener->tcp.accept_head == NULL)
		listener->tcp.accept_tail = NULL;
	if (listener->tcp.accept_count != 0)
		listener->tcp.accept_count--;
	accepted->queue_next = NULL;
	accepted->listener = NULL;
	*result = &accepted->inet.socket;

	spin_unlock_irqrestore(&socket->lock, irq);

	/* Reports the accepted socket. */
	return 0;
}

/* Starts or resumes an active connection to a peer. */
static int
tcp_connect(
	struct socket *socket,
	const struct sockaddr *address,
	socklen_t length,
	unsigned io_flags)
{
	struct tcp_endpoint *endpoint;
	struct thread *thread;
	struct tcp_discard cancelled;
	uint64_t deadline;
	uint64_t timeout;
	uint32_t generation;
	unsigned long irq;
	unsigned attempt;
	int error;
	uint64_t sequence;

	endpoint = tcp_endpoint(socket);
	thread = thread_current();
	cancelled.count = 0;
	deadline = 0;

	/*
	 * STOP/CONT is transparent at the syscall layer.  A redispatched connect
	 * resumes the existing attempt; an ordinary second connect still
	 * observes EALREADY/EISCONN as required by the socket API.
	 */
	if (thread != NULL && thread->syscall_stop_redispatch) {
		irq = spin_lock_irqsave(&socket->lock);
		if (endpoint->tcp.state == TCP_ESTABLISHED) {
			endpoint->tcp.connect_wait_deadline = 0;
			spin_unlock_irqrestore(&socket->lock, irq);
			return 0;
		}

		if (socket->error != 0) {
			error = socket->error;
			socket->error = 0;
			endpoint->tcp.connect_wait_deadline = 0;
			spin_unlock_irqrestore(&socket->lock, irq);
			return error;
		}

		if (endpoint->tcp.state != TCP_SYN_SENT ||
		    endpoint->tcp.active_connect_generation == 0) {
			endpoint->tcp.connect_wait_deadline = 0;
			spin_unlock_irqrestore(&socket->lock, irq);
			return ECONNABORTED;
		}

		generation = endpoint->tcp.active_connect_generation;
		deadline = endpoint->tcp.connect_wait_deadline;
		goto wait_for_connect;
	}

	/* Only a closed socket can start connecting. */
	irq = spin_lock_irqsave(&socket->lock);

	if (endpoint->tcp.state == TCP_SYN_SENT)
		error = EALREADY;
	else if (endpoint->tcp.state != TCP_CLOSED)
		error = EISCONN;
	else
		error = 0;

	spin_unlock_irqrestore(&socket->lock, irq);

	if (error != 0)
		return error;

	/* Records the peer and takes a local port when unbound. */
	error = inet_socket_connect(&endpoint->tcp.inet, address, length);
	if (error != 0)
		return error;
	if (endpoint->tcp.inet.remote_port == 0) {
		tcp_forget_peer(endpoint);
		return EADDRNOTAVAIL;
	}

	if (endpoint->tcp.inet.local_port == 0) {
		error = tcp_allocate_port(endpoint);
		if (error != 0) {
			tcp_forget_peer(endpoint);
			return error;
		}
	}

	/* Starts a new attempt with a fresh generation and sequence space. */
	endpoint->tcp.send_next = (uint32_t)sched_ticks() * 1103515245U +
	    endpoint->tcp.inet.local_port;
	irq = spin_lock_irqsave(&socket->lock);

	endpoint->tcp.connect_generation++;
	if (endpoint->tcp.connect_generation == 0)
		endpoint->tcp.connect_generation++;
	generation = endpoint->tcp.connect_generation;
	endpoint->tcp.active_connect_generation = generation;
	endpoint->tcp.connect_wait_deadline = 0;

	/*
	 * Make distinct attempts use distinct wire sequence spaces even when
	 * the scheduler tick did not advance between them.
	 */
	endpoint->tcp.send_next ^= generation * 2654435761U;

	/* A test build may start the sequence numbers just before they wrap. */
	endpoint->tcp.send_next = tcp_initial_sequence(endpoint->tcp.send_next);
	endpoint->tcp.send_unacknowledged = endpoint->tcp.send_next;
	endpoint->tcp.state = TCP_SYN_SENT;

	/* A new connection measures its own round trip (ws220-p004). */
	tcp_rtt_forget_locked(endpoint);

	spin_unlock_irqrestore(&socket->lock, irq);

	/* Sends the SYN, retrying a few times when buffers are short. */
	for (attempt = 0; attempt < 4U; attempt++) {
		error = tcp_send_reliable(endpoint, TCP_SYN, NULL, 0);
		if (error == 0)
			break;
		if (error != EAGAIN && error != EBUSY && error != ENOBUFS) {
			irq = spin_lock_irqsave(&socket->lock);
			tcp_connect_cancel_locked(endpoint, generation, &cancelled);
			spin_unlock_irqrestore(&socket->lock, irq);
			tcp_discard_free(&cancelled);
			return error;
		}

		if (thread != NULL)
			sched_sleep(sched_ticks() +
			    kern_ms_to_ticks(TCP_SYN_RETRY_MS));
	}

	if (error != 0) {
		irq = spin_lock_irqsave(&socket->lock);
		tcp_connect_cancel_locked(endpoint, generation, &cancelled);
		spin_unlock_irqrestore(&socket->lock, irq);
		tcp_discard_free(&cancelled);
		return error;
	}

	/* A non-blocking connect completes through the timer and poll. */
	if ((io_flags & SOCKET_IO_NONBLOCK) != 0)
		return EINPROGRESS;
	if (thread == NULL)
		return EAGAIN;

	/* Applies the send timeout as the wait deadline. */
	irq = spin_lock_irqsave(&socket->lock);

	timeout = socket->send_timeout_ticks;
	if (timeout != 0) {
		error = kern_deadline_after(sched_ticks(), timeout, &deadline);
		if (error != 0) {
			tcp_connect_cancel_locked(endpoint, generation, &cancelled);
			spin_unlock_irqrestore(&socket->lock, irq);
			tcp_discard_free(&cancelled);
			return error;
		}
	}

	endpoint->tcp.connect_wait_deadline = deadline;
wait_for_connect:
	/* Waits for the handshake, a failure, or the deadline. */
	while (endpoint->tcp.state == TCP_SYN_SENT &&
	    endpoint->tcp.active_connect_generation == generation &&
	    socket->error == 0) {
		sequence = waitq_sequence(&socket->connect_waitq);
		error = waitq_sleep(&socket->connect_waitq, &socket->lock,
		    sequence, deadline, WAITQ_INTERRUPTIBLE);
		if (error == EINTR) {
			/*
			 * POSIX requires an interrupted blocking connect to leave
			 * the request in progress.  The retransmission timer owns
			 * the SYN_SENT attempt from here; poll(POLLOUT) plus
			 * SO_ERROR observes completion.
			 */
			spin_unlock_irqrestore(&socket->lock, irq);
			return EINTR;
		}

		if (error == ETIMEDOUT) {
			tcp_connect_cancel_locked(endpoint, generation, &cancelled);
			spin_unlock_irqrestore(&socket->lock, irq);
			tcp_discard_free(&cancelled);
			return error;
		}
	}

	/* Reports a recorded failure, or the final state. */
	if (socket->error != 0) {
		error = socket->error;
		socket->error = 0;
		endpoint->tcp.connect_wait_deadline = 0;
		spin_unlock_irqrestore(&socket->lock, irq);
		return error;
	}

	if (endpoint->tcp.state == TCP_ESTABLISHED)
		error = 0;
	else
		error = ETIMEDOUT;
	endpoint->tcp.connect_wait_deadline = 0;

	spin_unlock_irqrestore(&socket->lock, irq);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Sends one segment of data on an established connection. */
static ssize_t
tcp_sendto(
	struct socket *socket,
	const void *buffer,
	size_t length,
	int flags,
	const struct sockaddr *address,
	socklen_t address_length)
{
	struct tcp_endpoint *endpoint;
	struct thread *thread;
	unsigned long irq;
	enum tcp_state state;
	uint32_t window;
	int error;
	uint64_t sequence;

	endpoint = tcp_endpoint(socket);
	thread = thread_current();

	(void)address_length;

	/* Rejects unsupported flags, an address, or a missing buffer. */
	if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0 ||
	    address != NULL ||
	    (buffer == NULL && length != 0))
		return -EINVAL;

	/* A shut-down or unconnected socket cannot send. */
	irq = spin_lock_irqsave(&socket->lock);

	if (socket->write_shutdown) {
		spin_unlock_irqrestore(&socket->lock, irq);
		if ((flags & MSG_NOSIGNAL) == 0 && thread != NULL &&
		    thread->proc != NULL)
			(void)signal_send_thread(thread, SIGPIPE);
		return -EPIPE;
	}

	/*
	 * A connection sends while it is established, and after the peer's FIN
	 * too (CLOSE_WAIT): the peer shut only its own writing and still reads
	 * the answer (RFC 793; BUG-151, a client's request, SHUT_WR, then the
	 * answer).
	 */
	if (endpoint->tcp.state != TCP_ESTABLISHED &&
	    endpoint->tcp.state != TCP_CLOSE_WAIT) {
		spin_unlock_irqrestore(&socket->lock, irq);
		return -ENOTCONN;
	}

	spin_unlock_irqrestore(&socket->lock, irq);

	if (length == 0)
		return 0;
	if (length > TCP_MSS)
		length = TCP_MSS;

	/*
	 * Waits for room, then queues the segment.  A segment the device or
	 * the pool refuses on its way out stays queued for the output retry
	 * (ws220-p003), so the writer goes on without sleeping for it.  Only
	 * two things send it back here: another writer took the room first,
	 * and it waits for room again; or no packet was left for the
	 * segment's copy, and it sleeps TCP_SEND_RETRY_MS, since the pool
	 * the whole stack shares tells no one when it has packets again.  A
	 * blocking send waits as long as the connection lasts; only a
	 * signal, an error or the end of the connection ends the wait.
	 */
	for (;;) {
		/*
		 * Waits for room rather than for silence: several segments may
		 * be queued, so the send proceeds as long as the ring has a
		 * free entry and the peer's window has space for this many
		 * more bytes.
		 */
		for (;;) {
			irq = spin_lock_irqsave(&socket->lock);
			window = endpoint->tcp.peer_window;

			/* An unknown or closed window still allows one segment. */
			if (window == 0)
				window = (uint32_t)TCP_MSS;
			if (endpoint->tcp.send_count < TCP_SEND_QUEUE_MAX &&
			    (tcp_in_flight(endpoint) == 0 ||
			     tcp_in_flight(endpoint) + (uint32_t)length <= window)) {
				spin_unlock_irqrestore(&socket->lock, irq);
				break;
			}

			if ((flags & MSG_DONTWAIT) != 0 || thread == NULL) {
				spin_unlock_irqrestore(&socket->lock, irq);
				return -EAGAIN;
			}

			sequence = waitq_sequence(&socket->send_waitq);
			error = waitq_sleep(&socket->send_waitq, &socket->lock, sequence,
			    0, WAITQ_INTERRUPTIBLE);
			if (socket->error != 0) {
				error = socket->error;
				socket->error = 0;
				spin_unlock_irqrestore(&socket->lock, irq);
				return -error;
			}

			spin_unlock_irqrestore(&socket->lock, irq);
			if (error == EINTR)
				return -EINTR;
		}

		/* Our own traffic also restarts the idle period. */
		irq = spin_lock_irqsave(&socket->lock);
		if (endpoint->tcp.state == TCP_ESTABLISHED && socket->keepalive != 0) {
			endpoint->tcp.keepalive_probes = 0;
			endpoint->tcp.keepalive_deadline =
			    sched_ticks() + TCP_KEEPALIVE_IDLE;
		}
		spin_unlock_irqrestore(&socket->lock, irq);

		/* Queues the segment and sends it, or leaves it to the output retry. */
		error = tcp_send_reliable(endpoint, TCP_ACK | TCP_PSH,
		    buffer, length);

		/* Another writer took the room first: waits for room again. */
		if (error == EAGAIN)
			continue;

		/* Queued, or an error the connection cannot pass. */
		if (error != ENOBUFS)
			break;

		/* No packet for the copy: a writer that does not wait is told to try later. */
		if ((flags & MSG_DONTWAIT) != 0 || thread == NULL)
			return -EAGAIN;
		if (signal_pending_unblocked(thread))
			return -EINTR;
		irq = spin_lock_irqsave(&socket->lock);

		error = socket->error;
		socket->error = 0;
		state = endpoint->tcp.state;

		/* The sleep below is counted for net.tcpstats when it is taken. */
		if (error == 0 && (state == TCP_ESTABLISHED || state == TCP_CLOSE_WAIT))
			endpoint->tcp.counters.send_buffer_sleeps++;

		spin_unlock_irqrestore(&socket->lock, irq);

		/* A failed or ended connection stops the wait; otherwise the pool gets a moment. */
		if (error != 0)
			return -error;
		if (state != TCP_ESTABLISHED && state != TCP_CLOSE_WAIT)
			return -EPIPE;
		sched_sleep(sched_ticks() + kern_ms_to_ticks(TCP_SEND_RETRY_MS));
	}

	/* Reports an error the connection cannot pass (no route). */
	if (error != 0)
		return -error;

	/* Succeeded: the bytes are queued, and the connection delivers them. */
	return (ssize_t)length;
}

/* Receives data from the socket's queue, keeping an unread remainder. */
static ssize_t
tcp_recvfrom(
	struct socket *socket,
	void *buffer,
	size_t length,
	int flags,
	struct sockaddr *address,
	socklen_t *address_length)
{
	struct tcp_endpoint *endpoint;
	struct packet_buf *packet;
	size_t copied;
	uint32_t window;
	unsigned long irq;
	int update;
	int sent;
	int error;

	endpoint = tcp_endpoint(socket);

	(void)address;
	(void)address_length;

	/* A shut-down or drained closing connection reports end of file. */
	if (socket->read_shutdown)
		return 0;
	if (endpoint->tcp.state == TCP_CLOSE_WAIT && socket->receive_head == NULL)
		return 0;

	/*
	 * After the peer's FIN was read once (its end-of-file marker taken),
	 * every later read reports the end again instead of waiting for data
	 * that cannot come (BUG-151: a client that shut its own side first
	 * reaches TIME_WAIT, and its second read slept for ever).
	 */
	if ((endpoint->tcp.state == TCP_LAST_ACK ||
	     endpoint->tcp.state == TCP_TIME_WAIT) &&
	    socket->receive_head == NULL)
		return 0;
	if (endpoint->tcp.state == TCP_CLOSED &&
	    (endpoint->tcp.inet.inet_flags & INET_SOCKET_CONNECTED) != 0 &&
	    socket->error == 0 &&
	    socket->receive_head == NULL)
		return 0;

	/* Takes the next packet and copies what fits. */
	error = socket_dequeue_packet(socket, flags, &packet);
	if (error != 0)
		return -error;
	if (length < packet->length)
		copied = length;
	else
		copied = packet->length;
	if (copied != 0)
		kern_memcpy(buffer, packet->data, copied);

	/* Retain a short unread suffix by placing it back at the queue head. */
	if (copied < packet->length) {
		(void)packet_buf_pull(packet, copied);
		(void)socket_requeue_packet_front(socket, packet);
	} else {
		packet_buf_free(packet);
	}

	/*
	 * Tells the peer about room the read has made (RFC 1122 4.2.3.3) when
	 * the window it was told of was nearly closed: too small for a full
	 * segment and now able to take one, or under two segments and now two
	 * segments larger.  A sender whose segments are larger than ours
	 * (QEMU's user network sends 1440 bytes whatever MSS we offer) avoids
	 * sending into a small window and would otherwise wait seconds for its
	 * persist timer each time the queue fills.  A window that was not
	 * nearly closed is left to the next acknowledgement.  This stack's own
	 * sender sends a segment it put beyond a closed or too small window
	 * again when an acknowledgement shows the window takes it (ws220-p005);
	 * a segment refused for want of room although the window took it is
	 * resent on duplicate acknowledgements or the timer (p007 keeps the
	 * window's promise).
	 */
	window = tcp_receive_window(endpoint);
	update = 0;
	if (endpoint->tcp.advertised_window < TCP_RECEIVE_MSS && window >= TCP_RECEIVE_MSS)
		update = 1;
	else if (endpoint->tcp.advertised_window < 2U * TCP_RECEIVE_MSS &&
	    window >= (uint32_t)endpoint->tcp.advertised_window + 2U * TCP_RECEIVE_MSS)
		update = 1;

	if (update != 0 &&
	    (endpoint->tcp.state == TCP_ESTABLISHED ||
	     endpoint->tcp.state == TCP_FIN_WAIT_1 ||
	     endpoint->tcp.state == TCP_FIN_WAIT_2)) {
		sent = tcp_send_ack(endpoint, 0);

		/* An update that went is counted for net.tcpstats (a refused one is sent again later). */
		if (sent == 0) {
			irq = spin_lock_irqsave(&socket->lock);

			endpoint->tcp.counters.window_updates_out++;

			spin_unlock_irqrestore(&socket->lock, irq);
		}
	}

	/* Reports the bytes received. */
	return (ssize_t)copied;
}

/*
 * Reports the window to advertise: the room left in the receive queue.
 *
 * The queue is bounded twice, by bytes and by how many packets it may hold
 * (the packets come from one small pool the whole stack shares), so the
 * window is the smaller of the bytes left and a full segment for each packet
 * left.  A peer that keeps within it is never refused.  It is read without
 * the socket lock, because segments are also sent with that lock held; a
 * value a moment old is what any peer sees anyway.  With no window scaling
 * the field holds at most 65535.
 */
static uint16_t
tcp_receive_window(
	struct tcp_endpoint *endpoint)
{
	const struct socket *socket = &endpoint->tcp.inet.socket;
	size_t limit = socket->receive_hiwat_bytes;
	size_t used = socket->receive_bytes;
	size_t room;
	size_t packets;

	/* The bytes left. */
	room = used < limit ? limit - used : 0;

	/* A full segment for each packet the queue may still take. */
	if (socket->receive_packet_limit != 0) {
		packets = socket->receive_packets < socket->receive_packet_limit ?
		    socket->receive_packet_limit - socket->receive_packets : 0;
		if (room > packets * TCP_RECEIVE_MSS)
			room = packets * TCP_RECEIVE_MSS;
	}

	/* Clamps to what the header can carry. */
	if (room > 0xffffU)
		room = 0xffffU;
	return (uint16_t)room;
}

/* Shuts down one or both directions, sending a FIN for the write side. */
static int
tcp_shutdown(
	struct socket *socket,
	int how)
{
	struct tcp_endpoint *endpoint;
	enum tcp_state old_state;
	enum tcp_state closing_state;
	unsigned long irq;
	int error;

	endpoint = tcp_endpoint(socket);

	/* Rejects an unknown direction. */
	if (how != SHUT_WR && how != SHUT_RDWR && how != SHUT_RD)
		return EINVAL;

	/* The read side is shut down by flag alone. */
	irq = spin_lock_irqsave(&socket->lock);

	if (how == SHUT_RD || how == SHUT_RDWR)
		socket->read_shutdown = 1;
	if (how == SHUT_RD) {
		spin_unlock_irqrestore(&socket->lock, irq);
		return 0;
	}

	if (socket->write_shutdown) {
		spin_unlock_irqrestore(&socket->lock, irq);
		return 0;
	}

	/* The write side moves to the closing state and sends a FIN. */
	if (endpoint->tcp.state != TCP_ESTABLISHED &&
	    endpoint->tcp.state != TCP_CLOSE_WAIT) {
		spin_unlock_irqrestore(&socket->lock, irq);
		return ENOTCONN;
	}

	old_state = endpoint->tcp.state;
	if (old_state == TCP_CLOSE_WAIT)
		closing_state = TCP_LAST_ACK;
	else
		closing_state = TCP_FIN_WAIT_1;
	socket->write_shutdown = 1;
	endpoint->tcp.state = closing_state;

	spin_unlock_irqrestore(&socket->lock, irq);

	/* A FIN that could not be sent restores the state. */
	error = tcp_send_reliable(endpoint, TCP_FIN | TCP_ACK, NULL, 0);
	if (error != 0) {
		irq = spin_lock_irqsave(&socket->lock);
		if (endpoint->tcp.state == closing_state) {
			endpoint->tcp.state = old_state;
			socket->write_shutdown = 0;
		}

		spin_unlock_irqrestore(&socket->lock, irq);
	}

	/* Reports why the shutdown failed. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Reports the local address. */
static int
tcp_getsockname(
	struct socket *socket,
	struct sockaddr *address,
	socklen_t *length)
{
	struct tcp_endpoint *endpoint;
	int error;

	endpoint = tcp_endpoint(socket);

	/* Reports the failure. */
	error = inet_socket_getsockname(&endpoint->tcp.inet, address, length);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Reports the peer address. */
static int
tcp_getpeername(
	struct socket *socket,
	struct sockaddr *address,
	socklen_t *length)
{
	struct tcp_endpoint *endpoint;
	int error;

	endpoint = tcp_endpoint(socket);

	/* Reports the failure. */
	error = inet_socket_getpeername(&endpoint->tcp.inet, address, length);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Closes a socket, aborting its queued children and unregistering it. */
static void
tcp_close(
	struct socket *socket)
{
	struct tcp_endpoint *endpoint;
	struct tcp_endpoint **link;
	struct tcp_socket *queued;
	unsigned long irq;

	/* A connection sends its FIN; a child leaves its listener. */
	endpoint = tcp_endpoint(socket);
	if (endpoint->tcp.state == TCP_ESTABLISHED ||
	    endpoint->tcp.state == TCP_CLOSE_WAIT)
		(void)tcp_shutdown(socket, SHUT_RDWR);
	if (endpoint->tcp.listener != NULL)
		tcp_listener_remove(endpoint);

	/* A listener aborts every half-open and unaccepted child. */
	queued = endpoint->tcp.half_open_head;
	while (queued != NULL) {
		endpoint->tcp.half_open_head = queued->queue_next;
		queued->queue_next = NULL;
		queued->listener = NULL;
		socket_set_error(&queued->inet.socket, ECONNABORTED);
		socket_release(&queued->inet.socket);
		queued = endpoint->tcp.half_open_head;
	}

	queued = endpoint->tcp.accept_head;
	while (queued != NULL) {
		endpoint->tcp.accept_head = queued->queue_next;
		queued->queue_next = NULL;
		queued->listener = NULL;
		socket_set_error(&queued->inet.socket, ECONNABORTED);
		socket_release(&queued->inet.socket);
		queued = endpoint->tcp.accept_head;
	}

	/* Unregisters the endpoint and frees it. */
	irq = spin_lock_irqsave(&tcp_registry_lock);

	for (link = &tcp_sockets; *link != NULL; link = &(*link)->next) {
		if (*link == endpoint) {
			*link = endpoint->next;
			break;
		}
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	tcp_retransmit_clear(endpoint);
	tcp_reorder_free(endpoint);
	kern_free(endpoint);
}

/* Reports whether the wait a closing connection is making is over. */
static int
tcp_drain_done(
	const struct tcp_endpoint *endpoint,
	enum tcp_drain until)
{
	unsigned index;

	/* Room for one more segment, or nothing outstanding at all. */
	if (until == TCP_DRAIN_ROOM)
		return endpoint->tcp.send_count < TCP_SEND_QUEUE_MAX;
	if (until == TCP_DRAIN_ALL)
		return endpoint->tcp.send_count == 0;

	/* No outstanding segment carries data; a FIN alone does not count. */
	for (index = 0; index < endpoint->tcp.send_count; index++) {
		if (endpoint->tcp.send_queue[(endpoint->tcp.send_first + index) %
		    TCP_SEND_QUEUE_MAX].length != 0)
			return 0;
	}
	return 1;
}

/*
 * Waits, for a bounded time, while a closing connection still needs the
 * peer to take what it sent.
 *
 * The endpoint is freed when the last reference goes, and with it the copies
 * kept for retransmission, so the data sent must be taken before then.  The
 * wait happens while the closing file still holds its reference, so that
 * acknowledgements and the retransmission timer reach the endpoint.  It ends
 * when the condition holds, the connection has failed, or TCP_CLOSE_DRAIN_MS
 * has passed.  A kernel thread (the network
 * worker among them) does not wait: it is what would deliver the
 * acknowledgements.
 */
static void
tcp_close_drain(
	struct tcp_endpoint *endpoint,
	enum tcp_drain until)
{
	struct socket *socket = &endpoint->tcp.inet.socket;
	struct thread *thread = thread_current();
	uint64_t deadline;
	uint64_t sequence;
	unsigned long irq;

	/* Only a process may wait for the network. */
	if (thread == NULL || thread->proc == NULL || thread->proc == &process0)
		return;

	deadline = sched_ticks() + kern_ms_to_ticks(TCP_CLOSE_DRAIN_MS);
	irq = spin_lock_irqsave(&socket->lock);

	/* Waits while the condition does not hold on a connection still alive. */
	while (!tcp_drain_done(endpoint, until) &&
	    endpoint->tcp.state != TCP_CLOSED && socket->error == 0 &&
	    sched_ticks() < deadline) {
		sequence = waitq_sequence(&socket->send_waitq);
		(void)waitq_sleep(&socket->send_waitq, &socket->lock, sequence,
		    deadline, 0);
	}

	spin_unlock_irqrestore(&socket->lock, irq);
}

/*
 * Closes a connection when its last file goes away: sends the FIN and lets
 * what was sent reach the peer.
 *
 * The socket layer has already marked both directions shut, so the FIN is
 * sent here rather than through tcp_shutdown(), which would take the mark to
 * mean it had been sent.  A full retransmission ring is first given time to
 * make room for it.
 */
static void
tcp_endpoint_close(
	struct socket *socket)
{
	struct tcp_endpoint *endpoint = tcp_endpoint(socket);
	enum tcp_state old_state;
	enum tcp_state closing_state;
	unsigned long irq;
	int error;

	/* Only a connection that has not sent its FIN owes one. */
	irq = spin_lock_irqsave(&socket->lock);
	old_state = endpoint->tcp.state;
	spin_unlock_irqrestore(&socket->lock, irq);
	if (old_state != TCP_ESTABLISHED && old_state != TCP_CLOSE_WAIT)
		return;

	/* Makes room for the FIN, then sends it. */
	tcp_close_drain(endpoint, TCP_DRAIN_ROOM);
	closing_state = old_state == TCP_CLOSE_WAIT ? TCP_LAST_ACK :
	    TCP_FIN_WAIT_1;
	irq = spin_lock_irqsave(&socket->lock);
	if (endpoint->tcp.state == old_state)
		endpoint->tcp.state = closing_state;
	spin_unlock_irqrestore(&socket->lock, irq);
	error = tcp_send_reliable(endpoint, TCP_FIN | TCP_ACK, NULL, 0);
	if (error != 0) {
		irq = spin_lock_irqsave(&socket->lock);
		if (endpoint->tcp.state == closing_state)
			endpoint->tcp.state = old_state;
		spin_unlock_irqrestore(&socket->lock, irq);
		return;
	}

	/*
	 * Lets what was sent be taken, the FIN included, for at most the drain
	 * time.  The FIN goes out through the retransmission ring, and a first
	 * transmission can be lost (a USB adapter drops a frame while its one
	 * transfer is busy); freeing the endpoint before the FIN is taken would
	 * leave it never sent again.  Closing second, a peer that has gone
	 * costs only that bounded wait.
	 */
	tcp_close_drain(endpoint, TCP_DRAIN_ALL);
}

/* Reports the readiness of a socket. */
static int
tcp_poll(
	struct socket *socket,
	short events,
	short *revents)
{
	struct tcp_endpoint *endpoint;
	short result;
	unsigned long irq;

	endpoint = tcp_endpoint(socket);
	result = 0;

	/* Rejects a missing socket or result. */
	if (socket == NULL || revents == NULL)
		return EINVAL;

	/* Derives readiness from the state under the socket lock. */
	irq = spin_lock_irqsave(&socket->lock);

	if (socket->error != 0)
		result |= POLLERR;
	if (endpoint->tcp.state == TCP_LISTEN) {
		/* A listener is readable when a child awaits accept. */
		if (endpoint->tcp.accept_head != NULL)
			result |= events & (POLLIN | POLLRDNORM);
	} else {
		/* Data, end of file, or a closed peer makes a connection readable. */
		if (socket->receive_head != NULL ||
		    socket->read_shutdown ||
		    endpoint->tcp.state == TCP_CLOSE_WAIT ||
		    endpoint->tcp.state == TCP_TIME_WAIT ||
		    (endpoint->tcp.state == TCP_CLOSED &&
		     (endpoint->tcp.inet.inet_flags & INET_SOCKET_CONNECTED) != 0))
			result |= events & (POLLIN | POLLRDNORM);

		/*
		 * A connection is writable while the send ring has room: a
		 * write does not have to wait for everything already sent to
		 * be acknowledged, only for space to put one more segment.
		 */
		if (!socket->write_shutdown &&
		    (endpoint->tcp.state == TCP_ESTABLISHED ||
		     endpoint->tcp.state == TCP_CLOSE_WAIT) &&
		    endpoint->tcp.send_count < TCP_SEND_QUEUE_MAX)
			result |= events & (POLLOUT | POLLWRNORM);
		if (endpoint->tcp.state == TCP_SYN_SENT && socket->error != 0)
			result |= events & (POLLOUT | POLLWRNORM);
		/*
		 * The connection hangs up when the peer's side is gone (its FIN
		 * or reset, or a read shutdown) or the socket is closing.  Our own
		 * SHUT_WR is not a hangup: it only stops POLLOUT above, so a
		 * client that sent a request and shut its side still sleeps until
		 * the answer comes (BUG-151, as Linux and FreeBSD do).
		 */
		if (socket->read_shutdown ||
		    endpoint->tcp.state == TCP_CLOSE_WAIT ||
		    endpoint->tcp.state == TCP_TIME_WAIT ||
		    socket->lifecycle != SOCKET_OPEN)
			result |= POLLHUP;
	}

	if (socket->lifecycle != SOCKET_OPEN)
		result |= POLLHUP;

	spin_unlock_irqrestore(&socket->lock, irq);

	*revents = result;

	/* Reports the derived readiness. */
	return 0;
}

/* Finds the referenced connection or listener for an incoming segment. */
static struct tcp_endpoint *
tcp_lookup(
	const struct tcp_segment_addresses *addresses,
	uint16_t source_port,
	uint16_t destination_port)
{
	struct tcp_endpoint *endpoint;
	struct tcp_endpoint *wildcard;
	unsigned long irq;
	unsigned long socket_irq;
	int match;
	int referenced;

	wildcard = NULL;
	irq = spin_lock_irqsave(&tcp_registry_lock);

	/* A connection matches on the full four-tuple. */
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next) {
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);
		match = tcp_connection_matches(endpoint, addresses, source_port, destination_port);
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
		if (match)
			goto found;
	}

	/* A listener matches on the port, preferring an exact address. */
	for (endpoint = tcp_sockets; endpoint != NULL; endpoint = endpoint->next) {
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);
		match = tcp_listener_matches(endpoint, addresses, destination_port);
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
		if (match == TCP_MATCH_EXACT)
			goto found;
		if (match == TCP_MATCH_WILDCARD)
			wildcard = endpoint;
	}

	endpoint = wildcard;
found:
	/* References the match unless it is being closed. */
	if (endpoint != NULL) {
		referenced = socket_tryref(&endpoint->tcp.inet.socket);
		if (!referenced)
			endpoint = NULL;
	}

	spin_unlock_irqrestore(&tcp_registry_lock, irq);

	/* Reports the referenced endpoint, or NULL. */
	return endpoint;
}

/*
 * Tells whether a connection is the one a segment belongs to, under the
 * socket lock: its ports, its peer, and its local address (or none yet),
 * of the segment's family.
 */
static int
tcp_connection_matches(
	const struct tcp_endpoint *endpoint,
	const struct tcp_segment_addresses *addresses,
	uint16_t source_port,
	uint16_t destination_port)
{
	int accepts;
	int same;
	int unspecified;

	/* A connection, on the ports. */
	if (endpoint->tcp.state == TCP_CLOSED ||
	    endpoint->tcp.state == TCP_LISTEN ||
	    endpoint->tcp.inet.local_port != destination_port ||
	    endpoint->tcp.inet.remote_port != source_port)
		return 0;

	/* An IPv4 segment: a socket that speaks IPv4, with the addresses. */
	if (addresses->family == AF_INET) {
		accepts = inet_socket_accepts_ipv4(&endpoint->tcp.inet);
		if (!accepts || endpoint->tcp.inet.remote_address != addresses->source)
			return 0;
		if (endpoint->tcp.inet.local_address != 0 &&
		    endpoint->tcp.inet.local_address != addresses->destination)
			return 0;
		return 1;
	}

	/* An IPv6 one: a socket that speaks IPv6, with the addresses. */
	accepts = inet_socket_accepts_ipv6(&endpoint->tcp.inet);
	if (!accepts)
		return 0;
	same = in6_equal(&endpoint->tcp.inet.remote6, &addresses->source6);
	if (!same)
		return 0;
	unspecified = in6_is_unspecified(&endpoint->tcp.inet.local6);
	same = in6_equal(&endpoint->tcp.inet.local6, &addresses->destination6);
	if (!unspecified && !same)
		return 0;

	/* Succeeded: the segment's connection. */
	return 1;
}

/*
 * Tells how a listener matches a segment, under the socket lock: not, on
 * the wildcard address, or on the exact one, of the segment's family (an
 * IPv6 listener that takes IPv4 too matches IPv4 segments on [::]).
 */
static int
tcp_listener_matches(
	const struct tcp_endpoint *endpoint,
	const struct tcp_segment_addresses *addresses,
	uint16_t destination_port)
{
	int accepts;
	int same;
	int unspecified;

	/* A listener on the port. */
	if (endpoint->tcp.state != TCP_LISTEN ||
	    endpoint->tcp.inet.local_port != destination_port)
		return TCP_MATCH_NONE;

	/* An IPv4 segment: a listener that takes IPv4, on its address or the wildcard. */
	if (addresses->family == AF_INET) {
		accepts = inet_socket_accepts_ipv4(&endpoint->tcp.inet);
		if (!accepts)
			return TCP_MATCH_NONE;
		if (endpoint->tcp.inet.local_address == addresses->destination)
			return TCP_MATCH_EXACT;
		if (endpoint->tcp.inet.local_address == 0)
			return TCP_MATCH_WILDCARD;
		return TCP_MATCH_NONE;
	}

	/* An IPv6 one: a listener that takes IPv6, on its address or [::]. */
	accepts = inet_socket_accepts_ipv6(&endpoint->tcp.inet);
	if (!accepts)
		return TCP_MATCH_NONE;
	same = in6_equal(&endpoint->tcp.inet.local6, &addresses->destination6);
	if (same)
		return TCP_MATCH_EXACT;
	unspecified = in6_is_unspecified(&endpoint->tcp.inet.local6);
	if (unspecified)
		return TCP_MATCH_WILDCARD;

	/* Succeeded: another address. */
	return TCP_MATCH_NONE;
}

/* Creates a half-open child for a SYN that reached a listener. */
static struct tcp_endpoint *
tcp_passive_syn(
	struct tcp_endpoint *listener,
	const struct tcp_segment_addresses *addresses,
	uint16_t source_port,
	uint32_t sequence)
{
	struct socket *created;
	struct tcp_endpoint *child;
	unsigned long irq;
	int error;

	/* Creates the child socket, of the listener's family. */
	error = socket_create(listener->tcp.inet.family, SOCK_STREAM, IPPROTO_TCP, &created);
	if (error != 0)
		return NULL;
	child = tcp_endpoint(created);

	/* The listener must still listen and have backlog room. */
	irq = spin_lock_irqsave(&listener->tcp.inet.socket.lock);

	if (listener->tcp.state != TCP_LISTEN ||
	    listener->tcp.half_open_count + listener->tcp.accept_count >=
	    listener->tcp.listen_backlog) {
		spin_unlock_irqrestore(&listener->tcp.inet.socket.lock, irq);
		socket_release(created);
		return NULL;
	}

	/* The child inherits the listener's address, options, and timeouts. */
	tcp_child_addresses(child, listener, addresses);
	child->tcp.inet.local_port = listener->tcp.inet.local_port;
	child->tcp.inet.remote_port = source_port;
	child->tcp.inet.ifindex = listener->tcp.inet.ifindex;
	child->tcp.inet.inet_flags = INET_SOCKET_BOUND | INET_SOCKET_CONNECTED;
	child->tcp.receive_next = sequence + 1U;
	child->tcp.send_next = (uint32_t)sched_ticks() * 1103515245U +
	    child->tcp.inet.local_port + source_port;
	child->tcp.send_next = tcp_initial_sequence(child->tcp.send_next);
	child->tcp.send_unacknowledged = child->tcp.send_next;
	child->tcp.listener = &listener->tcp;
	child->tcp.inet.socket.receive_timeout_ticks =
	    listener->tcp.inet.socket.receive_timeout_ticks;
	child->tcp.inet.socket.send_timeout_ticks =
	    listener->tcp.inet.socket.send_timeout_ticks;
	child->tcp.inet.socket.reuse_address =
	    listener->tcp.inet.socket.reuse_address;
	child->tcp.inet.bind_reuse_address =
	    listener->tcp.inet.bind_reuse_address;
	child->tcp.queue_next = listener->tcp.half_open_head;
	listener->tcp.half_open_head = &child->tcp;
	listener->tcp.half_open_count++;

	spin_unlock_irqrestore(&listener->tcp.inet.socket.lock, irq);

	/* Answers with a SYN-ACK, dropping the child when that fails. */
	irq = spin_lock_irqsave(&child->tcp.inet.socket.lock);

	child->tcp.state = TCP_SYN_RECEIVED;

	spin_unlock_irqrestore(&child->tcp.inet.socket.lock, irq);

	error = tcp_send_reliable(child, TCP_SYN | TCP_ACK, NULL, 0);
	if (error != 0) {
		tcp_listener_remove(child);
		socket_release(created);
		return NULL;
	}

	/* Reports the half-open child. */
	return child;
}

/*
 * Gives a listener's child its addresses: the segment's peer, and the
 * listener's address or, on a wildcard, the one the segment came to.  An
 * IPv4 segment to an IPv6 listener makes an IPv4-mapped child.
 */
static void
tcp_child_addresses(
	struct tcp_endpoint *child,
	const struct tcp_endpoint *listener,
	const struct tcp_segment_addresses *addresses)
{
	int unspecified;
	int linklocal;

	/* An IPv4 connection: the listener's address, or the segment's. */
	child->tcp.inet.v6only = listener->tcp.inet.v6only;
	if (addresses->family == AF_INET) {
		if (listener->tcp.inet.local_address == 0)
			child->tcp.inet.local_address = addresses->destination;
		else
			child->tcp.inet.local_address = listener->tcp.inet.local_address;
		child->tcp.inet.remote_address = addresses->source;
		if (child->tcp.inet.family == AF_INET6)
			child->tcp.inet.mapped = 1;
		return;
	}

	/* An IPv6 one: the same, with the interface a link-local peer is on. */
	unspecified = in6_is_unspecified(&listener->tcp.inet.local6);
	if (unspecified)
		child->tcp.inet.local6 = addresses->destination6;
	else
		child->tcp.inet.local6 = listener->tcp.inet.local6;
	child->tcp.inet.remote6 = addresses->source6;
	linklocal = in6_is_linklocal(&addresses->source6);
	if (linklocal)
		child->tcp.inet.scope6 = addresses->ifindex;
}

/*
 * Tells what error a reset reports to the socket, under the socket lock.
 *
 * A connection that is still sending its SYN has not been accepted by
 * anyone, so a reset there means the peer refused it: connect(2) reports
 * ECONNREFUSED.  RFC 793 accepts such a reset only when it acknowledges the
 * SYN, so a stray one cannot end the attempt; 0 means "ignore it".  A reset
 * in any other state resets a connection that existed.
 */
static int
tcp_reset_error_locked(
	const struct tcp_endpoint *endpoint,
	uint8_t flags,
	uint32_t acknowledgement)
{
	/* A connection past the handshake is reset. */
	if (endpoint->tcp.state != TCP_SYN_SENT)
		return ECONNRESET;

	/* A reset that acknowledges nothing does not answer the SYN. */
	if ((flags & TCP_ACK) == 0)
		return 0;

	/* A reset for another sequence space answers some other SYN. */
	if (acknowledgement != endpoint->tcp.send_next)
		return 0;

	/* Succeeded: the peer refused this connection. */
	return ECONNREFUSED;
}

/* Takes an IPv4 TCP segment whose checksum holds. */
static int
tcp_input(
	struct packet_buf *packet,
	uint32_t source,
	uint32_t destination)
{
	struct tcp_segment_addresses addresses;
	uint16_t checksum;
	int error;

	/* Drops a short or corrupt segment. */
	if (packet == NULL || packet->length < sizeof(struct tcp_wire)) {
		packet_buf_free(packet);
		return EINVAL;
	}

	checksum = net_checksum_pseudo(source, destination, IPPROTO_TCP, packet->data, packet->length);
	if (checksum != 0) {
		packet_buf_free(packet);
		return EINVAL;
	}

	/* Hands it on with its addresses. */
	kern_memset(&addresses, 0, sizeof(addresses));
	addresses.family = AF_INET;
	addresses.source = source;
	addresses.destination = destination;
	error = tcp_segment_input(packet, &addresses);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Takes an IPv6 TCP segment whose checksum holds (ws130-p003). */
static int
tcp6_input(
	struct packet_buf *packet,
	const struct in6_addr *source,
	const struct in6_addr *destination)
{
	struct tcp_segment_addresses addresses;
	uint16_t checksum;
	int multicast;
	int error;

	/* Drops a short or corrupt segment, and one to a group. */
	if (packet == NULL || packet->length < sizeof(struct tcp_wire)) {
		packet_buf_free(packet);
		return EINVAL;
	}

	checksum = net_checksum_pseudo6(source->s6_addr, destination->s6_addr, IPPROTO_TCP, packet->data, packet->length);
	if (checksum != 0) {
		packet_buf_free(packet);
		return EINVAL;
	}

	multicast = in6_is_multicast(destination);
	if (multicast) {
		packet_buf_free(packet);
		return EINVAL;
	}

	/* Hands it on with its addresses and the interface it came in on. */
	kern_memset(&addresses, 0, sizeof(addresses));
	addresses.family = AF_INET6;
	addresses.source6 = *source;
	addresses.destination6 = *destination;
	if (packet->device != NULL)
		addresses.ifindex = packet->device->ifindex;
	error = tcp_segment_input(packet, &addresses);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Handles an incoming TCP segment of either family, its checksum checked. */
static int
tcp_segment_input(
	struct packet_buf *packet,
	const struct tcp_segment_addresses *addresses)
{
	const struct tcp_wire *tcp;
	struct tcp_endpoint *endpoint;
	uint32_t sequence;
	uint32_t acknowledgement;
	uint16_t source_port;
	uint16_t destination_port;
	size_t header_length;
	size_t payload_length;
	uint8_t flags;
	enum tcp_state state;
	unsigned long socket_irq;
	struct tcp_discard retransmit;
	int reset_error;
	enum tcp_reset_check reset_check;
	uint32_t resend_sequence;
	int resend;
	int established;
	uint32_t sent_end;
	unsigned sent;
	int old_payload;
	int accept_payload;
	int discard_payload;
	int queued;
	int window_opened;
	int duplicate;
	int counted;
	int moved;
	int fast;
	int reprobe;
	uint32_t previous_unacknowledged;
	int window_update;
	int acknowledges_unsent;
	int acknowledges_old;
	uint32_t overlap;
	int starts_before;
	uint16_t segment_window;
	uint16_t previous_window;
	int retired;
	int accept_fin;
	struct packet_buf *eof;
	void *pulled;

	/* Drops a segment whose header does not fit. */
	tcp = (const struct tcp_wire *)packet->data;
	header_length = (size_t)(tcp->data_offset >> 4) * 4U;
	if (header_length < sizeof(*tcp) || header_length > packet->length) {
		packet_buf_free(packet);
		return EINVAL;
	}

	/* Finds the connection or listener; an unmatched segment is dropped. */
	source_port = wire_get16(tcp->source);
	destination_port = wire_get16(tcp->destination);
	endpoint = tcp_lookup(addresses, source_port, destination_port);
	if (endpoint == NULL) {
		tcp_send_reset(addresses, tcp, packet->length - header_length);
		packet_buf_free(packet);
		return 0;
	}

	sequence = wire_get32(tcp->sequence);
	acknowledgement = wire_get32(tcp->acknowledgement);
	flags = tcp->flags;
	payload_length = packet->length - header_length;
	socket_irq = spin_lock_irqsave(

	    &endpoint->tcp.inet.socket.lock);
	state = endpoint->tcp.state;

	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

	/* A listener answers a bare SYN with a half-open child. */
	if (state == TCP_LISTEN) {
		if ((flags & (TCP_SYN | TCP_ACK | TCP_RST)) == TCP_SYN)
			(void)tcp_passive_syn(endpoint, addresses, source_port, sequence);
		packet_buf_free(packet);
		socket_release(&endpoint->tcp.inet.socket);
		return 0;
	}

	/* A reset closes the connection and fails any pending connect. */
	if (flags & TCP_RST) {
		retransmit.count = 0;
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);

		/* Tells a refused connect from a reset connection. */
		reset_error = tcp_reset_error_locked(endpoint, flags,
		    acknowledgement);

		/*
		 * Past the SYN, a reset is taken only at the next sequence
		 * number expected; one elsewhere in the window is challenged,
		 * and one outside it ignored (RFC 5961 section 3.2).
		 */
		reset_check = TCP_RESET_ACCEPT;
		if (endpoint->tcp.state != TCP_SYN_SENT)
			reset_check = tcp_reset_check_locked(endpoint, sequence);
		if (reset_check == TCP_RESET_IGNORE)
			reset_error = 0;

		/* Answers a reset within the window with an acknowledgement, and the connection stays. */
		if (reset_check == TCP_RESET_CHALLENGE) {
			spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock,
			    socket_irq);
			(void)tcp_send_ack(endpoint, 0);
			packet_buf_free(packet);
			socket_release(&endpoint->tcp.inet.socket);
			return 0;
		}

		/* Ignores a reset that does not answer this connection's SYN, or lies outside the window. */
		if (reset_error == 0) {
			spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock,
			    socket_irq);
			packet_buf_free(packet);
			socket_release(&endpoint->tcp.inet.socket);
			return 0;
		}

		tcp_retransmit_reset(endpoint, &retransmit);
		endpoint->tcp.state = TCP_CLOSED;
		endpoint->tcp.active_connect_generation = 0;
		endpoint->tcp.connect_wait_deadline = 0;
		tcp_forget_peer(endpoint);
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
		tcp_discard_free(&retransmit);
		socket_set_error(&endpoint->tcp.inet.socket, reset_error);
		if (endpoint->tcp.listener != NULL) {
			tcp_listener_remove(endpoint);
			packet_buf_free(packet);
			/* Drop the listener queue and lookup references. */
			socket_release(&endpoint->tcp.inet.socket);
			socket_release(&endpoint->tcp.inet.socket);
			return 0;
		}

		socket_wake_connect(&endpoint->tcp.inet.socket);
		packet_buf_free(packet);
		socket_release(&endpoint->tcp.inet.socket);
		return 0;
	}

	/* A half-open child completes on the handshake ACK, or repeats its SYN-ACK. */
	if (state == TCP_SYN_RECEIVED) {
		retransmit.count = 0;
		resend_sequence = 0;
		resend = 0;
		established = 0;
		socket_irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);
		if ((flags & (TCP_SYN | TCP_ACK | TCP_RST)) == TCP_SYN &&
		    sequence + 1U == endpoint->tcp.receive_next) {
			resend = 1;

			/*
			 * The SYN-ACK is the oldest thing outstanding on a
			 * half-open connection, so that is what a repeated
			 * SYN asks to have sent again.
			 */
			resend_sequence = endpoint->tcp.send_count != 0
			    ? endpoint->tcp.send_queue[
				  endpoint->tcp.send_first].sequence
			    : endpoint->tcp.send_next;

			/*
			 * The SYN-ACK sent again is a resend: no sample from
			 * its handshake, and the data starts with three seconds
			 * (ws220-p004 review-1 M3).
			 */
			endpoint->tcp.rtt_timing = 0;
			endpoint->tcp.syn_timed_out = 1;
		}

		/*
		 * The handshake's ACK establishes the connection and gives the
		 * peer's first window, which later segments update by SND.WL1
		 * and SND.WL2 (RFC 9293 section 3.10.7.3).
		 */
		if ((flags & TCP_ACK) != 0 &&
		    acknowledgement == endpoint->tcp.send_next) {
			tcp_rtt_handshake_locked(endpoint, acknowledgement, sched_ticks());
			endpoint->tcp.send_unacknowledged = acknowledgement;
			tcp_retransmit_reset(endpoint, &retransmit);
			endpoint->tcp.peer_window = wire_get16(tcp->window);
			endpoint->tcp.send_window_sequence = sequence;
			endpoint->tcp.send_window_acknowledgement = acknowledgement;
			endpoint->tcp.state = TCP_ESTABLISHED;
			tcp_keepalive_arm_locked(endpoint);
			established = 1;
		}

		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
		tcp_discard_free(&retransmit);
		if (resend)
			(void)tcp_send_segment_at(endpoint, resend_sequence,
			    TCP_SYN | TCP_ACK, NULL, 0);
		if (established) {
			socket_wake_send(&endpoint->tcp.inet.socket);
			tcp_listener_established(endpoint);
		}

		packet_buf_free(packet);
		socket_release(&endpoint->tcp.inet.socket);
		return 0;
	}

	/* An active connect completes on the SYN-ACK for its sequence. */
	if (state == TCP_SYN_SENT) {
		retransmit.count = 0;
		established = 0;
		socket_irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);
		if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
		    endpoint->tcp.active_connect_generation != 0 &&
		    acknowledgement == endpoint->tcp.send_next) {
			tcp_rtt_handshake_locked(endpoint, acknowledgement, sched_ticks());
			endpoint->tcp.send_unacknowledged = acknowledgement;
			tcp_retransmit_reset(endpoint, &retransmit);
			endpoint->tcp.receive_next = sequence + 1U;
			endpoint->tcp.peer_window = wire_get16(tcp->window);
			endpoint->tcp.send_window_sequence = sequence;
			endpoint->tcp.send_window_acknowledgement = acknowledgement;
			endpoint->tcp.state = TCP_ESTABLISHED;
			endpoint->tcp.active_connect_generation = 0;
			endpoint->tcp.connect_wait_deadline = 0;
			tcp_keepalive_arm_locked(endpoint);
			waitq_wake_all(&endpoint->tcp.inet.socket.connect_waitq);
			waitq_wake_all(&endpoint->tcp.inet.socket.send_waitq);
			poll_notify();
			established = 1;
		}

		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
		tcp_discard_free(&retransmit);
		if (established)
			(void)tcp_send_ack(endpoint, 0);
		packet_buf_free(packet);
		socket_release(&endpoint->tcp.inet.socket);
		return 0;
	}

	/*
	 * A SYN on a connection past its handshake (most often the peer's
	 * SYN-ACK sent again because the handshake's last ACK was lost) is
	 * answered by an acknowledgement and dropped (RFC 5961 section 4): the
	 * peer that lost the ACK completes its handshake on it, and a SYN from
	 * anyone else ends nothing.
	 */
	if ((flags & TCP_SYN) != 0) {
		/* Acknowledges what this end has, and drops the SYN. */
		(void)tcp_send_ack(endpoint, 0);
		packet_buf_free(packet);
		socket_release(&endpoint->tcp.inet.socket);
		return 0;
	}

	/*
	 * Anything arriving from the peer is proof the connection is alive, so
	 * the idle period starts over and the unanswered probes are forgotten.
	 * This includes the peer's answer to a probe of our own.
	 */
	socket_irq = spin_lock_irqsave(&endpoint->tcp.inet.socket.lock);
	if (endpoint->tcp.state == TCP_ESTABLISHED &&
	    endpoint->tcp.inet.socket.keepalive != 0) {
		endpoint->tcp.keepalive_probes = 0;
		endpoint->tcp.keepalive_deadline =
		    sched_ticks() + TCP_KEEPALIVE_IDLE;
	}

	/* The segment of a connection past its handshake is counted for net.tcpstats. */
	endpoint->tcp.counters.segments_in++;
	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

	/*
	 * An acknowledgement retires every segment it covers, not only the
	 * oldest, and carries the peer's current window so the next send
	 * knows how much it may put in flight.
	 */
	if ((flags & TCP_ACK) != 0) {
		retransmit.count = 0;
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);
		endpoint->tcp.counters.acks_in++;

		/*
		 * Where the acknowledgement lies, modulo 2^32 (RFC 9293
		 * section 3.4): past what was handed to the device (the
		 * unsent entries are not; ws220-p003), before what is
		 * outstanding, or within it.
		 */
		sent_end = tcp_sent_end_locked(endpoint);
		acknowledges_unsent = tcp_sequence_before(sent_end, acknowledgement);
		acknowledges_old = tcp_sequence_before(acknowledgement,
		    endpoint->tcp.send_unacknowledged);

		/*
		 * An acknowledgement of something not yet sent is answered by
		 * an acknowledgement, and the segment dropped (RFC 9293
		 * section 3.10.7.4); net.tcpstats counts it.
		 */
		if (acknowledges_unsent) {
			endpoint->tcp.counters.acks_outside++;
			spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
			(void)tcp_send_ack(endpoint, 0);
			packet_buf_free(packet);
			socket_release(&endpoint->tcp.inet.socket);
			return 0;
		}

		/*
		 * One within what is outstanding retires what it covers.  An
		 * older one, overtaken on the way by a later one, is ignored,
		 * and net.tcpstats counts it; the segment's data and FIN are
		 * still taken below.
		 */
		previous_unacknowledged = endpoint->tcp.send_unacknowledged;
		if (!acknowledges_old) {
			endpoint->tcp.send_unacknowledged = acknowledgement;
			tcp_retire_acknowledged(endpoint, acknowledgement,
			    &retransmit);
		} else {
			endpoint->tcp.counters.acks_outside++;
		}

		/*
		 * The window is taken from an acceptable acknowledgement whose
		 * segment is newer than the one the window last came from
		 * (SND.WL1 and SND.WL2), so an old one cannot set it back.
		 */
		previous_window = endpoint->tcp.peer_window;
		segment_window = wire_get16(tcp->window);
		window_opened = 0;
		window_update = 0;
		if (!acknowledges_old)
			window_update = tcp_send_window_update_allowed_locked(endpoint, sequence, acknowledgement);
		if (window_update) {
			if (segment_window > endpoint->tcp.peer_window)
				window_opened = 1;
			endpoint->tcp.peer_window = segment_window;
			endpoint->tcp.send_window_sequence = sequence;
			endpoint->tcp.send_window_acknowledgement = acknowledgement;

			/* A closed window is counted for net.tcpstats. */
			if (endpoint->tcp.peer_window == 0)
				endpoint->tcp.counters.zero_windows_in++;
		}

		/*
		 * So is a duplicate acknowledgement (RFC 5681 section 2): one
		 * that takes nothing new while data waits, carries no data, no
		 * SYN or FIN, and leaves the window as it was.
		 */
		sent = tcp_sent_count_locked(endpoint);
		duplicate = 0;
		if (retransmit.count == 0 &&
		    acknowledgement == previous_unacknowledged &&
		    sent != 0 &&
		    endpoint->tcp.peer_window == previous_window &&
		    payload_length == 0 &&
		    (flags & (TCP_SYN | TCP_FIN)) == 0)
			duplicate = 1;

		/*
		 * An acknowledgement into the middle of the oldest segment moved
		 * SND.UNA without taking a segment: it is no duplicate, and the
		 * duplicates in a row start over (review-1 L1).
		 */
		moved = tcp_sequence_before(previous_unacknowledged, acknowledgement);
		if (moved)
			endpoint->tcp.duplicate_acks = 0;

		/*
		 * A duplicate counts towards a fast retransmit only while the
		 * oldest segment fits the window (review-1 G1: the answers to a
		 * closed window are not losses) and outside the duplicates of a
		 * recover a timeout ended at (review-1 M1).  Duplicates that do
		 * not count leave the count as it is (review-1 L2).
		 */
		counted = 0;
		if (duplicate) {
			endpoint->tcp.counters.duplicate_acks_in++;
			counted = tcp_oldest_fits_window_locked(endpoint);
		}

		/* Not the duplicates of the recover a timeout ended at. */
		if (counted && endpoint->tcp.dupack_guard != 0 && acknowledgement == endpoint->tcp.dupack_guard_sequence)
			counted = 0;

		/* One more in a row. */
		if (counted)
			endpoint->tcp.duplicate_acks++;

		/*
		 * The third duplicate in a row outside a recovery has the oldest
		 * segment resent at once (RFC 5681 section 3.2, RFC 6582 section
		 * 3.2) and starts a recovery up to what has been sent.  recover is
		 * not compared outside a recovery (review-1 G2).
		 */
		fast = 0;
		if (counted && endpoint->tcp.duplicate_acks == TCP_DUPLICATE_ACKS_FAST && endpoint->tcp.recovery == 0)
			fast = 1;

		/* The fast retransmit: a recovery up to what was sent, and the oldest resent at once. */
		if (fast) {
			endpoint->tcp.recover = tcp_sent_end_locked(endpoint);
			endpoint->tcp.recovery = 1;
			endpoint->tcp.recovery_by_timeout = 0;
			endpoint->tcp.dupack_guard = 0;
			endpoint->tcp.retransmit_deadline = sched_ticks();
			endpoint->tcp.retransmit_immediate = TCP_IMMEDIATE_LOSS;
			endpoint->tcp.counters.fast_retransmits++;
			endpoint->tcp.counters.immediate_resends++;
		}

		/*
		 * The oldest segment went out last as a probe of a window that
		 * could not take it, and the window now takes it (ws220-p005,
		 * after any acknowledgement, review-1 G1): the peer refused it,
		 * so it is sent again now, and the backoff of the closed window
		 * ends (review-1 M3).  Any other reopened window says nothing of
		 * what the peer took, and the resend it used to bring was a
		 * spurious one (ws220-p004 §6).
		 */
		reprobe = 0;
		if (sent != 0 && endpoint->tcp.retransmit_immediate == 0)
			reprobe = tcp_probe_fits_locked(endpoint);
		if (reprobe) {
			endpoint->tcp.send_queue[endpoint->tcp.send_first].probe = 0;
			endpoint->tcp.rto_backoff = 0;
			endpoint->tcp.retransmit_deadline = sched_ticks();
			endpoint->tcp.retransmit_immediate = TCP_IMMEDIATE_PROBE;
			endpoint->tcp.counters.immediate_resends++;
		}

		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);
		retired = retransmit.count != 0;
		if (retired)
			tcp_discard_free(&retransmit);

		/*
		 * A writer waits for room in flight, which an acknowledgement
		 * makes by retiring segments or by reopening the window; a
		 * window update alone retires nothing but must wake it too.
		 */
		if (retired || window_opened)
			socket_wake_send(&endpoint->tcp.inet.socket);
	}

	/*
	 * In-order payload is queued and then acknowledged; a read shutdown
	 * discards it.  Only payload that was actually queued moves
	 * receive_next: a segment the receive queue has no room for is left
	 * unacknowledged, so the peer sends it again instead of believing it
	 * delivered.  The acknowledgement goes out either way, carrying the
	 * window as it now stands.
	 */
	if (payload_length != 0) {
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);
		accept_payload = sequence == endpoint->tcp.receive_next;
		discard_payload = endpoint->tcp.inet.socket.read_shutdown;
		old_payload = tcp_sequence_at_or_before(sequence + (uint32_t)payload_length,
		    endpoint->tcp.receive_next);

		/*
		 * A segment that starts before receive_next and ends after it
		 * (a resend the peer cut differently, or one that overlaps a
		 * part already taken) has its part already taken cut off, and
		 * the rest is taken in order (RFC 9293 section 3.10.7.4).
		 */
		overlap = 0;
		if (!accept_payload && !old_payload) {
			starts_before = tcp_sequence_before(sequence, endpoint->tcp.receive_next);
			if (starts_before)
				overlap = endpoint->tcp.receive_next - sequence;
		}

		/* The segment then starts at receive_next, shorter by the part cut off. */
		if (overlap != 0) {
			sequence = endpoint->tcp.receive_next;
			payload_length -= overlap;
			accept_payload = 1;
		}

		/* A segment taken already, or one ahead of a gap, is counted for net.tcpstats. */
		if (!accept_payload && old_payload)
			endpoint->tcp.counters.old_segments_in++;
		else if (!accept_payload)
			endpoint->tcp.counters.out_of_order_in++;
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

		/*
		 * A segment wholly before receive_next was already taken; the
		 * peer sends it again because it did not see the acknowledgement,
		 * so it gets one (RFC 793).
		 */
		if (!accept_payload && old_payload) {
			(void)tcp_send_ack(endpoint, 0);
			goto payload_done;
		}

		/*
		 * A segment ahead of receive_next (one before it was lost) is
		 * kept until the gap is filled, and the peer is told of the gap
		 * at once by a duplicate acknowledgement (RFC 5681 section 4.2),
		 * so that it resends the one segment missing after three of them
		 * rather than the whole window after its timer (BUG-222).  A
		 * duplicate acknowledgement retires nothing and restarts no
		 * timer of this end's own sender (tcp_retire_acknowledged).
		 */
		if (!accept_payload) {
			pulled = NULL;
			if (!discard_payload)
				pulled = packet_buf_pull(packet, header_length);
			if (pulled != NULL)
				tcp_reorder_keep(endpoint, &packet, sequence);
			(void)tcp_send_ack(endpoint, 1);
			goto payload_done;
		}

		/* The header comes off, and with it any part of the payload already taken. */
		pulled = packet_buf_pull(packet, header_length + overlap);
		if (pulled == NULL) {
			packet_buf_free(packet);
			socket_release(&endpoint->tcp.inet.socket);
			return EINVAL;
		}

		queued = 1;
		if (!discard_payload) {
			queued = socket_enqueue_stream(&endpoint->tcp.inet.socket,
			    packet) == 0;
			packet = NULL;
		}

		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);
		if (queued && endpoint->tcp.receive_next == sequence) {
			endpoint->tcp.receive_next += (uint32_t)payload_length;
			endpoint->tcp.counters.data_bytes_in += payload_length;
		}

		/* A segment the receive queue had no room for is counted for net.tcpstats. */
		if (!queued)
			endpoint->tcp.counters.receive_refused++;
		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

		/* The segments kept ahead that now follow on, then one acknowledgement for all. */
		if (queued)
			tcp_reorder_drain(endpoint, discard_payload);
		(void)tcp_send_ack(endpoint, 0);
		if (!queued)
			goto fin_done;
	}

payload_done:
	/* An in-order FIN advances the state and queues an end-of-file marker. */
	if ((flags & TCP_FIN) != 0) {
		socket_irq = spin_lock_irqsave(
		    &endpoint->tcp.inet.socket.lock);
		accept_fin = sequence + (uint32_t)payload_length ==
		    endpoint->tcp.receive_next;
		if (accept_fin) {
			endpoint->tcp.receive_next++;
			if (endpoint->tcp.state == TCP_ESTABLISHED)
				endpoint->tcp.state = TCP_CLOSE_WAIT;
			else if (endpoint->tcp.state == TCP_FIN_WAIT_1 ||
			    endpoint->tcp.state == TCP_FIN_WAIT_2)
				endpoint->tcp.state = TCP_TIME_WAIT;
		}

		spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

		/*
		 * A FIN that is not the next thing, most often one sent again
		 * because the acknowledgement of the first was lost, is answered
		 * the same way; left unanswered, the peer repeats it forever.
		 */
		if (!accept_fin) {
			(void)tcp_send_ack(endpoint, 0);
			goto fin_done;
		}

		/* Acknowledges the FIN, and tells the reader of the end. */
		(void)tcp_send_ack(endpoint, 0);
		eof = packet_buf_alloc(0);
		if (eof != NULL)
			(void)socket_enqueue_packet(&endpoint->tcp.inet.socket,
			    eof);
		else
			socket_wake_receive(&endpoint->tcp.inet.socket);
	}

fin_done:
	/* A fully acknowledged FIN of ours advances the closing states. */
	socket_irq = spin_lock_irqsave(

	    &endpoint->tcp.inet.socket.lock);
	if (endpoint->tcp.state == TCP_FIN_WAIT_1 &&
	    endpoint->tcp.send_unacknowledged == endpoint->tcp.send_next)
		endpoint->tcp.state = TCP_FIN_WAIT_2;
	if (endpoint->tcp.state == TCP_LAST_ACK &&
	    endpoint->tcp.send_unacknowledged == endpoint->tcp.send_next)
		endpoint->tcp.state = TCP_CLOSED;

	spin_unlock_irqrestore(&endpoint->tcp.inet.socket.lock, socket_irq);

	/* Frees a packet that was not queued and drops the lookup reference. */
	if (packet != NULL)
		packet_buf_free(packet);
	socket_release(&endpoint->tcp.inet.socket);
	return 0;
}

/*
 * Answers a segment that belongs to no connection with a reset (RFC 793).
 *
 * The peer of a connection this end has already freed would otherwise
 * repeat its segment, a FIN most often, until its own timers give up.  A
 * reset is never answered, so two ends cannot exchange them forever.
 */
static void
tcp_send_reset(
	const struct tcp_segment_addresses *addresses,
	const struct tcp_wire *segment,
	size_t payload_length)
{
	struct net_route route;
	struct net_device *device;
	struct packet_buf *packet;
	struct tcp_wire *tcp;
	uint32_t sequence;
	uint32_t acknowledgement;
	uint16_t checksum;
	uint8_t flags;
	int error;

	/* Never answers a reset. */
	if ((segment->flags & TCP_RST) != 0)
		return;

	/*
	 * An acknowledging segment is answered from the sequence it expects;
	 * any other is acknowledged past its payload, SYN and FIN.
	 */
	sequence = 0;
	acknowledgement = 0;
	flags = TCP_RST;
	if ((segment->flags & TCP_ACK) != 0) {
		sequence = wire_get32(segment->acknowledgement);
	} else {
		acknowledgement = wire_get32(segment->sequence) + (uint32_t)payload_length;
		if ((segment->flags & TCP_SYN) != 0)
			acknowledgement++;
		if ((segment->flags & TCP_FIN) != 0)
			acknowledgement++;
		flags |= TCP_ACK;
	}

	/* Builds the reset. */
	packet = packet_buf_alloc(PACKET_BUF_DEFAULT_HEADROOM);
	if (packet == NULL)
		return;

	tcp = packet_buf_append(packet, sizeof(*tcp));
	if (tcp == NULL) {
		packet_buf_free(packet);
		return;
	}

	kern_memset(tcp, 0, sizeof(*tcp));
	wire_put16(tcp->source, wire_get16(segment->destination));
	wire_put16(tcp->destination, wire_get16(segment->source));
	wire_put32(tcp->sequence, sequence);
	wire_put32(tcp->acknowledgement, acknowledgement);
	tcp->data_offset = 5U << 4;
	tcp->flags = flags;

	/* An IPv6 segment is answered over IPv6 (ws130-p003). */
	if (addresses->family == AF_INET6) {
		tcp_send_reset6(addresses, packet);
		return;
	}

	/* Finds the interface the peer is reached through. */
	error = route_lookup_ref(addresses->source, &route);
	if (error != 0) {
		packet_buf_free(packet);
		return;
	}

	device = route.device;
	route.device = NULL;
	route_release(&route);
	if (device == NULL) {
		packet_buf_free(packet);
		return;
	}

	/* Sends it; a reset that is lost is sent again for the next segment. */
	checksum = net_checksum_pseudo(addresses->destination, addresses->source, IPPROTO_TCP,
	    packet->data, packet->length);
	wire_put16(tcp->checksum, checksum);
	(void)ipv4_output(device, addresses->source, IPPROTO_TCP, packet);
	net_device_release(device);
}

/*
 * Sends a built reset over IPv6, from the address the segment came to, out
 * of the interface it came in on for a link-local peer.  The packet is
 * consumed.
 */
static void
tcp_send_reset6(
	const struct tcp_segment_addresses *addresses,
	struct packet_buf *packet)
{
	struct net_device *device;
	struct tcp_wire *tcp;
	uint16_t checksum;
	int linklocal;

	/* The interface of a link-local peer. */
	device = NULL;
	linklocal = in6_is_linklocal(&addresses->source6);
	if (linklocal) {
		device = net_device_find_by_index_ref(addresses->ifindex);
		if (device == NULL) {
			packet_buf_free(packet);
			return;
		}
	}

	/* Sends it; a reset that is lost is sent again for the next segment. */
	tcp = (struct tcp_wire *)packet->data;
	checksum = net_checksum_pseudo6(addresses->destination6.s6_addr, addresses->source6.s6_addr, IPPROTO_TCP,
	    packet->data, packet->length);
	wire_put16(tcp->checksum, checksum);
	(void)ipv6_output(device, &addresses->destination6, &addresses->source6, IPPROTO_TCP, 0U, packet);
	if (device != NULL)
		net_device_release(device);
}

/*
 * Keeps a payload that came ahead of receive_next (its header pulled off)
 * in the connection's reorder queue, in sequence order, when it lies
 * within the window and the queue has room (a full queue gives up its
 * farthest segment for a nearer one).  A segment already kept, or none
 * kept, leaves *packet for the caller to free; a kept one is taken.
 */
static void
tcp_reorder_keep(
	struct tcp_endpoint *endpoint,
	struct packet_buf **packet,
	uint32_t sequence)
{
	struct tcp_socket *tcp = &endpoint->tcp;
	struct packet_buf *dropped;
	unsigned long irq;
	uint32_t length;
	uint32_t window;
	uint32_t offset;
	unsigned index;
	unsigned place;
	int before;

	/* The payload's place: within the window this end last advertised. */
	length = (uint32_t)(*packet)->length;
	if (length == 0U)
		return;
	dropped = NULL;
	irq = spin_lock_irqsave(&tcp->inet.socket.lock);

	window = tcp->advertised_window;
	if (window < TCP_RECEIVE_MSS)
		window = TCP_RECEIVE_MSS;
	offset = sequence - tcp->receive_next;
	if (offset == 0U || offset >= window || length > window - offset) {
		spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
		return;
	}

	/* Its place in the order (a segment of the same start is kept already). */
	for (place = 0; place < tcp->reorder_count; place++) {
		if (tcp->reorder_sequence[place] == sequence) {
			spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
			return;
		}

		/* The first one after it, compared modulo 2^32. */
		before = tcp_sequence_before(sequence, tcp->reorder_sequence[place]);
		if (before)
			break;
	}

	/* A full queue: the farthest goes for a nearer one, or the new one is not kept. */
	if (tcp->reorder_count >= TCP_REORDER_LIMIT) {
		if (place >= tcp->reorder_count) {
			spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
			return;
		}

		/* The farthest taken out. */
		tcp->reorder_count--;
		dropped = tcp->reorder[tcp->reorder_count];
		tcp->reorder[tcp->reorder_count] = NULL;
	}

	/* Kept in its place. */
	for (index = tcp->reorder_count; index > place; index--) {
		tcp->reorder[index] = tcp->reorder[index - 1U];
		tcp->reorder_sequence[index] = tcp->reorder_sequence[index - 1U];
		tcp->reorder_length[index] = tcp->reorder_length[index - 1U];
	}

	/* Into the place made. */
	tcp->reorder[place] = *packet;
	tcp->reorder_sequence[place] = sequence;
	tcp->reorder_length[place] = length;
	tcp->reorder_count++;
	tcp->counters.out_of_order_kept++;
	*packet = NULL;

	spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);

	/* Succeeded: the farthest one given up, when one was. */
	if (dropped != NULL)
		packet_buf_free(dropped);
}

/*
 * Moves the kept segments that now follow receive_next into the receive
 * queue, a part already taken cut off, and moves receive_next past them;
 * those wholly taken already are freed.  With discard (the read side shut
 * down) they are taken and freed.  Stops at the next gap, or at a segment
 * the queue has no room for (freed: the peer sends it again).
 */
static void
tcp_reorder_drain(
	struct tcp_endpoint *endpoint,
	int discard)
{
	struct tcp_socket *tcp = &endpoint->tcp;
	struct packet_buf *packet;
	unsigned long irq;
	uint32_t sequence;
	uint32_t length;
	uint32_t taken;
	unsigned index;
	void *pulled;
	int reaches;
	int error;

	/* Each kept segment at the front, while it reaches receive_next. */
	for (;;) {
		irq = spin_lock_irqsave(&tcp->inet.socket.lock);

		if (tcp->reorder_count == 0U) {
			spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
			return;
		}

		/* The front one still lies after receive_next, compared modulo 2^32: the gap is open. */
		reaches = tcp_sequence_at_or_before(tcp->reorder_sequence[0], tcp->receive_next);
		if (!reaches) {
			spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
			return;
		}

		/* The front one taken out, and how much of it was taken already. */
		packet = tcp->reorder[0];
		sequence = tcp->reorder_sequence[0];
		length = tcp->reorder_length[0];
		for (index = 1; index < tcp->reorder_count; index++) {
			tcp->reorder[index - 1U] = tcp->reorder[index];
			tcp->reorder_sequence[index - 1U] = tcp->reorder_sequence[index];
			tcp->reorder_length[index - 1U] = tcp->reorder_length[index];
		}

		/* The queue one shorter. */
		tcp->reorder_count--;
		tcp->reorder[tcp->reorder_count] = NULL;
		taken = tcp->receive_next - sequence;

		spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);

		/* Wholly taken already, or not wanted: freed. */
		if (taken >= length || discard) {
			if (discard && taken < length) {
				irq = spin_lock_irqsave(&tcp->inet.socket.lock);
				if (tcp->receive_next == sequence + taken)
					tcp->receive_next = sequence + length;
				spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
			}

			/* Freed. */
			packet_buf_free(packet);
			continue;
		}

		/* The part taken already cut off. */
		if (taken != 0U) {
			pulled = packet_buf_pull(packet, taken);
			if (pulled == NULL) {
				packet_buf_free(packet);
				return;
			}
		}

		/* The rest queued (a queue without room frees it: the peer sends it again). */
		error = socket_enqueue_stream(&tcp->inet.socket, packet);
		if (error != 0)
			return;

		/* Succeeded for this one: receive_next past it. */
		irq = spin_lock_irqsave(&tcp->inet.socket.lock);

		if (tcp->receive_next == sequence + taken) {
			tcp->receive_next = sequence + length;
			tcp->counters.data_bytes_in += length - taken;
		}

		spin_unlock_irqrestore(&tcp->inet.socket.lock, irq);
	}
}

/* Frees the segments a closing connection kept ahead of a gap. */
static void
tcp_reorder_free(
	struct tcp_endpoint *endpoint)
{
	unsigned index;

	/* Each one kept. */
	for (index = 0; index < endpoint->tcp.reorder_count; index++) {
		packet_buf_free(endpoint->tcp.reorder[index]);
		endpoint->tcp.reorder[index] = NULL;
	}

	/* None kept now. */
	endpoint->tcp.reorder_count = 0;
}
