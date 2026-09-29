<!-- awesome-plan project=zedbsd record=ws073-p035 -->

# ws073-p035: unix socket の blocking の connect が backlog の空きを待つ（BUG-108）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-108](../../bugs/BUG-108.md)
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29、BUG-039・031 より先に）。Queue の ID は main が記録する

## 範囲と受け入れ

- unix の stream の blocking の connect は listener の backlog の空きを待つ（nonblocking は EAGAIN のまま）。backlog の上限の見直し。
  同時の wltest の「EMFILE（errno 24）」の原因の特定。
- 受け入れ: ws073-p034 の `socket-desktop.sh` を間隔なし（`STAGGER=0`）で 40 ＋ 4 の client が全て描画する。

## 設計

`src/kern/net/unix-socket.c`:
- `unix_connect_resolved` の backlog の検査を loop にし、満杯なら blocking の connect は listener の `connect_waitq`（unix の listener では他に使われない）で
  眠る。起きるたびに listening・credential・空きを検査し直す。nonblocking（`SOCKET_IO_NONBLOCK`）か thread の無い呼び出しは EAGAIN、signal は EINTR、
  listener が閉じられたら ECONNREFUSED。
- `unix_accept` は `pending_count` を減らした所で、listener の close は `listening = 0` の所で `connect_waitq` を wake_all する（同じ lock の中）。
- backlog の clamp を 16 から `UNIX_LISTEN_BACKLOG_MAX` 128 に（BSD・Linux の SOMAXCONN、libwayland-server が listen に渡す値）。
- EINTR の connect は接続を作らずに終わる（POSIX は非同期に続けることも許す。簡単な方を選んだ）。

「EMFILE（errno 24）」の原因: **zedBSD の errno の番号では 24 は EAGAIN**（`include/uapi/errno.h`: EAGAIN 24、EMFILE 46、ENOENT 6）。p034 の報告で
Linux の番号として読み違えた。wltest の 4 本の失敗も wlshm と同じ「backlog の満杯で connect が EAGAIN」で、別の原因は無い
（確認: guest で存在しない display に wltest を繋ぐと `errno=6`（ENOENT）、probe の `connect` も 6 で "No such file or directory"）。

## 手順と結果

変えた file: `src/kern/net/unix-socket.c`。試験: [tests/unix-backlog.c](../tests/unix-backlog.c)（新規）、[tests/socket-desktop.sh](../tests/socket-desktop.sh)
（wltest も `STAGGER` に従う）。

- style: style-diff findings 0、`git diff --check` PASS。
- build: `build-image-noclang.sh build/amd64`（main 90af6b0c を merge した tree）`check-amd64-native-image: OK`、unix-socket.c の warning 0、Permission denied 0。
- QEMU（Venus の guest、runtime `build/ws073-venus`）:
  - `STAGGER=0 sh plan/ws073/tests/socket-desktop.sh build/amd64/hdd-image.img 40`（wlshm 40 と wltest 4 を間隔なしで一斉に）: 3 回とも
    **wlshm 40/40・wltest 4/4 描画、`SOCKET-DESKTOP:PASS`**（`build/ws073-p035/desktop-stagger0-{1,2,3}/desktop.png`）。修正前（p034 の kernel）は
    wlshm 17/40（`setup errno=5`）・wltest 0/4（errno 24 = EAGAIN）。
  - `unix-backlog`（listen 1 と 1000）: nonblocking の connect は満杯で EAGAIN、blocking の connect は待ち、accept で完了、listener の close で ECONNREFUSED、
    signal で EINTR、backlog 128 を accept なしで保持し 129 本目は EAGAIN → `UNIX-BACKLOG:PASS`（`build/ws073-p035/unix-backlog.txt`）。
    （Linux の host では同じ probe が合わない: Linux は listen 1 で 2 本を通し、閉じた listener を待つ connect を起こさない。probe は zedBSD の振る舞いの試験）
  - 回帰: `ssh-parallel.sh 10 10` 失敗 0。boot test PASS（`build/ws073-p035/boot-test/login.png`）。
- 未実施: 実機、i386・arm64 の image。

## Resume point

完了。次は BUG-039・BUG-031 の確認（p036 から）。
