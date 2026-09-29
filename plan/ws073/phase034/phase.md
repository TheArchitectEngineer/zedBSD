<!-- awesome-plan project=zedbsd record=ws073-p034 -->

# ws073-p034: system 全体の socket の上限 32 を外す（BUG-107）

Status: in-progress（2026-09-29）
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

（記入中）

## Resume point

amd64 の image（`build/ws073-p034/build.log`）→ `tests/socket-many.c`（unix の socketpair 200・TCP 60 接続・raw ICMP の 33 本目が ENFILE）→ SSH の並列 6・10 本 → デスクトップの guest。
