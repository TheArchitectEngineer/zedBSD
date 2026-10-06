/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Mail's TLS (WS169 p003; mail.h): the OpenSSL package's libssl and
 * libcrypto, loaded with dlopen the first time a server is reached, as
 * the browser does (libbrowser's net/tls.c), so that Mail builds without
 * the package.  Without it every connection fails with EPROTONOSUPPORT.
 *
 * A connection is TLS 1.2 or later and verifies the server's chain
 * against the default roots (the ca-certificates package) and any file
 * added with ml_tls_add_ca_file (the host tests' own CA), and the
 * certificate's name against the server's host.  A failure is EPROTO,
 * its reason kept for ml_tls_error.
 *
 * Only the connection thread of Mail (sync.c) makes connections; the
 * library is loaded under a lock so that a second thread would not load
 * it twice.
 */

#include "mail.h"

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The names the libraries are looked for under, in order (the package's, then a host's versioned one). */
#define TLS_LIBSSL		"libssl.so"
#define TLS_LIBSSL_VERSIONED	"libssl.so.3"
#define TLS_LIBCRYPTO		"libcrypto.so"
#define TLS_LIBCRYPTO_VERSIONED	"libcrypto.so.3"

/* OpenSSL's constants that are used (ssl.h, tls1.h, x509_vfy.h); stable across 1.1 and 3. */
#define TLS_VERIFY_PEER			1
#define TLS_CTRL_SET_TLSEXT_HOSTNAME	55
#define TLS_NAMETYPE_HOST_NAME		0
#define TLS_CTRL_SET_MIN_PROTO_VERSION	123
#define TLS_VERSION_1_2			0x0303
#define TLS_OP_IGNORE_UNEXPECTED_EOF	((uint64_t)1 << 7)
#define TLS_ERROR_WANT_READ		2
#define TLS_ERROR_WANT_WRITE		3
#define TLS_ERROR_SYSCALL		5
#define TLS_ERROR_ZERO_RETURN		6
#define TLS_VERIFY_OK			0

/* The most extra CA files the tests may add. */
#define TLS_CA_FILES_MAX	4U

/*
 * The library's functions, found with dlsym.  The pointers are set once,
 * when the libraries are loaded, and never change after.
 */
struct tls_library {
	const void *(*client_method)(void);
	void *(*ctx_new)(const void *method);
	int (*ctx_set_default_verify_paths)(void *ctx);
	int (*ctx_load_verify_locations)(void *ctx, const char *file, const char *path);
	void (*ctx_set_verify)(void *ctx, int mode, void *callback);
	long (*ctx_ctrl)(void *ctx, int command, long number, void *pointer);
	uint64_t (*ctx_set_options)(void *ctx, uint64_t options);
	void *(*ssl_new)(void *ctx);
	void (*ssl_free)(void *ssl);
	int (*set_fd)(void *ssl, int descriptor);
	long (*ssl_ctrl)(void *ssl, int command, long number, void *pointer);
	int (*set1_host)(void *ssl, const char *host);
	int (*connect)(void *ssl);
	int (*read)(void *ssl, void *bytes, int length);
	int (*write)(void *ssl, const void *bytes, int length);
	int (*get_error)(const void *ssl, int result);
	int (*pending)(const void *ssl);
	int (*shutdown)(void *ssl);
	long (*get_verify_result)(const void *ssl);
	const char *(*verify_error_string)(long result);
	unsigned long (*err_get_error)(void);
	void (*err_error_string_n)(unsigned long error, char *text, size_t length);
	void (*err_clear_error)(void);
};

/*
 * One function tls_find_all finds: in libcrypto or libssl, its name, and
 * its member of struct tls_library.
 */
struct tls_symbol {
	int crypto;
	const char *name;
	size_t offset;
};

/* The functions of struct tls_library, in its order.  The table is constant for the life of the program. */
static const struct tls_symbol tls_symbols[] = {
	{ 0, "TLS_client_method", offsetof(struct tls_library, client_method) },
	{ 0, "SSL_CTX_new", offsetof(struct tls_library, ctx_new) },
	{ 0, "SSL_CTX_set_default_verify_paths", offsetof(struct tls_library, ctx_set_default_verify_paths) },
	{ 0, "SSL_CTX_load_verify_locations", offsetof(struct tls_library, ctx_load_verify_locations) },
	{ 0, "SSL_CTX_set_verify", offsetof(struct tls_library, ctx_set_verify) },
	{ 0, "SSL_CTX_ctrl", offsetof(struct tls_library, ctx_ctrl) },
	{ 0, "SSL_CTX_set_options", offsetof(struct tls_library, ctx_set_options) },
	{ 0, "SSL_new", offsetof(struct tls_library, ssl_new) },
	{ 0, "SSL_free", offsetof(struct tls_library, ssl_free) },
	{ 0, "SSL_set_fd", offsetof(struct tls_library, set_fd) },
	{ 0, "SSL_ctrl", offsetof(struct tls_library, ssl_ctrl) },
	{ 0, "SSL_set1_host", offsetof(struct tls_library, set1_host) },
	{ 0, "SSL_connect", offsetof(struct tls_library, connect) },
	{ 0, "SSL_read", offsetof(struct tls_library, read) },
	{ 0, "SSL_write", offsetof(struct tls_library, write) },
	{ 0, "SSL_get_error", offsetof(struct tls_library, get_error) },
	{ 0, "SSL_pending", offsetof(struct tls_library, pending) },
	{ 0, "SSL_shutdown", offsetof(struct tls_library, shutdown) },
	{ 0, "SSL_get_verify_result", offsetof(struct tls_library, get_verify_result) },
	{ 1, "X509_verify_cert_error_string", offsetof(struct tls_library, verify_error_string) },
	{ 1, "ERR_get_error", offsetof(struct tls_library, err_get_error) },
	{ 1, "ERR_error_string_n", offsetof(struct tls_library, err_error_string_n) },
	{ 1, "ERR_clear_error", offsetof(struct tls_library, err_clear_error) }
};

/* The loaded library's functions, set once while tls_lock is held and read only after. */
static struct tls_library tls_library;

/* 0 before the first attempt to load the library, 1 once loaded, -1 when it cannot be (under tls_lock). */
static int tls_loaded;

/* The context every connection is made from (its roots and settings), made with the library. */
static void *tls_context;

/* The CA files added by the tests, loaded into the context when it is made (set before the first connection). */
static const char *tls_ca_files[TLS_CA_FILES_MAX];

/* How many entries of tls_ca_files are set. */
static size_t tls_ca_file_count;

/* The reason of the last failure, for ml_tls_error (one connection thread writes it). */
static char tls_reason[ML_TEXT_MAX];

/* Keeps two threads from loading the library at once; held only while it loads. */
static pthread_mutex_t tls_lock = PTHREAD_MUTEX_INITIALIZER;

static int tls_load(void);
static int tls_load_locked(void);
static void *tls_open_library(const char *name, const char *versioned, int flags);
static int tls_find_all(void *ssl, void *crypto);
static int tls_make_context(void);
static int tls_set_host(void *ssl, const char *host);
static void tls_fail(const char *what, void *ssl, int result);

/*
 * Trusts the CA certificates of a PEM file besides the default roots (the
 * host tests' own CA).  Must come before the first connection.
 */
int
ml_tls_add_ca_file(
	const char *path)
{
	/* No room for another. */
	if (tls_ca_file_count == TLS_CA_FILES_MAX)
		return ENOSPC;

	/* Kept until the context is made. */
	tls_ca_files[tls_ca_file_count] = path;
	tls_ca_file_count++;
	return 0;
}

/*
 * Starts TLS on a connected socket to a host: the handshake, the chain and
 * the name.  Returns 0 with the connection's state, EPROTONOSUPPORT
 * without the library, ENOMEM, or EPROTO (ml_tls_error says why).
 */
int
ml_tls_open(
	int fd,
	const char *host,
	void **tls)
{
	void *ssl;
	int result;
	int error;

	/* The library and the shared context. */
	*tls = NULL;
	tls_reason[0] = '\0';
	error = tls_load();
	if (error != 0)
		return error;

	/* The connection's state. */
	tls_library.err_clear_error();
	ssl = tls_library.ssl_new(tls_context);
	if (ssl == NULL)
		return ENOMEM;

	/* The socket. */
	result = tls_library.set_fd(ssl, fd);
	if (result != 1) {
		tls_fail("setup", ssl, 0);
		tls_library.ssl_free(ssl);
		return EPROTO;
	}

	/* The name sent (SNI) and checked. */
	error = tls_set_host(ssl, host);
	if (error != 0) {
		tls_fail("setup", ssl, 0);
		tls_library.ssl_free(ssl);
		return error;
	}

	/* The handshake, which verifies the chain and the name. */
	result = tls_library.connect(ssl);
	if (result != 1) {
		tls_fail("handshake", ssl, result);
		tls_library.ssl_free(ssl);
		return EPROTO;
	}

	/* Succeeded: the connection is secure. */
	*tls = ssl;
	return 0;
}

/*
 * Reads what the server sent, up to length bytes: *received is 0 at the
 * end of the connection.  Returns 0 or an errno value.
 */
int
ml_tls_read(
	void *tls,
	unsigned char *bytes,
	size_t length,
	size_t *received)
{
	int count;
	int result;
	int reason;

	/* One read (OpenSSL takes an int). */
	*received = 0;
	count = (int)length;
	if (length > INT_MAX)
		count = INT_MAX;

	/* Until a record gives bytes, the end, or a failure. */
	for (;;) {
		result = tls_library.read(tls, bytes, count);
		if (result > 0) {
			*received = (size_t)result;
			return 0;
		}

		/* A record that is not complete yet is read again. */
		reason = tls_library.get_error(tls, result);
		if (reason != TLS_ERROR_WANT_READ && reason != TLS_ERROR_WANT_WRITE)
			break;
	}

	/* The server's close (or a close without close_notify, which the context allows). */
	if (reason == TLS_ERROR_ZERO_RETURN)
		return 0;

	/* A failure of the socket underneath. */
	if (reason == TLS_ERROR_SYSCALL && errno != 0)
		return errno;

	/* Anything else is a broken connection. */
	tls_fail("read", tls, result);
	return EPROTO;
}

/*
 * Sends all of a buffer.  Returns 0 or an errno value.
 */
int
ml_tls_write(
	void *tls,
	const unsigned char *bytes,
	size_t length)
{
	size_t done;
	int count;
	int result;
	int reason;

	/* Until everything is sent. */
	done = 0;
	while (done < length) {
		/* One write (OpenSSL takes an int). */
		count = (int)(length - done);
		if (length - done > INT_MAX)
			count = INT_MAX;
		result = tls_library.write(tls, bytes + done, count);
		if (result > 0) {
			done += (size_t)result;
			continue;
		}

		/* A write that must wait is tried again. */
		reason = tls_library.get_error(tls, result);
		if (reason == TLS_ERROR_WANT_READ || reason == TLS_ERROR_WANT_WRITE)
			continue;

		/* Anything else ends the connection. */
		tls_fail("write", tls, result);
		return EPROTO;
	}

	/* Succeeded: everything is sent. */
	return 0;
}

/*
 * Tells whether bytes already decrypted wait to be read (then the socket
 * need not be readable for the next read).
 */
int
ml_tls_pending(
	void *tls)
{
	int pending;

	/* The bytes OpenSSL holds. */
	pending = tls_library.pending(tls);
	if (pending > 0)
		return 1;

	/* Nothing held. */
	return 0;
}

/*
 * Ends a connection (close_notify once, without waiting for the server's)
 * and frees its state; the socket stays open for the caller to close.
 */
void
ml_tls_close(
	void *tls)
{
	/* Nothing to end. */
	if (tls == NULL)
		return;

	/* The close, then the state. */
	(void)tls_library.shutdown(tls);
	tls_library.ssl_free(tls);
}

/*
 * Reports the reason of the last TLS failure, or "".
 */
const char *
ml_tls_error(void)
{
	/* The text kept by tls_fail. */
	return tls_reason;
}

/* Loads the libraries and makes the context, once; returns 0 or EPROTONOSUPPORT. */
static int
tls_load(void)
{
	int error;

	/* One loader at a time. */
	pthread_mutex_lock(&tls_lock);

	error = tls_load_locked();

	pthread_mutex_unlock(&tls_lock);

	/* Reports why TLS cannot be used. */
	if (error != 0)
		return error;

	/* Succeeded: connections can be made. */
	return 0;
}

/* Loads the libraries and makes the context under tls_lock. */
static int
tls_load_locked(void)
{
	void *ssl;
	void *crypto;
	int error;

	/* Already done. */
	if (tls_loaded > 0)
		return 0;

	/* Already known to be impossible. */
	if (tls_loaded < 0) {
		snprintf(tls_reason, sizeof(tls_reason), "the OpenSSL package is not installed");
		return EPROTONOSUPPORT;
	}

	/* libcrypto first (libssl needs it). */
	tls_loaded = -1;
	crypto = tls_open_library(TLS_LIBCRYPTO, TLS_LIBCRYPTO_VERSIONED, RTLD_NOW | RTLD_GLOBAL);
	if (crypto == NULL)
		return EPROTONOSUPPORT;

	/* Then libssl. */
	ssl = tls_open_library(TLS_LIBSSL, TLS_LIBSSL_VERSIONED, RTLD_NOW);
	if (ssl == NULL)
		return EPROTONOSUPPORT;

	/* The functions. */
	error = tls_find_all(ssl, crypto);
	if (error != 0)
		return EPROTONOSUPPORT;

	/* The context. */
	error = tls_make_context();
	if (error != 0)
		return EPROTONOSUPPORT;

	/* Succeeded: the libraries stay loaded for the program's life. */
	tls_loaded = 1;
	return 0;
}

/*
 * Opens a library by its name, or else by its versioned name; when neither
 * opens, keeps the loader's reason and returns NULL.
 */
static void *
tls_open_library(
	const char *name,
	const char *versioned,
	int flags)
{
	const char *reason;
	void *library;

	/* The package's name. */
	library = dlopen(name, flags);
	if (library != NULL)
		return library;

	/* A host's versioned name. */
	library = dlopen(versioned, flags);
	if (library != NULL)
		return library;

	/* Neither: the package is missing or cannot load. */
	reason = dlerror();
	if (reason == NULL)
		reason = "not found";
	snprintf(tls_reason, sizeof(tls_reason), "cannot load the OpenSSL package's %s: %s", name, reason);
	return NULL;
}

/* Finds every function the connections use (tls_symbols). */
static int
tls_find_all(
	void *ssl,
	void *crypto)
{
	void *library;
	void *found;
	size_t index;

	/* Each symbol, from libcrypto or libssl, into its member. */
	for (index = 0; index < sizeof(tls_symbols) / sizeof(tls_symbols[0]); index++) {
		/* The library that has it. */
		library = ssl;
		if (tls_symbols[index].crypto)
			library = crypto;

		/* The symbol. */
		found = dlsym(library, tls_symbols[index].name);
		if (found == NULL) {
			snprintf(tls_reason, sizeof(tls_reason), "OpenSSL has no %s", tls_symbols[index].name);
			return ENOENT;
		}

		/* Stored through the member's address (a data pointer copied into a function pointer). */
		memcpy((char *)&tls_library + tls_symbols[index].offset, &found, sizeof(found));
	}

	/* Succeeded: every function is there. */
	return 0;
}

/* Makes the context: TLS 1.2 or later, the peer verified against the default roots and the added CA files. */
static int
tls_make_context(void)
{
	const void *method;
	size_t index;
	long set;
	int result;

	/* The client context. */
	method = tls_library.client_method();
	tls_context = tls_library.ctx_new(method);
	if (tls_context == NULL) {
		snprintf(tls_reason, sizeof(tls_reason), "cannot make an OpenSSL context");
		return ENOMEM;
	}

	/* TLS 1.2 or later. */
	set = tls_library.ctx_ctrl(tls_context, TLS_CTRL_SET_MIN_PROTO_VERSION, TLS_VERSION_1_2, NULL);
	if (set != 1) {
		snprintf(tls_reason, sizeof(tls_reason), "OpenSSL refuses TLS 1.2 as the least version");
		return EPROTO;
	}

	/* The peer must verify; a close without close_notify reads as the end. */
	tls_library.ctx_set_verify(tls_context, TLS_VERIFY_PEER, NULL);
	(void)tls_library.ctx_set_options(tls_context, TLS_OP_IGNORE_UNEXPECTED_EOF);

	/* The default roots (a missing file only leaves them empty). */
	(void)tls_library.ctx_set_default_verify_paths(tls_context);

	/* The added CA files. */
	for (index = 0; index < tls_ca_file_count; index++) {
		result = tls_library.ctx_load_verify_locations(tls_context, tls_ca_files[index], NULL);
		if (result != 1) {
			snprintf(tls_reason, sizeof(tls_reason), "cannot read the CA file %s", tls_ca_files[index]);
			return EPROTO;
		}
	}

	/* Succeeded: connections can be made. */
	return 0;
}

/* Sets the name sent to the server (SNI) and matched against its certificate. */
static int
tls_set_host(
	void *ssl,
	const char *host)
{
	long sent;
	int result;

	/* Sent to the server. */
	sent = tls_library.ssl_ctrl(ssl, TLS_CTRL_SET_TLSEXT_HOSTNAME, TLS_NAMETYPE_HOST_NAME, (void *)host);
	if (sent != 1)
		return EPROTO;

	/* Matched against the certificate. */
	result = tls_library.set1_host(ssl, host);
	if (result != 1)
		return EPROTO;

	/* Succeeded: the name is set. */
	return 0;
}

/* Keeps the reason of a failure: the certificate's verification, or OpenSSL's error queue. */
static void
tls_fail(
	const char *what,
	void *ssl,
	int result)
{
	const char *described;
	unsigned long queued;
	long verified;
	int reason;
	char text[ML_TEXT_MAX / 2U];

	/* A certificate that did not verify says so. */
	verified = tls_library.get_verify_result(ssl);
	if (verified != TLS_VERIFY_OK) {
		described = tls_library.verify_error_string(verified);
		snprintf(tls_reason, sizeof(tls_reason), "%s: certificate verify failed: %s", what, described);
		tls_library.err_clear_error();
		return;
	}

	/* Otherwise the first queued error. */
	queued = tls_library.err_get_error();
	if (queued != 0) {
		tls_library.err_error_string_n(queued, text, sizeof(text));
		snprintf(tls_reason, sizeof(tls_reason), "%s: %s", what, text);
		tls_library.err_clear_error();
		return;
	}

	/* No queued error: the connection ended or failed underneath. */
	reason = tls_library.get_error(ssl, result);
	snprintf(tls_reason, sizeof(tls_reason), "%s: failed (OpenSSL error %d)", what, reason);
}
