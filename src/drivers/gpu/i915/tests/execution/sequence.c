/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The whole-run scenario "gen9" (ws229-p005): every scenario a Gen9 laptop
 * is judged by, from one image and one boot.
 *
 * The compute test "eu" runs first, on the start worker before the node is
 * published, as the execution scenarios do.  One thread then waits for the
 * node and runs the render scenarios one after another: "vkcs", "vkx",
 * "vke1", "vke2" and "vkc".  A scenario that fails does not stop the ones
 * after it.  A hang is handled where it can be:
 *
 *  - eu's hang resets the GT (the compute test's own hang path), which
 *    leaves the engines stopped; after any eu failure every engine is
 *    reset alone and resumed (drv_i915_engine_reset()), so the node takes
 *    work again.  An engine that cannot be resumed stops the run there;
 *  - a render scenario that leaves the render engine busy has hung it, and
 *    the driver has no recovery for a hang under a published node
 *    (worker.c), so the scenarios after it are not run.
 *
 * The run ends with one summary line per scenario (PASS, FAIL, SKIP or
 * NOT RUN, with the reason) and one verdict line, which is PASS only when
 * every scenario passed.
 */

#include "scenarios.h"
#include "eu-internal.h"
#include "../render/scenarios.h"
#include "../../i915.h"
#include "../../engine.h"
#include "../../submit.h"
#include "../../sync.h"
#include <kern/kcrt.h>

#include <kern/clock.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <kern/thread.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/* How many scenarios the run holds: eu and the five render scenarios. */
#define I915_GEN9_SCENARIOS		6U

/* How long the render engine is given to go idle after a render scenario, in twentieths of a second (3 s). */
#define I915_GEN9_IDLE_POLLS		60U

/* How long the reason of a scenario that did not pass may be. */
#define I915_GEN9_REASON_BYTES		96U

/*
 * How one scenario of the run ended.
 *
 * A scenario starts as not run; the run sets its outcome once it ran it.
 */
enum i915_gen9_outcome {
	I915_GEN9_NOT_RUN = 0,
	I915_GEN9_PASS,
	I915_GEN9_FAIL,
	I915_GEN9_SKIP
};

/*
 * One scenario of the run: its name, how it is run and how it ended.
 *
 * The render scenarios are run through their callable form; eu's entry has
 * no run function, because the start worker runs it before the thread.
 */
struct i915_gen9_scenario {
	/* The name the summary line gives it. */
	const char *name;

	/* The render scenario's run on the calling thread; NULL for eu. */
	int (*run)(struct i915_device *device);

	/* How it ended, what it returned, and why it did not pass or was not run. */
	enum i915_gen9_outcome outcome;
	int rc;
	char reason[I915_GEN9_REASON_BYTES];
};

/*
 * The run: the device and its scenarios in the order they run.
 *
 * One run per boot; the start worker fills it and the run's thread finishes
 * it after the start returned, so it is not on a stack.
 */
struct i915_gen9_run {
	struct i915_device *device;
	struct i915_gen9_scenario scenario[I915_GEN9_SCENARIOS];
};

/*
 * The one run of the boot.
 *
 * Filled by drv_i915_test_sequence_gen9() on the start worker and read and
 * finished by the run's thread alone afterwards.
 */
static struct i915_gen9_run i915_gen9_run_state;

static void i915_gen9_thread(void *argument);
static void i915_gen9_init(struct i915_gen9_run *run, struct i915_device *device);
static int i915_gen9_resume_engines(struct i915_device *device);
static int i915_gen9_render_idle(struct i915_device *device);
static void i915_gen9_not_run(struct i915_gen9_run *run, unsigned from, const char *reason);
static void i915_gen9_set_reason(struct i915_gen9_scenario *scenario, const char *reason);
static void i915_gen9_summary(struct i915_gen9_run *run);
static const char *i915_gen9_outcome_name(enum i915_gen9_outcome outcome);

/*
 * Runs the whole Gen9 run: eu now, the render scenarios on a thread.
 *
 * Called by the test runner on the start worker before the node is
 * published.  Returns once eu ran and the thread was started; the thread
 * logs the render scenarios, the summary and the verdict.
 */
void
drv_i915_test_sequence_gen9(
	struct i915_device *device)
{
	struct i915_gen9_run *run;
	struct i915_gen9_scenario *eu;
	struct thread *thread;
	int error;

	/* Starts the run with every scenario not run. */
	run = &i915_gen9_run_state;
	i915_gen9_init(run, device);
	kern_logf("i915: gen9: the run is eu, vkcs, vkx, vke1, vke2 and vkc, in this order\n");

	/* Runs eu while nothing else submits, and records how it ended. */
	eu = &run->scenario[0];
	error = drv_i915_test_execution_eu(device);
	eu->rc = error;
	if (error == 0) {
		/* Every marker of the compute test landed. */
		eu->outcome = I915_GEN9_PASS;
	} else if (error == I915_TEST_SKIPPED) {
		/* The compute test refused this GT and logged why. */
		eu->outcome = I915_GEN9_SKIP;
		i915_gen9_set_reason(eu, "refused this GT");
	} else {
		/* The compute test failed or hung; its own lines say how. */
		eu->outcome = I915_GEN9_FAIL;
		i915_gen9_set_reason(eu, "see the EU-TEST lines");
	}

	kern_logf("i915: gen9: eu %s rc=%d\n", i915_gen9_outcome_name(eu->outcome), eu->rc);

	/* After a failure of eu, which may have reset the GT, resumes every engine for the render scenarios. */
	if (eu->outcome == I915_GEN9_FAIL) {
		error = i915_gen9_resume_engines(device);
		if (error != 0) {
			kern_logf("i915: gen9: the engines could not be resumed after eu (rc=%d); the render scenarios are not run\n",
				  error);
			i915_gen9_not_run(run, 1U, "the engines could not be resumed after eu");
			i915_gen9_summary(run);
			return;
		}
	}

	/* Starts the thread that waits for the node and runs the render scenarios. */
	error = kthread_create(i915_gen9_thread, run, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0) {
		kern_logf("i915: gen9: the render scenarios' thread cannot be created: %d\n", error);
		i915_gen9_not_run(run, 1U, "no thread for the render scenarios");
		i915_gen9_summary(run);
		return;
	}

	/* The thread reclaims itself when the run is done. */
	thread->detached = 1U;
	thread_start(thread);
	kern_logf("i915: gen9: the render scenarios run once the node is published\n");
}

/* Runs the render scenarios in order, stops after a hang of the render engine, and logs the summary. */
static void
i915_gen9_thread(
	void *argument)
{
	struct i915_gen9_run *run;
	struct i915_gen9_scenario *scenario;
	unsigned index;
	int error;
	int idle;

	run = argument;

	/* Runs every render scenario after the one before it, whatever its verdict. */
	for (index = 1U; index < I915_GEN9_SCENARIOS; index++) {
		scenario = &run->scenario[index];

		/* Runs the scenario; its own lines carry its steps and its verdict. */
		kern_logf("i915: gen9: %s begin\n", scenario->name);
		error = scenario->run(run->device);
		scenario->rc = error;
		if (error == 0) {
			/* Every step of the scenario passed. */
			scenario->outcome = I915_GEN9_PASS;
		} else {
			/* The scenario's verdict line says what failed. */
			scenario->outcome = I915_GEN9_FAIL;
			i915_gen9_set_reason(scenario, "see its verdict line");
		}

		kern_logf("i915: gen9: %s %s rc=%d\n", scenario->name, i915_gen9_outcome_name(scenario->outcome), error);

		/* A render engine that does not go idle is hung, and nothing after it can run. */
		idle = i915_gen9_render_idle(run->device);
		if (idle == 0) {
			kern_logf("i915: gen9: the render engine is still busy after %s (a hang; the driver has no recovery under a published node): the rest are not run\n",
				  scenario->name);
			i915_gen9_set_reason(scenario, "left the render engine hung");
			i915_gen9_not_run(run, index + 1U, "the render engine hung in an earlier scenario");
			break;
		}
	}

	/* Logs one line per scenario and the verdict. */
	i915_gen9_summary(run);
}

/* Names the run's scenarios in order, all not run yet. */
static void
i915_gen9_init(
	struct i915_gen9_run *run,
	struct i915_device *device)
{
	/* Starts from an empty run on the device. */
	kern_memset(run, 0, sizeof(*run));
	run->device = device;

	/* eu, which the start worker runs itself. */
	run->scenario[0].name = "eu";
	run->scenario[0].run = NULL;

	/* The render scenarios, compute first, the shader compiler last. */
	run->scenario[1].name = "vkcs";
	run->scenario[1].run = drv_i915_test_render_compute_run;
	run->scenario[2].name = "vkx";
	run->scenario[2].run = drv_i915_test_render_executor_run;
	run->scenario[3].name = "vke1";
	run->scenario[3].run = drv_i915_test_render_features_run;
	run->scenario[4].name = "vke2";
	run->scenario[4].run = drv_i915_test_render_generality_run;
	run->scenario[5].name = "vkc";
	run->scenario[5].run = drv_i915_test_render_compiler_run;
}

/*
 * Resets every engine alone and resumes it (__intel_engine_reset_bh()).
 *
 * Runs on the start worker before the node is published, which owns the
 * execlists state then, under every forcewake domain the tests take.
 * Returns 0, or the first engine's reset error.
 */
static int
i915_gen9_resume_engines(
	struct i915_device *device)
{
	struct i915_gt_engines *es;
	unsigned held;
	unsigned index;
	int failed;
	int error;

	es = &device->gt.engines;

	/* Takes the forcewake domains the engine reset expects to be held. */
	held = 0U;
	error = drv_i915_test_forcewake_get_all(device, &held);
	if (error != 0) {
		drv_i915_test_forcewake_put_all(device, held);
		return error;
	}

	/* Resets and resumes every engine, keeping the first failure. */
	kern_logf("i915: gen9: resetting and resuming %u engines after eu\n", es->n);
	failed = 0;
	for (index = 0U; index < es->n; index++) {
		error = drv_i915_engine_reset(es, &device->gt.init, &device->gt.info, index, &device->gt.mmio, &device->gt.uncore_lock);
		if (error != 0 && failed == 0)
			failed = error;
	}

	/* Puts the forcewake domains back. */
	drv_i915_test_forcewake_put_all(device, held);

	/* Reports an engine that stayed stopped. */
	if (failed != 0)
		return failed;

	/* Succeeded: every engine takes work again. */
	return 0;
}

/*
 * Reports whether the render engine went idle within three seconds.
 *
 * Idle is what the worker's wait ends on: nothing pending in the ELSQ and
 * nothing running.  The two words are read without the worker's ordering,
 * which is enough for a verdict taken seconds after the last request.
 * Returns 1 when idle, 0 when still busy or when the GT has no render engine.
 */
static int
i915_gen9_render_idle(
	struct i915_device *device)
{
	struct i915_gt_engines *es;
	struct i915_execlists *el;
	struct i915_completion nap;
	unsigned index;
	unsigned polls;

	es = &device->gt.engines;

	/* Finds the render engine. */
	for (index = 0U; index < es->n; index++) {
		if (es->ge[index].info->class == I915_RENDER_CLASS)
			break;
	}

	/* A GT without a render engine has nothing that could be idle. */
	if (index == es->n)
		return 0;

	el = &es->el[index];

	/* A completion nobody signals: each wait on it simply lasts until its deadline. */
	drv_i915_completion_init(&nap, "i915 gen9");

	/* Looks for an idle engine a twentieth of a second at a time. */
	for (polls = 0U; polls < I915_GEN9_IDLE_POLLS; polls++) {
		if (el->have_active == 0 && el->pending[0] == NULL)
			return 1;

		(void)drv_i915_wait_for_completion(&nap, sched_ticks() + KERN_CLOCK_HZ / 20U);
	}

	/* The engine stayed busy. */
	return 0;
}

/* Marks every scenario from one index on as not run, with the reason. */
static void
i915_gen9_not_run(
	struct i915_gen9_run *run,
	unsigned from,
	const char *reason)
{
	unsigned index;

	/* Gives each scenario that was not reached the reason. */
	for (index = from; index < I915_GEN9_SCENARIOS; index++) {
		run->scenario[index].outcome = I915_GEN9_NOT_RUN;
		i915_gen9_set_reason(&run->scenario[index], reason);
	}
}

/* Copies the reason a scenario did not pass, cut to its room. */
static void
i915_gen9_set_reason(
	struct i915_gen9_scenario *scenario,
	const char *reason)
{
	unsigned index;

	/* Copies the characters that fit, then ends the string. */
	for (index = 0U; index + 1U < I915_GEN9_REASON_BYTES; index++) {
		if (reason[index] == 0)
			break;
		scenario->reason[index] = reason[index];
	}

	scenario->reason[index] = 0;
}

/* Logs one line per scenario and the run's verdict. */
static void
i915_gen9_summary(
	struct i915_gen9_run *run)
{
	struct i915_gen9_scenario *scenario;
	unsigned counts[4];
	unsigned index;
	const char *verdict;

	/* Logs every scenario and counts each outcome. */
	kern_memset(counts, 0, sizeof(counts));
	for (index = 0U; index < I915_GEN9_SCENARIOS; index++) {
		scenario = &run->scenario[index];
		counts[scenario->outcome]++;
		if (scenario->outcome == I915_GEN9_PASS) {
			/* A pass needs no reason. */
			kern_logf("i915: gen9 summary: %s PASS\n", scenario->name);
		} else if (scenario->outcome == I915_GEN9_NOT_RUN) {
			/* A scenario never reached has no return value. */
			kern_logf("i915: gen9 summary: %s NOT RUN (%s)\n", scenario->name, scenario->reason);
		} else {
			/* A failure or a skip says what it returned and where to look. */
			kern_logf("i915: gen9 summary: %s %s rc=%d (%s)\n",
				  scenario->name,
				  i915_gen9_outcome_name(scenario->outcome),
				  scenario->rc,
				  scenario->reason);
		}
	}

	/* The run passes only when every scenario passed. */
	verdict = "FAIL";
	if (counts[I915_GEN9_PASS] == I915_GEN9_SCENARIOS)
		verdict = "PASS";

	kern_logf("i915: gen9: verdict %s (pass=%u fail=%u skip=%u not_run=%u)\n",
		  verdict,
		  counts[I915_GEN9_PASS],
		  counts[I915_GEN9_FAIL],
		  counts[I915_GEN9_SKIP],
		  counts[I915_GEN9_NOT_RUN]);
}

/* Names an outcome for the log. */
static const char *
i915_gen9_outcome_name(
	enum i915_gen9_outcome outcome)
{
	/* Chooses the word of the outcome. */
	switch (outcome) {
	case I915_GEN9_PASS:
		return "PASS";
	case I915_GEN9_FAIL:
		return "FAIL";
	case I915_GEN9_SKIP:
		return "SKIP";
	default:
		break;
	}

	/* A scenario the run did not reach. */
	return "NOT RUN";
}
