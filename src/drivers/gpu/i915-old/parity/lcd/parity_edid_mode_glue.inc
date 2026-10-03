/*
 * WS031 Linux-parity — zedBSD glue at the end of drm_edid_mode_port.c.
 *
 * Picks the panel's preferred timing: the first detailed timing descriptor of the
 * base block whose pixel clock is not zero (EDID 1.3 with the preferred-timing
 * feature bit, and EDID 1.4 always, define the first detailed timing as the
 * preferred one; descriptors with a zero pixel clock are display descriptors, not
 * timings) and converts it with the reference's drm_mode_detailed().  No EDID
 * quirk is applied: the quirk table is keyed by vendor / product and is not
 * ported, so display_info.quirks stays 0.
 */
static struct drm_display_mode edid_mode_object;
static int edid_mode_taken;

struct drm_display_mode *drm_mode_create(struct drm_device *dev)
{
	(void)dev;
	if (edid_mode_taken)
		return 0;
	edid_mode_taken = 1;
	memset(&edid_mode_object, 0, sizeof(edid_mode_object));
	return &edid_mode_object;
}

void drm_mode_set_name(struct drm_display_mode *mode)
{
	(void)mode;             /* the name is a label for logs; the caller prints the numbers */
}

int parity_edid_preferred_mode(const u8 *edid128, struct drm_display_mode *mode, unsigned *index)
{
	static struct drm_device dev;
	struct drm_connector connector;
	struct drm_edid drm_edid;
	const struct edid *edid = (const struct edid *)edid128;
	unsigned i;

	memset(&connector, 0, sizeof(connector));
	connector.name = "eDP-1";
	connector.dev = &dev;
	drm_edid.edid = edid;

	for (i = 0; i < ARRAY_SIZE(edid->detailed_timings); i++) {
		const struct detailed_timing *timing = &edid->detailed_timings[i];
		struct drm_display_mode *m;

		if (le16_to_cpu(timing->pixel_clock) == 0)
			continue;
		edid_mode_taken = 0;
		m = drm_mode_detailed(&connector, &drm_edid, timing);
		if (m == 0)
			return -EINVAL;
		*mode = *m;
		mode->type |= DRM_MODE_TYPE_PREFERRED;
		if (index != 0)
			*index = i;
		return 0;
	}
	return -EINVAL;
}
