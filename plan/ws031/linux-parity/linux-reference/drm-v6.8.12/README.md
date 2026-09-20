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

Fetched on 2026-09-19 from the same tree and tag (E-111; input of `port_lcd_calc.py`: `drm_connector.h` now, the other four for the plane slice):

| file | path in the kernel tree | sha256 |
|---|---|---|
| drm_connector.h | include/drm/drm_connector.h | 1eb905598bb46d733afe49917276689bfbef7284f3395ffa72131d4ef14c4ce3 |
| drm_fourcc.h | include/uapi/drm/drm_fourcc.h | a9607fd835647f6feae3633bbb4ca00bb15e7ff229c9df18fdc0f19a820c61fe |
| drm_blend.h | include/drm/drm_blend.h | 46a47b37fcb9dbb1da3f98d58db0075d21b1a0cde21290c5d627d4ace05f044f |
| drm_color_mgmt.h | include/drm/drm_color_mgmt.h | 6332adb2c833e3a6ac6a7b1083446eb1e2964c3e2b7f40014309a067f0b8ee5e |
| uapi_drm_mode.h | include/uapi/drm/drm_mode.h (saved as uapi_drm_mode.h) | 6f1e99012854f40c59e62ba9ab031aa6e0f7354f41f25d0a9d23e6dfc6bd370b |
| i915_drm.h | include/uapi/drm/i915_drm.h (E-112) | 37fe8b9995b560a5a30209fc06ae41127fc7aaf68477164bed7d054638b3dfb3 |

Fetched on 2026-09-20 from the same tree and tag (E-123, HDMI hotplug: drm_helper_probe_detect / epoch counter, connector status names):

| file | path in the kernel tree | sha256 |
|---|---|---|
| drm_probe_helper.c | drivers/gpu/drm/drm_probe_helper.c | 05cedd4c3bd94a7a451d525433754952d8e48fa1dd9c8b918a0912247181e9c6 |
| drm_connector.c | drivers/gpu/drm/drm_connector.c | eacf432724ef7c03d6ee16dfb655aad9cbd7278987f24851fefe511d9afbfc07 |
