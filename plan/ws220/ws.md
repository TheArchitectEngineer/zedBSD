<!-- awesome-plan project=zedbsd record=ws220 -->

# WS220: TCP の転送の速さの安定（継続の転送ですぐ 1 MB/s 未満に落ちる、window の制御の直し）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-13 Q1 作成。P1 が p001 を実行: 読み・net.tcpstats・host の模型・直しの設計は済み、T1 の基線の測りと design-reviewer 待ち）
Resume point: [p001](phase001/phase.md) の §6（T1 の依頼 A・B）と §8（ユーザーの判断: QEMU の網、RTO の最小）→ 基線の後に目標の値をここに書き、p002 以後を Queue に
Primary Milestone: MG005
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「WSを作ります。TCPのコントロールが不安定です。ウィンドウ制御がよくないと思われ、継続した転送で、TCPの速度がすぐに1MB/s未満になります。これを解決します。」

## 目的と完了の条件

対象は `src/kern/net/tcp.c`・`inet-socket.c`・`socket.c` など、TCP の送り・受けの制御（受けの window の広告と更新、window scale、送りの輻輳の window と slow start・congestion avoidance、再送の timer（RTO・RTT の推定）、fast retransmit・recovery、delayed ACK、Nagle、socket の buffer の大きさ）。

1. 原因を測って特定する: 継続の転送（例: 数百 MB を 1 本の TCP で、zedBSD が送り・受けの両方）で、時間ごとの速さ・cwnd・受けの window・再送・重複 ACK・RTO を記録する道具（kernel の counter か trace、host の tcpdump/pcap）を作り、落ちる仕組みを記録する。
2. 直す: 継続の転送で速さが落ちず、有線（Gigabit の Ethernet）で回線か相手の上限に近い速さを保つ。損失があっても回復して戻る。
3. 受け入れ: QEMU（virtio-net、host との間）と実機（5330・Pi 4 の GENET の有線）で、送り・受けの両向きに 1 GB の継続の転送の速さの時系列が、落ち込みから戻り、平均が目標以上（目標の値は p001 の測りの後に決める）。損失を入れた試験（QEMU の netem などで 0.1〜1 %）でも止まらず回復する。
4. 既存の網の試験の回帰が無い。build の warning 0、規約の全文の見直し。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調べと測り: 今の TCP の制御の読み、測りの道具（net.tcpstats、tcp-stream-series.sh、本物の tcp.c の host の模型）、落ちる仕組みの特定、直しの設計 → design-reviewer | in-progress（T1 の基線待ち） |
| [p002](phase002/phase.md) | 正しさ: sequence の剰余の比較（wrap の後に ACK を全部捨てる）、SND.WL1/WL2、重なりのある segment、諦めの時間（R2: data 100 s・SYN 180 s）、RST の窓の検査と同期済みの SYN への ACK（RFC 5961）、S2（net.tcpstats の sequence は superuser だけ）、測りの道具の直し（review S1・S3・S8） | in-progress（2026-10-13 P4 c13a8413f、実装・模型・build 済み、T1 の QEMU 待ち） |
| [p003](phase003/phase.md) | 送りの packet の不足: device が拒んだ data の segment で書き手が 10 ms 眠らない、未送信を順に出す、ACK の出し直し | in-progress（2026-10-13 P3、実装・模型・build 済み、review-1 の重大 1・中 2 を直した、tcp-stream-series.sh の止まりも直した。T1 の QEMU 待ち。模型の queue 7・5・2・1 が 0.68〜0.10 → 37 MB/s、2 つの書き手の二重送信が消えた） |
| [p004](phase004/phase.md) | RTT の推定と RTO（RFC 6298）: SRTT・RTTVAR、Karn、RTO = SRTT + max(G, 4·RTTVAR, 200 ms)、SYN の 3 s | in-progress（2026-10-13 P3、実装・模型・build 済み、design review-1 の重大 2・中 5 を直した（backoff の指数を分け sample でだけ戻す）、T1 の QEMU 待ち。模型の lan-loss-0.1 の停止 1000 → 202 ms） |
| [p005](phase005/phase.md) | fast retransmit と NewReno の回復、窓 0 の probe と FreeBSD と同じ persist（`TCP_MAXUNACKTIME`）、窓が開いた時の再送を外す | in-progress（2026-10-13 P3、実装・模型・build 済み、design review-1 の重大 2・中 8 と M7 のユーザーの決定を入れた、T1 の QEMU 待ち。模型の lan-loss-0.1 4.63 → 36.54、lan-loss-1 0.46 → 12.16 MB/s） |
| p006（案） | 輻輳の制御（cwnd）と byte の送りの buffer、相手の MSS、Nagle | planning |
| p007（案） | 受けの窓の約束、受け側の SWS 回避、window scale、delayed ACK | planning |
| p008（案） | 受けの SACK（RFC 2018） | planning（候補） |
| p009（案） | 規約の全文の見直し（WS の全ての C、試験を含む） | planning |

試験と道具（[tests/](tests/)）: `tcp-model/run.sh`（本物の tcp.c の host の模型と WS220 の目標の scenario、`run.sh NAME...` で選ぶ、全部で約 5 分）、`tcp-stream-series.sh`（QEMU と実機の長い転送の時系列と host の capture、run は down・down-disk・up・up-ssh、T1）、`stream.py`・`analyze.py`（`analyze: wire` の行で 100 ms の停止）、`config-amd64-tcp.mk`（SSH の guest + sysctl + tcp-stream）、`config-amd64-tcp-wrap.mk`（ISN を wrap の 16 MiB 前に）、`userland/tests/tcp-stream`（塞がる送り手）。WS の完了の時に回帰に残す物は master.md の Tools に移す。

## ユーザーの決定（2026-10-13、p001 の後）

ユーザー（原文）:「はい、推奨でいいです。」→ QEMU の受け入れは usb-net（CDC ECM）のまま（virtio-net は足さない、完了の条件 3 の「virtio-net」は usb-net に読み替え）。RTO の最小は 200 ms（Linux・FreeBSD と同じ、RFC 6298 の SHOULD の 1 s から外れることを phase.md に記録）。

2026-10-13 Q1: review-1 の判定「致命的 0、重大 8、p002 は条件つきで開始可」。T1 の依頼 A（基線、T1-569 の再実行）は S1（塞がる送り手と disk へ書く down の run を足す）・S3（veth・bridge・tap の tso/gso/gro を切る）・S8（sample の細かさ、頭と尻の 0、4 GiB の wrap、目標の自己参照）を直してから。S2（net.tcpstats が superuser でなくても全接続の sequence・address・port を出す、main に入っている）は p002 と一緒に直す（superuser でなければ sequence の 3 つの欄を 0）。S6: ユーザーに伝えた「RTO の最小 200 ms は Linux・FreeBSD と同じ」は不正確（Linux は SRTT + max(4·RTTVAR, 200 ms) の形、review の記憶）→ 単純な 200 ms の床ではなく RFC 6298 の式に最小の項 200 ms を入れる形にし、ユーザーに訂正を報告する。依存: WS221（Jumbo frame）の MTU と MSS。p003 以後は S4〜S7 を設計に入れてから。

2026-10-13 ユーザー（原文）:「遅くなった時の向きは受信です」「接続を諦めるまでの時間は延ばしてよいです。」→ 症状は受けの向き（zedBSD が受ける）。基線の測りと直しの順は受けの側（候補 5・6・7、p007）を重く見る。SYN の再送を諦めるまでの時間は RFC 1122 の R2 に合わせて最低 3 分に延ばす（利用者に見える変更として承認済み）。

2026-10-13 Q1（WS221 p001 の依頼）: p006・p007 の設計に「MSS・segment の大きさは `tcp_device_mss(device)`（interface の MTU − 40/−60）から決め、定数にしない」「受けの queue の上限は packet の pool の class（標準 2 KB × 256、jumbo 9216 × 32）を考慮」を入れる。WS221 p005（TCP の MSS）は WS220 p006・p007 の後。

2026-10-13 Q1（WS221 review-1 S3）: MSS を MTU − 40/−60 で決める時は packet の slot の大きさでも抑える（lo0 の MTU 65535 で segment が slot を超え、自分の機械への TCP が止まる）。p006 の設計にこれを入れる。tcp_device_mss の持ち主は WS220 p006（WS221 p005 はそれを使う、review M7）。

2026-10-13 Q1（WS221 第 2 版 §3.4 の要件、p006 に写す）: (1) `tcp_device_mss` は slot の上限で抑える（暫定 `PACKET_BUF_STORAGE_SIZE − 64`、WS221 p002 の `net_device_frame_max(device)` で置き換える）、(2) IPv6 は送る時の path MTU − 60（`ipv6_route_source`）、(3) IPv4 の gateway 経由（`route.gateway != 0`）は min(1460, MTU − 40)、on-link だけ device の MTU。`tcp_stats_header` の 48 → 72 byte の拡張（WS221 M6）は WS221 p002 が行う（WS220 p002 は main に入った後なので順は問題なし、読み手は struct_size で読む）。

2026-10-13 P3（ws220-p005 review-1 M7・M8、ユーザーの決定「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」）: p005 は窓 0 の相手を FreeBSD と同じ persist（間隔 clamp(RTO × backoff, 5 s, 60 s)、答えない相手は 12 段と min(2 h, RTO × 2559) の沈黙で、close の後は 60 s の沈黙で落とす）と `TCP_MAXUNACKTIME`（既定 0）で扱う。答え続ける相手が共有の packet の pool を接続ごとに最大 8 個握れる問題は **p006(b)（送りの buffer を pool から切り離す）で解く**。pool の逼迫で落とす・`TCP_MAXUNACKTIME` の既定を 0 以外にする、は入れない。cwnd の無い NewReno（損失の後も送る量を減らさない）は送りの ring が 8 segment の間だけ許し、**ring の既定（`CONFIG_TCP_SEND_QUEUE_MAX`）を広げるのは p006(a)（cwnd）の後に限る**。p001 §4 の目標の移し替え（Q1 の判断）: rtt-10ms-loss-0.1 の 2 MB/s は p006 へ（ring の上限 0.78 MB/s）、slow-reader の `immediate_resends` 0 は p007(a) へ。
