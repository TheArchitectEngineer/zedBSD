/*
 * WS031 Linux-parity -- what the extracted intel_opregion.c text needs.  zedBSD project code.
 * The mailbox layout, struct intel_opregion and the functions are the reference's text (generated); this header maps
 * the Linux types and the notifier interface onto zedBSD (opregion_service.h).  The OpRegion service's backend is
 * chosen when the instance is bound: SHADOW (driver-owned RAM in the OpRegion format, for synthetic tests) today;
 * FIRMWARE (the real shared region) only after the runtime protocol is accepted (not enabled: VBT_ONLY).
 */
#ifndef PARITY_OPREGION_COMPAT_H
#define PARITY_OPREGION_COMPAT_H

/* the extracted reference text keeps its own style (unused parameters / helpers), as in lcd_compat.h */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "../opregion_service.h"
#include "../backend_sync.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef BIT
#define BIT(n) (1u << (n))
#endif
#ifndef container_of
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#endif
#define ACPI_VIDEO_CLASS "video"

/* the workqueue item of the shared backend (backend_sync.h) */
struct work_struct { struct parity_kwork kwork; };

#define register_acpi_notifier(nb) parity_register_acpi_notifier(nb)
#define unregister_acpi_notifier(nb) parity_unregister_acpi_notifier(nb)

struct opregion_header;
struct opregion_acpi;
struct opregion_swsci;
struct opregion_asle;
struct opregion_asle_ext;
#include "opreg_struct.h"       /* reference, extracted: struct intel_opregion (display/intel_opregion.h) */

/* the device the extracted functions take: only the OpRegion state */
struct drm_i915_private { struct { struct intel_opregion opregion; } display; };

#endif /* PARITY_OPREGION_COMPAT_H */
