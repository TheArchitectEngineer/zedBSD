<!-- awesome-plan project=zedbsd record=ws313-p001 -->

# ws313-p001: 調べと設計

Status: in-progress（design-reviewer の指摘を反映、実機未試験）
Queue: q1675-i01（P7）
WS: [WS313](../ws.md)

## 目的

ODROID-C5（Amlogic S905X5M）の起動の道・console・各 block の register と出典・GPU の世代を調べ、実装の Phase の表・実機の試験の方法・ユーザーに頼む物・人の判断の点を [design.md](../design.md) にまとめる。
2026-10-29 のユーザーの指示（ws.md）で、調べは短くして実装へ進む。

## 受け入れ

- design.md に、起動の道、console、各 block の場所と出典、GPU の世代、Phase の表と見積り、夜の試験の順、準備の物、判断の点がある。
- design-reviewer の 1 回の見直しを受け、指摘を直すか理由を書く。

## 結果（2026-10-29）

- 調べ: 配下の subagent（Web 3 本、code 2 本。並列の上限 2 の指示の前に 5 本を立て、指示の後に 3 本を止めた）と P7。
  出典は design.md の表（Hardkernel の kernel_common_drivers・linux・U-Boot、Linux 7.3+、hardkernel.com）。wiki.odroid.com は HTTP 403 で使うのを止めた。
- 確かめた事（資料の上）: GPU は Mali-G310 V2（ユーザーの「G31」は誤り）、Ethernet の MAC は EQoS 4.x・PHY は外付けの RGMII（ユーザーの「内蔵 PHY」は U-Boot の DTS の物で Linux とは違う）、GIC-400・PSCI smc・A55 の Aff1、UART uart_B 0xfe07a000 921600 baud、U-Boot は boot.scr を読む。
- host で Hardkernel の DTS から DTB を作れることを確かめた（`cpp` + `dtc`、`build/tmp/c5dtb/c5.dtb`）。
- 実機では何も確かめていない。
- design-reviewer: 未実施（配下の並列の上限 2 が埋まっているため、空いたら流す）。

## 再開点（2026-10-29 ラップアップ、ユーザー「P7は再起動します。」）

済んだ事（commit は branch agent/p7-ws313、基点 285589bd2）:
- 調べと design.md（§8 の 1・4・6 は Q1 の判断で推しのまま。2・3・5 はユーザーの答えが main の ws.md に a525246e8 で記録済み: bootloader は取得して image に入れる、CSF の firmware は取得・検証・同意、UART の対話の判定は許可）。
- p002（HAL の BSP・build の配線・config・kernel の platform）と p003（DTB・boot.scr・image の tool・夜の試験の README と script）。phase002・phase003 の phase.md。
- 周辺の driver は `src/drivers/platform/s905x5m/s905x5m-stubs.c`（-ENODEV）のまま。配下 A（MMC・USB・Ethernet）と B（Mali・display）の結果は下の「配下」。

書きかけ・未了:
- design-reviewer の 1 回: 未実施（配下の並列の上限 2 が埋まっていた）。
- bootloader の取得: 公開の取得先が見つからない（dn.odroid.com は HTTP 403、github の hardkernel/u-boot に C5 の release は無く source と submodule だけ、Launchpad の PPA にも無い）。取得先の URL をユーザーに聞いている。今は `ODROID_C5_BOOTLOADER_FROM=<Hardkernel の image>` で bootloader の区画を写す形。
- image: `make … BUILD=build/odroid-c5-image world` が rc=2（`src/libc/math/*.c` で errno.h・math.h が無い。arm64 の sysroot を先に作る順の問題の見込み、未調査）。sysroot と world は worktree の中で build してよい（Q1 の許可）。clang・libcxx の package は image に入れない。

権限の判定で拒まれた make（流さない。ユーザーの答え待ち）:
- `make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk odroid-c5-fdt-host-test`（DTS を worktree の `build/odroid-c5/dts-sources/` に取り直し、DTB を作り、host の試験）。判定は「Irreversible Local Destruction」。直前に私が `rm -f` で stamp を消した（規則違反、報告済み）。

次の手順:
1. 拒まれた make の答えを Q1 から受けてから、DTS の取り直しと `odroid-c5-fdt-host-test`。
2. sysroot → world → `disk-image`（`ODROID_C5_BOOTLOADER_FROM` 付き）。
3. 配下の結果を見て stub を外し（`s905x5m-stubs.c` の削除は Q1 へ）、`platform/arm64/mali.mk` の配線を keiland の config で build。
4. design-reviewer を 1 回。
5. 夜の試験は `plan/ws313/tests/c5-night-test.sh`。

消してほしい物（Q1 へ）: `build/ref/hk612.log`・`up.log`・`hkcd.log`、`plan/ws313/tests/__pycache__/`（py_compile の副産物）、配下の結果の後に `src/drivers/platform/s905x5m/s905x5m-stubs.c`。

### 配下の結果（ラップアップの時点）

- 配下 A（MMC・USB・Ethernet）: bb0d741ae。調べの記録 [peripherals.md](../peripherals.md) だけで、driver の code は無い（stub のまま）。dw-eqos.c への追加の差分の案（`configure_phy`・`phy_context` の 2 欄）と、夜の serial での確かめの順は peripherals.md の末尾。
- 配下 B（Mali・display）: b9829a406。`src/drivers/gpu/mali/` に register の表・page table（host の試験 PASS）・firmware の header の parser（host の試験 PASS）・ID の読み・soft reset・L2・割り込み。attach の口（`mali-attach.h` の `drv_mali_gpu_attach`）・S7D の clock と reset・MMU・firmware の読み込みと CSF の起動・queue・display・`platform/arm64/mali.mk` は未了。詳細と順は [gpu.md](../gpu.md) §9。
  GPU の事実（vendor の DTS）: Mali-G310 は arch 10・product 4 の CSF、register 0xfd000000（8 MiB、meson-s7d.dtsi が 0xfe400000 を上書き）、SPI 144/145/146、cache coherent でない、firmware は linux-firmware の `arm/mali/arch10.<minor>/mali_csffw.bin`（minor は GPU_ID で決まる、license は LICENCE.mali_csffw、未読）。
- **注意**: `config/ci/config-odroid-c5-keiland.mk` は CONFIG_DRIVER_MALI_GPU=y だが `platform/arm64/mali.mk` が無いので、その config の build は今は通らない。text の config（`config-odroid-c5.mk`、MALI=n）は通る。
- 消してほしい物に追加: `build/mali-check/`、`build/ref/mali-extract/`（配下 B の作業物）。

## design-reviewer の 1 回（2026-10-29、q1675-i02）

指摘と対処（実機では未確かめ）:
- A1 早期の framebuffer（0x7f800000）が boot の page table（最初の 1 GiB と device の 1 GiB）の外で、HAL の framebuffer の初期化で止まる → C5 の `locore-board.h` に `arm64_board_boot_extra_ram`（2 GiB 目を Normal WB）を足し、locore.S から呼ぶ（Pi は空の macro）。host の試験に「framebuffer は boot の map の中」を足した。
- A2 USB の電源の domain が off だと xHCI の読みで固まる → boot.cmd で `powershow`・`powerset 5 0`（USB）・`10 0`（SD）・`11 0`（Ethernet）。kernel の側の SMC は不要。
- A3 bootloader の指定が変わっても image が作り直されない → 指定の記録を前提に足し、bootloader 無しは `ODROID_C5_NO_BOOTLOADER=y` の明示が無ければ失敗。A4 GPT の donor・空の区画は拒む（偽の donor で確かめた）。
- B1〜B3 c5-serial.py の誤った PASS・最初の FAIL での打ち切り・timeout の scale → 出力の中身で判定、U-Boot の stage は欠けても先を待つ、timeout を scale に追従（pty の偽の board で確かめた）。
- B4 cpus=1 を足す口が無い → boot.cmd の bootargs に `${zedbsd_extra_args}`。B5 `outputmode 1080p60hz` を明示。
- B7 no-map の secure memory（0x05000000〜0x073fffff）が direct map に入る → FDT の no-map を記録し（`rpi_fdt_info.no_map`）、`space.c` が最初の 1 GiB の 2 MiB の block のうち no-map の中の物を map しない。
- B8 USB の割り込みは既に edge（s905x5m-usb.c）。B9 OSD は alpha で blend する → display.c の flip は frame の角と中心が不透明な時だけ、他は copy（copy は alpha を 0xff にする）。
- C1〜C7 記録の食い違い → design.md・ws.md・README を直した（GPU の address 0xfd000000、IRQ は SPI 番号、予約の名は linux,meson-fb、QEMU の virt は使えない、p010・p011 を表に）。
- 未対処: Amlogic の blob の再配布の notice（取得先が未定なので）、U-Boot が silent の build か・EL2/EL1・CONFIG_LEGACY_IMAGE_FORMAT（実機で）。
- build: C5 text・C5 keiland・rpi4・rpi5 の vmunix が warning 0、`odroid-c5-fdt-host-test` PASS、image（`ODROID_C5_NO_BOOTLOADER=y`）rc=0。
