/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Checks exact stop wire fields and separates native proof from old descriptor retirement. */
static void
fixture_quiesce_proposal(
	void)
{
	struct venus_transport transport;
	struct venus_request *pending;
	struct drv_gpu_completion completions[32];
	void *occupied[28];
	void *reserved;
	void *posted;
	uint8_t *packet;
	uint8_t *ring;
	unsigned quiesced;
	unsigned bad;
	unsigned slot;
	unsigned available;
	int error;

	/* Old profiles must refuse native quiescence without reserving or posting a command. */
	fixture_prepare(&transport);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 3U;
	assert(drv_venus_transport_start(&transport, NULL) == 0);
	pending = NULL;
	quiesced = 0U;
	available = transport.available;
	assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == ENOTSUP);
	assert(pending == NULL && quiesced == 0U && transport.available == available);
	assert(drv_venus_transport_stop(&transport) == 0);

	/* An independently successful response of another type or length is still not a stop ACK. */
	for (bad = 0U; bad < 3U; bad++) {
		fixture_prepare(&transport);
		drv_venus_store16(fixture_registers + 24U, 64U);
		fixture_capset_bytes = 168U;
		fixture_vendor_magic = 0x5a424453U;
		fixture_vendor_flags = 7U;
		assert(drv_venus_transport_start(&transport, NULL) == 0);
		assert(transport.strict_queue == 1U && transport.quiesce == 1U);
		fixture_drop = 1U;
		fixture_clock_frozen = 1U;
		memset(completions, 0, sizeof(completions));
		assert(drv_venus_transport_job_reserve(&transport, 17U, 3U, &completions[0], &reserved) == 0);
		assert(drv_venus_transport_job_reserve(&transport, 17U, 3U, &completions[1], &posted) == 0);
		assert(drv_venus_transport_job_commit(&transport, posted, &completions[1]) == 0);

		/* Posting one stop command is nonblocking and does not end either existing owner. */
		pending = NULL;
		quiesced = 0U;
		assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == EAGAIN);
		assert(pending != NULL && pending->supervised == 1U && pending->completion == NULL);
		slot = (unsigned)(pending - transport.requests);
		packet = pending->request.address;
		ring = transport.ring.address;
		assert(drv_venus_load32(ring + slot * 32U + 8U) == 48U);
		assert(drv_venus_load32(packet) == 0x0207U);
		assert(drv_venus_load32(packet + 4U) == 3U);
		assert(drv_venus_load64(packet + 8U) != 0U);
		assert(drv_venus_load32(packet + 16U) == 17U && packet[20U] == 0U);
		assert(drv_venus_load32(packet + 24U) == 16U);
		assert(drv_venus_load32(packet + 32U) == 0x5a425351U);
		assert(drv_venus_load32(packet + 36U) == 1U);
		assert(drv_venus_load64(packet + 40U) == 0U);
		assert(completions[0].calls == 0U && completions[1].calls == 0U);
		assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == EAGAIN);

		/* The PCI peer returns the exact stop descriptor, independently from older native work. */
		fixture_complete_head((uint16_t)(slot * 2U));
		if (bad == 1U)
			drv_venus_store32(pending->response.address, 0x1103U);
		if (bad == 2U)
			drv_venus_store32(ring + 1280U + 8U + (transport.used % transport.queue_size) * 8U, 25U);
		assert(fixture_irq(fixture_irq_argument) == 1U);
		error = drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced);
		if (bad != 0U) {
			assert(error == EIO && quiesced == 0U && pending != NULL);
			assert(completions[0].calls == 0U && completions[1].calls == 0U);
			drv_venus_transport_fail(&transport, EIO);
			assert(completions[0].calls == 1U && completions[0].error != 0);
			assert(completions[1].calls == 1U && completions[1].error != 0);
		} else {
			assert(error == 0 && quiesced == 1U && pending == NULL);
			assert(completions[0].calls == 1U && completions[0].error == ENODEV);
			assert(completions[1].calls == 0U);
			assert(drv_venus_transport_idle(&transport, 17U) == EAGAIN);

			/* Native idle alone cannot recycle a previously posted DMA descriptor or its callback. */
			fixture_complete_head(2U);
			assert(fixture_irq(fixture_irq_argument) == 1U);
			assert(completions[1].calls == 1U && completions[1].error == 0);
			assert(drv_venus_transport_idle(&transport, 17U) == 0);
		}
		fixture_drop = 0U;
		assert(drv_venus_transport_stop(&transport) == 0);
		assert(fixture_dma == 0U && fixture_maps == 0U);
	}

	/* Full control storage returns immediately and never overwrites another context's descriptor. */
	fixture_prepare(&transport);
	drv_venus_store16(fixture_registers + 24U, 64U);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 7U;
	assert(drv_venus_transport_start(&transport, NULL) == 0);
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	memset(completions, 0, sizeof(completions));
	for (slot = 0U; slot < 28U; slot++) {
		assert(drv_venus_transport_job_reserve(&transport, 80U, 3U, &completions[slot], &occupied[slot]) == 0);
	}
	for (slot = 28U; slot < 32U; slot++) {
		assert(drv_venus_transport_submit(&transport, 80U, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 0U, &completions[slot]) == 0);
	}
	pending = NULL;
	quiesced = 0U;
	available = transport.available;
	assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == EAGAIN);
	assert(pending == NULL && quiesced == 0U && transport.available == available);
	drv_venus_transport_fail(&transport, EIO);
	fixture_drop = 0U;
	assert(drv_venus_transport_stop(&transport) == 0);
	assert(fixture_dma == 0U && fixture_maps == 0U);

	puts("QUIESCE guest exact48B/old-profile refusal/NODATA24B/retained posted DMA/full capacity/no false ACK PASS");
}
