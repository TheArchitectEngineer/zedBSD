<!-- awesome-plan project=zedbsd record=ws156-p002 -->

# ws156-p002: 通知の口（protocol・libkeiland）と compositor の受け取り

Phase ID: `ws156-p002`
Parent: [WS156](../ws.md)
Status: cleared（2026-10-06 Q1 判定: T1-269 の QEMU で期待どおり。実機は UAT）
Phase disposition: normal
Queue: Q1 の P2 の列（2026-10-06、q823 → ws089-p013 → WS164 の起動 → **WS156 p002** → …）

## 範囲

[p001](../phase001/phase.md) の §2（口と中身）・§3.3（置き換え）・§4（続けて来た時）・§5（log）の normal path の受け取りの側だけ。
popup の描画と動き・× と click・全画面と lock・system の通知の使う側は p003、log の画面と Super+N は p004、D-Bus は p006。

## 受け入れ条件

1. system manager の版 13 に `get_notify` があり、`kl_system_notify_v1`（post・withdraw、posted・activated・closed・result）が wire の上で通る。
2. compositor が通知を model に保つ: 番号、置き換え（同じ client の番号の時は同じ場所で言葉を変える）、待ち 8・client ごと 32・log 100（古い物から expired）、個別に消した物（dismiss）と withdraw は log に残さない、clear で log を空にする。
3. libkeiland に `kl_system_notify`・`kl_system_notify_withdraw`・`kl_system_take_notify_event`・`kl_app_notify` がある（KL_VERSION 49、`KL_SYSTEM_HAS_NOTIFY`）。
4. 試験の client `keiland-notify` が通知を出し、番号と事象を 1 行ずつ出す。
5. build の warning 0、style-check の新しい違反 0、host 試験 PASS。QEMU で `keiland-notify` の post が `KWL NOTIFY post` の行になる（T1）。

## 実装（2026-10-06 P2）

- protocol: `userland/desktop/libkeiland/system/kl-system-protocol.h`（`KL_SYSTEM_MANAGER_VERSION` 13、`GET_NOTIFY` 9、`CAPABILITY_NOTIFY` 0x400、
  request DESTROY/POST/WITHDRAW、event POSTED/ACTIVATED/CLOSED/RESULT、flag URGENT/ACTION、reason DISMISSED/EXPIRED/CLEARED/WITHDRAWN）。
- compositor:
  - `userland/desktop/wayland/notify.h`・`notify.c`: 純粋な model（server を知らない。host で単独に試せる）。post・withdraw・show_next・hide・dismiss・clear・shown・log（新しい順）・waiting・find。
  - `notify-shell.c`: `kl_system_notify_v1` の object と request、`kwl_notify_post_system`（compositor 自身の通知、client 0、p003 が使う）、`kwl_notify_model()`（p003・p004 が読む）。
    log の行 `KWL NOTIFY object|post|refused|withdraw|closed`。
  - `kwl.h`（`KWL_SYSTEM_NOTIFY`）、`system.c`（版 13 の capability と get_notify）、`protocol.c`（振り分け）、`Makefile`・`Makefile.linux`・`Makefile.freebsd`。
- libkeiland: `system/system-protocol.c`（`kl_system_notify_v1_interface`）、`system.c`・`system-view.c`・`system-private.h`（事象の ring 32）、`ui/app.c` の `kl_app_notify`、
  `keiland.h`・`keiland-ui.h`、`exports.map`（`exports.py` で作り直し、`--check` OK）。
- 試験の client: `userland/tests/keiland-notify/`（`[--app=] [--urgent] [--action] [--replaces=] [--withdraw-after-ms=] [--wait-ms=] TITLE [BODY]`、
  出力 `KEILAND-NOTIFY open|posted|result|activated|closed|done`）。`platform/amd64/vmunix.mk` に link の規則（keiland-system と同じ形）。
- 試験の image: `plan/ws156/tests/config-amd64-notify.mk`（zdesktop の image + keiland-notify）。

## 確認（2026-10-06 P2、host のみ）

| 確認 | 結果 |
| --- | --- |
| `plan/ws156/tests/run-host-notify-model.sh`（model の 19 項目、2 回） | 19/19 PASS ×2 |
| `plan/ws131/tests/host-system.sh`（capability に HAS_NOTIFY、posted、置き換えで番号が同じ、長すぎる title は INVALID、withdraw で closed と result） | PASS |
| build（zedBSD: libkeiland.so・wayland・settings・keiland-notify、`BUILD=build/p2-b194`、`ZEDBSD_CONFIG=plan/ws156/tests/config-amd64-notify.mk`。Linux: wayland・settings） | warning 0 |
| `python3 plan/tools/style-check.py <変えた file> --summary` | 新しい違反 0 |
| `python3 userland/desktop/libkeiland/exports.py --check` | OK |

未実施: QEMU（T1 に依頼: zdesktop の image で `keiland-notify Hello World` → `KEILAND-NOTIFY posted id=1` と compositor の `KWL NOTIFY post client=… id=1`、
`--replaces=1` で同じ番号、`--withdraw-after-ms=500` で `closed reason=4`）。実機は第 1 段の範囲外。

## 残り・移管

- popup の描画（p003）、log の画面と Super+N（p004）、D-Bus（p006）。
- 準正常・異常の未実装は [backlog-p2](../../ws177/backlog-p2.md) の WS156 の行。
