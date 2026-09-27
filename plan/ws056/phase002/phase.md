<!-- awesome-plan project=zedbsd record=ws056p002 -->

# ws056-p002: console での `POSIX-R2.ELF` の EINTR の経路の特定と修正（BUG-046）

Phase ID: `ws056-p002`
Parent: [WS056](../ws.md)
Status: cleared（2026-09-27。EINTR の経路を特定して libc で直し、guest で確かめた。`POSIX-R2.ELF` の console の status 0 は、timer の後の別の停止（下の「残り」）でまだ得られない）
Phase disposition: normal
Queue: なし（2026-09-27 のサブエージェントの作業。rate limit で止まった分を `salvage/ws056` db678c8b（親 0c65ed8f）から片付けのサブエージェントが検証して commit した）
依存: p001

## 目的と受け入れ

p001 の残り: console で `POSIX-R2.ELF` の SIGEV_THREAD の timer の試験（`thread-timer-callback`・`thread-timer-multiple-callback`）が `sem_timedwait` の EINTR で落ちる（[BUG-046](../../bugs/BUG-046.md)）。経路を特定して直す。
受け入れ（この Phase の範囲）: 経路の説明、修正、SIGEV_THREAD の timer と mask の置き換えの試験で EINTR 0、console で timer の試験が PASS、libc の build（warning 0）、規約。

## 原因

libc は wake の signal（63、`SIGRTMAX` より上の予約）を program の thread で block し、worker だけが受ける（p001 の BUG-042 の修正）。ところが
`sigprocmask(SIG_SETMASK)`・`pthread_sigmask(SIG_SETMASK)`・`sigsuspend`・`ppoll`・`pselect` は program の mask を
`PUBLIC_SIGNAL_MASK` で切ってから kernel に渡していたため、**置き換えの mask では予約の bit が 0 になり、program の thread で 63 が unblock された**。
kernel は signal を unblock している thread のどれかに wake を渡すので、program の thread が選ばれるとその thread の待ち（`sem_timedwait`）が EINTR で戻る。
console と SSH の違いは、sh・端末の処理が mask を保存と復元（`SIG_SETMASK`）する回数と timing の違い。

## 修正

| file | 変更 |
| --- | --- |
| `userland/base/libc/signal.c` | `__libc_signal_mask_keep_reserved()`（新、libc 内部）: program の mask の public の bit と、thread の今の予約の bit を合わせる。`sigprocmask` の `SIG_SETMASK`（`pthread_sigmask` もこれを通る）と `sigsuspend` がこれを使う。`SIG_BLOCK`・`SIG_UNBLOCK` は従来どおり予約の bit を外して渡す |
| `userland/base/libc/poll.c` | `ppoll`・`pselect` の待ちの mask も同じく予約の bit を保つ |
| `plan/ws056/tests/sigev-thread-mask.c`・`guest-sigev.sh` | 再現と確認: 各回に mask を空に置き換え（`sigprocmask` と `pthread_sigmask` を交互）、1 ms の SIGEV_THREAD の timer の callback が post する semaphore を `sem_timedwait` で待つ。300 回の EINTR を数える |
| `plan/ws056/tests/config-amd64-serial.mk`・`console-posix-r2.sh`・`console-probe.sh` | console（serial mirror）で `POSIX-R2.ELF` を走らせる（`AS_SH=1` で ELF を `/bin/sh` にも置く。試験が自身を `/bin/sh` として spawn・exec するため） |
| `plan/ws056/tests/spawn-probe.c`・`guest-spawn-probe.sh` | SIGEV_THREAD の timer を使った process からの `posix_spawn` の確認（SSH） |

HAL・kernel は不変。salvage にあった `userland/base/tests/posix-r2.c` の調査用の印（`M1`〜`M6` の write）は commit していない。

## 確認（2026-09-27、QEMU。実機は未実施）

image: `make -j48 ZEDBSD_CONFIG=plan/ws056/tests/config-amd64-serial.mk BUILD=build/ws056/image disk-image`（status 0、warning 0）→
`sh plan/tools/guest/hybrid-image.sh build/ws056/image build/ws056/hybrid.img`（main の `build/ws053-full-hal-guest` の package に、この木の vmunix・libc.so・make・sh）。

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws056/tests/guest-sigev.sh build/ws056/hybrid.img 3`（修正後の libc） | EINTR 0/300・0/300・0/300、status 0 ×3 |
| 同じ試験を修正前の libc（`build/ws053-full-hal-guest/hdd-image.img` そのまま） | EINTR 155/300・29/300・92/300、status 1 ×3（サブエージェントの報告は 18〜68/300） |
| console の `POSIX-R2.ELF`（`AS_SH=1 console-posix-r2.sh ... 5`、と印入りの ELF で `console-probe.sh`） | 2 回とも `R2:TIMER:PASS`（timer の試験は通る）。その後に止まる（下の「残り」） |
| SSH の `guest-spawn-probe.sh` | spawn の子の status 23、`/bin/echo` 0。`POSIX-R2.ELF` は SSH では従来どおり `conformance-identity`（console が要る）まで |
| style（`style-check.py`） | `signal.c` 71 → 67・`poll.c` の件数は減るだけ（変えた行に新しい違反なし）、試験の C 0 |
| boot test | 未実施（guest は起動・login した。libc の変更の最終の boot test は merge 後の回帰で） |
| 実機 | 未実施 |

## 残り

- console で `POSIX-R2.ELF` は timer の試験・WNOWAIT・tmpfs・lock・NOFILE・`posix_spawn` まで通り（印 M1〜M6 を確認）、最後の `execve("/bin/sh")`
  （`R2_EXEC_FINAL=1` の自身。`R2:01-06:PASS` を書いて 0 で終わるはず）の後に何も出ずに止まる（2/2）。修正前は timer で落ちていたので、以前からあったかは不明。
  多 thread（SIGEV_THREAD の worker が居る）の process の console での execve が疑わしい。**別の bug として追跡**（ID の割り当ては main）。
  これが直るまで BUG-046 の受け入れ案「console で 5 回連続 status 0」と p001 の clear の条件は満たせない。
- p001 の clear はユーザーの判断（この Phase は p001 を clear しない）。
