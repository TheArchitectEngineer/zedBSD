// SPDX-License-Identifier: GPL-2.0
/*
 * Intel ACPI functions
 *
 * _DSM related code stolen from nouveau_acpi.c.
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_acpi.c
 * (sha256 7abb35c45a67aa587d440586a7cb10811fd1f48875590dd7ea13a9928472c0d3) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: acpi_display_type, intel_acpi_device_id_update;
 *  - the includes are replaced by: opregion_compat.h;
 *  - parity_acpi_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "opregion_compat.h"

#define ACPI_DISPLAY_INDEX_SHIFT		0
#define ACPI_DISPLAY_INDEX_MASK			(0xf << 0)
#define ACPI_DISPLAY_PORT_ATTACHMENT_SHIFT	4
#define ACPI_DISPLAY_PORT_ATTACHMENT_MASK	(0xf << 4)
#define ACPI_DISPLAY_TYPE_SHIFT			8
#define ACPI_DISPLAY_TYPE_MASK			(0xf << 8)
#define ACPI_DISPLAY_TYPE_OTHER			(0 << 8)
#define ACPI_DISPLAY_TYPE_VGA			(1 << 8)
#define ACPI_DISPLAY_TYPE_TV			(2 << 8)
#define ACPI_DISPLAY_TYPE_EXTERNAL_DIGITAL	(3 << 8)
#define ACPI_DISPLAY_TYPE_INTERNAL_DIGITAL	(4 << 8)
#define ACPI_VENDOR_SPECIFIC_SHIFT		12
#define ACPI_VENDOR_SPECIFIC_MASK		(0xf << 12)
#define ACPI_BIOS_CAN_DETECT			(1 << 16)
#define ACPI_DEPENDS_ON_VGA			(1 << 17)
#define ACPI_PIPE_ID_SHIFT			18
#define ACPI_PIPE_ID_MASK			(7 << 18)
#define ACPI_DEVICE_ID_SCHEME			(1ULL << 31)

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static u32 acpi_display_type(struct intel_connector *connector);

static u32 acpi_display_type(struct intel_connector *connector)
{
	u32 display_type;

	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_VGA:
	case DRM_MODE_CONNECTOR_DVIA:
		display_type = ACPI_DISPLAY_TYPE_VGA;
		break;
	case DRM_MODE_CONNECTOR_Composite:
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_Component:
	case DRM_MODE_CONNECTOR_9PinDIN:
	case DRM_MODE_CONNECTOR_TV:
		display_type = ACPI_DISPLAY_TYPE_TV;
		break;
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
		display_type = ACPI_DISPLAY_TYPE_EXTERNAL_DIGITAL;
		break;
	case DRM_MODE_CONNECTOR_LVDS:
	case DRM_MODE_CONNECTOR_eDP:
	case DRM_MODE_CONNECTOR_DSI:
		display_type = ACPI_DISPLAY_TYPE_INTERNAL_DIGITAL;
		break;
	case DRM_MODE_CONNECTOR_Unknown:
	case DRM_MODE_CONNECTOR_VIRTUAL:
		display_type = ACPI_DISPLAY_TYPE_OTHER;
		break;
	default:
		MISSING_CASE(connector->base.connector_type);
		display_type = ACPI_DISPLAY_TYPE_OTHER;
		break;
	}

	return display_type;
}

void intel_acpi_device_id_update(struct drm_i915_private *dev_priv)
{
	struct drm_device *drm_dev = &dev_priv->drm;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	u8 display_index[16] = {};

	/* Populate the ACPI IDs for all connectors for a given drm_device */
	drm_connector_list_iter_begin(drm_dev, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		u32 device_id, type;

		device_id = acpi_display_type(connector);

		/* Use display type specific display index. */
		type = (device_id & ACPI_DISPLAY_TYPE_MASK)
			>> ACPI_DISPLAY_TYPE_SHIFT;
		device_id |= display_index[type]++ << ACPI_DISPLAY_INDEX_SHIFT;

		connector->acpi_device_id = device_id;
	}
	drm_connector_list_iter_end(&conn_iter);
}


#include "parity_acpi_glue.inc"
