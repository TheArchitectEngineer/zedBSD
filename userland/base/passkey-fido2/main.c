/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * /usr/libexec/passkey-fido2 (fido2.h; ws172-p003): the security key
 * style of /sbin/passkey, which starts it with the same request on its
 * standard input; its answer goes to passkey's standard output.
 *
 *   auth NAME fido2 KEY-PIN                   the login with a key
 *   enroll-fido2 NAME PASSWORD LABEL KEY-PIN  a new key for the account
 *   remove-fido2 NAME PASSWORD ID             one of the account's keys goes
 *
 * It runs as root alone (its real user ID), reads nothing from its command
 * line or environment, makes the challenge, opens and claims the keys,
 * starts the device helper (helper.c) and checks the key's answer itself:
 * the public key is the account's own line's, never the key's word.  The
 * answer is passkey's: "status touch" lines, then "ok uid=N" (with
 * "id=ID" after a registration) or "fail REASON"; the exit status 0, 1 or 2.
 */

#include "fido2.h"

#include "userland/base/common/account.h"
#include "userland/base/login/verify.h"
#include "userland/base/libpasskey/verify.h"
#include "userland/base/passkey/passkey.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The helper's account, and the first user ID of a person's account. */
#define MAIN_HELPER_ACCOUNT	"_passkey"
#define MAIN_UID_FIRST		1000U

/* The largest /etc/passkey, read and written whole, and the longest line. */
#define MAIN_FILE_MAX		65536U
#define MAIN_LINE_MAX		4096U

/* The exit statuses. */
#define MAIN_EXIT_OK		0
#define MAIN_EXIT_FAIL		1
#define MAIN_EXIT_INTERNAL	2

/* The account's keys read from /etc/passkey (the lines as they are, and taken apart). */
struct main_keys {
	char lines[PASSKEY_FIDO2_MAX][MAIN_LINE_MAX];
	struct fido2_record records[PASSKEY_FIDO2_MAX];
	size_t count;
};

/* The file's text, the new text and the account's keys (large; one request a run). */
static char main_text[MAIN_FILE_MAX];
static char main_output[MAIN_FILE_MAX + MAIN_LINE_MAX];
static struct main_keys main_account_keys;

static void main_setup(void);
static int main_read(char *buffer, size_t capacity, size_t *length);
static int main_fail(const char *reason);
static int main_ok(uid_t uid, const char *extra);
static int main_account(const char *name, struct passwd *account, char *buffer, size_t size);
static int main_usable(const char *name, uid_t uid);
static int main_file(size_t *length, int need_private);
static int main_keys(const char *name, uid_t uid, struct main_keys *keys);
static int main_helper_account(uid_t *uid, gid_t *gid);
static int main_auth(const char *name, uid_t uid, char *pin);
static int main_enroll(const char *name, uid_t uid, const char *label, char *pin);
static int main_remove(const char *name, uid_t uid, const char *id);
static int main_change(const char *name, const char *field, const char *added);
static int main_run(const struct fido2_job *job, struct fido2_message *message);
static const char *main_verify_reason(int error);

/* Answers one request. */
int
main(void)
{
	struct passkey_request request;
	struct passwd account;
	char buffer[PASSKEY_REQUEST_MAX + 1U];
	char strings[LOGIN_VERIFY_BUFFER];
	size_t length;
	int status;
	int error;
	uid_t real;
	int found;
	int is_auth;

	/* Root alone, a clean environment, no core file. */
	real = getuid();
	if (real != 0)
		return MAIN_EXIT_INTERNAL;
	main_setup();

	/* The request. */
	length = 0U;
	error = main_read(buffer, sizeof(buffer), &length);
	if (error == 0)
		error = passkey_request_parse(buffer, length, &request);
	if (error != 0) {
		passkey_wipe(buffer, sizeof(buffer));
		return main_fail("bad-request");
	}

	/* A security key's: the login with one, a registration or a removal. */
	is_auth = 0;
	if (request.operation == PASSKEY_OP_AUTH)
		is_auth = strcmp(request.fields[2], "fido2") == 0;
	if (!is_auth && request.operation != PASSKEY_OP_ENROLL_FIDO2 && request.operation != PASSKEY_OP_REMOVE_FIDO2) {
		passkey_wipe(buffer, sizeof(buffer));
		return main_fail("bad-request");
	}

	/* The login's account. */
	if (is_auth) {
		found = main_account(request.fields[1], &account, strings, sizeof(strings));
		if (!found) {
			passkey_wipe(buffer, sizeof(buffer));
			return main_fail("no-such-user");
		}

		/* The login, and nothing secret stays. */
		status = main_auth(request.fields[1], account.pw_uid, request.fields[3]);
		passkey_wipe(buffer, sizeof(buffer));
		return status;
	}

	/* A change needs the account's password. */
	error = login_verify(request.fields[1], request.fields[2], &account, strings, sizeof(strings));
	if (error != 0) {
		passkey_wipe(buffer, sizeof(buffer));
		passkey_wipe(strings, sizeof(strings));
		return main_fail("bad-secret");
	}

	/* The registration, or the removal. */
	if (request.operation == PASSKEY_OP_ENROLL_FIDO2)
		status = main_enroll(request.fields[1], account.pw_uid, request.fields[3], request.fields[4]);
	else
		status = main_remove(request.fields[1], account.pw_uid, request.fields[3]);

	/* Nothing secret stays. */
	passkey_wipe(buffer, sizeof(buffer));
	passkey_wipe(strings, sizeof(strings));
	return status;
}

/* Clears the environment, keeps no core file, takes the default signals and gives fd 2 to /dev/null. */
static void
main_setup(void)
{
	struct rlimit none;
	sigset_t all;
	int descriptor;
	int signal_number;

	/* No variable of the caller's. */
	(void)clearenv();
	(void)setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);

	/* No core file of a process that held secrets. */
	none.rlim_cur = 0;
	none.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &none);

	/* The signals as a new process has them. */
	for (signal_number = 1; signal_number < 32; signal_number++)
		(void)signal(signal_number, SIG_DFL);
	sigemptyset(&all);
	(void)sigprocmask(SIG_SETMASK, &all, NULL);

	/* fd 2 is /dev/null, so a new file never takes it. */
	descriptor = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (descriptor >= 0 && descriptor != 2) {
		(void)dup2(descriptor, 2);
		(void)close(descriptor);
	}
}

/* Reads the whole request (at most PASSKEY_REQUEST_MAX bytes) from standard input. */
static int
main_read(
	char *buffer,
	size_t capacity,
	size_t *length)
{
	ssize_t count;
	size_t used;

	/* Until the end of input, one byte beyond the bound being an error. */
	used = 0U;
	for (;;) {
		count = read(0, buffer + used, capacity - used);
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0)
			return errno;
		if (count == 0)
			break;
		used += (size_t)count;
		if (used == capacity)
			return EMSGSIZE;
	}

	/* Succeeded: the request. */
	*length = used;
	return 0;
}

/* Answers a failure; returns the exit status. */
static int
main_fail(
	const char *reason)
{
	/* The line, at once. */
	printf("fail %s\n", reason);
	(void)fflush(stdout);
	return MAIN_EXIT_FAIL;
}

/* Answers a success with the account's user ID (and extra after it); returns the exit status. */
static int
main_ok(
	uid_t uid,
	const char *extra)
{
	/* The line, at once. */
	if (extra != NULL)
		printf("ok uid=%u %s\n", (unsigned)uid, extra);
	else
		printf("ok uid=%u\n", (unsigned)uid);
	(void)fflush(stdout);
	return MAIN_EXIT_OK;
}

/* Looks an account up in passwd; 1 when it is there. */
static int
main_account(
	const char *name,
	struct passwd *account,
	char *buffer,
	size_t size)
{
	struct passwd *found;
	int error;

	/* The account's entry. */
	found = NULL;
	error = getpwnam_r(name, account, buffer, size, &found);
	if (error != 0 || found == NULL)
		return 0;

	/* It is there. */
	return 1;
}

/*
 * Tells whether an account may use a key: a person's (user ID 1000 and
 * up), its password neither locked nor expired.
 */
static int
main_usable(
	const char *name,
	uid_t uid)
{
	struct spwd shadow;
	struct spwd *found;
	char buffer[LOGIN_VERIFY_BUFFER];
	long today;
	int usable;
	int error;

	/* A person's account. */
	if (uid < MAIN_UID_FIRST)
		return 0;

	/* Its shadow line. */
	found = NULL;
	error = getspnam_r(name, &shadow, buffer, sizeof(buffer), &found);
	if (error != 0 || found == NULL)
		return 0;

	/* Not locked, not expired. */
	usable = 1;
	if (shadow.sp_pwdp[0] == '!' || shadow.sp_pwdp[0] == '*' || shadow.sp_pwdp[0] == '\0')
		usable = 0;
	today = (long)(time(NULL) / 86400);
	if (shadow.sp_expire > 0 && today >= shadow.sp_expire)
		usable = 0;
	passkey_wipe(buffer, sizeof(buffer));
	return usable;
}

/*
 * Reads /etc/passkey whole into main_text: a missing file is empty.  With
 * need_private, a file not root's or readable by anyone else is refused
 * (EPERM).
 */
static int
main_file(
	size_t *length,
	int need_private)
{
	struct stat status;
	int error;

	/* Its owner and mode. */
	*length = 0U;
	error = stat(PASSKEY_FILE, &status);
	if (error != 0 && errno == ENOENT)
		return 0;
	if (error != 0)
		return errno;
	if (need_private && (status.st_uid != 0 || (status.st_mode & 077) != 0))
		return EPERM;

	/* Its text. */
	error = account_file_read(PASSKEY_FILE, main_text, sizeof(main_text), length);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Gathers the account's keys from /etc/passkey (a malformed line is left
 * out).  Returns 0, or EPERM for a file that is not private.
 */
static int
main_keys(
	const char *name,
	uid_t uid,
	struct main_keys *keys)
{
	size_t length;
	unsigned index;
	int error;

	/* The file. */
	memset(keys, 0, sizeof(*keys));
	error = main_file(&length, 1);
	if (error != 0)
		return error;

	/* Each of the account's key lines that reads. */
	for (index = 0U; keys->count < PASSKEY_FIDO2_MAX; index++) {
		error = passkey_record_find(main_text, length, name, uid, "fido2", index, keys->lines[keys->count], MAIN_LINE_MAX);
		if (error == ENOENT)
			break;
		if (error != 0)
			continue;
		error = fido2_record_parse(keys->lines[keys->count], &keys->records[keys->count]);
		if (error == 0)
			keys->count++;
	}

	/* Succeeded: the keys. */
	return 0;
}

/* Looks up the helper's account (_passkey); returns 0 or ENOENT. */
static int
main_helper_account(
	uid_t *uid,
	gid_t *gid)
{
	struct passwd account;
	char buffer[1024];
	int found;

	/* Its entry. */
	found = main_account(MAIN_HELPER_ACCOUNT, &account, buffer, sizeof(buffer));
	if (!found || account.pw_uid == 0)
		return ENOENT;

	/* Succeeded: its IDs. */
	*uid = account.pw_uid;
	*gid = account.pw_gid;
	return 0;
}

/*
 * Logs in with a key: the account's keys, a new challenge, the helper's
 * answer checked against the account's own public key, and a larger count
 * kept.
 */
static int
main_auth(
	const char *name,
	uid_t uid,
	char *pin)
{
	static struct fido2_job job;
	static struct fido2_message message;
	struct pk_credential allowed[PASSKEY_FIDO2_MAX];
	struct pk_expectation expectation;
	struct pk_assertion assertion;
	struct main_keys *keys;
	char line[MAIN_LINE_MAX];
	uint8_t challenge[FIDO2_CHALLENGE_SIZE];
	uint32_t count;
	size_t matched;
	size_t index;
	int usable;
	int error;

	/* A usable account with keys. */
	usable = main_usable(name, uid);
	if (!usable) {
		passkey_wipe(pin, strlen(pin));
		return main_fail("locked-account");
	}

	/* Its keys. */
	keys = &main_account_keys;
	error = main_keys(name, uid, keys);
	if (error != 0 || keys->count == 0U) {
		passkey_wipe(pin, strlen(pin));
		if (error != 0)
			return main_fail("internal");
		return main_fail("not-enrolled");
	}

	/* The challenge and the client data hash. */
	error = pk_crypto_random(challenge, sizeof(challenge));
	if (error == 0)
		error = fido2_client_data_hash(name, challenge, job.client_data_hash);
	if (error != 0) {
		passkey_wipe(pin, strlen(pin));
		return main_fail("internal");
	}

	/* The helper's job: the credentials allowed and the key's PIN. */
	job.kind = FIDO2_JOB_ASSERT;
	(void)snprintf(job.pin, sizeof(job.pin), "%s", pin);
	passkey_wipe(pin, strlen(pin));
	for (index = 0U; index < keys->count; index++) {
		job.ids[index] = keys->records[index].id;
		job.id_sizes[index] = keys->records[index].id_size;
	}

	/* As many as the account has. */
	job.id_count = keys->count;

	/* The helper's answer. */
	error = main_run(&job, &message);
	passkey_wipe(job.pin, sizeof(job.pin));
	if (error != 0) {
		if (error == ETIMEDOUT)
			return main_fail("timeout");
		return main_fail("device");
	}

	/* The helper's own failure is the answer. */
	if (message.kind == FIDO2_MESSAGE_FAIL)
		return main_fail(message.reason);
	if (message.kind != FIDO2_MESSAGE_ASSERTION)
		return main_fail("device");

	/* What is expected: the login's relying party, this client data hash, the user present and verified, the account's keys. */
	memset(&expectation, 0, sizeof(expectation));
	for (index = 0U; index < keys->count; index++) {
		allowed[index].id = keys->records[index].id;
		allowed[index].id_size = keys->records[index].id_size;
		allowed[index].cose_key = keys->records[index].cose_key;
		allowed[index].cose_key_size = keys->records[index].cose_key_size;
		allowed[index].sign_count = keys->records[index].sign_count;
	}

	/* The relying party, the hash, the flags, the keys. */
	expectation.rp_id = FIDO2_RP;
	memcpy(expectation.client_data_hash, job.client_data_hash, sizeof(expectation.client_data_hash));
	expectation.required_flags = PK_FLAG_UP | PK_FLAG_UV;
	expectation.credentials = allowed;
	expectation.credential_count = keys->count;

	/* The answer, checked here. */
	assertion.credential_id = message.id;
	assertion.credential_id_size = message.id_size;
	assertion.auth_data = message.auth_data;
	assertion.auth_data_size = message.auth_data_size;
	assertion.signature = message.signature;
	assertion.signature_size = message.signature_size;
	error = pk_verify_assertion(&expectation, &assertion, &matched, &count);
	if (error != 0)
		return main_fail(main_verify_reason(error));

	/* A larger count is kept (a failure to keep it does not undo the login). */
	if (count > keys->records[matched].sign_count) {
		error = fido2_record_recount(keys->lines[matched], count, line, sizeof(line));
		if (error == 0)
			(void)main_change(name, keys->records[matched].id_text, line);
	}

	/* Succeeded: the account's key. */
	return main_ok(uid, NULL);
}

/*
 * Registers a new key: a usable account with room, a valid label, the one
 * key there making a credential, its authenticator data read here.
 */
static int
main_enroll(
	const char *name,
	uid_t uid,
	const char *label,
	char *pin)
{
	static struct fido2_job job;
	static struct fido2_message message;
	static struct pk_made_credential made;
	struct main_keys *keys;
	struct tm day;
	time_t now;
	char id[FIDO2_BASE64_MAX];
	char cose_key[FIDO2_BASE64_MAX];
	char date[16];
	char line[MAIN_LINE_MAX];
	char extra[FIDO2_BASE64_MAX + 8U];
	size_t index;
	int usable;
	int valid;
	int error;

	/* A usable account, a label, room for one more key. */
	usable = main_usable(name, uid);
	valid = fido2_label_valid(label);
	keys = &main_account_keys;
	error = main_keys(name, uid, keys);
	if (!usable || !valid || error != 0 || keys->count >= PASSKEY_FIDO2_MAX) {
		passkey_wipe(pin, strlen(pin));
		if (!usable)
			return main_fail("locked-account");
		if (error != 0)
			return main_fail("internal");
		return main_fail("bad-request");
	}

	/* The helper's job: a new client data hash, the user's ID and name, the keys not to make again, the key's PIN. */
	job.kind = FIDO2_JOB_MAKE;
	error = pk_crypto_random(job.client_data_hash, sizeof(job.client_data_hash));
	if (error == 0)
		error = fido2_user_id(name, job.user_id);
	if (error != 0) {
		passkey_wipe(pin, strlen(pin));
		return main_fail("internal");
	}

	/* The name, the key's PIN (the request's copy wiped). */
	job.user_name = name;
	(void)snprintf(job.pin, sizeof(job.pin), "%s", pin);
	passkey_wipe(pin, strlen(pin));
	for (index = 0U; index < keys->count; index++) {
		job.ids[index] = keys->records[index].id;
		job.id_sizes[index] = keys->records[index].id_size;
	}

	/* As many as the account has. */
	job.id_count = keys->count;

	/* The helper's answer. */
	error = main_run(&job, &message);
	passkey_wipe(job.pin, sizeof(job.pin));
	if (error != 0) {
		if (error == ETIMEDOUT)
			return main_fail("timeout");
		return main_fail("device");
	}

	/* The helper's own failure is the answer. */
	if (message.kind == FIDO2_MESSAGE_FAIL)
		return main_fail(message.reason);
	if (message.kind != FIDO2_MESSAGE_MADE)
		return main_fail("device");

	/* Read here: the login's relying party, the user present and verified, an ES256 key on the curve. */
	error = pk_ctap2_read_made(FIDO2_RP, message.auth_data, message.auth_data_size, &made);
	if (error != 0 || (made.flags & PK_FLAG_UV) == 0U)
		return main_fail("device");

	/* The account's new line. */
	now = time(NULL);
	(void)gmtime_r(&now, &day);
	(void)strftime(date, sizeof(date), "%Y-%m-%d", &day);
	error = fido2_base64_encode(made.id, made.id_size, id, sizeof(id));
	if (error == 0)
		error = fido2_base64_encode(made.cose_key, made.cose_key_size, cose_key, sizeof(cose_key));
	if (error == 0)
		error = fido2_record_line(name, uid, id, cose_key, made.sign_count, label, date, line, sizeof(line));
	if (error != 0)
		return main_fail("internal");

	/* Kept, under the lock. */
	error = main_change(name, NULL, line);
	if (error != 0)
		return main_fail("internal");

	/* Succeeded: the new key's ID. */
	(void)snprintf(extra, sizeof(extra), "id=%s", id);
	return main_ok(uid, extra);
}

/* Removes one of the account's keys by its ID. */
static int
main_remove(
	const char *name,
	uid_t uid,
	const char *id)
{
	struct main_keys *keys;
	size_t index;
	int found;
	int same;
	int error;

	/* The account's key of that ID. */
	keys = &main_account_keys;
	error = main_keys(name, uid, keys);
	if (error != 0)
		return main_fail("internal");
	found = 0;
	for (index = 0U; index < keys->count && !found; index++) {
		same = strcmp(keys->records[index].id_text, id) == 0;
		if (same)
			found = 1;
	}

	/* Not one of the account's. */
	if (!found)
		return main_fail("not-enrolled");

	/* Its line goes, under the lock. */
	error = main_change(name, id, NULL);
	if (error != 0)
		return main_fail("internal");

	/* Succeeded. */
	return main_ok(uid, NULL);
}

/*
 * Changes /etc/passkey under the account files' lock, the signals held:
 * the name's key line of the ID field goes (none when field is NULL) and
 * added comes at the end (none when NULL).  Returns 0 or an errno value.
 */
static int
main_change(
	const char *name,
	const char *field,
	const char *added)
{
	sigset_t held;
	sigset_t previous;
	size_t length;
	size_t written;
	int version;
	int error;

	/* The signals that would stop the change are held; the lock. */
	sigfillset(&held);
	(void)sigprocmask(SIG_BLOCK, &held, &previous);
	error = account_files_lock();
	if (error != 0) {
		(void)sigprocmask(SIG_SETMASK, &previous, NULL);
		return error;
	}

	/* The file read again under the lock, a version this program may write, the new text. */
	error = main_file(&length, 0);
	version = passkey_record_version(main_text, length);
	if (error == 0 && version > PASSKEY_VERSION)
		error = EROFS;
	if (error == 0)
		error = passkey_record_edit(main_text, length, name, "fido2", field, added, main_output, sizeof(main_output), &written);
	if (error == 0)
		error = account_file_write(PASSKEY_FILE, 0600, main_output, written);

	/* The lock and the signals given back. */
	account_files_unlock();
	(void)sigprocmask(SIG_SETMASK, &previous, NULL);
	if (error != 0)
		return error;

	/* Succeeded: the file is changed. */
	return 0;
}

/*
 * Opens and claims the keys, runs the helper as _passkey and closes the
 * keys.  Returns 0 with its answer, or an errno value.
 */
static int
main_run(
	const struct fido2_job *job,
	struct fido2_message *message)
{
	static struct fido2_devices devices;
	uid_t uid;
	gid_t gid;
	int error;

	/* The helper's account, and the keys. */
	error = main_helper_account(&uid, &gid);
	if (error != 0)
		return error;
	error = fido2_devices_open(&devices);
	if (error != 0)
		return error;

	/* The helper's answer, the keys given back. */
	error = fido2_run_helper(&devices, job, uid, gid, message);
	fido2_devices_close(&devices);
	if (error != 0)
		return error;

	/* Succeeded: the answer. */
	return 0;
}

/* Gives passkey's reason for an answer that did not verify. */
static const char *
main_verify_reason(
	int error)
{
	/* The check that failed. */
	switch (error) {
	case PK_VERIFY_NOT_ALLOWED:
		return "no-key";
	case PK_VERIFY_FLAGS:
	case PK_VERIFY_SIGNATURE:
		return "bad-secret";
	case PK_VERIFY_REPLAY:
		return "cloned";
	default:
		break;
	}

	/* A malformed answer, or another party's. */
	return "device";
}
