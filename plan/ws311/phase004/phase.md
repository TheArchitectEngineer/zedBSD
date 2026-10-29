<!-- awesome-plan project=zedbsd record=ws311-p004 -->
# ws311-p004: 実機の測りと既定の判定

Status: cleared（2026-10-29 P3 q1673-i01: 判定は「切り替える」、既定を cubic にした tree で tcp-model・5 platform の build・QEMU・VF2 の LAN を確かめた）
Parent: [WS311](../ws.md)
Queue: q1673 / q1673-i01（P3）
依存: [p003](../phase003/phase.md)

## 1. 判定の条件（p001 §5.3、Q1 の判断 2026-10-29）

ユーザーの原文「性能がよくなったらCUBICに切り替える」→ Q1:「切り替えの条件は「どこかで有意に良く、どこでも悪くない」」。有意に良い = 実機の経路 2 で CUBIC の中央値が Reno の中央値を Reno の 5 回の幅より大きく上回る。悪くない = 模型の全部の期待、実機の LAN（CUBIC ≥ Reno − max(1 %、Reno の幅)）と経路 1（≥ 0.97）。

## 2. 実機（VF2 ↔ 5330、128e60640 の kernel、`plan/ws311/tests/vf2-cc.sh`、板が送る向き、`tcp-perf --cc`、各 run の途中の net.tcpstats で cc を確かめた）

| 経路 | 長さ × 回 | Reno 中央値（最小〜最大） | CUBIC 中央値（最小〜最大） | 比 | 基準 |
| --- | --- | --- | --- | --- | --- |
| LAN、IPv4、MTU 1500 | 30 秒 × 3 | 949.2（949.1〜949.2） | 949.2（949.1〜949.2） | 1.000 | 悪くない |
| LAN、IPv4、MTU 9000（両端、終わりに 1500 へ戻した） | 30 秒 × 5 | 991.0（990.8〜991.1） | 991.0（990.9〜991.1） | 1.000 | 悪くない |
| LAN、IPv6（5330 の global 240f:10c:ce32:1:4401:8787:b00c:e03a）、MTU 1500 | 30 秒 × 5 | 936.1（936.0〜936.1） | 935.9（935.8〜936.1） | 1.000 | 悪くない（差 0.2 < 1 %） |
| 経路 1: netem delay 20ms rate 100mbit loss 0.01% limit 171 | 60 秒 × 5 | 44.5（40.0〜48.2） | 46.1（45.1〜48.8） | 1.036 | ≥ 0.97 |
| 経路 2: netem delay 80ms rate 35mbit loss 0.004% limit 240 | 300 秒 × 5 | 15.8（14.7〜16.7） | 18.3（17.7〜19.9） | 1.158 | 有意に良い（CUBIC の最小 17.7 > Reno の最大 16.7、差 2.5 > Reno の幅 2.0） |

- netem は 5330 の ingress を ifb311 に回して掛けた（Q5 の承認）。run の間は 5330 の GRO を off。終わりの読み戻し（経路 1・2 とも）: `qdisc fq_codel 0: root …` だけ、ifb の device 0、module 0、`generic-receive-offload: on`（元どおり）。MTU 9000 の後は板 mtu 1500、5330 mtu 1500 を読み戻した。
- 板の受ける向きは 5330 の CC（Linux の cubic）が決めるので判定に使わない（vf2-tcp.sh の受けは 949.3 で非退行）。
- Linux CUBIC との実機の取り合い（p001 §5.2、Q6）は作っていない（centris の配線・設定の変更が要る、Q1 への問いは未）: 模型の fair-* だけ。
- 10GbE: WS221 p010 は planned（WS235 の後）なので測っていない。p010 の後に Reno・CUBIC を比べる（記録だけ）。
- 5330 に置いた物（Q1 が消す）: /tmp/vf2-tcp.sRaLIX、/tmp/vf2-cc.5TiM6c、/tmp/vf2-cc.s34kky、/tmp/vf2-cc.mDHGkV、/tmp/vf2-cc.S6hKyf、/tmp/vf2-cc.o8PS8C（と p004 の vf2-tcp.sh が作る 1 つ）。

## 3. 模型（`plan/ws311/tests/cc-model.sh`、5 つの種の中央値、300 秒、20 秒から）

| scenario | 結果 |
| --- | --- |
| cc-lan | 比 1.000（≥ 0.99） |
| cc-20ms-loss（100 Mbps、20 ms、queue = BDP、0.01 %） | 8.257 → 8.788 MB/s、比 1.064（≥ 0.97） |
| cc-80ms-loss（35 Mbps、80 ms、queue = BDP、0.004 %、big の build: 送りの ring 1024） | 3.133 → 2.480、比 0.792（記録） |
| 同じ経路、送りの ring を kernel の 256 のまま（受けの上限と pool だけ上げた build、build/ws311/host-ring256） | Reno 3.195、CUBIC 3.661、比 1.146（実機の経路 2 の 1.158 と合う） |
| fair-cubic-cubic | Jain の中央値 0.998（≥ 0.95） |
| fair-cubic-reno | Reno の取り分の中央値 0.473（≥ 0.25） |

cc-80ms-loss の big の build で CUBIC が悪いのは、ring 1024 で始まりの slow start が約 1 MB まで行き過ぎ（HyStart が無い）、SACK の無い NewReno の回復と回復の終わりの burst が重なるため（p003 §5）。kernel の ring 256（約 374 KB）では行き過ぎが BDP + queue の約 700 KB に届かず、模型も実機も CUBIC が 15 % 速い。ring を広げる時（WS221 p010 の 10GbE など）はこの所見を見直す（Future Work の F-WS311-sack の HyStart++・SACK・PRR）。

## 4. 判定と既定の切り替え

条件を満たした（経路 2 で有意に良く、LAN・経路 1・模型の期待のどれでも悪くない）→ `CONFIG_TCP_CC_DEFAULT` を `TCP_CC_CUBIC` にした（src/kern/net/tcp.c）。`sysctl net.tcp.congestion=reno` か `TCP_CONGESTION` で Reno に戻せる。

tcp-model の追従（既定が cubic になったので）: rtt-step の `--expect-retransmits-max` 64 → 72（CUBIC は timeout の後の閾値が flight の 0.7 で、2 回目の偽の timeout の後の slow start が B の持つ segment を少し多く送り直す: 69、Reno は 57。p002 §4 の一覧で「数え直す」とした物）。reno-bottleneck・reno-two-flows は `--cc reno` を明示（CA の伸び 0.8 MSS/RTT は Reno だけの期待）。

## 5. 既定を cubic にした tree の確かめ（2026-10-29）

| コマンド | 結果 |
| --- | --- |
| `OUT=build/ws311/host-p004 plan/ws220/tests/tcp-model/run.sh`（模型、既定 cubic） | `tcp-model: PASS`（rtt-step の上限を 72 にした後） |
| `build/ws311/build5.sh p004`（vf2・ci-amd64・ci-pcat・ci-rpi4・ci-zybo） | 5 つとも rc=0、warning 0 |
| `plan/ws221/tests/qemu-tcp.sh "" 20 inet,inet6`（QEMU） | `qemu-tcp: PASS`、net.tcpstats の接続は cc=cubic |
| `flock /tmp/i915-hw.lock plan/ws221/tests/vf2-tcp.sh --kernel build/vf2/vmunix --peer awe@10.0.30.3,10.0.30.3 --runs send,recv p004-lan`（実機） | 送り 949.1、受け 949.3 Mbps、板の 1 秒ごとの net.tcpstats は 94 行とも cc=cubic |

VF2 の /boot/boot0/vmunix はこの tree（既定 cubic、sha256 の先頭 75cdcaf394548507）、vmunix.old は 128e60640 の tree。en0 は 1500。
