/*
 * WS031 Linux-parity — minimal DRM device management + vblank (see drm_device.c).
 *
 * Ports the drm_dev_init() side of DRM device state, a drmm managed-resource
 * list with the drm_add_action_or_reset() contract, and drm_vblank_init() /
 * drm_vblank_worker_init() for the display "noirq" bring-up (P3).  It does not
 * open /dev/dri, start display output, or wait on real vblanks.
 */
#ifndef PARITY_DRM_DEVICE_H
#define PARITY_DRM_DEVICE_H

#include <stdint.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include "backend_sync.h"

struct i915_device;

#define PARITY_DRM_MAX_PIPES  4    /* ADL-P pipe_mask = A|B|C|D */
#define PARITY_DRMM_MAX      16    /* managed cleanup-action slots */

/* A managed cleanup action (drmm_add_action_or_reset). */
struct parity_drmm_action {
	void (*fn)(void *arg);
	void *arg;
};

/* Per-CRTC vblank state (drm_vblank_crtc subset). */
struct parity_drm_vblank_crtc {
	struct spinlock lock;        /* per-CRTC vblank lock */
	struct wait_queue queue;     /* vblank wait queue */
	unsigned pipe;               /* CRTC/pipe index */
	uint64_t count;              /* vblank sequence (seqlock-guarded in the reference) */
	uint32_t seqlock;            /* seqlock generation (even = stable) */
	int disable_timer_inited;    /* vblank-disable timer state (modelled) */
	uint64_t disable_deadline;
	struct parity_kworkqueue worker;  /* per-CRTC vblank worker (drm_vblank_worker_init) */
	int worker_created;
	int inited;
};

/* DRM device management state (drm_device subset). */
struct parity_drm_device {
	struct i915_device *parent;  /* back-reference to the parent device */
	uint32_t driver_features;    /* DRIVER_* feature flags subset */
	int open_count;              /* device open refcount */

	struct spinlock managed_lock;/* drmm managed-resource list lock */
	struct parity_drmm_action drmm[PARITY_DRMM_MAX];
	unsigned drmm_count;

	struct spinlock event_lock;  /* event list lock */
	int minor_registered;        /* minor bookkeeping; 0 = not published */

	unsigned num_crtcs;          /* INTEL_NUM_PIPES fully initialised */
	int vblank_disable_immediate;
	struct parity_drm_vblank_crtc vblank[PARITY_DRM_MAX_PIPES];
	int vblank_inited;

	int inited;
};

/* drm_dev_init(): initialise the device management state (no minor published). */
int parity_drm_dev_init(struct parity_drm_device *ddev, struct i915_device *parent,
	uint32_t driver_features);

/* drm_dev_fini(): run every managed action in reverse and release the device. */
void parity_drm_dev_fini(struct parity_drm_device *ddev);

/*
 * drmm_add_action_or_reset(): register a cleanup action, or -- if the list is
 * full -- run it immediately and report failure (the reference contract).
 */
int parity_drmm_add_action_or_reset(struct parity_drm_device *ddev,
	void (*fn)(void *arg), void *arg);

/* drm_vblank_init(): initialise num_crtcs vblank CRTCs, each with its own worker. */
int parity_drm_vblank_init(struct parity_drm_device *ddev, unsigned num_crtcs);

/*
 * Test-only, one-shot fault injection: force the per-CRTC worker create for
 * `pipe` to fail on the next drm_vblank_init (pipe < 0 disables).  Used by the
 * GPU-free ktest to exercise the mid-init worker-create-failure unwind.
 */
void parity_drm_vblank_test_fail_worker_at(int pipe);

#endif /* PARITY_DRM_DEVICE_H */
