<!-- awesome-plan project=zedbsd record=ws073p008 -->

# ws073-p008: BUG-069 — 端末と pty の読み書きが waitq_sleep の EAGAIN を失敗として返す

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-069](../../bugs/BUG-069.md)（ws073-p007 の確認で見つけた）

## 目的と受け入れ

blocking の pty の slave の write は全てを書き、master の read と端末の read は data が来るまで待つ（spurious な EAGAIN・途中の count を返さない）。
console の `POSIX-R2.ELF` が続けて status 0 で終わる（BUG-046 の受け入れの案「console で 5 回連続 status 0」）。

## 再現（修正前、QEMU。p007 の kernel）

- console の `POSIX-R2.ELF` の 5 回中 1 回が `R2:TIMER:PASS` の前で止まった。gdbstub: main の thread は `pty_master_read` の `waitq_sleep`、
  back-pressure の子（6000 byte の blocking の write）は exit 42（write が途中で返った）の zombie。
- [tests/pty-backpressure.c](../tests/pty-backpressure.c)（同じ試験の繰り返し。SSH）: 50 回中 3 回、100 回中 9 回で master の read が EAGAIN で返り、
  子の write は途中まで。

## 原因

`waitq_sleep(..., WAITQ_INTERRUPTIBLE)` は signal を調べる間に条件の lock を外し、その間に来た wakeup を EAGAIN（条件を見直せ）で返す。
`src/kern/tty.c` の 4 か所（`tty_read_canonical`・`tty_read_noncanonical`・`pty_output_bytes`・`pty_master_read`）がそれを失敗として返していた。
同じ file の他の 2 か所（2105・2730 行付近）は既に再試行にしていた。BUG-057（socket）と同じ種類。他の file の interruptible な
`waitq_sleep` の呼び出しを全て見た（script で 77 か所）: EINTR だけを失敗にして loop に戻るか、EAGAIN を除いており、残りはこの 4 か所だけ。

## 修正

`src/kern/tty.c` の 4 か所で EAGAIN を再試行（loop が条件を見直す）にした。HAL は触れていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `plan/ws056/tests/config-amd64-serial.mk` の vmunix warning 0、i386 pcat の vmunix warning 0。
- `pty-backpressure 200`（SSH）: 0/200 失敗（修正前 3/50・9/100）。
- console の `POSIX-R2.ELF`（p007 と同じ手順。`/bin/sh` にも置く）: **10 回続けて status 0**、毎回 `R2:TIMER:PASS`・`R2:01-06:PASS`。
  p007 の exec の修正と合わせて BUG-046 の受け入れの案（5 回連続）を満たす（BUG-046 の判断と ws056-p001 の clear は WS056・ユーザーの側）。
- boot test: lean amd64 image（`plan/ws045/tests/config-amd64-base.mk`、exec.c と tty.c の修正を含む）で `plan/tools/boot-test.sh` PASS
  （`build/ws073-img/boot-test-p008/login.png`）。
- 規約: `tests/style-diff.py src/kern/tty.c` 0。
