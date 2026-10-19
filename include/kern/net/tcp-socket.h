/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * TCP socket
 */

#ifndef KERN_KERN_NET_TCP_SOCKET_H
#define KERN_KERN_NET_TCP_SOCKET_H

#include "kern/net/inet-socket.h"

enum tcp_state {
	TCP_CLOSED,
	TCP_LISTEN,
	TCP_SYN_SENT,
	TCP_SYN_RECEIVED,
	TCP_ESTABLISHED,
	TCP_FIN_WAIT_1,
	TCP_FIN_WAIT_2,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_TIME_WAIT
};

/*
 * How many segments may be outstanding at once, and one of them.
 *
 * The window this opens is TCP_SEND_QUEUE_MAX segments; the peer's own
 * advertised window narrows it further whenever that is smaller.
 */
#ifndef CONFIG_TCP_SEND_QUEUE_MAX
#define CONFIG_TCP_SEND_QUEUE_MAX 8
#endif
#define TCP_SEND_QUEUE_MAX ((unsigned)CONFIG_TCP_SEND_QUEUE_MAX)

/*
 * How many segments that came ahead of a gap a connection keeps until the
 * gap is filled (BUG-222).  On the 64-bit machines it is the whole receive
 * window (44 segments, tcp.c's TCP_RECEIVE_PACKETS): a segment dropped for
 * want of room is one the sender (without SACK) resends a round trip
 * after the one before it, and with the window queued in the USB adapter a
 * round trip is tens of milliseconds (T1-516: a 2 % loss fell to a tenth
 * of the speed).  The window counts only the receive queue's free slots,
 * so the queue and the segments kept ahead stay within about one window
 * of packets together.  The 32-bit boards keep a few (tcp.c).
 */
#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64) || \
    defined(HAL_ARCH_RISCV64)
#define TCP_REORDER_MAX 44U
#else
#define TCP_REORDER_MAX 16U
#endif

/*
 * One segment in the send ring: its copy, where it lies in the sequence
 * space, its TCP flags, and whether it went out as a probe of a window
 * that could not take it (ws220-p005; set when it is marked sent).
 */
struct tcp_pending {
	struct packet_buf *packet;
	uint32_t sequence;
	uint32_t advance;
	uint16_t length;
	uint8_t flags;
	uint8_t probe;
};

/*
 * What one connection has done since its socket was created, which
 * net.tcpstats reports (ws220-p001) to follow a transfer's control over
 * time.  Each count only grows, and is moved and read under the socket
 * lock, so a reader sees the counts of one moment.  The meaning of each is
 * that of the field of the same name in struct tcp_stats_entry
 * (<uapi/sysctl.h>).
 */
struct tcp_counters {
	uint64_t segments_out;
	uint64_t segments_in;
	uint64_t data_segments_out;
	uint64_t data_bytes_out;
	uint64_t data_bytes_in;
	uint64_t retransmit_segments;
	uint64_t retransmit_bytes;
	uint64_t immediate_resends;
	uint64_t send_refused;
	uint64_t send_buffer_sleeps;
	uint64_t segment_send_failures;
	uint64_t acks_in;
	uint64_t duplicate_acks_in;
	uint64_t acks_outside;
	uint64_t zero_windows_in;
	uint64_t zero_windows_out;
	uint64_t window_updates_out;
	uint64_t old_segments_in;
	uint64_t out_of_order_in;
	uint64_t out_of_order_kept;
	uint64_t receive_refused;
	uint64_t fast_retransmits;
};

struct tcp_socket {
	struct inet_socket inet;
	enum tcp_state state;
	uint32_t send_next;
	uint32_t send_unacknowledged;
	uint32_t receive_next;
	uint32_t connect_generation;
	uint32_t active_connect_generation;
	uint64_t connect_wait_deadline;
	uint16_t peer_window;
	/*
	 * The sequence and acknowledgement numbers of the segment peer_window
	 * was last taken from (RFC 9293's SND.WL1 and SND.WL2).  A segment
	 * older than that one, by these two numbers, does not set the window
	 * again, so an acknowledgement overtaken on the way cannot replace the
	 * window with one the peer has already replaced itself.
	 */
	uint32_t send_window_sequence;
	uint32_t send_window_acknowledgement;
	/* The window last put in a segment; a read that opens it says so. */
	uint16_t advertised_window;
	/*
	 * Segments queued and not yet acknowledged, oldest first.  Several
	 * may be in flight at once, bounded by the peer's advertised window
	 * and by the size of this ring; each keeps a copy of its payload so
	 * it can be sent again.  The deadline and the attempt count belong to
	 * the oldest entry, which is the one a timeout resends.
	 *
	 * The last send_unsent entries have not been handed to the device yet
	 * (ws220-p003): the device or the packet pool refused them, or a
	 * refused one came before them.  They go out in order, oldest first,
	 * through the output retry below, never through the retransmission
	 * timer, which the first sent entry starts.  So three positions follow
	 * a connection: send_unacknowledged; the end of what was sent (the
	 * first unsent entry's sequence, or send_next with none: RFC 9293's
	 * SND.NXT); and send_next, the end of what was queued, which the next
	 * segment takes.  Unlike BSD's snd_max, send_next is never behind
	 * what was sent.
	 */
	struct tcp_pending send_queue[CONFIG_TCP_SEND_QUEUE_MAX];
	unsigned send_first;
	unsigned send_count;
	unsigned send_unsent;
	/*
	 * Set while one thread hands unsent entries to the device with the
	 * socket lock released; another that queues meanwhile leaves its
	 * entry unsent for that one to send, so entries go out in order.
	 * Only the thread that set it moves the end of what was sent.
	 */
	unsigned send_flushing;
	/*
	 * Pure acknowledgements the device or the pool refused, which the
	 * output retry sends again, and the receive_next of the last one
	 * refused: while it stands they are duplicates, each of which the
	 * peer counts towards its fast retransmit; once it has moved, one
	 * acknowledgement says all of them.
	 */
	unsigned ack_pending;
	uint32_t ack_pending_receive_next;
	/*
	 * When the output retry runs for the unsent entries and the pending
	 * acknowledgements: on the network worker's next pass after a full
	 * device refused them (the device's transmit completion runs a pass),
	 * and at the deadline in any case.  Both are 0 while nothing waits.
	 */
	unsigned output_retry_on_pass;
	uint64_t output_retry_deadline;
	uint64_t retransmit_deadline;
	unsigned retransmit_count;
	/*
	 * The tick the retransmission timer first found the oldest segment
	 * unacknowledged since the peer last answered, or 0 while no timeout
	 * has run since then.  The attempt is given up once the timer has
	 * gone on resending for RFC 1122's R2 from that tick; an
	 * acknowledgement of any kind sets it back to 0, so a peer that
	 * answers is never given up.
	 */
	uint64_t retransmit_since;
	/*
	 * Set when the device or the pool refused the timer's last resend of
	 * the oldest segment, so a resend refused again is tried after
	 * TCP_SEND_RETRY_MS rather than the next tick (ws220-p003 review-1
	 * M2); 0 again once a resend goes or the timer starts over.
	 */
	unsigned retransmit_refused;
	/*
	 * The round-trip time estimate of RFC 6298 (ws220-p004): how many
	 * samples have been taken (0: none, so the next is the first of
	 * section 2.2), the smoothed round-trip time and its variation in
	 * microseconds, and the retransmission timeout, in clock ticks before
	 * any backoff (0 stands for the initial second; a handshake whose SYN
	 * timed out sets three seconds, section 5.7).
	 *
	 * rto_backoff is how many times that timeout is doubled: one more for
	 * each timeout that really expired, back to 0 only with a valid sample
	 * (Karn's rule, RFC 6298 section 5 note).  retransmit_count above, the
	 * resends of the oldest segment, is a different count (an immediate
	 * resend adds to it, and an acknowledgement that moves sets it back).
	 * retransmit_immediate marks a deadline set to now for an immediate
	 * resend (recovery, a reopened window), which does not double.
	 *
	 * One segment at a time is timed: while rtt_timing is set, the one
	 * ending at rtt_sequence, handed to the device at tick rtt_start; it
	 * is in the ring and marked sent.  A resend by the timer stops the
	 * timing (Karn), as do the segment going unsent again or taken back
	 * and the ring's reset; the next segment sent starts it again.
	 * syn_timed_out is set when the timer resent the SYN (or SYN-ACK), or
	 * a repeated SYN had the SYN-ACK sent again: the handshake then gives
	 * no sample and the data starts with three seconds.
	 */
	uint32_t rtt_samples;
	uint32_t srtt_us;
	uint32_t rttvar_us;
	uint64_t rto;
	unsigned rto_backoff;
	unsigned retransmit_immediate;
	unsigned rtt_timing;
	uint32_t rtt_sequence;
	uint64_t rtt_start;
	unsigned syn_timed_out;
	/*
	 * NewReno's recovery (RFC 6582, ws220-p005): duplicate_acks counts the
	 * duplicate acknowledgements in a row; the third starts a fast
	 * retransmit.  A fast retransmit or a timeout that really expired sets
	 * recover to the end of what was sent and recovery: while it lasts,
	 * each acknowledgement that moves but stops short of recover (a
	 * partial one) has the next segment resent at once, and one that
	 * reaches recover ends it.  recover means nothing outside a recovery
	 * (ws220-p005 review-1 G2: compared later it would turn false across
	 * 2 GiB).  recovery_by_timeout says a timeout started the recovery: when
	 * it ends exactly at recover, dupack_guard holds back the duplicates of
	 * that recover (the answers to the resends of a spurious timeout) until
	 * an acknowledgement goes beyond it (review-1 M1).  Duplicates count
	 * only while the oldest segment fits the window (review-1 G1): the
	 * answers to a closed window are not losses.
	 */
	unsigned duplicate_acks;
	uint32_t recover;
	unsigned recovery;
	unsigned recovery_by_timeout;
	unsigned dupack_guard;
	uint32_t dupack_guard_sequence;
	/*
	 * Each resend by the timer takes the next number, which no
	 * acknowledgement sets back: a refused resend is still the last one
	 * when the number is unchanged (review-1 M4).
	 */
	unsigned retransmit_serial;
	/*
	 * The persist state of a window too small for the oldest segment
	 * (ws220-p005, as FreeBSD has it): while persist is set the timer
	 * probes the window every clamp(RTO x 2^persist_shift, 5 s, 60 s)
	 * (the doubling stops at 512), apart from the retransmission backoff;
	 * persist_shift goes back to 0 when the state is entered and left.
	 * last_receive is the tick of the last segment from the peer: a peer
	 * that stops answering is dropped after 12 probes and an idle of
	 * min(2 h, RTO x 2559), and after 60 s once this end has closed
	 * (FIN_WAIT_1, LAST_ACK).  A peer that keeps answering is kept
	 * (RFC 1122 section 4.2.2.17) unless TCP_MAXUNACKTIME is set:
	 * maxunack (ticks, 0: off; maxunack_seconds as set) drops a
	 * connection with a reset when its data has not moved since
	 * unack_since (the tick data was queued with nothing outstanding, or
	 * an acknowledgement took more than a byte; 0 with nothing
	 * outstanding).
	 */
	unsigned persist;
	unsigned persist_shift;
	uint64_t last_receive;
	uint64_t maxunack;
	unsigned maxunack_seconds;
	uint64_t unack_since;
	/*
	 * TCP_NODELAY as the caller set it, and the idle-probe state for
	 * SO_KEEPALIVE: when the next probe is due and how many have gone
	 * unanswered.
	 */
	unsigned nodelay;
	uint64_t keepalive_deadline;
	unsigned keepalive_probes;
	unsigned listen_backlog;
	unsigned half_open_count;
	unsigned accept_count;
	struct tcp_socket *listener;
	struct tcp_socket *queue_next;
	struct tcp_socket *half_open_head;
	struct tcp_socket *accept_head;
	struct tcp_socket *accept_tail;
	/*
	 * Segments that came ahead of receive_next (BUG-222): their payloads
	 * (the header pulled off), sequence numbers and lengths, ordered by
	 * sequence, kept until the gap before them is filled so that one lost
	 * segment costs the peer one resend, not the whole window.
	 */
	struct packet_buf *reorder[TCP_REORDER_MAX];
	uint32_t reorder_sequence[TCP_REORDER_MAX];
	uint32_t reorder_length[TCP_REORDER_MAX];
	unsigned reorder_count;
	struct tcp_counters counters;
};

int
tcp_init(void);
int
tcp_socket_create(
	int protocol,
	struct socket **result);
int
tcp_stats_copy(
	void *output,
	size_t capacity,
	size_t *needed,
	int superuser);

#endif
