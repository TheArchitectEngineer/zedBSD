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
| p002（案） | 正しさ: sequence の剰余の比較（wrap の後に ACK を全部捨てる）、SND.WL1/WL2、重なりのある segment、諦めの時間 | planning（p001 の設計の review の後） |
| p003（案） | 送りの packet の不足: device が拒んだ data の segment で書き手が 10 ms 眠らない、ACK の出し直し | planning |
| p004（案） | RTT の推定と RTO（RFC 6298） | planning |
| p005（案） | fast retransmit と NewReno の回復、persist timer、窓が開いた時の再送を外す | planning |
| p006（案） | 輻輳の制御（cwnd）と byte の送りの buffer、相手の MSS、Nagle | planning |
| p007（案） | 受けの窓の約束、受け側の SWS 回避、window scale、delayed ACK | planning |
| p008（案） | 受けの SACK（RFC 2018） | planning（候補） |
| p009（案） | 規約の全文の見直し（WS の全ての C、試験を含む） | planning |

試験と道具（[tests/](tests/)）: `tcp-model/run.sh`（本物の tcp.c の host の模型と WS220 の目標の scenario、数秒）、`tcp-stream-series.sh`（QEMU と実機の長い転送の時系列、T1）、`stream.py`・`analyze.py`、`config-amd64-tcp.mk`（SSH の guest + sysctl）。WS の完了の時に回帰に残す物は master.md の Tools に移す。
