<!-- awesome-plan project=zedbsd record=ws258-p001 -->

# ws258-p001 全ドライバの先頭の comment の出典の監査と直し

Status: cleared（2026-10-18 Q1 判定: 1,031 file を点検、44 file を直した（merge 181f43b52）、出典の照合と直し（6ff8706b3、merge b8a176065、Linux・Mesa は全部一致、手元に無い出典は未確認と記録）、build 3 組 warning 0 は P1 の報告。直さずに残した i915/intel の抜粋・GT の core は WS244 へ、rtl8822b-tables.inc は WS259 へ移した（2026-10-18 ユーザーの決定））（旧: in-progress（2026-10-18 q1248-i01、P1））

- 親: [WS258](../ws.md)。Queue: q1248-i01。
- 範囲: `src/drivers/` と `include/drivers/` の .c・.h・.S・.inc（約 1,030 file）。comment だけを変え、動作は変えない。
- 手順: 各 file の先頭の comment（最初の #include より前）を読み、Linux・FreeBSD・Mesa・nouveau・amdgpu・vendor の資料などの出典・参考・派生の記述があれば外し、その内容が要る出典なら該当の関数・表の block に `Values referenced:` か `Procedure referenced:`（版・path・関数か表の名前）で書く。file の役割の説明は残す。code・comment・構造を外部から写していないかも見る（写していれば Q1 に報告）。
- Q1 の予備の grep（先頭の comment に Linux などの語がある file）: src/drivers/gpu/i915 に 66 file ほか。語だけの一致なので、file ごとに読んで決める。

## 実行の記録（2026-10-18 q1248-i01、P1）

### 点検の方法

`src/drivers/` と `include/drivers/` の .c・.h・.S・.inc 1,031 file について、先頭の comment（最初の `#include` より前、license の block を除く。`#include` の無い header は file の頭の最初の comment）を script で抜き出し、出典・参考・派生の語（Linux・FreeBSD・OpenBSD・Mesa・anv・blorp・nouveau・amdgpu・the reference・follows・derived・ported・transcribed・MIT・GPL・PRM・Technical Reference Manual・specification・SHA-256 など）を含む 165 file を file ごとに読んで決めた。残りの 866 file は語の一致が無く、直す要が無い。header guard の後の block ごとの `Values referenced:` は規則どおりなので残した（amdrdna-regs.h・gfx10-isa.h・display-dpcd-xd.h・xd-vbt-layout.h ほか）。

### 直した file（44、comment だけ）

| driver | 数 | file | commit |
| --- | --- | --- | --- |
| usb・pci・typec・i2c・gpio・pc98 | 9 | usb.c、usb.h、pci.h、pci-pcat-assign.c、pci-window.c、ucsi.h、lpss-i2c.c、intel-gpio.c、pc98-ide.c | cd5eccda3 |
| rpi4 | 7 | rpi4-board.c、rpi4-mmal.c、rpi4-vchiq-wire.h、rpi4-vchiq.c、rpi4-h264.h（cd5eccda3）、rpi4-bt-link.c、rpi4-bt.c（f7235146b） | cd5eccda3・f7235146b |
| amdrdna・nvrtx・bcm2711 | 6 | bytecode.c、products.c、cl.c、native-bin.c、native-shader.c、qpu.c | ceac7454d |
| i915 display・compiler・render | 16 | gen9-link.c、gen9-backlight.c、gen9-mode.c、gen9-modeset.c、gen9-outputs.c、interrupts.c、present.c、scanout.c、compile-compute.inc、compile.c、eu.c、eu.h、compute.c、state.h、transport.h、blit.h | e91b944a4 |
| i915 tests | 6 | gen9-fake-hw.h、host-gen9-modeset-test.c、eu-test.c、fhd-render.c、draw-fixture.c、draw-fixture.h | e5466651d |

直し方: 先頭からは出典の名前・関数名・版を外して file の役割だけにし、出典は該当の関数・表・define の block の comment に `Procedure referenced: <版> <path> (<関数>)` か `Values referenced: …` の複数行の形で移した。同じ file を直すついでに、先頭の開発の管理情報（ws・BUG・T1 の番号、plan/ の path）も外した（guardrail の「source の中の開発の管理情報」）。

### 直さずに Q1 に報告した file

1. **upstream の著作権表示のある抜粋（22 file）**: `src/drivers/gpu/i915/intel/` の commands.h、gt-regs.h、gt-power.h、lrc-offsets.h、mocs.h、mocs-table*.inc、mcr-ranges.inc、pci-ids.h、workarounds.h、engine-table*.inc、forcewake-ranges*.inc、genxml*.h、eu-encoding-gen*.h、display-regs-gen9*.h、null-state-gen9.inc と、`src/drivers/wifi/rtl8822b/rtl8822b-tables.inc`（Intel・Realtek などの Copyright と MIT の表示、「Modified for zedBSD: excerpted …」と SHA-256 の出典の一覧が先頭にある）。Linux の macro の名前ごと写した抜粋で、表示を外すと license の条件に触れるおそれがあり、規則（file の上部に出典を書かない・symbol を写さない）と食い違う。扱いはユーザーの判断が要る。bits.h はこの抜粋の補助で、同じ判断に従う。
2. **Linux の構造・手順を写した疑い（i915 の GT の core）**: context.c・context.h（Linux の lrc_* の関数を「stands for」と書き、名前も対応）、power.c（「a direct port of Linux's intel_pcode.c」）、reset.c・reset.h（「The control flow is Linux's」）、tlb.c・tlb.h（「This is Linux intel_gt_invalidate_tlb_full()」）、request.c・request.h（「re-derived from the reference」）、defaults.h、device-info.c・device-info.h、dma.h、engine.h、ggtt.c、gt-power.h、irq.c・irq.h、memory.h、migrate.h、park.h、ppgtt.h、pxp.h、runtime-pm.c・runtime-pm.h、submit.h、sync.h、verify-workarounds.h、workarounds.c・workarounds.h、worker.c、workqueue.h、tests/contracts/pci_contract_test.c。指示どおり直していない。

### 判断して残した物（Q1 の確認用）

- 公開の規格の節の名指し（ACPI 6.5 の節: aml-define.c・aml-field.c・aml-operator.c・aml-sync.c・aml-table.c）は、実装する範囲を示す役割の説明として残した。UCSI（ucsi.h）・H.264（rpi4-h264.h）・Bluetooth・PL011 の TRM（rpi4-bt*.c）は「値はここから」という宣言だったので block へ移した。
- i915 の display の host 試験（host-gen9-mode-test.c、host-xd-*-test.c、xd-regs-map.h、dp-fixture-latitude5330.h など）の「Linux の計算・dump と比べる」は試験の役割（照合の相手）なので残した。
- 誤検出（語だけの一致）: ufs.c の「4BSD-derived」（file system の系譜）、bcm43455-firmware.h の「upstream names」（firmware の file の名前）、xd-clock.h の「the reference」（基準の clock）、vbt-parse.h・xd-vbt.c の SHA-256（hash の計算）、pc98・pcat の editor の mode 行、mode-plan.c の「reference model」（host の試験の model）ほか。

### build（comment だけの変更）

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws258 build/ws258/vmunix`: rc=0、warning 0、`amd64 vmunix check: PASS`（cd5eccda3 の時点と e5466651d の時点の 2 回）。
- `… BUILD=build/ws258-i915t I915_TESTS=y build/ws258-i915t/vmunix`: rc=0、warning 0、PASS（e5466651d）。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws258-rpi4 build/ws258-rpi4/vmunix`: rc=0、warning 0、`arm64 vmunix check: PASS`（ceac7454d と f7235146b の 2 回）。
- `git diff --check` は空。QEMU・実機は動作を変えないので行わない。

### 残り

- 上の報告 1・2 の扱い（ユーザーの判断）。その判断で i915 の GT の core を直すなら別の Phase。

## 出典の照合（2026-10-18 q1251-i01、P1）

ユーザー:「照合させる」。q1248 で新しく書いた `Procedure referenced:`・`Values referenced:`（44 file、`git diff 9d995be42 411c13214 -- src include`）の path・関数名・表の名前を、Linux 6.8.12（build/ws229-p001/linux/linux-6.8.12.tar.xz）と Mesa 25.0.7（build/mesa-tools/mesa-25.0.7.tar.xz）の実物（必要な directory だけを build/tmp/ws258-verify-1792321227/ に展開）に grep で照らした。

### 直した物（comment だけ）

- gen9-outputs.c: `ibx_digital_port_connected` は intel_dp.c に無く g4x_dp.c にある → `g4x_dp.c (ibx_digital_port_connected)` に直した。
- compile.c: `brw_spill_reg` は Mesa 25.0.7 に無い → `brw_reg_alloc::spill_reg`（brw_reg_allocate.cpp）に直した。
- compile-compute.inc: directory だけの `src/intel/compiler` を file に直した（emit_work_group_id_setup・emit_barrier は brw_fs_nir.cpp、generate_barrier は brw_generator.cpp。r0 の dword 1・6・7 は emit_work_group_id_setup で一致）。
- 版の無かった Linux・Mesa の参照に版を足した: intel-gpio.c（2 箇所）・lpss-i2c.c に `v6.8.12`、cl.c・native-bin.c・native-shader.c・qpu.c・eu.c・compile-compute.inc・compile.c・compute.c・blit.h・state.h・draw-fixture.c に `25.0.7`。80 桁を超えた行は折り返した。

### file ごとの結果

| 結果 | file |
| --- | --- |
| 一致（Linux 6.8.12） | gen9-backlight.c（lpt/cnp_setup・lpt/bxt_get・lpt/bxt_set_backlight、soc/intel_pch.c intel_virt_detect_pch）、gen9-link.c（hsw_prepare_dp_ddi_buffers、hsw_set_signal_levels、skl_ddi_set_iboost、intel_ddi_prepare_link_retrain、intel_ddi_set_idle_link_train、intel_dp_prepare/stop_link_train、clock_recovery、channel_equalization）、gen9-mode.c（intel_ddi_buf_trans.c の skl/kbl の DP・eDP・HDMI 表と intel_ddi_buf_trans_init、skl_get_cdclk、intel_dp_compute_link_config_wide、intel_link_compute_m_n、skl_ddi_dp_set_dpll_hw_state、skl_ddi_calculate_wrpll、skl_watermark.c の 4 関数）、gen9-modeset.c（hsw_power_well_enable、skl_ddi_pll_enable/disable、hsw_crtc_enable ほか 5 関数、intel_ddi.c の 4 関数と Display WA #1143、intel_ddi_post_disable、skl_write_plane_wm、skl_plane_ctl、spt_hpd_detection_setup）、interrupts.c（intel_display_irq.c の gen11_display_irq_reset・gen11_de_irq_postinstall・gen11_display_irq_handler）、scanout.c（intel_fb.c の 3 関数、skl_plane_stride、i915_gem_domain.c の VTD_GUARD と pin_to_display_plane、i915_gem_create.c の dumb_create の 64 byte）、intel-gpio.c（TGL_LP_HOSTSW_OWN 0x0b0・GPI_IS 0x100・GPI_IE 0x120、intel_gpio_irq_init）、lpss-i2c.c（ADL の 0x51e8 ほかが bxt_i2c_info、TGL-LP の 0xa0e8 ほかが spt_i2c_info）、present.c（出典の参照を外しただけ） |
| 直して一致 | gen9-outputs.c、compile.c、compile-compute.inc |
| 一致（Mesa 25.0.7、版を足した） | cl.c・native-bin.c・native-shader.c（broadcom/cle/v3d_packet.xml、min_ver 42）、qpu.c（broadcom/qpu/qpu_pack.c）、eu.c（brw_lower_scoreboard.cpp）、compute.c（genX_cmd_compute.c の compute_load_indirect_params、genX_pipeline.c）、state.h（genxml/gen120.xml、src/intel/vulkan）、blit.h・draw-fixture.c（src/intel/blorp、RECTLIST は blorp_genX_exec_brw.h）、bytecode.c（参照は Mesa でなく自前の説明のみ） |
| 未確認（出典が手元に無い） | transport.h（venus-protocol の commit ca19b635…）、products.c（pci.ids 2026-10-15）、pc98-ide.c（qemu-pc98・linux-pc98。Linux 6.8.12 に pata_pc9800.c は無い）、rpi4-board.c（QEMU hw/arm/raspi4b.c）、rpi4-bt-link.c・rpi4-bt.c（Bluetooth Core 5.4、PL011 TRM、Raspberry Pi の mailbox）、rpi4-h264.h（ITU-T H.264）、rpi4-mmal.c・rpi4-vchiq.c・rpi4-vchiq-wire.h（Raspberry Pi userland・FreeBSD sys/contrib/vchiq）、ucsi.h（UCSI 1.2・3.1） |
| 未確認（特定の file・関数を名指さない一般の参照） | pci.h（Linux・FreeBSD・NetBSD・OpenBSD の driver API の形）、usb.h（Linux の USB の driver model）、eu-test.c（同じ GPU の Linux i915 の観察） |
| 参照の追加が無い（comment の整理だけ） | usb.c、pci-pcat-assign.c、pci-window.c、gen9-fake-hw.h、host-gen9-modeset-test.c、fhd-render.c、draw-fixture.h、eu.h、nvrtx 以外の残り |

### build（照合の直しの後）

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws258 build/ws258/vmunix`: rc=0、warning 0、PASS。
- 同 `BUILD=build/ws258-i915t I915_TESTS=y`: rc=0、warning 0、PASS。
- `config-rpi4.mk BUILD=build/ws258-rpi4`: rc=0、warning 0、`arm64 vmunix check: PASS`。
- `git diff --check` 空、追加行に 80 桁超え無し。
- 消してよい展開物（Q1 へ）: build/tmp/ws258-verify-1792321227/。
