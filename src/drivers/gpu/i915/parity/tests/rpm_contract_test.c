/*
 * GPU-free contract tests for the Linux-parity device runtime-PM layer.
 * Verifies init_early vs enable, probe-time suspend inhibit, and the
 * get_sync vs resume_and_get failure-usage-count difference.
 */
#include <stdio.h>
#include "../osdep/runtime_pm.h"

static int g_fail;
static int g_checks;

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { printf("    FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

struct mock_rpm { int resume_fail; int resume_calls; int suspend_calls; };
static int  m_resume(void *p)  { struct mock_rpm *m = p; m->resume_calls++;  return m->resume_fail ? -5 : 0; }
static void m_suspend(void *p) { struct mock_rpm *m = p; m->suspend_calls++; }

int
main(void)
{
	static struct osdep_trace trace;
	static struct osdep_rpm pm;
	struct mock_rpm mock = { 0, 0, 0 };
	static const struct osdep_rpm_backend be = { "mock-rpm", m_resume, m_suspend };

	printf("== runtime-PM contract tests (GPU-free) ==\n");
	osdep_trace_init(&trace);

	/* init_early is not enable */
	printf("[init] init_early initialises struct; autosuspend NOT enabled\n");
	osdep_rpm_init_early(&pm, &be, &mock, &trace);
	CHECK(osdep_rpm_usage(&pm) == 0, "usage starts 0");
	CHECK(osdep_rpm_active(&pm), "device active during probe");
	CHECK(!osdep_rpm_is_enabled(&pm), "runtime PM not enabled yet (that is driver-load end)");

	/* probe-time suspend inhibit */
	printf("[probe] put during probe does not suspend (autosuspend off)\n");
	osdep_rpm_get_noresume(&pm);
	CHECK(osdep_rpm_usage(&pm) == 1, "usage 1 after get");
	osdep_rpm_put(&pm);
	CHECK(osdep_rpm_usage(&pm) == 0, "usage 0 after put");
	CHECK(osdep_rpm_active(&pm), "still active (probe never suspends)");
	CHECK(mock.suspend_calls == 0, "no suspend during probe");

	/* get_sync while active: no actual resume needed */
	printf("[get_sync] while active only bumps the count\n");
	CHECK(osdep_rpm_get_sync(&pm) == 0, "get_sync ok");
	CHECK(mock.resume_calls == 0, "no resume needed (already active)");
	osdep_rpm_put(&pm);

	/* enable is a separate, later step; now suspend becomes possible when idle */
	printf("[enable] after enable, an idle put suspends\n");
	osdep_rpm_enable(&pm);
	CHECK(osdep_rpm_is_enabled(&pm), "enabled");
	osdep_rpm_get_noresume(&pm);
	osdep_rpm_put(&pm);
	CHECK(!osdep_rpm_active(&pm), "suspended when enabled + idle");
	CHECK(mock.suspend_calls == 1, "one suspend");

	/* get_sync failure: usage stays incremented */
	printf("[get_sync] resume failure leaves usage incremented (caller must put)\n");
	{
		int before = osdep_rpm_usage(&pm);
		mock.resume_fail = 1;
		CHECK(osdep_rpm_get_sync(&pm) == -5, "get_sync returns -errno");
		CHECK(osdep_rpm_usage(&pm) == before + 1, "usage NOT unwound on get_sync failure");
		CHECK(!osdep_rpm_active(&pm), "device still suspended");
		osdep_rpm_put(&pm);   /* caller balances the leaked ref */
	}

	/* resume_and_get failure: usage unwound */
	printf("[resume_and_get] resume failure unwinds usage (caller must not put)\n");
	{
		int before = osdep_rpm_usage(&pm);
		CHECK(osdep_rpm_resume_and_get(&pm) == -5, "resume_and_get returns -errno");
		CHECK(osdep_rpm_usage(&pm) == before, "usage unwound on resume_and_get failure");
	}

	/* success path resumes */
	printf("[resume] a successful resume_and_get brings the device back\n");
	{
		mock.resume_fail = 0;
		CHECK(osdep_rpm_resume_and_get(&pm) == 0, "resume_and_get ok");
		CHECK(osdep_rpm_active(&pm), "device active again");
		CHECK(mock.resume_calls >= 1, "backend resume was called");
		osdep_rpm_put(&pm);
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
