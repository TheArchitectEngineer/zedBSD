/*
 * GPU-free contract tests for the Linux-parity PCI layer (rev2, M2 §1.1).
 * Verifies parent-API behaviour — resource-aware enable, enable refcount, MSI
 * setup with mid-failure cleanup — not just low-level bit writes.
 * Reference functions cited per group.
 */
#include <stdio.h>
#include "mock_pci.h"

static int g_fail;
static int g_checks;

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { printf("    FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

int
main(void)
{
	static struct osdep_trace trace;
	static struct osdep_pci p;
	struct mock_pci mock;

	printf("== PCI contract tests (mock config space, GPU-free) ==\n");
	osdep_trace_init(&trace);

	/* config access width [ref: pci_bus_read_config_*] */
	printf("[cfg] typed width access assembles little-endian\n");
	mock_pci_setup_full(&mock);
	osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
	CHECK(osdep_pci_read16(&p, 0x00) == 0x8086u, "read16 vendor");
	CHECK(osdep_pci_read32(&p, 0x00) == 0x46a88086u, "read32 = device<<16|vendor");
	osdep_pci_write8(&p, 0x40, 0xAAu);
	CHECK(osdep_pci_read8(&p, 0x41) == 0x50u, "write8 spared the next byte");
	mock_pci_setup_full(&mock);

	/* capability walk [ref: pci_find_capability] */
	printf("[cap] present caps found; absent -> 0 (never operate a missing cap)\n");
	CHECK(osdep_pci_find_capability(&p, OSDEP_PCI_CAP_ID_MSI) == 0x50u, "MSI at 0x50");
	CHECK(osdep_pci_find_capability(&p, OSDEP_PCI_CAP_ID_MSIX) == 0u, "MSI-X absent -> 0");

	/* BAR classification [ref: pci resource flags] */
	printf("[bar] BAR classification IO vs MEM\n");
	{
		uint64_t addr;
		CHECK(osdep_pci_bar_kind(&p, 0u, &addr) == OSDEP_PCI_RES_MEM, "BAR0 is MEM");
		CHECK(addr == 0xdf000000u, "BAR0 address decoded");
		CHECK(osdep_pci_bar_kind(&p, 1u, &addr) == OSDEP_PCI_RES_NONE, "BAR1 unused");
	}

	/* resource-aware enable [ref: pci_enable_resources] */
	printf("[enable] MEM-only device enables MEM decode, NOT IO\n");
	{
		uint16_t status_before = osdep_pci_read16(&p, OSDEP_PCI_STATUS);
		CHECK(osdep_pci_enable_device(&p) == 0, "enable ok");
		CHECK(osdep_pci_read16(&p, OSDEP_PCI_COMMAND) & OSDEP_PCI_CMD_MEMORY, "MEM enabled");
		CHECK(!(osdep_pci_read16(&p, OSDEP_PCI_COMMAND) & OSDEP_PCI_CMD_IO),
		      "IO NOT enabled (no IO resource)");
		CHECK(osdep_pci_read16(&p, OSDEP_PCI_STATUS) == status_before, "STATUS untouched");
		CHECK((osdep_pci_read16(&p, 0x44) & OSDEP_PCI_PM_STATE_MASK) == OSDEP_PCI_D0, "driven to D0");
	}
	printf("[enable] device with an IO BAR also enables IO decode\n");
	{
		mock_pci_setup_io_and_mem(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		CHECK(osdep_pci_enable_device(&p) == 0, "enable ok");
		CHECK(osdep_pci_read16(&p, OSDEP_PCI_COMMAND) & OSDEP_PCI_CMD_IO, "IO enabled (IO BAR present)");
		CHECK(osdep_pci_read16(&p, OSDEP_PCI_COMMAND) & OSDEP_PCI_CMD_MEMORY, "MEM enabled");
	}

	/* enable reference count [ref: pci_enable_device / pci_disable_device] */
	printf("[refcount] enable x2 / disable x1 leaves the device enabled\n");
	{
		mock_pci_setup_full(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		CHECK(osdep_pci_enable_device(&p) == 0, "enable #1");
		CHECK(osdep_pci_enable_device(&p) == 0, "enable #2 (refcount)");
		CHECK(osdep_pci_is_enabled(&p), "enabled");
		osdep_pci_disable_device(&p);
		CHECK(osdep_pci_is_enabled(&p), "still enabled after one disable (a user remains)");
		CHECK(osdep_pci_read16(&p, OSDEP_PCI_COMMAND) & OSDEP_PCI_CMD_MEMORY, "decode still on");
		osdep_pci_disable_device(&p);
		CHECK(!osdep_pci_is_enabled(&p), "disabled after last disable");
		CHECK(!(osdep_pci_read16(&p, OSDEP_PCI_COMMAND) & OSDEP_PCI_CMD_MEMORY), "decode cleared");
	}

	/* MSI setup present [ref: pci_enable_msi / arch msi compose] */
	printf("[msi] setup allocates a vector, builds message from it, then enables\n");
	{
		mock_pci_setup_full(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		CHECK(osdep_pci_setup_msi(&p) == 0, "setup_msi ok");
		CHECK(mock.alloc_calls == 1, "one vector allocated");
		CHECK(osdep_pci_read16(&p, 0x50 + OSDEP_PCI_MSI_FLAGS) & OSDEP_PCI_MSI_ENABLE, "enable bit set");
		CHECK(osdep_pci_read32(&p, 0x50 + OSDEP_PCI_MSI_ADDR_LO) == 0xFEE00000u, "arch message address");
		/* data derived from the allocated vector (0x21), not a fixed constant */
		CHECK((osdep_pci_read16(&p, 0x50 + OSDEP_PCI_MSI_DATA_64) & 0xFFu) == 0x21u,
		      "message data = allocated vector, not a fixed copy");
		CHECK(osdep_pci_msi_enabled(&p), "msi marked enabled");
		/* teardown frees the vector and clears enable */
		osdep_pci_teardown_msi(&p);
		CHECK(mock.free_calls == 1 && mock.last_free_vector == 0x21, "teardown freed the vector");
		CHECK(!(osdep_pci_read16(&p, 0x50 + OSDEP_PCI_MSI_FLAGS) & OSDEP_PCI_MSI_ENABLE), "enable cleared");
	}

	/* MSI absent -> non-fatal, no write [ref: pci_enable_msi returns -ENOSYS/-ENODEV] */
	printf("[msi] absent MSI cap: -ENODEV, no config write, fall back to INTx\n");
	{
		mock_pci_setup_no_msi(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		mock.write16_calls = 0; mock.write32_calls = 0;
		CHECK(osdep_pci_setup_msi(&p) == -19, "setup_msi -> -ENODEV");
		CHECK(mock.write16_calls == 0 && mock.write32_calls == 0, "no config write");
		CHECK(mock.alloc_calls == 0, "no vector allocated");
	}

	/* MSI vector allocation fails -> not enabled, nothing to free */
	printf("[msi] vector allocation failure: not enabled, no leak\n");
	{
		mock_pci_setup_full(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		mock.msi_alloc_fail = 1;
		CHECK(osdep_pci_setup_msi(&p) == -5, "setup_msi -> -EIO (alloc failed)");
		CHECK(!osdep_pci_msi_enabled(&p), "MSI not enabled");
		CHECK(!(osdep_pci_read16(&p, 0x50 + OSDEP_PCI_MSI_FLAGS) & OSDEP_PCI_MSI_ENABLE), "enable bit not set");
		CHECK(mock.free_calls == 0, "nothing to free (alloc failed)");
	}

	/* MSI mid-failure after alloc -> acquired vector released */
	printf("[msi] message-config failure after alloc releases the vector\n");
	{
		mock_pci_setup_full(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		mock.msi_alloc_bad = 1;   /* alloc returns a vector that cannot form a message */
		CHECK(osdep_pci_setup_msi(&p) < 0, "setup_msi fails on message config");
		CHECK(!osdep_pci_msi_enabled(&p), "not enabled");
		CHECK(mock.alloc_calls == 1 && mock.free_calls == 1, "the acquired vector was released");
	}

	/* power state without PM cap: no-op, no write */
	printf("[power] set_power_state with no PM cap writes nothing\n");
	{
		mock_pci_setup_no_pm(&mock);
		osdep_pci_init(&p, mock_pci_backend(), &mock, &trace);
		mock.write16_calls = 0;
		CHECK(osdep_pci_set_power_state(&p, OSDEP_PCI_D0) == 0, "returns 0");
		CHECK(mock.write16_calls == 0, "no write when PM cap absent");
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
