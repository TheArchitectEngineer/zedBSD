/* WS031 Linux-parity OS adaptation layer — device runtime PM (see runtime_pm.h). */
#include "runtime_pm.h"

static void
tr(struct osdep_rpm *pm, uint16_t op, const char *what, uint64_t a0, uint64_t a1)
{
	if (pm->trace != 0)
		osdep_trace_emit(pm->trace, 0u, op, what, a0, a1);
}

void
osdep_rpm_init_early(struct osdep_rpm *pm, const struct osdep_rpm_backend *backend,
		     void *priv, struct osdep_trace *trace)
{
	pm->backend = backend;
	pm->priv = priv;
	pm->trace = trace;
	pm->usage_count = 0;
	/* The device is powered during probe (post pci_enable_device -> D0). */
	pm->active = 1;
	/* Autosuspend is OFF until enable() at driver-load end: probe never suspends. */
	pm->enabled = 0;
	pm->resumes = 0u;
	pm->suspends = 0u;
	tr(pm, OSDEP_TR_NOTE, "rpm_init_early", 0u, 0u);
}

void
osdep_rpm_enable(struct osdep_rpm *pm)
{
	pm->enabled = 1;
	tr(pm, OSDEP_TR_NOTE, "rpm_enable", 0u, 0u);
}

int
osdep_rpm_is_enabled(const struct osdep_rpm *pm)
{
	return pm->enabled;
}

void
osdep_rpm_get_noresume(struct osdep_rpm *pm)
{
	pm->usage_count++;
	tr(pm, OSDEP_TR_ACQUIRE, "rpm_get_noresume", (uint64_t)pm->usage_count, 0u);
}

static int
do_resume(struct osdep_rpm *pm)
{
	int rc;

	if (pm->active)
		return 0;
	rc = pm->backend->resume(pm->priv);
	if (rc == 0) {
		pm->active = 1;
		pm->resumes++;
	}
	return rc;
}

int
osdep_rpm_get_sync(struct osdep_rpm *pm)
{
	int rc;

	pm->usage_count++;             /* usage incremented first */
	rc = do_resume(pm);
	if (rc != 0) {
		/* get_sync: usage count is NOT unwound on failure. */
		tr(pm, OSDEP_TR_FAIL, "rpm_get_sync", (uint64_t)rc, (uint64_t)pm->usage_count);
		return rc;
	}
	tr(pm, OSDEP_TR_ACQUIRE, "rpm_get_sync", (uint64_t)pm->usage_count, 0u);
	return 0;
}

int
osdep_rpm_resume_and_get(struct osdep_rpm *pm)
{
	int rc;

	pm->usage_count++;
	rc = do_resume(pm);
	if (rc != 0) {
		/* resume_and_get: unwind the usage count on failure. */
		pm->usage_count--;
		tr(pm, OSDEP_TR_FAIL, "rpm_resume_and_get", (uint64_t)rc, (uint64_t)pm->usage_count);
		return rc;
	}
	tr(pm, OSDEP_TR_ACQUIRE, "rpm_resume_and_get", (uint64_t)pm->usage_count, 0u);
	return 0;
}

void
osdep_rpm_put(struct osdep_rpm *pm)
{
	if (pm->usage_count > 0)
		pm->usage_count--;
	tr(pm, OSDEP_TR_RELEASE, "rpm_put", (uint64_t)pm->usage_count, 0u);

	/* Suspend only when enabled AND idle.  During probe (!enabled) never suspend. */
	if (pm->enabled && pm->usage_count == 0 && pm->active) {
		if (pm->backend->suspend != 0)
			pm->backend->suspend(pm->priv);
		pm->active = 0;
		pm->suspends++;
	}
}

int
osdep_rpm_usage(const struct osdep_rpm *pm)
{
	return pm->usage_count;
}

int
osdep_rpm_active(const struct osdep_rpm *pm)
{
	return pm->active;
}
