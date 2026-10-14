<!-- awesome-plan project=zedbsd record=ws037-p003 -->

# ws037-p003: 定数の一括の改名、driver の骨格、段の印（N0: GPU を見つけて ID を読む）

Status: in-progress（2026-10-14 P1。改名・骨格・N0・N1 を実装、build と host 試験まで。実機・QEMU は T1 待ち）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: Q1 の割当（2026-10-14、ユーザー「P1: RTX」）
依存: p001（作業の文書と監査の表）、p002（試験の道）
実行者: phase-runner（high）

## 範囲

1. **定数の一括の改名**（code を書く前、ws.md の「ライセンスの扱い」2）: `plan/ws037/temp/` の作業の文書の定数（register・bit・field・class・method の名前）を全て独自の名前（例 `NVRTX_…`）に一括で変え、対応表を temp に置く（commit しない）。register の定義は監査の表の MIT の出典から取る。
2. **骨格**: `src/drivers/gpu/nvrtx/`（`nvrtx.c`・`device.c`・`mmio.c`・`stage.c` など）。PCI の attach（vendor 0x10de、Turing 以降の device の表は p001 の表から）、BAR0（MMIO）・BAR1・BAR2/3 の map、`CONFIG_DRIVER_PCI_NVRTX`（menuconfig、既定 n、CI の config には入れない）。i915 の `src/drivers/gpu/i915/` の attach の形を手本。
3. **段の印**: `stage.c` が GOP の framebuffer に 1 行ずつ印を書く（例 `nvrtx: N0 found 10de:1f02 at 01:00.0`、`nvrtx: N0 boot0=… chipset=TU106`）。印は kernel の log にも出す。HAL の API の変更は要らない見込み（framebuffer は今の console と同じ口）。要るなら差分を plan に置いて承認を得る。
4. **N0 の確かめ**: chip の ID の register（boot0）を読み、chipset が TU106 であること、BAR の大きさを印に出す。GPU を初期化しない（読むだけ）。

## 受け入れ

- build（amd64）exit 0・自前の warning 0。`CONFIG_DRIVER_PCI_NVRTX=n` の build が変わらない。
- 実機（p002 の方式）で段の印 N0 が GOP の画面に出て、chipset が TU106（写真か capture）。他の機器（i915 の 5330・QEMU）の boot-test が PASS（nvrtx を有効にした image でも、NVIDIA の GPU が無ければ何もしない）。
- `git grep` で GPL の source の名前・comment が code に無い（改名の対応表と照らす）。

## 結果（2026-10-14、P1）

- 改名（temp、commit しない）: `plan/ws037/temp/rename/rename.py` が 3 つの作業の文書の定数 343 を独自の名前（`NVRTX_` と block の語の置き換え: RM の ctrl → `DPYQ_`・`SUBQ_`・`DEVQ_`・`CHQ_`・`VASQ_`、RPC → `RPCF_`・`RPCE_`、register → `REG_…`、class → `CLS_…`、語の辞書 約 200）へ変え、`temp/rename/renamed/` と対応表 `temp/rename/rename-map.tsv` を作った。Linux・DRM・nouveau の software の API と Vulkan の拡張の名前 29 は hardware の定数でないので残し、表に `software-api` と記した。道具は旧名が改名の版に残らないこと・新名が重ならないことを assert で確かめる。zedBSD の code の定数は改名の版から取った（`NVRTX_REG_MC_IDENT_0` など）。
- 骨格（commit 済み）: `src/drivers/gpu/nvrtx/`（`nvrtx.c` 登録と attach、`discover.c` N0・N1、`identify.c` chip の判定（hardware に触れない）、`stage.c` 段の印と boot の parameter、`nvrtx-private.h`）、`include/drivers/pci/pci-nvrtx.h`。`CONFIG_DRIVER_PCI_NVRTX`（Makefile の既定 n と `-D`、`platform/amd64/vmunix.mk`、`src/kern/platform/pcat.c` の登録、`config/drivers/pci.drivers` と `menu.list` の GPU）。
- 振る舞い: vendor 0x10de の display class の function を attach に渡す。N0 は memory の BAR を順に記録し（register・VRAM・instance の窓）、register の窓の先頭 1 page だけを読み取り専用・uncached で map して identity の 2 つの register を読み、chip を判定（0x162・0x164・0x166 = TU102・TU104・TU106 は GSP の道の対象、TU116・117・Ampere・Ada・Blackwell は名前だけ）。PCIe の FLR の bit も記録。N1 は loader の GOP の framebuffer が VRAM の窓の中か（offset）を記録。**GPU には書かない**。最後に全部を返して attach は ENODEV（bind しない）なので、firmware の console は続き、PCI の suspend の対象にもならない。boot の parameter: `nvrtx.off=1`・`nvrtx.stop=N0|N1`・`nvrtx.gsp=1`（p004 まで「P0 は無い」と印を出すだけ）。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws037-k-y CONFIG_DRIVER_PCI_NVRTX=y vmunix` rc 0・warning 0・`amd64 vmunix check: PASS`（nvrtx の symbol あり）。既定（n）の `BUILD=build/ws037-k-n` も rc 0・warning 0・PASS、nvrtx の symbol 0。既定 n で `pcat.c` の前処理の結果が基の commit `093aa786a` の物と byte で一致（他の source は `-DCONFIG_DRIVER_PCI_NVRTX=0` が足されるだけで参照なし）。
- host の試験: `plan/ws037/tests/nvrtx-identify-host-test.sh` PASS（14 の identity: TU10x・TU116・GA104・AD102・GB202・未知・旧世代・全 1・big-endian・VF）。`git diff --check` 0。
- 未実施: 実機（方式 B の VFIO の guest と方式 A）での N0・N1 の印、他の機器の boot-test（nvrtx 有効でも NVIDIA が無ければ何もしない）。T1 に依頼する（p004 の手順と一緒に）。
