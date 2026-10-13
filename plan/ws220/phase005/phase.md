<!-- awesome-plan project=zedbsd record=ws220-p005 -->
# ws220-p005: fast retransmit と NewReno の回復、窓 0 の probe と persist、即時の再送の整理

Status: in-progress / test-wait（2026-10-13 P3: 設計・実装・模型・build（amd64・rpi4・pcat、warning 0）は済み。design review-1（重大 2・中 8・軽微 6）と M7 のユーザーの決定（FreeBSD と同じ persist）を同じ attempt で入れた（§7）。§5 の T1 の QEMU 待ち。cleared は Q1 の判定）
Parent: [WS220](../ws.md)
Queue: q945（承認: 2026-10-13 ユーザーの再開の指示「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」と WS220 ws.md のユーザーの決定。M7 はユーザーの決定 2026-10-13「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」）
設計の出所: [p001 §4 の p005 の行](../phase001/phase.md)、[p001 review-1](../phase001/review-1.md) の M3・S5-2、[p004](../phase004/phase.md) の §6、本 Phase の [review-1](review-1.md) と Q1 の決め、[FreeBSD の persist の調べ](freebsd-persist.md)
依存: p004（main 51f753f27）の出力。p003・p004 は T1 待ち（T1-577・T1-579）。結果で分類や RTO が変わったら §1 を見直す。

## ゴール

1. 重複の ACK（RFC 5681 2 の定義）が 3 つ続いたら最古の segment を直ちに再送する（RFC 5681 3.2）。
2. NewReno の回復（RFC 6582）: `recover` = 回復に入った時の送った端。`recover` に届かない ACK（partial）ごとに次の最古を直ちに再送し、届いたら回復を終える。
3. RTO の後も同じ回復に入る（RFC 6582 4）。p004 の `recovery_end` の連鎖をこれに統一する。
4. 窓が開いた時の即時の再送を外し、窓が入れない所へ出した probe だけを窓が開いたら出し直す。
5. 窓 0（足りない窓）は FreeBSD と同じ persist timer で probe する（間隔 clamp(RTO × backoff, 5 s, 60 s)、答えない相手と close の後の相手を落とす、`TCP_MAXUNACKTIME`）。
6. net.tcpstats: `TCP_STATS_HAVE_FAST_RETRANSMIT`、`fast_retransmits`。

範囲の外: cwnd・ssthresh（p006）、limited transmit（RFC 3042、p006 で決める）、F-RTO（p006 の前に決める）、受けの窓と SACK（p007・p008）、共有の pool を窓 0 の相手が握る問題（p006(b) の送りの buffer の切り離し）。

## 1. 決めたこと（review-1 と M7 の後）

| 項目 | 決め | 理由 |
| --- | --- | --- |
| 重複の ACK | 条件は RFC 5681 2: 送った segment があり、ACK が**前の** SND.UNA と同じ（L1）、data・SYN・FIN 無し、窓が前と同じ、何も retire しない。`duplicate_acks_in`（統計）は全部数える | |
| 続けての数 `duplicate_acks` | 重複のうち、最古の segment が窓に入る（**窓が閉じていない**、G1(c)）もので、RTO の回復が `recover` ちょうどで終わった後のその `recover` の重複でない（M1 の `dupack_guard`）ものだけ 1 足す。数えない重複・窓の変わる ACK・data の ACK は数を変えない（L2）。0 に戻すのは進む ACK（segment の途中までの ACK も、L1）・RTO・reset | 閉じた窓への答えは損失でない（FreeBSD は persist の間数えない）。zedBSD 同士でも 3 つに届く（模型の lan-loss-1-zedbsd の fast 146） |
| fast retransmit | 3 つ目で、回復の外なら: `recover` = 送った端、回復に入る、最古を即時の再送（種類 LOSS）。`fast_retransmits` と `immediate_resends` を 1 足す。**回復の外では `recover` と比べない**（G2） | RFC 5681 3.2、RFC 6582 3.2。回復の外の比べは 2^31 の剰余で 2 GiB ごとに偽になる（G2） |
| `recover` の命 | 回復の中だけ意味を持つ。full ACK・`tcp_retransmit_reset`・新しい connect で回復を終える。送った印が未送信に戻って送った端が `recover` より手前になったら `recover` を下げる（L3） | G2・L3 |
| partial ACK | 回復の中で `before(ACK, recover)` だけで判定（`retransmit_count` は見ない、M5）、最古を即時の再送（種類 LOSS） | RFC 6582 3.2 の 5 |
| RTO の後 | 本当の timeout で回復に入る（`recovery_by_timeout`）。その回復が `recover` ちょうどの ACK で終わったら、その `recover` の重複を ACK が越えるまで数えない（M1、RFC 6582 4.1 の代わり） | 偽の timeout の後の go-back の答えで偽の fast retransmit をしない |
| Karn（M2） | 即時の再送の種類: LOSS（fast retransmit・partial ACK）は計りをいつも捨てる、PROBE（窓が開いた probe）は計っている segment の時だけ。本当の timeout はいつも捨てる（p004 のまま） | p004 で計りを残したのは窓が開いた時の再送が毎往復来たため。p005 はそれを外したので、穴の後ろで計った segment の膨らんだ sample を防ぐ |
| 拒まれた再送（M4） | 「まだ最古で、この再送が最後か」を `retransmit_serial`（timer の再送ごとの通番、重複の ACK で戻らない）で判定する | `retransmit_count` は重複の ACK で 0 に戻り、判定が偽になって短い retry が戻らなかった |
| probe の印（G1(a)） | `struct tcp_pending` の `probe`: その segment の**直近の送り**（最初の送り・timer の再送・fast retransmit・partial ACK の再送・persist の probe のどれでも）の終わりがその時の SND.UNA + 相手の窓を越えていたら 1、送るたびに付け直す。L4 の「socket に 1 つの欄」は採らない: 窓の外に出した新しい segment が後で最古になるので、segment ごとに要る（p006(b) で送りごとの記録に移す） | |
| 出し直し（G1(b)） | **どの ACK の後でも**、最古が probe で SND.UNA + 窓がその終わりに届いたら即時の再送（種類 PROBE、即時の再送が既に決まっていなければ）。probe の印を外し、`rto_backoff` = 0、persist を抜ける（M3） | 窓が開くのは ACK の進みと同時のこともある |
| persist（M7、ユーザーの決定: FreeBSD と同じ） | **再送と分ける**。最古の data の segment が窓に入らない状態（ESTABLISHED・CLOSE_WAIT・FIN_WAIT_1・LAST_ACK）で再送の期限が来たら persist: probe（今の segment の形）を出し直し、次の期限 = clamp(RTO × backoff[shift], 5 s, 60 s)、backoff = {1,2,4,8,16,32,64,128,256,512,512,512,512}、shift は 12 まで。`rto_backoff`・回復・R2 は動かさない。persist に入る時（最初の probe を送った時、timer が persist を見つけた時）と抜ける時（窓が開いた・probe が ACK された・窓に入る segment の再送へ移った・reset）に shift = 0 | FreeBSD stable/15 の tcp_setpersist・tcp_timer_persist（[調べ](freebsd-persist.md)）。p001 §4 の「persist timer を置く」の形。M3（窓 0 の backoff が残る）も解ける |
| 答えない相手（M7 (2)） | persist の期限で、shift ≥ 12 かつ now − `last_receive` ≥ min(2 h, RTO × 2559) なら ETIMEDOUT で落とす。`last_receive` は相手の segment を受けるたびと確立で更新 | FreeBSD と同じ。答え続ける相手は落とさない（RFC 1122 4.2.2.17、RFC 6429） |
| close の後（M7 (3)） | FIN_WAIT_1・LAST_ACK の persist で now − `last_receive` ≥ 60 s なら落とす（答える相手には効かない） | FreeBSD と同じ |
| `TCP_MAXUNACKTIME`（M7 (4)） | UAPI の `<uapi/netinet.h>` に `TCP_MAXUNACKTIME 68`（FreeBSD と同じ番号、int の秒、0 = 無効）。setsockopt・getsockopt、listener の子は受け継ぐ。system の既定は `CONFIG_TCP_MAXUNACKTIME_MS`（0、build の定数。sysctl の口は足さない: 今の zedBSD の sysctl は読むだけの項目ばかりで、書く口の仕組みから作ることになり重い）。`unack_since` = 何も待っていない時に data を積んだ時刻・2 byte 以上 ACK された時刻、全部 ACK で 0。timer が毎回確かめ（`tcp_timer_next_deadline` にも期限を入れる）、超えたら SND.UNA の sequence で RST を送って ETIMEDOUT で落とす | FreeBSD と同じ。SND.UNA にしたのは RFC 5961 の受け手が RCV.NXT ちょうどだけを受けるため（模型で B が閉じた）。既定を 0 以外にする・pool の逼迫で落とすはユーザーの決定で入れない |
| net.tcpstats | `TCP_STATS_HAVE_FAST_RETRANSMIT`、`fast_retransmits`（`struct tcp_counters` にも、M6）。`immediate_resends` = fast retransmit・partial ACK・probe の出し直し。`backoff` は再送の指数で、probe の答え・出し直しでも 0 に戻り、persist の probe はそれと別（注記） | UAPI の大きさ・版は不変 |
| L5・L6 | 記録だけ: L5 の「60 s の probe が 2 回答えを失うと約 120 s で諦める」は persist を分けたので当たらない（persist は R2 でなく FreeBSD の規則で諦める）。L6 の最初の probe を窓 0 で直ちに出すのは今のまま（RFC 9293 SHLD-29 から外れる） | |

不変条件:
- `recovery` ≠ 0 の間だけ `recover` が意味を持つ。fast retransmit は回復の外でだけ起きる。
- persist の間（`persist` ≠ 0）は最古の data の segment が窓に入らず、再送の指数・回復は動かない。
- probe の印はその segment の直近の送りについての印で、送るたびに付け直される。

cwnd の無い段の NewReno（M8）: 損失の後も送る量を減らさないのは RFC 5681 の MUST（ssthresh の半減）に反する。ring が 8 segment（8 KB）の間だけ許す。ring の既定を広げるのは p006(a)（cwnd）の後に限る（ws.md に記録）。

## 2. 変えた file

- `src/kern/net/tcp.c`:
  - 定数 `TCP_DUPLICATE_ACKS_FAST`・`TCP_IMMEDIATE_LOSS`・`TCP_IMMEDIATE_PROBE`・`TCP_PERSIST_*`・`CONFIG_TCP_MAXUNACKTIME_MS`、表 `tcp_persist_backoff`。
  - 関数 `tcp_probe_fits_locked`・`tcp_oldest_fits_window_locked`・`tcp_persist_applies_locked`・`tcp_persist_interval_locked`・`tcp_persist_given_up_locked`・`tcp_maxunack_expired_locked`・`tcp_persist_leave_locked`。
  - ACK の受理: 前の SND.UNA、重複の数え（窓・guard）、fast retransmit、どの ACK の後でも probe の出し直し。
  - `tcp_retire_acknowledged`: partial ACK・full ACK・guard、probe の答えで指数と persist を戻す、`unack_since`。
  - `tcp_timer_run`: `TCP_MAXUNACKTIME`（RST と drop）、persist と再送の分岐（persist の間隔・shift・諦め、R2 は再送の時だけ）、通番、Karn の種類、probe の印。
  - `tcp_mark_sent_locked`: probe の印と persist に入る。`tcp_unmark_sent_locked`: `recover` を下げる。`tcp_send_reliable`: `unack_since`。`tcp_timer_next_deadline`: `TCP_MAXUNACKTIME` の期限。setsockopt・getsockopt: `TCP_MAXUNACKTIME`。`tcp_passive_syn`: 子に受け継ぐ。確立と受けで `last_receive`。reset・connect で状態を消す。`tcp_recvfrom` の注記を直した（G1(d)）。
- `include/kern/net/tcp-socket.h`: `tcp_pending.probe`、`tcp_counters.fast_retransmits`、欄 `duplicate_acks`・`recover`・`recovery`・`recovery_by_timeout`・`dupack_guard`・`dupack_guard_sequence`・`retransmit_serial`・`persist`・`persist_shift`・`last_receive`・`maxunack`・`maxunack_seconds`・`unack_since`。
- `include/uapi/netinet.h`: `TCP_MAXUNACKTIME`（68）。
- `include/uapi/sysctl.h`: 注記だけ（`immediate_resends`・`backoff`）。
- `plan/ws220/tests/analyze.py`: COUNTS に `fast_retransmits`（M6）。
- `plan/ws220/tests/tcp-model/model.c`・`run.sh`: §3。

## 3. 模型の scenario

模型の変更: `--drop-ab LIST`、`--expect-fast-retransmits LOW:HIGH`、`--expect-timeouts-max N`、`--close-at-ms T`（A の書き手が止まり SHUT_WR）、`--maxunacktime S`、`--expect-probe-gap-s LOW:HIGH`（persist の間の probe の間隔）。`rtt:` の行に `fast`・`timeouts`、`close:`（A が閉じた時刻、B の状態）と `persist:`（probe の間隔）の行。A が閉じた後は 1 s 進めて RST を B に届ける。

| scenario | 確かめること | p004 | p005 |
| --- | --- | --- | --- |
| lan-loss-0.1（目標 10） | 損失 0.1 %、Linux 相当の窓 | 4.63 FAIL、停止 202 ms | **36.54 PASS**、停止 1.3 ms、fast 30、timeout 0 |
| lan-loss-1（目標 2） | 損失 1 % | 0.46 FAIL、1212 ms | **12.16 PASS**、203 ms、fast 77、timeout 2、再送 85 |
| rtt-10ms-loss-0.1 | RTT 10 ms、損失 0.1 % | 0.65 FAIL | 0.77 FAIL（ring の上限 0.78。損失の値段は消えた、timeout 0。**目標は p006 へ移す**、M6） |
| lan-loss-1-zedbsd・queue-1-loss-1 | zedBSD 同士の損失（S5-2） | 14.64 | **17.43**・17.35 PASS |
| slow-reader | 遅い読み手（S5-2、G1 の比べ） | 1.87、停止 180 ms | 1.87、停止 179 ms PASS、再送 8566（p003 14246、fast 779） |
| zero-window | 5 s の窓 0 | 2.95、再送 7 | 2.95 PASS、再送 3、閉じた窓の間の fast retransmit 0（開いた後の 1 は拒まれた segment の穴） |
| two-holes・two-holes-zedbsd | 1 つの窓に 2 つの穴（穴の後ろ 4 segment） | — | fast 1、partial の再送 1、timeout 0 PASS |
| window-lost | 1 つの窓を全部失う | — | timeout 1、partial の再送 7、偽の fast 0 PASS |
| rtt-step・rtt-step-zedbsd | 偽の timeout（RTT 0.2 → 600 ms） | zedBSD の窓で収まらない | 両方 PASS、再送 6、fast 0、SRTT 593595 μs |
| delayed-ack-200 | 遅延 ACK 200 ms | 再送 0 | 再送 0、fast 0 PASS |
| lan-wrap-loss（G2） | wrap を跨ぐ損失 | — | 36.31 PASS、fast あり・timeout 0 |
| persist-answering（M7） | 読み手が 300 s 止まり答え続ける | — | 落ちない、probe の間隔 5〜60 s（再送 14）、全部届く PASS |
| persist-silent（M7 (2)） | 窓 0 の後に相手が黙る | — | 542 s で ETIMEDOUT（probe 16、間隔 5〜60 s） PASS |
| persist-closed（M7 (3)） | A が 1 s で SHUT_WR、相手は 2 s から黙る | — | 70.6 s（黙ってから約 60 s）で落ちる PASS |
| maxunacktime（M7 (4)） | `TCP_MAXUNACKTIME` 10 s、答え続ける相手 | — | 10.1 s で落ち、B は RST で CLOSED PASS |

既存の scenario の再送の上限は p005 の値（slow-reader 8566、lan-loss-1 85）。

## 4. 実行した command と結果

- `git merge main`（3f0d01614）。
- 模型の全部: `OUT=build/p3-ws220-host sh plan/ws220/tests/tcp-model/run.sh`（log `build/p3-ws220-p005-model5.log`）: FAIL は rtt-10ms（0.78）と rtt-10ms-loss-0.1（0.77）の 2 つだけ（ring の上限、p006）。他は全部 PASS（unresolved-giveup・data-giveup 104.7 s、syn-giveup・connect-unresolved 183 s、p003・p004 の scenario も）。
- build（warning 0）: amd64（vmunix・sysctl、vmunix check PASS）、rpi4（vmunix check PASS）、pcat（i386）。log `build/p3-ws220-p005p-*.log`。
- `python3 plan/tools/style-check.py src/kern/net/tcp.c`: 111（p004 の tcp.c と内容で比べ新しい指摘 0）。model.c の増えた指摘は option の解き方の `strcmp` だけ（既存の形、p009）。`git diff --check` ok。analyze.py: 合成の series で `fast_retransmits=6` の数えを確かめた。
- 中間の commit 14d230247（review-1 の直し、persist の前）。

未実施: QEMU（§5）、実機。

## 5. T1 への依頼（ws220-p004 の T1-579 の後、同じ形で）

- 何: ws220-p005 の QEMU の確かめ。T1-579（p004）と同じ測りを p005 の kernel で流して比べる。
- image A: p005 を merge した main の SHA の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk build/t1-ws220-p005`。
- 流す物: (1) guest で root の `sysctl net.tcpstats | head -3`: SSH の接続の行に `features=0x32`。(2) `plan/ws220/tests/tcp-stream-series.sh IMAGE_A --bytes 1073741824 --out build/ws220-p005-series-0`。(3) `... --loss 0.1 --bytes 268435456 --out build/ws220-p005-series-01`。(4) `... --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-p005-series-1`。
- 合否（review-1 M6 の案）:
  - 各 run が `tcp-stream-series: PASS`、`features=0x32`。
  - 損失 0.1 % の up: 終わりの net.tcpstats の `fast_retransmits` > 0、wire の `longest_stall_ms` が T1-579 より短い、`retransmit_segments` が損失の数（wire の `resent_segments`）に近い。損失 1 % の up は `fast_retransmits` > 0 と完走、記録。
  - 損失 0 の up・down・down-disk: mean が T1-579 の 90 % 以上。損失 0 の up の `retransmit_segments` が T1-579 以下。
  - 損失のある down は記録だけ（受けの窓は p007）。
- 返す物: T1-579 と同じ（summary.txt の `analyze:` の行（summary・wire・rtt、summary に `fast_retransmits` の増え）、series-*.csv、終わりの net.tcpstats の `fast_retransmits`・`immediate_resends`・`retransmit_segments`・`duplicate_acks_in`・`backoff`・`rto_ms`、settings.txt、失敗なら transfer-*.txt・setup.txt、pcap は path だけ）。

## 6. 残りと判断

- **共有の pool を窓 0 の相手が握る問題（M7）**: ユーザーの決定（2026-10-13「FreeBSD と同じ＋p006 で pool を切り離す」）で、p005 は FreeBSD と同じ persist・諦め・`TCP_MAXUNACKTIME`（既定 0）まで。答え続ける相手は既定では落とさないので、接続ごとに ring の packet（最大 8 個）を握り続けられる。p006(b)（送りの buffer を pool から切り離す）で解く。pool の逼迫で落とす・`TCP_MAXUNACKTIME` の既定を 0 以外にする、は入れない。
- **M8**: cwnd の無い NewReno は ring ≤ 8 の間だけ許す。ring の既定を広げるのは p006(a) の後。
- M6: rtt-10ms-loss-0.1 の目標 2 MB/s は ring の上限 0.78 で届かないので p006 へ移す。p001 §4 の「slow-reader の `immediate_resends` 0」は p007(a)（窓の約束）の目標に移す（Q1 の判断）。
- 重複の ACK の (e)（窓が同じ）は緩めていない。模型では zedBSD 同士でも数えられた。実の網で足りなければ p007 と一緒に決める。
- 偽の timeout の後の partial ACK の再送（rtt-step の 6）は cwnd が無い間の go-back の代わりとして残る。F-RTO は p006 の前に決める。
- persist の probe は今の segment の形（1 segment）。`TCP_MAXUNACKTIME` の system の既定を変える sysctl は作っていない（build の定数だけ）。
- 削除してほしい出力（Q1、P3 の worktree）: `build/tmp/p3-p005r.tFTu`（style の比べ・analyze の確かめ）、`build/p3-ws220-pcat5`、`build/p3-ws220-p005*.log`・`build/p3-ws220-p005-model*.log`。

## 7. review-1 と M7 への対応（2026-10-13、6923b94e6 の設計の review、全文は [review-1.md](review-1.md)）

| 指摘 | 対応 |
| --- | --- |
| G1 閉じた窓に出した segment を出し直す道 | (a) probe の印を直近の送りごとに付け直す (b) どの ACK の後でも出し直す (c) 窓が閉じている間の重複は数えない (d) `tcp_recvfrom` の注記。slow-reader・lan-loss-1-zedbsd を p004 と比べた（§3） |
| G2 recover を回復の外で比べる | 回復の外では比べない。full ACK・reset・新しい connect で回復を終える。lan-wrap-loss |
| M1 RTO の後の重複 | RTO の回復が `recover` ちょうどで終わったら、その重複を越えるまで数えない（`dupack_guard`）。rtt-step・rtt-step-zedbsd・delayed-ack-200 で fast 0 |
| M2 Karn | LOSS の即時の再送は計りをいつも捨てる |
| M3 窓 0 の backoff が残る | probe の答え・出し直しで `rto_backoff` = 0。persist を分けて shift も入る時と抜ける時に 0 |
| M4 拒まれた再送の判定 | `retransmit_serial` |
| M5 partial ACK の判定 | `recovery` と `before(ACK, recover)` だけ。two-holes は穴の後ろ 4 segment |
| M6 受け入れの基準 | §3・§5 に。rtt-10ms-loss-0.1 の目標は p006 へ、slow-reader の `immediate_resends` 0 は p007(a) へ。analyze.py と `tcp_counters` に `fast_retransmits` |
| M7 窓 0 の相手 | ユーザーの決定で FreeBSD と同じ persist・諦め・`TCP_MAXUNACKTIME` を入れた（§1）。pool の問題は p006(b)（§6、ws.md） |
| M8 cwnd の無い NewReno | §1・§6・ws.md |
| L1〜L4 | L1 前の SND.UNA と比べ、途中までの ACK で数を 0。L2 数えない重複は数を変えない。L3 `recover` を下げる。L4 は採らない（§1 の理由） |
| L5・L6 | §1 に記録 |
