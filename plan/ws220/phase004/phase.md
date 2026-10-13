<!-- awesome-plan project=zedbsd record=ws220-p004 -->
# ws220-p004: RTT の推定と RTO（RFC 6298）

Status: in-progress / test-wait（2026-10-13 P3: 設計（§1）・実装・模型・build（amd64・rpi4、warning 0）は済み。design-reviewer の結果と §5 の T1 の QEMU 待ち。cleared は Q1 の判定）
Parent: [WS220](../ws.md)
Queue: q943（承認: 2026-10-13 ユーザーの再開の指示「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」と WS220 ws.md のユーザーの決定「はい、推奨でいいです。」（RTO の最小 200 ms））
設計の出所: [p001 §4 の p004 の行](../phase001/phase.md)、[review-1](../phase001/review-1.md) の S4（RTT の計り始めは device に渡った時刻）・S6（最小の項の形、SYN の 3 s、clock の粒度）、ws.md の Q1 の記録（S6: 単純な 200 ms の床でなく、式に最小の項 200 ms を入れる）、[p003](../phase003/phase.md) の結果（送った印・未送信・timer の再送の分類）

## ゴール

1. RFC 6298 の SRTT・RTTVAR を持つ: 最初の sample で SRTT = R、RTTVAR = R/2、以後 RTTVAR = 3/4·RTTVAR + 1/4·|SRTT − R|、SRTT = 7/8·SRTT + 1/8·R。
2. 1 往復に 1 つの sample（RFC 6298 3）: 一度に 1 つの segment だけを計る。計り始めは device に渡した時刻（p003 の「送った印」）。
3. Karn: 再送した segment の ACK からは sample を取らない。timer の再送（timeout・すぐの再送）があれば今の計りを捨てる。
4. RTO = SRTT + max(G, 4·RTTVAR, 200 ms)、最大 60 s（Linux と同じ形、review-1 S6、ユーザーの決定「最小 200 ms」）。sample の前は 1 s（RFC 6298 2.1）。
5. timeout ごとに倍（今の backoff のまま）、上限 60 s。新しい data の ACK で backoff を戻す（今のまま）。
6. SYN（または SYN-ACK）を再送して sample が無いまま接続ができたら、RTO を 3 s にする（RFC 6298 5.7）。
7. net.tcpstats: `TCP_STATS_HAVE_RTT` を立て、`srtt_us`・`rttvar_us` を埋め、`rto_ms` は backoff を含む今の timeout。

範囲の外: fast retransmit・NewReno・「窓が開いた時の再送」と「回復した時に次を直ちに再送」の除去（p005）、cwnd と偽の timeout の取り消し（F-RTO、p006 の前に決める、review-1 S6）、受けの窓（p007）。UAPI は注記だけ（欄と大きさは変えない）。

## 1. 決めたこと

| 項目 | 決め | 理由 |
| --- | --- | --- |
| 時計 | `sched_ticks()`（amd64・arm64 は 1 ms、i386・sparcv9・m68030 は 10 ms）。sample R は tick の差を μs にした値（tick の粒度で 0 もある）。SRTT・RTTVAR は μs の整数で持つ | 細かい時計の API は kernel に無い（`include/kern/clock.h`）。LAN の 0.2 ms の RTT は 0 か 1 tick になるが、最小の項 200 ms が RTO を決めるので害は無い。G は 1 tick |
| 式 | RTO = SRTT + max(G, 4·RTTVAR, 200 ms) を tick に切り上げ、上限 60 s | review-1 S6 と ws.md の Q1 の記録。単純な 200 ms の床は遅延 ACK（最大 200 ms）の相手で偽の timeout を出す。この形は常に SRTT より 200 ms 以上長い。RFC 6298 2.4 の SHOULD（最小 1 s）から外れる（ユーザーの決定） |
| 計る segment | `tcp_mark_sent_locked`（device に渡す直前の印）で、計っていなければその segment を計る: `rtt_sequence` = segment の終わり（sequence + advance）、`rtt_start` = 印の時刻、`rtt_timing` = 1 | review-1 S4（未送信の間は計らない）。1 往復に 1 つ |
| sample を取る | `tcp_retire_acknowledged` で ACK が進み `rtt_sequence` 以上を覆った時（剰余の比較）。SYN_SENT・SYN_RECEIVED が確立する時（SYN・SYN-ACK の ACK）も、ring を消す前に取る | handshake の sample は最初の data の前に RTO を合わせる |
| 計りを捨てる | timer の再送（timeout も、`retransmit_deadline = now` のすぐの再送も同じ道）を device に渡そうとした時、拒まれて未送信に戻った計っていた segment、ring の reset、新しい connect | Karn（RFC 6298 3）。再送の後の ACK は元の送りへの答えか再送への答えか分からない。新しい segment を印した時に計り直す |
| backoff | `tcp_backoff_timeout(rto, count)` = rto を count 回倍、上限 60 s。timer・net.tcpstats・印・retire の期限は全部これを通す | 今は TCP_INITIAL_RTO（1 s）を倍にしている |
| backoff を戻す時 | 今のまま（新しい data の ACK で `retransmit_count` = 0）。RFC 6298 5 の注記は「新しい sample で RTO が縮む」で、ACK で戻すのは FreeBSD・Linux と同じ実用の形 | p005 の前に変えると損失の回復の値が読めなくなる |
| SYN の 3 s | 確立した時、SYN（SYN-ACK）を一度でも再送していて（`retransmit_count` ≠ 0）sample が無ければ RTO = 3 s | RFC 6298 5.7 |
| 新しい接続 | connect の始めと listener の子（calloc で 0）で SRTT・RTTVAR・rto・計りを 0 に。`rto` = 0 は「sample 前の 1 s」 | 同じ socket の再 connect に古い値を持ち越さない |
| net.tcpstats | `features` に `TCP_STATS_HAVE_RTT`（0x02）を立てる（sample の前も: 欄の意味は「持つ」、値 0 = sample 前）。`srtt_us`・`rttvar_us`、`rto_ms` = backoff を含む今の timeout | p001 の UAPI の約束（bit を立てて欄を埋める） |

不変条件:
- `rtt_timing` ≠ 0 なら、`rtt_sequence` を終わりに持つ segment は送った印があり、ring の中にあるか ACK されたばかり。
- timer の再送の後、新しい segment を印するまで sample は取らない。
- RTO は 200 ms + SRTT 以上、60 s 以下（backoff の前も後も）。

## 2. 変えた file

- `src/kern/net/tcp.c`:
  - 定数 `TCP_RTO_MIN_TERM_US`（200000）・`TCP_SYN_RETRIED_RTO`（3 s）・`TCP_RTT_SAMPLE_MAX_US`（60 s）。
  - 新しい関数 `tcp_rto_locked`（sample の前は 1 s）、`tcp_rtt_sample_locked`（RFC 6298 2.2・2.3 と RTO の式）、`tcp_rtt_forget_locked`（新しい connect）、`tcp_rtt_handshake_locked`（handshake の sample か 5.7 の 3 s）。
  - `tcp_backoff_timeout(rto, count)`: 接続の RTO を倍にする。timer・net.tcpstats・`tcp_mark_sent_locked`・`tcp_retire_acknowledged` の期限は全部これか `tcp_rto_locked` を通す（TCP_INITIAL_RTO を直に使うのは `tcp_rto_locked` だけ）。
  - `tcp_mark_sent_locked`: 計っていなければ、その segment の終わりと印の時刻で計り始める。`tcp_unmark_sent_locked`: 拒まれて未送信に戻った segment を計っていたら捨てる。
  - `tcp_timer_run` の再送: 計りを捨てる（Karn）。`tcp_retransmit_reset`: 計りを捨てる（推定は残す）。
  - `tcp_retire_acknowledged`: ACK が計っている segment を覆えば sample。SYN_SENT・SYN_RECEIVED の確立: ring を消す前に `tcp_rtt_handshake_locked`。`tcp_connect`: 推定を消す。
  - net.tcpstats: `TCP_STATS_HAVE_RTT`、`srtt_us`・`rttvar_us`、`rto_ms` は backoff を含む接続の timeout。
- `include/kern/net/tcp-socket.h`: 欄 `rtt_measured`・`srtt_us`・`rttvar_us`・`rto`・`rtt_timing`・`rtt_sequence`・`rtt_start`。
- `include/uapi/sysctl.h`: 注記だけ（`rto_ms`・`srtt_us`・`rttvar_us` の意味）。欄・大きさ・version は不変。
- `plan/ws220/tests/tcp-model/model.c`・`run.sh`: 下の §3。

## 3. 模型の scenario

模型の変更: `--expect-srtt-us LOW:HIGH`（終わりの A の SRTT を確かめる、範囲の外は `check: FAIL`）、`--lose-first-syn`（A の最初の SYN を失う）、「connected」の行に A の `rto_ms`・`srtt_us`、interval の行に `a.srtt_us`・`a.rto_ms`、`check_tcpstats` は全部の entry に `TCP_STATS_HAVE_RTT`、A の entry の `srtt_us` が socket と同じで `rto_ms` が 0 でないことを確かめる。

足した scenario:

| scenario | 引数 | 目標 | 結果 |
| --- | --- | --- | --- |
| rtt-estimate-lan | `--bytes 16Mi --expect-srtt-us 0:2000` | 30 MB/s、SRTT ≤ 2 ms | 37.24 PASS（SRTT 170 μs、RTTVAR 300 μs、RTO 201 ms） |
| rtt-estimate-10ms | `--bytes 4Mi --delay-us 5000 --expect-srtt-us 9000:13000` | SRTT 9〜13 ms | PASS（SRTT 10000 μs、RTO 210 ms） |
| syn-retried | `--bytes 16Mi --lose-first-syn` | 30 MB/s、確立の時の RTO が 3 s | 37.24 PASS（1.0 s 後に確立、rto_ms=3000、最初の data の sample で 201 ms へ） |

## 4. 実行した command と結果

- `git merge main`（470cdd823、main 282f46038 以後、衝突なし）。
- 模型の全部: `OUT=build/p3-ws220-host sh plan/ws220/tests/tcp-model/run.sh`（log `build/p3-ws220-p004-model.log`）。

| scenario | p003 | p004 |
| --- | --- | --- |
| lan-clean | 37.28 | 37.28 PASS |
| rtt-10ms | 0.78 FAIL（p006） | 0.78 FAIL（p006） |
| lan-loss-0.1 | 1.04 FAIL、最長の停止 1000 ms | **4.65** FAIL（p005）、最長の停止 **201 ms** |
| lan-loss-1 | 0.10 FAIL、3000 ms | **0.49** FAIL（p005）、**603 ms** |
| rtt-10ms-loss-0.1 | 0.39 FAIL、1010 ms | **0.65** FAIL（p005）、221 ms |
| lan-loss-1-zedbsd | 2.47 | 9.47 PASS（停止 403 ms） |
| queue-1-loss-1 | 2.47 | 9.45 PASS |
| lan-ackloss-1・lan-wrap・slow-reader・overlap・overlap-wrap・blind-segments | 37.18・37.24・1.91・37.26・37.24・37.22 | 同じ値で PASS |
| queue-7/5/2/1・two-writers | 37.28・37.28・37.28・37.26・37.26 | 同じ値で PASS |
| unresolved | 11.17（ARP 8、停止 1000 ms） | 15.45 PASS（ARP 9、停止 603 ms） |
| unresolved-giveup | 123.5 s・ARP 14 | 103.2 s で諦め PASS |
| connect-unresolved・syn-giveup | 183.0 s | 183.0 s PASS（sample が無いので 1 s から） |
| data-giveup | 123.5 s | 103.2 s PASS（100.5〜200 s） |

  `tcp-model: FAIL` は p005・p006 の 4 つだけ（前と同じ顔ぶれ）。
  p001 §4 の p004 の目標「lan-loss-1 の最長の停止 < 0.5 s」は**届かない**: 603 ms = RTO 201 ms + 再送も失って backoff の 402 ms。1 回の損失の停止は約 201 ms（lan-loss-0.1 の最長の停止）。再送の損失で backoff を伸ばすのは RFC 6298 5.5 の MUST で、これを縮めるのは p005（fast retransmit で RTO に頼らない）。
- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-ws220-amd64 ZEDBSD_USER_PROGRAMS=sysctl build/p3-ws220-amd64/vmunix`（`amd64 vmunix check: PASS`）、`make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p3-ws220-rpi4 ZEDBSD_USER_PROGRAMS=sysctl build/p3-ws220-rpi4/vmunix`（`arm64 vmunix check: PASS`）。log `build/p3-ws220-p004-*-2.log` の `warning:` 0。
- `python3 plan/tools/style-check.py src/kern/net/tcp.c`: 111 → 111（p003 の tcp.c と内容で比べ、新しい指摘 0）。model.c の増えた指摘は option の解き方の `--lose-first-syn` の 3 つ（既存の形、p009）。`git diff --check` ok。

未実施: QEMU（§5）、実機、i386 など 10 ms の clock の build と動き（式は `KERN_CLOCK_HZ` で書いた）。

## 5. T1 への依頼（ws220-p003 の T1-577 の後、同じ形で）

- 何: ws220-p004 の QEMU の確かめ。T1-577（p003）と同じ測りを p004 の kernel で流して比べる。
- image A: p004 を merge した main の SHA の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk build/t1-ws220-p004`（過去の build の成果は使わない）。wrap の image は不要（sequence の扱いを変えていない）。
- 流す物: (1) guest で root の `sysctl net.tcpstats | head -3`: SSH の接続の行に `features=0x22`（HAVE_SEQUENCE と HAVE_RTT）、0 でない `srtt_us`、`rto_ms` が 200〜1000。(2) `plan/ws220/tests/tcp-stream-series.sh IMAGE_A --bytes 1073741824 --out build/ws220-p004-series-0`。(3) `... --loss 0.1 --bytes 268435456 --out build/ws220-p004-series-01`。(4) `... --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-p004-series-1`。
- 合否:
  - 各 run が `tcp-stream-series: PASS`。
  - 損失 0 の up: 終わりの net.tcpstats の `retransmit_segments` が T1-577 以下（Linux の相手の遅延 ACK で偽の timeout が増えない、review-1 S6）、mean の MB/s が T1-577 の 90 % 以上。
  - 損失 0 の down・down-disk: mean が T1-577 の 90 % 以上。
  - 損失 0.1 % の up: wire の `longest_stall_ms` が T1-577 より短い（1 s の RTO が約 0.2〜0.4 s に）、mean は記録（p005 の範囲）。損失 1 % は完走と記録だけ。
- 返す物: T1-577 と同じ（summary.txt の analyze の行、終わりの net.tcpstats の `srtt_us`・`rttvar_us`・`rto_ms`・`retransmit_segments`・`backoff`、settings.txt、失敗なら transfer-*.txt・setup.txt、pcap は path だけ）。

## 6. 残りと判断

- 偽の timeout の取り消し（F-RTO、RFC 5682）は入れていない。RTO = SRTT + 200 ms 以上なので遅延 ACK（最大 200 ms）では起きにくいが、RTT が急に伸びる時（USB の adapter の queue、T1-516 の 24 ms）には起きうる。p006（cwnd）の前に入れるかを決める（review-1 S6 のとおり）。
- backoff は新しい data の ACK で戻す（RFC 6298 5 の注記は「新しい sample で」）。FreeBSD・Linux と同じ実用の形として残した。
- sample は tick の粒度（1 ms、i386 などは 10 ms）。LAN の RTT は 0〜1 tick で、SRTT は約 0.2 ms に平均される。RTO を決めるのは最小の項なので害は無い。
- 削除してほしい出力（Q1、P3 の worktree）: `build/tmp/p3-p004.d5tj`（style の比べ）、`build/p3-ws220-p004-*.log`・`build/p3-ws220-p004-model.log`。
