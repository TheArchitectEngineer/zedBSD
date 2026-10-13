<!-- awesome-plan project=zedbsd record=ws220-p005 -->
# ws220-p005: fast retransmit と NewReno の回復、窓 0 の probe、即時の再送の整理

Status: in-progress / test-wait（2026-10-13 P3: 設計・実装・模型・build（amd64・rpi4・pcat、warning 0）は済み。design-reviewer の結果と §5 の T1 の QEMU 待ち。cleared は Q1 の判定）
Parent: [WS220](../ws.md)
Queue: q945（承認: 2026-10-13 ユーザーの再開の指示「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」と WS220 ws.md のユーザーの決定）
設計の出所: [p001 §4 の p005 の行](../phase001/phase.md)、[p001 review-1](../phase001/review-1.md) の M3（NewReno の詳細と既存の規則の衝突）・S5-2（窓が開いた時の再送は zedBSD の受け手の拒否の唯一の速い回復）、[p004](../phase004/phase.md) の §6（RTT が RTO を超える時の窓が開いた時の再送、回復の範囲 `recovery_end` を p005 で置き換える）
依存: p004（main 51f753f27）の出力（`rto_backoff`・`retransmit_immediate`・Karn の範囲・`recovery_end`）。p003・p004 は T1 待ち（T1-577・T1-579）。結果で分類や RTO が変わったら §1 を見直す。

## ゴール

1. 重複の ACK（RFC 5681 2 の定義）を 3 つ続けて受けたら、最古の segment を直ちに再送する（fast retransmit、RFC 5681 3.2）。
2. NewReno の回復（RFC 6582）: 回復に入った時の送った端を `recover` に置き、`recover` に届かない ACK（partial ACK）ごとに次の最古を直ちに再送し、届いたら回復を終える。2 度目の fast retransmit は `recover` を越えた ACK の後だけ。
3. RTO の後も `recover` = その時の送った端（RFC 6582 4、go-back で来る重複の ACK で偽の fast retransmit をしない）。RTO の後の partial ACK は同じ規則で次を再送する（p004 の `recovery_end` の連鎖をこの規則に置き換える）。
4. 窓が開いた時の即時の再送を外す。残すのは窓 0（足りない窓）に送った probe の segment だけ: 窓が開いたら直ちに出し直す（RFC 9293 3.8.6.1 の persist の続き）。probe の出し直しは再送の timer が backoff で行い、相手が答える間は諦めない（RFC 1122 4.2.2.17、p002 の R2）。
5. net.tcpstats: `TCP_STATS_HAVE_FAST_RETRANSMIT`（0x10）を立て、`fast_retransmits` を数える。

範囲の外: cwnd・ssthresh・fast recovery の窓の膨らませ（p006、ここでは ring と相手の窓だけが送りを縛る）、limited transmit（RFC 3042、cwnd が無いので意味が無い。p006 で決める）、F-RTO（p006 の前に決める）、受けの窓と SACK（p007・p008）。

## 1. 決めたこと

| 項目 | 決め | 理由 |
| --- | --- | --- |
| 重複の ACK の数 | 新しい欄 `duplicate_acks`（続けて来た数）。数えるのは今の `duplicate_acks_in` と同じ条件（送った segment があり、ACK = SND.UNA、data・SYN・FIN 無し、窓が前と同じ、RFC 5681 2）。進む ACK、RTO、reset で 0 | M3 の 3 点目: zedBSD の受け手は読み手が読むと窓を変えるので数えられない場合がある。RFC の定義は変えず、模型の lan-loss-1-zedbsd・slow-reader で確かめる（足りなければ (e) を緩める案を §6 に） |
| fast retransmit | 3 つ目の重複の ACK で、回復の中でなく、かつ ACK が `recover` を越えている（`recover` が無いか、at_or_before(recover, ACK)）なら: `recover` = 送った端、回復に入る、最古を即時の再送（`retransmit_deadline` = now、`retransmit_immediate`、指数は進めない）。`fast_retransmits` を 1 足す | RFC 5681 3.2、RFC 6582 3.2 の 1〜2 |
| 回復の中 | 重複の ACK は何もしない（cwnd が無いので膨らませも無い）。partial ACK（進むが `recover` に届かない）は次の最古を即時の再送（RFC 6582 3.2 の 5）。期限は進む ACK ごとに張り直す（RFC 6298 5.3、slow-but-steady）。full ACK（`recover` 以上）で回復を終える | RFC 6582。p006 で cwnd を足す時に deflation などを入れる |
| RTO の後 | 本当の timeout で `recover` = 送った端、回復に入る（`duplicate_acks` = 0）。partial ACK は同じく次の最古を再送 | RFC 6582 4（偽の fast retransmit を防ぐ）。cwnd が無いので RTO の後の go-back を slow start に任せられない。partial ACK で穴だけを出し直すのが p004 の `recovery_end` の連鎖と同じ働きで、それを `recover` の規則に統一する（`recovery_end`・`recovery_active` は `recover`・`recovery` に名を替える） |
| 窓が開いた時の再送 | 外す。ただし最古の送った segment が **probe**（送った時、その終わりが SND.UNA + 相手の窓を越えていた = 窓 0 か足りない窓に「1 つは送ってよい」で出した）なら、窓がその segment を入れるだけ開いた ACK で直ちに出し直す（即時の再送、指数は進めない） | p001 §4 p005。probe は相手が拒んだと分かっている segment なので出し直しは偽でない（BSD の persist で窓が開くと snd_nxt を戻して送るのと同じ）。それ以外の「窓が開いた」は相手が受けた segment を拒んだ証拠にならず、偽の再送（p004 §6）になる |
| probe の印 | `struct tcp_pending` に `probe`（uint8_t）。`tcp_mark_sent_locked` と、再送の timer の再送（窓が入れない所へ出した再送も probe）で付け直す。窓が開いて出し直したら外す | 実装で足した: timer の再送を probe にしないと、窓 0 の間に timer が出した再送は窓が開いても backoff の期限まで待った（模型 zero-window で 5.43 → 6.69 s に退行した） |
| persist timer | 別の timer は作らない。probe の segment は再送の timer が `rto_backoff` で倍々に出し直し（上限 60 s）、進まない ACK でも指数は戻らず（p004 G2）、R2 は相手の ACK で戻る（p002）ので諦めない | RFC 9293 3.8.6.1（probe は指数で間を伸ばす SHOULD）、RFC 1122 4.2.2.17（答える相手を諦めない）。今の ring の作りでは 1 つの segment の出し直しが probe と同じ働きをする |
| zedBSD の受け手の拒否（S5-2、候補 5） | 窓が開いた時の再送を外すと、受け手が queue の満杯で順番の segment を拒んだ時の速い回復は、その後ろの segment が起こす重複の ACK の fast retransmit になる（後ろが無ければ RTO）。模型の slow-reader・lan-loss-1-zedbsd の非退行を受け入れに入れる。根本の直しは p007(a)（窓の約束） | |
| Karn | 変えない（p004: 即時の再送は、それが計っている segment の時だけ計りを捨てる） | fast retransmit も即時の再送の道を通る |
| R2・帳簿 | 変えない（`retransmit_count` は最古の再送の数、`retransmit_since` は相手の ACK で戻る） | M3 の 2 点目は p004 の G2 で済んだ |
| net.tcpstats | `features` に `TCP_STATS_HAVE_FAST_RETRANSMIT`、`fast_retransmits`。`immediate_resends` は partial ACK と probe の出し直しと fast retransmit を含む即時の再送の数（注記を直す） | UAPI の欄は既にある、大きさ・版は不変 |

不変条件:
- `recovery` ≠ 0 の間、`recover` は回復に入った時の送った端で、ACK が `recover` 以上で 0 に戻る。
- fast retransmit は回復の外で、`recover` を越えた ACK の後にだけ起きる（1 つの窓に 1 回）。
- probe の印は送った segment にだけ付く。

## 2. 変えた file

- `src/kern/net/tcp.c`:
  - 定数 `TCP_DUPLICATE_ACKS_FAST`（3）、関数 `tcp_probe_fits_locked`。
  - ACK の受理: 重複の ACK を `duplicate_acks` に続けて数え、3 つ目で fast retransmit（回復の外、`recover` を越えた後）。窓が開いた時の即時の再送は probe が入る時だけ。
  - `tcp_retire_acknowledged`: p004 の「`retransmit_count` ≠ 0 かつ `recovery_end` の前」を、NewReno の「回復の中で `recover` に届かない ACK（partial）」に替えた。進む ACK で `duplicate_acks` = 0。
  - `tcp_timer_run`: 本当の timeout で `recover`・回復・`duplicate_acks` = 0（`recovery_end` の置き換え）。再送が窓に入らなければ probe の印。
  - `tcp_mark_sent_locked`: probe の印。`tcp_retransmit_reset`・`tcp_rtt_forget_locked`: 回復・`recover`・`duplicate_acks` を消す。
  - net.tcpstats: `TCP_STATS_HAVE_FAST_RETRANSMIT`、`fast_retransmits`。
- `include/kern/net/tcp-socket.h`: `struct tcp_pending` に `probe`、`tcp_counters` に `fast_retransmits`、`recovery_end`・`recovery_active` を `duplicate_acks`・`recover`・`recover_set`・`recovery` に。
- `include/uapi/sysctl.h`: 注記だけ（`immediate_resends` の意味）。
- `plan/ws220/tests/tcp-model/model.c`・`run.sh`: §3。

## 3. 模型の scenario

模型の変更: `--drop-ab LIST`（A の data の frame を番号で落とす）、`--expect-fast-retransmits LOW:HIGH`、`--expect-timeouts-max N`（本当の timeout = 指数の段の数）。`rtt:` の行に `fast`・`timeouts`。

| scenario | 確かめること | p004 | p005 |
| --- | --- | --- | --- |
| lan-loss-0.1（目標 10） | 損失 0.1 %、Linux 相当の窓 | 4.63 FAIL、停止 202 ms | **36.54 PASS**、停止 1.3 ms、fast 30、timeout 0 |
| lan-loss-1（目標 2） | 損失 1 % | 0.46 FAIL、1212 ms | **12.16 PASS**、203 ms、fast 77、timeout 2（二重の損失 1 と流れの尻）、再送 85 |
| rtt-10ms-loss-0.1（目標 2） | RTT 10 ms、損失 0.1 % | 0.65 FAIL、222 ms | 0.77 FAIL（**ring の上限 0.78 = 損失の値段は消えた**、timeout 0、停止 21 ms。目標は p006） |
| lan-loss-1-zedbsd・queue-1-loss-1 | zedBSD 同士の損失（S5-2 の非退行） | 14.64 | **17.43**・17.35 PASS |
| slow-reader | 遅い読み手（S5-2 の非退行） | 1.87、再送 ≤ 14246 | 1.91 PASS、再送 6929（fast 1568） |
| zero-window | 5 s の窓 0 | 2.95、再送 7 | 2.95 PASS、再送 6（probe の印が無いと 2.39） |
| two-holes・two-holes-zedbsd（新） | 1 つの窓に 2 つの穴 | — | fast 1・partial の再送 1・timeout 0、35.75・35.53 PASS |
| window-lost（新） | 1 つの窓（8 segment）を全部失う | — | timeout 1、partial の再送 7、偽の fast retransmit 0 PASS |
| rtt-step・rtt-step-zedbsd（新） | RTT 0.2 → 600 ms（偽の timeout）、Linux 相当と zedBSD の窓 | rtt-step 再送 1、zedBSD の窓では収まらない（p004 §6） | 両方 PASS: 再送 6（偽の timeout の後の partial ACK の再送、1 つの窓の内）、fast 0、SRTT 593595 μs |

既存の scenario の再送の上限は p005 の値に更新した（slow-reader 6929、lan-loss-1 85。上限は退行の検出に使う）。

## 4. 実行した command と結果

- `git merge main`（3f0d01614、main 51f753f27 以後）。
- 模型の全部: `OUT=build/p3-ws220-host sh plan/ws220/tests/tcp-model/run.sh`（log `build/p3-ws220-p005-model2.log`）: FAIL は rtt-10ms（0.78）と rtt-10ms-loss-0.1（0.77）の 2 つだけ（どちらも ring の 8 KB / 往復の上限、p006）。他は全部 PASS（data-giveup 103.7 s、syn-giveup・connect-unresolved 183 s、unresolved 15.43、p003・p004 の scenario も PASS）。
- build（warning 0）: amd64（vmunix・sysctl、vmunix check PASS）、rpi4（vmunix check PASS）、pcat（i386、Q1 が link を直した後、vmunix まで通る）。log `build/p3-ws220-p005-*.log`。
- `python3 plan/tools/style-check.py src/kern/net/tcp.c`: 111（p004 の tcp.c と内容で比べ新しい指摘 0）。model.c の増えた指摘は option の解き方の `strcmp` 3 つ（既存の形、p009）。`git diff --check` ok。

未実施: QEMU（§5）、実機、design-reviewer の review（Q1 が回す）。

## 5. T1 への依頼（ws220-p004 の T1-579 の後、同じ形で）

- 何: ws220-p005 の QEMU の確かめ。T1-579（p004）と同じ測りを p005 の kernel で流して比べる。
- image A: p005 を merge した main の SHA の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk build/t1-ws220-p005`。
- 流す物: (1) guest で root の `sysctl net.tcpstats | head -3`: SSH の接続の行に `features=0x32`（HAVE_SEQUENCE・HAVE_RTT・HAVE_FAST_RETRANSMIT）。(2) `plan/ws220/tests/tcp-stream-series.sh IMAGE_A --bytes 1073741824 --out build/ws220-p005-series-0`。(3) `... --loss 0.1 --bytes 268435456 --out build/ws220-p005-series-01`。(4) `... --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-p005-series-1`。
- 合否:
  - 各 run が `tcp-stream-series: PASS`。
  - 損失 0.1 %・1 % の up: 終わりの net.tcpstats の `fast_retransmits` が 0 でない、mean の MB/s が T1-579 より高い、wire の `longest_stall_ms` が T1-579 以下。損失 0.1 % の up の mean は損失 0 の up の 50 % 以上（目安、ECM の上限）。
  - 損失 0 の up・down・down-disk: mean が T1-579 の 90 % 以上、損失 0 の up の `retransmit_segments` が T1-579 以下（窓が開いた時の再送を外したので減るはず）。
  - 損失 0.1・1 % の down（Linux の host が送る）は記録だけ（zedBSD の受けの窓は p007）。
- 返す物: T1-579 と同じ（summary.txt の `analyze:` の行、series-*.csv、終わりの net.tcpstats の `fast_retransmits`・`immediate_resends`・`retransmit_segments`・`duplicate_acks_in`・`backoff`・`rto_ms`、settings.txt、失敗なら transfer-*.txt・setup.txt、pcap は path だけ）。

## 6. 残りと判断

- 重複の ACK の定義（窓が同じ）は変えていない。zedBSD 同士でも模型では数えられた（lan-loss-1-zedbsd の fast 146）。読み手が忙しく窓が毎回変わる実の網で数えられなければ、(e) を緩める（窓の更新だけの違いを許す）案を p007 と一緒に決める。
- 偽の timeout の後の partial ACK の再送（rtt-step の 6）は、cwnd が無い間の go-back の代わりとして残る。F-RTO（RFC 5682）は p006 の前に決める（p004 §6）。
- limited transmit（RFC 3042）は cwnd が無いので入れていない（p006 で決める）。
- slow-reader の再送（6929）の多くは zedBSD の受け手の拒否（候補 5、`receive_refused`）の回復。p001 の目標「slow-reader の `immediate_resends` 0」は p007(a)（窓の約束）の後。
- rtt-10ms・rtt-10ms-loss-0.1 の目標は ring の上限で p006。
- 削除してほしい出力（Q1、P3 の worktree）: `build/tmp/p3-p005.FQEC`（style の比べ）、`build/p3-ws220-pcat5`（pcat の build の確かめ）、`build/p3-ws220-p005-*.log`・`build/p3-ws220-p005-model*.log`。
