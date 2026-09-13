/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Models a backend proof separately from transport descriptor idleness. */
static unsigned quiesce_calls;
static unsigned quiesce_ready;
static unsigned quiesce_idle_pending;
static unsigned quiesce_idle_calls;
static struct venus_session *quiesce_observed_session;

/* Never simulates native idle merely because an opaque command returned to its caller. */
int
drv_venus_transport_quiesce(
	struct venus_transport *transport,
	uint32_t context,
	struct venus_request **pending,
	unsigned *quiesced)
{
	assert(transport != NULL && context != 0U && pending != NULL);
	quiesce_calls++;
	if (transport->quiesce == 0U)
		return ENOTSUP;
	if (quiesce_ready == 0U)
		return EAGAIN;
	*quiesced = 1U;
	return 0;
}

/* Checks metadata-only old-profile close and sticky raw-work admission through actual adapters. */
static void
fixture_backend_quiesce_proposal(
	void)
{
	struct venus_controller controller;
	struct venus_session *session;
	uint8_t configuration[16];
	uint8_t raw[4];
	void *private_session;
	int error;

	/* A context with no native stream can close on an old host, but still needs its descriptor barrier. */
	fixture_controller(&controller, configuration);
	assert(venus_open(&controller, &private_session) == 0);
	session = private_session;
	assert(session->native_commands_submitted == 0U);
	quiesce_calls = 0U;
	quiesce_idle_calls = 0U;
	quiesce_idle_pending = 0U;
	assert(venus_stop_begin(&controller, session, EIO) == 0);
	assert(venus_stop_poll(&controller, session) == 0);
	assert(quiesce_calls == 0U && quiesce_idle_calls == 2U);
	assert(quiesce_calls == 0U && controller.transport.failed == 0U);
	venus_close(&controller, session);

	/* A clean context with an unused reservation still requires an actual host ACK. */
	assert(venus_open(&controller, &private_session) == 0);
	session = private_session;
	controller.transport.quiesce = 1U;
	quiesce_ready = 0U;
	quiesce_idle_pending = 1U;
	assert(venus_stop_begin(&controller, session, EIO) == 0);
	assert(quiesce_calls == 1U && session->quiesced == 0U);
	assert(venus_stop_poll(&controller, session) == EAGAIN);
	quiesce_ready = 1U;
	quiesce_idle_pending = 0U;
	assert(venus_stop_poll(&controller, session) == 0);
	venus_close(&controller, session);
	controller.transport.quiesce = 0U;

	/* Invalid stream length never reaches the host and cannot create native ownership. */
	assert(venus_open(&controller, &private_session) == 0);
	session = private_session;
	memset(raw, 0, sizeof(raw));
	assert(venus_command(&controller, session, raw, 3U) == EINVAL);
	assert(session->native_commands_submitted == 0U);

	/* The native possibility is visible inside the host send, before completion or copyout exists. */
	quiesce_observed_session = session;
	assert(venus_command(&controller, session, raw, sizeof(raw)) == 0);
	assert(session->native_commands_submitted == 1U);
	quiesce_observed_session = NULL;
	assert(venus_stop_begin(&controller, session, EIO) == ENOTSUP);
	assert(session->quiesced == 0U);
	venus_close(&controller, session);

	/* An uncertain failed native send retains dirty ownership and cannot take the empty shortcut. */
	assert(venus_open(&controller, &private_session) == 0);
	session = private_session;
	quiesce_observed_session = session;
	fixture_timeout = 0x207U;
	error = venus_command(&controller, session, raw, sizeof(raw));
	assert(error == ETIMEDOUT);
	assert(session->native_commands_submitted == 1U);
	quiesce_observed_session = NULL;
	controller.transport.failed = 0U;
	controller.transport.quiesce = 1U;
	quiesce_ready = 0U;
	assert(venus_stop_begin(&controller, session, EIO) == 0);
	assert(venus_stop_poll(&controller, session) == EAGAIN);
	assert(session->quiesced == 0U);
	quiesce_ready = 1U;
	quiesce_idle_pending = 1U;
	assert(venus_stop_poll(&controller, session) == EAGAIN);
	assert(session->quiesced == 1U);
	quiesce_idle_pending = 0U;
	assert(venus_stop_poll(&controller, session) == 0);
	venus_close(&controller, session);
	assert(fixture_allocations == 0U && fixture_dma == 0U);
	puts("QUIESCE backend metadata-only/sticky-before-send/unknown-send/old-host refusal/two independent barriers PASS");
}
