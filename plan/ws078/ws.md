<!-- awesome-plan project=zedbsd record=ws078 -->

# WS078: Kei Operating System への名前の移行

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（名前の棚卸しと対応表）から。ユーザーの判断待ちの点（下の「判断が要る点」）を先に確かめる
<!-- awesome-plan-current:end -->

## 目標（2026-09-28 ユーザーの決定、要旨）

- プロジェクトの名前は **Kei Operating System**（Kei は日本語の「軽い」）。カーネルの内部名は **zedbsd**。
- デスクトップの名前は **Keiland**（Kei + Wayland）。カーネルからデスクトップまで OS として垂直統合しているので、「デスクトップ環境」
  「デスクトップ」とはあえて呼ばない。Keiland は内部名。
- OS の見えるところから zedBSD・zed・z の名前を**徐々に**外す。
- 実行ファイルの改名: zdesktop → `/bin/wayland`、zdesktop-x11server → `/bin/xserver`、zdesktop-browser → `/bin/browser`。
- シンボル名に zedbsd を含めない（これまでの方針）。カーネル・ドライバ・UAPI などに新規の実装で混入したものを一斉に改める。
  望ましい接頭辞は `ZEDBSD_` ではなく **`KERN_`**。
- ロゴなどは K の 1 文字にしない（KDE の商標の侵害のおそれ）。必ず **Kei** の 3 文字にする。

原文は [master.md](../master.md) の決定の行。

## 棚卸し（2026-09-28、main の概算。p001 で詳しくする）

- `zedbsd` を含む識別子は kernel・driver・HAL にはほとんど無い（src/kern の `__ZEDBSD_SIGEV_THREAD_SIGNAL` 等）。
  ヒットの大半は注釈・文字列の "zedBSD"（src/drivers 790 file 等）。
- UAPI・libc: `__ZEDBSD__`（toolchain の target が定義する OS の識別子）、`__ZEDBSD_LEGACY_VISIBLE`・`__ZEDBSD_POSIX_2024_VISIBLE`・
  `__zedbsd_float_bits`・`__zedbsd_atomic_*` 等。
- bootloader: `zbl_uefi_zedbsd_config*`・`ZBL_ZEDBSD_CONFIG_*`。
- make: `ZEDBSD_` の変数が約 1,400（`ZEDBSD_CONFIG`・rootfs の option の `ZEDBSD_GRAPHICAL_BOOT` 等）。
- userland: 1,118 file に 3,456（zdesktop 等の名前、Wayland の protocol の `zed_titlebar_v1`・`zed_glass_v1`・`zed_gpu_buffer_v1` 等）。
  `zwp_`・`zxdg_` は upstream の Wayland の接頭辞であり、改めない。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws078-p001 | 棚卸しと対応表（識別子・path・見える文字列・protocol 名を分類し、新しい名前を決める。判断が要る点を列挙） | planning | なし |
| ws078-p002 | kernel・driver・UAPI・libc・bootloader の識別子の改名（`KERN_` 等。機械的。build の warning 0 と boot test） | planning | p001 |
| ws078-p003 | 実行ファイルの改名（`/bin/wayland`・`/bin/xserver`・`/bin/browser`）と参照（session.sh・zsessiond・App Home・試験） | planning | p001 |
| ws078-p004 | 見える文字列: boot の logo（Kei）・greeter・lock・banner・os-release 等 | planning | p001 |
| ws078-p005 | 全文規約確認と回帰（必須の最終確認） | planning | 全 Phase |

改名は作業中の agent と衝突しやすい。p002・p003 は他の agent が merge を終えた静かな時点で main か 1 つの agent が一度に行い、
その後に各 agent へ新しい名前を知らせる。

## 判断が要る点（ユーザーへ）

1. make の `ZEDBSD_` 変数（`ZEDBSD_CONFIG`、rootfs の option `ZEDBSD_GRAPHICAL_BOOT` 等）も改めるか。
2. `__ZEDBSD__`（compiler が定義する OS の識別子、toolchain の target の名前 `zedbsd`）はカーネルの内部名として残すか。
3. source の directory（`userland/base/zdesktop` 等）も改名するか、実行ファイルの名前だけか。
4. `zed_*` の Wayland の protocol と `libzdesktop` 等の名前。
