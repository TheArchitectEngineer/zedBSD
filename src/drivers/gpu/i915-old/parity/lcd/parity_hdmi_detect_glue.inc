/*
 * WS031 E-123 -- zedBSD glue of intel_hdmi_detect_port.c (included at its end).  zedBSD project code.
 * What the reference's intel_hdmi_set_edid() calls outside i915:
 *  - drm_edid_read_ddc(): the contract of drm_edid.c (base block + extensions over the connector's DDC adapter, each
 *    block's header / checksum checked) through parity_drm_edid_read() of dp/ (the reference's drm_do_probe_ddc_edid);
 *    the EDID is kept in one buffer per connector (the reference allocates a struct drm_edid);
 *  - drm_edid_connector_update(): the part the hotplug path depends on -- the epoch counter goes up when a previously
 *    stored EDID changes (_drm_edid_connector_property_update); display-info parsing and the property are not ported;
 *  - drm_edid_is_digital(): EDID byte 20 bit 7 (DRM_EDID_INPUT_DIGITAL);
 *  - intel_hdmi_dp_dual_mode_detect(): the DP dual-mode adaptor probe (I2C 0x40) is a recorded step: none.
 */
#include "parity_hotplug.h"

#define HPD_EDID_MAX_BLOCKS 4u

struct drm_edid { const u8 *edid; size_t size; };

static struct {
	u8 buf[HPD_EDID_MAX_BLOCKS * 128u];
	struct drm_edid e;
	u8 stored[HPD_EDID_MAX_BLOCKS * 128u];   /* the connector's EDID property (what the epoch compares) */
	size_t stored_size;
	int have_stored;
} hpd_edid[PARITY_HPD_MAX_CONNECTORS];
static struct parity_hpd_edid_info hpd_edid_last;
static unsigned hpd_edid_reads, hpd_edid_fails;

static unsigned hpd_conn_index(struct drm_connector *connector)
{
	return (unsigned)(connector->base.id - 1) % PARITY_HPD_MAX_CONNECTORS;
}

const struct drm_edid *drm_edid_read_ddc(struct drm_connector *connector, struct i2c_adapter *adapter)
{
	unsigned idx = hpd_conn_index(connector), ext = 0u;
	int rc;
	const u8 *b;

	hpd_edid_reads++;
	if (adapter == NULL)
		return NULL;
	rc = parity_drm_edid_read(adapter, hpd_edid[idx].buf, HPD_EDID_MAX_BLOCKS, &ext);
	memset(&hpd_edid_last, 0, sizeof(hpd_edid_last));
	hpd_edid_last.rc = rc;
	if (rc < 1) {
		hpd_edid_fails++;
		kern_logf("i915: parity hpd EDID read over %s failed: rc %d\n", adapter->name, rc);
		return NULL;
	}
	b = hpd_edid[idx].buf;
	hpd_edid[idx].e.edid = b;
	hpd_edid[idx].e.size = (size_t)rc * 128u;
	hpd_edid_last.blocks = (unsigned)rc;
	hpd_edid_last.extensions = ext;
	hpd_edid_last.mfg[0] = (char)('A' - 1 + ((b[8] >> 2) & 0x1f));
	hpd_edid_last.mfg[1] = (char)('A' - 1 + (((b[8] & 3) << 3) | (b[9] >> 5)));
	hpd_edid_last.mfg[2] = (char)('A' - 1 + (b[9] & 0x1f));
	hpd_edid_last.product = (unsigned)b[10] | ((unsigned)b[11] << 8);
	hpd_edid_last.serial = (unsigned)b[12] | ((unsigned)b[13] << 8) | ((unsigned)b[14] << 16) | ((unsigned)b[15] << 24);
	hpd_edid_last.version = b[18];
	hpd_edid_last.revision = b[19];
	hpd_edid_last.digital = (b[20] & 0x80u) != 0u;
	hpd_edid_last.checksum = b[127];
	/* the first detailed timing descriptor (the preferred mode) */
	hpd_edid_last.pixel_clock_khz = ((unsigned)b[54] | ((unsigned)b[55] << 8)) * 10u;
	hpd_edid_last.hactive = (unsigned)b[56] | (((unsigned)b[58] & 0xf0u) << 4);
	hpd_edid_last.vactive = (unsigned)b[59] | (((unsigned)b[61] & 0xf0u) << 4);
	kern_logf("i915: parity hpd EDID %s: %u block(s) (extensions %u) header ok, checksum ok | %s product 0x%04x serial 0x%08x "
		"EDID %u.%u %s | DTD1 %ux%u pixel clock %u kHz\n", connector->name, (unsigned)rc, ext, hpd_edid_last.mfg,
		hpd_edid_last.product, hpd_edid_last.serial, hpd_edid_last.version, hpd_edid_last.revision,
		hpd_edid_last.digital ? "digital" : "analog", hpd_edid_last.hactive, hpd_edid_last.vactive,
		hpd_edid_last.pixel_clock_khz);
	return &hpd_edid[idx].e;
}

void drm_edid_connector_update(struct drm_connector *connector, const struct drm_edid *drm_edid)
{
	unsigned idx = hpd_conn_index(connector);
	size_t size = drm_edid != NULL ? drm_edid->size : 0u;

	/* _drm_edid_connector_property_update(): a changed EDID bumps the epoch when one was stored before */
	if (hpd_edid[idx].have_stored &&
	    (size != hpd_edid[idx].stored_size || (size != 0u && memcmp(drm_edid->edid, hpd_edid[idx].stored, size) != 0))) {
		connector->epoch_counter++;
		drm_dbg_kms(connector->dev, "[CONNECTOR:%d:%s] EDID changed, epoch counter %llu\n",
			connector->base.id, connector->name, connector->epoch_counter);
	}
	hpd_edid[idx].stored_size = size;
	if (size != 0u)
		memcpy(hpd_edid[idx].stored, drm_edid->edid, size);
	hpd_edid[idx].have_stored = 1;
}

bool drm_edid_is_digital(const struct drm_edid *drm_edid)
{
	return drm_edid != NULL && drm_edid->edid != NULL && (drm_edid->edid[20] & 0x80u) != 0u;
}

void drm_edid_free(const struct drm_edid *drm_edid)
{
	(void)drm_edid;     /* the buffer belongs to the connector */
}

static void intel_hdmi_dp_dual_mode_detect(struct drm_connector *connector)
{
	kern_logf("i915: parity hpd step intel_hdmi_dp_dual_mode_detect (DP dual-mode probe not ported): %s -> none\n",
		connector->name);
}

static const struct drm_connector_funcs hpd_hdmi_connector_funcs = { .detect = intel_hdmi_detect };

const struct drm_connector_funcs *parity_hpd_hdmi_connector_funcs(void)
{
	return &hpd_hdmi_connector_funcs;
}

void parity_hpd_edid_info(struct parity_hpd_edid_info *out)
{
	*out = hpd_edid_last;
	out->reads = hpd_edid_reads;
	out->fails = hpd_edid_fails;
}

const u8 *parity_hpd_edid_bytes(unsigned idx, unsigned *size)
{
	if (idx >= PARITY_HPD_MAX_CONNECTORS || hpd_edid[idx].e.edid == NULL) {
		*size = 0u;
		return NULL;
	}
	*size = (unsigned)hpd_edid[idx].e.size;
	return hpd_edid[idx].e.edid;
}

void parity_hpd_edid_forget(void)
{
	memset(hpd_edid, 0, sizeof(hpd_edid));
	memset(&hpd_edid_last, 0, sizeof(hpd_edid_last));
	hpd_edid_reads = hpd_edid_fails = 0u;
}
