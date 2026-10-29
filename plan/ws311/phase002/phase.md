<!-- awesome-plan project=zedbsd record=ws311-p002 -->
# ws311-p002: cwnd を使っている時だけ増やす門、Reno の 2 回目の timeout、模型の拡張

Status: in-progress（2026-10-29 P3 q1673-i01）
Parent: [WS311](../ws.md)
Queue: q1673 / q1673-i01（P3）
依存: [p001](../phase001/phase.md)（cleared、第 3 版）

## 1. 目標

- p001 §4.1 の門（RFC 7661、Reno と CUBIC で共通）を tcp.c に入れる。
- Q1 の判断（2026-10-29、ws.md）で Q9 を改めた: 同じ回復の中の 2 回目以降の timeout で ssthresh を保つ（RFC 5681 §3.1）を Reno に入れる。
- p001 §5.1 の模型の拡張（drop-tail の bottleneck、2 本目の接続、上限、新しい check）。tcp-model は plan/ws220/tests/tcp-model のまま直す（Q1 の判断）。
- 受け入れ: tcp-model の全 scenario PASS、5 platform の vmunix の build warning 0、qemu-tcp PASS、VF2 の LAN の非退行。

## 2. 変えた所

| file | 要点 |
| --- | --- |
| `include/kern/net/tcp-socket.h` | `cwnd_round_end`・`cwnd_round_flight`・`cwnd_prev_flight`・`cwnd_blocked`。`TCP_REORDER_MAX` を `CONFIG_TCP_REORDER_MAX` で上書きできる形（未定義なら今のまま） |
| `src/kern/net/tcp.c` | `tcp_cwnd_limited_locked`（slow start: cwnd < 2 × max(この round、前の round の最大の送り済み flight)、CA: max + MSS ≥ cwnd、または go-back-N の保持の印）を `tcp_cwnd_grow_locked` の門に。flight は device が受け取った後に数える（`tcp_cwnd_note_sent_locked`、書き手の owner の送りと `tcp_send_unsent`）。round は ACK が `cwnd_round_end` に届いたら進める（`tcp_cwnd_round_advance_locked`）。始め直す所: `tcp_start_sending`、回復の終わり、timeout の go-back-N、Packet Too Big（`tcp_cwnd_round_reset_locked`）。`tcp_cwnd_loss_locked` に `repeated`（呼ぶ時点の `recovery_by_timeout != 0`）: 2 回目以降の timeout は ssthresh を保ち cwnd = 1 MSS だけ。`TCP_RECEIVE_PACKETS` を `CONFIG_TCP_RECEIVE_PACKETS` で上書きできる形 |
| `plan/ws220/tests/tcp-model/model.c` | `--bottleneck-mbps`・`--bottleneck-queue`（wire の後の drop-tail の router）、`--run-s`（時間で終わる run）、`--receive-buffer`（B の SO_RCVBUF）、`--second-flow-at-ms`・`--fair-skip-s`（2 本目の接続、`fair:` 行）、`cc:` 行（終わりの cwnd・最大の送り済み flight・cwnd 律速の割合・slow start の倍増・CA の区間の伸び）と常時の check「cwnd ≤ max(2 × 最大の送り済み flight + 2 MSS、IW)」、`--expect-cwnd-limited`・`--expect-slow-start`（3 RTT 以内に倍）・`--expect-ca-growth`・`--expect-ssthresh-held`（`timeouts:` 行）。net.tcpstats の check を 2 本の接続に、report に `a.cwnd`・`a.ssthresh`・`a.sent_flight` |
| `plan/ws220/tests/tcp-model/run.sh` | 2 つ目の build（`$OUT/big`: `CONFIG_TCP_SEND_QUEUE_MAX`・`CONFIG_TCP_RECEIVE_PACKETS`・`CONFIG_TCP_REORDER_MAX` = 1024）、scenario `timeout-repeated`・`reno-bottleneck`・`reno-two-flows` |

## 3. 確かめ（2026-10-29、host）

| コマンド | 結果 |
| --- | --- |
| `OUT=build/ws311/host-p002 plan/ws220/tests/tcp-model/run.sh` | `tcp-model: PASS`（全 54 scenario、2 分 11 秒、build/ws311/p002-model-3.txt） |
| 同じ模型を変更前の tcp.c（78cf0206c）で `--bytes 16777216` | `check: FAIL: A's congestion window 16778988 is more than twice the most it sent (37960)`（新しい check が WS221 p011 の不具合を捕まえる）。新しい tcp.c: cwnd 76588 で PASS |
| `timeout-repeated` を 40d914c43 の tcp.c（Reno の直しの前）で | `timeouts: repeated=2 ssthresh_moved=1` → FAIL。直しの後は `ssthresh_moved=0` で PASS |

速さ（mean MB/s、変更前 → 後）: 変わらない物が大半（lan-clean 112.99、lan-jumbo 117.98 など）。変わった物: pool-held 8.83 → 112.46（cwnd が flight の 2 倍で止まり、書き手が pool の 64 の copy を越えて積まない）、queue-1-loss-1 17.68 → 31.82・lan-loss-1-zedbsd 23.87 → 33.79（2 回目の timeout で ssthresh が 2 MSS に落ちなくなった）、lan-loss-0.1 106.56 → 107.82、rtt-step-zedbsd 0.07 → 0.05（目標 0）。

模型の bottleneck の所見（Reno、20 ms・100 Mbps）:
- 既定の上限の build では B の窓が 128 KB（受けの列の 128 packet）で止まり、cwnd 律速は 2 %（review-2 C1 のとおり）。big の build で 100 %。
- reorder の列 128 の時は、穴の後ろの segment を B が捨てて NewReno が 1 RTT に 1 個ずつ直し、平均 4.4 MB/s。1024 にして queue = BDP（250 KB）で 11.1 MB/s（link の 93 %）。
- queue = BDP/4（62.5 KB）では平均 5.67 MB/s、1 秒の区間の 9 つが 1 MB/s 未満: drop-tail が 1 窓で複数を落とすと、SACK の無い NewReno の回復が 1 RTT に 1 個になる（p001 §5.1 の所見、Future Work の候補）。CA の伸びは 0.98〜1.01 MSS/RTT、slow start の倍増は最も遅くて 1.78 RTT。
- 2 本の Reno（2 本目は 5 秒後）: 取り分 0.500 / 0.500、Jain 1.000（drop-tail で 2 本の損失が揃う）。

## 4. 算法に依る既存の期待の一覧（p003 で CUBIC の run に当てる時の扱い）

| 期待 | scenario | 算法に依るか | p003 での扱い |
| --- | --- | --- | --- |
| `--expect-fast-retransmits` | two-holes・two-holes-zedbsd 1:1、window-lost・rtt-step・rtt-step-zedbsd・zero-window 0:1、delayed-ack-200 0:0、lan-wrap-loss 1:100 | 回数は損失の形で決まり、窓の大きさにはほぼ依らない（two-holes は 1 窓に 2 つの穴） | 同じ期待。外れたら理由を書く |
| `--expect-retransmits-max` | lan-ackloss-1 0、delayed-ack-* 0、slow-reader 8566、rtt-step 64、rtt-step-zedbsd 128、zero-window 10、two-holes 2、window-lost 8、persist-answering 16 | rtt-step の 2 つは偽の timeout の後の flight（窓の大きさ）に依る | rtt-step・rtt-step-zedbsd は CUBIC の run で flight を数え直す。他は同じ |
| `--expect-timeouts-max` | lan-jumbo-loss・lan-loss-0.1・rtt-10ms-loss-0.1・two-holes 0、lan-loss-1 2、window-lost 1、timeout-repeated 3 | 窓が大きいと 1 窓の損失が増え得る（lan-loss-1） | 同じ期待で走らせ、外れたら窓の大きさとの関係を書く |
| `--expect-retransmits-losses` | lan-jumbo-loss・lan-loss-0.1・lan-loss-1・rtt-10ms-loss-0.1 | 依らない（損失ごとに 1 回） | 同じ |
| `--expect-stall-max-ms` 700 | burst-loss | timeout の後の slow start に依る（CUBIC も同じ slow start） | 同じ |
| `--expect-stall-rto` | loss-stall-0.1・loss-stall-1 | 依らない | 同じ |
| `--expect-segments-*` | nagle-1024・nodelay-1024 | 依らない（Nagle） | 同じ |
| 最低の mean MB/s | 全部 | 損失のある物（lan-loss-*、rtt-10ms-loss-0.1、queue-1-loss-1、burst-loss、reno-bottleneck）は依る | 同じ最低で走らせる（CUBIC の目標は §5.1 の cc-*） |
| `--expect-ca-growth 0.8` | reno-bottleneck | 依る（CUBIC の W_est は約 0.53、W_max の近くは約 0、review-2 M5） | Reno だけ。CUBIC は p001 §5.1 の軌跡の check |

## 5. 残り

- 5 platform の build（warning 0）、qemu-tcp、VF2 の LAN の非退行（`plan/ws221/tests/vf2-tcp.sh`、5330 を相手に）。
