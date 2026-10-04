<!-- awesome-plan project=zedbsd record=ws005-p032 -->
# ws005-p032: BUG-158 — panic を見える・残る形にし、AX211 の scan の失敗から off→on なしに戻る

Status: in-progress（q684-i01、P3 generation8、2026-10-04）
Disposition: normal
Parent: [WS005](../ws.md)
Bug: [BUG-158](../../bugs/BUG-158.md)（解析は同 ticket の「解析（2026-10-04、P3 / q684-i01）」と「割り込みの観点」）
Queue: q684（2026-10-04 user「BUG-158は…実装はOpus 5.5 Mid,テストはT1です。」「バグ修正はP3に移管します。」）

## 範囲

BUG-158 の解析の直し方 (1)〜(4) を実装する。フリーズの直接の原因は未確定（実機の判定は次の UAT の (B)。QEMU の passthrough の gdbstub は P4 の q684-i02）。
この Phase は「次の UAT で panic か deadlock かを分けられる」ことと「scan の 1 回の失敗で WiFi が ENETDOWN のまま戻らない」機能の不具合の直しまで。

1. panic・fatal・未処理の supervisor fault を klog の ring に残し、graphical（`/dev/graphics` を取った後）でも text を画面に戻す。syslogd が kernel log を
   `/var/log/kernel.log` に差分で追記して `fsync` する（前回の起動の分は `kernel.log.old`）。
2. AX211: `ax211_radio_scan_stop` の失敗の分岐で network worker の中で `session_stop` を呼ばず、poll の recovery に回す。recovery の後に、net device が
   open のままなら、WLAN の退役の thread（network worker でない）で driver が自分で再 open する（off→on なし）。poisoned の command transaction は
   既存の runtime stop の device reset の後（`drv_intel_ax211_command_after_device_reset`）に解け、再 open で新しい transaction になる。
3. AX211: runtime stop で master-disable の表示が timeout した時は、その回は DMA を release せず STOP_REQUIRED に留め、次の close の retry で release する。
4. 割り込みの mask の順の確認（HAL は変えない）。

範囲外: HAL（`include/hal/hal.h`・`src/hal/`）、i915 の scanout を firmware の plane に戻す panic の hook（残り）、ISR での cause の claim（ticket の「割り込みの観点」の案）。

## 受け入れ

- kernel の build（`config/ci/config-amd64.mk`、AX211 が有効）が warning 0。syslogd の build が warning 0。
- host 試験: AX211 の core 試験（既存）と、共通 WLAN の退役の thread が restart を呼ぶ試験・command の poisoned → reset → 再利用の試験が PASS。
- style（`plan/ws073/tests/style-diff.py`）の変えた行の findings 0、`git diff --check` 空。
- QEMU（T1、Q1 経由）: syslogd の `kernel.log` の追記と、panic の文字が graphical でも出ること（下の依頼）。
- 実機（次の UAT、ユーザー）: 下の手順 (B)。AX211 の passthrough は Guardrail で停止中なので使わない。

## 実装

（実装の後に書く）

## 検証

（実行の後に書く）

## 実機の手順（次の UAT、ユーザー）

（実装の後に書く）

## 残り

（実装の後に書く）
