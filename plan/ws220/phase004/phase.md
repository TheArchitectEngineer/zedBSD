<!-- awesome-plan project=zedbsd record=ws220-p004 -->
# ws220-p004: RTT の推定と RTO（RFC 6298）

Status: in-progress（2026-10-13 P3: 設計（§1）を書いた。design-reviewer は Q1 が回す）
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

（実装の後に書く）

## 3. 模型の scenario

（実装の後に書く）

## 4. 実行した command と結果

（実装の後に書く）

## 5. T1 への依頼

（実装の後に書く）

## 6. 残りと判断

（実装の後に書く）
