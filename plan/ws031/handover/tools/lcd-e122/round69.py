#!/usr/bin/env python3
"""WS031 E-122 round 69: OpRegion unit 2b -- the reference lifecycle on the SHADOW backend: intel_opregion_setup (via a
memremap table: ASLS token -> the shadow, ASLS + RVDA -> a shadow VBT), register / resume(_display) / suspend(_display)
/ unregister / cleanup, DIDL / CADL from the service's connectors (intel_acpi_device_id_update, generated from
intel_acpi.c), the SWSCI-absent branch (check_swsci_function / swsci / notify_adapter), the DMI table (no DMI data in
zedBSD: dmi_check_system matches nothing -- recorded), _DSM (no AML: recorded boundary).
usage: round69.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
J = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
NL = chr(10)
TAB = chr(9)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

j = json.load(open(J))
if not any(x["out"] == "opreg_pci_config.h" for x in j["range_headers"]):
    j["range_headers"].append({"dir": "i915", "out": "opreg_pci_config.h", "path": "drivers/gpu/drm/i915/intel_pci_config.h",
        "source": "intel_pci_config.h",
        "ranges": [["#define ASLE" + TAB * 5 + "0xe4", "(1 << 0)" + NL, "ASLE, ASLS, SWSCI, SWSCI_SCISEL, SWSCI_GSSCIE"]]})
for f in j["new_files"]:
    if f["out"] == "intel_opregion_port.c":
        for n in ("check_swsci_function", "swsci", "intel_opregion_notify_adapter", "set_did", "intel_didl_outputs",
                  "intel_setup_cadls", "intel_no_opregion_vbt_callback", "intel_load_vbt_firmware", "intel_opregion_setup",
                  "intel_opregion_register", "intel_opregion_resume_display", "intel_opregion_resume",
                  "intel_opregion_suspend_display", "intel_opregion_suspend", "intel_opregion_unregister",
                  "intel_opregion_cleanup"):
            if n not in f["functions"]:
                f["functions"].append(n)
        labels = [r[2] for r in f["ranges"]]
        if "power_state_map" not in labels:
            f["ranges"].append(["static const struct {" + NL + TAB + "pci_power_t pci_power_state;", "};" + NL, "power_state_map"])
        if "intel_no_opregion_vbt" not in labels:
            f["ranges"].append(["static const struct dmi_system_id intel_no_opregion_vbt[] = {", NL + "};" + NL, "intel_no_opregion_vbt"])
if not any(x["out"] == "intel_acpi_port.c" for x in j["new_files"]):
    j["new_files"].append({"dir": "i915", "out": "intel_acpi_port.c", "path": "drivers/gpu/drm/i915/display/intel_acpi.c",
        "source": "display/intel_acpi.c", "includes": ["opregion_compat.h"], "glue": "parity_acpi_glue.inc",
        "functions": ["acpi_display_type", "intel_acpi_device_id_update"],
        "ranges": [["#define ACPI_DISPLAY_INDEX_SHIFT", "(1ULL << 31)" + NL, "ACPI _DOD display id encodings"]]})
json.dump(j, open(J, "w"), indent=1, sort_keys=True)

open(L + "parity_acpi_glue.inc", "w").write("/* WS031 Linux-parity -- zedBSD glue of intel_acpi_port.c: none needed (the extracted functions use opregion_compat.h). */" + NL)

c = open(L + "opregion_compat.h").read()
c = rep(c, "struct drm_connector_state { unsigned target; };" + NL + "struct drm_connector { const struct drm_connector_state *state; };" + NL +
        "struct intel_connector { struct drm_connector base; };",
        "struct drm_connector_state { unsigned target; };" + NL +
        "struct drm_connector { const struct drm_connector_state *state; int connector_type; };" + NL +
        "struct intel_connector { struct drm_connector base; u32 acpi_device_id; };")
c = rep(c, "struct drm_device { struct { struct drm_modeset_lock connection_mutex; } mode_config; };",
        "struct drm_device { struct { struct drm_modeset_lock connection_mutex; } mode_config; void *dev; };")
c = rep(c, "struct drm_i915_private { struct drm_device drm; struct { struct intel_opregion opregion; } display; struct parity_kworkqueue *unordered_wq; };",
        "struct drm_i915_private { struct drm_device drm; struct { struct intel_opregion opregion; struct { const char *vbt_firmware; } params; } display;" + NL +
        "	struct parity_kworkqueue *unordered_wq; };")
c = rep(c, '#include "opreg_struct.h"       /* reference, extracted: struct intel_opregion (display/intel_opregion.h) */',
        '#include "opreg_struct.h"       /* reference, extracted: struct intel_opregion (display/intel_opregion.h) */' + NL +
        '#include "opreg_pci_config.h"   /* reference, extracted: ASLS / SWSCI config offsets (intel_pci_config.h) */')
c = c.rstrip(NL)
assert c.endswith("#endif /* PARITY_OPREGION_COMPAT_H */")
c = c[:-len("#endif /* PARITY_OPREGION_COMPAT_H */")] + r"""/* ---- unit 2b: the lifecycle's surroundings ---- */
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

#endif /* PARITY_OPREGION_COMPAT_H */
"""
open(L + "opregion_compat.h", "w").write(c)
print("done")
