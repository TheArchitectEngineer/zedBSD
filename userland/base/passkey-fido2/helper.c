/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The device helper (fido2.h; ws172-p003; docs/architecture/security.md,
 * "The parts", "The security key").  A child of passkey-fido2: it gives up
 * root for _passkey inside the empty root /var/empty, may start no
 * process, and holds only the keys' descriptors (opened and claimed by
 * passkey-fido2) and its pipe.  It never reads /etc/passkey, does not make
 * the challenge and does not judge the answer.
 *
 * To log in, it first asks every key, without the user and without the
 * PIN, whether it holds one of the account's credentials; the PIN goes to
 * that key only, then the assertion is asked with the user's touch.  To
 * register, exactly one key must be there; it makes a new credential with
 * the PIN, and the authenticator data goes back as the key gave it.
 */

#include "fido2.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

/* How long the channel's opening may take on one key. */
#define HELPER_OPEN_MS		2000U

/* A key the helper talks to: its channel, transport, device and what it is. */
struct helper_key {
	struct pk_hid hid;
	struct pk_transport transport;
	struct pk_device device;
	struct pk_info info;
};

/* The helper's pipe to passkey-fido2, and whether the touch was asked for (the keepalive tells it once). */
static int helper_pipe = -1;
static int helper_touch_told;

static int helper_sandbox(uid_t uid, gid_t gid);
static int helper_open(struct fido2_devices *devices, size_t index, struct helper_key *key);
static void helper_assert(struct fido2_devices *devices, const struct fido2_job *job);
static void helper_make(struct fido2_devices *devices, const struct fido2_job *job);
static int helper_token(struct helper_key *key, const struct fido2_job *job, unsigned permissions, uint8_t *token, size_t *token_size);
static const char *helper_reason(const struct helper_key *key, int error);
static void helper_keepalive(void *context, uint8_t status);
static void helper_send(const char *line);
static void helper_fail(const char *reason);

/*
 * Runs the helper's job on the keys and writes its answer on pipe_out;
 * it never returns.
 */
void
fido2_helper(
	struct fido2_devices *devices,
	const struct fido2_job *job,
	int pipe_out,
	uid_t uid,
	gid_t gid)
{
	int error;

	/* The pipe, and the sandbox. */
	helper_pipe = pipe_out;
	error = helper_sandbox(uid, gid);
	if (error != 0) {
		helper_fail("internal");
		_exit(2);
	}

	/* The job. */
	if (job->kind == FIDO2_JOB_ASSERT)
		helper_assert(devices, job);
	else
		helper_make(devices, job);

	/* Done: the answer was sent. */
	_exit(0);
}

/*
 * Gives up everything but the keys and the pipe: the standard descriptors
 * on /dev/null, no core file, an alarm, the empty root (no program to
 * start in it), and _passkey's group and user.  Returns 0 or an errno value.
 */
static int
helper_sandbox(
	uid_t uid,
	gid_t gid)
{
	struct rlimit none;
	int descriptor;
	int result;

	/* Nothing read from or written to passkey's own descriptors. */
	descriptor = open("/dev/null", O_RDWR);
	if (descriptor < 0)
		return errno;
	(void)dup2(descriptor, 0);
	(void)dup2(descriptor, 1);
	(void)dup2(descriptor, 2);
	if (descriptor > 2)
		(void)close(descriptor);

	/* No core file, and an end of its own (the empty root has no program to start). */
	none.rlim_cur = 0;
	none.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &none);
	(void)alarm(FIDO2_HELPER_SECONDS);

	/* The empty root. */
	result = chroot("/var/empty");
	if (result != 0)
		return errno;
	result = chdir("/");
	if (result != 0)
		return errno;

	/* _passkey's group alone, then its user (root cannot come back). */
	result = setgroups(1U, &gid);
	if (result != 0)
		return errno;
	result = setgid(gid);
	if (result != 0)
		return errno;
	result = setuid(uid);
	if (result != 0)
		return errno;

	/* Succeeded: the helper holds nothing else. */
	return 0;
}

/*
 * Opens a CTAP2 channel on a key and asks what it is.  Returns 0 or an
 * errno value.
 */
static int
helper_open(
	struct fido2_devices *devices,
	size_t index,
	struct helper_key *key)
{
	struct pk_hid_io io;
	int error;

	/* The node's reports, the channel, CTAP2 over it. */
	memset(key, 0, sizeof(*key));
	pk_os_posix_io(&devices->handles[index], &io);
	error = pk_hid_open(&key->hid, &io, HELPER_OPEN_MS);
	if (error != 0)
		return error;
	(void)pk_hid_transport(&key->transport, &key->hid);
	pk_device_init(&key->device, &key->transport, FIDO2_TOUCH_MS);
	key->device.keepalive = helper_keepalive;

	/* What the key is. */
	error = pk_ctap2_get_info(&key->device, &key->info);
	if (error != 0)
		return error;

	/* Succeeded: the key answers. */
	return 0;
}

/* Logs in: the key that holds a credential, its PIN, the assertion with the touch. */
static void
helper_assert(
	struct fido2_devices *devices,
	const struct fido2_job *job)
{
	static struct helper_key key;
	struct pk_assertion_request request;
	struct pk_assertion_reply reply;
	char line[FIDO2_MESSAGE_MAX];
	char id[2U * PK_CREDENTIAL_ID_MAX + 1U];
	char auth_data[2U * PK_AUTH_DATA_MAX + 1U];
	char signature[2U * PK_SIGNATURE_MAX + 1U];
	uint8_t token[PK_PIN_TOKEN_MAX];
	size_t token_size;
	size_t index;
	int found;
	int error;

	/* The silent question: which key holds one of the credentials (without the user, without the PIN). */
	memset(&request, 0, sizeof(request));
	request.rp_id = FIDO2_RP;
	memcpy(request.client_data_hash, job->client_data_hash, sizeof(request.client_data_hash));
	request.allow_ids = job->ids;
	request.allow_sizes = job->id_sizes;
	request.allow_count = job->id_count;
	found = 0;
	for (index = 0U; index < devices->count && !found; index++) {
		error = helper_open(devices, index, &key);
		if (error != 0)
			continue;
		error = pk_ctap2_get_assertion(&key.device, &request, &reply);
		if (error == 0)
			found = 1;
	}

	/* None holds one. */
	if (!found) {
		helper_fail("no-key");
		return;
	}

	/* The PIN to that key alone. */
	error = helper_token(&key, job, PK_PERMISSION_GET_ASSERTION, token, &token_size);
	if (error != 0)
		return;

	/* The assertion, with the user's touch and the PIN's verification. */
	request.presence = 1;
	request.pin_token = token;
	request.pin_token_size = token_size;
	request.pin_protocol = pk_ctap2_choose_protocol(&key.info);
	error = pk_ctap2_get_assertion(&key.device, &request, &reply);
	pk_crypto_wipe(token, sizeof(token));
	if (error != 0) {
		helper_fail(helper_reason(&key, error));
		return;
	}

	/* The answer's bytes. */
	(void)fido2_hex_encode(reply.credential_id, reply.credential_id_size, id, sizeof(id));
	(void)fido2_hex_encode(reply.auth_data, reply.auth_data_size, auth_data, sizeof(auth_data));
	(void)fido2_hex_encode(reply.signature, reply.signature_size, signature, sizeof(signature));
	(void)snprintf(line, sizeof(line), "assertion %s %s %s", id, auth_data, signature);
	helper_send(line);
}

/* Registers: the one key there, its PIN, a new credential with the touch. */
static void
helper_make(
	struct fido2_devices *devices,
	const struct fido2_job *job)
{
	static struct helper_key key;
	static struct pk_made_credential made;
	struct pk_make_request request;
	char line[FIDO2_MESSAGE_MAX];
	char auth_data[2U * PK_AUTH_DATA_MAX + 1U];
	uint8_t token[PK_PIN_TOKEN_MAX];
	size_t token_size;
	int error;

	/* Exactly one key. */
	if (devices->count == 0U) {
		helper_fail("no-key");
		return;
	}

	/* Two or more: a rogue one could slip its own credential in. */
	if (devices->count > 1U) {
		helper_fail("many-keys");
		return;
	}

	/* It answers. */
	error = helper_open(devices, 0U, &key);
	if (error != 0) {
		helper_fail("device");
		return;
	}

	/* The PIN. */
	error = helper_token(&key, job, PK_PERMISSION_MAKE_CREDENTIAL, token, &token_size);
	if (error != 0)
		return;

	/* The credential, with the user's touch, not one of the account's again. */
	memset(&request, 0, sizeof(request));
	request.rp_id = FIDO2_RP;
	request.user_id = job->user_id;
	request.user_id_size = sizeof(job->user_id);
	request.user_name = job->user_name;
	memcpy(request.client_data_hash, job->client_data_hash, sizeof(request.client_data_hash));
	request.pin_token = token;
	request.pin_token_size = token_size;
	request.pin_protocol = pk_ctap2_choose_protocol(&key.info);
	request.exclude_ids = job->ids;
	request.exclude_sizes = job->id_sizes;
	request.exclude_count = job->id_count;
	error = pk_ctap2_make_credential(&key.device, &request, &made);
	pk_crypto_wipe(token, sizeof(token));
	if (error != 0) {
		helper_fail(helper_reason(&key, error));
		return;
	}

	/* The authenticator data as the key gave it (passkey-fido2 reads it again). */
	(void)fido2_hex_encode(made.auth_data, made.auth_data_size, auth_data, sizeof(auth_data));
	(void)snprintf(line, sizeof(line), "made %s", auth_data);
	helper_send(line);
}

/*
 * Gets a PIN token of the permissions from the key, which must have a PIN
 * of its own.  Returns 0, or an errno value after the failure was sent.
 */
static int
helper_token(
	struct helper_key *key,
	const struct fido2_job *job,
	unsigned permissions,
	uint8_t *token,
	size_t *token_size)
{
	unsigned protocol;
	int error;

	/* A key without a PIN is not used. */
	protocol = pk_ctap2_choose_protocol(&key->info);
	if ((key->info.options & PK_OPTION_CLIENT_PIN_SET) == 0U || protocol == 0U) {
		helper_fail("device");
		return EPERM;
	}

	/* The token. */
	*token_size = PK_PIN_TOKEN_MAX;
	error = pk_ctap2_pin_token(&key->device, &key->info, protocol, job->pin, permissions, FIDO2_RP, token, token_size);
	if (error != 0) {
		helper_fail(helper_reason(key, error));
		return error;
	}

	/* Succeeded. */
	return 0;
}

/* Gives passkey's reason for a key's failure. */
static const char *
helper_reason(
	const struct helper_key *key,
	int error)
{
	uint8_t status;

	/* The key's own answers. */
	if (error != EPROTO)
		return "device";
	status = key->device.last_status;
	switch (status) {
	case PK_CTAP2_PIN_INVALID:
		return "bad-secret";
	case PK_CTAP2_PIN_BLOCKED:
	case PK_CTAP2_PIN_AUTH_BLOCKED:
		return "key-locked";
	case PK_CTAP2_NO_CREDENTIALS:
		return "no-key";
	case PK_CTAP2_OPERATION_DENIED:
	case PK_CTAP2_USER_ACTION_TIMEOUT:
	case PK_CTAP2_KEEPALIVE_CANCEL:
		return "timeout";
	case PK_CTAP2_CREDENTIAL_EXCLUDED:
		return "bad-request";
	default:
		break;
	}

	/* Anything else the key said. */
	return "device";
}

/* Tells passkey-fido2, once, that the key waits for the user's touch. */
static void
helper_keepalive(
	void *context,
	uint8_t status)
{
	/* Only the wait for the user, once. */
	(void)context;
	if (status != PK_HID_KEEPALIVE_UP_NEEDED || helper_touch_told)
		return;
	helper_touch_told = 1;
	helper_send("touch");
}

/* Writes one message line on the pipe. */
static void
helper_send(
	const char *line)
{
	size_t length;
	size_t done;
	ssize_t written;

	/* The line and its end, whole. */
	length = strlen(line);
	done = 0U;
	while (done < length) {
		written = write(helper_pipe, line + done, length - done);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return;
		done += (size_t)written;
	}

	/* Its end. */
	(void)write(helper_pipe, "\n", 1U);
}

/* Sends a failure. */
static void
helper_fail(
	const char *reason)
{
	char line[64];

	/* "fail REASON". */
	(void)snprintf(line, sizeof(line), "fail %s", reason);
	helper_send(line);
}
