/*
 * WS031 Linux-parity OS adaptation layer — device runtime PM.
 *
 * This is the DEVICE runtime-PM layer only, and only the part P0..P2 need:
 * early struct init, the usage-count get/put used during probe, and cleanup.
 * It is kept separate from two other "power" concerns the reference also keeps
 * separate — uncore forcewake (mmio.h) and display power-domain / GT wakerefs
 * (a later layer) — so they are never merged into one "power get".
 *
 * Two distinctions from the reference are preserved:
 *   - init_early (intel_runtime_pm_init_early: initialise the struct) is NOT
 *     enable (intel_runtime_pm_enable: turn on autosuspend at driver-load end).
 *     During probe the device stays resumed because runtime PM is not yet enabled;
 *     a put does not suspend it.  enable() exists but P0..P2 do not call it.
 *   - get_sync and resume_and_get differ on FAILURE: get_sync leaves the usage
 *     count incremented (the caller must put), resume_and_get decrements it back
 *     (the caller must not).  They are not one convenience wrapper.
 */
#ifndef PARITY_OSDEP_RUNTIME_PM_H
#define PARITY_OSDEP_RUNTIME_PM_H

#include <stdint.h>
#include "trace.h"

struct osdep_rpm_backend {
	const char *name;
	int  (*resume)(void *priv);    /* bring device to D0; 0 / -errno */
	void (*suspend)(void *priv);   /* allow D3; only when enabled and idle */
};

struct osdep_rpm {
	const struct osdep_rpm_backend *backend;
	void *priv;
	struct osdep_trace *trace;

	int usage_count;   /* pm_runtime usage count */
	int active;        /* device resumed (D0) */
	int enabled;       /* autosuspend enabled (driver-load end; NOT during probe) */
	unsigned resumes;  /* diagnostics */
	unsigned suspends;
};

/* intel_runtime_pm_init_early equivalent: initialise the struct only. */
void osdep_rpm_init_early(struct osdep_rpm *pm, const struct osdep_rpm_backend *backend,
			  void *priv, struct osdep_trace *trace);

/* intel_runtime_pm_enable equivalent: turn on autosuspend. Driver-load end, not P0..P2. */
void osdep_rpm_enable(struct osdep_rpm *pm);
int  osdep_rpm_is_enabled(const struct osdep_rpm *pm);

/* Plain get/put (get_noresume/put_noidle style): adjust the count only. */
void osdep_rpm_get_noresume(struct osdep_rpm *pm);

/*
 * get_sync: increment usage, resume.  On resume failure returns -errno with the
 * usage count STILL incremented (caller must put).
 */
int osdep_rpm_get_sync(struct osdep_rpm *pm);
/*
 * resume_and_get: increment usage, resume.  On resume failure DECREMENTS the
 * usage count back and returns -errno (caller must not put).
 */
int osdep_rpm_resume_and_get(struct osdep_rpm *pm);

/* put: drop one usage reference.  While !enabled the device never suspends. */
void osdep_rpm_put(struct osdep_rpm *pm);

int osdep_rpm_usage(const struct osdep_rpm *pm);
int osdep_rpm_active(const struct osdep_rpm *pm);

#endif /* PARITY_OSDEP_RUNTIME_PM_H */
