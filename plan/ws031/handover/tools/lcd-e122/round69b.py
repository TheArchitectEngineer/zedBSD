#!/usr/bin/env python3
"""WS031 E-122 round 69b: compat completion for the lifecycle text.  usage: round69b.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
c = open(L + "opregion_compat.h").read()
old = '#pragma GCC diagnostic ignored "-Wunused-function"' + NL
assert c.count(old) == 1
c = c.replace(old, old + '#pragma GCC diagnostic ignored "-Wunused-variable"' + NL + '#pragma GCC diagnostic ignored "-Wsign-compare"' + NL, 1)
tail = "#endif /* PARITY_OPREGION_COMPAT_H */"
assert c.rstrip(NL).endswith(tail)
c = c.rstrip(NL)[:-len(tail)] + r"""/* ---- round 69b ---- */
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

""" + tail + NL
open(L + "opregion_compat.h", "w").write(c)
print("done")
