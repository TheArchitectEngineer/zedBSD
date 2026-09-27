<!-- awesome-plan project=zedbsd record=ws073p007 -->

# ws073-p007: BUG-068 — 多 thread の process の execve が兄弟の thread の終わりを待ち続ける

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-068](../../bugs/BUG-068.md)（main の依頼。ws056-p001 を塞いでいた）

## 目的と受け入れ

他の thread が生きている process の `execve` が、兄弟の thread を全て終わらせて新しい image に移る。console の `POSIX-R2.ELF` が最後の自分自身への
`execve` の後に `R2:01-06:PASS` を出して status 0 で終わる。

## 再現と切り分け（QEMU。main の kernel、serial mirror 付き `plan/tools/posix/config-amd64-serial.mk`）

- console の `POSIX-R2.ELF`（`/bin/sh` にも置き、root の login shell は `/bin/sh.orig` に替えて SSH を使えるようにした）: `R2:TIMER:PASS` の後で止まる（ticket と同じ）。
- gdbstub（`vmunix` の `all_processes` を、`-g` で compile した `process.c` の型で読む）: 止まった process の thread は 2 つ。main の thread は
  `process_exec_file` の `sched_sleep`（兄弟の終わりを待つ loop）、残りの 1 つは `usync` の待ち（libc の detached thread の reaper）で
  `terminate_requested` 0。exec が待っている 3 つ目の thread は既に list に無い。
- 最小の再現 [tests/exec-threads.c](../tests/exec-threads.c) の `reaper`: detached の thread を 1 つ作って libc の reaper を起こし、次に sem_wait で
  止まる thread を detach する（reaper が kernel の join でそれを待つ）。そして自分を `execve`。SSH で 1 回目から止まり、`timeout` の signal でも
  終わらない（決定的）。`plain`・`thread`・`detached`・`detach`・`timer`・`spawn` は通る。

## 原因

`process_exec_file()`（`src/kern/exec.c`）は兄弟の thread を 1 つずつ `terminate_requested` にし、`while (other->state != THREAD_ZOMBIE)
sched_sleep()` で待ってから自分で `thread_wait` していた。兄弟が kernel の `thread_join` で待たれている（libc の reaper）と、兄弟が zombie になった
瞬間に reaper が起きて先に reap する（REAPING → DEAD、list から外れる）。exec の loop は ZOMBIE を見逃し、DEAD のまま永久に待つ。
reaper は exec より前に作られた thread なので、list の順で後から終わらせる予定で、まだ生きている。console で出て SSH で出なかったのは
SSH では test が途中（`conformance-identity`）で終わり exec に届かないため。

## 修正

`src/kern/exec.c`: `exec_thread_retired()`（新）が ZOMBIE・REAPING・DEAD を「終わった」とし、exec の待ちはそれで抜ける。zombie のままなら従来どおり
exec が `thread_wait` で reap し、joiner が先に取ったなら `thread_wait` は EBUSY で何もしない。HAL は触れていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `make -j48 ZEDBSD_CONFIG=plan/tools/posix/config-amd64-serial.mk BUILD=build/ws073-serial vmunix` warning 0。
- `tests/exec-threads.c`（SSH）: 7 つの mode 全て `EXEC:PASS`、status 0。`reaper` を 10 回続けて 10/10（修正前は 1 回目で止まる）。
- console の `POSIX-R2.ELF`（この tree の libc で static link、`/bin/sh` にも置く）を 5 回: 4 回 `R2:TIMER:PASS`・`R2:01-06:PASS`・status 0。
  5 回目は `R2:TIMER:PASS` の前（pty の back-pressure の試験）で止まった。gdbstub で調べると別の不具合で、pty の master の read と slave の write が
  `waitq_sleep` の EAGAIN（条件の lock を外した間の wakeup）を失敗として返していた → [BUG-069](../../bugs/BUG-069.md)、[ws073-p008](../phase008/phase.md)。
- 規約: `tests/style-diff.py src/kern/exec.c` 0。

## 残り

- BUG-046 の受け入れ（console で 5 回続けて status 0）は BUG-069 の修正（p008）の後で確かめる。
