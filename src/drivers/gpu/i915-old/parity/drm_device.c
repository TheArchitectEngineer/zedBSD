/*
 * WS031 Linux-parity — minimal DRM device management + vblank (see drm_device.h).
 *
 * Structure-faithful port of drm_dev_init()'s device state, the drmm managed
 * cleanup list, and drm_vblank_init() / drm_vblank_worker_init() for ADL-P.  The
 * drmm list runs its actions in reverse at fini and, on a full list,
 * drmm_add_action_or_reset() runs the action immediately (the reference
 * contract).  Per-CRTC state (lock, wait queue, index, seqlock, disable timer)
 * is initialised for each of the INTEL_NUM_PIPES CRTCs, and -- as in the
 * reference -- EACH CRTC gets its OWN vblank worker (drm_vblank_worker_init runs
 * per drm_vblank_crtc, not once per device).  A mid-init failure (a full drmm
 * list, or a per-CRTC worker that will not start) reclaims only the elements
 * already initialised, via the per-CRTC cleanup actions run in reverse at fini.
 */
#include "../internal.h"
#include <kern/klog.h>
#include "drm_device.h"

/*
 * Test-only, one-shot worker-create fault injection.  Never triggers in normal
 * operation (armed only by parity_drm_vblank_test_fail_worker_at from ktest);
 * lets the GPU-free test exercise the mid-init worker-create-failure unwind.
 */
static int g_vblank_worker_fail_pipe = -1;

void
parity_drm_vblank_test_fail_worker_at(int pipe)
{
	g_vblank_worker_fail_pipe = pipe;
}

static int
vblank_worker_should_fail(unsigned pipe)
{
	if (g_vblank_worker_fail_pipe >= 0 &&
	    (unsigned)g_vblank_worker_fail_pipe == pipe) {
		g_vblank_worker_fail_pipe = -1;   /* one-shot */
		return 1;
	}
	return 0;
}

int
parity_drm_dev_init(struct parity_drm_device *ddev, struct i915_device *parent,
	uint32_t driver_features)
{
	if (ddev == 0)
		return -1;

	ddev->parent = parent;
	ddev->driver_features = driver_features;
	ddev->open_count = 0;

	spin_init(&ddev->managed_lock, LOCK_RANK_DEVICE, "parity-drm-managed");
	ddev->drmm_count = 0;
	spin_init(&ddev->event_lock, LOCK_RANK_DEVICE, "parity-drm-event");
	ddev->minor_registered = 0;   /* not registered to userspace here */

	ddev->num_crtcs = 0;
	ddev->vblank_disable_immediate = 0;
	ddev->vblank_inited = 0;
	ddev->inited = 1;
	return 0;
}

int
parity_drmm_add_action_or_reset(struct parity_drm_device *ddev,
	void (*fn)(void *arg), void *arg)
{
	if (ddev == 0 || fn == 0)
		return -1;

	if (ddev->drmm_count >= PARITY_DRMM_MAX) {
		/* Contract: a registration that cannot be recorded runs the action now. */
		fn(arg);
		return -1;
	}
	ddev->drmm[ddev->drmm_count].fn = fn;
	ddev->drmm[ddev->drmm_count].arg = arg;
	ddev->drmm_count++;
	return 0;
}

void
parity_drm_dev_fini(struct parity_drm_device *ddev)
{
	if (ddev == 0 || !ddev->inited)
		return;

	/* Run managed actions in reverse registration order (drmm teardown). */
	while (ddev->drmm_count > 0u) {
		ddev->drmm_count--;
		ddev->drmm[ddev->drmm_count].fn(ddev->drmm[ddev->drmm_count].arg);
	}
	ddev->num_crtcs = 0;
	ddev->vblank_inited = 0;
	ddev->inited = 0;
}

/*
 * Per-CRTC cleanup (drm_vblank_init_release + drm_vblank_worker_fini).  Runs the
 * worker down FIRST -- so neither the worker nor its (modelled) disable timer can
 * still touch the CRTC -- then releases the timer/seqlock state.  Tolerates
 * worker_created == 0 so it is safe when init failed before the worker started.
 */
static void
parity_drm_vblank_crtc_cleanup(void *arg)
{
	struct parity_drm_vblank_crtc *vc = (struct parity_drm_vblank_crtc *)arg;

	if (!vc->inited)
		return;
	if (vc->worker_created) {
		parity_kworkqueue_destroy(&vc->worker);   /* stop + join the worker */
		vc->worker_created = 0;
	}
	vc->disable_timer_inited = 0;   /* synchronous timer delete (modelled) */
	vc->disable_deadline = 0u;
	vc->seqlock = 0u;
	vc->inited = 0;
}

/* drm_vblank_init(): per-CRTC vblank state + a per-CRTC drm_vblank_worker_init(). */
int
parity_drm_vblank_init(struct parity_drm_device *ddev, unsigned num_crtcs)
{
	unsigned i;
	int rc;

	if (ddev == 0 || !ddev->inited)
		return -1;
	if (num_crtcs == 0u || num_crtcs > PARITY_DRM_MAX_PIPES)
		return -1;   /* the pipe array cannot hold this count */

	ddev->vblank_disable_immediate = 0;
	ddev->num_crtcs = 0;
	ddev->vblank_inited = 0;

	for (i = 0u; i < num_crtcs; i++) {
		struct parity_drm_vblank_crtc *vc = &ddev->vblank[i];

		/* device/pipe index + wait queue. */
		spin_init(&vc->lock, LOCK_RANK_DEVICE, "parity-vblank-crtc");
		waitq_init(&vc->queue, "parity-vblank-crtc");
		vc->pipe = i;
		vc->count = 0u;
		vc->worker_created = 0;

		/*
		 * Init the disable timer and seqlock BEFORE registering the cleanup,
		 * and register the cleanup BEFORE creating the worker: that ordering
		 * makes both a full-list registration failure and a worker-create
		 * failure recoverable without leaking (the cleanup tolerates a
		 * not-yet-created worker).
		 */
		vc->disable_timer_inited = 1;   /* vblank-disable timer armed-but-idle */
		vc->disable_deadline = 0u;
		vc->seqlock = 0u;
		vc->inited = 1;

		rc = parity_drmm_add_action_or_reset(ddev,
			parity_drm_vblank_crtc_cleanup, vc);
		if (rc != 0) {
			/*
			 * Full drmm list: the contract already ran THIS CRTC's cleanup.
			 * Earlier CRTCs' cleanups stay registered and are reclaimed by
			 * dev_fini; report failure so the whole init unwinds.
			 */
			return rc;
		}

		/* drm_vblank_worker_init(): one worker per CRTC. */
		if (vblank_worker_should_fail(i))
			rc = -1;
		else
			rc = parity_kworkqueue_create(&vc->worker, "parity-vblank-crtc");
		if (rc != 0) {
			/*
			 * The worker never started; this CRTC's cleanup is already
			 * registered (and tolerates worker_created == 0), so dev_fini
			 * unwinds this and every earlier CRTC.  No leak.
			 */
			ddev->num_crtcs = i;
			return rc != 0 ? -1 : 0;
		}
		vc->worker_created = 1;
		ddev->num_crtcs = i + 1u;
	}

	ddev->vblank_inited = 1;
	kern_logf("i915: parity P3 drm_vblank_init: vblank_slots=%u "
		"(per-pipe worker+timer+seqlock)\n", num_crtcs);
	return 0;
}
