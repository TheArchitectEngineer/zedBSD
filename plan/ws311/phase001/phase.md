<!-- awesome-plan project=zedbsd record=ws311-p001 -->
# ws311-p001: CUBIC の調べと設計

Status: in-progress（2026-10-29 P3 q1664-i05: 設計の第 2 版。design-reviewer の review-1（致命的 2・重大 4・中 9・軽微 7）を §9 の表で全部に答えた。§8 の問いの答え待ち）
Parent: [WS311](../ws.md)
Queue: q1664 / q1664-i05（P3）
依存: WS221 p008・p009・p011（cleared）

## 1. 由来と目標

ユーザー（原文、2026-10-29）:「なるほど。みんなCUBICに帯域を奪われて、たまりかねたんですかね……。CUBICを実装して、性能がよくなったらCUBICに切り替えるWSを作って、取り組んでほしいです。jumbo frameの続きでやってほしいです。」

ws.md の目標（Q1 の解釈）: CUBIC（RFC 9438）を今の Reno 型と並べて実装し、接続ごと・system の既定を選べるようにする。同じ条件で測り、良ければ既定を CUBIC に切り替え、良くなければ切り替えず値と理由を残す。

Q1 の指示（2026-10-29）: WS221 p011 で見つけた「cwnd が送りの窓に抑えられない時も増え続ける」件をこの p001 で扱う。

## 2. 今の tcp.c の輻輳の制御（HEAD b7c005f54 で読んだ所）

| 所 | 今の形 |
| --- | --- |
| 状態（`include/kern/net/tcp-socket.h`） | `cwnd`・`ssthresh`・`cwnd_acked`（byte）、`recovery`・`recover`・`recovery_by_timeout`、`send_max` |
| 始まり（`tcp_start_sending`） | IW = min(10 MSS, 14600)（RFC 6928）、ssthresh = 無限 |
| 増やす（`tcp_cwnd_grow_locked`、ACK ごと、recovery の外と timeout の go-back-N の間） | slow start: acked（1 ACK に最大 2 MSS、RFC 3465 の L）。CA: 窓 1 つ分の ACK ごとに 1 MSS |
| 減らす（`tcp_cwnd_loss_locked`） | ssthresh = max(flight / 2, 2 MSS)（flight = `tcp_sent_end_locked` − una）。fast retransmit: cwnd = ssthresh、timeout: cwnd = 1 MSS。本当に満了した timeout は backoff のたびに呼ぶ（tcp.c:1150） |
| recovery の終わり | cwnd = min(cwnd, ssthresh)（RFC 6582 §3.2 step 3） |
| 門 | 書く時に `tcp_send_room_locked`（min(相手の窓, cwnd) − `tcp_in_flight`、in_flight は unsent を含む）、unsent の送りは `tcp_unsent_fits_cwnd_locked`（送り済みの flight） |
| 書き手を起こす | 低水位（ring・buffer の半分、limit の 1/4 が空いた時、`tcp_send_wake_due_locked`）。ACK のたびではない |
| 上限 | `TCP_CWND_MAX` 0x40000000、送りの ring 256 entry（64 bit、32 bit は 8）、送りの buffer 既定 384 KB（SO_SNDBUF 最大 1 MB）、jumbo の copy 8、受けの buffer 既定 128 KB |
| RTT の標本 | 1 RTT に 1 segment（Karn）、tick 単位（`KERN_CLOCK_HZ`、多くは 1000、ppc・m68k は 100） |

**不具合（WS221 p011 で見つけた）**: 損失の無い間、ACK が来れば cwnd が増える。LAN では相手の窓が送りを抑え、cwnd は使われないまま 9 億 byte 台へ。害: 窓が開いた時の burst、CUBIC の W_max の意味の喪失、RFC 7661・RFC 9438 §5.8 に反する。

## 3. RFC 9438 の要点（2026-10-29 に rfc-editor.org の本文を読んだ）

- β = 0.7（§4.6）、C = 0.4（§5.1）、α = 3(1 − β)/(1 + β) ≈ 0.529、W_est ≥ cwnd_prior の後は α = 1（§4.3）。
- W_cubic(t) = C (t − K)³ + W_max、K = ∛((W_max − cwnd_epoch) / C)。t は CA の段の始まりからの秒、app-limited の間は数えない（§4.2）。
- target = W_cubic(t + RTT) を [cwnd, 1.5 cwnd] に clamp、ACK ごとに cwnd += (target − cwnd) / cwnd（§4.4・4.5）。
- Reno-friendly: W_est = cwnd_epoch から、ACK ごとに W_est += α × acked / cwnd。**W_cubic(t)**（RTT を足さない値）< W_est なら cwnd = W_est（§4.3）。
- 減らす: ssthresh = flight_size × β、cwnd = max(ssthresh, 2 MSS)（§4.6）。fast convergence（§4.7 SHOULD）。
- timeout（§4.8）: cwnd = 1 MSS、ssthresh は β。次の CA で K = 0、W_max = その段の始まりの cwnd。
- 偽の輻輳の戻し（§4.9 MAY）、HyStart++（§4.10 SHOULD、RFC 9406）、app-limited は増やさない（§5.8）。
- 応答関数（§5.2）: CUBIC の平均の窓 ≈ (C(3+β)/(4(1−β)))^¼ × RTT^¾ / p^¾ segment、Reno-friendly ≈ 1.22/√p。p = 1e-4 の境は RTT ≈ 56 ms: それより短い RTT では W_est（Reno と同じ速さ）が勝つ（review F3）。

## 4. 設計

### 4.1 cwnd を使っている時だけ増やす（Reno・CUBIC 共通、先に入れる、RFC 7661・RFC 9438 §5.8）

review F1・F9 で第 1 版の「ACK ごとに戻す 1 bit」をやめ、round の状態にした:

- `cwnd_round_end`（seq）・`cwnd_round_max_flight`（byte）: 送るたびに**送り済みの** flight（`tcp_sent_end_locked` − una、unsent を含まない）の最大を round の中で覚える。una が `cwnd_round_end` を越えたら、その round の最大を `cwnd_prev_max_flight` に移し、`cwnd_round_end` = `tcp_sent_end_locked`。
- `cwnd_blocked`（round の間保つ）: 書く時の門が cwnd で断った時（cwnd < 相手の窓 の時に限る）、または `tcp_unsent_fits_cwnd_locked` が止めた時に 1。round の終わりで 0 に戻す。device の拒み（ENOBUFS・EBUSY）や pool の空は立てない（自分の device が律速の時に cwnd-limited と見ない、review F9）。
- 増やす条件: slow start は cwnd < 2 × max(`cwnd_round_max_flight`, `cwnd_prev_max_flight`) か `cwnd_blocked`。CA は max(…) ≥ cwnd − MSS か `cwnd_blocked`。どちらでもない ACK は増やさない（Reno の `cwnd_acked` も溜めない）。
- 予想: LAN（相手の窓が律速）で cwnd ≈ 2 × flight で止まる。速さは変わらない。Reno の挙動の変更なので、比べる基準の「Reno」はこの門を入れた後の Reno とする（review F22）。
- 出典: Linux の `tcp_is_cwnd_limited`（最大の flight の 2 倍の規則）の考え方を参考にする。code・名前・構造は写さない（guardrail「出典」、comment は `Procedure referenced:`）。

### 4.2 選び方の口

- 接続ごと: `setsockopt(IPPROTO_TCP, TCP_CONGESTION, name, len)`・`getsockopt`。名前（`"reno"`・`"cubic"`）は 1〜`TCP_CA_NAME_MAX`（16）− 1 byte、NUL は有っても無くてもよい、空・長すぎは EINVAL、知らない名前の戻り値は p003 で FreeBSD の source を読んで揃える（第 1 版の ENOENT は未確認、review F18 は ESRCH と言う、これも未確認）。get は NUL まで書き、len が足りなければ切って NUL を付ける（Linux の形）。番号は FreeBSD の 64。今の `tcp_setsockopt`・`tcp_getsockopt` は int の option の形なので、文字列の option の分岐を足す。どの state でも変えてよく、変えた時は §4.4 の「空にする」をする（cwnd・ssthresh は保つ）。
- system の既定: sysctl `net.tcp.congestion`（読み書きは文字列、superuser だけが書く。kernel の中は enum の 1 語、atomic で読む、review F19）。`tcp_socket_create` が読む。listen の子は `tcp_passive_syn` の中で listener の値を継ぐ（今の code に無い、足す）。sysctl.c の `leaves[]` と userland の `sysctl` の一覧に足す。build の既定 `CONFIG_TCP_CC_DEFAULT`（p003 では reno、p004 の判定で決める）。
- net.tcpstats の entry: uint32 を 4 つ（`cc`・`cubic_w_max_kb`・`cubic_k_ms`・`cwnd_state`（§4.1 の bit））足して 312 → 328 byte（全 arch で 328、static assert を直す）、`TCP_STATS_HAVE_CC` の bit。読み手（userland の sysctl は `element_size != sizeof` で拒む、tcp-model の model.c、plan/tools の解析）を同じ commit で直す（review F12）。

### 4.3 CUBIC の状態と計算

状態（接続ごと、byte と ms、review F11・F17）: `cubic_w_max`・`cubic_cwnd_prior`・`cubic_w_est`・`cubic_origin`（byte）、`cubic_epoch_started`（flag）・`cubic_epoch_ms`・`cubic_k_ms`・`cubic_last_ack_ms`、`cubic_after_timeout`（flag）、`cubic_est_rem`（W_est の端数）。

時間: `sched_ticks()` を ms へ `kern_ticks_to_ms`（`KERN_CLOCK_HZ` で換算、HAL は変えない）。srtt は `rtt_samples` が 0 なら 0 として使う（review F8）。

ACK（CA、`tcp_cubic_grow_locked`、§4.1 の門を通った時だけ）:
1. epoch が無ければ始める: epoch = 今、W_est = cwnd、`cubic_est_rem` = 0。`cubic_after_timeout` なら K = 0、origin = W_max = cwnd（§4.8、review F6）。そうでなく cwnd < W_max なら K = ∛((W_max − cwnd) × 2.5e9 / MSS) ms（掛けてから割る、ΔW ≤ 2³⁰ で約 2.7e18 が uint64 に入る）、origin = W_max。他は K = 0、origin = cwnd。
2. app-limited の後: 前の ACK から 1 RTO 以上送るものが無かった（idle）時だけ epoch をその分ずらす（RFC 9438 §4.2。review F1: 毎 ACK にはずらさない）。
3. W_cubic(τ)（byte）: Δ = |τ − K|（ms、2²⁰ に抑える）、off = (Δ³ / 2 500 000) × MSS / 1000（milli-segment を経て byte、途中は約 3e16 で uint64 に入る。C = 0.4 = 1/2.5 と ms³ → s³ の 1e9 をまとめた、review F7）、W = origin ± off（τ < K なら引く、0 未満にしない）。W は `TCP_CWND_MAX` で抑える。
4. target = W_cubic(t + srtt) を [cwnd, 1.5 cwnd] に。`cwnd_acked` += acked × (target − cwnd) / cwnd（64 bit）、MSS 溜まるごとに cwnd += MSS。
5. Reno-friendly: W_est += (529 × acked × MSS + `cubic_est_rem`) / (1000 × cwnd)、余りは `cubic_est_rem`（W_est ≥ cwnd_prior の後は α = 1/1）。**W_cubic(t)**（RTT を足さない、review F16）< W_est なら cwnd = W_est。

減らす（`tcp_cwnd_loss_locked` の CUBIC の側）:
- fast retransmit: W_max = cwnd < W_max ? cwnd × (1 + β)/2 : cwnd（fast convergence、§8 Q8）、cwnd_prior = cwnd、ssthresh = max(flight × 7 / 10, 2 MSS)（64 bit）、cwnd = ssthresh、epoch を空に。
- timeout の最初の 1 回（`recovery_by_timeout == 0` かつ `rto_backoff == 0`）: 上と同じ W_max・ssthresh、cwnd = 1 MSS、`cubic_after_timeout` = 1。同じ回復の中の 2 回目以降の timeout は ssthresh・W_max・cwnd_prior を変えず cwnd = 1 MSS だけ（review F6）。Reno の側は §8 Q9。
- 立方根: 64 bit の整数の二分探索（K の計算は損失ごとに 1 回）。浮動小数は使わない。

### 4.4 空にする所（review F10・F11）

CUBIC の epoch・W_est と端数・§4.1 の round の状態を空にする所: cwnd を CC の外で変える所（recovery の終わりの cwnd = min(cwnd, ssthresh)、go-back-N の開始）、算法の切り替え、send_mss の変化（Packet Too Big、`tcp_start_sending`）、接続の始まり（`tcp_start_sending`・`tcp_rtt_forget_locked`）。W_max・cwnd_prior は byte なので MSS の変化の後も使えるが、epoch は始め直す。

### 4.5 入れないこと

- HyStart++（review F5）: RFC 9406 は round に 8 個以上の RTT の標本が要るが、今の tcp.c は 1 RTT に 1 標本・tick の分解能（100 Hz の機械は 10 ms）。足すには `struct tcp_pending` に送った時刻（256 entry × 8 byte、socket ごと）と ACK ごとの標本が要る。推し: この WS では入れず Future Work（RFC 9438 §4.10 は SHOULD、Reno の slow start は MAY）。§8 Q3。
- 偽の輻輳の戻し（§4.9 MAY）: 検出の手段（timestamp・DSACK）が無い。
- ECN、他の算法。

## 5. 試験と判定（review F2・F3・F4・F13・F14・F15）

### 5.1 模型（plan/ws220/tests/tcp-model）

足す物:
- bottleneck の段: wire（送り手の速さ）→ byte で数える drop-tail の queue（溢れは loss に数え、packet は copy で持ち pool を使わない）→ 遅延。今の `--queue` は device の拒み（ENOBUFS、TCP が送り直す）で輻輳の信号にならないので別の物（review F4）。
- 2 本目の接続: A → B の別の port、開始の時刻のずらし（`--second-flow-at-ms`）、算法（`--second-flow-cc`）。host は A・B のまま。
- 上限の引き上げ: model の build で `CONFIG_TCP_SEND_QUEUE_MAX`（`#ifndef`）、SO_SNDBUF・SO_RCVBUF を scenario ごとに上げる。各 run の終わりに「cwnd が律速だった」時間の割合（in_flight ≈ cwnd < send_window）を出し、80 % 未満の run は判定に入れない（review F2）。
- 新しい check: 終わりの cwnd ≤ 2 × 最大の送り済み flight（p011 の不具合を捕まえる）、slow start の 1 round の倍増、CA の 1 MSS/RTT 以上の伸び（§4.1 の門の stall を捕まえる、review F1・F15）。
- 既存の scenario の期待のうち算法に依る物（`--expect-fast-retransmits`・`--expect-retransmits-max`・`--expect-timeouts-max`・`--expect-segments-*`）を p002 の初めに一覧にし、CUBIC の run では理由を書いて別の期待にする（黙って緩めない）。

scenario（各 60 秒以上、ユーザーの 2026-10-13 の決定）と、RFC の応答関数からの期待:

| 名前 | link | 期待（CUBIC / Reno） |
| --- | --- | --- |
| `cc-lan` | 1000 Mbps、片道 0.05 ms、損失 0 | 同じ（≥ 0.99） |
| `cc-20ms-loss` | 100 Mbps、片道 10 ms（BDP 250 KB）、queue = BDP、損失 0.01 % | Reno-friendly の域: ≥ 0.97 |
| `cc-100ms-loss` | 50 Mbps、片道 50 ms（BDP 625 KB）、queue = BDP/2、損失 0.001 % | CUBIC の域: ≥ 1.2 |
| `cc-100ms-step` | 同じ link、損失 0、定常の後に `--drop-ab` で 1 つ落とす | 落とした後に定常の 90 % へ戻る時間 CUBIC < Reno |
| `fair-cubic-cubic` | `cc-20ms-loss` の link、2 本目は 5 秒後 | 10〜60 秒の 1 秒ごとの速さの Jain の指数 ≥ 0.95 |
| `fair-cubic-reno` | 同じ | Reno の取り分 ≥ 25 % |

### 5.2 実機（p004）

- VF2 ↔ 5330（1GbE LAN）: Reno（門の後）と CUBIC、両向き、1500・9000、IPv4・IPv6、各 30 秒 × 5、中央値。基準: CUBIC の中央値 ≥ Reno の中央値 × 0.99、かつ差が Reno 自身の 5 回の幅の中（review F14）。
- 遅延の経路（§8 Q5 の承認が要る）: 5330 の ingress を ifb に回して netem（delay 20 ms・loss 0.01 %・rate 100mbit、BDP が §2 の上限の中）。zedBSD が送る向きだけを判定に使う（egress の qdisc は ACK にしか掛からない、review F13）。終わりに qdisc と ifb を消し `tc qdisc show` で確かめる。基準: `cc-20ms-loss` と同じ ≥ 0.97。
- Linux CUBIC との取り合い（§8 Q6）: 同じ netem の bottleneck へ VF2 と centris が同時に 5330 へ送る形。centris と 5330 の配線（同じ switch か）は未確認。作れなければ model の fair-* だけで判定。
- 10GbE（WS221 p010 の後）: p010 が終わっていれば Reno・CUBIC を比べる。終わっていなければ記録して後に回す。

### 5.3 既定の切り替えの判定（p004）

5.1 の全部の期待を満たし、5.2 の LAN の基準を満たし、遅延の経路を測れた時はその基準も満たす → `CONFIG_TCP_CC_DEFAULT` を cubic に。満たさない → reno のまま、値と理由を ws.md に。

## 6. 出典と書き方（review F21）

- RFC の定数は `Value referenced: RFC 9438 section 4.6` などをその block に。
- Linux・FreeBSD の手順を参考にする block は複数行の `Procedure referenced:`、code・名前・構造は写さない。
- source の comment・log に WS・Phase・Bug の番号を書かない。

## 7. Phase（ws.md の案を改める、review F22）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p001 | この設計 → design-reviewer | — |
| p002 | §4.1 の門（Reno）と §5.1 の模型の拡張（bottleneck・2 本目・上限・新しい check・算法に依る期待の一覧）。tcp-model の全 scenario と VF2 の LAN の非退行 | p001 |
| p003 | §4.2 の口（sockopt・sysctl・UAPI・net.tcpstats と読み手）、§4.3・4.4 の CUBIC、§5.1 の cc-*・fair-* の scenario、tcp-perf の `--cc`（試験用）。build の warning 0、qemu-tcp の非退行 | p002 |
| p004 | 実機の測りと既定の判定（§5.2・5.3） | p003 |
| p005 | 規約の全文の見直し | p003（p004 の後） |

## 8. 問い（推し付き、ユーザーか Q1 の判断）

| # | 問い | 推し |
| --- | --- | --- |
| Q1 | 接続ごとの口は FreeBSD・Linux と同じ `TCP_CONGESTION`（文字列、番号 64）でよいか | はい |
| Q2 | system の既定の sysctl の名前 | `net.tcp.congestion`（zedBSD の `net.tcpstats` に揃える） |
| Q3 | HyStart++ | この WS では入れない（RTT の標本が足りない、§4.5）。Future Work |
| Q4 | §4.1 の門を Reno にも入れるか（Reno の挙動が変わる） | 入れる（不具合の直し）。p002 で模型と VF2 の非退行を確かめ、その Reno を比べる基準にする |
| Q5 | 5330 の ingress に ifb と netem を run の間だけ（`modprobe ifb`・`tc`、終わりに消す） | 使う |
| Q6 | Linux CUBIC との取り合いの実機の試験 | p004 で試す。作れなければ model の fair-* だけ |
| Q7 | 判定の数値（§5.1 の表、§5.2 の 0.99・5 回の中央値） | この値 |
| Q8 | fast convergence | 入れる（§4.7 SHOULD） |
| Q9 | 同じ回復の中の 2 回目以降の timeout で ssthresh を変えない形を Reno にも入れるか | 入れない（Reno は今の挙動を保ち、比べる基準を動かさない）。CUBIC だけ |

## 9. review-1 への答え（2026-10-29、design-reviewer）

| # | 重さ | 答え |
| --- | --- | --- |
| F1 | 致命的 | §4.1 を round の状態に変えた。epoch のずらしは idle の後だけ（§4.3 step 2） |
| F2 | 致命的 | §5.1 の link を上限の中の BDP に、模型で上限を上げる、cwnd が律速だった run だけを判定に |
| F3 | 重大 | §3 に応答関数、§5.1 の表に域ごとの期待（Reno-friendly の域は ≥ 0.97） |
| F4 | 重大 | §5.1 に drop-tail の段と 2 本目の接続を確定として書いた |
| F5 | 重大 | HyStart++ を外した（§4.5、Q3） |
| F6 | 重大 | §4.3 の timeout の 1 回目と 2 回目以降、`cubic_after_timeout`、Q9 |
| F7 | 中 | §4.3 step 1・3・4 に計算の順と幅 |
| F8 | 中 | `kern_ticks_to_ms`、epoch の flag、srtt の 0 |
| F9 | 中 | §4.1 の送り済みの flight、印を立てる所を 2 つに |
| F10 | 中 | §4.4 の一覧 |
| F11 | 中 | 状態を byte に、MSS の変化で epoch を始め直す |
| F12 | 中 | §4.2 の 4 つの uint32（328 byte）、HAVE の bit、読み手を同じ commit で |
| F13 | 中 | §5.2 の ifb、zedBSD が送る向きだけを判定 |
| F14 | 中 | §5.1 の step の scenario、Jain の区間、§5.2 の 5 回の中央値 |
| F15 | 中 | §5.1 の新しい check と算法に依る期待の一覧 |
| F16 | 軽微 | §3・§4.3 step 5 で W_cubic(t) と W_cubic(t + RTT) を分けた |
| F17 | 軽微 | §4.3 の状態の一覧 |
| F18 | 軽微 | §4.2 の長さの規則、戻り値は p003 で FreeBSD を読んで揃える |
| F19 | 軽微 | §4.2 の enum・atomic、`leaves[]`、子が継ぐ |
| F20 | 軽微 | 依存と BDP の数を直した（ws.md も同じ commit で） |
| F21 | 軽微 | §6 |
| F22 | 軽微 | §7 で門と模型を p002 に分けた |
