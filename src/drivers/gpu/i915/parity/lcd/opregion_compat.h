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
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"

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

/* the workqueue item of the shared backend (backend_sync.h): INIT_WORK / queue_work map onto parity_kwork */
struct work_struct { struct parity_kwork kwork; void (*func)(struct work_struct *work); };
struct parity_opregion_worker_stats { unsigned started, finished, queued_new, queued_pending; };
extern struct parity_opregion_worker_stats parity_opregion_wstats;
static inline void parity_opregion_work_trampoline(void *ctx)
{
	struct work_struct *w = ctx;

	parity_opregion_wstats.started++;
	w->func(w);
	parity_opregion_wstats.finished++;
}
#define INIT_WORK(w, f) do { (w)->func = (f); parity_kwork_init(&(w)->kwork, parity_opregion_work_trampoline, (w)); } while (0)
static inline int parity_opregion_queue_work(struct parity_kworkqueue *wq, struct work_struct *w)
{
	int r = wq != 0 ? parity_kqueue_work(wq, &w->kwork) : 0;

	if (r == 1)
		parity_opregion_wstats.queued_new++;
	else
		parity_opregion_wstats.queued_pending++;   /* already pending: not a failure */
	return r;
}
#define queue_work(wq, w) parity_opregion_queue_work((wq), (w))

/* diagnostics of the extracted text: kept silent (the service logs its own records) */
#define drm_dbg(dev, ...) ((void)(dev))
#define drm_dbg_kms(dev, ...) ((void)(dev))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* acpi_video_get_backlight_type(): the backlight policy -- an explicit input of the service configuration */
enum acpi_backlight_type { acpi_backlight_undef = -1, acpi_backlight_none = 0, acpi_backlight_video, acpi_backlight_vendor,
	acpi_backlight_native };
int parity_opregion_backlight_policy(void);
#define acpi_video_get_backlight_type() ((enum acpi_backlight_type)parity_opregion_backlight_policy())

/* the connection mutex and the connector walk of asle_set_backlight(): the service's registered backlight targets */
struct drm_modeset_lock { int unused; };
struct drm_device { struct { struct drm_modeset_lock connection_mutex; } mode_config; void *dev; };
void parity_opregion_connection_lock(void);
void parity_opregion_connection_unlock(void);
#define drm_modeset_lock(l, ctx) (parity_opregion_connection_lock(), 0)
#define drm_modeset_unlock(l) parity_opregion_connection_unlock()
struct drm_connector_state { unsigned target; };
struct drm_connector { const struct drm_connector_state *state; int connector_type; };
struct intel_connector { struct drm_connector base; u32 acpi_device_id; };
struct drm_connector_list_iter { unsigned idx; };
struct intel_connector *parity_opregion_connector_next(struct drm_connector_list_iter *it);
#define drm_connector_list_iter_begin(dev, it) ((it)->idx = 0u)
#define drm_connector_list_iter_end(it) ((void)(it))
#define for_each_intel_connector_iter(c, it) while (((c) = parity_opregion_connector_next(it)) != NULL)
void parity_opregion_backlight_set_acpi(const struct drm_connector_state *st, u32 level, u32 max);
#define intel_backlight_set_acpi(st, level, max) parity_opregion_backlight_set_acpi((st), (level), (max))

#define register_acpi_notifier(nb) parity_register_acpi_notifier(nb)
#define unregister_acpi_notifier(nb) parity_unregister_acpi_notifier(nb)

struct opregion_header;
struct opregion_acpi;
struct opregion_swsci;
struct opregion_asle;
struct opregion_asle_ext;
#include "opreg_struct.h"       /* reference, extracted: struct intel_opregion (display/intel_opregion.h) */
#include "opreg_pci_config.h"   /* reference, extracted: ASLS / SWSCI config offsets (intel_pci_config.h) */

/* the device the extracted functions take: only the OpRegion state */
struct drm_i915_private { struct drm_device drm; struct { struct intel_opregion opregion; struct { const char *vbt_firmware; } params; } display;
	struct parity_kworkqueue *unordered_wq; };

/* ---- unit 2b: the lifecycle's surroundings ---- */
#include <kern/klog.h>
#ifndef ENOTSUPP
#define ENOTSUPP 524            /* Linux-internal errno, as in the reference */
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define HAS_DISPLAY(i915) (1)
#define HAS_DDI(i915) (1)
typedef int pci_power_t;
#define PCI_D0 0
#define PCI_D1 1
#define PCI_D2 2
#define PCI_D3hot 3
#define PCI_D3cold 4
typedef uint64_t resource_size_t;
#define drm_err(dev, fmt, ...) kern_logf("i915: parity opregion (drm_err) " fmt, ##__VA_ARGS__)
#define DRM_INFO(fmt, ...) kern_logf("i915: parity opregion (DRM_INFO) " fmt, ##__VA_ARGS__)
#define WARN_ON(x) parity_opregion_warn_on(!!(x), #x)
#define drm_WARN_ON(dev, x) parity_opregion_warn_on(!!(x), #x)
int parity_opregion_warn_on(int cond, const char *what);
#define MISSING_CASE(x) kern_logf("i915: parity opregion MISSING_CASE %ld\n", (long)(x))
#define BUILD_BUG_ON(c) _Static_assert(!(c), "BUILD_BUG_ON")
/* DRM_MODE_CONNECTOR_* (include/uapi/drm/drm_mode.h, the UAPI values) */
#define DRM_MODE_CONNECTOR_Unknown 0
#define DRM_MODE_CONNECTOR_VGA 1
#define DRM_MODE_CONNECTOR_DVII 2
#define DRM_MODE_CONNECTOR_DVID 3
#define DRM_MODE_CONNECTOR_DVIA 4
#define DRM_MODE_CONNECTOR_Composite 5
#define DRM_MODE_CONNECTOR_SVIDEO 6
#define DRM_MODE_CONNECTOR_LVDS 7
#define DRM_MODE_CONNECTOR_Component 8
#define DRM_MODE_CONNECTOR_9PinDIN 9
#define DRM_MODE_CONNECTOR_DisplayPort 10
#define DRM_MODE_CONNECTOR_HDMIA 11
#define DRM_MODE_CONNECTOR_HDMIB 12
#define DRM_MODE_CONNECTOR_TV 13
#define DRM_MODE_CONNECTOR_eDP 14
#define DRM_MODE_CONNECTOR_VIRTUAL 15
#define DRM_MODE_CONNECTOR_DSI 16
void intel_acpi_device_id_update(struct drm_i915_private *dev_priv);

/* PCI config of the service instance: ASLS reads the instance's token; SWSCI config access is not provided (the target
 * has no SWSCI mailbox; any access is recorded as an error, never faked) */
struct pci_dev;
#define to_pci_dev(d) ((struct pci_dev *)(d))
int parity_opregion_pci_read32(struct pci_dev *pdev, int where, u32 *val);
int parity_opregion_pci_access_unported(const char *what);
#define pci_read_config_dword(pdev, where, val) parity_opregion_pci_read32((pdev), (where), (val))
#define pci_read_config_word(pdev, where, val) (*(val) = 0, parity_opregion_pci_access_unported("pci_read_config_word"))
#define pci_write_config_word(pdev, where, val) parity_opregion_pci_access_unported("pci_write_config_word")
#define pci_write_config_dword(pdev, where, val) parity_opregion_pci_access_unported("pci_write_config_dword")
#define msleep(ms) ((void)parity_opregion_pci_access_unported("msleep (SWSCI polling)"))

/* memremap / memunmap through the instance's mapping table (SHADOW: driver-owned RAM; FIRMWARE: not enabled) */
#define MEMREMAP_WB 1
void *parity_opregion_memremap(resource_size_t phys, size_t size, unsigned long flags);
void parity_opregion_memunmap(void *p);
#define memremap(p, s, f) parity_opregion_memremap((p), (s), (f))
#define memunmap(p) parity_opregion_memunmap(p)

/* VBT firmware (display.params.vbt_firmware = NULL here: the reference returns -ENOENT first) */
struct firmware { size_t size; const u8 *data; };
#define request_firmware(fw, name, dev) (-ENOENT)
#define release_firmware(fw) ((void)(fw))
#define kmemdup(p, n, gfp) ((void *)0)
#define kfree(p) ((void)(p))
#define GFP_KERNEL 0
int parity_vbt_validate(const void *bytes, size_t size);
#define intel_bios_is_valid_vbt(b, s) (parity_vbt_validate((b), (s)) != 0)

/* DMI: zedBSD has no DMI data source -- dmi_check_system() matches nothing (recorded; the table is kept) */
enum dmi_field { DMI_NONE, DMI_SYS_VENDOR, DMI_PRODUCT_NAME, DMI_PRODUCT_VERSION, DMI_BOARD_VENDOR, DMI_BOARD_NAME };
struct dmi_strmatch { unsigned char slot; char substr[79]; };
struct dmi_system_id { int (*callback)(const struct dmi_system_id *); const char *ident; struct dmi_strmatch matches[4]; void *driver_data; };
#define DMI_MATCH(a, b) { .slot = a, .substr = b }
int parity_opregion_dmi_check_system(const struct dmi_system_id *list);
#define dmi_check_system(list) parity_opregion_dmi_check_system(list)

/* ACPI _DSM evaluation (intel_dsm_get_bios_data_funcs_supported): no AML in zedBSD -- a recorded boundary */
void parity_opregion_boundary(const char *what);
#define intel_dsm_get_bios_data_funcs_supported(i915) parity_opregion_boundary("intel_dsm_get_bios_data_funcs_supported: ACPI _DSM (no AML)")

/* cancel_work_sync on the instance's queue */
int parity_opregion_cancel_work_sync(struct work_struct *w);
#define cancel_work_sync(w) parity_opregion_cancel_work_sync(w)

/* ---- round 69b ---- */
#define DRM_INFO_ONCE(fmt, ...) kern_logf("i915: parity opregion (DRM_INFO_ONCE) " fmt, ##__VA_ARGS__)
#define DRM_DEBUG_KMS(fmt, ...) ((void)0)
/* SWSCI polling (swsci()): not reachable without the SWSCI mailbox; reaching it is recorded, never faked */
#define wait_for(COND, MS) (parity_opregion_pci_access_unported("wait_for (SWSCI completion polling)"), -ETIMEDOUT)
#define swsci_setup(i915) parity_opregion_boundary("swsci_setup: the SWSCI mailbox (v2.x) is not ported -- absent on the target")
/* the reference's non-static functions (intel_opregion.h) and the DMI callback the table names */
int intel_opregion_setup(struct drm_i915_private *dev_priv);
void intel_opregion_cleanup(struct drm_i915_private *i915);
void intel_opregion_register(struct drm_i915_private *dev_priv);
void intel_opregion_unregister(struct drm_i915_private *dev_priv);
void intel_opregion_resume(struct drm_i915_private *dev_priv);
void intel_opregion_suspend(struct drm_i915_private *dev_priv, pci_power_t state);
void intel_opregion_asle_intr(struct drm_i915_private *dev_priv);
int intel_opregion_notify_adapter(struct drm_i915_private *dev_priv, pci_power_t state);
static int intel_no_opregion_vbt_callback(const struct dmi_system_id *id);

#endif /* PARITY_OPREGION_COMPAT_H */
