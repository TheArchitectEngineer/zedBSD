<!-- awesome-plan project=zedbsd record=ws311 -->

# WS311: TCP の輻輳の制御に CUBIC を実装し、性能が良ければ既定を CUBIC に切り替える

Status: completed（2026-10-29、Q1 の判定: 既定を CUBIC に切り替えた）
Primary Milestone: MG005

## ユーザーの指示（原文、2026-10-29）

「なるほど。みんなCUBICに帯域を奪われて、たまりかねたんですかね……。CUBICを実装して、性能がよくなったらCUBICに切り替えるWSを作って、取り組んでほしいです。jumbo frameの続きでやってほしいです。」

（前の流れ: zedBSD の TCP は RFC 5681 の Reno 型、Linux・Windows・FreeBSD 14 以降の既定は CUBIC、と Q1 が説明した。）

## 結果

- `src/kern/net/tcp-cubic.c`（新）と `tcp.c` の CUBIC（RFC 9438、固定小数、epoch・W_est・fast convergence・timeout の後 K=0）。選び方の口は `TCP_CONGESTION`（FreeBSD と同じ 64、名前 "reno"・"cubic"）と sysctl `net.tcp.congestion`（書くのは superuser）。`net.tcpstats` は 328 byte・version 2・`TCP_STATS_HAVE_CC`。既定 `CONFIG_TCP_CC_DEFAULT` は CUBIC。
- Reno も直した: cwnd を使っている時だけ増やす門（RFC 7661、WS221 p011 で見つけた cwnd が無制限に増える件）、同じ回復の中の 2 回目以降の timeout で ssthresh を保つ（RFC 5681 §3.1）。
- 判定（切り替えの条件「どこかで有意に良く、どこでも悪くない」、Q1 の判断、ユーザーの「性能がよくなったらCUBICに切り替える」）: 実機 VF2 → 5330 で、経路 2（netem 80 ms・35 Mbit・0.004 %）Reno 15.8 → CUBIC 18.3 Mbps（比 1.158、CUBIC の最小が Reno の最大を上回る）、経路 1（20 ms・100 Mbit・0.01 %）44.5 → 46.1（比 1.036）、LAN（IPv4 1500・9000、IPv6）は同じ（949.2・991.0・936）。模型（5 つの種）の公平さ Jain 0.998、Reno の取り分 0.473。5 platform の build warning 0、QEMU の qemu-tcp（inet・inet6）PASS。

## 制限・移管

- Linux CUBIC との実機の取り合い、10GbE の比べ（WS221 p010）、模型の cc-100ms-step と書き手の休み → 再開の scenario は未実施。
- 送りの ring を広げる時は、HyStart の無い slow start の行き過ぎ（ring 1024 の模型で 80 ms の経路の比 0.792）を見直す。
- SACK・PRR・HyStart++ は Future Work（F-WS311-sack）。
- 試験の道具は `plan/tools/tcp-cc/`（`cc-model.sh`・`vf2-cc.sh`・`run-cubic-host-test.sh`、[plan/tools.md](../tools.md) に登録）。模型は `plan/ws220/tests/tcp-model/`。

## p001 の問いへの答え（2026-10-29 Q1 の判断。どれも範囲の中の技術の判断で、ユーザーの回答ではない）

- Q1 `TCP_CONGESTION`（文字列、64）: はい。Q2 sysctl `net.tcp.congestion`: はい。Q3 HyStart++: この WS では入れず Future Work。
- Q4 cwnd を使っている時だけ増やす門を Reno にも入れる: 入れる（不具合の直し、p002 で模型と VF2 の非退行）。Q9 2 回目以降の timeout で ssthresh を変えない形: 当初は CUBIC だけ → 2026-10-29 review-2 M1 の後の Q1 の判断で改めた:「Reno の同じ回復の中の 2 回目の timeout で ssthresh を保つ形を p002 で Reno にも入れる（RFC 5681 §3.1 からの逸脱の直し。Q9 の答えを改める。ws.md に記録）」。
- Q5 5330（Debian、TCP の相手）の ingress に `modprobe ifb`・`tc netem` を run の間だけ使い、終わりに消して元に戻す: 承認（Q1。5330 の MTU を run の間だけ変えてきたのと同じ扱い、flock /tmp/i915-hw.lock の中で）。
- Q6 Linux CUBIC との取り合い: p004 で試し、作れなければ模型の fair-* だけ。Q7 判定の数値: §5 のとおり。Q8 fast convergence: 入れる。
- review-2 は p002 の実装の前に流す。
- review-2 の後の Q1 の判断（2026-10-29、原文）: 「(1) 推しで良い。実機に CUBIC の域の経路（5330 の netem、delay 80 ms・rate 35 mbit・loss 0.004 %・limit ≈ BDP、300 秒 × 5）を足し、切り替えの条件は「どこかで有意に良く、どこでも悪くない」（ユーザーの原文「性能がよくなったらCUBICに切り替える」に合わせる）。実機で良くなったと示せず模型だけで良い時は、切り替えずに値を揃えて Q1 に返す（Q1 がユーザーに聞く）。(2) 推しで良い。Reno の同じ回復の中の 2 回目の timeout で ssthresh を保つ形を p002 で Reno にも入れる（…）。(3) plan/ws220/tests/tcp-model をそのまま直して良い。SACK が無い事の所見は phase.md に残して。Q1 が Future Work に候補として足す。」


## Phase（全て cleared、記録は git の履歴に）

p001 調べと設計（review 2 回）→ p002 Reno の門と模型 → p003 CUBIC と UAPI → p004 実機の測りと既定の判定（切り替える）→ p005 規約の全文の見直し。
