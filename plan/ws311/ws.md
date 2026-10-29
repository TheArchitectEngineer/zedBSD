<!-- awesome-plan project=zedbsd record=ws311 -->

# WS311: TCP の輻輳の制御に CUBIC を実装し、性能が良ければ既定を CUBIC に切り替える

Status: incomplete
Primary Milestone: MG005
Resume point: 2026-10-29 P3 q1673-i01: p001 cleared（第 3 版、review-2 に答えた）。p002 の門（tcp.c）と模型の拡張を実装中。

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
| [p002](phase002/phase.md) | cwnd を使っている時だけ増やす門（Reno も、WS221 p011 で見つけた不具合）、Reno の 2 回目の timeout で ssthresh を保つ（RFC 5681 §3.1）、tcp-model の拡張（drop-tail の bottleneck、2 本目の接続、上限、新しい check） | in-progress | p001 |
| p003 | CUBIC の実装（TCP_CONGESTION・sysctl・net.tcpstats の UAPI、cc-*・fair-* の scenario） | planning | p002 |
| p004 | 実機の測り（Reno と CUBIC、遅延の経路、取り合い）と既定の切り替えの判定 | planning | p003 |
| p005 | 規約の全文の見直し | planning | p003（p004 の後） |
