<!-- awesome-plan project=zedbsd record=ws289-p002 -->

# ws289-p002 汎用の SDHCI の core の分離（rpi4-sdhci.c を glue に）

- 親: [ws289](../ws.md)。設計: [design.md](../design.md) 版 2 の §2 D1・§3.1〜§3.3・§3.7・§8・§9 の p002 の行・§11 の U6。
- Queue: Q1 の割当（P1、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p1-ws289-p002`、branch `agent/p1-ws289-p002`、基点 main 1680183ed）。Fable は使わない（ユーザー）。
- 出典の決定: 2026-10-23 ユーザー（Q1 経由）「rpi4を参照するよりも、共通部分は分離しましょう。」
- Status: cleared（2026-10-23 Q1: T1 の QEMU raspi4b の boot-test PASS（main 8b2837d2d、試験の config plan/ws289/tests/config-rpi4-sd.mk、SD の root で init が進み login:、PNG build/review/ws289-p002-raspi4b.png）、host 試験の register の書き込み 45,664 回が一致、rpi4・rpi5・amd64 の kernel の warning 0（build の warning は openssl など外部 package だけ）。merge 803d0dbfb。boot-test.sh は raspi4b の SD を 2 の冪に（2d9fe7019）。Pi 4 の実機はユーザーの試験の一覧 U39）

## 範囲と到達点

- 範囲: `src/drivers/mmc/sdhci.c`・`include/drivers/mmc/sdhci.h`（新規）、`src/drivers/platform/rpi4/rpi4-sdhci.c`・`.h`（glue に）、`tests/sdhci-host-test.c`（`tests/rpi4-sdhci-host-test.c` から `git mv`）、`platform/arm64/vmunix.mk`（rpi4・rpi5 に core を link、host 試験の規則）。Zynq の glue は p004（この Phase では書かない）。
- 到達点（design §9）: rpi4・rpi5・amd64 の build warning 0、host 試験 PASS（同じ command の列）、`nm -u` の境界、T1 の raspi4b の boot-test PASS（QEMU の証拠）。Pi 4 の実機はユーザーの試験の一覧（U6）。

## 実装

- core（Zlib）: rpi4-sdhci.c の関数の移動。file static の `unit`・`last_error_status` を `struct sdhci_unit sdhci_units[DRV_SDHCI_UNIT_MAX]` に入れ、全部の内部関数が unit を引数に取る。公開の口は `drv_sdhci_attach(config, &disk)`・`drv_sdhci_card_info(unit, &card)`（dw-mmc の形）。register の access は 32 bit だけ（今の道のまま。command は transfer mode と、block count は block size と 1 word）。時計は arm64 の `cntpct_el0`/`cntfrq_el0` の asm を `kern_rtc_read_counter` に（`start_deadline`・`deadline_passed`、読めない時は `EIO`。待ちの中の counter の読みの回数は前と同じ 1 回ずつ）。disk の名前は `mmcblk<unit>`。`card_present` は log だけ（design §3.2 L2。rpi4 は NULL）。`max_clock_hz` は 0 で 25 MHz。base clock は config → capabilities → 100 MHz の順。
- v2 の divisor（design §3.2 L1）: attach の log の `HOST_VERSION` の読み（前から有る読み）の下位 byte を覚え、v3（値 2）以上は divisor の上限 0x200、それ未満は 0x80（8 bit の 2 の冪）。
- glue `rpi4-sdhci.c`（90 行）: `DIRECT_BASE` の map、`HAL_BOARD_RPI5` の 200 MHz（Value referenced の注記ごと core から移した）、unit 0、`drv_rpi4_sdhci_init`・`drv_rpi4_sdhci_disk` は不変。rpi4.c・rpi5.c・Wi-Fi の code は触っていない。QEMU の legacy controller の fallback（init を 2 回）は、失敗した attach が unit を空けたままにするので同じ。
- 規約: 新しい core と glue は plan/coding-style.md の全文で書いた（関数を条件の中で呼ばない、`function_result` を使わない、`Checks the operation status.` の類を使わない、log の引数の中の register の読みを前の行に出す、など）。Guardrail の「source の中の開発の管理情報」に従い、移した comment から BUG-288・BUG-295・ws134-p006 の参照を外した。

### design からの差（小さい物、Q1 の確認用）

1. `struct drv_sdhci_config` に `physical_base` を足した（boot log の `sdhci: base=%p …` の行を今と同じ値で出すため。core は物理 address を知らない）。
2. v3 の divisor の上限を `< 0x400` の loop（0x400 まで行くと符号が 0 = 分周無しになる潜在の誤り）から 0x200 の上限にした。0x400 に届くのは base clock が 409.6 MHz を超える時だけで、Pi 4（100 MHz）・Pi 5（200 MHz）では到達しない（結果の divisor は同じ）。
3. host 試験の置き場所は design §3.7 のとおり `tests/sdhci-host-test.c`（Q1 の依頼文の `plan/ws289/tests/` ではなく。回帰の試験として Master の Tools に載っている物の改名のため）。試験に `-t`（全 register の access の印字）と、v2/v3 の divisor の case を足した。
4. `drv_sdhci_card` に bus_width（ACMD6 の結果 1/4）と card_clock（分周後の値）を入れる（新しい状態だけで register の access は増えない）。

## 確認（host、2026-10-23）

- `make -j16 -o build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` → rc 0、`warning:` 0、`arm64 vmunix check: PASS`（log の中に `src/drivers/mmc/sdhci.c` の compile が 1 行）。
- `make -j16 -o build/rpi5/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi5.mk build/rpi5/vmunix` → rc 0、`warning:` 0、`arm64 vmunix check: PASS`（sdhci.c の compile 1 行）。
- `make -j16 -o build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` → rc 0、`warning:` 0、`amd64 vmunix check: PASS`（amd64 は core を link しない。回帰の無い事だけ）。
- worktree の `build/llvm`・`build/NoctLang` は main の checkout の共有の物への symlink（読むだけ。toolchain は変えていない）。
- `make -o build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk sdhci-host-test` → `sdhci-host-test: PASS`（前と同じ 7 つの試験 ＋ attach の検査 ＋ v2/v3 の divisor）。
- **同じ command の列の証拠**: main の `rpi4-sdhci.c` と `tests/rpi4-sdhci-host-test.c`（印字の 2 行だけ足した写し、scratchpad）と、新しい core と試験の `-t` の出力を比べた。register の **書き込み 45664 行が byte 一致**（`cmp`）。読みを含む 63925 行は並べ替えると一致（md5 同じ）し、違いは log の引数の中の副作用の無い読み（`HOST_VERSION`・`PRESENT`・`AUTO_CMD_ERROR`）の順だけ（前は C の引数の評価の順で決まり、host の gcc は右から評価していた）。新しい試験の末尾の 16 行は v2/v3 の divisor の case の分。`-v` の driver の log は `base=` の値（試験の偽の物理 address）以外一致。
- `llvm-nm -u` の境界: core（rpi4・rpi5 とも）の未定義は `bio_complete disk_alloc disk_create disk_set_stats_kind kern_logf kern_mmio_read32 kern_mmio_write32 kern_rtc_read_counter mutex_init mutex_lock mutex_unlock` だけ（`drv_rpi4_*`・`drv_rp1_*`・`drv_zynq_*` 無し）。glue の未定義は `drv_sdhci_attach` だけ。
- `git diff --check` → clean。

## 未実施

- QEMU raspi4b の boot-test（T1 に依頼、下）。
- 実機の Pi 4 の SD の起動（ユーザーの試験の一覧へ、U6）。Pi 5 の SD（glue の 200 MHz の道）は build だけ。実機の確認は p003 の Pi 5 の実機の試験に含める（design §3.7 M5）。

## T1 への依頼

- tree: branch `agent/p1-ws289-p002` の最新の commit（Q1 が main に merge した後なら main の merge 以降）。
- image: `config/ci/config-rpi4.mk` の image（T1 の通常の Pi 4 の image の作り方、新しい BUILD の dir で `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk disk-image`。vmunix と DTB は image の横。T1-709 と同じ形）。
- 流す物: `BOOT_MODE=raspi4b plan/tools/boot-test.sh <image>`（2 GiB、TCG）。raspi4b は eMMC2 の attach が落ちて legacy controller（`0xfe300000`）の fallback で同じ unit 0 をもう一度 attach する道を通る。
- 合格: SD の root が mount され、画面に login prompt（PNG）。PNG の path と image の tree の SHA を返す。

## 残り・引き継ぎ（Q1 へ）

- Master の Tools の行（`plan/master.md` の `tests/rpi4-sdhci-host-test.c` の行）を `tests/sdhci-host-test.c`・`make … sdhci-host-test`・`sdhci-host-test: PASS` に直す事（担当は master.md を書かない）。`plan/bugs/BUG-295.md` の 31 行の試験の名前も同じ（履歴の記述なので注記で可）。
- ユーザーの試験の一覧に「Pi 4 の SD の起動（WS289 p002 の後）」（design §11 の Q1 への依頼 (5)）。
- p004 で core に足す物: CSD の復号（design §3.8）、Zynq の glue。
