<!-- awesome-plan project=zedbsd record=ws311-p001 -->
# ws311-p001: CUBIC の調べと設計

Status: cleared（2026-10-29 P3 q1673-i01: 設計の第 3 版。review-2（致命的 1・重大 4・中 6・軽微 8）を §10 で答えた。S4・M1・L8 は Q1 の判断（ws.md）で答えた）
Parent: [WS311](../ws.md)
Queue: q1664 / q1664-i05（P3）→ q1673 / q1673-i01（P3、review-2 から）
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
| RTT の標本 | 1 RTT に 1 segment（Karn）、tick 単位（`KERN_CLOCK_HZ`、多くは 1000、ppc・m68k・i386・sparcv9 は 100） |

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
- 第 3 版（p002 の実装に合わせた、review-2 L8）: flight は device が受け取った後に数える（`tcp_send_unsent` と書き手の owner の送りの後、拒まれた segment は数えない）。`cwnd_blocked` は `tcp_unsent_fits_cwnd_locked` が止めた時だけ立てる。書く時の門の断り（in_flight + len > cwnd）は送り済みの flight が cwnd − len を越えた後にしか起きず、CA の条件（max ≥ cwnd − MSS）と slow start の条件（cwnd ≥ 2 MSS なら cwnd < 2 × max）に含まれるので印は要らない。round を始め直す所（§4.4）は round の最大を今の送り済みの flight で始める（go-back-N の直後の 1 MSS で門が閉じない）。
- 予想: LAN（相手の窓が律速）で cwnd ≈ 2 × flight で止まる。速さは変わらない。Reno の挙動の変更なので、比べる基準の「Reno」はこの門を入れた後の Reno とする（review F22）。
- 出典: Linux の `tcp_is_cwnd_limited`（最大の flight の 2 倍の規則）の考え方を参考にする。code・名前・構造は写さない（guardrail「出典」、comment は `Procedure referenced:`）。

### 4.2 選び方の口

- 接続ごと: `setsockopt(IPPROTO_TCP, TCP_CONGESTION, name, len)`・`getsockopt`。名前（`"reno"`・`"cubic"`）は 1〜`TCP_CA_NAME_MAX`（16）− 1 byte、NUL は有っても無くてもよい、空・長すぎは EINVAL、知らない名前の戻り値は p003 で FreeBSD の source を読んで揃える（第 1 版の ENOENT は未確認、review F18 は ESRCH と言う、これも未確認）。get は FreeBSD の形に揃える（p003 で FreeBSD の source を読んで確かめる。review-2 L5: Linux も FreeBSD も min(len, 16) byte を写し NUL を保証しないと言う、未確認）。番号は FreeBSD の 64。今の `tcp_setsockopt`・`tcp_getsockopt` は int の option の形なので、文字列の option の分岐を足す。どの state でも変えてよく、変えた時は §4.4 の「空にする」をする（cwnd・ssthresh は保つ）。
- system の既定: sysctl `net.tcp.congestion`（読み書きは文字列、superuser だけが書く。kernel の中は enum の 1 語、atomic で読む、review F19）。`tcp_socket_create` が読む。listen の子は `tcp_passive_syn` の中で listener の値を継ぐ（今の code に無い、足す）。sysctl.c の `leaves[]` と userland の `sysctl` の一覧に足す。3 段の名前なので uapi/sysctl.h に NET_TCP とその下の番号を足し、userland の `sysctl` に文字列を書く分岐と表示の分岐を足す（今の `set_name` は数だけ、review-2 L6）。include/libc/netinet/tcp.h に `TCP_CONGESTION`・`TCP_CA_NAME_MAX`。build の既定 `CONFIG_TCP_CC_DEFAULT`（p003 では reno、p004 の判定で決める）。
- net.tcpstats の entry: uint32 を 4 つ（`cc`・`cubic_w_max_kb`・`cubic_k_ms`・`cwnd_state`（§4.1 の bit））足して 312 → 328 byte（全 arch で 328、static assert を直す）、`TCP_STATS_HAVE_CC` の bit。読み手（userland の sysctl は `element_size != sizeof` で拒む、tcp-model の model.c、plan/tools の解析）を同じ commit で直す（review F12）。

### 4.3 CUBIC の状態と計算

状態（接続ごと、byte と ms、review F11・F17）: `cubic_w_max`・`cubic_cwnd_prior`・`cubic_w_est`・`cubic_origin`（byte）、`cubic_epoch_started`（flag）・`cubic_epoch_ms`・`cubic_k_ms`・`cubic_limited_since_ms`、`cubic_after_timeout`（flag）、`cubic_est_rem`・`cubic_grow_rem`（端数）。

時間: `sched_ticks()` を ms へ `kern_ticks_to_ms`（`KERN_CLOCK_HZ` で換算、HAL は変えない）。srtt は `rtt_samples` が 0 なら 0 として使う（review F8）。

ACK（CA、`tcp_cubic_grow_locked`、§4.1 の門を通った時だけ）:
1. epoch が無ければ始める: epoch = 今、W_est = cwnd、`cubic_est_rem` = 0。`cubic_after_timeout` なら K = 0、origin = W_max = cwnd（§4.8、review F6）。そうでなく cwnd < W_max なら K = ∛((W_max − cwnd) × 2.5e9 / MSS) ms（掛けてから割る、ΔW ≤ 2³⁰ で約 2.7e18 が uint64 に入る）、origin = W_max。他は K = 0、origin = cwnd。
2. app-limited の間（review-2 S2、RFC 9438 §4.2 の MUST NOT・§5.8）: §4.1 の門が伸びを断った ACK で `cubic_limited_since_ms`（断り始めの時刻、0 は無し）を立てる。次に門を通る ACK で、断っていた時間（今 − その時刻）だけ epoch を後ろへずらし、印を消す。毎 ACK に少しずつずらすのではなく、断っていた区間をまとめてずらす（review F1）。`cubic_last_ack_ms` は使わない（状態の一覧から外す）。
3. W_cubic(τ)（byte）: Δ = |τ − K|（ms、2²⁰ に抑える）、off = (Δ³ / 2 500 000) × MSS / 1000（milli-segment を経て byte、途中は約 3e16 で uint64 に入る。C = 0.4 = 1/2.5 と ms³ → s³ の 1e9 をまとめた、review F7）、W = origin ± off（τ < K なら引く、0 未満にしない）。W は `TCP_CWND_MAX` で抑える。
4. target = W_cubic(t + srtt) を [cwnd, 1.5 cwnd] に。`cwnd_acked` += (acked × (target − cwnd) + `cubic_grow_rem`) / cwnd（64 bit）、余りは `cubic_grow_rem`（review-2 L2）、MSS 溜まるごとに cwnd += MSS。
5. Reno-friendly: W_est += (529 × acked × MSS + `cubic_est_rem`) / (1000 × cwnd)、余りは `cubic_est_rem`（W_est ≥ cwnd_prior の後は α = 1/1）。**W_cubic(t)**（RTT を足さない、review F16）< W_est なら cwnd = W_est。

減らす（`tcp_cwnd_loss_locked` の CUBIC の側）:
- fast retransmit: W_max = cwnd < W_max ? cwnd × (1 + β)/2 : cwnd（fast convergence、§8 Q8）、cwnd_prior = cwnd、ssthresh = max(flight × 7 / 10, 2 MSS)（64 bit）、cwnd = ssthresh、epoch を空に。
- timeout の最初の 1 回: 判定は呼び手が引数で渡す（`first`: 呼ぶ時点の `recovery_by_timeout == 0`。tcp.c の timer は `rto_backoff++` を loss の処理の前にするので rto_backoff は使えない、review-2 S1）。上と同じ W_max・ssthresh、cwnd = 1 MSS、`cubic_after_timeout` = 1。同じ回復の中の 2 回目以降の timeout は ssthresh・W_max・cwnd_prior を変えず cwnd = 1 MSS だけ（review F6）。Reno の側は §8 Q9（review-2 M1 で Q1 に再度の判断を頼んだ）。
- `cubic_after_timeout` の寿命（review-2 M6）: 回復の外（`recovery == 0`）で始めた最初の epoch が使って消す。timeout の go-back-N の中で ssthresh に届いて CA の伸びになっても、その epoch は回復の終わりで空にする（§4.4）ので印は残る。timeout の後の slow start の中で fast retransmit があれば、その損失の処理が W_max を決め直し、印を消す。
- 立方根: 64 bit の整数の二分探索（K の計算は損失ごとに 1 回）。浮動小数は使わない。

### 4.4 空にする所（review F10・F11）

CUBIC の epoch・W_est と端数・§4.1 の round の状態（算法の切り替えでは `cwnd_acked` も、review-2 L3）を空にする所: cwnd を CC の外で変える所（recovery の終わりの cwnd = min(cwnd, ssthresh)、go-back-N の開始）、算法の切り替え、send_mss の変化（Packet Too Big、`tcp_start_sending`）、接続の始まり（`tcp_start_sending`・`tcp_rtt_forget_locked`）。W_max・cwnd_prior は byte なので MSS の変化の後も使えるが、epoch は始め直す。

### 4.5 入れないこと

- HyStart++（review F5）: RFC 9406 は round に 8 個以上の RTT の標本が要るが、今の tcp.c は 1 RTT に 1 標本・tick の分解能（100 Hz の機械は 10 ms）。足すには `struct tcp_pending` に送った時刻（256 entry × 8 byte、socket ごと）と ACK ごとの標本が要る。推し: この WS では入れず Future Work（RFC 9438 §4.10 は SHOULD、Reno の slow start は MAY）。§8 Q3。
- 偽の輻輳の戻し（§4.9 MAY）: 検出の手段（timestamp・DSACK）が無い。
- ECN、他の算法。

## 5. 試験と判定（review F2・F3・F4・F13・F14・F15）

### 5.1 模型（plan/ws220/tests/tcp-model）

足す物:
- bottleneck の段: wire（送り手の速さ、`--mbps`、bottleneck より速く）→ byte で数える drop-tail の queue（`--bottleneck-queue`、溢れは loss に数え、packet は copy で持ち pool を使わない）→ bottleneck の link（`--bottleneck-mbps`）→ 遅延（review-2 L1）。今の `--queue` は device の拒み（ENOBUFS、TCP が送り直す）で輻輳の信号にならないので別の物（review F4）。
- 2 本目の接続: A → B の別の port、開始の時刻のずらし（`--second-flow-at-ms`）、算法（`--second-flow-cc`）。host は A・B のまま。
- 上限の引き上げ（review-2 C1）: 受け手 B（zedBSD）の窓は SO_RCVBUF でなく受けの列の packet の数（`TCP_RECEIVE_PACKETS` 128 × 1024 byte）と reorder の列（`TCP_REORDER_MAX` 128）で 128 KB に抑えられる。model の 2 つ目の build だけで `CONFIG_TCP_SEND_QUEUE_MAX`・`CONFIG_TCP_RECEIVE_PACKETS`・`CONFIG_TCP_REORDER_MAX`（どれも未定義なら kernel の既定のまま）を 1024 に上げ、SO_SNDBUF・SO_RCVBUF（`--send-buffer`・`--receive-buffer`）を 1 MB にする。pool（1024、HOLD の予約 256）は A と B が分け合うので、BDP + queue を約 450 segment 以下にする。
- 「cwnd が律速」の割合（review F2・review-2 M2）: 送り済みの flight が cwnd の 1 MSS の中にあり、cwnd < 相手の窓の時間の割合（§4.1 の門と同じ物差し。kernel の書き手の低水位による 0.75〜1 cwnd の揺れは模型の書き手には無い）。cc-20ms・cc-100ms・cc-step では ≥ 80 % を期待に入れ、届かない run は FAIL（試験の不備）とする。cc-lan と fair-* には当てない（相手の窓・link が律速なのが正常）。
- 新しい check: 終わりの cwnd ≤ max(2 × 最大の送り済み flight + 2 MSS、IW)（p011 の不具合を捕まえる。2 MSS は slow start の 1 ACK の伸び、review-2 L4）、slow start が cwnd に律速される間は 2 RTT 以内に倍になる（`--expect-slow-start`）、CA の伸び（`--expect-ca-growth N`、8 RTT 以上で 80 % 以上 cwnd 律速の区間ごと）。CA の伸びは Reno だけに 0.8 MSS/RTT を当て、CUBIC は算法の期待（下の軌跡の check）で見る（W_est は約 0.53 MSS/RTT、W_max の近くはほぼ 0、review-2 M5）。
- CUBIC の計算の host 単体の試験（review-2 S3(c)）: K・W_cubic・W_est の整数の計算を double の参照と格子（W_max 4〜8000 segment、MSS 536〜8960、t 0〜200 s）で比べ、W の差 ≤ 1 MSS、K の差 ≤ 1 ms。模型の軌跡の check: 損失の無い step の後、t = K で cwnd が W_max の ±5 % に入る（`cc-100ms-step`）。
- 所見（2026-10-29 P3、模型の 20 ms・100 Mbps・queue = BDP、Reno）: SACK が無いので、始まりの slow start の行き過ぎで drop-tail が数百 segment を落とすと、NewReno が 1 RTT に 1 個ずつ直して約 10 秒ほぼ止まる（両算法に同じに効く。HyStart++ も SACK も範囲の外、Q1 が Future Work の候補にする）。判定は始まりの 20 秒を除いて数える。
- 既存の scenario の期待のうち算法に依る物（`--expect-fast-retransmits`・`--expect-retransmits-max`・`--expect-timeouts-max`・`--expect-segments-*`）を p002 の初めに一覧にし、CUBIC の run では理由を書いて別の期待にする（黙って緩めない）。

scenario（各 60 秒以上、ユーザーの 2026-10-13 の決定）と、RFC の応答関数からの期待（第 2 版の表。review-2 S3 により p003 の初めに改める: 期待を上限（BDP + queue・rwnd・ring・pool）込みの見積りで scenario ごとに数値で書く、run は virtual time で 300〜600 秒、種を 5 つ以上にして中央値、始まりの 20 秒を除く。CUBIC の域の組は「Reno の窓 ≪ BDP ≤ CUBIC の窓」、例: RTT 100 ms・BDP 500 segment・queue 50 segment・p = 2e-5 で Reno 約 273、CUBIC 約 627 segment（review-2 の見積り、未確認）。cc-100ms-loss の 1.2 は上限 642 segment で約 1.15 しか出ない見込み）:

| 名前 | link | 期待（CUBIC / Reno） |
| --- | --- | --- |
| `cc-lan` | 1000 Mbps、片道 0.05 ms、損失 0 | 同じ（≥ 0.99） |
| `cc-20ms-loss` | 100 Mbps、片道 10 ms（BDP 250 KB）、queue = BDP、損失 0.01 % | Reno-friendly の域: ≥ 0.97 |
| `cc-100ms-loss` | 50 Mbps、片道 50 ms（BDP 625 KB）、queue = BDP/2、損失 0.001 % | CUBIC の域: ≥ 1.2 |
| `cc-100ms-step` | 同じ link、損失 0、定常の後に `--drop-ab` で 1 つ落とす | 落とした後に定常の 90 % へ戻る時間 CUBIC < Reno |
| `fair-cubic-cubic` | `cc-20ms-loss` の link、2 本目は 5 秒後 | 10〜60 秒の 2 本の平均の速さの Jain の指数 ≥ 0.95（区間の平均でとる、毎秒の指数の平均は参考、review-2 L7） |
| `fair-cubic-reno` | 同じ | Reno の取り分 ≥ 25 % |

### 5.2 実機（p004）

- VF2 ↔ 5330（1GbE LAN）: Reno（門の後）と CUBIC、両向き、1500・9000、IPv4・IPv6、各 30 秒 × 5、中央値。基準（片側、review-2 M3）: CUBIC の中央値 ≥ Reno の中央値 − max(1 %、Reno の 5 回の幅)。
- 遅延の経路（Q5 で承認）: 5330 の ingress を ifb に回して netem。zedBSD が送る向きだけを判定に使う（egress の qdisc は ACK にしか掛からない、review F13）。netem の `limit` は BDP 程度の packet の数を明示する（既定 1000 は約 6 BDP、review-2 M4）。run の間は 5330 の NIC の `ethtool -K gro off lro off`（ifb は GRO の後に掛かり、損失が塊で当たる。RTL8156 の driver が GRO をするかは未確認）、終わりに元に戻す。始めに残りの qdisc が無いことを確かめ、失敗・中断の時も `trap EXIT` で qdisc と ifb を消し、`tc qdisc show`・`ip link show ifb0` の読み戻しを summary に。
  - 経路 1（Reno-friendly の域）: delay 20 ms・loss 0.01 %・rate 100mbit。基準: CUBIC ≥ Reno × 0.97。
  - 経路 2（CUBIC の域、review-2 S4・Q1 の判断 2026-10-29）: delay 80 ms・rate 35 mbit・loss 0.004 %・limit ≈ BDP、300 秒 × 5（送りの ring 374 KB の中、見積りで Reno 約 193・CUBIC 約 315 segment）。CUBIC が有意に良いかを測る。
- Linux CUBIC との取り合い（§8 Q6）: 同じ netem の bottleneck へ VF2 と centris が同時に 5330 へ送る形。centris と 5330 の配線（同じ switch か）は未確認。作れなければ model の fair-* だけで判定。
- 10GbE（WS221 p010 の後）: p010 が終わっていれば Reno・CUBIC を比べる。終わっていなければ記録して後に回す。

### 5.3 既定の切り替えの判定（p004）

条件（Q1 の判断 2026-10-29、ユーザーの原文「性能がよくなったらCUBICに切り替える」に合わせる、review-2 S4）: **どこかで有意に良く、どこでも悪くない**。
- 「有意に良い」: 実機の経路 2 で CUBIC の 5 回の中央値が Reno の中央値を Reno の 5 回の幅より大きく上回る。
- 「悪くない」: 5.1 の全部の期待、5.2 の LAN と経路 1 の基準。
- 満たす → `CONFIG_TCP_CC_DEFAULT` を cubic に。実機で良くなったと示せず模型だけで良い時は切り替えず、値を揃えて Q1 に返す（Q1 がユーザーに聞く）。悪い所があれば reno のまま、値と理由を ws.md に。

## 6. 出典と書き方（review F21）

- RFC の定数は `Value referenced: RFC 9438 section 4.6` などをその block に。
- Linux・FreeBSD の手順を参考にする block は複数行の `Procedure referenced:`、code・名前・構造は写さない。
- source の comment・log に WS・Phase・Bug の番号を書かない。

## 7. Phase（ws.md の案を改める、review F22）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p001 | この設計 → design-reviewer | — |
| p002 | §4.1 の門（Reno）、Reno の 2 回目の timeout で ssthresh を保つ（Q9 の改め）と §5.1 の模型の拡張（bottleneck・2 本目・上限・新しい check・算法に依る期待の一覧）。tcp-model の全 scenario と VF2 の LAN の非退行 | p001 |
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
| Q9 | 同じ回復の中の 2 回目以降の timeout で ssthresh を変えない形を Reno にも入れるか | 第 2 版の推しは「CUBIC だけ」。review-2 M1（RFC 5681 §3.1「the value of ssthresh is held constant」）で改め、Q1 の判断（2026-10-29）で Reno にも p002 で入れる |

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

## 10. review-2 への答え（2026-10-29 P3 q1673-i01、design-reviewer）

| # | 重さ | 答え |
| --- | --- | --- |
| C1 | 致命的 | §5.1: 受けの packet の数・reorder の列の上限を model の build だけで上げる CONFIG の knob（kernel の既定は不変）、pool の分け合いの上限。p002 で 20 ms・100 Mbps・queue = BDP の Reno が cwnd 律速 100 % で 11.1 MB/s（link の 93 %）を確かめた |
| S1 | 重大 | §4.3: 最初の timeout の判定を呼び手の引数（呼ぶ時点の `recovery_by_timeout == 0`）に |
| S2 | 重大 | §4.3 step 2: 門が断った区間をまとめて epoch からずらす。§5.1 に書き手の休み → 再開の scenario を p003 で足す |
| S3 | 重大 | §5.1: 期待の数値を上限込みの見積りで p003 の初めに改める、長い run・複数の種・始まりを除く、整数の計算の host 単体試験、t = K の軌跡の check |
| S4 | 重大 | §5.2 経路 2・§5.3 の条件（Q1 の判断） |
| M1 | 中 | Q9 を改め、Reno にも p002 で入れる（Q1 の判断） |
| M2 | 中 | §5.1 の割合の定義と、届かない run は FAIL、当てる scenario を限る |
| M3 | 中 | §5.2 の片側の基準 |
| M4 | 中 | §5.2 の netem の limit・GRO・trap |
| M5 | 中 | §5.1: CA の伸びの check は Reno だけ |
| M6 | 中 | §4.3 の `cubic_after_timeout` の寿命 |
| L1 | 軽微 | §5.1 の bottleneck の速さ |
| L2 | 軽微 | §4.3 step 4 の `cubic_grow_rem` |
| L3 | 軽微 | §4.4 の `cwnd_acked` |
| L4 | 軽微 | §5.1 の check の 2 MSS の許し（と IW） |
| L5 | 軽微 | §4.2 get は FreeBSD の形（p003 で source を読む） |
| L6 | 軽微 | §4.2 の NET_TCP の番号・userland の文字列の分岐・libc の header |
| L7 | 軽微 | §5.1 の Jain は区間の平均 |
| L8 | 軽微 | §2 の 100 Hz の arch、§4.1 の第 3 版の注（`cwnd_blocked`）、review-2 と実装の順（門の実装は review-2 と並んで始めた、review の答えで変わった所は無い）。tcp-model は plan/ws220/tests のまま直す（Q1 の判断 2026-10-29） |
