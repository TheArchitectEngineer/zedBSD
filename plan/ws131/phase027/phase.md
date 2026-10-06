# ws131-p027: sessiond が session の socket で Power Off・Restart を受ける

Status: cleared（2026-10-06 Q1 判定: T1-227b の (b) wheel でない時 poweroff=0 restart=0 で薄い、(a) Power Off で `SESSIOND POWER poweroff from=kei`・約 12 s で QEMU 終了。Restart は未実施。実機は UAT）
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

## 設計（2026-10-06 P2）

- 頼めるのは console の session だけ: sessiond が session の script に渡す control の socket（--control-fd）で来た `POWER poweroff|reboot`。他の経路は無い（ssh の利用者は socket に届かない）。
- 権利: 他の利用者が login していない（utmpx の USER_PROCESS の ut_user が session の利用者と違う物が 0）時は session の利用者が誰でも可。他の利用者がいる時は root か wheel の利用者だけ（logind の power-off-multiple-sessions が管理者を求めるのと同じ考え）。それ以外は `FAIL others`。違う言葉は `ERROR`。
- 実行は login の画面の POWER と同じ（`/sbin/poweroff`・`/sbin/reboot` を fork して exec、init が sessiond を含めて止める）。sessiond の log `SESSIOND POWER what from=user`、拒否は `refused others=N`、syslog の auth の log にも。
- backend（zedBSD）: `power_actions` は sessiond の descriptor（login の画面か session）があれば poweroff・reboot を出す。`kl_backend_power_action` は `kl_backend_session_send`（login の画面か session の descriptor）で送る。断り（FAIL・ERROR）の答えで `power_asked` を戻し、再び頼める。
- compositor: session の POWER の答えは `ZWL POWER answer error=`（handoff.c）。dialog（ws099-p037）は actions に出たので Power Off・Restart が押せるようになる。
- D12 の改訂: plan/ws131/design.md の D12 の行（表 2 つ）と §4.2 の電源の段。

## 実装

| 所 | 内容 |
| --- | --- |
| `userland/desktop/sessiond/power-rules.c`・`.h`（新、純粋） | `sessiond_power_program`（言葉 → program）、`sessiond_power_decide`（言葉、他の利用者の数、root・wheel） |
| `userland/desktop/sessiond/power.c`（新） | `sessiond_power_run`（log・syslog・fork・exec）、`sessiond_power_session`（utmpx で他の利用者を数え、`account_in_wheel`、答え） |
| `sessiond/greeter.c` | login の画面の POWER を `sessiond_power_program`・`sessiond_power_run` に（動作は同じ、log に from=） |
| `sessiond/session.c` | `POWER ` の行を `sessiond_power_session` へ、先頭の protocol の説明 |
| `sessiond/sessiond.h`・`Makefile` | 宣言、source |
| `userland/desktop/libkeiland-backend-zedbsd/power-zedbsd.c`・`session-zedbsd.c` | 上の backend の変更、条件の中の呼び出しの直し（既存の指摘 1 つ） |
| `userland/desktop/libkeiland-backend/keiland-backend.h` | power の説明の comment だけ（API は不変） |
| `userland/desktop/wayland/handoff.c` | `ZWL POWER answer error=` |

## 確認

| 確認 | 結果 |
| --- | --- |
| sessiond・compositor（zedBSD）・Linux の Keiland の build | 成功、warning 0 |
| `sh plan/ws131/tests/host-power-rules.sh`（ASan・UBSan でも） | 11 checks ok |
| `sh plan/ws131/tests/host-power.sh` | 17/17・6/6（session に sessiond の descriptor が無い時は何も出さない、に直した） |
| `sh plan/ws131/tests/host-session.sh` | 54/54（新: session の POWER poweroff を書く、FAIL others で EACCES、再び頼める、OK で答え） |
| style-check（変えた file） | 新しい指摘 0（session.c の既存の 1 つは触っていない所） |
| keiland-os-boundary | 既存の FAIL だけ |
| QEMU | 未実施。T1 に依頼: AAT の image で App Home の Power Off → dialog で Power Off が押せる（`poweroff=1`）→ 押すと guest が止まる |

## 2026-10-06 ユーザーの決定（誰が頼めるか）

P2 の既定（他の利用者がいなければ誰でも、いれば root・wheel だけ）を見せた質問へのクリックの回答「wheel だけに限る」: 他の利用者の有無に関わらず、Power Off・Restart を頼めるのは root と wheel の利用者だけ（console の session の control socket から）。wheel でない利用者の dialog では Power Off・Restart を押せない形にする。

## q793-i02（2026-10-06 P2）: wheel だけに限る

- ユーザーの決定（クリック）:「wheel だけに限る」→ 他の利用者の有無に関わらず Power Off・Restart は root と wheel の利用者だけ。前の「一人なら誰でも」（utmpx を数える）をやめた。
- sessiond: `sessiond_power_decide(what, uid, in_wheel, program)`（others の引数と utmpx の数えを削除）、拒否の答えは `FAIL wheel`、log `SESSIOND POWER what by user refused (not in wheel)`。
- backend: `power_actions` は session では `power_administrator()`（getuid が 0、または getpwuid と getgrnam("wheel") で primary か member）の時だけ poweroff・reboot を出す。毎回 /etc/group を読むので、group の変更は次に dialog を開いた時に効く。wheel でない利用者の dialog の Power Off・Restart は薄く押せない。
- 試験: host-power-rules 11 ok（wheel・root は可、wheel でない利用者は一人でも EPERM）、host-session 52/52（host の利用者が wheel なら往復、違えば ENOTSUP。この host の awe は wheel でないので後者を確認）、host-power 17/17・6/6。sessiond・compositor の build warning 0、style-check 0。
- AAT の image の kei は `userland/base/etc/group` で wheel（`wheel:x:0:root,kei`）なので dialog は `poweroff=1 restart=1`。
