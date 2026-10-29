<!-- awesome-plan project=zedbsd record=ws309-p014 -->

# ws309-p014 process の起動（J5 の形）と clipboard の書き込みを poll で

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.10（J5）・§4.11（clipboard の spin）。Queue: q1662-i10（P1）。
- 状態: cleared（2026-10-29 q1662-i10 P1、branch `agent/p1-ws309i`。下の「結果」の未実施を参照）。
- 依存: p005（io-file）、J5（Q1 の決定: io-file の thread から `posix_spawn`、pid・kill・`waitpid(WNOHANG)` は main）。

## 計画に無かった依存と Q1 の決定

- zedBSD の libc の `posix_spawn` は file action が 16 個まで（`KERN_SPAWN_ACTION_MAX`）で、closefrom の action が無い。今の fork の子は 3〜1023 を閉じていたので、
  posix_spawn では compositor の descriptor（sessiond の control・auth の fd など、design のレビュー 7）が子に漏れる。
- 2026-10-29 Q1 の決定（Q1 の原文「Go with (a). Add `posix_spawn_file_actions_addclosefrom_np` to libc, matching the glibc and FreeBSD semantics
  (EBADF for a negative fd; at spawn, close every fd at or above the given one). I've added include/libc/spawn.h and userland/base/libc/posix.c to your
  ownership」、HAL ではなく libc の追加なので事前の承認は不要）: libc に `posix_spawn_file_actions_addclosefrom_np` を足す。

## 結果（q1662-i10）

- libc（`include/libc/spawn.h`・`userland/base/libc/posix.c`）: `posix_spawn_file_actions_addclosefrom_np(actions, from)`（負の from は EBADF、満杯は EINVAL）。
  子（vfork と exec の間）で from から `RLIMIT_NOFILE`（上限 1024）までを閉じる（開いていない番号は飛ばす）。
- compositor の新しい `spawn.c`: `kwl_spawn(server, command, then, context)`（`/bin/sh -c`、入力は /dev/null）と `kwl_spawn_lent`（1 つの fd を 3（3 そのものなら 4）に
  貸す、input method の `WAYLAND_SOCKET`）。main は引数・環境（`environ` の写しに XDG_RUNTIME_DIR・WAYLAND_DISPLAY・XDG_ACTIVATION_TOKEN を足し引き）を作り、
  io-file の lane（T7）が `posix_spawn`（`POSIX_SPAWN_SETSID`（無い system では付けない）、signal mask を空に、SIGINT・SIGTERM・SIGHUP・SIGCHLD を既定に、
  closefrom）。pid は job の done で main の then に。lane が無ければその場で。
- 呼び手（then で今の log の行を出す）: App Home（`KWL HOME launch name= pid=`、名前は写し）、desktop の program（`desk.starting` で二重の起動を防ぐ）、
  installer・Welcome、Notes（corner、`corner.launching` は頼んだ時から）、Bluetooth の menu の Settings、通知の action、input method（接続を先に作り、program の
  pid は後から。pid が来る前の method の変更は `kill_pending` で、pid が来た時に SIGTERM）。main は fork しない（`fork()` は compositor に残っていない）。
  `waitpid(-1)`（home.c）は残す（design §11）。
- clipboard（clipboard.c）: 提供の text の書き込みは spin しない。今書けるだけ書き、残りは pass ごと（`kwl_clipboard_poll`、10 ms 以内に回る）に、2 s で諦める。
  同時に 8 本、text は写しを持ち（offer が変わってもよい）、lock の clear で書き込み中のものも閉じて写しを消す。design §4.11 の「poll の集合に入れる」形ではなく、
  読みの側と同じ pass ごとの形にした（main の poll は 10 ms で回るので 1 pipe 分（64 KiB）の遅れは最大 10 ms）。
- 確かめ（2026-10-29）:
  - build: amd64 `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/sysroot/usr/lib/libc.o build/amd64/dynamic/libc.so build/amd64/bin/wayland`
    exit 0・`warning:` 0。arm64 `config/ci/config-rpi5.mk` の `build/arm64/sysroot/usr/lib/libc.o build/rpi5/dynamic/libc.so build/rpi5/bin/wayland` exit 0・0。
    riscv64 `config/ci/config-visionfive2-keiland.mk` の `build/riscv64/sysroot/usr/lib/libc.o build/visionfive2/dynamic/libc.so build/visionfive2/bin/wayland` exit 0・0。
    Linux `keiland-linux.mk`（-Werror、glibc の addclosefrom_np）exit 0・0。
  - host `plan/ws309/tests/clip-write/run.sh`（ASan+UBSan）PASS: 60 KiB を 4 KiB の pipe へ、offer は待たず、pass で全て届く、読まない相手は 2 s で一部、lock の clear で閉じる。
  - QEMU（libc）`plan/ws309/tests/spawn-closefrom/run.sh build/ws309/p014w …` PASS（静的に link した試験: 負の fd は EBADF、dup2 で 4 を残して 5 から閉じる）。
    同じ条件の 2 回（p014u・p014v）は guest の SSH が 240 s で答えず（test の中身の前。`zdesktop-guest.sh start` は QEMU を起こした（start.log に pid と ssh の port）、
    `guest.py wait` が「no answer on SSH within 240s」）。image は `build/ws309/img/hdd-image.img`（2026-10-29 14:16 の build、snapshot で起動）、前後の p014t・p014w・p014z は
    同じ image で答えた。その時の host には他の担当の QEMU（p2-ws068、CPU 84%）が動いていた。原因は未解析（gdbstub・QMP で見ていない、Q1 の指示: 再び出たら gdbstub・QMP で見る）。
    参考（2026-10-30 Q1 から、P4 の解析）: host の fdatasync が遅い時（load 12〜15）、boot の USB stick の xHCI の command が timeout して stick を失い、/sbin/init が
    動けない事が gdbstub の klog_buffer で見つかった（[BUG-359](../../bugs/BUG-359.md)）。同じ症状なら stick と disk を `cache=unsafe` にすると避けられる。p014u・p014v が
    これだったかは確かめていない。
  - QEMU（compositor）`plan/ws309/tests/spawn-guest.sh build/ws309/p014z … build/amd64/dynamic/libc.so` PASS: `KWL DESKTOP start pid=122 …`、program は
    3〜20 の fd を何も持たない（`closed-ok`）、`KWL THREADS … io-file …`、KWL FAILED 無し。
- 注意: この後の compositor は新しい libc の `posix_spawn_file_actions_addclosefrom_np` を要る。古い image に /bin/wayland だけを入れる試験は
  `ld.so: undefined symbol` で起動しない（libc.so も一緒に入れるか image を作り直す）。
- 未実施: input method の起動（試験の image に keiland-ime が無い）、App Home・Notes・Settings の起動の QEMU、FreeBSD の build（`POSIX_SPAWN_SETSID` は #ifdef）、実機。
