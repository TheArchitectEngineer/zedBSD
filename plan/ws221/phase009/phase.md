<!-- awesome-plan project=zedbsd record=ws221-p009 -->
# ws221-p009: TCP の受けの窓（窓の約束、受け側の SWS 回避、window scale、delayed ACK）（旧 ws220-p007）

Status: planning（2026-10-28 WS220 p007 から移した。設計は WS220 p001 §4 の案のまま、design-reviewer は未）
Parent: [WS221](../ws.md)
Queue: なし
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

## 結果

まだ無い。
