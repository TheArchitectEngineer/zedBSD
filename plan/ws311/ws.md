<!-- awesome-plan project=zedbsd record=ws311 -->

# WS311: TCP の輻輳の制御に CUBIC を実装し、性能が良ければ既定を CUBIC に切り替える

Status: incomplete（全 Phase cleared、WS の受け入れの確かめは Q1）
Primary Milestone: MG005
Resume point: 2026-10-29 P3 q1673-i01: p001〜p005 cleared。既定を CUBIC に切り替えた（p004 の判定）。残り: 下の「残り」。

## ユーザーの指示（原文、2026-10-29）

「なるほど。みんなCUBICに帯域を奪われて、たまりかねたんですかね……。CUBICを実装して、性能がよくなったらCUBICに切り替えるWSを作って、取り組んでほしいです。jumbo frameの続きでやってほしいです。」

（前の流れ: zedBSD の TCP は RFC 5681 の Reno 型、Linux・Windows・FreeBSD 14 以降の既定は CUBIC、と Q1 が説明した。）

## 目標（Q1 の解釈）

- zedBSD の TCP に CUBIC（RFC 9438）を、今の Reno 型と並べて実装し、接続ごと・system の既定を選べるようにする。
- Reno と CUBIC を同じ条件で測り（VF2 の 1GbE、10.0.10.3 の 10GbE、遅延と損失のある経路、CUBIC の相手との取り合い）、性能が良ければ既定を CUBIC に切り替える。良くなければ切り替えず、測った値と理由を残す。
- 順: WS221（Jumbo frame と TCP、p010 の 10GbE を含む）の続き。窓の前提（自分の window scale の広告、byte で数える受けの窓）は WS221 の p008・p009 の残りに依存。

## p001 の問いへの答え（2026-10-29 Q1 の判断。どれも範囲の中の技術の判断で、ユーザーの回答ではない）

- Q1 `TCP_CONGESTION`（文字列、64）: はい。Q2 sysctl `net.tcp.congestion`: はい。Q3 HyStart++: この WS では入れず Future Work。
- Q4 cwnd を使っている時だけ増やす門を Reno にも入れる: 入れる（不具合の直し、p002 で模型と VF2 の非退行）。Q9 2 回目以降の timeout で ssthresh を変えない形: 当初は CUBIC だけ → 2026-10-29 review-2 M1 の後の Q1 の判断で改めた:「Reno の同じ回復の中の 2 回目の timeout で ssthresh を保つ形を p002 で Reno にも入れる（RFC 5681 §3.1 からの逸脱の直し。Q9 の答えを改める。ws.md に記録）」。
- Q5 5330（Debian、TCP の相手）の ingress に `modprobe ifb`・`tc netem` を run の間だけ使い、終わりに消して元に戻す: 承認（Q1。5330 の MTU を run の間だけ変えてきたのと同じ扱い、flock /tmp/i915-hw.lock の中で）。
- Q6 Linux CUBIC との取り合い: p004 で試し、作れなければ模型の fair-* だけ。Q7 判定の数値: §5 のとおり。Q8 fast convergence: 入れる。
- review-2 は p002 の実装の前に流す。
- review-2 の後の Q1 の判断（2026-10-29、原文）: 「(1) 推しで良い。実機に CUBIC の域の経路（5330 の netem、delay 80 ms・rate 35 mbit・loss 0.004 %・limit ≈ BDP、300 秒 × 5）を足し、切り替えの条件は「どこかで有意に良く、どこでも悪くない」（ユーザーの原文「性能がよくなったらCUBICに切り替える」に合わせる）。実機で良くなったと示せず模型だけで良い時は、切り替えずに値を揃えて Q1 に返す（Q1 がユーザーに聞く）。(2) 推しで良い。Reno の同じ回復の中の 2 回目の timeout で ssthresh を保つ形を p002 で Reno にも入れる（…）。(3) plan/ws220/tests/tcp-model をそのまま直して良い。SACK が無い事の所見は phase.md に残して。Q1 が Future Work に候補として足す。」

## Phase（p001 の第 2 版で改めた、[phase001/phase.md](phase001/phase.md) §7）

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（RFC 9438、今の tcp.c の cwnd の形、選び方の口、測りの条件と「良くなった」の基準）→ design-reviewer | cleared（2026-10-29 P3 q1673-i01: 第 3 版、review-1・review-2 の全部に答えた、§10） | — |
| [p002](phase002/phase.md) | cwnd を使っている時だけ増やす門（Reno も、WS221 p011 で見つけた不具合）、Reno の 2 回目の timeout で ssthresh を保つ（RFC 5681 §3.1）、tcp-model の拡張（drop-tail の bottleneck、2 本目の接続、上限、新しい check） | cleared（2026-10-29 P3 q1673-i01: b2c86795a、tcp-model 全 PASS、5 platform warning 0、qemu-tcp PASS、VF2 実機 LAN 949 Mbps 非退行） | p001 |
| [p003](phase003/phase.md) | CUBIC の実装（TCP_CONGESTION・sysctl・net.tcpstats の UAPI、cc-*・fair-* の scenario） | cleared（2026-10-29 P3 q1673-i01: 128e60640、host 単体試験・tcp-model・cc-model・qemu-tcp PASS、5 platform warning 0、VF2 LAN 非退行。cc-100ms-step と書き手の休み → 再開の scenario は未実施） | p002 |
| [p004](phase004/phase.md) | 実機の測り（Reno と CUBIC、遅延の経路、取り合い）と既定の切り替えの判定 | cleared（2026-10-29 P3 q1673-i01: 実機の経路 2 で CUBIC 18.3 / Reno 15.8 Mbps（比 1.158、有意）、経路 1 は 1.036、LAN（1500・9000・IPv6）は同じ → 既定を cubic に。Linux との実機の取り合い・10GbE は未実施） | p003 |
| [p005](phase005/phase.md) | 規約の全文の見直し | cleared（2026-10-29 P3 q1673-i01: 9d4bad11a、conformance-reviewer、書き方だけ。tcp-model・host・qemu-tcp PASS、5 platform warning 0） | p003（p004 の後） |

## 結果（2026-10-29 P3 q1673-i01）

- 門（RFC 7661、Reno・CUBIC 共通）: 相手の窓が律速の LAN で cwnd が 9 億 byte 台へ伸びた不具合を直した（VF2 の cwnd の最大 749129、送りの ring の flight の 2 倍の中）。Reno の同じ回復の中の 2 回目以降の timeout で ssthresh を保つ（RFC 5681 §3.1）。
- CUBIC（RFC 9438、HyStart++ と偽の輻輳の戻しは無し）、`TCP_CONGESTION`（FreeBSD の 64）、`net.tcp.congestion`、net.tcpstats 328 byte（version 2、`TCP_STATS_HAVE_CC`）、tcp-perf `--cc`。
- 既定: CUBIC（p004 の判定、[phase004](phase004/phase.md)）。

## 残り（未実施、Q1 の判断に置く）

- 模型の cc-100ms-step（t = K で W_max の ±5 % の軌跡の check）と書き手の休み → 再開の scenario（p001 §5.1・review-2 S2）。
- Linux CUBIC との実機の取り合い（centris の配線・設定が要る、Q6）。模型の fair-* は PASS。
- 10GbE の比べ（WS221 p010 の後）。ring を広げる時は p004 §3 の所見（HyStart の無い slow start の行き過ぎ）を見直す。
- Future Work の候補（Q1 が F-WS311-sack に書く）: SACK、PRR（RFC 6937）または RFC 6582 §3.2 step 3 の選択肢 (1)、HyStart++。
