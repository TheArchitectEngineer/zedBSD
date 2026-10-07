<!-- awesome-plan project=zedbsd record=ws052p011 -->

# ws052-p011: sessiond の sleep と backend、sleep が使えるかの kernel の flag

Phase ID: `ws052-p011`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-07 P2: kernel の flag と sessiond は実装・build・host 試験まで（2458dc330）。backend は未着手、UAT の BUG-246 を先にするため区切り）
Phase disposition: normal
Queue: 2026-10-07 Q1 の ACK「p011（sessiond の sleep.c・backend、A1 の kernel の bit）に進んでよい」。UAPI の A1 はユーザーの承認（2026-10-07、N1）

## 範囲

設計 [ws052-p007](../phase007/phase.md)（第 4 版）の §1.1（sessiond の答え）、§1.2（backend）、§6 A1（`KERN_SYSTEM_POWER_FLAG_CAN_SLEEP`）。compositor（p012）と Settings（p013）は範囲外。

## 実装（2026-10-07）

- UAPI（`include/uapi/system.h`）: `struct system_power_info` の `reserved[0]` を `flags` に、`KERN_SYSTEM_POWER_FLAG_CAN_SLEEP 0x1U`、大きさ 32 と `flags` の offset 20 の static assert。kernel（`system-device.c` の `system_get_power`）が `kern_sleep_supported()` の時に立てる。
- sessiond: 新しい `sleep.c`（`POWER suspend` で helper の子 process: networkd に SLEEP_PREPARE（opcode 80、`NETWORKD_SLEEP_PREPARE_SECONDS`+5 秒）→ cancel の pipe の byte を見る → `KERN_SYSTEM_SLEEP` S0IDLE → SLEEP_END（2 秒、2 回まで）→ 答えの 1 行を pipe へ。親は session と greeter の loop の poll に pipe を足し、答えを要求した socket へ（閉じた socket には書かない、`sessiond_sleep_forget`）。`POWER cancel` は答え無し。poweroff・reboot の後と helper の進行中は `ERROR busy`、`NOSLEEP unsupported` は sessiond の間覚える）、新しい `sleep-rules.c`・`.h`（答えの行と networkd の答えの分類と errno の名前、純粋）、`power.c`（`sessiond_power_started`、session の `suspend`・`cancel` は wheel を問わない）、`greeter.c`（greeter の `suspend`・`cancel`、poll、文書の一覧）、`session.c`（poll、forget、文書の一覧）、`sessiond.h`、`Makefile`（sleep.c・sleep-rules.c・`userland/base/net/protocol.c`）。

## 確かめ（2026-10-07）

- build: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws052 build/ws052/bin/sessiond` rc=0（自分の file の warning 0、外部の OpenSSL の warning は別）、同じ config の `vmunix` rc=0 warning 0。
- host: `sh plan/ws052/tests/run-host-sessiond-sleep.sh` 21/0 PASS。
- QEMU・実機: 未。

## 再開の情報（2026-10-07、BUG-246 の前の区切り）

- 次: backend（`keiland-backend.h` に `struct kl_backend_power_outcome`（kind・error・wake・device[48]・radio・network の reason・resume の error）と `kl_backend_power_outcome`・`kl_backend_power_cancel_suspend`、`kl_backend_power_state` に `lid`（-1・0・1））、`power-zedbsd.c`（`power_read` で flags と lid、`power_actions` に SUSPEND の bit（descriptor があり CAN_SLEEP、wheel は問わない）、`kl_backend_power_action(SUSPEND)` は `POWER suspend`・既に出ていれば EALREADY・session の要求の待ちは EBUSY）、`session-zedbsd.c`（答えの語 `SLEPT`・`NOSLEEP` を解いて outcome に、SUSPEND の答えの時だけ `power_asked` を戻す、`session_gone` で SUSPEND が出ていれば `session_answer(POWER, EIO)`、`POWER cancel` は `KL_BACKEND_SESSION_NONE` で送る）、Linux（`power-linux.c`）と unsupported（`unsupported/power-unsupported.c`）に outcome・cancel の stub と lid=-1。
- 注意: `power-dialog.c`・`greeter.c`・`system.c` は actions の bit を読むので、SUSPEND の bit が立つと app（system extension）に SUSPEND が見える。§1.3（app の SUSPEND は compositor の sleep.c を通す）は p012 で。それまで backend が SUSPEND の bit を出すのは p012 と一緒に入れるか、p011 では出さず p012 で出す（案: p011 は bit を出す関数を用意し、power_actions には p012 で足す）。
- host 試験: backend の答えの解釈は `session-zedbsd.c` の static。純粋な部分を分けるなら `power-outcome.c` に。
