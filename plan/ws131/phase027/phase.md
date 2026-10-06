# ws131-p027: sessiond が session の socket で Power Off・Restart を受ける

Status: planned（2026-10-06 Q1 が作成）
WS: [WS131](../ws.md)
Related: [ws099-p037](../../ws099/phase037/phase.md)（App Home の Power Off と確認の dialog）・[BUG-235](../../bugs/BUG-235.md)

## 出典

2026-10-06 P2 の報告: 今の zedBSD では sessiond は session の socket で POWER を受けず（`sessiond/session.c`、POWER は greeter の socket だけ）、
libkeiland-backend-zedbsd の power_actions も session では 0（ENOTSUP）。WS131 design の D12 は「zedBSD の session は unsupported、sessiond の拡張は別の WS（ユーザーの判断）」。
2026-10-06 ユーザー（クリックの回答）「sessiond に口を足す（別の Phase）」: p037 の dialog は Power Off・Restart を actions に無い時は押せない形で出し、sessiond に session の POWER を足す作業をこの Phase で続けて行う。

## 範囲

- sessiond: login 中の session（console の session の利用者）から poweroff・reboot を受ける。誰が頼めるか（console の session の持ち主だけ、他の session がある時の扱い）を設計に書く。
- libkeiland-backend-zedbsd: power_actions に poweroff・reboot を出し、session の socket へ頼む。
- WS131 design D12 を改訂する。Linux（logind）・FreeBSD は変えない。
- 試験: host の sessiond の試験、QEMU の確認は T1（dialog の Power Off で guest が止まる）。
