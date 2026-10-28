<!-- awesome-plan project=zedbsd record=ws221-p009 -->
# ws221-p009: TCP の受けの窓（窓の約束、受け側の SWS 回避、window scale、delayed ACK）（旧 ws220-p007）

Status: in-progress（2026-10-29 P3 q1664-i03: 自分の window scale と byte で数える窓は 13a032348 で済み（下の「i03 の結果」）。残り: 受け側の SWS 回避の見直し、delayed ACK の timer（今は 2 つ目か worker の batch の終わり）、slow-reader の receive_refused 0）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i01（P3）
依存: p008（輻輳の制御と byte の送りの buffer。受けの queue の上限は packet の pool の class を考慮）

## 由来

2026-10-28 ユーザーの回答（クリック）「WS221 へ移す (推し)」: WS220（completed）の未着手の p006（輻輳の制御と送りの buffer、`tcp_device_mss`）・p007（受けの窓）を WS221 の Phase に移した（P4、q1660）。 WS220 の設計（[ws220/ws.md](../../ws220/ws.md)、p001 §4 の表は git の commit `fb1f6afee` の `plan/ws220/phase001/phase.md`）から写した。

## WS220 の設計（p001 §4 の行、写し）

| 段（Phase の案） | 直すこと | 理由（候補） | 先に確かめる模型の scenario |
| --- | --- | --- | --- |
| p007 受けの窓 | (a) 窓の約束を守る: 受信 queue を「packet の数」でなく byte で数える（`socket_enqueue_stream` が tail の残りを埋めた後の残りを次の packet にする、または窓 = 空き packet × 実際に受けた segment の大きさ）。(b) 受け側の SWS 回避（RFC 9293 3.8.6.2.2）と窓の更新を「min(MSS, buffer/2) 開いたら」に。(c) RFC 7323 の window scale（受けの buffer を 64 KB より大きくする時。pool 256 × 2 KB の内で、受けの buffer の上限を決める）。(d) delayed ACK（2 segment に 1 つ、最大 40 ms）と受けの batch の終わりで 1 つの ACK | 5・6・7 | slow-reader の `receive_refused` 0、受けの窓の scenario を足す（相手 = 模型の A に 1448 byte の segment） |

## WS220 の記録（2026-10-13、写し）

- 2026-10-13 Q1（WS221 p001 の依頼）: p006・p007 の設計に「MSS・segment の大きさは `tcp_device_mss(device)`（interface の MTU − 40/−60）から決め、定数にしない」「受けの queue の上限は packet の pool の class（標準 2 KB × 256、jumbo 9216 × 32）を考慮」を入れる。WS221 p005（TCP の MSS）は WS220 p006・p007 の後。
- 2026-10-13 P3（ws220-p005 review-1 M7・M8、ユーザーの決定「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」）: p005 は窓 0 の相手を FreeBSD と同じ persist（間隔 clamp(RTO × backoff, 5 s, 60 s)、答えない相手は 12 段と min(2 h, RTO × 2559) の沈黙で、close の後は 60 s の沈黙で落とす）と `TCP_MAXUNACKTIME`（既定 0）で扱う。答え続ける相手が共有の packet の pool を接続ごとに最大 8 個握れる問題は **p006(b)（送りの buffer を pool から切り離す）で解く**。pool の逼迫で落とす・`TCP_MAXUNACKTIME` の既定を 0 以外にする、は入れない。cwnd の無い NewReno（損失の後も送る量を減らさない）は送りの ring が 8 segment の間だけ許し、**ring の既定（`CONFIG_TCP_SEND_QUEUE_MAX`）を広げるのは p006(a)（cwnd）の後に限る**。p001 §4 の目標の移し替え（Q1 の判断）: rtt-10ms-loss-0.1 の 2 MB/s は p006 へ（ring の上限 0.78 MB/s）、slow-reader の `immediate_resends` 0 は p007(a) へ。
- 症状は受けの向き（ユーザー「遅くなった時の向きは受信です」）。WS220 p001 §4 の目標の移し替え（Q1）: slow-reader の `immediate_resends` 0 はこの Phase（旧 p007(a)）へ。

## WS220 のユーザーの決定（原文、写し）

- ユーザー（原文）:「はい、推奨でいいです。」→ QEMU の受け入れは usb-net（CDC ECM）のまま（virtio-net は足さない、完了の条件 3 の「virtio-net」は usb-net に読み替え）。RTO の最小は 200 ms（Linux・FreeBSD と同じ、RFC 6298 の SHOULD の 1 s から外れることを phase.md に記録）。
- 2026-10-13 ユーザー（原文）:「遅くなった時の向きは受信です」「接続を諦めるまでの時間は延ばしてよいです。」→ 症状は受けの向き（zedBSD が受ける）。基線の測りと直しの順は受けの側（候補 5・6・7、p007）を重く見る。SYN の再送を諦めるまでの時間は RFC 1122 の R2 に合わせて最低 3 分に延ばす（利用者に見える変更として承認済み）。
- 2026-10-13 ユーザー:「TCPですが、60秒以上の転送も計測してください。そうでないとTCP Windowの実装の問題が見えないです。」→ WS220 の T1 の測りは全部 60 秒を超える転送を含める（損失 0 の 1 GiB と時系列の前半・後半の比較）。

## 試験の道具

- `plan/ws220/tests/`（WS220 の完了の後もその場に残す）: `tcp-model/run.sh`（本物の tcp.c の host の模型、scenario を名前で選ぶ）、`tcp-stream-series.sh`（QEMU と実機の長い転送の時系列）、`stream.py`・`analyze.py`、`config-amd64-tcp.mk`・`config-amd64-tcp-wrap.mk`。`userland/tests/tcp-stream`（塞がる送り手）。
- 既存の網の回帰: `plan/ws033/tests/tcp-receive-speed.sh`・`tcp-loss-speed.sh`・`lan-hotplug.sh`。

## 結果（2026-10-29 P3 q1664）

実装と数値は [ws.md](../ws.md) の「結果」の表。試験: WS220 の `tcp-model/run.sh` PASS（scenario を新しい速さに追従させた: model の writer の room に cwnd、batch の終わりの `tcp_ack_flush`、`--expect-retransmits-losses`、persist 系の bytes 64 MB、rtt-step の段を 2 ms、unresolved の上限を flight 分、window-lost の FR 0:1）、`plan/ws221/tests/qemu-tcp.sh` PASS、VF2 実機の `plan/ws221/tests/vf2-tcp.sh`。

## i03 の結果（2026-10-29 P3 q1664-i03、13a032348）

Q1（2026-10-29）: 「p008・p009 の残り（自分の window scale の広告、byte の受けの窓）は WS311 の前提にもなるので、WS221 の中で済ませておいて。」

| 項目 | 内容 |
| --- | --- |
| byte で数える窓 (a) | `socket_enqueue_stream` を 1 つの lock の下に書き直した: 全部が tail に入れば今まで通り、入らなければ tail が `SOCKET_STREAM_PACKET_BYTES`（1024）より少ない時だけ tail を先頭の bytes で満たし、残りを自分の packet に（全部を取れる時だけ取る）。queue の packet は最後のもの以外 1024 byte 以上を持つので、窓の packet の上限を「空き packet × 1024」にした（前は × 1460 で 512 byte 未満の segment で約束が破れた）。全長の segment（1024 より大きい）は写さない |
| 自分の window scale (c) | SYN の option の shift を `TCP_RECEIVE_WSCALE` 5（`SOCKET_BUFFER_MAX` 1 MB を覆う最小）に。両端が option を出した接続は窓の field を 5 bit 右へ（SYN では scale しない、32 byte に切り捨て）、`advertised_window` は byte の値（uint32）で持つ |
| 受けの buffer の既定 | 64-bit（pool 1024）の TCP の接続は 128 KB（`TCP_RECEIVE_BUFFER`、128 packet × 1024 と同じ）。32-bit は 64 KB のまま。`SO_RCVBUF` で変えられる |
| 試験 | tcp-model PASS: model の `--peer-window` を A の読む scale で書く形に直し、窓が 128 KB になって flight が 128 segment になった 3 つの上限（unresolved 76→140、unresolved-giveup 84→148、rtt-step-zedbsd 64→128）を追従させた。rtt-10ms は 6.1→12.2 MB/s（窓 / RTT）、lan-loss-1-zedbsd 14.1→12.0、no-route 58.3→50.9（flight が大きい分の損失）。qemu-tcp.sh PASS（53.0・56.6 Mbps）。VF2 実機（相手 5330 の Debian、Q1 の許可、flock）: 1500 で受け 949.3・949.3、送り 949.1・949.1（受けの窓 131072）、9000 で受け 990.6・990.8（前の 987 から上がった）、送り 990.9・990.9（受けの窓 71680 = jumbo 8 × 8960）、両方 receive_refused 0・再送 0 |
