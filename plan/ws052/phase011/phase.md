<!-- awesome-plan project=zedbsd record=ws052p011 -->

# ws052-p011: sessiond の sleep と backend、sleep が使えるかの kernel の flag

Phase ID: `ws052-p011`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-07 P2: kernel の flag・sessiond・backend を実装・build・host 試験まで。QEMU・実機は compositor（p012）の後にまとめて）
Phase disposition: normal
Queue: 2026-10-07 Q1 の ACK「p011（sessiond の sleep.c・backend、A1 の kernel の bit）に進んでよい」。UAPI の A1 はユーザーの承認（2026-10-07、N1）

## 範囲

設計 [ws052-p007](../phase007/phase.md)（第 4 版）の §1.1（sessiond の答え）、§1.2（backend）、§6 A1（`KERN_SYSTEM_POWER_FLAG_CAN_SLEEP`）。compositor（p012）と Settings（p013）は範囲外。

## 実装（2026-10-07）

- UAPI（`include/uapi/system.h`）: `struct system_power_info` の `reserved[0]` を `flags` に、`KERN_SYSTEM_POWER_FLAG_CAN_SLEEP 0x1U`、大きさ 32 と `flags` の offset 20 の static assert。kernel（`system-device.c` の `system_get_power`）が `kern_sleep_supported()` の時に立てる。
- sessiond: 新しい `sleep.c`（`POWER suspend` で helper の子 process: networkd に SLEEP_PREPARE（opcode 80、`NETWORKD_SLEEP_PREPARE_SECONDS`+5 秒）→ cancel の pipe の byte を見る → `KERN_SYSTEM_SLEEP` S0IDLE → SLEEP_END（2 秒、2 回まで）→ 答えの 1 行を pipe へ。親は session と greeter の loop の poll に pipe を足し、答えを要求した socket へ（閉じた socket には書かない、`sessiond_sleep_forget`）。`POWER cancel` は答え無し。poweroff・reboot の後と helper の進行中は `ERROR busy`、`NOSLEEP unsupported` は sessiond の間覚える）、新しい `sleep-rules.c`・`.h`（答えの行と networkd の答えの分類と errno の名前、純粋）、`power.c`（`sessiond_power_started`、session の `suspend`・`cancel` は wheel を問わない）、`greeter.c`（greeter の `suspend`・`cancel`、poll、文書の一覧）、`session.c`（poll、forget、文書の一覧）、`sessiond.h`、`Makefile`（sleep.c・sleep-rules.c・`userland/base/net/protocol.c`）。

- backend: `keiland-backend.h` に `struct kl_backend_power_outcome`（kind・error・wake・device・network の reason・resume の error）、`kl_backend_power_outcome`・`kl_backend_power_cancel_sleep`、`kl_backend_power_state` に `lid`（-1・0・1）と `can_sleep`。新しい `libkeiland-backend-zedbsd/power-outcome.c`・`.h`（答えの行を outcome に、純粋）。`power-zedbsd.c`（`power_read` で lid と `KERN_SYSTEM_POWER_FLAG_CAN_SLEEP`、`kl_backend_power_action(SUSPEND)` は新しい `power_sleep`: descriptor と can_sleep が要る（wheel は問わない）、既に出ていれば EALREADY、他の action・要求の答え待ちは EBUSY、`POWER suspend`。`kl_backend_power_cancel_sleep` は `POWER cancel` を `KL_BACKEND_SESSION_NONE` で）、`session-zedbsd.c`（SUSPEND が出ている時の答えは login の答えの経路に流さず `session_slept` で outcome にし `power_asked` を戻し `session_answer(POWER, error)`、session の descriptor が閉じたら SUSPEND は ERROR で終える）、`backend-private.h`（`power_outcome`）、`sources.mk`。Linux（`power-linux.c`）と unsupported（`unsupported/power-unsupported.c`、FreeBSD も）に stub（ENOTSUP）と lid=-1・can_sleep=0。
- `power_actions` にはまだ SUSPEND の bit を足していない（app の system extension の SUSPEND が lock を通らずに眠らせないよう、§1.3 の compositor の経路（p012）と一緒に足す）。

## 確かめ（2026-10-07）

- build: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws052 build/ws052/bin/sessiond` rc=0（自分の file の warning 0、外部の OpenSSL の warning は別）、同じ config の `vmunix` rc=0 warning 0。
- host: `sh plan/ws052/tests/run-host-sessiond-sleep.sh` 21/0 PASS、`sh plan/ws052/tests/run-host-backend-sleep.sh` 13/0 PASS。
- build（backend の後）: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland` と `make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux` rc=0 warning 0。
- QEMU・実機: 未。

## 再開の情報

- p011 の範囲は実装済み。次は WS113 p003 → WS113 p004 の 1 出力の切り替え → ws052-p012（compositor の sleep.c、`power_actions` の SUSPEND の bit と system.c の経路、handoff.c の答えの経路）→ p013（2026-10-07 Q1 の順）。
- 以前の区切りの記録（2026-10-07、BUG-246 の前）: backend は未着手だった（今は済み）。
