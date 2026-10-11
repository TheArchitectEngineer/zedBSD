/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The bookkeeping of one xHCI isochronous endpoint (ws197-p007a1, design
 * 2.1a of plan/ws197/phase007/phase.md: X1 to X6, X9, X12), without the
 * hardware: the FIFO of requests and their TDs on the transfer ring (one
 * TD a TRB), the free room on the ring, the endpoint's state, and what
 * each Transfer Event does to them.  pci-xhci.c keeps one of these per
 * isochronous endpoint under its active lock and acts on what the calls
 * report; the host test (plan/ws197/tests) runs them alone.
 *
 * The rules (design 2.1a):
 *
 *   - Skip rule: a TD's event ends every earlier TD that has no result
 *     as MISSED.
 *   - Missed Service, Ring Underrun and Ring Overrun end nothing; with
 *     TDs outstanding they put the endpoint in SKIP_WAIT, and the
 *     worker stops the endpoint if no TD event comes in time.
 *   - Stopped and Stopped - Length Invalid only count: the TD they name
 *     runs again from its start after the endpoint is rung.
 *   - While an owner (STOPPING, CANCELLING, RECOVERING) or a halt
 *     (HALT_PENDING) holds the endpoint, a TD event only records its
 *     TD's result; the owner's cleanup ends the TDs.
 */

#ifndef KERN_DRIVERS_PCI_XHCI_ISOCH_H
#define KERN_DRIVERS_PCI_XHCI_ISOCH_H

#include <stdint.h>

/* The most requests queued on an endpoint, and the most TDs (packets) of one. */
#define DRV_XHCI_ISOCH_QUEUE_MAX	4U
#define DRV_XHCI_ISOCH_PACKETS_MAX	32U

/* An interrupt at least every this many TDs; the TDs between carry BEI. */
#define DRV_XHCI_ISOCH_IOC_EVERY	8U

/* The completion codes the bookkeeping tells apart (xHCI 6.4.5). */
#define DRV_XHCI_COMPLETION_SUCCESS		1U
#define DRV_XHCI_COMPLETION_BABBLE		3U
#define DRV_XHCI_COMPLETION_TRANSACTION		4U
#define DRV_XHCI_COMPLETION_SHORT_PACKET	13U
#define DRV_XHCI_COMPLETION_RING_UNDERRUN	14U
#define DRV_XHCI_COMPLETION_RING_OVERRUN	15U
#define DRV_XHCI_COMPLETION_MISSED_SERVICE	23U
#define DRV_XHCI_COMPLETION_STOPPED		26U
#define DRV_XHCI_COMPLETION_STOPPED_LENGTH	27U

/* What an event or a cleanup asks of the driver (bits). */
#define DRV_XHCI_ISOCH_COMPLETE		0x1U	/* requests at the head are done: take them */
#define DRV_XHCI_ISOCH_WAKE		0x2U	/* the worker has work (a skip wait) */
#define DRV_XHCI_ISOCH_CHECK_HALT	0x4U	/* read the endpoint context: Halted? */
#define DRV_XHCI_ISOCH_RING		0x8U	/* TDs are left: ring the endpoint */

/* A packet's result. */
enum drv_xhci_iso_result {
	DRV_XHCI_ISO_PENDING = 0,
	DRV_XHCI_ISO_OK,
	DRV_XHCI_ISO_SHORT,
	DRV_XHCI_ISO_MISSED,
	DRV_XHCI_ISO_ERROR,
	DRV_XHCI_ISO_CANCELLED
};

/* The endpoint's state (design 2.1a's table). */
enum drv_xhci_isoch_state {
	DRV_XHCI_ISOCH_NORMAL = 0,
	DRV_XHCI_ISOCH_SKIP_WAIT,
	DRV_XHCI_ISOCH_STOPPING,
	DRV_XHCI_ISOCH_CANCELLING,
	DRV_XHCI_ISOCH_HALT_PENDING,
	DRV_XHCI_ISOCH_RECOVERING
};

/*
 * One request (an URB): its caller's tag, the ring slot of its first TD,
 * its TDs, how many have a result, how many from its start are retired
 * (their slots free for new TDs), and each packet's length asked, result
 * and actual length.
 */
struct drv_xhci_isoch_request {
	uintptr_t tag;
	unsigned first;
	unsigned count;
	unsigned done;
	unsigned retired;
	uint32_t length[DRV_XHCI_ISOCH_PACKETS_MAX];
	uint32_t actual[DRV_XHCI_ISOCH_PACKETS_MAX];
	uint8_t result[DRV_XHCI_ISOCH_PACKETS_MAX];
};

/*
 * One endpoint: the ring's TD slots (its TRBs less the link), the next
 * slot to fill and the first slot not yet retired, the TDs on the ring,
 * the state, the FIFO of requests (a circle of QUEUE_MAX from head), and
 * counts of the events that end nothing.
 */
struct drv_xhci_isoch {
	unsigned slots;
	unsigned producer;
	unsigned consumer;
	unsigned outstanding;
	enum drv_xhci_isoch_state state;
	unsigned head;
	unsigned queued;
	struct drv_xhci_isoch_request requests[DRV_XHCI_ISOCH_QUEUE_MAX];
	uint32_t missed_service;
	uint32_t underruns;
	uint32_t overruns;
	uint32_t stopped;
	uint32_t foreign_stops;
	uint32_t strays;
};

void drv_xhci_isoch_init(struct drv_xhci_isoch *isoch, unsigned slots);
unsigned drv_xhci_isoch_room(const struct drv_xhci_isoch *isoch);
int drv_xhci_isoch_enqueue(struct drv_xhci_isoch *isoch, uintptr_t tag, unsigned count, const uint32_t *lengths, unsigned *first);
int drv_xhci_isoch_bei(unsigned index, unsigned count);
int drv_xhci_isoch_locate(const struct drv_xhci_isoch *isoch, unsigned slot, unsigned *position, unsigned *packet);
unsigned drv_xhci_isoch_event(struct drv_xhci_isoch *isoch, unsigned code, int pointed, unsigned slot, uint32_t residual);
void drv_xhci_isoch_halted(struct drv_xhci_isoch *isoch);
int drv_xhci_isoch_begin(struct drv_xhci_isoch *isoch, enum drv_xhci_isoch_state owner);
unsigned drv_xhci_isoch_stop_done(struct drv_xhci_isoch *isoch, unsigned dequeue);
unsigned drv_xhci_isoch_flush(struct drv_xhci_isoch *isoch, enum drv_xhci_iso_result result);
int drv_xhci_isoch_take(struct drv_xhci_isoch *isoch, struct drv_xhci_isoch_request *request);

#endif
