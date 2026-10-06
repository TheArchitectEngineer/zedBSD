<!-- awesome-plan project=zedbsd record=ws170-p004 -->

# ws170-p004: compositor のメッセージの API の骨格と偽の backend

Status: in-progress（実装・host の試験・build 済み。QEMU は T1）
Disposition: normal
Parent: [WS170](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)

## 実装（2026-10-07、P2）

設計は [p001](../phase001/phase.md) §3。Q1（2026-10-07）: loopback を desktop の設定で選ぶ形は Q1 の技術判断として了承。

- 線の形: `kl_system_phone_v1`（manager の version 16、`get_phone` は opcode 11、capability `0x1000`）: send・call、event の received・status・result。
- compositor: `wayland/phone-shell.c`（新規）。backend の表（none・loopback）、設定 `phone.backend`（INT 0〜1、既定 0、compositor、desktop.conf に残す）。loopback: send は result OK・status SENT・DELIVERED、同じ番号から「Echo: …」を全部の phone object へ received。call は NO_ANSWER。backend 無しは result UNAVAILABLE（client で ENODEV）。log は長さだけ。`system.c`・`protocol.c`・`kwl.h`・3 つの Makefile、`settings-keys.c`。
- libkeiland（KL_VERSION は次の番号、worktree では 55 と仮置き）: `KL_SYSTEM_HAS_PHONE`（0x2000）、`KL_SYSTEM_CHANGED_PHONE`（0x1000）、`KL_PHONE_*`、`struct kl_phone_event`、`kl_system_phone_send`・`kl_system_phone_call`・`kl_system_take_phone_event`（16 個の ring）。`exports.map`。
- WS131 の `host-system` の試験に phone-shell.c と capability の期待を足した。

## 確かめ

- host: `sh plan/ws170/tests/run-host-phone-shell.sh` → PASS 8（backend 無しの send・call は UNAVAILABLE、loopback の send は OK・SENT・DELIVERED と 2 つの client への Echo、call は NO_ANSWER、channel の違いは INVALID、壊れた call は EPROTO、ring）。`plan/ws131/tests/host-system.sh` PASS。
- build: zedBSD の `bin/wayland`・`libkeiland.so` warning 0、`keiland-os-boundary` PASS、style-check 0。
- 未実施: QEMU（T1、AAT `apps.phone.message`）、Linux・FreeBSD の compositor の build。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS170 p004 の行。
