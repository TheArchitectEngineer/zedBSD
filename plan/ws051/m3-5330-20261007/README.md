# M-3: USB-C の DP の正解値（5330、2026-10-07 Q1）

ユーザーの決定（2026-10-07、M-3「5330 で採取する」）に従い、design §14.5 の手順で採った。`/tmp/i915-hw.lock`（centris）の下で
`~/bigbang/igpu-mode.sh host` → 15 秒待つ → 読み取り → `igpu-mode.sh vfio`（2 回、両方とも vfio に戻した）。モニタはユーザーが USB-C の DP に挿した。

- host: Debian 13、Linux 6.19.13+deb13-amd64（参照の source の 6.8.12 と版が違う）。drm.debug は変えていない。intel-gpu-tools は Debian の package。
- 接続: DP-2 = TC Port E/TC#2、mode dp-alt、pin assignment C、max lanes 4、DPCD rev 1.4、1920x1280（plane 1B、pipe B）。eDP-1 も点灯。
- PLL: TC PLL 2（id 4）が pipe B で on。MG/DKL の値は `debugfs/i915_shared_dplls_info.txt`。TBT PLL は off（cfgcr0 0x600054）。
- files: `debugfs/`（i915_display_info・i915_shared_dplls_info・power_domain・dmc・cdclk・dp_mst・各 connector・DP-2 の EDID と modes・intel_reg dump）、
  1 回目の `range-0x*.txt`（intel_reg read の範囲: 0x160000〜（FIA・DKL）、0x16C000、0x44000、0x45000、0x46000、0x60000（transcoder）、0x64000（DDI_BUF）、0x70000（pipe・plane）、0xC4000）、`intel_reg-dump.txt`、`dmesg.txt`（i915・typec・ucsi の行）。
  1 回目の debugfs の file は path の誤りで空（2 回目の `debugfs/` を使う）。
