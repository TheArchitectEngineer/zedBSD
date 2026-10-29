<!-- awesome-plan project=zedbsd record=ws311-p001 -->
# ws311-p001: CUBIC の調べと設計

Status: in-progress（2026-10-29 P3 q1664-i05、設計の第 1 版、design-reviewer の前）
Parent: [WS311](../ws.md)
Queue: q1664 / q1664-i05（P3）
依存: WS221 p008・p009（cleared）

## 1. 由来と目標

ユーザー（原文、2026-10-29）:「なるほど。みんなCUBICに帯域を奪われて、たまりかねたんですかね……。CUBICを実装して、性能がよくなったらCUBICに切り替えるWSを作って、取り組んでほしいです。jumbo frameの続きでやってほしいです。」

ws.md の目標（Q1 の解釈）: CUBIC（RFC 9438）を今の Reno 型と並べて実装し、接続ごと・system の既定を選べるようにする。同じ条件で測り、良ければ既定を CUBIC に切り替え、良くなければ切り替えず値と理由を残す。

Q1 の指示（2026-10-29）: WS221 p011 で見つけた「cwnd が送りの窓に抑えられない時も増え続ける」件をこの p001 で扱う。

## 2. 今の tcp.c の輻輳の制御（読んだ所）

| 所 | 今の形 |
| --- | --- |
| 状態（`include/kern/net/tcp-socket.h`） | `cwnd`・`ssthresh`・`cwnd_acked`（byte）、`recovery`・`recover`・`recovery_by_timeout`、`send_max` |
| 始まり（`tcp_start_sending`） | IW = min(10 × MSS, 14600)（RFC 6928）、ssthresh = 無限 |
| 増やす（`tcp_cwnd_grow_locked`、ACK ごと、recovery の外と timeout の go-back-N の間） | slow start: acked（1 ACK に最大 2 MSS、RFC 3465 の L=2）。CA: 窓 1 つ分の ACK ごとに 1 MSS |
| 減らす（`tcp_cwnd_loss_locked`） | ssthresh = max(flight / 2, 2 MSS)（flight = 送った端 − una）。fast retransmit: cwnd = ssthresh。timeout: cwnd = 1 MSS |
| recovery の終わり（ACK の処理） | cwnd = min(cwnd, ssthresh)（RFC 6582 §3.2 step 3） |
| 門 | 送りの量 = min(相手の窓, cwnd)（`tcp_send_limit_locked`）、unsent の送り直しは `tcp_unsent_fits_cwnd_locked` |
| 上限 | `TCP_CWND_MAX` 0x40000000 |

**見つけた不具合（WS221 p011）**: CA でも slow start でも、ACK が来れば cwnd が増える。LAN では相手の窓（約 500 KB）が送りを抑えるので cwnd は使われないまま 9 億 byte 台まで増える（VF2 の net.tcpstats の samples）。害: (a) 窓が開いた瞬間（相手の窓の拡大、別の流れが去った時）に cwnd の分を一度に送る burst、(b) CUBIC では W_max・K が意味を失う、(c) RFC 9438 §5.8・RFC 7661 に反する。fast retransmit の後は ssthresh が flight から決まるので回復の後は正気に戻るが、損失の無い間は無制限。

## 3. CUBIC（RFC 9438）の要点（2026-10-29 に rfc-editor.org の本文を読んだ）

- 定数: β_cubic = 0.7（§4.6 SHOULD）、C = 0.4（§5.1 SHOULD）、α_cubic = 3(1 − β)/(1 + β)（≈ 0.529）、W_est ≥ cwnd_prior の後は α = 1（§4.3）。
- 窓の関数: W_cubic(t) = C (t − K)³ + W_max、K = ∛((W_max − cwnd_epoch) / C)、t は CA の段の始まりからの秒。**app-limited の間は t に数えない**（§4.2 MUST NOT）。
- target = W_cubic(t + RTT) を [cwnd, 1.5 cwnd] に clamp（§4.2）、ACK ごとに cwnd += (target − cwnd) / cwnd（segment の単位、§4.4・4.5）。
- Reno-friendly: W_est = cwnd_epoch から始め、ACK ごとに W_est += α × acked / cwnd。W_cubic(t) < W_est なら cwnd = W_est（§4.3）。
- 減らす: ssthresh = flight_size × β（§4.6、cwnd で計る実装は他の手段で cwnd の伸びを止める MUST）、cwnd = max(ssthresh, 2 MSS)（fast retransmit）。
- fast convergence（§4.7 SHOULD）: cwnd < W_max なら W_max = cwnd × (1 + β) / 2、他は W_max = cwnd。
- timeout（§4.8）: Reno と同じく cwnd = 1 MSS、ssthresh は β で。次の CA で K = 0、W_max = その段の始まりの cwnd。
- 偽の輻輳（§4.9）: F-RTO・偽の fast retransmit の戻しは MAY。
- slow start（§4.10）: HyStart++（RFC 9406）SHOULD、Reno の slow start は MAY。
- app-limited（§5.8）: app-limited・rwnd-limited の流れは cwnd を増やさない。

## 4. 設計

### 4.1 選び方の口

- 接続ごと: `setsockopt(IPPROTO_TCP, TCP_CONGESTION, name, len)`・`getsockopt`。名前の文字列（`"reno"`・`"cubic"`、最長 `TCP_CA_NAME_MAX` 16、NUL まで）。FreeBSD と Linux が同じ名前の option を文字列で持つ。番号は FreeBSD の 64（zedBSD の netinet.h は `TCP_MAXUNACKTIME` 68 で FreeBSD の番号に揃えている）。知らない名前は ENOENT（FreeBSD・Linux と同じ）。どの state でも変えてよく、変えた時は CUBIC の段の状態（W_max・epoch・W_est）を空にし、cwnd・ssthresh は保つ（Linux の `tcp_set_congestion_control` と同じ）。
- system の既定: sysctl `net.tcp.congestion`（文字列、superuser だけが書く、`kern.hostname` の書ける leaf と同じ形）。新しい socket が socket を作る時に読む（listen の子は listener のを継ぐ）。build の既定は `CONFIG_TCP_CC_DEFAULT`（p002 では reno、p003 の判定で cubic に変えるかを決める）。
- UAPI: `include/uapi/netinet.h` に `TCP_CONGESTION`・`TCP_CA_NAME_MAX`、`include/uapi/sysctl.h` に leaf の番号。net.tcpstats の entry に `cc`（0 reno・1 cubic）と CUBIC の `w_max`・`k_ms` を足す（entry は struct_size で読むので末尾に足す、WS220 の UAPI の規則）。

### 4.2 tcp.c の形

- 接続ごとに `cc`（enum）と CUBIC の状態: `cubic_w_max`・`cubic_cwnd_prior`（byte）、`cubic_epoch_ms`（0 = 段が始まっていない）、`cubic_k_ms`、`cubic_origin`（W_max か cwnd、K = 0 の時）、`cubic_w_est`（byte）、`cubic_acked`（W_est の端数）。
- 3 つの口の振り分け（関数の pointer の表は作らない: 2 つだけ、switch で十分、規約の「debugger で止まれる」）:
  - `tcp_cwnd_grow_locked`: slow start は今のまま（HyStart++ は §4.5）。CA は `cc` で Reno か `tcp_cubic_grow_locked`。
  - `tcp_cwnd_loss_locked`: Reno は今のまま。CUBIC は fast convergence で W_max、cwnd_prior = cwnd、ssthresh = max(flight × 7 / 10, 2 MSS)、fast retransmit は cwnd = ssthresh、timeout は cwnd = 1 MSS。epoch を 0 に。
  - recovery の終わり: 今のまま cwnd = min(cwnd, ssthresh)。CUBIC の epoch は次の CA の ACK で始まる。
- CUBIC の ACK（`tcp_cubic_grow_locked`、byte を MSS で割って segment の固定小数で）:
  1. epoch が 0 なら始める: epoch = 今、cwnd_epoch = cwnd、W_est = cwnd。cwnd < W_max なら K = ∛((W_max − cwnd) / C)、origin = W_max。そうでなければ K = 0、origin = cwnd。
  2. t = 今 − epoch + srtt（target は t + RTT）。W_cubic = origin + C (t − K)³（segment、t は ms、C(t − K)³ = 0.4 × Δ³ / 10⁹ segment、Δ = |t − K| ms、Δ は 2²¹ ms（約 35 分）で抑える = 64 bit で溢れない）。
  3. target を [cwnd, 1.5 cwnd] に clamp。cwnd_acked に acked × (target − cwnd) / cwnd の byte を足し、MSS 溜まるごとに cwnd += MSS（今の Reno の端数の数え方と同じ）。
  4. Reno-friendly: W_est += α × acked × MSS / cwnd（α は 529/1000、W_est ≥ cwnd_prior の後 1）。W_cubic < W_est なら cwnd = W_est。
- 立方根: 整数の ∛（64 bit、二分探索で 22 回、または Newton。K の計算は損失ごとに 1 回なので速さは要らない）。浮動小数は kernel で使わない。
- 時間: `sched_ticks()` の ms（hz 1000）。srtt は今の `srtt_us`。

### 4.3 cwnd を「使っている時だけ」増やす（Reno・CUBIC の両方、RFC 7661・RFC 9438 §5.8）

- 送りの側で印をつける: segment を送る時に、送った後の flight が cwnd − MSS 以上なら、または `tcp_unsent_fits_cwnd_locked` が cwnd で止めたら、`cwnd_limited = 1`（ACK の処理で読み、grow の後に 0 に戻す）。
- `tcp_cwnd_grow_locked` は `cwnd_limited` が立っている時だけ増やす。slow start も同じ（Linux の `tcp_is_cwnd_limited` は slow start で cwnd < 2 × 最大の flight なら許す: 同じ形にして、窓に抑えられた LAN で cwnd が flight の 2 倍を超えない）。
- CUBIC の t: app-limited の ACK の間は epoch を進める（epoch += 前の ACK からの時間、Linux の `bictcp_cwnd_event(CA_EVENT_TX_START)` に近い形）。
- 効き目（予想）: VF2 の LAN では cwnd ≈ 相手の窓 × 2 で止まる。速さは変わらない（窓で抑えられている）。tcp-model の全 scenario が通ることを p002 で確かめる（Reno の挙動が変わるので非退行が要る）。

### 4.4 選ばないこと

- 偽の輻輳の戻し（F-RTO、§4.9 MAY）: 入れない（zedBSD に timestamp・DSACK が無く、検出の手段が無い）。
- ECN: zedBSD は ECN を扱わない。
- BBR などの他の算法: 範囲の外。

### 4.5 HyStart++（RFC 9406、RFC 9438 §4.10 SHOULD）

案: p002 で CUBIC と一緒に入れる。round（ACK が round の始まりの send_next を越える）ごとの最小 RTT を比べ、RTT の増え（max(4 ms, min(16 ms, last/8))）で slow start を終えて CSS（Conservative Slow Start、増え方 1/4）に、CSS で RTT が戻れば slow start に戻る。要るのは RTT の標本（今の Karn の標本で足りる、round ごとに最小を取る）。LAN（RTT 0.1〜0.5 ms）では閾値 4 ms に届かず効かない。高い BDP の model の scenario で slow start の行き過ぎ（大きい burst loss → timeout）を減らす。Reno にも使える（Linux は CUBIC だけ、FreeBSD は newreno でも選べる）。推し: CUBIC の時だけ（判定の比較で Reno の挙動を変えないため）。

## 5. 試験と「良くなった」の基準

### 5.1 host の模型（plan/ws220/tests/tcp-model、p002）

- 足す: `--cc reno|cubic`（A の算法）、bottleneck の queue（今の `--queue` は device の queue: link の速さ・遅延と組み、drop-tail の bottleneck として使えるかを p002 の初めに確かめる。足りなければ link に byte の queue を足す）、2 本目の流れ（A2 → B2 が同じ link を通る、`--second-flow-cc`）。
- 新しい scenario（各 60 秒以上、ユーザーの 2026-10-13 の決定「60秒以上の転送も計測」）:
  - `bdp-20ms`: 1000 Mbps、片道 10 ms、queue = BDP（約 1.7 MB）、損失 0。
  - `bdp-20ms-loss`: 同じで 0.01 % の損失。
  - `bdp-100ms`: 1000 Mbps、片道 50 ms、queue = BDP/2。
  - `fair-cubic-cubic`・`fair-cubic-reno`: 2 本、bdp-20ms の link。
  - 既存の全 scenario を `--cc cubic` でも通す（LAN では差が出ないこと）。
- 基準（推し）: (a) bdp-20ms-loss と bdp-100ms で CUBIC の 60 秒の平均が Reno の 1.2 倍以上、(b) 損失の後に 90 % の速さへ戻るまでの時間が Reno より短い、(c) fair-cubic-cubic の Jain の指数 ≥ 0.95、(d) fair-cubic-reno で Reno が 25 % 以上を取る（CUBIC が Reno を潰さない）、(e) 既存の全 scenario PASS。

### 5.2 実機（p003）

- VF2 ↔ 5330（1GbE、LAN、RTT 約 0.15 ms）: Reno と CUBIC、両向き、1500 と 9000、IPv4 と IPv6、各 30 秒 × 3。基準: CUBIC が Reno の 99 % 以上（LAN では差が出ないはず、悪くならないこと）。
- 遅延と損失のある経路: 相手の Linux に `tc qdisc netem`（delay 20 ms・loss 0.01 %、終わりに消す、MTU と同じく run の中だけ）。5330 の qdisc を変えるのは Q1 の承認が要る（問い Q5）。基準: 5.1 の (a) と同じ 1.2 倍。
- Linux の CUBIC との取り合い: 同じ 1 Gbps の bottleneck を zedBSD と Linux が分ける形が今の機械の並びでは作りにくい（1 Gbps の port は VF2 だけ）。案: netem の rate 制限（`tc qdisc … netem rate 500mbit`）を 5330 の受けの側に置き、VF2（zedBSD CUBIC）と centris（Linux CUBIC）が 5330 へ同時に送る。問い Q6。
- 10GbE（10.0.10.3 の RTL8127、WS221 p010 の後）: p010 が終わっていれば p003 で Reno・CUBIC を比べる。終わっていなければ p003 の後に回し、記録する。
- 既定の切り替えの判定（p003）: 5.1 の (a)〜(e) と 5.2 の LAN の非退行を満たし、5.2 の遅延の経路（測れた時）で悪くない → `CONFIG_TCP_CC_DEFAULT` を cubic に。満たさない → reno のまま、値と理由を ws.md に。

## 6. Phase（ws.md の案を確定）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p001 | この設計 → design-reviewer | — |
| p002 | 実装: §4.1 の口（sockopt・sysctl・UAPI・net.tcpstats）、§4.2 の CUBIC、§4.3 の cwnd-limited の門（Reno も）、§4.5 の HyStart++（Q3 の答え次第）、tcp-model の §5.1 の拡張と scenario、userland の `sysctl` の表示と tcp-perf の `--cc`（接続ごとに選ぶ試験用）。build の warning 0、tcp-model PASS、QEMU の qemu-tcp の非退行 | p001 |
| p003 | 実機の測りと既定の判定（§5.2） | p002 |
| p004 | 規約の全文の見直し | p002（p003 の後） |

## 7. 問い（推し付き、ユーザーか Q1 の判断）

| # | 問い | 推し |
| --- | --- | --- |
| Q1 | 接続ごとの口は FreeBSD・Linux と同じ `TCP_CONGESTION`（文字列、番号は FreeBSD の 64）でよいか | はい |
| Q2 | system の既定の sysctl の名前 | `net.tcp.congestion`（zedBSD の `net.tcpstats` に合わせる。FreeBSD の `net.inet.tcp.cc.algorithm` に合わせる案も） |
| Q3 | HyStart++ を p002 で入れるか | 入れる、CUBIC の時だけ |
| Q4 | §4.3 の cwnd-limited の門を Reno にも入れるか（Reno の挙動が変わる） | 入れる（不具合の直し、RFC 7661）。tcp-model の全 scenario と VF2 の非退行で確かめる |
| Q5 | 実機の遅延の経路に 5330 の `tc qdisc netem` を run の間だけ使ってよいか | 使う（終わりに消し、消えたことを `tc qdisc show` で確かめる） |
| Q6 | Linux CUBIC との取り合いの実機の試験をするか（§5.2 の形） | p003 で試す。作れなければ model の fair-* だけで判定 |
| Q7 | 判定の数値（§5.1 の 1.2 倍、Jain 0.95、Reno 25 %、LAN 99 %） | この値 |
| Q8 | fast convergence | 入れる（§4.7 SHOULD、単一の流れでは効き目が小さいが害も小さい） |
