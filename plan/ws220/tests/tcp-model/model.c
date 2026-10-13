/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host model of a TCP transfer (ws220-p001).
 *
 * Two hosts, A and B, run the kernel's own TCP (src/kern/net/tcp.c with
 * socket.c and packet-buf.c, built as the kernel builds them; shim.c stands
 * in for the rest of the kernel).  A sends a stream to B over a link of a
 * chosen rate, one-way delay and loss, with a device queue of a chosen
 * length on each side, the way a USB adapter queues frames for its one
 * transfer.  The clock is virtual, so a transfer of a gigabyte takes
 * seconds of the host's time.  Every interval the model prints the rate the
 * reader received at and the state and counts of both ends, so the way the
 * rate falls (and why) can be seen; at the end a summary line, and the exit
 * status says whether the whole stream arrived intact.
 *
 *   tcp-model [--bytes N] [--mbps R] [--delay-us D] [--loss-ab P] [--loss-ba P]
 *             [--queue F] [--seed S] [--wrap-before N] [--interval-ms I]
 *             [--limit-s L] [--reader-kbps K] [--peer-window W]
 *             [--overlap-ab P] [--overlap-bytes K] [--blind-at-ms T]
 *             [--blackhole-at-ms T] [--connect-blackhole] [--second-writer]
 *             [--unresolved-at-ms T --unresolved-ms D] [--expect-in-order]
 *             [--quiet]
 *
 * --loss-ab is the share (percent) of the frames from A to B (the data) the
 * link loses, --loss-ba of the frames back (the acknowledgements).
 * --wrap-before N starts A's sequence numbers N bytes before they wrap.
 * --peer-window W rewrites the window of every segment from B to A to W,
 * as a peer would advertise whose window does not follow its queue (a
 * Linux receiver after its buffer has grown): A's sender then sees no
 * window open while segments are outstanding.
 * --overlap-ab P moves the start of that share (percent) of A's data
 * segments K bytes (--overlap-bytes, default 100) back, filled with the
 * stream's bytes before it, as a peer that cuts its resends differently
 * would send them (ws220-p002): B must take the part it has not taken yet.
 * --blind-at-ms T gives B, at T, segments that look like A's but that A
 * did not send: a reset within the window but not at the next sequence
 * number, a reset outside the window, and a SYN (RFC 5961); none may end
 * the connection.
 * --blackhole-at-ms T makes the link lose everything from T on, so the
 * time A takes to give up can be seen (RFC 1122's R2, at least 100 s);
 * --connect-blackhole loses A's SYN from the start and reports when the
 * connect gives up (R2 for a SYN, at least 3 minutes).
 *
 * Each frame that leaves a device's queue runs a pass of the network
 * worker, as a USB adapter's transmit completion does
 * (net_device_schedule_poll): what the device refused is sent again then
 * (ws220-p003).
 * --second-writer makes a second writer write on A's connection whenever
 * A's device is given a data segment, while the writer (or the worker) that
 * gave it waits for the device's answer: two threads writing one socket.
 * The stream must still arrive whole and, with --expect-in-order on a link
 * that loses nothing, without a segment arriving ahead of a gap at B.
 * --unresolved-at-ms T --unresolved-ms D refuses every frame A gives its
 * device from T for D as IPv4 refuses one for a neighbour still being
 * resolved (EAGAIN); the summary counts the refusals, which a retry every
 * TCP_SEND_RETRY_MS keeps to about D / 10 ms.
 */

#include "kern/net/tcp-socket.h"
#include "kern/net/packet-buf.h"
#include "kern/net/socket.h"
#include "model.h"
#include "src/kern/net/internal.h"

#include <uapi/errno.h>
#include <uapi/netinet.h>
#include <uapi/sysctl.h>

/* The host C library functions the model calls, declared by hand (the kernel headers are on the path). */
extern int printf(const char *format, ...);
extern void *malloc(size_t size);
extern void free(void *pointer);
extern void *memcpy(void *destination, const void *source, size_t count);
extern void *memset(void *destination, int value, size_t count);
extern int strcmp(const char *left, const char *right);
extern double strtod(const char *text, char **end);
extern unsigned long long strtoull(const char *text, char **end, int base);

/* The port B listens on. */
#define MODEL_PORT 5001U

/* The most frames a device queue may be given. */
#define MODEL_QUEUE_MAX 256U

/* A nanosecond time that never comes. */
#define MODEL_NEVER UINT64_MAX

/* How long a writer the stack refused for want of packets sleeps before it tries again (tcp.c's TCP_SEND_RETRY_MS). */
#define MODEL_SEND_RETRY_NS 10000000ULL

/* The bytes a frame takes on the wire beyond the TCP segment: IPv4, Ethernet, its FCS, preamble and gap. */
#define MODEL_FRAME_OVERHEAD 58U

/* One frame on its way: what it carries, between which hosts, and when it arrives. */
struct model_frame {
	struct model_frame *next;
	uint64_t arrive_ns;
	uint32_t source;
	uint32_t destination;
	size_t length;
	uint8_t data[2048];
};

/*
 * One direction of the link: the device queue of the sending host (the
 * packets it still holds, the oldest on the wire), the frames in flight,
 * and what happened to them.
 */
struct model_link {
	struct packet_buf *queue[MODEL_QUEUE_MAX];
	uint32_t queue_source[MODEL_QUEUE_MAX];
	uint32_t queue_destination[MODEL_QUEUE_MAX];
	unsigned queue_first;
	unsigned queue_count;
	uint64_t transmit_end_ns;
	struct model_frame *flight_head;
	struct model_frame *flight_tail;
	double loss;
	uint64_t frames;
	uint64_t queue_full;
	uint64_t lost;
	uint64_t receive_no_packet;
};

/* The model's settings, from the command line. */
struct model_settings {
	uint64_t bytes;
	double mbps;
	uint64_t delay_ns;
	double loss_ab;
	double loss_ba;
	unsigned queue;
	uint64_t seed;
	uint64_t wrap_before;
	uint64_t interval_ns;
	uint64_t limit_ns;
	double reader_kbps;
	unsigned peer_window;
	double overlap_ab;
	unsigned overlap_bytes;
	uint64_t blind_at_ns;
	uint64_t blackhole_at_ns;
	int connect_blackhole;
	int second_writer;
	uint64_t unresolved_at_ns;
	uint64_t unresolved_ns;
	int expect_in_order;
	int quiet;
};

/* The settings in force. */
static struct model_settings settings;

/* The two directions: 0 from A to B, 1 from B to A. */
static struct model_link links[2];

/* The time now, in nanoseconds. */
static uint64_t now_ns;

/* The state of the random numbers that decide losses. */
static uint64_t random_state;

/* The sockets: A's connection, B's listener, and B's accepted connection. */
static struct socket *sender;
static struct socket *listener;
static struct socket *receiver;

/* The writer on A: bytes still to write, the next byte's offset, and when it may try again after a refusal. */
static uint64_t send_left;
static uint64_t send_offset;
static uint64_t send_sleep_until_ns;

/*
 * The bytes of the writes still inside the stack (a second writer's place in
 * the stream is after them), and whether a second writer is writing now.
 */
static uint64_t writer_reserved;
static int second_writer_active;

/* A frame left a device's queue since the last pass of the network worker, which its completion runs. */
static int worker_pass;

/* When the transfer began, and the frames A's device refused as for an unresolved neighbour. */
static uint64_t transfer_start_ns;
static uint64_t unresolved_refusals;

/* The reader on B: bytes read, bytes that differed from what was written, and the bytes it may read now. */
static uint64_t received;
static uint64_t corrupt;
static double read_budget;
static uint64_t read_budget_ns;

/* The writer's or the reader's error, which ends the transfer. */
static int transfer_error;

/* The sequence number of the stream's first byte on A, which places a segment in the stream. */
static uint32_t stream_sequence;

/* How many of A's segments were moved back to overlap, and whether the blind segments were given. */
static uint64_t overlapped;
static int blind_given;

static int tcp_socket_create_inet(int type, int protocol, struct socket **result);
static void parse_settings(int argc, char **argv);
static uint64_t isn_tick_base(uint64_t wrap_before);
static double random_unit(void);
static uint8_t stream_byte(uint64_t offset);
static uint64_t frame_time_ns(size_t length);
static void link_start(struct model_link *link, uint64_t start_ns);
static void link_advance(struct model_link *link);
static void rewrite_window(struct model_frame *frame);
static void overlap_frame(struct model_frame *frame);
static void give_blind_segments(void);
static void give_blind_segment(uint32_t sequence, uint32_t acknowledgement, uint8_t flags);
static void run_applications(void);
static void run_writer(void);
static void run_reader(void);
static int connect_hosts(void);
static void step(void);
static void report(uint64_t elapsed_ns, uint64_t interval_bytes);
static struct tcp_socket *tcp_of(struct socket *socket);
static int check_tcpstats(void);

/*
 * Runs the transfer and reports how it went.
 */
int
main(
	int argc,
	char **argv)
{
	struct socket_family_ops family;
	uint64_t next_report_ns;
	uint64_t last_received;
	uint64_t progress_mark;
	uint64_t last_progress_ns;
	uint64_t longest_stall_ns;
	uint64_t start_ns;
	uint64_t interval_bytes;
	uint64_t out_of_order;
	double rate;
	double min_rate;
	double mean;
	unsigned slow_intervals;
	unsigned intervals;
	int error;
	int complete;

	/* The settings, and the clock that sets the sender's first sequence number. */
	parse_settings(argc, argv);
	random_state = settings.seed * 2654435761ULL + 1U;
	model_kernel_init(isn_tick_base(settings.wrap_before));
	links[0].loss = settings.loss_ab / 100.0;
	links[1].loss = settings.loss_ba / 100.0;
	links[0].transmit_end_ns = MODEL_NEVER;
	links[1].transmit_end_ns = MODEL_NEVER;

	/* The kernel's socket core, packet pool and TCP, with IPv4 sockets made by TCP. */
	socket_core_init();
	packet_buf_pool_init();
	error = tcp_init();
	if (error != 0) {
		printf("tcp-model: tcp_init failed (%d)\n", error);
		return 2;
	}
	family.create = tcp_socket_create_inet;
	(void)socket_family_register(AF_INET, &family);

	/* The connection; with its SYN lost, only the time the connect gives up is wanted. */
	if (settings.connect_blackhole)
		links[0].loss = 1.0;
	error = connect_hosts();
	if (settings.connect_blackhole) {
		printf("tcp-model: summary connect gave up after %.3f s (state %d, error %d) complete=%d mean_mbs=0\n",
		    (double)now_ns / 1e9, (int)tcp_of(sender)->state, error, error != 0 && now_ns >= 180000000000ULL);
		return 0;
	}
	if (error != 0) {
		printf("tcp-model: no connection (%d)\n", error);
		return 2;
	}
	stream_sequence = tcp_of(sender)->send_unacknowledged;
	transfer_start_ns = now_ns;
	printf("tcp-model: connected at %llu us, A's first sequence number %u\n",
	    (unsigned long long)(now_ns / 1000U), stream_sequence);

	/* The transfer, reported each interval until the reader has it all or the time is up. */
	send_left = settings.bytes;
	start_ns = now_ns;
	next_report_ns = now_ns + settings.interval_ns;
	last_received = 0;
	progress_mark = 0;
	last_progress_ns = now_ns;
	longest_stall_ns = 0;
	min_rate = -1.0;
	slow_intervals = 0;
	intervals = 0;
	while (received < settings.bytes && transfer_error == 0 && now_ns - start_ns < settings.limit_ns) {
		step();

		/* The link that loses everything from its time on. */
		if (settings.blackhole_at_ns != 0 && now_ns - start_ns >= settings.blackhole_at_ns) {
			links[0].loss = 1.0;
			links[1].loss = 1.0;
		}

		/* The blind segments, once, at their time. */
		if (settings.blind_at_ns != 0 && !blind_given && now_ns - start_ns >= settings.blind_at_ns) {
			give_blind_segments();
			blind_given = 1;
		}

		/* The longest time the reader got nothing. */
		if (received != progress_mark) {
			if (now_ns - last_progress_ns > longest_stall_ns)
				longest_stall_ns = now_ns - last_progress_ns;
			last_progress_ns = now_ns;
			progress_mark = received;
		}

		/* An interval's report: the rate, and the state of both ends. */
		if (now_ns >= next_report_ns) {
			interval_bytes = received - last_received;
			rate = (double)interval_bytes / 1048576.0 / ((double)settings.interval_ns / 1e9);
			if (min_rate < 0.0 || rate < min_rate)
				min_rate = rate;
			if (rate < 1.0)
				slow_intervals++;
			intervals++;
			if (!settings.quiet)
				report(now_ns - start_ns, interval_bytes);
			last_received = received;
			next_report_ns += settings.interval_ns;
		}
	}

	/* A stall still going at the end counts too. */
	if (now_ns - last_progress_ns > longest_stall_ns)
		longest_stall_ns = now_ns - last_progress_ns;

	/* The summary. */
	complete = received == settings.bytes && corrupt == 0;
	mean = 0.0;
	if (now_ns > start_ns)
		mean = (double)received / 1048576.0 / ((double)(now_ns - start_ns) / 1e9);
	/* B's segments that came ahead of a gap. */
	out_of_order = 0;
	if (receiver != NULL)
		out_of_order = tcp_of(receiver)->counters.out_of_order_in;

	/* The summary line, which run.sh reads. */
	printf("tcp-model: summary bytes=%llu received=%llu corrupt=%llu seconds=%.3f mean_mbs=%.2f min_interval_mbs=%.2f "
	    "intervals=%u below_1mbs=%u longest_stall_ms=%.1f overlapped=%llu b_out_of_order=%llu unresolved_refusals=%llu "
	    "error=%d complete=%d\n",
	    (unsigned long long)settings.bytes, (unsigned long long)received, (unsigned long long)corrupt,
	    (double)(now_ns - start_ns) / 1e9, mean, min_rate < 0.0 ? 0.0 : min_rate, intervals, slow_intervals,
	    (double)longest_stall_ns / 1e6, (unsigned long long)overlapped,
	    (unsigned long long)out_of_order, (unsigned long long)unresolved_refusals, transfer_error, complete);
	report(now_ns - start_ns, 0);

	/* A link that loses nothing delivers A's segments in the order A queued them. */
	if (settings.expect_in_order && out_of_order != 0) {
		printf("check: FAIL: %llu segments reached B ahead of a gap on a link that loses nothing\n",
		    (unsigned long long)out_of_order);
		return 1;
	}

	/* net.tcpstats as the kernel copies it out must agree with the sockets (of a connection still up). */
	if (transfer_error == 0) {
		error = check_tcpstats();
		if (error != 0)
			return 1;
	}

	/* Reports whether the whole stream arrived as it was written. */
	if (!complete)
		return 1;

	/* Succeeded: the reader has every byte. */
	return 0;
}

/* Creates a TCP socket for the IPv4 family, as the internet layer does. */
static int
tcp_socket_create_inet(
	int type,
	int protocol,
	struct socket **result)
{
	int error;

	(void)type;

	/* A TCP socket. */
	error = tcp_socket_create(protocol, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Takes a segment IPv4 routed out of a host's device: queued for the wire
 * when the device has room, refused (and freed) when it does not.
 */
int
model_link_output(
	unsigned host,
	struct packet_buf *packet,
	uint32_t source,
	uint32_t destination)
{
	struct model_link *link;
	unsigned slot;
	size_t header;

	link = &links[host];

	/* A second writer writes while this segment's sender waits for the device. */
	header = (size_t)(packet->data[12] >> 4) * 4U;
	if (settings.second_writer && host == 0U && !second_writer_active && receiver != NULL &&
	    packet->length > header) {
		second_writer_active = 1;
		run_writer();
		second_writer_active = 0;
	}

	/* IPv4 refuses a frame for a neighbour still being resolved, and the frame is lost. */
	if (host == 0U && settings.unresolved_ns != 0 && transfer_start_ns != 0 &&
	    now_ns >= transfer_start_ns + settings.unresolved_at_ns &&
	    now_ns < transfer_start_ns + settings.unresolved_at_ns + settings.unresolved_ns) {
		unresolved_refusals++;
		packet_buf_free(packet);
		return EAGAIN;
	}

	/* A full device refuses the frame, as a USB adapter's driver does. */
	if (link->queue_count >= settings.queue) {
		link->queue_full++;
		packet_buf_free(packet);
		return ENOBUFS;
	}

	/* The frame waits its turn; an idle wire takes it at once. */
	slot = (link->queue_first + link->queue_count) % MODEL_QUEUE_MAX;
	link->queue[slot] = packet;
	link->queue_source[slot] = source;
	link->queue_destination[slot] = destination;
	link->queue_count++;
	if (link->queue_count == 1U)
		link_start(link, now_ns);

	/* Succeeded: the device has the frame. */
	return 0;
}

/* Reads the command line into the settings. */
static void
parse_settings(
	int argc,
	char **argv)
{
	int index;

	/* The defaults: 64 MiB over a gigabit LAN with a USB adapter's queue. */
	settings.bytes = 64ULL * 1048576ULL;
	settings.mbps = 1000.0;
	settings.delay_ns = 100000U;
	settings.loss_ab = 0.0;
	settings.loss_ba = 0.0;
	settings.queue = 9U;
	settings.seed = 1U;
	settings.wrap_before = 0;
	settings.interval_ns = 1000000000ULL;
	settings.limit_ns = 600ULL * 1000000000ULL;
	settings.reader_kbps = 0.0;
	settings.peer_window = 0;
	settings.overlap_ab = 0.0;
	settings.overlap_bytes = 100U;
	settings.blind_at_ns = 0;
	settings.blackhole_at_ns = 0;
	settings.connect_blackhole = 0;
	settings.second_writer = 0;
	settings.unresolved_at_ns = 0;
	settings.unresolved_ns = 0;
	settings.expect_in_order = 0;
	settings.quiet = 0;

	/* Each option and its value. */
	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--quiet") == 0) {
			settings.quiet = 1;
			continue;
		}
		if (strcmp(argv[index], "--connect-blackhole") == 0) {
			settings.connect_blackhole = 1;
			continue;
		}
		if (strcmp(argv[index], "--second-writer") == 0) {
			settings.second_writer = 1;
			continue;
		}
		if (strcmp(argv[index], "--expect-in-order") == 0) {
			settings.expect_in_order = 1;
			continue;
		}
		if (index + 1 >= argc)
			break;
		if (strcmp(argv[index], "--bytes") == 0)
			settings.bytes = strtoull(argv[index + 1], NULL, 0);
		else if (strcmp(argv[index], "--mbps") == 0)
			settings.mbps = strtod(argv[index + 1], NULL);
		else if (strcmp(argv[index], "--delay-us") == 0)
			settings.delay_ns = (uint64_t)(strtod(argv[index + 1], NULL) * 1000.0);
		else if (strcmp(argv[index], "--loss-ab") == 0)
			settings.loss_ab = strtod(argv[index + 1], NULL);
		else if (strcmp(argv[index], "--loss-ba") == 0)
			settings.loss_ba = strtod(argv[index + 1], NULL);
		else if (strcmp(argv[index], "--queue") == 0)
			settings.queue = (unsigned)strtoull(argv[index + 1], NULL, 0);
		else if (strcmp(argv[index], "--seed") == 0)
			settings.seed = strtoull(argv[index + 1], NULL, 0);
		else if (strcmp(argv[index], "--wrap-before") == 0)
			settings.wrap_before = strtoull(argv[index + 1], NULL, 0);
		else if (strcmp(argv[index], "--interval-ms") == 0)
			settings.interval_ns = strtoull(argv[index + 1], NULL, 0) * 1000000ULL;
		else if (strcmp(argv[index], "--limit-s") == 0)
			settings.limit_ns = strtoull(argv[index + 1], NULL, 0) * 1000000000ULL;
		else if (strcmp(argv[index], "--reader-kbps") == 0)
			settings.reader_kbps = strtod(argv[index + 1], NULL);
		else if (strcmp(argv[index], "--peer-window") == 0)
			settings.peer_window = (unsigned)strtoull(argv[index + 1], NULL, 0);
		else if (strcmp(argv[index], "--overlap-ab") == 0)
			settings.overlap_ab = strtod(argv[index + 1], NULL);
		else if (strcmp(argv[index], "--overlap-bytes") == 0)
			settings.overlap_bytes = (unsigned)strtoull(argv[index + 1], NULL, 0);
		else if (strcmp(argv[index], "--blackhole-at-ms") == 0)
			settings.blackhole_at_ns = strtoull(argv[index + 1], NULL, 0) * 1000000ULL;
		else if (strcmp(argv[index], "--blind-at-ms") == 0)
			settings.blind_at_ns = strtoull(argv[index + 1], NULL, 0) * 1000000ULL;
		else if (strcmp(argv[index], "--unresolved-at-ms") == 0)
			settings.unresolved_at_ns = strtoull(argv[index + 1], NULL, 0) * 1000000ULL;
		else if (strcmp(argv[index], "--unresolved-ms") == 0)
			settings.unresolved_ns = strtoull(argv[index + 1], NULL, 0) * 1000000ULL;
		index++;
	}

	/* A queue the model can hold. */
	if (settings.queue == 0 || settings.queue > MODEL_QUEUE_MAX)
		settings.queue = MODEL_QUEUE_MAX;
}

/*
 * Finds the tick to start the clock at so that A's first data byte lies
 * wrap_before bytes before the sequence numbers wrap (0: an ordinary start).
 *
 * tcp_connect() takes the initial sequence number as
 * (tick * 1103515245 + port) ^ (generation * 2654435761); A's port is the
 * first ephemeral one (49152) and its first connect is generation 1.  The
 * multiplier is odd, so it has an inverse modulo 2^32.
 */
static uint64_t
isn_tick_base(
	uint64_t wrap_before)
{
	uint32_t inverse;
	uint32_t wanted;
	uint32_t product;
	unsigned round;

	/* An ordinary start at a second after boot. */
	if (wrap_before == 0)
		return 1000U;

	/* The inverse of the multiplier, by Newton's iteration. */
	inverse = 1103515245U;
	for (round = 0; round < 5U; round++)
		inverse *= 2U - 1103515245U * inverse;

	/* The SYN takes one number before the first data byte. */
	wanted = (uint32_t)(0x100000000ULL - wrap_before) - 1U;
	product = (wanted ^ 2654435761U) - 49152U;
	return (uint64_t)(uint32_t)(product * inverse);
}

/* A uniform random number in [0, 1), from a 64-bit xorshift. */
static double
random_unit(
	void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (double)(random_state >> 11) / 9007199254740992.0;
}

/* The byte of the stream at an offset, so the reader can tell a byte out of place. */
static uint8_t
stream_byte(
	uint64_t offset)
{
	return (uint8_t)((offset * 2654435761ULL) >> 24);
}

/* The time a segment of this length takes on the wire. */
static uint64_t
frame_time_ns(
	size_t length)
{
	size_t bytes;

	/* The frame on the wire, never shorter than Ethernet's minimum with its preamble and gap. */
	bytes = length + MODEL_FRAME_OVERHEAD;
	if (bytes < 84U)
		bytes = 84U;
	return (uint64_t)((double)bytes * 8.0 * 1000.0 / settings.mbps);
}

/* Puts the oldest queued frame on the wire. */
static void
link_start(
	struct model_link *link,
	uint64_t start_ns)
{
	struct packet_buf *packet;

	packet = link->queue[link->queue_first];
	link->transmit_end_ns = start_ns + frame_time_ns(packet->length);
}

/*
 * Finishes what the link has done by now: frames off the wire (lost, or on
 * their way), and frames that have arrived (given to TCP).
 */
static void
link_advance(
	struct model_link *link)
{
	struct model_frame *frame;
	struct packet_buf *packet;
	uint64_t end_ns;
	int error;

	/* Each frame whose transmission has ended leaves the device. */
	while (link->queue_count != 0 && link->transmit_end_ns <= now_ns) {
		packet = link->queue[link->queue_first];
		end_ns = link->transmit_end_ns;
		link->frames++;

		/* The link loses it, or carries it to the other host. */
		if (random_unit() < link->loss) {
			link->lost++;
		} else {
			frame = malloc(sizeof(*frame));
			frame->next = NULL;
			frame->arrive_ns = end_ns + settings.delay_ns;
			frame->source = link->queue_source[link->queue_first];
			frame->destination = link->queue_destination[link->queue_first];
			frame->length = packet->length;
			memcpy(frame->data, packet->data, packet->length);
			if (link == &links[1] && settings.peer_window != 0)
				rewrite_window(frame);
			if (link == &links[0] && settings.overlap_ab > 0.0 && random_unit() < settings.overlap_ab / 100.0)
				overlap_frame(frame);
			if (link->flight_tail != NULL)
				link->flight_tail->next = frame;
			else
				link->flight_head = frame;
			link->flight_tail = frame;
		}

		/*
		 * The device has copied it and gives the packet back; the next one
		 * follows on the wire, and the completion runs a worker pass.
		 */
		packet_buf_free(packet);
		worker_pass = 1;
		link->queue_first = (link->queue_first + 1U) % MODEL_QUEUE_MAX;
		link->queue_count--;
		link->transmit_end_ns = MODEL_NEVER;
		if (link->queue_count != 0)
			link_start(link, end_ns);
	}

	/* Each frame that has arrived goes to TCP, and the applications see what it did. */
	while (link->flight_head != NULL && link->flight_head->arrive_ns <= now_ns) {
		frame = link->flight_head;
		link->flight_head = frame->next;
		if (link->flight_head == NULL)
			link->flight_tail = NULL;
		error = model_deliver(frame->data, frame->length, frame->source, frame->destination);
		if (error != 0)
			link->receive_no_packet++;
		free(frame);
		run_applications();
	}
}

/* Puts the --peer-window window in a segment from B, with its checksum made again. */
static void
rewrite_window(
	struct model_frame *frame)
{
	uint16_t checksum;

	/* The window field (bytes 14 and 15) and the checksum (16 and 17) of the TCP header. */
	frame->data[14] = (uint8_t)(settings.peer_window >> 8);
	frame->data[15] = (uint8_t)(settings.peer_window & 0xffU);
	frame->data[16] = 0;
	frame->data[17] = 0;
	checksum = net_checksum_pseudo(frame->source, frame->destination, IPPROTO_TCP, frame->data, frame->length);
	frame->data[16] = (uint8_t)(checksum >> 8);
	frame->data[17] = (uint8_t)(checksum & 0xffU);
}

/*
 * Moves the start of a data segment from A --overlap-bytes back, the bytes
 * put in front being the stream's own, with its checksum made again.  A
 * segment too near the stream's start, or one with a SYN or no data, is
 * left as it is.
 */
static void
overlap_frame(
	struct model_frame *frame)
{
	uint8_t moved[2048];
	uint32_t sequence;
	uint32_t offset;
	uint16_t checksum;
	size_t header;
	size_t payload;
	size_t extra;
	size_t index;

	/* Only a data segment, far enough into the stream, with room to grow. */
	header = (size_t)(frame->data[12] >> 4) * 4U;
	if (header > frame->length || (frame->data[13] & 0x02U) != 0)
		return;
	payload = frame->length - header;
	extra = settings.overlap_bytes;
	if (payload == 0 || frame->length + extra > sizeof(frame->data))
		return;
	sequence = ((uint32_t)frame->data[4] << 24) | ((uint32_t)frame->data[5] << 16) |
	    ((uint32_t)frame->data[6] << 8) | (uint32_t)frame->data[7];
	offset = sequence - stream_sequence;
	if (offset < extra)
		return;

	/* The header with the earlier sequence number, the stream's bytes before the payload, then the payload. */
	memcpy(moved, frame->data, header);
	sequence -= (uint32_t)extra;
	moved[4] = (uint8_t)(sequence >> 24);
	moved[5] = (uint8_t)(sequence >> 16);
	moved[6] = (uint8_t)(sequence >> 8);
	moved[7] = (uint8_t)sequence;
	for (index = 0; index < extra; index++)
		moved[header + index] = stream_byte((uint64_t)(offset - extra) + index);
	memcpy(moved + header + extra, frame->data + header, payload);
	frame->length += extra;
	memcpy(frame->data, moved, frame->length);

	/* The checksum over the segment as it now is. */
	frame->data[16] = 0;
	frame->data[17] = 0;
	checksum = net_checksum_pseudo(frame->source, frame->destination, IPPROTO_TCP, frame->data, frame->length);
	frame->data[16] = (uint8_t)(checksum >> 8);
	frame->data[17] = (uint8_t)(checksum & 0xffU);
	overlapped++;
}

/*
 * Gives B the segments of someone who knows the connection's addresses and
 * ports but not its sequence numbers: a reset within the window B
 * advertised but not at its next sequence number, a reset far outside it,
 * and a SYN.  B answers the first and the SYN with an acknowledgement
 * (RFC 5961 sections 3.2 and 4) and ignores the second; none resets.
 */
static void
give_blind_segments(
	void)
{
	struct tcp_socket *b;

	/* The numbers B expects now, which a blind sender does not know exactly. */
	b = tcp_of(receiver);
	give_blind_segment(b->receive_next + 1000U, b->send_next, 0x04U);
	give_blind_segment(b->receive_next + 0x80000000U, b->send_next, 0x04U);
	give_blind_segment(b->receive_next - 5000U, 0, 0x02U);
	printf("tcp-model: blind segments given at receive_next %u, B state %d\n", b->receive_next, (int)b->state);
}

/* Gives B one segment from A's address and port with the numbers and flags given. */
static void
give_blind_segment(
	uint32_t sequence,
	uint32_t acknowledgement,
	uint8_t flags)
{
	uint8_t segment[20];
	uint16_t port;
	uint16_t checksum;

	/* The header: A's port to B's, the numbers, no options, the flags and a window. */
	memset(segment, 0, sizeof(segment));
	port = tcp_of(sender)->inet.local_port;
	segment[0] = (uint8_t)(port >> 8);
	segment[1] = (uint8_t)port;
	segment[2] = (uint8_t)(MODEL_PORT >> 8);
	segment[3] = (uint8_t)MODEL_PORT;
	segment[4] = (uint8_t)(sequence >> 24);
	segment[5] = (uint8_t)(sequence >> 16);
	segment[6] = (uint8_t)(sequence >> 8);
	segment[7] = (uint8_t)sequence;
	segment[8] = (uint8_t)(acknowledgement >> 24);
	segment[9] = (uint8_t)(acknowledgement >> 16);
	segment[10] = (uint8_t)(acknowledgement >> 8);
	segment[11] = (uint8_t)acknowledgement;
	segment[12] = 5U << 4;
	segment[13] = flags;
	if (acknowledgement != 0)
		segment[13] |= 0x10U;
	segment[14] = 0xffU;
	segment[15] = 0xffU;
	checksum = net_checksum_pseudo(MODEL_ADDRESS_A, MODEL_ADDRESS_B, IPPROTO_TCP, segment, sizeof(segment));
	segment[16] = (uint8_t)(checksum >> 8);
	segment[17] = (uint8_t)(checksum & 0xffU);

	/* B takes it as it takes any segment from the link. */
	(void)model_deliver(segment, sizeof(segment), MODEL_ADDRESS_A, MODEL_ADDRESS_B);
	run_applications();
}

/* Lets the writer and the reader act on what has changed. */
static void
run_applications(
	void)
{
	run_writer();
	run_reader();
}

/*
 * Writes as much of the stream as the stack takes now.
 *
 * A write the stack refuses although its ring and the peer's window have
 * room was refused for want of packets: the kernel's writer then sleeps
 * TCP_SEND_RETRY_MS, and so does this one.  Otherwise it waits for an
 * acknowledgement, which the next event brings.  A second writer
 * (--second-writer), called while a write is still inside the stack, writes
 * one piece after the bytes of that write.
 */
static void
run_writer(
	void)
{
	struct tcp_socket *tcp;
	uint8_t chunk[1024];
	uint64_t length;
	uint64_t index;
	uint64_t base;
	uint32_t window;
	uint32_t in_flight;
	ssize_t written;

	/* A writer asleep, or done. */
	if (sender == NULL || now_ns < send_sleep_until_ns)
		return;

	/* Each piece the stack takes, after the bytes of any write still inside it. */
	while (send_left > writer_reserved) {
		length = send_left - writer_reserved;
		if (length > sizeof(chunk))
			length = sizeof(chunk);
		base = send_offset + writer_reserved;
		for (index = 0; index < length; index++)
			chunk[index] = stream_byte(base + index);
		writer_reserved += length;
		written = sender->ops->sendto(sender, chunk, (size_t)length, MSG_DONTWAIT, NULL, 0);
		writer_reserved -= length;
		if (written > 0) {
			send_offset += (uint64_t)written;
			send_left -= (uint64_t)written;
			if (second_writer_active)
				return;
			continue;
		}

		/* An error other than a full ring or window ends the transfer. */
		if (written != -EAGAIN) {
			transfer_error = (int)-written;
			return;
		}

		/* A refusal with room left is a shortage of packets: the kernel's writer sleeps. */
		tcp = tcp_of(sender);
		window = tcp->peer_window;
		if (window == 0)
			window = 1024U;
		in_flight = tcp->send_next - tcp->send_unacknowledged;
		if (tcp->send_count < TCP_SEND_QUEUE_MAX && (in_flight == 0 || in_flight + (uint32_t)length <= window))
			send_sleep_until_ns = now_ns + MODEL_SEND_RETRY_NS;
		return;
	}
}

/* Reads what has arrived, as fast as the reader's rate allows. */
static void
run_reader(
	void)
{
	uint8_t buffer[65536];
	ssize_t got;
	size_t want;
	ssize_t index;

	/* No reader before the connection is accepted. */
	if (receiver == NULL)
		return;

	/* The reader's allowance grows with the time since it was last given. */
	if (settings.reader_kbps > 0.0) {
		read_budget += (double)(now_ns - read_budget_ns) * settings.reader_kbps * 1000.0 / 8.0 / 1e9;
		if (read_budget > 65536.0)
			read_budget = 65536.0;
		read_budget_ns = now_ns;
	}

	/* Each read until nothing is left (or the allowance is spent). */
	for (;;) {
		want = sizeof(buffer);
		if (settings.reader_kbps > 0.0) {
			if (read_budget < 1.0)
				return;
			if ((double)want > read_budget)
				want = (size_t)read_budget;
		}
		got = receiver->ops->recvfrom(receiver, buffer, want, MSG_DONTWAIT, NULL, NULL);
		if (got <= 0) {
			if (got < 0 && got != -EAGAIN)
				transfer_error = (int)-got;
			return;
		}

		/* Each byte checked against the stream. */
		for (index = 0; index < got; index++) {
			if (buffer[index] != stream_byte(received + (uint64_t)index))
				corrupt++;
		}
		received += (uint64_t)got;
		if (settings.reader_kbps > 0.0)
			read_budget -= (double)got;
	}
}

/*
 * Makes the connection: B listens, A connects without waiting, and the
 * link carries the handshake until B accepts.
 */
static int
connect_hosts(
	void)
{
	struct sockaddr_in address;
	uint64_t limit_ns;
	int error;

	/* B's listener on its port, every address. */
	error = socket_create(AF_INET, SOCK_STREAM, 0, &listener);
	if (error != 0)
		return error;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = model_ntohs(MODEL_PORT);
	error = listener->ops->bind(listener, (struct sockaddr *)&address, sizeof(address));
	if (error != 0)
		return error;
	error = listener->ops->listen(listener, 4);
	if (error != 0)
		return error;

	/* A's connect to B, which completes later. */
	error = socket_create(AF_INET, SOCK_STREAM, 0, &sender);
	if (error != 0)
		return error;
	address.sin_addr.s_addr = model_ntohl(MODEL_ADDRESS_B);
	error = sender->ops->connect(sender, (struct sockaddr *)&address, sizeof(address), SOCKET_IO_NONBLOCK);
	if (error != EINPROGRESS)
		return error;

	/* The handshake, for at most ten seconds (400 when the SYN is lost on purpose, until the connect gives up). */
	limit_ns = 10000000000ULL;
	if (settings.connect_blackhole)
		limit_ns = 400000000000ULL;
	while (now_ns < limit_ns) {
		step();
		if (tcp_of(sender)->state == TCP_CLOSED)
			return ETIMEDOUT;
		if (receiver == NULL)
			(void)listener->ops->accept(listener, &receiver, NULL, NULL, SOCKET_IO_NONBLOCK);
		if (receiver != NULL && tcp_of(sender)->state == TCP_ESTABLISHED)
			return 0;
	}

	/* The handshake did not finish. */
	return ETIMEDOUT;
}

/*
 * Moves the clock to the next thing that happens (a frame leaving a
 * device, a frame arriving, the kernel's tick, the writer waking) and does
 * it.
 */
static void
step(
	void)
{
	uint64_t next_ns;
	uint64_t tick_ns;
	unsigned direction;

	/* The next tick of the kernel's clock, and the earliest event before it. */
	tick_ns = (now_ns / 1000000ULL + 1U) * 1000000ULL;
	next_ns = tick_ns;
	for (direction = 0; direction < 2U; direction++) {
		if (links[direction].queue_count != 0 && links[direction].transmit_end_ns < next_ns)
			next_ns = links[direction].transmit_end_ns;
		if (links[direction].flight_head != NULL && links[direction].flight_head->arrive_ns < next_ns)
			next_ns = links[direction].flight_head->arrive_ns;
	}
	if (send_sleep_until_ns > now_ns && send_sleep_until_ns < next_ns)
		next_ns = send_sleep_until_ns;

	/* The clock moves on; the shim's clock is in microseconds. */
	if (next_ns > now_ns)
		now_ns = next_ns;
	model_clock_set(now_ns / 1000U);

	/* What the links did by now. */
	for (direction = 0; direction < 2U; direction++)
		link_advance(&links[direction]);

	/* A tick, or a device's transmit completion: the network worker's pass, then the applications. */
	if (now_ns == tick_ns || worker_pass) {
		worker_pass = 0;
		tcp_timer_run();
		run_applications();
	}

	/* A writer waking from its sleep. */
	if (send_sleep_until_ns != 0 && now_ns >= send_sleep_until_ns) {
		send_sleep_until_ns = 0;
		run_applications();
	}
}

/* Prints one interval: the rate, and what both ends and the links show. */
static void
report(
	uint64_t elapsed_ns,
	uint64_t interval_bytes)
{
	struct tcp_socket *a;
	struct tcp_socket *b;
	struct packet_buf_pool_usage usage;
	double rate;

	a = tcp_of(sender);
	b = receiver != NULL ? tcp_of(receiver) : NULL;
	packet_buf_pool_usage(&usage);
	rate = (double)interval_bytes / 1048576.0 / ((double)settings.interval_ns / 1e9);
	printf("t=%.3f mbs=%.2f received=%llu a.state=%d a.in_flight=%u a.ring=%u a.peer_window=%u a.backoff=%u "
	    "a.retransmits=%llu a.immediate=%llu a.dupacks_in=%llu a.acks_outside=%llu a.refused=%llu a.send_fail=%llu "
	    "b.window=%u b.reorder=%u b.ooo_in=%llu b.ooo_kept=%llu b.old_in=%llu b.refused=%llu b.send_fail=%llu "
	    "link.ab.lost=%llu link.ab.full=%llu link.ba.lost=%llu link.ba.full=%llu rx_nobuf=%llu pool=%u/%u max=%u fail=%llu\n",
	    (double)elapsed_ns / 1e9, rate, (unsigned long long)received, (int)a->state, a->send_next - a->send_unacknowledged,
	    a->send_count, a->peer_window, a->retransmit_count,
	    (unsigned long long)a->counters.retransmit_segments, (unsigned long long)a->counters.immediate_resends,
	    (unsigned long long)a->counters.duplicate_acks_in, (unsigned long long)a->counters.acks_outside,
	    (unsigned long long)a->counters.send_refused, (unsigned long long)a->counters.segment_send_failures,
	    b != NULL ? b->advertised_window : 0U, b != NULL ? b->reorder_count : 0U,
	    (unsigned long long)(b != NULL ? b->counters.out_of_order_in : 0U),
	    (unsigned long long)(b != NULL ? b->counters.out_of_order_kept : 0U),
	    (unsigned long long)(b != NULL ? b->counters.old_segments_in : 0U),
	    (unsigned long long)(b != NULL ? b->counters.receive_refused : 0U),
	    (unsigned long long)(b != NULL ? b->counters.segment_send_failures : 0U),
	    (unsigned long long)links[0].lost, (unsigned long long)links[0].queue_full,
	    (unsigned long long)links[1].lost, (unsigned long long)links[1].queue_full,
	    (unsigned long long)(links[0].receive_no_packet + links[1].receive_no_packet),
	    usage.used, usage.total, usage.used_max, (unsigned long long)usage.alloc_failures);
}

/*
 * Reads net.tcpstats through tcp_stats_copy(), as sysctl does: the length
 * first, then the value, and checks it against the sockets: three of them
 * (the listener and the two ends), and the bytes A queued and B took in
 * order as the counts say.  Prints one line and reports 0 when they agree.
 */
static int
check_tcpstats(
	void)
{
	static uint8_t buffer[sizeof(struct tcp_stats_header) + 8U * sizeof(struct tcp_stats_entry)];
	const struct tcp_stats_header *header;
	const struct tcp_stats_entry *entry;
	uint64_t sent;
	uint64_t taken;
	size_t needed;
	unsigned index;
	int error;

	/* The length, then the value. */
	error = tcp_stats_copy(NULL, 0, &needed, 1);
	if (error != 0 || needed > sizeof(buffer)) {
		printf("tcpstats: FAIL: length %llu (%d)\n", (unsigned long long)needed, error);
		return 1;
	}
	error = tcp_stats_copy(buffer, sizeof(buffer), &needed, 1);
	if (error != 0) {
		printf("tcpstats: FAIL: copy (%d)\n", error);
		return 1;
	}

	/* The layout and the sockets' counts. */
	header = (const struct tcp_stats_header *)(const void *)buffer;
	sent = 0;
	taken = 0;
	for (index = 0; index < header->count; index++) {
		entry = (const struct tcp_stats_entry *)(const void *)(buffer + header->struct_size + index * header->element_size);
		if (entry->local_port != MODEL_PORT && entry->state == TCP_ESTABLISHED)
			sent = entry->data_bytes_out;
		if (entry->local_port == MODEL_PORT && entry->state == TCP_ESTABLISHED)
			taken = entry->data_bytes_in;
	}
	if (header->version != TCP_STATS_VERSION || header->count != 3U ||
	    needed != sizeof(*header) + 3U * sizeof(*entry) || sent != send_offset || taken != received) {
		printf("tcpstats: FAIL: version %u count %u sent %llu (wrote %llu) taken %llu (read %llu)\n", header->version,
		    header->count, (unsigned long long)sent, (unsigned long long)send_offset, (unsigned long long)taken,
		    (unsigned long long)received);
		return 1;
	}

	/* The superuser is given the sequence numbers (ws220-p002). */
	for (index = 0; index < header->count; index++) {
		entry = (const struct tcp_stats_entry *)(const void *)(buffer + header->struct_size + index * header->element_size);
		if ((entry->features & TCP_STATS_HAVE_SEQUENCE) == 0) {
			printf("tcpstats: FAIL: the superuser's entry %u has no sequence numbers\n", index);
			return 1;
		}
	}

	/* Any other reader is given every socket without them. */
	error = tcp_stats_copy(buffer, sizeof(buffer), &needed, 0);
	if (error != 0 || header->count != 3U) {
		printf("tcpstats: FAIL: copy for a user (%d)\n", error);
		return 1;
	}
	for (index = 0; index < header->count; index++) {
		entry = (const struct tcp_stats_entry *)(const void *)(buffer + header->struct_size + index * header->element_size);
		if ((entry->features & TCP_STATS_HAVE_SEQUENCE) != 0 || entry->send_unacknowledged != 0 ||
		    entry->send_next != 0 || entry->receive_next != 0 || entry->local_port == 0) {
			printf("tcpstats: FAIL: a user's entry %u shows sequence numbers or lacks its port\n", index);
			return 1;
		}
	}

	/* Succeeded: the value agrees with the sockets. */
	printf("tcpstats: ok sockets=%u length=%llu packets_used_max=%u user_hides_sequence=1\n", header->count,
	    (unsigned long long)needed, header->packets_used_max);
	return 0;
}

/* The TCP state of a socket (the socket is the first member of its endpoint). */
static struct tcp_socket *
tcp_of(
	struct socket *socket)
{
	return (struct tcp_socket *)socket;
}
