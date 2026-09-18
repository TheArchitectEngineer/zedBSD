/*
 * WS031 Linux-parity — intel_bios_init (see bios.h).
 *
 * Faithful port of intel_bios_init(): list init, init_vbt_defaults(), then a
 * REAL VBT acquisition -- OpRegion (carried from P2) first, else the PCI
 * expansion ROM read honestly through config space 0x30 + a device mapping and
 * scanned for the "$VBT" signature.  A found+validated VBT is parsed (BDB header
 * + block walk); a genuine absence (no ROM BAR, or no "$VBT") takes
 * init_vbt_missing_defaults(), which generates default child devices for the
 * non-TC DDI ports.  The DGFX SPI-flash path is intentionally NOT run on ADL-P
 * (not IS_DGFX).  Nothing here fabricates a VBT or touches the QEMU ROM config:
 * "absent" is a real read result, distinct from "unimplemented".
 */
#include "../internal.h"
#include <kern/klog.h>
#include "osdep/pci.h"
#include "parity.h"
#include "osdep/trace.h"
#include <hal/hal.h>
#include "bios.h"
#include "osdep/firmware.h"

/* vbt_header field offsets (signature[20],version,header_size,vbt_size,...). */
#define VBT_OFF_VBT_SIZE    24u   /* u16 */
#define VBT_OFF_BDB_OFFSET  28u   /* u32 */
#define VBT_HEADER_SIZE     48u
/* bdb_header field offsets (signature[16],version,header_size,bdb_size). */
#define BDB_OFF_VERSION     16u   /* u16 */
#define BDB_OFF_HEADER_SIZE 18u   /* u16 */
#define BDB_OFF_BDB_SIZE    20u   /* u16 */
#define BDB_HEADER_SIZE     22u

/* PHY / DVO_PORT / DEVICE_TYPE constants (intel_vbt_defs.h subset). */
#define PHY_A               0u
#define PHY_F               5u
#define PHY_I               8u
#define DVO_PORT_HDMIA      0u
#define DVO_PORT_HDMIE      12u
#define DVO_PORT_HDMIF      14u
#define DEVICE_TYPE_TMDS_DVI_SIGNALING  (1u << 4)
#define DEVICE_TYPE_DISPLAYPORT_OUTPUT  (1u << 2)
#define DEVICE_TYPE_INTERNAL_CONNECTOR  (1u << 12)

/* Copy buffer for a VBT lifted out of the PCI ROM (single-threaded probe). */
#define PARITY_VBT_MAX  8192u
static uint8_t parity_vbt_buf[PARITY_VBT_MAX];

static uint16_t
rd16(const uint8_t *b)
{
	return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static uint32_t
rd32(const uint8_t *b)
{
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int
parity_bios_is_valid_vbt(const void *buf, size_t size)
{
	const uint8_t *b = (const uint8_t *)buf;
	uint16_t vbt_size;
	uint32_t bdb_offset;
	uint16_t bdb_size;

	if (b == 0)
		return 0;
	if (size < VBT_HEADER_SIZE)
		return 0;
	if (b[0] != '$' || b[1] != 'V' || b[2] != 'B' || b[3] != 'T')
		return 0;

	vbt_size = rd16(b + VBT_OFF_VBT_SIZE);
	if ((size_t)vbt_size > size)
		return 0;
	size = vbt_size;

	bdb_offset = rd32(b + VBT_OFF_BDB_OFFSET);
	if ((size_t)bdb_offset + BDB_HEADER_SIZE > size)
		return 0;

	bdb_size = rd16(b + bdb_offset + BDB_OFF_BDB_SIZE);
	if ((size_t)bdb_offset + bdb_size > size)
		return 0;

	return 1;
}

int
parity_bios_process_vbt(struct parity_vbt_state *vbt, const void *buf, size_t size)
{
	const uint8_t *b = (const uint8_t *)buf;
	const uint8_t *bdb;
	uint32_t bdb_offset;
	uint16_t bdb_hdr_size, bdb_size;
	size_t off;

	if (!parity_bios_is_valid_vbt(buf, size))
		return -1;

	bdb_offset = rd32(b + VBT_OFF_BDB_OFFSET);
	bdb = b + bdb_offset;
	vbt->version = rd16(bdb + BDB_OFF_VERSION);
	bdb_hdr_size = rd16(bdb + BDB_OFF_HEADER_SIZE);
	bdb_size = rd16(bdb + BDB_OFF_BDB_SIZE);

	/*
	 * init_bdb_blocks(): walk the block list (each: id u8, size u16, data).
	 * We count blocks and note the general features/definitions/driver features
	 * blocks; child-device extraction from general_definitions is modelled as
	 * block presence, not fabricated into display_devices.
	 */
	vbt->num_bdb_blocks = 0;
	off = bdb_hdr_size;
	while (off + 3u <= (size_t)bdb_size) {
		uint16_t blen = rd16(bdb + off + 1u);

		vbt->num_bdb_blocks++;
		if (blen == 0u)
			break;
		off += 3u + (size_t)blen;
	}

	vbt->vbt_found = 1;
	return 0;
}

void
parity_bios_init_vbt_missing_defaults(struct parity_vbt_state *vbt)
{
	unsigned port;

	/*
	 * init_vbt_missing_defaults(): iterate PORT_A..PORT_F; skip the ports whose
	 * ADL-P phy is TC (PHY_F..PHY_I); generate a default child device for the
	 * rest.  On ADL-P (DISPLAY_VER 13) ports A/B/C map to PHY_A/B/C (combo) and
	 * ports D/E/F are TC1..TC3 -> PHY_F..PHY_H, so 3 defaults (A,B,C) result.
	 */
	vbt->num_display_devices = 0;
	for (port = 0u; port <= 5u; port++) {   /* PORT_A .. PORT_F */
		unsigned phy = (port < 3u) ? (PHY_A + port) : (PHY_F + (port - 3u));
		struct parity_vbt_child *c;

		if (phy >= PHY_F && phy <= PHY_I)
			continue;   /* intel_phy_is_tc(): TypeC, not generated here */
		if (vbt->num_display_devices >= 8u)
			break;

		c = &vbt->display_devices[vbt->num_display_devices++];
		c->port = port;
		if (port == 5u)
			c->dvo_port = DVO_PORT_HDMIF;
		else if (port == 4u)
			c->dvo_port = DVO_PORT_HDMIE;
		else
			c->dvo_port = (uint8_t)(DVO_PORT_HDMIA + port);

		c->device_type = 0u;
		if (port != 0u && port != 4u)
			c->device_type |= DEVICE_TYPE_TMDS_DVI_SIGNALING;
		if (port != 4u)
			c->device_type |= DEVICE_TYPE_DISPLAYPORT_OUTPUT;
		if (port == 0u)
			c->device_type |= DEVICE_TYPE_INTERNAL_CONNECTOR;
	}

	/* Bypass some minimum baseline VBT version checks (reference). */
	vbt->version = 155u;
	vbt->missing_defaults_used = 1;
}

/*
 * oprom_get_vbt(): map the PCI expansion ROM and lift a validated $VBT.  Returns
 * 1 with (*out,*out_size) set into parity_vbt_buf, or 0 on a genuine absence (no
 * ROM BAR, unmappable, or no valid $VBT).  ROM decode is left disabled on exit.
 */
static int
oprom_get_vbt(struct osdep_pci *pci, const void **out, size_t *out_size)
{
	uint32_t orig, base, probed, size32;
	size_t rom_size, i;
	void *rom = NULL;
	const volatile uint8_t *p;
	int found = -1;

	orig = osdep_pci_read32(pci, 0x30u);       /* PCI ROM base address register */
	base = orig & 0xFFFFF800u;
	if (base == 0u)
		return 0;                          /* no ROM BAR assigned: real absence */

	/* Size the ROM window (write 1s to the address bits, keep enable, restore). */
	osdep_pci_write32(pci, 0x30u, 0xFFFFF800u | (orig & 1u));
	probed = osdep_pci_read32(pci, 0x30u) & 0xFFFFF800u;
	osdep_pci_write32(pci, 0x30u, orig);
	if (probed == 0u)
		return 0;
	size32 = (~probed) + 1u;
	rom_size = (size_t)size32;
	if (rom_size > (256u * 1024u))
		rom_size = 256u * 1024u;           /* bound the mapping */

	osdep_pci_write32(pci, 0x30u, base | 1u);  /* enable ROM decode */
	if (hal_space_map_device((hal_physaddr_t)base, rom_size, HAL_SPACE_READ, &rom) != HAL_OK ||
	    rom == 0) {
		osdep_pci_write32(pci, 0x30u, orig);
		return 0;
	}

	p = (const volatile uint8_t *)rom;
	for (i = 0u; i + 4u < rom_size; i += 4u) {
		if (p[i] == '$' && p[i + 1u] == 'V' && p[i + 2u] == 'B' && p[i + 3u] == 'T') {
			found = (int)i;
			break;
		}
	}
	if (found >= 0) {
		size_t avail = rom_size - (size_t)found;

		if (avail >= VBT_HEADER_SIZE) {
			uint16_t vbt_size = (uint16_t)(p[(size_t)found + VBT_OFF_VBT_SIZE] |
				(p[(size_t)found + VBT_OFF_VBT_SIZE + 1u] << 8));

			if (vbt_size != 0u && (size_t)vbt_size <= avail &&
			    (size_t)vbt_size <= PARITY_VBT_MAX) {
				size_t k;

				for (k = 0u; k < (size_t)vbt_size; k++)
					parity_vbt_buf[k] = p[(size_t)found + k];
				if (parity_bios_is_valid_vbt(parity_vbt_buf, vbt_size)) {
					(void)hal_space_unmap_device(rom, rom_size);
					osdep_pci_write32(pci, 0x30u, orig);
					*out = parity_vbt_buf;
					*out_size = vbt_size;
					return 1;
				}
			}
		}
	}

	(void)hal_space_unmap_device(rom, rom_size);
	osdep_pci_write32(pci, 0x30u, orig);       /* leave ROM decode disabled */
	return 0;
}

/* ---------------- SHA-256 (FIPS 180-4), for pinning the explicit blob ---------------- */

static uint32_t sha_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

void
parity_sha256(const void *data, size_t len, uint8_t out[32])
{
	static const uint32_t K[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
	};
	uint32_t hs[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
	const uint8_t *msg = (const uint8_t *)data;
	uint64_t bits = (uint64_t)len * 8u;
	size_t total = ((len + 9u + 63u) / 64u) * 64u;
	size_t off;
	unsigned i;

	for (off = 0u; off < total; off += 64u) {
		uint32_t w[64], a, b, cc, d, e, f, g, hh;

		for (i = 0u; i < 16u; i++) {
			uint32_t word = 0u;
			unsigned k;

			for (k = 0u; k < 4u; k++) {
				size_t pos = off + i * 4u + k;
				uint8_t byte;

				if (pos < len)
					byte = msg[pos];
				else if (pos == len)
					byte = 0x80u;
				else if (pos >= total - 8u)
					byte = (uint8_t)(bits >> (8u * (total - 1u - pos)));
				else
					byte = 0u;
				word = (word << 8) | byte;
			}
			w[i] = word;
		}
		for (i = 16u; i < 64u; i++) {
			uint32_t s0 = sha_rotr(w[i - 15u], 7u) ^ sha_rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
			uint32_t s1 = sha_rotr(w[i - 2u], 17u) ^ sha_rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);

			w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
		}
		a = hs[0]; b = hs[1]; cc = hs[2]; d = hs[3]; e = hs[4]; f = hs[5]; g = hs[6]; hh = hs[7];
		for (i = 0u; i < 64u; i++) {
			uint32_t S1 = sha_rotr(e, 6u) ^ sha_rotr(e, 11u) ^ sha_rotr(e, 25u);
			uint32_t ch = (e & f) ^ (~e & g);
			uint32_t t1 = hh + S1 + ch + K[i] + w[i];
			uint32_t S0 = sha_rotr(a, 2u) ^ sha_rotr(a, 13u) ^ sha_rotr(a, 22u);
			uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
			uint32_t t2 = S0 + mj;

			hh = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
		}
		hs[0] += a; hs[1] += b; hs[2] += cc; hs[3] += d; hs[4] += e; hs[5] += f; hs[6] += g; hs[7] += hh;
	}
	for (i = 0u; i < 8u; i++) {
		out[i * 4u] = (uint8_t)(hs[i] >> 24); out[i * 4u + 1u] = (uint8_t)(hs[i] >> 16);
		out[i * 4u + 2u] = (uint8_t)(hs[i] >> 8); out[i * 4u + 3u] = (uint8_t)hs[i];
	}
}

/* ---------------- the reference parser's log hooks (vbt/vbt_compat.h) ---------------- */

void
parity_vbt_emit(const char *text)
{
	/* The message is the reference's format text; arguments are not rendered in the kernel. */
	kern_logf("i915: parity vbt: %s", text);
}

int
parity_vbt_fmtcheck(const char *fmt, ...)
{
	(void)fmt;
	return 0;
}

/*
 * The explicit blob: requested by the build, found in the read-only firmware
 * provider, byte-identical to the pinned capture (size + SHA-256), and only for
 * the machine it was read from (PCI subsystem id).  Any miss = not used.
 */
static const uint8_t explicit_blob_sha256[32] = { 0x3b, 0xff, 0x4a, 0x09, 0x20, 0xd5, 0x5c, 0x9a, 0xee, 0x0e, 0xa3, 0xc6, 0x78, 0x90, 0x4f, 0x98, 0x2f, 0x86, 0x71, 0xc0, 0xe7, 0xb5, 0xbc, 0x97, 0xa3, 0x35, 0xe4, 0x29, 0xb2, 0x96, 0x24, 0xcd };

static int
explicit_blob_get(struct parity_vbt_state *vbt, struct osdep_pci *pci,
	const void **out, size_t *out_size)
{
	static struct osdep_firmware fw;
	unsigned i;

	vbt->blob_requested = 1;
	vbt->blob_name = PARITY_VBT_EXPLICIT_NAME;
	vbt->subsys_vendor = osdep_pci_read16(pci, 0x2cu);
	vbt->subsys_device = osdep_pci_read16(pci, 0x2eu);
	vbt->blob_subsys_ok = vbt->subsys_vendor == PARITY_VBT_EXPLICIT_SUBSYS_VENDOR &&
		vbt->subsys_device == PARITY_VBT_EXPLICIT_SUBSYS_DEVICE;
	if (osdep_request_firmware(&fw, PARITY_VBT_EXPLICIT_NAME) != 0 || fw.data == 0)
		return 0;
	vbt->blob_found = 1;
	vbt->blob_size = fw.size;
	parity_sha256(fw.data, fw.size, vbt->blob_sha256);
	vbt->blob_hash_ok = 1;
	for (i = 0u; i < 32u; i++)
		if (vbt->blob_sha256[i] != explicit_blob_sha256[i])
			vbt->blob_hash_ok = 0;
	vbt->blob_valid = parity_vbt_validate(fw.data, fw.size);
	if (!vbt->blob_subsys_ok || !vbt->blob_hash_ok || !vbt->blob_valid) {
		osdep_release_firmware(&fw);
		return 0;
	}
	/* The blob is static read-only data: the pointer stays valid after the handle is dropped. */
	*out = fw.data;
	*out_size = fw.size;
	osdep_release_firmware(&fw);
	return 1;
}

int
parity_intel_bios_init_ex(struct parity_vbt_state *vbt, struct osdep_pci *pci,
	int opregion_has_vbt, int explicit_blob, struct osdep_trace *trace)
{
	const void *vbt_buf = 0;
	size_t vbt_size = 0;
	int origin = PARITY_VBT_ORIGIN_NONE;
	unsigned i;
	int rc;

	vbt->version = 0u;
	vbt->vbt_found = 0;
	vbt->source = PARITY_VBT_SRC_NONE;
	vbt->missing_defaults_used = 0;
	vbt->num_bdb_blocks = 0u;
	vbt->num_display_devices = 0u;
	vbt->parsed_live = 0;
	vbt->blob_name = 0;
	vbt->blob_size = 0u;
	vbt->blob_requested = vbt->blob_found = vbt->blob_hash_ok = vbt->blob_subsys_ok = vbt->blob_valid = 0;
	vbt->subsys_vendor = vbt->subsys_device = 0u;

	vbt->has_display = 1;   /* HAS_DISPLAY(ADL-P) */
	if (!vbt->has_display) {
		osdep_trace_emit(trace, PARITY_STAGE_P3, OSDEP_TR_NOTE,
			"intel_bios_init:skip_no_display", 0u, 0u);
		return 0;
	}

	/*
	 * VBT byte source, in order:
	 *  1. OpRegion (mailbox 4 / RVDA).  The probe reports whether P2 found one;
	 *     P2 does not retain the buffer yet, so this contributes no bytes today.
	 *  2. the explicit blob, only when the build asked for it and the machine
	 *     matches.  This is NOT "an OpRegion exists": ASLS and the OpRegion state
	 *     are left exactly as the firmware reported them.
	 *  3. the PCI ROM ($VBT).  ADL-P is not DGFX, so no SPI path.
	 */
	if (opregion_has_vbt)
		vbt->source = PARITY_VBT_SRC_OPREGION;   /* present but not lifted: no bytes */

	if (vbt_buf == 0 && explicit_blob && explicit_blob_get(vbt, pci, &vbt_buf, &vbt_size)) {
		vbt->source = PARITY_VBT_SRC_EXPLICIT_BLOB;
		origin = PARITY_VBT_ORIGIN_EXPLICIT_BLOB;
	}
	if (vbt_buf == 0 && oprom_get_vbt(pci, &vbt_buf, &vbt_size)) {
		if (parity_vbt_validate(vbt_buf, vbt_size)) {
			vbt->source = PARITY_VBT_SRC_PCI_ROM;
			origin = PARITY_VBT_ORIGIN_PCI_ROM;
		} else {
			vbt_buf = 0;
			vbt_size = 0u;
		}
	}

	/* intel_bios_init() proper: the reference text, on the chosen bytes (or none). */
	rc = parity_vbt_init(&vbt->parsed, vbt_buf, vbt_size, origin);
	if (rc != 0) {
		kern_logf("i915: parity P3 intel_bios_init: parser state busy/invalid rc=%d\n", rc);
		return rc;
	}
	vbt->parsed_live = 1;
	vbt->vbt_found = vbt_buf != 0;
	vbt->version = vbt->parsed.bdb_version;
	vbt->num_bdb_blocks = vbt->parsed.num_bdb_blocks;
	vbt->missing_defaults_used = vbt->parsed.missing_defaults_used;

	/*
	 * The child devices come from ONE place: the real VBT when there is one, the
	 * reference's init_vbt_missing_defaults() otherwise.  Never a mix.
	 */
	for (i = 0u; i < vbt->parsed.n_encoders && vbt->num_display_devices < 8u; i++) {
		const struct parity_vbt_encoder *e = &vbt->parsed.enc[i];
		struct parity_vbt_child *ch = &vbt->display_devices[vbt->num_display_devices++];

		ch->port = e->port >= 0 ? (unsigned)e->port : 0u;
		ch->dvo_port = e->dvo_port;
		ch->device_type = e->device_type;
	}

	if (!vbt->vbt_found)
		osdep_trace_emit(trace, PARITY_STAGE_P3, OSDEP_TR_NOTE,
			"intel_bios_init:vbt_absent", (uint64_t)(unsigned)vbt->source, 0u);
	else
		osdep_trace_emit(trace, PARITY_STAGE_P3, OSDEP_TR_ACQUIRE,
			"intel_bios_init:vbt_parsed", (uint64_t)vbt->version,
			(uint64_t)vbt->num_bdb_blocks);

	kern_logf("i915: parity P3 intel_bios_init: source=%d vbt_found=%d version=%u "
		"bdb_blocks=%u child_devices=%u missing_defaults=%d parser_errors=%u arena_peak=%u\n",
		vbt->source, vbt->vbt_found, (unsigned)vbt->version,
		vbt->num_bdb_blocks, vbt->num_display_devices, vbt->missing_defaults_used,
		vbt->parsed.log_errors, vbt->parsed.arena_peak);
	if (vbt->blob_requested)
		kern_logf("i915: parity P3 VBT explicit blob: name=%s requested=1 found=%d size=%u "
			"sha256=%02x%02x%02x%02x%02x%02x%02x%02x.. hash_ok=%d valid=%d subsys=%04x:%04x "
			"subsys_ok=%d used=%d (explicit supply; OpRegion present=%d is unchanged)\n",
			vbt->blob_name, vbt->blob_found, vbt->blob_size,
			vbt->blob_sha256[0], vbt->blob_sha256[1], vbt->blob_sha256[2], vbt->blob_sha256[3],
			vbt->blob_sha256[4], vbt->blob_sha256[5], vbt->blob_sha256[6], vbt->blob_sha256[7],
			vbt->blob_hash_ok, vbt->blob_valid, vbt->subsys_vendor, vbt->subsys_device,
			vbt->blob_subsys_ok, vbt->source == PARITY_VBT_SRC_EXPLICIT_BLOB, opregion_has_vbt);
	for (i = 0u; i < vbt->parsed.n_encoders; i++) {
		const struct parity_vbt_encoder *e = &vbt->parsed.enc[i];

		kern_logf("i915: parity P3 VBT child[%u]: port=%c dvo_port=%u type=0x%04x aux_ch=%d ddc_pin=%d "
			"dp=%d edp=%d hdmi=%d typec=%d tbt=%d max_lanes=%d max_rate=%d hpd_invert=%d lane_reversal=%d\n",
			i, e->port >= 0 ? (char)('A' + e->port) : '-', e->dvo_port, e->device_type, e->aux_ch,
			e->ddc_pin, e->supports_dp, e->supports_edp, e->supports_hdmi, e->supports_typec_usb,
			e->supports_tbt, e->dp_max_lane_count, e->dp_max_link_rate, e->hpd_invert, e->lane_reversal);
	}
	return 0;
}

int
parity_intel_bios_init(struct parity_vbt_state *vbt, struct osdep_pci *pci,
	int opregion_has_vbt, struct osdep_trace *trace)
{
	return parity_intel_bios_init_ex(vbt, pci, opregion_has_vbt, 0, trace);
}

void
parity_intel_bios_driver_remove(struct parity_vbt_state *vbt)
{
	if (vbt == 0 || !vbt->parsed_live)
		return;
	parity_vbt_fini(&vbt->parsed);
	vbt->parsed_live = 0;
}
