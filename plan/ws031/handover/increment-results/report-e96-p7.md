# WS031 E-96 報告: P7 完成 — `i915_driver_probe` を最後まで実行

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

`i915_gem_init` の後に残っていた `intel_pxp_init` → `intel_display_driver_probe` → `i915_driver_register` を実装し、実機で **`i915_driver_probe` の最後まで到達**しました（`outcome=STOPPED where="i915_driver_probe complete"`）。これで、専門家方針「Linux の通常初期化を実装として完成させる」の主経路は parity 上で一通り踏破したことになります。

```
P7 pxp_init: rc=0 full=1 engine=vcs0 kcr_base=0x32000 (KCR/irq init は mei-pxp bind 時のため無し)
P7 display_driver_probe: rc=0 active_crtcs=0 initial_commit=0 ipc_enabled=1
P7 hpd_init: encoders=2 pins=A,B | SHPD_FILTER=0xf8 SDEIMR=0x3f043f07 SHOTPLUG_DDI=0x88 TC=0
P7 driver_register: power_domains_enable: wells_on 8 -> 2 dc_state=0x2 verify_mismatches=0 | runtime_pm: probe usage=0
P7 DC_STATE_EN=0x00000002 (DC6 武装, DMC payload あり)
teardown: INIT reference re-taken: wells_on=8 DC_STATE_EN=0
ktest: 365 checks, 0 failures（GPU-free / 実機とも）
```

- **電源**: `intel_power_domains_enable` が P3 以来保持していた INIT 参照を手放し、点いていた 8 本の well が 2 本（always_on と、所属 domain の無い PW_1）に減り、最後に DC_off well が落ちて **DC6 が武装**されました。撤収時に INIT を取り直すと元の 8 本・DC 無しに戻ります。参照数と HW 状態の照合（verify_state）は不一致 0。
- **hotplug**: エンコーダは port A/B なので、正本 `gen11_hpd_irq_setup` のとおり DE 側は不変・TC 系はクリア、PCH（ADP）側はフィルタ 250、SDEIMR の A/B をアンマスク、SHOTPLUG_CTL_DDI に A/B の enable。
- **PXP**: ADL-P は has_pxp・VDBOX ありで full feature。vcs0 に pinned context と streaming page を作るところまで（KCR 初期化は mei-pxp component の bind 時なので、ここでは走りません）。
- **IPC** 有効化。**initial_commit** は active crtc が 0 なので空コミット（正本と同値）。

## 実装

- `pxp.{c,h}`、`driver_probe.{c,h}`（hotplug ピン表・irq setup・poll disable・IPC・power_domains_enable/disable・runtime_pm enable/disable・register の N/A 一覧）。
- `power_domains`: DC_off well の **disable を正本化**（`gen9_dc_off_power_well_disable` → `skl_enable_dc6` → `gen9_set_dc_state` の再書込ループ、DMC payload が無ければ何もしない）。`is_enabled` を HW 読取りに。
- ktest +11（hotplug ピン／レジスタ、IPC、DC6 の 3 形、PXP の 3 形）。P5-d の well 検査は DC_off の HW 読取り化に合わせて更新。

## 記録した適応（正本との差）

- DRM object model が無いため：initial_commit は active crtc 0 の形のみ実行（active crtc があれば未実装として停止）、connector 検出と polling は簿記のみ、fbdev は CONFIG_DRM_FBDEV_EMULATION=n 相当。
- userspace 向け登録（drm_dev / debugfs / sysfs / pmu / perf / hwmon / audio component / acpi video / dsm / switcheroo）は N/A として件数を記録。
- `intel_runtime_pm_enable` は probe 参照を落としますが autosuspend は武装しません（`intel_runtime_suspend` 未移植、装置は D0 のまま）。
- `verify_state` は DEBUG_RUNTIME_PM 相当の well 側照合のみ。

## 次

`i915_driver_probe` の移植はここで一区切りです。残っているのは (a) DRM object model が要る部分（active crtc 時の initial_commit、connector 検出・hotplug 処理本体、fbdev）、(b) runtime suspend / resume、(c) 実機 EU 試験（要・明示解除）。どれへ進むかはご判断ください。

git commit / push はしていません。HAL インタフェースは不変です。
