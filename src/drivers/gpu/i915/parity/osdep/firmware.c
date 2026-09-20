/*
 * WS031 Linux-parity — read-only firmware provider (see firmware.h).
 */
#include "firmware.h"
#include <errno.h>

/* Embedded reference blobs (firmware_adlp_dmc.c). */
extern const unsigned char parity_fw_adlp_dmc[];
extern const unsigned parity_fw_adlp_dmc_size;
/* An explicit, machine-specific VBT (firmware_vbt_dell_latitude_5330.c); see bios.c for when it is used. */
extern const unsigned char parity_fw_vbt_dell_latitude_5330[];
extern const unsigned parity_fw_vbt_dell_latitude_5330_size;
extern const unsigned char parity_fw_tgl_dmc[];
extern const unsigned parity_fw_tgl_dmc_size;
extern const unsigned char parity_fw_vbt_dell_latitude_5320[];
extern const unsigned parity_fw_vbt_dell_latitude_5320_size;

struct fw_entry {
	const char *name;
	const unsigned char *data;
	const unsigned *size;
};

static const struct fw_entry fw_table[] = {
	{ "i915/adlp_dmc.bin", parity_fw_adlp_dmc, &parity_fw_adlp_dmc_size },
	{ "i915/tgl_dmc_ver2_12.bin", parity_fw_tgl_dmc, &parity_fw_tgl_dmc_size },
	{ "zedbsd/vbt/dell-latitude-5330-1028-0b02.vbt", parity_fw_vbt_dell_latitude_5330,
	  &parity_fw_vbt_dell_latitude_5330_size },
	{ "zedbsd/vbt/dell-latitude-5320-1028-0a1f.vbt", parity_fw_vbt_dell_latitude_5320,
	  &parity_fw_vbt_dell_latitude_5320_size },
};

static const struct osdep_firmware_test_ops *g_fw_test;

void
osdep_firmware_test_set(const struct osdep_firmware_test_ops *ops)
{
	g_fw_test = ops;
}

static int
name_eq(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return (*a == *b) ? 1 : 0;
}

int
osdep_request_firmware(struct osdep_firmware *fw, const char *name)
{
	unsigned i;

	if (g_fw_test != 0)
		return g_fw_test->request(g_fw_test->ctx, fw, name);

	for (i = 0u; i < sizeof(fw_table) / sizeof(fw_table[0]); i++) {
		if (name_eq(name, fw_table[i].name)) {
			fw->data = fw_table[i].data;
			fw->size = *fw_table[i].size;
			return 0;
		}
	}
	fw->data = 0;   /* genuine absence: -ENOENT, no bytes */
	fw->size = 0u;
	return -ENOENT;
}

void
osdep_release_firmware(struct osdep_firmware *fw)
{
	/* The blob is static read-only data: drop the handle only. */
	fw->data = 0;
	fw->size = 0u;
}
