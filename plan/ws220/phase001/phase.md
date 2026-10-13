<!-- awesome-plan project=zedbsd record=ws220-p001 -->
# ws220-p001: 調べと測り、直しの設計（TCP の継続の転送ですぐ 1 MB/s 未満に落ちる）

Status: in-progress（2026-10-13 P1: 読み・道具・host の模型・直しの設計は済み（commit 508d33db9 と本 file の commit）。残り: T1 の基線の測り（QEMU）と実機の測り、design-reviewer。cleared は Q1 の判定）
Parent: [WS220](../ws.md)

## ゴール

- 今の TCP の送り・受けの制御を全部読んで記録し、RFC と比べて誤り・欠落を挙げる。
- 「継続した転送で速さがすぐ 1 MB/s 未満に落ちる」を説明できる原因の候補を、file:行と確度つきで順位づける。
- 落ちる仕組みを測る道具（kernel の接続ごとの状態と counter の口、host の側の長い転送と時系列の script）を作る。
- 可能なら tcp.c の本物の code を host で動かす模型で落ち込みを再現し、原因を確かめる。
- 直しの設計（順、段、受け入れの目標の決め方、QEMU と実機の試験）を書く。

行番号は commit 508d33db9 の file（`src/kern/net/tcp.c` は net.tcpstats の counter を足した後）。

## 0. 前提の訂正: QEMU に virtio-net は無い

WS220 の完了の条件 3 は「QEMU（virtio-net、host との間）」と書くが、zedBSD に virtio-net の driver は無い（`src/drivers/` に virtio の
網の driver が無く、QEMU の試験の網は全部 QEMU の `usb-net`、つまり CDC ECM（`src/drivers/usb/usb-cdc-ecm.c`）: ue0 が slirp、ue1 が host の tap）。
ECM の driver は 1 frame に 1 本の USB の transfer で送り・受けし（BUG-222 の T1-516: 負荷の下で ACK が約 0.55 ms ごと）、QEMU の受けの速さは
約 6〜11 MB/s が driver の上限（T1-517: 損失 0 % で 8.4 MB/s、`tcp-receive-speed` で 9.8〜11.1 MB/s）。

**判断が要る（ユーザー）**: QEMU の受け入れを (a) usb-net（ECM、tap）のままにして目標の値を ECM の上限に合わせる、か (b) virtio-net の driver を
別の WS で足してから QEMU の受け入れをそちらで行う、か。本 Phase の道具は (a) で動く（`tcp-stream-series.sh` は usb-net の tap）。設計の
既定は (a)（TCP の直しは driver に依らず、ECM の上限より下の落ち込みが TCP の問題）。

## 1. 今の制御の全記録と RFC との比較

| 項目 | 今の実装（file:行） | RFC との比較 |
| --- | --- | --- |
| 送る segment の大きさ | 常に `TCP_MSS` = 1024（tcp.c:54）。`tcp_sendto` は 1 回の write で最大 1024 byte（tcp.c:2244）。相手の SYN の MSS option は読まない（SYN-ACK の処理 tcp.c:3380〜、`tcp_passive_syn` tcp.c:2997〜 とも option を解かない） | RFC 9293 3.7.1: 送る segment は相手の MSS 以下にする（1024 は 536 未満の相手では超える）。1460 の相手には segment が小さい（転送の効率が約 70 %） |
| 受ける MSS の広告 | SYN に MSS option 1460（IPv6 1440）だけ（tcp.c:1339〜、`TCP_RECEIVE_MSS` tcp.c:62） | 可。window scale・SACK-permitted・timestamp は出さない |
| 受けの window | `tcp_receive_window`（tcp.c:2456〜）= min(受信 queue の残りの byte, 残りの packet の数 × 1460)、65535 で頭打ち（tcp.c:2477）。queue は 64 KB（`SOCKET_BUFFER_DEFAULT`）・44 packet（`TCP_RECEIVE_PACKETS` tcp.c:94）。先の segment の reorder は別に 44（`TCP_REORDER_MAX` tcp-socket.h:53） | RFC 7323 の window scale が無いので 64 KB が上限（rate ≤ 64 KB / RTT）。**「1 packet = 1460 byte」の約束は相手の segment が 1460 より小さいと守れない**（1024 の segment は 1 packet に 1 つしか入らない: socket.c:687〜 の継ぎ足しは tail の残りに収まる時だけ）。RFC 9293 3.8.6.2.2 の受け側の SWS 回避（小さい窓を広告しない）は無い |
| window の更新 | 受けた segment ごとに直ちに ACK（窓を載せる）。read の後の更新は「広告した窓が 1 MSS 未満で今 1 MSS 以上」か「2 MSS 未満で 2 MSS 以上増えた」時だけ（tcp_recvfrom tcp.c:2410〜2433） | RFC 1122 4.2.3.3 / RFC 9293 3.8.6.2.2 の「min(MSS, buffer/2) 増えたら更新」に近いが閾値が違う。更新の ACK が device の queue で落ちると、相手は persist timer（Linux は指数の backoff で最大 120 s）に頼る |
| window scale（RFC 7323） | 無し（相手の option も読まない。読まないので相手の scale を使わず、正しい） | 欠落。LAN の RTT が数 ms に伸びると 64 KB では足りない |
| SYN の option | MSS だけ送る。受けた SYN の option は全部無視 | 相手の MSS を使わない（上） |
| 送りの window（流量制御） | ring の空き（`TCP_SEND_QUEUE_MAX` = 8、tcp-socket.h:37）と相手の窓 `peer_window`（tcp_send_reliable tcp.c:1681〜1700、tcp_sendto tcp.c:2250〜）。窓 0 か不明でも、何も出ていなければ 1 segment は出す（tcp.c:1691・2258） | 送りの上限は **8 segment × 1024 = 8 KB**（相手の窓が何であれ）。`peer_window` はどの ACK の窓でも取る（tcp.c:3454）: RFC 9293 3.10.7.4 の SND.WL1/WL2 の条件（古い ACK の窓を使わない）が無い |
| cwnd・ssthresh・slow start・congestion avoidance（RFC 5681） | 無し。送りは ring と相手の窓だけで決まる | 欠落。8 segment の今は害が小さいが、ring を広げる時に必須（下の模型: ring 44 で device の queue が 9 だと 0.88 MB/s に落ちる） |
| RTT の推定・RTO（RFC 6298） | 無し。RTO は常に `TCP_INITIAL_RTO` = 1 s（tcp.c:110）、再送ごとに倍、8 倍（8 s）で頭打ち（tcp.c:580〜590）。新しい ACK で 1 s に戻る（tcp.c:1569〜1580） | SRTT・RTTVAR・Karn の規則が無い。RFC 6298 は最小 1 s を SHOULD とするが、実装の多く（Linux・FreeBSD）は 200 ms。LAN では 1 s の待ちが毎回の損失の値段になる |
| 再送の対象 | timer は ring の一番古い segment だけを送り直す（tcp.c:562〜）。送り直した segment が ACK されると次の古い segment を直ちに送り直す（`recovering` tcp.c:1569〜1576、`immediate_resends`） | go-back 風。相手が先の segment を持っている（Linux も zedBSD も持つ）と無駄な再送になる |
| 諦め | 再送 5 回で ETIMEDOUT（`TCP_RETRANSMIT_MAX` tcp.c:126、tcp.c:532）: 1+2+4+8+8 = 約 23 s。どの ACK（進まない ACK も）でも回数は 0 に戻る（tcp.c:1557〜1561） | RFC 1122 4.2.3.5: R2 は少なくとも 100 s（SYN は 3 分）。23 s は短い |
| fast retransmit・recovery（RFC 5681・6582） | 無し。重複の ACK は数えず、timer も延ばさない（tcp.c:1550〜1561 の注記）だけ | 欠落。**損失 1 回ごとに 1 s の停止**（2 回目の損失なら 2 s 以上） |
| SACK（RFC 2018） | 無し（送りも受けも） | 欠落。受けの側は SACK が無いと、相手（Linux の NewReno）が 1 往復に 1 つの穴しか埋められない |
| 窓が開いた時の再送 | ACK が何も retire せず窓が開いたら、一番古い segment を直ちに送り直す（tcp.c:3479〜3483）。相手が「場所が無くて捨てた」とみなす | 本来は persist timer と通常の再送の仕事。窓の増減のたびに無駄な再送が出る（下の模型の slow-reader で 3 s に約 1100 回） |
| zero window の probe | persist timer は無い。窓 0 で何も出ていない時は 1 segment を出し（tcp.c:1691・2258）、それを再送の timer が送り直す（ACK ごとに回数が 0 に戻るので約 2 s 間隔） | RFC 9293 3.8.6.1 の persist timer の代わりとして一応動く |
| delayed ACK | 無し。受けた segment ごとに ACK（tcp.c:3529・3548・3582） | RFC 5681 4.2 の「2 つの full segment に 1 つ以上」は満たす。ACK が倍になり、USB の adapter の送りの queue（8）を食う |
| Nagle（RFC 896） | 無し。`TCP_NODELAY` は値を覚えるだけ（tcp.c:844・890） | write ごとに 1 segment なので小さい write が小さい segment になる |
| ACK の受理 | `acknowledgement >= send_unacknowledged && acknowledgement <= send_next` を **符号無しで**比べる（tcp.c:3437） | **誤り**: RFC 9293 3.4 は sequence を 2^32 の剰余で比べる。送りの sequence が 2^32 を越えた後の ACK は全部捨てられ（下の模型で再現）、接続は止まって約 23 s 後に ETIMEDOUT。初めの sequence は ticks から作る擬似乱数（tcp.c:2068・3032）なので、1 GB の転送が wrap を越える確率は約 1/4、4 GB 以上なら必ず |
| 受けの順序 | sequence がちょうど `receive_next` の segment だけ受ける（tcp.c:3510）。終わりが `receive_next` 以前なら重複として ACK（tcp.c:3528）。先の segment は reorder に保つ（tcp.c:3786〜、窓の内だけ、`offset = sequence - receive_next` tcp.c:3810） | **欠落**: `receive_next` の前から始まり後ろまで続く segment（重なりのある再送、相手が segment を継ぎ直した時）は、受けも保ちもされない（offset が負 = 巨大）。RFC 9293 3.10.7.4 は窓の内の部分を切って受ける |
| 受信 queue の満杯 | 順番どおりの segment を queue が取れないと捨て、ACK を進めない（tcp.c:3560〜3575、`receive_refused`） | 窓の約束を守っていれば起きないはずだが、上の「1 packet = 1460」の約束が破れると起きる |
| 送りの packet の不足 | data の segment が device か pool の不足（ENOBUFS・EAGAIN）で出ないと ring から取り戻し（tcp.c:1745〜1770）、書き手は **10 ms 眠って**やり直す（`TCP_SEND_RETRY_MS` tcp.c:125、tcp.c:2328）。注記は「writer retries at once」と書くが実際は 10 ms 待つ | device の送りの queue は ECM・NCM で 8 + 転送中 1（`ECM_TX_QUEUE_MAX` usb-cdc-ecm.c:54、`NCM_TX_QUEUE_MAX` usb-cdc-ncm-net.c:69）、TCP の ring は 8。**余裕は 1 frame だけ**で、他の frame（逆向きの ACK、別の接続、SSH の session、ARP）が混ざると満杯になり、10 ms ごとに数 segment しか出なくなる |
| 再送・ACK の送り失敗 | timer の再送が ENOBUFS で出なくても次の deadline（2 s 以上）まで待つ（tcp.c:597）。pure ACK・窓の更新の失敗は捨てる（`(void)tcp_send_segment_at`） | 失敗は損失と同じ扱い。今は数えていなかった（net.tcpstats の `segment_send_failures` で数える） |
| socket の buffer | 受け: `SO_RCVBUF` は 2 KB〜1 MB で受け付けるが、窓は 65535 と 44 packet で頭打ち。送り: `SO_SNDBUF` は値を覚えるだけで TCP は使わない（送りの buffer が無く、ring の 8 つの packet の写しだけ） | 自動の調整（auto-tuning）は無い |
| packet buffer | stack 全体で 1 つの pool、64-bit で 256 個 × 2 KB（packet-buf.h:25）。受信 queue 44 + reorder 44 + 送りの写し 8 が接続ごと。alloc と free は 256 個を線形に探す（packet-buf.c:101・183、irq を止めた lock の下） | 複数の接続や driver の受信の ring で尽きうる。尽きた数は数えていなかった（net.tcpstats の header の `packet_alloc_failures`） |

## 2. 原因の候補（順位）

確度: 高 = code と模型（本物の tcp.c）で再現した、中 = code から確か、実の網での頻度は未測定、低 = 可能性。

| 順位 | 候補 | 向き | file:行 | 確度と証拠 |
| --- | --- | --- | --- | --- |
| 1 | **device の送りの queue が満杯の時、data の segment を取り戻して書き手が 10 ms 眠る**。ECM・NCM の queue は 9 frame で ring は 8: 余裕 1。満杯になると 10 ms に queue の分しか出ず、1 MB/s 未満で安定する | zedBSD が送る | tcp.c:1745〜1770・2328、usb-cdc-ecm.c:54、usb-cdc-ncm-net.c:69 | 高（模型）: device の queue 9 → 37 MB/s、7 → **0.68 MB/s**、5 → 0.49 MB/s、2 → 0.20 MB/s。ring を 44 にすると queue 9 で 0.88 MB/s。実の網での引き金（他の frame）の頻度は未測定（`send_refused`・`send_buffer_sleeps`・ifconfig の TX dropped で測る） |
| 2 | **fast retransmit が無く RTO が 1 s 固定**: 損失 1 回ごとに約 1 s 止まる（送り直しも失えば 2 s 以上）。Linux の相手は窓をほぼ一定で返すので「窓が開いた時の再送」も効かない | zedBSD が送る | tcp.c:110・562〜590・1550〜1576・3479 | 高（模型、相手の窓を一定にした Linux 相当）: 損失 0.1 % で 37 → **1.04 MB/s**（30 s の 18 s が 1 MB/s 未満、最長の停止 1.0 s）、1 % で **0.10 MB/s**（最長 3.0 s）。損失ごとに重複の ACK が 7 つ来て捨てられる（`duplicate_acks_in`） |
| 3 | **sequence の wrap の後の ACK を全部捨てる**（符号無しの比較）: 速さが 0 に落ち約 23 s 後に接続が切れる | zedBSD が送る | tcp.c:3437 | 高（模型）: wrap の 4 MiB 前から始めると、ちょうど 4194304 byte で止まり 29 s 後に ENOTCONN（`acks_outside` が増える）。1 GB の転送で約 1/4 の確率 |
| 4 | **送りの ring が 8 × 1024 B**: 速さ ≤ 8 KB / RTT。RTT 10 ms で 0.78 MB/s。USB の adapter の queue で RTT が伸びると 1 MB/s 前後 | zedBSD が送る | tcp-socket.h:37、tcp.c:54・2244 | 高（模型: RTT 0.2 ms で 37 MB/s、10 ms で 0.78 MB/s。ring 44・queue 64 なら 110 MB/s = 線の上限）。落ち込みでなく天井 |
| 5 | **受けの窓が「1 packet = 1460 byte」を約束するが、1460 未満の segment では queue がそれより早く満ちる**: 順番どおりの segment を捨てて（`receive_refused`）相手の再送を待つ | zedBSD が受ける | tcp.c:2456〜2480、socket.c:687〜 | 高（模型、zedBSD 同士 1024 byte の segment、遅い読み手）: 始めに `receive_refused` 1 で 1 s 止まり、以後も窓の開閉ごとに refused と無駄な再送（3 s に約 1100 回）。Linux の 1460 の segment では起きにくく、QEMU の slirp（1440）では window いっぱいの時に最後の segment で起きうる |
| 6 | **ACK・窓の更新が device の queue の満杯で捨てられる**（delayed ACK が無く segment ごとに ACK）: 相手の重複の ACK が欠けて RTO（Linux ≥ 200 ms、cwnd が 1 に）、窓 0 の後の更新が欠けると persist の backoff | zedBSD が受ける | tcp.c:3529・3548・3582・2420〜2433 | 中: code から確か、頻度は未測定（`segment_send_failures`、ifconfig の TX dropped）。模型では逆向きは ACK だけなので起きなかった |
| 7 | **window scale と SACK が無い**: 受けの速さ ≤ 62 KB / RTT（負荷の下の RTT 24 ms で約 2.6 MB/s、T1-516）。損失が窓に複数あると Linux の NewReno は 1 往復に 1 つずつ | zedBSD が受ける | tcp.c:1339〜・2477 | 中〜高（天井。BUG-222 の 5330 の 4.9 MB/s と合う）。落ち込みというより上限 |
| 8 | **重なりのある segment を受けない**: 相手が継ぎ直した再送を受けず、相手の RTO の backoff が伸びる | zedBSD が受ける | tcp.c:3510〜3528・3810 | 低〜中: code から確か、Linux が重なる再送を出す頻度は低い |
| 9 | 諦めが 23 s、古い ACK の窓を使う、packet の pool が共有で 256（尽きると受信の frame を driver が捨てる）、alloc・free の線形の探索 | 両向き | tcp.c:126・3454、packet-buf.c:101・183 | 低（落ち込みの主因ではない。pool の不足は `packet_alloc_failures` で測る） |

「継続した転送ですぐ 1 MB/s 未満」に最も合うのは、送りの向きの 1（他の frame が混ざると 1 MB/s 未満で張り付く）と 2（損失 0.1 % で約 1 MB/s、
1 % で 0.1 MB/s）。3 は「止まる」。受けの向きの天井は 7（と driver）、落ち込みは 5・6 が候補。どれが実の網で起きているかは net.tcpstats の
counter で分かる（下の表の「見る値」）。

## 3. 測りの道具

### 3.1 kernel: `net.tcpstats`（sysctl、commit 508d33db9）

- UAPI: `include/uapi/sysctl.h` に `CTL_NET` = 4、`NET_TCPSTATS` = 1、`struct tcp_stats_header`（48 byte: version・大きさ・数、packet の pool の
  総数・今・最大・alloc の失敗、`time_ms`）と `struct tcp_stats_entry`（312 byte、ILP32・LP64 で同じ、`_Static_assert`）。hw.diskstats と同じ形
  （header + 要素、小さい buffer は ENOMEM と要る長さ）。HAL の API は変えていない。
- 要素: 家族・状態・address・port、送りの側（una・nxt・相手の窓・in flight・ring の使用・send MSS）、受けの側（rcv_nxt・広告した窓・受信 queue の
  byte と packet・reorder）、timer（backoff・今の RTO・deadline の残り）、`features`（cwnd・RTT・window scale・SACK・fast retransmit を持つかの bit。
  今は 0 で、cwnd・ssthresh・srtt・rttvar・fast_retransmits の欄は「持たない」= 0。直しの段がその bit を立てて欄を埋める）、counter 21 個
  （segment の送り・受け、data の送りと byte、順番どおりに受けた byte、再送と byte、直ちの再送、取り戻し、10 ms の眠り、送りの失敗、ACK・重複の ACK・
  範囲外の ACK、窓 0 の受け・送り、窓の更新、古い segment・先の segment・保った数、queue の満杯での拒否）。
- kernel: `src/kern/net/tcp.c` の `tcp_stats_copy`（registry の lock の下で数えて写す、各 socket はその lock で）、`tcp_stats_fill_locked`、
  全ての segment を数えるため送りを `tcp_send_segment_at`（数える）と `tcp_output_segment`（作って出す）に分けた。counter は socket の lock の下で
  動かす（`include/kern/net/tcp-socket.h` の `struct tcp_counters`）。`src/kern/net/packet-buf.c` に `packet_buf_pool_usage`（最大の使用と
  失敗の数）。`src/kern/sysctl.c` に葉 `net.tcpstats`。
- userland: `sysctl net.tcpstats` が 1 行目に時刻と pool、以下 1 socket 1 行の `name=value`（`userland/base/sysctl/main.c`、`sysctl -a` にも入る）。
- 確かめ: amd64（`config/ci/config-amd64.mk`）・rpi4（`config/ci/config-rpi4.mk`）の vmunix と sysctl の build は warning 0。pcat（ILP32）は
  全 object の compile が warning 0（tcp.o・sysctl.o を含み、ILP32 の `_Static_assert` を通る）、link は既存の `drv_xhci_isoch_*` の未定義
  （pcat の vmunix.mk に pci-xhci-isoch.c が無い、本変更と無関係）で失敗。host の模型が `tcp_stats_copy` を本物で呼び、長さ・数・送りの byte・
  受けの byte が socket と合うことを毎回確かめる（`tcpstats: ok`）。guest での `sysctl net.tcpstats` の表示は未実施（T1）。
- style-check: 変えた file で増えた指摘は sysctl/main.c の 1（`strcmp` の比較の連鎖に 1 行足した、同じ関数の他の行と同じ形）だけ。

### 3.2 host: 長い転送の時系列（`plan/ws220/tests/tcp-stream-series.sh`）

- QEMU: SSH の guest（`plan/ws220/tests/config-amd64-tcp.mk` = `plan/tools/guest/config-amd64-ssh.mk` + sysctl）に 2 つ目の usb-net（ECM）を host の
  tap に付け、bridge と veth で namespace `zbs`（10.77.0.1）へ。guest は 10.77.0.2。netem（`--loss`・`--delay`）は両向き。
  - down（zedBSD が受ける）: namespace の `stream.py serve`（`GET /N` に N byte）から guest の `fetch -o /dev/null`。
  - up（zedBSD が送る）: namespace から guest の sshd へ `ssh … dd if=/dev/zero bs=65536 count=N/65536`、host の `stream.py sink` が読む。
  - 両端の採取: guest は約 1 秒ごとに `sysctl net.tcpstats` と `ifconfig IF`、host は `ss -tin`（cwnd・ssthresh・rtt・retrans）。
  - `plan/ws220/tests/analyze.py` が向きごとに `series-down.csv`・`series-up.csv`（秒、MB/s、窓・ring・timer、counter の増え、host の cwnd・rtt）と
    summary の行（平均・最低・中央値の 1 秒の速さ、1 MB/s 未満の秒、0 の秒と最長の 0、counter の合計）。
- 実機: `tcp-stream-series.sh ssh:root@MACHINE --server-ip この host の LAN の address`（5330・Pi 4 を同じ LAN で。netem は無し）。
- 出力は `--out`（既定 build/ws220-series）の後ろに新しい directory（`plan/tools/fresh-out.sh`）、host の物は消さない。
- 確かめ: `sh -n`、`stream.py`（127.0.0.1 で 100 MiB の serve→sink）、`analyze.py`（合成の sample）を host で。QEMU と実機での実行は未実施（T1・Q1）。

### 3.3 host: 本物の tcp.c の模型（`plan/ws220/tests/tcp-model/run.sh`）

- `src/kern/net/tcp.c`・`socket.c`・`packet-buf.c`・`checksum.c`・`in6.c` を kernel と同じ形（freestanding、kernel の header）で compile し、
  `shim.c`（virtual の clock、1 thread の lock と wait queue、2 つの device と route、IPv4 の出力 = 模型の link）と `model.c`（2 つの host A・B、
  線の速さ・片道の遅延・損失・device の queue の長さ、書き手と読み手、時系列と summary）を link する。TCP の判断は本物のまま。
- 書き手は kernel の書き手と同じく、ring と窓に空きがあるのに断られたら 10 ms 眠る。`--peer-window W` は B から A への segment の窓を W に
  書き換えて（checksum も作り直す）、窓が queue に追従しない相手（Linux）を表す。`--wrap-before N` は A の sequence を wrap の N byte 前から。
  `MODEL_CFLAGS=-DCONFIG_TCP_SEND_QUEUE_MAX=44` などで what-if。
- run.sh の既定は WS220 の目標の scenario（下の表）を流す。p001 の時点（直す前）の結果（2026-10-13、host で約 2 s）:

| scenario | 条件 | 目標（平均 MB/s 以上、全部届く） | 直す前 |
| --- | --- | --- | --- |
| lan-clean | 1 Gb/s、RTT 0.2 ms、queue 9 | 30 | 37.28 PASS |
| rtt-10ms | RTT 10 ms | 4 | 0.78 FAIL（8 KB / RTT） |
| lan-loss-0.1 | 損失 0.1 %、相手の窓一定 | 10 | 1.04 FAIL（18/30 s が 1 MB/s 未満、停止 1.0 s） |
| lan-loss-1 | 損失 1 %、相手の窓一定 | 2 | 0.10 FAIL（停止最長 3.0 s） |
| rtt-10ms-loss-0.1 | RTT 10 ms、0.1 % | 2 | 0.39 FAIL |
| lan-loss-1-zedbsd | 損失 1 %、相手は zedBSD | 2 | 6.31 PASS（窓が動くので「窓が開いた時の再送」が偶然効く。停止 2.0 s） |
| lan-ackloss-1 | ACK の損失 1 % | 30 | 37.18 PASS |
| lan-wrap | wrap の 4 MiB 前から | 10 | 0.14 FAIL（4 MiB で止まり ENOTCONN） |
| slow-reader | 読み手 2 MB/s | 1.6 | 1.71 PASS（始めに refused で 1 s 停止、無駄な再送が多い） |

what-if（ring 44、2026-10-13）: queue 64 なら lan-clean 110.7 MB/s（線の上限）、queue 9 だと 0.88 MB/s（候補 1）、損失 0.1 % は 0.91 MB/s（ring を
広げるだけでは損失に効かない）。ring 8 で queue 7・5・2 → 0.68・0.49・0.20 MB/s。

模型の限り: 2 つの host が 1 つの packet の pool と registry を共有する（実際は別）。driver の USB の転送の時間、受けの driver の packet の確保の
順、network worker の budget は模していない。Linux の送り手（cwnd、SACK）は模していない（受けの向きは QEMU と実機で測る）。模型の C は
style-check の指摘 42（試験の code、WS の規約の全文の Phase で直す）。

## 4. 直しの設計

方針: 直しは TCP（`src/kern/net/tcp.c`・`include/kern/net/tcp-socket.h`・必要なら `socket.c`）の中で行い、HAL の API と driver の I/F は変えない。
各段は模型の scenario を目標にして host で先に確かめ（数十秒）、段のまとまりごとに T1 に QEMU（と Q1 の実機）を依頼する。net.tcpstats の
`features` の bit を各段で立て、欄を埋める（cwnd → `TCP_STATS_HAVE_CWND` など）。

| 段（Phase の案） | 直すこと | 理由（候補） | 先に確かめる模型の scenario |
| --- | --- | --- | --- |
| p002 正しさ | (a) sequence の比較を全部 2^32 の剰余の比較に（`SEQ_LT`・`SEQ_LEQ` の static inline、ACK の受理 tcp.c:3437、`> send_next` の ACK には ACK を返して捨てる）。(b) 窓の更新を SND.WL1/WL2 の条件で（古い ACK の窓を使わない）。(c) 重なりのある segment は `receive_next` から後ろを切って受ける。(d) 諦めを RFC 1122 に（R2 = 再送の合計で約 100 s 以上、SYN は 3 分、backoff の上限 60 s） | 3・8・9 | lan-wrap が全部届く（目標 10）、wrap の前後で `acks_outside` 0。重なりの scenario を足す（`--resegment`: 再送を 2 つの segment を継いで送る A の模型） |
| p003 送りの packet 不足 | (a) 取り戻した data の segment を書き手の 10 ms の眠りで待たない: segment は ring に残し「device が拒んだ」印を付け、device の送りが空いた時か次の tick（1 ms）に再送の経路で出す（driver の送りの完了は `net_device_schedule_poll` で network worker を回し（usb-cdc-ecm.c:823、usb-cdc-ncm-net.c:887）、worker は回るたびに `tcp_timer_run` を呼ぶ（core.c:602））。書き手は ring の空きを待つ（今の `send_waitq`）。(b) pure ACK の送り失敗は「ACK が要る」印を残して次の tick に出し直す | 1・6 | queue 7・5・2 で lan-clean が 30 MB/s 以上（今 0.68〜0.20）、ring 44 の what-if で queue 9 でも線の上限近く |
| p004 RTT と RTO | RFC 6298 の SRTT・RTTVAR（1 往復に 1 sample、Karn: 再送した segment の ACK は使わない）、RTO = SRTT + 4·RTTVAR、最小 200 ms（RFC の 1 s から外す: Linux・FreeBSD と同じ。決めはユーザーか design-reviewer）、最大 60 s、新しい ACK で backoff を戻す。clock は 1 ms の tick | 2（停止の長さ） | lan-loss-1 の最長の停止 < 0.5 s。net.tcpstats の srtt_us・rttvar_us・rto_ms |
| p005 損失の回復 | RFC 5681 の fast retransmit（重複の ACK 3 つ）と RFC 6582 NewReno の recovery（partial ACK ごとに次の穴を再送）、「窓が開いた時の再送」（tcp.c:3479）と「回復した時に次を直ちに再送」（tcp.c:1569〜1576）を外し、窓 0 には persist timer（RFC 9293 3.8.6.1）を置く | 2 | lan-loss-0.1（10）・lan-loss-1（2）・rtt-10ms-loss-0.1（2）、slow-reader の無駄な再送が消える（`immediate_resends` 0） |
| p006 輻輳の制御と送りの buffer | (a) RFC 5681 の cwnd・ssthresh（slow start、congestion avoidance、IW は RFC 6928 の 10 segment）。(b) 送りの ring（8 個の packet の写し）を byte の送りの buffer に替える（`SO_SNDBUF`、既定 256 KB 程度、copy は 1 回の write で buffer の空きまで）、segment は送る時に buffer から切る、大きさは min(相手の MSS, device の MTU − 40)（SYN の MSS option を読む）。再送は buffer から作るので pool の packet を持たない。(c) Nagle（`TCP_NODELAY` で切る） | 4・1（ring を広げると device の queue を溢れさせる: cwnd が要る） | rtt-10ms（4、64 KB の窓で 6.4 MB/s）、ring を広げても queue 9 で lan-clean ≥ 30 |
| p007 受けの窓 | (a) 窓の約束を守る: 受信 queue を「packet の数」でなく byte で数える（`socket_enqueue_stream` が tail の残りを埋めた後の残りを次の packet にする、または窓 = 空き packet × 実際に受けた segment の大きさ）。(b) 受け側の SWS 回避（RFC 9293 3.8.6.2.2）と窓の更新を「min(MSS, buffer/2) 開いたら」に。(c) RFC 7323 の window scale（受けの buffer を 64 KB より大きくする時。pool 256 × 2 KB の内で、受けの buffer の上限を決める）。(d) delayed ACK（2 segment に 1 つ、最大 40 ms）と受けの batch の終わりで 1 つの ACK | 5・6・7 | slow-reader の `receive_refused` 0、受けの窓の scenario を足す（相手 = 模型の A に 1448 byte の segment） |
| p008（候補） | 受けの SACK（RFC 2018: SACK-permitted と SACK の block）。Linux の送り手の損失の回復を 1 往復に複数の穴へ | 7 | QEMU の down で損失 1 % |
| p009 規約の全文 | WS の全ての C（模型と試験を含む）の見直し | — | — |

段の順の理由: p002 は小さく危険が低い正しさの直し（wrap は接続が切れる）。p003 は単独で最も大きい落ち込み（候補 1）を除き、ring を広げる
p006 の前提（device の queue を溢れても崩れない）。p004 → p005 は損失の値段を 1 s から 1 往復へ。p006 は天井を上げる（cwnd が無いと危険なので
p005 の後）。p007 は受けの向き。driver の上限（ECM の 1 frame ずつの転送、NCM の受信の URB）は TCP の外で、WS033（BUG-222）の範囲として扱う。

各段の確かめ: build（amd64・rpi4 の vmunix）warning 0、模型の `run.sh` の該当の scenario、段のまとまり（p003〜p005、p006〜p007）ごとに T1 の
QEMU の `tcp-stream-series.sh`（損失 0・0.1・1 %）、最後に実機。既存の網の試験の回帰（`plan/ws033/tests/tcp-receive-speed.sh`・
`tcp-loss-speed.sh`・`lan-hotplug.sh`）は WS の最後に T1 でまとめて。

## 5. 受け入れの目標の決め方

1. **基線**: p001 の T1 の測り（下の依頼 A）で、今の kernel の QEMU（ECM・tap）の down・up の 1 GB の平均・最低・中央値と counter を得る。
   実機（5330 の RTL8156 NCM 2.5 Gb/s の link、Pi 4 の GENET 1 Gb/s）も同じ script で（依頼 B）。
2. **路の上限**: 同じ路で TCP 以外が律速の値を分ける。QEMU は ECM の driver の上限（down は既存の 8〜11 MB/s が目安、up は今の 8 KB の ring で
   測れないので、p006 の後に ring を大きくした値）。実機は link の速度と driver（NCM の URB、GENET の ring）の上限。Linux の相手同士の値（同じ
   Linux の host と別の Linux の機械）を参考に取れれば取る。
3. **目標**（各路、両向き、1 GB）: (i) 損失 0 %: 平均 ≥ 路の上限の 80 %、最初の 5 s の後に 1 MB/s 未満の秒が無く、どの 1 秒も中央値の 50 % 以上。
   (ii) 損失 0.1 %（QEMU の netem）: 平均 ≥ 損失 0 % の 50 %、最長の 0 の続きが 0.5 s 未満。(iii) 損失 1 %: 平均 ≥ 損失 0 % の 20 %、止まらない
   （最長の 0 が 2 s 未満）、全部届く。(iv) wrap を越える転送（模型と、4 GB の QEMU の 1 回）で止まらない。値は基線の後に WS220 の ws.md に
   書き、ユーザーに示す（WS220 の原文「目標の値は p001 の測りの後に決める」）。
4. 暫定の数（基線の前の見込み）: QEMU（ECM）down ≥ 6 MB/s、up ≥ 6 MB/s、5330 ≥ 30 MB/s、Pi 4 ≥ 30 MB/s。driver の上限がこれより低ければ
   その 80 %。

## 6. T1・Q1 に頼む測り

**依頼 A（T1、QEMU、基線）**
- image: 本 Phase の commit 以降の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk BUILD`（SSH の guest + sysctl）。
- まず guest で `sysctl net.tcpstats` が `net.tcpstats: time_ms=… sockets=…` と SSH の接続の行を出すこと（新しい kernel の口の確認）。
- `plan/ws220/tests/tcp-stream-series.sh IMAGE --bytes 1073741824 --out build/ws220-series-0`（損失 0、down と up、各 1 GB、ECM で各 2〜3 分）。
- `… --loss 0.1 --bytes 268435456 --out build/ws220-series-01` と `… --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-series-1`
  （直す前の up は損失で 0.1〜1 MB/s に落ちる見込みなので byte を減らす。1 回が長すぎる時は up だけ `--direction up --bytes 16777216`）。
- sudo（ip・tc・namespace）が要る。QEMU は 1 つ。全体で 15〜25 分の見込み。
- 返す物: 各 run の `summary.txt`（`analyze: summary …` の行と host の時計の MB/s）、`series-*.csv`、`settings.txt`、失敗なら `transfer-*.txt`。
  合否: 転送が全部届いたか（`tcp-stream-series: PASS`）。速さは基線として記録（直す前なので目標の判定はしない）。

**依頼 B（Q1 か T1、実機、基線）**: 5330（10.0.30.3、ue0 = RTL8156）と Pi 4（GENET）に本 Phase の kernel と sysctl を入れ、この host から
`plan/ws220/tests/tcp-stream-series.sh ssh:root@MACHINE --server-ip この host の LAN の address --bytes 1073741824`。up は機械の sshd の
暗号の速さも入る（`top` で sshd の CPU を見る）。

**見る値**（summary の counter）: up の `send_refused`・`send_buffer_sleeps`（候補 1）、`retransmit_segments`・`duplicate_acks_in`（候補 2）、
`acks_outside`（候補 3）、in_flight_bytes が 8192 に張り付く（候補 4）。down の `receive_refused`（候補 5）、`segment_send_failures`（候補 6）、
`out_of_order_in`・`old_segments_in`、host の `ss` の cwnd・retrans（Linux が損失をどう見たか）、`receive_window` の推移（候補 7）。header の
`packet_alloc_failures`（pool）。ifconfig の TX dropped・RX errors。

## 7. 実行した command と結果

- `git merge main`（57830aff7）。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p1-ws220-amd64 ZEDBSD_USER_PROGRAMS=sysctl build/p1-ws220-amd64/vmunix build/p1-ws220-amd64/bin/sysctl`
  rc 0・warning 0（`amd64 vmunix check: PASS`）。同じく `config/ci/config-rpi4.mk`・`build/p1-ws220-rpi4` rc 0・warning 0。
  `config/ci/config-pcat.mk`・`build/p1-ws220-pcat`: compile は warning 0、link は既存の `drv_xhci_isoch_*` の未定義で失敗（無関係）。
- `python3 plan/tools/style-check.py`（tcp.c 125 → 125、sysctl.c 17 → 17、packet-buf.c 11 → 11、sysctl/main.c 62 → 63）、`git diff --check` ok。
- `plan/ws220/tests/tcp-model/run.sh`（上の表、`tcp-model: FAIL` は直す前の目標の未達で想定どおり）、what-if は §3.3。
- `stream.py` の serve→sink（100 MiB、両端の byte が一致）、`analyze.py` の合成の sample、`sh -n tcp-stream-series.sh`。

未実施: QEMU と実機の測り（依頼 A・B）、guest での `sysctl net.tcpstats` の表示、design-reviewer。

## 8. 残りと判断

- ユーザーの判断: §0 の QEMU の受け入れの網（usb-net のままか virtio-net を足すか）。§4 p004 の RTO の最小（200 ms、RFC 6298 の 1 s から外す）。
- 次: Q1 が design-reviewer に本 file を回し、T1 に依頼 A を積む。基線の後に §5 の目標の値を ws.md に書き、p002 以後を Queue にする。
- 削除してほしい出力（Q1、P1 の worktree の build/ の中）: `build/p1-ws220-pcat`（link の失敗の確かめに使った物）、`build/ws220-host-q44`（what-if の build）。
  `build/p1-ws220-amd64`・`build/p1-ws220-rpi4`・`build/ws220-host` は次の段で使い回すので残す。
