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

## 対応表（2026-09-28 ユーザーの回答で決定。p001 で参照の全体を確かめる）

| 今 | 新しい source | 実行ファイル・library |
| --- | --- | --- |
| userland/base/zdesktop | userland/desktop/wayland | /bin/wayland |
| userland/base/zdesktop-x11server | userland/desktop/xserver | /bin/xserver |
| userland/base/zdesktop-browser | userland/desktop/browser | /bin/browser |
| userland/base/zdesktop-terminal | userland/desktop/terminal | /bin/terminal |
| userland/base/zdesktop-files | userland/desktop/files | /bin/files |
| userland/base/zsessiond | userland/desktop/sessiond | /bin/sessiond |
| userland/base/libzdesktop | userland/desktop/libkeiland | libkeiland.so |
| libvulkan・libegl・libglesv2・libwayland・libwayland-egl・libtruetype・mview・egltest・wltest・wlshm・vkdemo | userland/desktop/<同じ名前>（2026-09-28 ユーザー「desktop へ移す」） | 名前は今のまま |

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

## ユーザーの判断（2026-09-28）

1. make の `ZEDBSD_` 変数（`ZEDBSD_CONFIG`・rootfs の option 等）: **今は残す**。
2. `__ZEDBSD__`（toolchain の target `zedbsd` が定義する OS の識別子）: **残す**（カーネルの内部名）。他の `__ZEDBSD_*`・`__zedbsd_*` の補助の識別子は改める。
3. source の directory: **実行ファイルと一緒に改名する**。さらに 2026-09-28 ユーザー:「baseを分離して、userland/desktop/という階層を
   作ってください。そこに userland/desktop/wayland のように置いてください。」→ Keiland の部品は `userland/base` から出して
   `userland/desktop/<name>` に置く（`userland/desktop/wayland` 等）。menuconfig の Desktop の分類（BUG-080）はこの階層と揃える。
4. Keiland の Wayland の protocol と library: 接頭辞は **`keiland_`**（`zed_titlebar_v1` → `keiland_titlebar_v1` 等、`libzdesktop` → `libkeiland`）。
   `zwp_`・`zxdg_` は upstream の名前であり改めない。
