// SPDX-License-Identifier: GPL-2.0
/*
 * Intel ACPI functions
 *
 * _DSM related code stolen from nouveau_acpi.c.
 */

/*
 * zedBSD display rebuild: the ACPI display id layout of Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_acpi.c (sha256
 * 7abb35c45a67aa587d440586a7cb10811fd1f48875590dd7ea13a9928472c0d3), as the
 * pre-rebuild tree's generated intel_acpi_port.c carried it (commit
 * 7e7ff337).  No definition is changed.
 *
 * Private to display/opregion.c.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_ACPI_DISPLAY_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_ACPI_DISPLAY_INC

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

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_ACPI_DISPLAY_INC */
