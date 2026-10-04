/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of who may trace whom (ws160-p001): cred_may_trace and
 * cred_ids_differ of src/kern/cred.c compiled freestanding like the kernel.
 * It checks that the superuser traces anyone, that a user traces its own
 * plain processes only, never a root process, a set-id process of its own
 * identity, a process with another saved or effective identity, nor while
 * its own effective identity differs from its real one; and which
 * differences cred_ids_differ sees.
 *
 *   sh plan/ws160/tests/run-host-cred-trace.sh
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

/* The kernel's credential, as include/kern/cred.h has it (the groups' count is not looked at here). */
struct ucred {
	int refs;
	uid_t ruid, euid, suid;
	gid_t rgid, egid, sgid;
	unsigned ngroups;
	gid_t groups[16];
};

int cred_may_trace(const struct ucred *tracer, const struct ucred *target, unsigned target_set_id);
int cred_ids_differ(const struct ucred *left, const struct ucred *right);
static void check(int condition, const char *what);
static void user(struct ucred *cred, uid_t uid, gid_t gid);

/* The checks that failed, and those that ran. */
static int failures;
static int checks;

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Makes a credential of one identity. */
static void
user(
	struct ucred *cred,
	uid_t uid,
	gid_t gid)
{
	/* All three of each. */
	memset(cred, 0, sizeof(*cred));
	cred->ruid = uid;
	cred->euid = uid;
	cred->suid = uid;
	cred->rgid = gid;
	cred->egid = gid;
	cred->sgid = gid;
}

int
main(void)
{
	struct ucred root;
	struct ucred kei;
	struct ucred other;
	struct ucred target;
	int result;

	/* The three identities. */
	user(&root, 0, 0);
	user(&kei, 1000, 1000);
	user(&other, 1001, 1001);

	/* The superuser traces anyone, set-id or not. */
	result = cred_may_trace(&root, &kei, 0U);
	check(result == 1, "root traces kei's process");
	result = cred_may_trace(&root, &kei, 1U);
	check(result == 1, "root traces a set-id process");

	/* A user traces its own plain process. */
	target = kei;
	result = cred_may_trace(&kei, &target, 0U);
	check(result == 1, "kei traces its own process");

	/* Not a root process, nor another user's. */
	result = cred_may_trace(&kei, &root, 0U);
	check(result == 0, "kei does not trace root's process");
	result = cred_may_trace(&kei, &other, 0U);
	check(result == 0, "kei does not trace another user's process");

	/* Not its own identity's process that changed identity (sudo after setuid back, a set-id image). */
	result = cred_may_trace(&kei, &target, 1U);
	check(result == 0, "kei does not trace a set-id process of its own identity");

	/* Not a process running setuid root for kei (effective 0, real 1000). */
	target = kei;
	target.euid = 0;
	target.suid = 0;
	result = cred_may_trace(&kei, &target, 0U);
	check(result == 0, "kei does not trace its setuid-root process");

	/* Not one whose saved identity or group differs. */
	target = kei;
	target.suid = 0;
	result = cred_may_trace(&kei, &target, 0U);
	check(result == 0, "a different saved user identity refuses");
	target = kei;
	target.egid = 0;
	result = cred_may_trace(&kei, &target, 0U);
	check(result == 0, "a different effective group refuses");

	/* Not while the tracer's own effective identity differs from its real one. */
	target = kei;
	target.ruid = 1001;
	target.euid = 1000;
	result = cred_may_trace(&target, &kei, 0U);
	check(result == 0, "a tracer of mixed identity refuses");

	/* Nothing without credentials. */
	result = cred_may_trace(NULL, &kei, 0U);
	check(result == 0, "no tracer credential refuses");

	/* What cred_ids_differ sees. */
	target = kei;
	result = cred_ids_differ(&kei, &target);
	check(result == 0, "the same identities do not differ");
	target.groups[0] = 5;
	target.ngroups = 1;
	result = cred_ids_differ(&kei, &target);
	check(result == 0, "the supplementary groups do not count");
	target.suid = 0;
	result = cred_ids_differ(&kei, &target);
	check(result == 1, "a saved user identity differs");
	target = kei;
	target.sgid = 0;
	result = cred_ids_differ(&kei, &target);
	check(result == 1, "a saved group identity differs");

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-cred-trace: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-cred-trace: %d checks passed\n", checks);
	return 0;
}
