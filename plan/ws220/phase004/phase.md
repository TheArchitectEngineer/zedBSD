<!-- awesome-plan project=zedbsd record=ws220-p004 -->
# ws220-p004: RTT の推定と RTO（RFC 6298）

Status: in-progress / test-wait（2026-10-13 P3: 設計・実装・模型・build（amd64・rpi4、warning 0）は済み。design review-1（重大 2・中 5・軽微 6）を同じ attempt で直した（§7）。§5 の T1 の QEMU 待ち。cleared は Q1 の判定）
Parent: [WS220](../ws.md)
Queue: q943（承認: 2026-10-13 ユーザーの再開の指示「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」と WS220 ws.md のユーザーの決定「はい、推奨でいいです。」（RTO の最小 200 ms））
設計の出所: [p001 §4 の p004 の行](../phase001/phase.md)、[p001 review-1](../phase001/review-1.md) の S4・S6、ws.md の Q1 の記録（S6: 単純な 200 ms の床でなく式に最小の項 200 ms を入れる）、[p003](../phase003/phase.md) の結果、本 Phase の [review-1](review-1.md)（design-reviewer、a52a81575 に対して）と Q1 の決め
依存: p003 の出力（`tcp_mark_sent_locked`・`tcp_unmark_sent_locked`・拒まれ方の分類、main にある）に scoped-output で依存。p003 は T1-577 待ち。p003 の QEMU の結果で分類が変わったら §1 を見直す（review-1 L6）。

## ゴール

1. RFC 6298 の SRTT・RTTVAR を持つ: 最初の sample で SRTT = R、RTTVAR = R/2、以後 RTTVAR = 3/4·RTTVAR + 1/4·|SRTT − R|（古い SRTT で先に）、SRTT = 7/8·SRTT + 1/8·R。
2. 1 往復に 1 つの sample（RFC 6298 3）: 一度に 1 つの segment だけを計る。計り始めは device に渡す印の時刻。
3. Karn: 再送した segment の ACK からは sample を取らない。backoff を戻すのは有効な sample の時だけ（RFC 6298 5 の注記）。
4. RTO = SRTT + max(G, 4·RTTVAR, 200 ms) を tick に切り上げて 1 tick を足し、最大 60 s（Linux と同じ形）。sample の前は 1 s（2.1）。
5. 本当に期限が切れた timeout ごとに倍（5.5）、上限 60 s。
6. SYN（SYN-ACK）の timer が切れたか出し直して sample が無いまま接続ができたら、RTO を 3 s にする（5.7）。
7. net.tcpstats: `TCP_STATS_HAVE_RTT`、`srtt_us`・`rttvar_us`・`rtt_samples`（前の `reserved`）、`backoff` = RTO の倍の指数、`rto_ms` = 倍を含む今の timeout。

範囲の外: fast retransmit・NewReno・「窓が開いた時の再送」と「回復した時に次を直ちに再送」の除去（p005）、cwnd と偽の timeout の取り消し（F-RTO、p006 の前に決める）、受けの窓（p007）。UAPI は大きさと版を変えない。

## 1. 決めたこと（review-1 の後）

| 項目 | 決め | 理由 |
| --- | --- | --- |
| 時計 | `sched_ticks()`（amd64・arm64 は 1 ms、i386・sparcv9・m68030 は 10 ms）。sample R は tick の差を μs にした値（0 もある）、上限 60 s（`TCP_RTT_SAMPLE_MAX_US`）、計算は 64 bit。SRTT・RTTVAR は μs の uint32、`rtt_start` は tick の uint64、\|SRTT − R\| は大きい方から引く | 細かい時計の API は kernel に無い。lock の下だけで読み書きするので 32 bit の機械でもちぎれない（L4） |
| 式 | RTO = SRTT + max(G, 4·RTTVAR, 200 ms)（μs）を tick に切り上げ、さらに 1 tick、上限 60 s。G = 1 tick は 200 ms が常に勝つので効かない（L4、式の形を RFC に合わせて残す） | review S6。足す 1 tick: 期限は segment を出した tick から数えるので、本当の時間より最大 1 tick 早く切れる。遅延 ACK 200 ms の相手で ACK と期限が同時になり偽の timeout が出た（模型 delayed-ack-200、足す前は lone segment ごとに 128 回）。RFC 6298 2.4 の最小 1 s から外れる（ユーザーの決定。式の形の訂正の報告は Q1、L3） |
| 再送の数と backoff の指数を分ける（G1） | `retransmit_count`（最古の再送の数、R2・`still_oldest`・回復の判定の帳簿、今の意味のまま）と、新しい `rto_backoff`（RTO を倍にする指数）。指数は**本当に期限が切れた** timeout でだけ 1 増え、**有効な sample** の時だけ 0 に戻る。窓が開いた時・回復の即時の再送（`retransmit_immediate` の印）は指数を進めない。期限は全部 `tcp_backoff_timeout(rto, rto_backoff)` | Karn の後半（RFC 6298 5 の注記、Linux・FreeBSD も sample で戻す）。新しい data の ACK で戻すと、RTT が RTO を超えて伸びた時に毎往復の偽の timeout が続き sample が取れない（review G1） |
| 進まない ACK（G2） | 重複・窓 0 の ACK では `retransmit_since`（R2）・`retransmit_count`・`retransmit_refused` を今のまま 0 に、指数は戻さない | 窓 0 の相手への再送が倍々に離れる（RFC 9293 3.8.6.1 の SHOULD）。模型 zero-window: 5 s の窓 0 で再送 7（p003 は 17739） |
| 回復の範囲（G1 の補い） | 本当の timeout は `recovery_end` = その時の送った端を覚える。ACK の後の「次を直ちに再送」は、次の segment が `recovery_end` より前の時だけ（BSD の go-back-N の範囲、NewReno の recover と同じ考え） | 範囲が無いと、timeout の後に書いた新しい segment まで ACK ごとに直ちに再送され続け、計りが毎回捨てられて sample が取れない（模型 rtt-step で確かめた）。p005 はこの再送の仕組みごと NewReno に替える |
| 計る segment | `tcp_mark_sent_locked` で、計っていなければその segment（`rtt_sequence` = sequence + advance、`rtt_start` = 印の tick） | 1 往復に 1 つ、device に渡す時刻から |
| sample を取る | `tcp_retire_acknowledged`: ACK が進み `rtt_sequence` 以上を覆った時。順は **sample → RTO の計算 → 期限の張り直し**（L5）。確立（SYN_SENT・SYN_RECEIVED）: `tcp_retransmit_reset` の**前**に `tcp_rtt_handshake_locked`（sample か 5.7） | |
| 計りを捨てる（Karn、M1、M3） | (1) 本当の timeout: いつも捨てる（BSD と同じ）。(2) 即時の再送: 再送する segment が計っている segment の時だけ（RFC 6298 3 は「再送した segment の sample を使わない」。後ろの segment の sample は使える）。(3) 拒まれて未送信に戻った計っていた segment。(4) 固い error で取り戻した計っていた segment（M1）。(5) ring の reset、新しい connect。(6) SYN_RECEIVED で重複の SYN に SYN-ACK を出し直した時（M3） | (2) の理由: zedBSD 同士では窓の小さな増えが毎往復来て、そのたびに捨てると RTT が伸びた時に sample が取れない（模型）。Q1 の決め「即時の再送は計りを捨てる」を Karn の範囲（その segment）に絞った |
| SYN の 3 s（M3） | 独立の印 `syn_timed_out`: SYN_SENT・SYN_RECEIVED の timer の再送（device が拒んでも、期限は切れた）と、重複 SYN への SYN-ACK の出し直しで立てる。確立で、印があり sample が 0 なら RTO = 3 s。確立で指数・印・計りを 0 に | `retransmit_count` は拒まれた再送で戻り、重複 SYN の出し直しでは動かないので 5.7 の判定に使えない（review M3） |
| 最初の sample（M2） | 新しい `rtt_samples`（sample の数、上限で止まる）。0 なら次が最初（2.2）。5.7 の 3 s（`rto`）とは独立 | rto ≠ 0 や srtt ≠ 0 では見分けられない（review M2） |
| 新しい接続 | connect の始めで推定・指数・印を 0 に（`tcp_rtt_forget_locked`）、listener の子は calloc で 0 | |
| net.tcpstats（M5） | `features` に `TCP_STATS_HAVE_RTT`。entry の `reserved`（uint32）を `rtt_samples` に（大きさ・版は不変、古い kernel は 0）。注記: HAVE_RTT が立ち `rtt_samples` = 0 なら `srtt_us`・`rttvar_us` は意味を持たない。`backoff` は RTO の倍の指数（前回の有効な sample の後の timeout の数）、`rto_ms` = 倍を含む timeout。`sysctl` の表示に `rtt_samples`、`analyze.py` に `srtt_us`・`rttvar_us`・`rtt_samples` の列と `analyze: rtt` の行 | 既存の注記「bit の無い欄の 0 は持たない」との食い違いを解く（review M5） |

不変条件（M1 で強めた）:
- `rtt_timing` ≠ 0 なら、`rtt_sequence` を終わりに持つ segment は ring の中にあり送った印がある。外れる道（ACK で retire → sample か、ACK が覆わない限り残る / 未送信に戻る / 取り戻し / reset / 本当の timeout / その segment の即時の再送 / 重複 SYN の出し直し）は全部 `rtt_timing` を 0 にする。
- `rto_backoff` が 0 に戻るのは有効な sample・確立・新しい connect だけ。
- RTO は SRTT + 200 ms + 1 tick 以上（sample の後）、60 s 以下。

WS221 との順（M5）: WS221 p002 も `include/uapi/sysctl.h`（net.tcpstats の header 48 → 72 byte）を変える予定。p004 が先に main に入る（WS221 は今動いていない）。WS221 p002 は `rtt_samples` の入った sysctl.h の上で作り直すこと（Q1 が順を管理）。

## 2. 変えた file

- `src/kern/net/tcp.c`:
  - 定数 `TCP_RTO_MIN_TERM_US`・`TCP_SYN_RETRIED_RTO`・`TCP_RTT_SAMPLE_MAX_US`。
  - 関数 `tcp_rto_locked`・`tcp_rtt_sample_locked`（2.2・2.3、RTO、指数を 0 に、`rtt_samples` を 1 増やす）・`tcp_rtt_forget_locked`・`tcp_rtt_handshake_locked`。`tcp_backoff_timeout(rto, exponent)`。
  - `tcp_timer_run`: 再送の数と指数を分け、本当の timeout だけ指数を進めて回復の範囲（`recovery_end`）を置く。拒まれた再送は数と（本当の timeout なら）指数を戻し、即時の印も戻す。Karn は (1)(2) のとおり。SYN の timeout で `syn_timed_out`。
  - `tcp_retire_acknowledged`: 回復は `recovery_end` まで、sample → RTO → 期限（指数つき）。進まない ACK は指数を戻さない（今までどおり数と R2 は戻す）。
  - `tcp_mark_sent_locked`: 計り始め、期限は指数つき。`tcp_unmark_sent_locked`・取り戻し・`tcp_retransmit_reset`: 計りを捨てる。取り戻しは net.tcpstats の `data_segments_out`・`data_bytes_out` も戻す（書き手が同じ byte を書き直すので二重に数えない）。
  - 窓が開いた時の即時の再送と回復の即時の再送に `retransmit_immediate`。重複 SYN への SYN-ACK の出し直しで計りを捨て `syn_timed_out`。
  - net.tcpstats: `rtt_samples`・`srtt_us`・`rttvar_us`・`backoff`（指数）・`rto_ms`。
- `include/kern/net/tcp-socket.h`: 欄 `rtt_samples`・`srtt_us`・`rttvar_us`・`rto`・`rto_backoff`・`retransmit_immediate`・`rtt_timing`・`rtt_sequence`・`rtt_start`・`syn_timed_out`・`recovery_end`・`recovery_active`。
- `include/uapi/sysctl.h`: entry の `reserved` → `rtt_samples`（同じ位置・大きさ）、注記（`backoff`・`rto_ms`・`srtt_us`・`rttvar_us`・`rtt_samples`）。
- `userland/base/sysctl/main.c`: net.tcpstats の行に `rtt_samples=`。
- `plan/ws220/tests/analyze.py`: STATE に `srtt_us`・`rttvar_us`・`rtt_samples`、`analyze: rtt` の行（転送の間の SRTT の中央値と最大、RTO の中央値と最大、指数の最大）。
- `plan/ws220/tests/tcp-model/model.c`・`run.sh`: §3。

## 3. 模型の scenario

模型の変更: device の完了の一巡と tick を `KERN_CLOCK_HZ` から（1 ms 固定をやめた）。option: `--expect-srtt-us`・`--lose-first-syn`・`--ack-delay-ms`（B の遅延 ACK: 1 つ目を保ち 2 つ目で置き換えて直ちに送る）・`--writer-gap-ms`（書き手が 1 つずつ間を置く）・`--delay-step-at-ms/--delay-step-us`（途中で片道の遅延を変える）・`--reader-pause-at-ms/-ms`（B の読み手が止まり窓 0）・`--no-route-at-ms/-ms`（A の IPv4 が固い error、書き手は 100 ms 後に同じ byte を書き直す）・`--expect-first-srtt-us`・`--expect-first-rttvar-us`・`--expect-srtt-max-us`・`--expect-retransmits-max`・`--expect-stall-rto`。終わりに `rtt:`（sample の数、SRTT・RTTVAR・RTO・指数、最初の sample、SRTT の最大、再送、即時の再送）と `stall:`（最長の停止、1 つの segment の連続の再送の最大、二重の損失の数、指数の最大）の行。check_tcpstats は HAVE_RTT と A の srtt を確かめる。

p004 の scenario（全部 PASS、seed は 1 固定）:

| scenario | 確かめること | 結果 |
| --- | --- | --- |
| rtt-estimate-lan | SRTT ≤ 2 ms | 37.24 MB/s、SRTT 170 μs |
| rtt-estimate-10ms | SRTT 9〜13 ms | SRTT 10000 μs |
| syn-retried | 最初の SYN を失う（RTT 10 ms）: 確立の時 RTO 3 s、最初の sample で SRTT = R・RTTVAR = R/2 | rto_ms=3000、最初の SRTT 10000・RTTVAR 5000 μs |
| loss-stall-0.1・loss-stall-1（M4） | 最長の停止 ≤ (2^(e+1) − 1)·RTO + 20 ms（e = 指数の最大）。二重の損失の数を出す | 0.1 %: 202 ms（二重 0、e 1）。1 %: 1212 ms（二重 1、e 3 = 402 + 804 ms。Karn で指数は sample まで戻らない） |
| delayed-ack-40・delayed-ack-200 | 1 つずつの segment（書き手 300 ms ごと）に相手が 40・200 ms 遅れて ACK: 偽の再送 0、SRTT が遅れを取り込む | 再送 0、SRTT 39993・199993 μs（200 ms は 1 tick を足す前は 128 回の偽の再送） |
| delayed-ack-stream | 流れの転送で遅延 ACK 200 ms | 35.74 MB/s、再送 0 |
| rtt-step（G1） | 20 ms で片道 0.1 → 300 ms（RTT 600 ms、Linux 相当の動かない窓）: 再送 ≤ 4、SRTT が 500〜700 ms に追う | 再送 1、SRTT 593595 μs、RTO 794 ms |
| zero-window（G2） | B の読み手が 5 s 止まる: A の探りが倍々（再送 ≤ 10） | 再送 7（p003 は 17739） |
| no-route（M1） | 50 ms の固い error、取り戻した write を書き直す: SRTT ≤ 5 ms、net.tcpstats の送った byte が書いた byte と同じ | SRTT の最大 274 μs、tcpstats ok |

既存の scenario の受け入れ（G2）: slow-reader と `--peer-window` の損失の scenario の `retransmit_segments` は p003 以下（`--expect-retransmits-max` に p003 の値: slow-reader 14246、lan-loss-0.1 30、lan-loss-1 83、rtt-10ms-loss-0.1 10、lan-ackloss-1 0。p003 の値は同じ模型で p003 の tcp.c を build して取った）。

模型で見られないこと: HZ = 100 の模型（i386 の header は 64 bit の host で build できない。tick の幅は `KERN_CLOCK_HZ` に従うようにした）、zedBSD 同士で RTT が RTO を超えて伸びた時（窓の小さな増えの即時の再送が毎往復続く、p005 で外す）。

## 4. 実行した command と結果

- `git merge main`（470cdd823）。
- 模型の全部: `OUT=build/p3-ws220-host sh plan/ws220/tests/tcp-model/run.sh`（log `build/p3-ws220-p004-model2.log`）。

| scenario | p003 | p004（review-1 の後） |
| --- | --- | --- |
| lan-clean・queue-7/5/2/1・two-writers・lan-wrap・overlap・overlap-wrap・blind-segments | 37.2〜37.3 | 同じ値で PASS |
| rtt-10ms | 0.78 FAIL（p006） | 0.78 FAIL（p006） |
| lan-loss-0.1 | 1.04 FAIL、停止 1000 ms | **4.63** FAIL（p005）、停止 **202 ms**、再送 30 |
| lan-loss-1 | 0.10 FAIL、3000 ms | **0.46** FAIL（p005）、1212 ms（上の loss-stall-1）、再送 83 |
| rtt-10ms-loss-0.1 | 0.39 FAIL、1010 ms | **0.65** FAIL（p005）、222 ms、再送 10 |
| lan-loss-1-zedbsd・queue-1-loss-1 | 2.47 | **14.64** PASS |
| lan-ackloss-1 | 37.18 | 37.18 PASS（再送 0） |
| slow-reader | 1.91 | 1.87 PASS（再送 ≤ p003） |
| unresolved | 11.17、停止 1000 ms | 15.37 PASS、606 ms |
| unresolved-giveup・data-giveup | 123.5 s | 103.7 s で諦め PASS |
| connect-unresolved・syn-giveup | 183.0 s | 183.0 s PASS |
| p004 の scenario（§3） | — | 全部 PASS |

  `tcp-model: FAIL` は p005・p006 の 4 つ（rtt-10ms、lan-loss-0.1、lan-loss-1、rtt-10ms-loss-0.1）の平均の速さだけで、前と同じ顔ぶれ。p001 §4 の p004 の目標「lan-loss-1 の最長の停止 < 0.5 s」は使わず、review M4 のとおり loss-stall の「timeout が説明する停止」に替えた（seed 1 の lan-loss-1 には二重の損失と sample 前の損失があり、0.5 s は RFC 6298 5.5 と Karn に従うと超える）。
- G1 の確かめの経緯: 指数を分けただけでは rtt-step は 300 s で終わらなかった（窓が開いた時・回復の即時の再送が毎往復計りを捨てる）。Karn を「再送した segment」に絞り、回復の範囲を置いて、動かない窓の相手で収まった（再送 1）。zedBSD 同士の窓では今も収まらない（§6）。
- build（warning 0）: amd64 `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-ws220-amd64 ZEDBSD_USER_PROGRAMS=sysctl build/p3-ws220-amd64/vmunix build/p3-ws220-amd64/bin/sysctl`（vmunix check PASS）、rpi4 同じ形（arm64 vmunix check PASS）、pcat（i386、HZ 100）`make -j16 ZEDBSD_CONFIG=config/ci/config-pcat.mk BUILD=build/p3-ws220-pcat build/p3-ws220-pcat/vmunix`: tcp.c を含む compile は warning 0、link は `drv_xhci_isoch_*` の未定義で失敗（WS220 と無関係、main の pcat の build の問題として Q1 に報告）。
- `python3 plan/tools/style-check.py src/kern/net/tcp.c`: 111（p003 の tcp.c と内容で比べ、新しい指摘 0）。model.c の増えた指摘は option の解き方の `strcmp` の形だけ（既存の形、p009）。`git diff --check` ok。
- analyze.py: 合成の series で `analyze: rtt samples=5 srtt_us_median=203 … backoff_max=1` を確かめた。

未実施: QEMU（§5）、実機、HZ = 100 の動き（compile だけ）。

## 5. T1 への依頼（ws220-p003 の T1-577 の後、同じ形で）

- 何: ws220-p004 の QEMU の確かめ。T1-577（p003）と同じ測りを p004 の kernel で流して比べる。
- image A: p004 を merge した main の SHA の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk build/t1-ws220-p004`（wrap の image は不要）。
- 流す物: (1) guest で root の `sysctl net.tcpstats | head -3`: SSH の接続の行に `features=0x22`、`rtt_samples` が 0 でない、`srtt_us` が 0 でなく `rto_ms` が 200〜1000。(2) `plan/ws220/tests/tcp-stream-series.sh IMAGE_A --bytes 1073741824 --out build/ws220-p004-series-0`。(3) `... --loss 0.1 --bytes 268435456 --out build/ws220-p004-series-01`。(4) `... --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-p004-series-1`。
- 合否:
  - 各 run が `tcp-stream-series: PASS`。
  - 損失 0・0.1・1 % の up（usb-net の上り）: `analyze: rtt` の行で `rto_ms_median` がおよそ 200〜230 ms、`srtt_us` の時系列（series-up.csv）が出ている。終わりの net.tcpstats の `retransmit_segments` が T1-577 以下（偽の timeout が増えない、review M4）。
  - 損失 0 の up・down・down-disk: mean の MB/s が T1-577 の 90 % 以上。
  - 損失 0.1 % の up: wire の `longest_stall_ms` が T1-577 より短い（1 s の RTO が約 0.2〜0.4 s に）。損失 1 % は完走と記録だけ。
- 返す物: T1-577 と同じ（各 run の summary.txt の `analyze:` の行（summary・wire・rtt）、series-*.csv、終わりの net.tcpstats の `srtt_us`・`rttvar_us`・`rtt_samples`・`backoff`・`rto_ms`・`retransmit_segments`、settings.txt、失敗なら transfer-*.txt・setup.txt、pcap は path だけ）。

## 6. 残りと判断

- 偽の timeout の取り消し（F-RTO、RFC 5682）は入れていない。遅延 ACK 200 ms の相手は 1 tick を足して模型で 0 にしたが、相手の遅延 ACK の timer の揺れや RTT の急な伸び（USB の adapter の queue）では起きうる（L2）。p006（cwnd）の前に入れるかを決める。
- p004 だけの段では RTO が約 200 ms になる分、偽の timeout の後の回復の即時の再送と窓が開いた時の再送が増えうる（L1）。模型では p003 より増えていない（slow-reader 14246 以下、zero-window は 17739 → 7）。p005 で外す前提。
- zedBSD 同士で RTT が RTO を超えて伸びた時: 相手の窓の小さな増えで「窓が開いた時の即時の再送」が毎往復出て、計っている segment がそれに当たると sample が取れない（模型: rtt-step を `--peer-window` 無しで流すと収まらない）。この再送は p005 で外す。
- 回復の範囲（`recovery_end`）は p004 で足した（G1 の確かめに要った）。p005 で NewReno の recover に置き換える。
- L3（式の形の訂正のユーザーへの報告）は Q1。
- pcat の link の失敗（`drv_xhci_isoch_*` 未定義）は WS220 の外。
- 削除してほしい出力（Q1、P3 の worktree）: `build/tmp/p3-p004r.nbVF`（p003 の tcp.c の比べ・debug の模型・style の比べ・analyze の確かめ）、`build/p3-ws220-pcat`（i386 の build の確かめ）、`build/p3-ws220-host100`（失敗した HZ 100 の模型）、`build/p3-ws220-p004-*.log`・`build/p3-ws220-p004-model*.log`。

## 7. review-1 への対応（2026-10-13、a52a81575 の設計の review、全文は [review-1.md](review-1.md)）

| 指摘 | 対応 |
| --- | --- |
| G1 backoff を新しい data の ACK で戻す | 指数 `rto_backoff` を分け、本当の timeout でだけ増やし有効な sample でだけ 0 に。即時の再送は指数を進めない。net.tcpstats の `backoff` は指数（注記を直した）。模型 rtt-step。収めるために Karn を「再送した segment」に絞り、回復の範囲を足した（§1） |
| G2 進まない ACK で backoff を戻す | 指数は戻さない（R2・数は今のまま）。模型 zero-window と、slow-reader・peer-window の損失の再送が p003 以下 |
| M1 取り戻しで計りが残る | 取り戻しで計りを捨てる（net.tcpstats の送った byte も戻す）。不変条件を強めて外れる道を数え上げた。R の上限 60 s、64 bit の計算。模型 no-route |
| M2 最初の sample の見分け | `rtt_samples`。5.7 と独立。模型 syn-retried の最初の sample |
| M3 5.7 の判定と重複 SYN | `syn_timed_out`（timer の再送と重複 SYN への出し直しで立つ）、確立で reset の前に判定、出し直しで計りを捨てる |
| M4 受け入れの基準 | seed 固定で loss-stall（timeout が説明する停止、二重の損失の数）。遅延 ACK 40・200 ms、RTT の段差、SYN の喪失、固い error の後、窓 0 の scenario。analyze.py の列と行。T1 の依頼に rto_ms・srtt_us の時系列と再送が増えないこと。HZ = 100 は compile だけ（模型は §3 の限り） |
| M5 UAPI の 0 の意味 | `reserved` → `rtt_samples`、注記。WS221 p002 との順を §1 に |
| L1・L2 | §6 |
| L3 | Q1 がユーザーに報告 |
| L4・L5 | §1（G の項、単位・型・符号、retire の中の順） |
| L6 | 冒頭の「依存」 |

## 8. T1-577 の「退行」の調べ（2026-10-13、q947）

p003 と一緒に調べた。p004 の RTO（約 203 ms）は止まりと無関係（再送 0、約 200 ms は Linux の zero window probe）。同じ session の A/B（T1-581）で p003＋p004 の kernel は p002 の kernel より速く止まりも短い。詳細と表は [p003 §8](../phase003/phase.md)。
