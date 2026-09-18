# DRM core reference files (upstream stable v6.8.12)

The fixed i915 reference tree (`../ubu-i915-src`) does not contain the DRM core.  These three files were fetched on
2026-09-18 from the kernel.org stable tree at tag `v6.8.12`
(`https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/<path>?h=v6.8.12`) and are the input of
`plan/ws031/handover/tools/port_dp_aux_pps.py`.  Unmodified.

| file | path in the kernel tree | sha256 |
|---|---|---|
| drm_dp_helper.c | drivers/gpu/drm/display/drm_dp_helper.c | 030568524ac5db3fbd09725df196b22a430ec18fc1432dbb952298ce7c791a73 |
| drm_dp.h | include/drm/display/drm_dp.h | 306a1a47ba001c417baa3dab1f3a58c7e5de3c7a0537d26806a130603b599c5f |
| drm_edid.c | drivers/gpu/drm/drm_edid.c | a01138078180d234149ac4403839a99b9c85232955a9830ad5552637cd8661a7 |

Not yet compared with the same files of the positive-control environment (Ubuntu 6.8.0-139.139).
