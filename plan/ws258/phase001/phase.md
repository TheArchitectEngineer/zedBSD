<!-- awesome-plan project=zedbsd record=ws258-p001 -->

# ws258-p001 全ドライバの先頭の comment の出典の監査と直し

Status: in-progress（2026-10-18 q1248-i01、P1）

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
