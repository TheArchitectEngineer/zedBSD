<!-- awesome-plan project=zedbsd record=ws036 -->

# WS036: amd64の成果を他のplatformへ反映し、buildを通す

<!-- awesome-plan-current:start -->
Status: completed（2026-09-27）
Primary Milestone: MG008
Related Milestones: MG003, MG001
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（完了）。実機の確認はユーザー。pcat の現在の起動の失敗は BUG-066（他の WS の回帰、WS073）
<!-- awesome-plan-current:end -->

## 目標

GPU driver の導入などで amd64 を中心に大きく変わった kernel の成果を、i386 PC/AT（pcat）、PC-98、arm64（Raspberry Pi 4）、
sun4u、X68000 へ反映し、各 platform の壊れた build を通す。QEMU で起動できる platform は login まで確かめる。

2026-09-23 ユーザー指示「ビルドに失敗しているプラットフォームについては、…amd64で開発した成果をi386, pc/at, pc98, arm64/rpi4, sun4u, x68kにも
反映し、壊れたビルドを通す、というwsを作ってphaseを作成してください。」

- 2026-09-23 ユーザー決定: sun4u（sparcv9）と x68k（m68k）はコードを残してサポート外（その platform だけの Phase と toolchain の Phase は canceled）。
- 2026-09-23 ユーザー決定: i386（pcat・pc98）は移植デモ用（基本の command と Xzed が動けばよい）。
- 2026-09-24 ユーザー指示: rpi4 を build し QEMU で login prompt まで。aarch64 は主対象（同日の platform の決定）。

## 結果

**amd64・pcat・pc98・rpi4 の kernel と userland が warning 0 で build でき、QEMU で login まで起動する**（2026-09-27 の ws036-p021 で確認。
その後 main に入った他の WS の変更で pcat が起動しなくなった: BUG-066、WS073 が扱う）。

- **pcat・pc98**（2026-09-23）: kcrt への追従、kernel の source 一覧、`hal_mmio_read8/write8`（承認済み）、rtld の未使用関数、image の検査の option、
  i386 の VGA text の画面を読む `boot-test.sh`（`BOOT_MODE=bios-ide`）。pc98 は PC-98 fork の QEMU で login（`plan/tools/pc98-boot.py`）。
- **rpi4**（p012、2026-09-24）: uapi/libc の include の分離と kcrt、audio・SD の lock・serial の console driver（`rpi4-console.c`）、
  sched の起動時の競合（全 platform）、SD を FAT の boot＋UFS の root に。QEMU raspi4b で login、`dyntest`。
- **toolchain**（p026・p029、2026-09-27）: LLVM の zedbsd target に AArch64（`__ZEDBSD__`、driver の link の emulation `aarch64elf`、4 KiB の page）、
  patch level zedbsd7。aarch64 の sysroot（`make sysroot-arm64`）。rpi4 の build は sysroot と clang の driver で（`KERN_UAPI_NATIVE`・
  `KERN_KCRT_NATIVE`・tree の libc の header の直接の読み込みを外した）。開発 file（`/usr/include`・`/usr/lib`）を rpi4 の root に。
  libcxx・clang の package は検査済みの LLVM の source を hard link で写して patch する（toolchain の tree を書き換えない）。
- **Noct**（p028）: rpi4 でも build できる（aarch64 の JIT 付き。既定では選ばない）。zedinst は SD の配置を知らないので rpi4 には入れない。
- **起動 parameter**（p027）: rpi4 の HAL が DTB の `/chosen/bootargs` を `boot.command-line` で渡す。kernel の parser は全 platform で
  `=` の無い token と未知の名前を数えて無視し、空白・tab・改行で区切る（2026-09-27 ユーザーの判断: 案 A）。root を名指さない行（firmware の
  Linux 向けの行）では従来の legacy autoroot、`rootpart=` があれば native root。
- **最終確認**（p021）: 4 platform の build と起動。i386 の build を止めていた `sched.c` の 64-bit の atomic を直し、`rpi4-console.c` を全文の規約に。
- 途中で直した不具合: rpi4 の `rtld.c` の macro の再定義（`src/rtld/elf.h`）、arm64 の未定義 symbol の検査の awk の引用（検査が常に通っていた）。

## 制限と移管

- 実機（amd64 の機械、PC/AT、PC-98、Raspberry Pi 4）での確認は未実施。ユーザーが行う。
- **GitHub Release `rev-0` の LLVM の cache は zedbsd6 のまま**。zedbsd7 の archive の upload と `ZEDBSD_LLVM_CACHE_SHA256` の更新はユーザーの操作
  （それまで `make toolchain-cache` と CI は identity の不一致で止まる）。
- pcat の起動の回帰は BUG-066（WS073）。
- aarch64 の外部 package（libcxx は WS044 p011 で build、clang＋lldb は WS044 p003）。
- sun4u・x68k はサポート外（コードは残す）。

## Phase 一覧

| Phase | 内容 | 結果 |
| --- | --- | --- |
| p001 | 調査と設計（rpi4 分は p012 で） | cleared（q366-i01） |
| p002 | rtld の未使用関数 | cleared（Queue 外、2026-09-23） |
| p003・p004・p013〜p020・p022・p023 | sun4u・x68k と GCC・toolchain | canceled（2026-09-23 ユーザー決定: サポート外） |
| p005〜p009 | pcat・pc98 の build と起動 | cleared（Queue 外、2026-09-23） |
| p010・p011・p024・p025 | rpi4 の kernel・userland・kcrt・include の分離 | cleared（q366-i01、p012 にまとめた） |
| p012 | rpi4 の build と QEMU での login | cleared（q366-i01） |
| p021 | 全 platform の回帰と規約 | cleared（2026-09-27） |
| p026 | LLVM の AArch64 zedbsd target、aarch64 の sysroot、rpi4 を sysroot と driver に | cleared（2026-09-27） |
| p027 | rpi4 の起動 parameter、parser を緩める | cleared（2026-09-27） |
| p028 | Noct を rpi4 に | cleared（2026-09-27） |
| p029 | LLVM の package が toolchain の source を書き換えない | cleared（2026-09-27） |

Phase の記録は WS の完了で削除した（git の履歴に残る）。残した道具（Master の Tools 節に登録する）:
`plan/tools/guest/rpi4-serial.sh`・`plan/tools/guest/amd64-serial.sh`（QEMU の guest にシリアルで login して command を実行。rpi4 は `APPEND` で
`/chosen/bootargs`）、`plan/tools/rpi4/bootargs-rpi4.sh`（rpi4 の起動 parameter の試験）、`plan/tools/rpi4/noct-rpi4.sh`（rpi4 の Noct の JIT と API）。
