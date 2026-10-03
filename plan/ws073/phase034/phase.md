<!-- awesome-plan project=zedbsd record=ws073-p034 -->

# ws073-p034: system 全体の socket の上限 32 を外す（BUG-107）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-107](../../bugs/BUG-107.md)
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29、BUG-039・031 より先に。デスクトップの unix socket も多くデモの安定に効く）。Queue の ID は main が記録する

## 範囲と受け入れ

- `SOCKET_MAX 32U`（`include/kern/net/socket.h`）の固定の上限を外す。設計の選択をここに記録する。
- 確認: SSH の並列 6〜10 本が失敗しない。デスクトップの Venus の guest で client を多数開く。多数の socket を開く probe。

## 設計

socket 自身は既に 1 つずつ `kern_calloc` で割り当てられ、`SOCKET_MAX` は `socket_create` の数の上限（`socket_count`）に過ぎない。
ただし 4 か所が `SOCKET_MAX` の大きさの snapshot の配列を kernel の stack に置き、上限が全 socket の数を保証することに頼っていた:
`tcp_timer_run`（全 TCP socket の再送・keepalive）、`icmp_deliver`（raw ICMP）、`packet_socket_deliver`（AF_PACKET）、`route_socket_notify`（AF_ROUTE）。
上限を上げるだけでは stack の配列が大きくなり（1024 × 8 byte、kernel の stack は 16 KiB、`-Wframe-larger-than=8192`）、減らせば取りこぼす。

選んだ形:
1. `SOCKET_MAX` を 1024 に（暴走した program への guard。socket は file でもあり file の pool 2048 が大きい方に残る）。完全な無制限にはしない:
   各 socket は受信・送信の buffer を持ちうるので、system 全体の上限は残す方が安全。
2. broadcast の family（raw ICMP・packet・route）は新しい `SOCKET_BROADCAST_MAX 32` を family ごとの上限にし、作成の時に registry を数えて
   満ちていれば ENFILE で断る。snapshot は 32 のまま stack に置け、取りこぼしが起きない（以前は全体の上限 32 がそれを保証していた）。
   これらは特権の raw socket か少数の listener（networkd 等）で、32 は足りる。
3. TCP の timer は socket の数を数え、32 を超えたら `kern_calloc` で数に合わせた snapshot を割り当てる（worker の thread の文脈）。
   割り当てに失敗したら stack の 32 で最初の socket を扱い、残りは次の pass（deadline の検査は `<= now` なので遅れるだけ）。
   数えた後に作られた socket も次の pass。
- 代案（採らない）: 固定の配列の pool（socket の実体は既に動的）、全 family の per-endpoint の link で snapshot（icmp・packet の配達は並行しうる）。

## 手順と結果

変えた file: `include/kern/net/socket.h`（`SOCKET_MAX` 1024、`SOCKET_BROADCAST_MAX` 32）、`src/kern/net/icmp.c`・`packet-socket.c`・`route-socket.c`
（作成の時に registry を数えて 32 で ENFILE、snapshot は `SOCKET_BROADCAST_MAX`）、`src/kern/net/tcp.c`（timer の snapshot を数に合わせて割り当て、
`TCP_TIMER_STACK_SNAPSHOT` 32 を fallback）。試験: [tests/socket-many.c](../tests/socket-many.c)、[tests/socket-desktop.sh](../tests/socket-desktop.sh)、
[tests/ssh-parallel.sh](../tests/ssh-parallel.sh)、[tests/resources.c](../tests/resources.c)（p030 の probe、`KERN_SYSTEM_GET_RESOURCES`）。

- style: 変えた 5 file の style-diff findings 0、`git diff --check` PASS。
- build: `build-image-noclang.sh build/amd64`（main 59a5b6e6 を merge した tree）`check-amd64-native-image: OK`、変えた net の file の warning 0。
  i386 の target で変えた 5 file を kernel の flag で compile（`-Werror`）PASS（i386 の image の build・起動は未実施）。
- boot test（QEMU）PASS（`build/ws073-p034/boot-test/login.png`）。
- デスクトップの Venus の guest（`plan/ws035/tests/zdesktop-guest.sh`、runtime `build/ws073-venus`、renderer は main の `build/ws035-sq-venus/install`）、
  `sh plan/ws073/tests/socket-desktop.sh IMAGE 40`（zdesktop ＋ wl_shm の窓 40 ＋ EGL の窓 4、0.3 s おき）:
  - 修正前（main の `build/ws035-sq/hdd-image.img`、09-29 05:04 の kernel）: 窓は約 10 枚で止まり、以後 SSH も繋がらない（sshd が socket を作れない）→ FAIL
    （`build/ws073-p034/desktop-before/desktop.png`）。
  - 修正後: socket 100（上限 32 の 3 倍）、**wlshm 40/40・wltest 4/4 が描画、FAIL 0**、`SOCKET-DESKTOP:PASS`（`build/ws073-p034/desktop-after/desktop.png`）。
- 同じ guest で（デスクトップの 44 client を開いたまま）:
  - `socket-many 200 60`: unix の socketpair 200・TCP の接続 60（＋listener）で 1 byte の往復 0 失敗、raw ICMP は 32 本開き 33 本目が ENFILE、
    閉じた後に再び開ける → `SOCKET-MANY:PASS`（その時の socket 約 620）。
  - `ssh-parallel.sh 6 15`・`ssh-parallel.sh 10 15`（1 s の sleep を持つ session を 6・10 本並列）: 失敗 0、messages の ENFILE・penalty 0、落ち 0、`SSH-PARALLEL:PASS`。
    修正前は 6 本並列で `socketpair: Too many open files in system` と OpenSSH の penalty（ws073-p030 の観測）。
- 未実施: 実機、i386・arm64 の image の起動、TCP の socket が 32 を超える時の timer の再送の遅れの計測（割り当てが失敗する経路は試していない）。

### 別の発見（範囲外、未修正、main に起票を依頼）

client を一斉に（0.3 s の間隔なしで）40 本起こすと、compositor の listen の backlog（`src/kern/net/unix-socket.c` で 16 に clamp）が満ち、
unix の `connect` は待たずに EAGAIN を返す（wlshm は `setup errno=5`）。同時に wltest（EGL）の 4 本が `errno=24`（EMFILE）で窓を開けなかった
（単独では開ける。原因は未調査）。Linux の AF_UNIX の blocking の connect は backlog の空きを待つ。デスクトップの起動の直後に多数の app を開く時に効く。

## Resume point

完了。次は BUG-039・BUG-031 の確認（p035 から）。
