<!-- awesome-plan project=zedbsd record=ws308-p004 -->

# ws308-p004 syscall ごとの process->lock を無くす（thread ごとの credential）と、credential の更新の競争

- 親: [WS308](../ws.md)。Queue: q1654（担当 P1）。前提: [p001](../phase001/phase.md)。
- Status: cleared（2026-10-28 P1: 実装 c438ea61c、review の反映 5a2746649、規約 b8ce0cdc3。QEMU の試験 PASS、5330 の null call の thread QD32 610k→1.41M IOPS（[p006](../phase006/phase.md)））
- Disposition: normal
- 範囲の決定: 2026-10-28 ユーザー（クリック）: iostress で見つけた前からの不具合 3 つは「WS308 の中で P1 が直す (推し)」。→ p004 は不具合 1（seteuid の繰り返しで retired_creds が溜まる）の直しとして行う。

## 不具合（[p006](../phase006/phase.md) の iostress で見つけた。WS308 の前の main でも出る）

- 多 thread の process で seteuid を繰り返し、他の thread が syscall の中に居続けると、他の process が命令の取り込みの page fault で落ち、guest の shell が消える。
- 原因: 全 syscall が `process_cred_read_enter/leave`（process->lock を 2 回）で reader を数え、置き換えた credential は reader が 0 になるまで `process->retired_creds` に積まれた。reader が常に居ると 0 にならず、seteuid ごとの記録と credential が溜まり kernel の heap が尽きる。

## 設計（FreeBSD の td_ucred と同じ形）

- `struct thread` に `cred`（参照）。syscall の入口で `process_cred_sync(thread)` が `process->cred` と pointer を比べ（acquire）、違う時だけ process->lock の下で取り替える。thread の参照が credential の memory を保つので、pointer が同じなら同じ credential（ABA なし）。thread->cred を書くのはその thread だけ、`thread_release` が放す。1 thread が抱える古い credential は高々 1 つなので溜まらない。
- `process_cred_replace(process, expected, replacement, set_id)`: process->lock の下で `process->cred` が expected の時だけ公開し（release store）、set_id も同じ臨界区間で立てる。違えば EAGAIN で、`sys_cred_set_call` は最新の credential から作り直す（`cred_set_compose`）。exec の `process_cred_commit` は set_id も同じ区間で書き、古い参照を返して IRQ を戻してから放す。reservation の API・`cred_readers`・`retired_creds` は無くした。
- 自 process の credential の読み: `cred_current()`・`cred_current_ref()` は thread の credential（1 回の syscall の中で一つ）。syscall.c の自 process の `process->cred` の生の読みは `cred_current()` に。他の process の ruid（wait の event、SIGCHLD の info）は lock の下で、SIGCHLD の ruid は zombie にする臨界区間で取る。ptrace の attach は credential と set_id を一緒に読む（`process_cred_ref_set_id`）。si_uid は送り手の real user ID（POSIX）に揃えた（kill・sigqueue）。透過の stop からの再実行の前にも sync する。
- 他 thread に効く時期: 他の thread は次の syscall の入口で新しい credential に移る。窓は「setuid が戻る前に始まり、まだ続いている syscall」だけで FreeBSD と同じ。Linux＋glibc は setxid の broadcast で全 thread を待つのでこれより強い。setuid が戻った後に始まった syscall が古い credential を使う道は無い（design-reviewer）。以前の zedBSD は読むたびに最新を読んでいたので、1 回の syscall の中で credential が変わり得た。

## レビュー（2026-10-28、design-reviewer（opus）、c438ea61c）と反映（5a2746649）

- High の退行なし。H1（前から: setuid 系の read-modify-write が不可分でなく、同時の seteuid(0) が setuid(1000) を消して root に戻る）→ expected と EAGAIN。M1（前から: set_id が lock の外で後から立ち、ptrace の attach の窓）→ 同じ臨界区間。M2（1 syscall の中で snapshot と最新が混ざる）→ `cred_current_ref` も thread の credential。L1（fallback の comment）・L2（redispatch の sync）・L3（SIGCHLD の si_uid）・L4（si_uid を ruid）・L5（IRQ を止めた中の release）・L6（comment、store）→ 反映。

## 試験

- 新しい `plan/ws308/tests/credrace.c`・`qemu-credrace.sh`: lost update（setuid(1000) と seteuid(0) の同時、2000 回）、権限を落とした thread の後で他の thread が root だけの file の open・chmod・kill(1, 0) を断られる（2000 回）、read で寝ていた thread が起きた後に断られる（201 回）、SIGCHLD と sigqueue の si_uid が real user ID（201 回）。
- 結果（2026-10-28、QEMU/KVM 8 CPU、serial の image）:

| | lost | drop | sleeper | siginfo |
| --- | --- | --- | --- | --- |
| Linux（host、root） | 0/2000 | 0/2000 | 0/201 | 0/201 |
| 今（5a2746649） | 0/2000 | 0/2000 | 0/201 | 0/201 |
| 修正の前の main（af37831de） | 3/2000 FAIL | 0/2000 | 0/201 | 201/201 FAIL（queue_uid が euid の 0） |

- iostress の setuid（cached・direct・boot disk の direct、各 20 s、seteuid 2200〜2900 万回）: 今 PASS 3/3、修正の前の main は iostress が rc=137。
- build: config-5330・serial と、全 platform の vmunix（config/ci の 16 と plan/tools/ports の 8、d30136b22）で rc 0・warning 0。
- 未実施: setuid の binary の exec を兄弟の thread の seteuid と競わせる試験（setuid の binary を image に置く道が要る）、5330 の数。
