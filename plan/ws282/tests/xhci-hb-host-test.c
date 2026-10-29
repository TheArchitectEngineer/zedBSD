/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The xHCI isochronous endpoint context of a high-speed high-bandwidth
 * endpoint and the Isoch TRB's burst counts, on the host.  The values
 * are xHCI 1.2's 4.11.2.3, 4.14.2.1.1, 6.2.3 and Table 6-34.
 */

#include <drivers/pci/pci-xhci-control.h>
#include <drivers/pci/pci-xhci-isoch.h>

#include <stdio.h>
#include <stdlib.h>

static unsigned failures;

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	if (!good) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* The endpoint context: Max Burst Size, Max Packet Size and Max ESIT Payload. */
static void
test_context(
	void)
{
	struct drv_xhci_endpoint_context_words words;
	int made;

	/* High speed IN, 3 x 1024 a microframe, bInterval 1. */
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_HIGH, 5U, 0x1000U | 1024U, 1U, NULL, &words);
	check(made, "three transactions taken");
	check(words.word0 == 0U, "interval 0 (every microframe)");
	check(words.word1 == ((5U << 3) | (2U << 8) | (1024U << 16)), "max burst 2, packet 1024, CErr 0");
	check(words.word4 == ((3072U << 16) | 3072U), "ESIT payload and average TRB 3072");

	/* High speed IN, 2 x 960. */
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_HIGH, 5U, 0x0800U | 960U, 1U, NULL, &words);
	check(made && words.word1 == ((5U << 3) | (1U << 8) | (960U << 16)), "max burst 1, packet 960");
	check(words.word4 == ((1920U << 16) | 1920U), "ESIT payload 1920");

	/* One transaction keeps the earlier encoding. */
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_HIGH, 5U, 1024U, 4U, NULL, &words);
	check(made && words.word0 == (3U << 16), "high speed interval 3");
	check(words.word1 == ((5U << 3) | (1024U << 16)) && words.word4 == ((1024U << 16) | 1024U), "one transaction");

	/* Refused: the reserved count 3, bits 15:13, full speed with extra transactions, a high-bandwidth packet over 1024. */
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_HIGH, 5U, 0x1800U | 1024U, 1U, NULL, &words);
	check(!made, "the reserved extra count refused");
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_HIGH, 5U, 0x2000U | 512U, 1U, NULL, &words);
	check(!made, "bits 15:13 refused");
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_FULL, 5U, 0x0800U | 512U, 1U, NULL, &words);
	check(!made, "full speed with extra transactions refused");
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_HIGH, 5U, 0x0800U | 1100U, 1U, NULL, &words);
	check(!made, "a high-bandwidth packet over 1024 refused");
	made = drv_xhci_endpoint_context_encode(DRV_USB_SPEED_SUPER, 5U, 1024U, 1U, NULL, &words);
	check(!made, "SuperSpeed isochronous still refused");
}

/* Returns TBC from control bits. */
static unsigned
tbc(
	uint32_t bits)
{
	return (bits >> 7) & 3U;
}

/* Returns TLBPC from control bits. */
static unsigned
tlbpc(
	uint32_t bits)
{
	return (bits >> 16) & 15U;
}

/* The Isoch TRB's TBC and TLBPC. */
static void
test_burst(
	void)
{
	uint32_t bits;

	/* Three full packets in a burst of three: one burst of three. */
	bits = drv_xhci_isoch_burst_bits(0x110U, 1024U, 2U, 3072U);
	check(tbc(bits) == 0U && tlbpc(bits) == 2U, "3072 of 3 x 1024: TBC 0, TLBPC 2");

	/* A short interval: two packets, the second partial. */
	bits = drv_xhci_isoch_burst_bits(0x110U, 1024U, 2U, 1500U);
	check(tbc(bits) == 0U && tlbpc(bits) == 1U, "1500 of 3 x 1024: TBC 0, TLBPC 1");

	/* An empty interval counts one packet. */
	bits = drv_xhci_isoch_burst_bits(0x100U, 1024U, 2U, 0U);
	check(tbc(bits) == 0U && tlbpc(bits) == 0U, "0 bytes: TBC 0, TLBPC 0");

	/* One transaction an interval. */
	bits = drv_xhci_isoch_burst_bits(0x100U, 1024U, 0U, 1024U);
	check(bits == 0U, "1024 of 1 x 1024: both 0");

	/* The formula past one burst (TBC counts bursts less one). */
	bits = drv_xhci_isoch_burst_bits(0x100U, 1024U, 0U, 3072U);
	check(tbc(bits) == 2U && tlbpc(bits) == 0U, "3 packets in bursts of 1: TBC 2, TLBPC 0");
	bits = drv_xhci_isoch_burst_bits(0x100U, 1024U, 1U, 3072U);
	check(tbc(bits) == 1U && tlbpc(bits) == 0U, "3 packets in bursts of 2: TBC 1, TLBPC 0");

	/* Nothing before xHCI 1.0, or without a packet size. */
	check(drv_xhci_isoch_burst_bits(0x096U, 1024U, 2U, 3072U) == 0U, "0.96: no burst fields");
	check(drv_xhci_isoch_burst_bits(0x100U, 0U, 2U, 3072U) == 0U, "no packet size");

	/* Only bits 8:7 and 19:16. */
	bits = drv_xhci_isoch_burst_bits(0x120U, 1024U, 2U, 3072U);
	check((bits & ~((3U << 7) | (15U << 16))) == 0U, "no other bits");
}

int
main(
	void)
{
	test_context();
	test_burst();
	if (failures != 0U) {
		printf("xhci-hb-host-test: %u FAIL\n", failures);
		return EXIT_FAILURE;
	}
	printf("xhci-hb-host-test: PASS\n");
	return EXIT_SUCCESS;
}
