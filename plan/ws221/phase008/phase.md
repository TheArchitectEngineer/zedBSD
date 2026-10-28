<!-- awesome-plan project=zedbsd record=ws221-p008 -->
# ws221-p008: TCP の輻輳の制御と送りの buffer、相手の MSS、`tcp_device_mss`、Nagle（旧 ws220-p006）

Status: in-progress（2026-10-29 P3 q1664-i04: 案 B の (1)〜(3) を a774d28e3 に実装、tcp-model・packet-buf の host 試験 PASS、VF2 の kernel の build warning 0。残り: IPv6 の Packet Too Big の試験（QEMU か実機、未実施）、VF2 の実機の非退行、5 platform の build）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i01（P3）
依存: WS220 p005（main、cleared）。WS221 p005 はこの Phase の `tcp_device_mss` を使う

## 由来

2026-10-28 ユーザーの回答（クリック）「WS221 へ移す (推し)」: WS220（completed）の未着手の p006（輻輳の制御と送りの buffer、`tcp_device_mss`）・p007（受けの窓）を WS221 の Phase に移した（P4、q1660）。 WS220 の設計（[ws220/ws.md](../../ws220/ws.md)、p001 §4 の表は git の commit `fb1f6afee` の `plan/ws220/phase001/phase.md`）から写した。

## WS220 の設計（p001 §4 の行、写し）

| 段（Phase の案） | 直すこと | 理由（候補） | 先に確かめる模型の scenario |
| --- | --- | --- | --- |
| p006 輻輳の制御と送りの buffer | (a) RFC 5681 の cwnd・ssthresh（slow start、congestion avoidance、IW は RFC 6928 の 10 segment）。(b) 送りの ring（8 個の packet の写し）を byte の送りの buffer に替える（`SO_SNDBUF`、既定 256 KB 程度、copy は 1 回の write で buffer の空きまで）、segment は送る時に buffer から切る、大きさは min(相手の MSS, device の MTU − 40)（SYN の MSS option を読む）。再送は buffer から作るので pool の packet を持たない。(c) Nagle（`TCP_NODELAY` で切る） | 4・1（ring を広げると device の queue を溢れさせる: cwnd が要る） | rtt-10ms（4、64 KB の窓で 6.4 MB/s）、ring を広げても queue 9 で lan-clean ≥ 30 |

## WS220 の記録（2026-10-13、写し）: `tcp_device_mss` の要件と所有

- 2026-10-13 Q1（WS221 p001 の依頼）: p006・p007 の設計に「MSS・segment の大きさは `tcp_device_mss(device)`（interface の MTU − 40/−60）から決め、定数にしない」「受けの queue の上限は packet の pool の class（標準 2 KB × 256、jumbo 9216 × 32）を考慮」を入れる。WS221 p005（TCP の MSS）は WS220 p006・p007 の後。
- 2026-10-13 Q1（WS221 review-1 S3）: MSS を MTU − 40/−60 で決める時は packet の slot の大きさでも抑える（lo0 の MTU 65535 で segment が slot を超え、自分の機械への TCP が止まる）。p006 の設計にこれを入れる。tcp_device_mss の持ち主は WS220 p006（WS221 p005 はそれを使う、review M7）。
- 2026-10-13 Q1（WS221 第 2 版 §3.4 の要件、p006 に写す）: (1) `tcp_device_mss` は slot の上限で抑える（暫定 `PACKET_BUF_STORAGE_SIZE − 64`、WS221 p002 の `net_device_frame_max(device)` で置き換える）、(2) IPv6 は送る時の path MTU − 60（`ipv6_route_source`）、(3) IPv4 の gateway 経由（`route.gateway != 0`）は min(1460, MTU − 40)、on-link だけ device の MTU。`tcp_stats_header` の 48 → 72 byte の拡張（WS221 M6）は WS221 p002 が行う（WS220 p002 は main に入った後なので順は問題なし、読み手は struct_size で読む）。
- 2026-10-13 P3（ws220-p005 review-1 M7・M8、ユーザーの決定「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」）: p005 は窓 0 の相手を FreeBSD と同じ persist（間隔 clamp(RTO × backoff, 5 s, 60 s)、答えない相手は 12 段と min(2 h, RTO × 2559) の沈黙で、close の後は 60 s の沈黙で落とす）と `TCP_MAXUNACKTIME`（既定 0）で扱う。答え続ける相手が共有の packet の pool を接続ごとに最大 8 個握れる問題は **p006(b)（送りの buffer を pool から切り離す）で解く**。pool の逼迫で落とす・`TCP_MAXUNACKTIME` の既定を 0 以外にする、は入れない。cwnd の無い NewReno（損失の後も送る量を減らさない）は送りの ring が 8 segment の間だけ許し、**ring の既定（`CONFIG_TCP_SEND_QUEUE_MAX`）を広げるのは p006(a)（cwnd）の後に限る**。p001 §4 の目標の移し替え（Q1 の判断）: rtt-10ms-loss-0.1 の 2 MB/s は p006 へ（ring の上限 0.78 MB/s）、slow-reader の `immediate_resends` 0 は p007(a) へ。

## WS220 のユーザーの決定（原文、写し）

- ユーザー（原文）:「はい、推奨でいいです。」→ QEMU の受け入れは usb-net（CDC ECM）のまま（virtio-net は足さない、完了の条件 3 の「virtio-net」は usb-net に読み替え）。RTO の最小は 200 ms（Linux・FreeBSD と同じ、RFC 6298 の SHOULD の 1 s から外れることを phase.md に記録）。
- 2026-10-13 ユーザー（原文）:「遅くなった時の向きは受信です」「接続を諦めるまでの時間は延ばしてよいです。」→ 症状は受けの向き（zedBSD が受ける）。基線の測りと直しの順は受けの側（候補 5・6・7、p007）を重く見る。SYN の再送を諦めるまでの時間は RFC 1122 の R2 に合わせて最低 3 分に延ばす（利用者に見える変更として承認済み）。
- 2026-10-13 ユーザー:「TCPですが、60秒以上の転送も計測してください。そうでないとTCP Windowの実装の問題が見えないです。」→ WS220 の T1 の測りは全部 60 秒を超える転送を含める（損失 0 の 1 GiB と時系列の前半・後半の比較）。

## 試験の道具

- `plan/ws220/tests/`（WS220 の完了の後もその場に残す）: `tcp-model/run.sh`（本物の tcp.c の host の模型、scenario を名前で選ぶ）、`tcp-stream-series.sh`（QEMU と実機の長い転送の時系列）、`stream.py`・`analyze.py`、`config-amd64-tcp.mk`・`config-amd64-tcp-wrap.mk`。`userland/tests/tcp-stream`（塞がる送り手）。
- 既存の網の回帰: `plan/ws033/tests/tcp-receive-speed.sh`・`tcp-loss-speed.sh`・`lan-hotplug.sh`。

## 結果（2026-10-29 P3 q1664）

実装と数値は [ws.md](../ws.md) の「結果」の表。試験: WS220 の `tcp-model/run.sh` PASS（scenario を新しい速さに追従させた: model の writer の room に cwnd、batch の終わりの `tcp_ack_flush`、`--expect-retransmits-losses`、persist 系の bytes 64 MB、rtt-step の段を 2 ms、unresolved の上限を flight 分、window-lost の FR 0:1）、`plan/ws221/tests/qemu-tcp.sh` PASS、VF2 実機の `plan/ws221/tests/vf2-tcp.sh`。

## i04: byte の送りの buffer の扱い（案 B、2026-10-29）

- P3 の調べ: 送りは ring（64-bit で 256 entry、各 entry が共有の pool の packet を 1 つ持つ）で、entry ごとの packet に unsent・send_flushing・output retry・probe・RTT の計時・NewReno・初回の送信の zero-copy（e94e0040f・09e15688b）が結び付く。byte の buffer への全面の置き換えは tcp.c の送りの側の書き直しで、送るたびに copy が 1 回増える（VF2 の送り 949 を落とす恐れ）。今の実害は「pool 1024 に対し接続ごとに 256 を握れ、遅い相手への送り 4 本で driver の受けが飢える」。
- WS220 のユーザーの決定（原文、[ws220/ws.md](../../ws220/ws.md)）:「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」
- Q1 の判断（2026-10-29、原文の要点）:「案 B で進めて」「決定の目的は『答え続ける相手が pool を握らない』で、案 B の (1)（送りの copy は受けの予約を取らない＋接続ごとの送りを SO_SNDBUF の byte で抑える）で満たせる。byte の buffer そのものは Future Work（WS311 の後に再評価）」。(1) は「遅い相手への送り 4 本で driver の受けが飢えない」ことを tcp-model か QEMU で確かめる。
- **Future Work（WS311 の後に再評価）**: byte の送りの buffer（kmem、SO_SNDBUF 既定 256 KB、segment は送る時に切る、再送は buffer から）。案 B で pool の問題は解いたので、利点は再送の時に任意の境界で切れること（SACK・RACK の前提）と、copy の回数の比較で決める。

| 項目 | 内容（a774d28e3） |
| --- | --- |
| (1) pool の切り離し | `PACKET_BUF_ALLOC_HOLD`: 送りの copy（data の segment）は標準の packet の最後の `PACKET_BUF_HOLD_RESERVE`（64-bit 256、32-bit 8）を取らない（jumbo の受けの予約と同じ形、拒否は `alloc_failures` に数える）。SYN・FIN の copy、ACK、再送、datagram、driver の受けは取れる。接続ごとの送りの payload を `send_bytes` で数え、`SO_SNDBUF`（TCP の既定は 64-bit で 384 KB、ring の 256 × 1460 より大きいので既定では ring が上限、32-bit は 64 KB）で抑える。書き手を起こすのは ring の半分と送りの buffer の半分が空いてから |
| (2) Nagle | RFC 9293 3.7.4 に Minshall の変更（Linux と同じ）: 末尾の未送の短い data の entry は、先に送った短い segment がまだ ACK されていない間だけ持つ（full の segment の flight は何も止めない、転送の最後の短い片はすぐ出る）。短い末尾（`fresh`: device に一度も渡していない）は次の write の bytes を取って MSS まで伸びる（copy の tailroom の内）。`TCP_NODELAY` で切る（設定した時に持っている物はすぐ送る）。短い segment の ACK で output retry が送る |
| (3) IPv6 の path MTU | `icmp6_too_big` が `tcp6_path_mtu_lowered` を呼ぶ: その相手への接続の `send_mss` を path MTU − 60（最小 1220）に下げ、送った entry のうち大きい物の最初から後ろを unsent に戻して output retry で切って送り直す（Linux の simple retransmit と同じ、cwnd は下げない）。`tcp_send_segment_cut`: entry が今の MSS より大きければ複数の segment に切る（FIN・PSH は最後だけ）。timer の再送は ACK された所の後ろから。write の途中で下がって EMSGSIZE で取り戻した segment は新しい大きさで送り直す |

試験（i04）:
- `plan/ws220/tests/tcp-model/run.sh` PASS（全 scenario）。足した scenario: `pool-held`（他の送り手が予約の外を全部握り A に 64 を残す: receive_no_packet 0 で完走 6.3 MB/s。予約を 0 にした build では B の受けが 23 frame 落ち接続が死ぬ = FAIL を確かめた）、`send-buffer-16k`（ring の payload の最大 16384 = SO_SNDBUF、74 MB/s）、`nagle-1024`（1024 byte の write が 11532 segment = 1460 で詰まる）、`nodelay-1024`（16311 segment）。直した scenario: `two-writers` を `--write-bytes 1460`（短い write が次の write と結ばれるので、model の 2 人目の書き手が 1 人目の bytes の間に入る）。model の書き手の「packet の不足」の判定に送りの buffer を足した。1024 byte の write の scenario は 1460 の segment になって速くなった（lan-clean 110.6 → 113.0 MB/s）
- `plan/ws221/tests/run-packet-buf-host-test.sh` PASS（`test_hold_reserve` を足した: 保持は予約の外まで、予約は受けと通過の packet が取れる）
- VF2 の kernel（`config-vf2-tcp.mk`）rc=0・warning 0
- 未実施: IPv6 の Packet Too Big の試験（model は IPv4 だけ）、VF2 の実機の非退行、5 platform の build
