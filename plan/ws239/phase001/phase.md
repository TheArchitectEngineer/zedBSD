<!-- awesome-plan project=zedbsd record=ws239-p001 -->
# ws239-p001: 調査・実装・host 試験・T の手順

Status: in-progress（2026-10-15 P: 実装・build・host 試験を終え、Q1 の判定待ち。QEMU の確かめは p002）
Disposition: normal
Parent: [WS239](../ws.md)
依存: 無し

## 範囲

[ws.md](../ws.md) の目的: graphical boot（logo・`kmsg=quiet`・`login=graphical`）で compositor が起動に失敗した時に、
(1) `/dev/system` を通して kernel の message を console へつなぎ直し、(2) graphical boot（logo）を解除し、(3) tty の login（getty）を出す。
正常な graphical boot は変えない。HAL の API（`include/hal/hal.h`）は変えない。

## 調査（2026-10-15、今の仕組み）

- `kmsg=quiet`: `src/kern/entry.c` が boot の行に `kmsg=quiet` があると `kern_log_set_quiet(1)`。`src/kern/klog.c` は quiet の間、記録を ring
  （dmesg）と debug port にだけ出し、console には出さず spinner を回す。pcat の text（`src/drivers/platform/pcat/graphics/text.c`）は
  cells を保つが描かない（logo が残る）。
- quiet の解除: `kern_text_reveal()`（`src/kern/text-display.c`）が `kern_log_set_quiet(0)` と backend の `reveal`（logo と spinner を消し、
  保った cells を描く）。呼ぶのは console の read（`src/drivers/generic/console.c` の `console_read`、つまり getty の最初の read）と、
  panic の fatal の形だけ。**`/dev/system` にはこれに当たる操作が無かった。**
- `login=graphical`: sessiond（init の service `greeter`、`replaces=getty_console`、`restart=on-failure`）が `kern.boot.login` を読む。
  sessiond が 0 で終わると init は `getty_console` を起こす（`release_replaced`）。sessiond が異常終了すると init は 5 回まで起こし直し、
  その後 getty。
- 既存の失敗の扱い（BUG-122、ws099-p010）: greeter（`/bin/wayland --greeter`）が起動から 20 秒以内に error・signal で終わると失敗。
  6 回続くと（待ち 1・2・4・8・16 秒）sessiond は 0 で終わり getty へ。greeter・display（`/dev/gpu0`）が無い時もすぐ getty へ。
- 抜けていた所:
  1. kernel の message は getty の read で暗黙に戻るだけで、`/dev/system` を通した明示の操作が無い。fallback の理由も画面に出ない。
  2. READY（display を取る直前の合図）の前に **status 0** で終わる greeter は「自分で終わった」扱いで、数えずに待ち無しで起こし直し続ける。
  3. READY を言わずに **固まった** greeter は sessiond が永久に待つ（logo のまま）。
  4. 起動時の自動 login（`/etc/keiland/autologin`）の session が READY を言わずに固まると、sessiond は永久に待つ（logo のまま）。

## 「失敗」の定義（fallback-rules.c）

- greeter が起動に失敗した = (a) READY の前に終わった（status に依らず: すぐ終わる・起動中の crash）、(b) 起動から
  `SESSIOND_GREETER_START_SECONDS`（60 秒）以内に READY を言わない（sessiond が SIGTERM→SIGKILL で終わらせる、固まり）、
  (c) READY の後でも起動から 20 秒以内に error・signal で終わった（BUG-122 の従来どおり）。
- 起動時の自動 login の session が 60 秒以内に READY を言わない（遅れた READY も数える）と SIGKILL で終わらせ、これを 1 回目の失敗とする。
  READY の前に終わった session も 1 回目の失敗。
- console に戻す: 続けての失敗が `SESSIOND_GREETER_FAILURES`（6 回、従来どおり）、または 2 回以上で最初の失敗の試みの開始から
  `SESSIOND_FAILURE_STREAK_MS`（90 秒）を過ぎた時。すぐ終わる compositor は約 30 秒（6 回）、固まる compositor は約 2 分（2 回）で console。

## 設計（最小の変更）

1. **新しい `/dev/system` の操作**（kernel の ABI の追加、Q1 に報告）: `KERN_SYSTEM_SHOW_CONSOLE = _IO('s', 21)`（`include/uapi/system.h`）。
   引数無し、root だけ（EPERM）、何度でも可。`src/drivers/generic/system-device.c` の `system_show_console()` が既存の
   `kern_text_reveal()` を呼ぶ（kmsg を console に戻す = `kmsg=console` と同じ出力先、logo と spinner を消し、保った text を描く）。
   その後に `system: the console is shown again (pid N asked; the log was quiet)` を kernel の log に出す（画面で確かめる目印）。
   新しい kernel の処理は書かず、getty の read が既に通る経路を ioctl から呼ぶだけ。HAL は不変。
2. **sessiond**（`userland/desktop/sessiond/`）:
   - `fallback-rules.c/.h`（新）: 上の定義（`sessiond_greeter_failed()`、`sessiond_console_due()`）。file・process に触れないので host 試験できる。
   - `greeter.c`: greeter の READY を記録し、READY 無しで 60 秒経った greeter を終わらせて失敗とする。終わり方の判定を `sessiond_greeter_failed()` に。
   - `session.c`・`sessiond.h`: `session_handoff()` が READY の有無を返し、遅れた READY も記録する（返事は従来どおり）。
     `session_bounded`（main が自動 login の間だけ立てる）の時、60 秒 READY の無い session を SIGKILL。READY の無かった session は `EIO` を返す。
   - `main.c`: 自動 login の失敗を 1 回目に数え、`sessiond_console_due()` で console に戻す。graphical が求められていた時の
     console への戻し（greeter・display 無し、失敗の続き）で `main_console_back()`: `/dev/system` に `KERN_SYSTEM_SHOW_CONSOLE`、
     `/dev/console` に `sessiond: <理由>; the console login takes over` の 1 行。その後 0 で終わり、init が getty を起こす（従来どおり）。
   - `login=console`（boot-parameters）の時は何も変えない（getty の read が従来どおり reveal する）。
3. 変えない物: init、getty、klog、text の backend、HAL、正常な graphical boot（READY を言う compositor は従来と同じ流れ）。

制限・注意:
- READY を言わない独自の session の script を自動 login に使うと 60 秒で終わる（普通の `/etc/keiland/session` は compositor が READY を言う）。
  greeter 経由の login の session は従来どおり時間で終わらせない。
- console に戻した後に出るのは、戻した後の kernel の message。quiet の間の message は dmesg で読む（ring に残る）。前の分を画面に流し直すのは範囲外。
- sessiond 自体が落ち続けた場合（init が 5 回起こし直した後に getty）は、getty の read による従来の reveal に頼る（`/dev/system` は呼ばない）。
- rpi4 の text の backend には `reveal` が無いので、kmsg を console に戻す所だけが効く（logo の解除は pcat のみ）。rpi4 の config には sessiond が入るが、
  rpi4 の QEMU の確かめは p002 の範囲外（GPU が無い）。

## 確かめ（2026-10-15、P、worktree `/home/awe/zedBSD-worktrees/ws239`）

- build（共有の toolchain は read-only の symlink）:
  - `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` → rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。
  - `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` → rc 0、warning 0、arm64 vmunix check PASS。
  - `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk ZEDBSD_USER_PROGRAMS=sessiond build/amd64/bin/sessiond`（と init）→ rc 0、
    sessiond の source の warning 0（log の warning は外部 package の openssl と perl の locale だけ）。
  - `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk ZEDBSD_USER_PROGRAMS=sessiond build/arm64/bin/sessiond` → rc 0、warning 0。
- host 試験: `sh plan/ws239/tests/run-host-fallback-rules.sh` → `WS239 p001 fallback rules host test PASS (plain, ASan/UBSan)`
  （gnu89 -Wall -Wextra -Werror、22 項目: READY 前の終わり・固まり・READY 後の quick な error と signal・clean な終わり・遅い crash、6 回、時間の上限）。
- `git diff --check` OK。
- 未実施: QEMU・実機（p002 で T に依頼）。ioctl の root 以外の EPERM は実機・QEMU で未確認。

## T の手順（ws239-p002 用）

image は T が依頼の tree（このブランチの SHA、または main に merge 後の SHA）で作る。config は
[config-amd64-fallback.mk](../tests/config-amd64-fallback.mk)（CI の config のまま: graphical boot・`kmsg=quiet`・`login=graphical`・sessiond・
compositor・自動 login）。失敗させる compositor は個別の file の複写だけで入れる（[wayland-exit.sh](../tests/wayland-exit.sh): すぐ exit 1、
[wayland-hang.sh](../tests/wayland-hang.sh): READY を言わず sleep）。console の login は tty で確かめる（SSH の login ではない）。

| 場合 | image | 起動 | 合格の条件 |
| --- | --- | --- | --- |
| A: display 無し（`/dev/gpu0` 無し） | `plan/tools/guest/test-image.sh plan/ws239/tests/config-amd64-fallback.mk build/ws239-a` | `OUTPUT=build/ws239-a-boot BOOT_TIMEOUT=240 plan/tools/boot-test.sh build/ws239-a/hdd-image.img`（`-vga std` なので GPU の node は無い） | boot-test PASS。`login.png` に logo が無く、`system: the console is shown again (pid N asked; the log was quiet)` と `sessiond: there is no greeter or no display; the console login takes over` の行、その後の kernel の message、`login:` が見える |
| B: compositor がすぐ終わる | `test-image.sh plan/ws239/tests/config-amd64-fallback.mk build/ws239-b --file /bin/wayland=plan/ws239/tests/wayland-exit.sh --mode /bin/wayland=0755` | Venus の guest（`plan/ws035/tests/zdesktop-guest.sh start build/ws239-b/hdd-image.img`、`/dev/gpu0` が要るので boot-test.sh の `-vga std` では試せない）。console は標準 VGA の head にある | 起動から約 90 秒以内に、VGA の head の screendump（PNG）に logo が無く、`system: the console is shown again` と `sessiond: the graphical login failed to start; the console login takes over` と `login:`。SSH で `/var/log/sessiond.log` に `SESSIOND GREETER failed ready=0`（自動 login の account があれば先に `SESSIOND AUTOLOGIN failed errno=5`）、`SESSIOND CONSOLE reason=greeter-failed failures=6`、`SESSIOND CONSOLE show`。`dmesg` に `the console is shown again` |
| C: compositor が固まる | B と同じで `--file /bin/wayland=plan/ws239/tests/wayland-hang.sh` | B と同じ Venus の guest | 起動から約 3 分以内に B と同じ画面。sessiond.log に `SESSIOND SESSION not ready`（自動 login）または `SESSIOND GREETER not ready` が 2 回、`SESSIOND CONSOLE reason=greeter-failed failures=2` |
| D: 正常（変わらないこと） | `test-image.sh plan/ws239/tests/config-amd64-fallback.mk build/ws239-d`（複写無し） | B と同じ Venus の guest | 自動 login の desktop（または greeter）が出る（venus の head の PNG）。sessiond.log に `SESSIOND CONSOLE`・`not ready`・`failed ready=0` が無く、`dmesg` に `the console is shown again` が無い |

tty の login（A・B・C）: console に `root`（Enter）とその image の root の password（Enter）を打ち（QMP の `send-key`、または T の通常の手順）、
`echo WS239-LOGIN-OK; dmesg | grep -c 'the console is shown again'; tail -n 3 /var/log/sessiond.log` の結果が出た screendump の PNG を撮る。
`WS239-LOGIN-OK` と 1 が見えれば合格。console の log・serial の log は判定に使わない（画面の PNG と SSH の結果で判定）。

注: A は変更の前の image でも getty の read で `login:` が出る（従来の暗黙の reveal）。変更の効果の目印は 2 つの行（kernel の
`system: the console is shown again` と sessiond の行）。B・C・D は GPU の node が要るので Venus の guest を使う（BUG-122 の
`plan/ws099/tests/bug122-recovery.sh` と同じ guest）。boot-test.sh 以外の起動の手段を使うことの可否は Q1 が判断する。

## 成果

- commit: このブランチ `agent/ws239`（SHA は Q1 への報告に書く）。
- 変えた file: `include/uapi/system.h`、`src/drivers/generic/system-device.c`、`userland/desktop/sessiond/{main.c,greeter.c,session.c,sessiond.h,Makefile}`、
  新 `userland/desktop/sessiond/fallback-rules.{c,h}`、`plan/ws239/tests/`（host 試験と T の入力）。
- 残件: p002（QEMU の確かめ、T）。WS の最後の規約の全文の見直しの Phase は ws.md に無い（Q1 が要るか判断）。
