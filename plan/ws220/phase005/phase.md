<!-- awesome-plan project=zedbsd record=ws220-p005 -->
# ws220-p005: fast retransmit と NewReno の回復、窓 0 の probe、即時の再送の整理

Status: in-progress（2026-10-13 P3: 設計（§1）を書いた。design-reviewer は Q1 が回す）
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
| probe の印 | `struct tcp_pending` に `probe`（uint8_t）。`tcp_mark_sent_locked` で付ける。未送信に戻した時・取り戻した時は意味を失う（次の印で付け直す） | |
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

（実装の後に書く）

## 3. 模型の scenario

予定: 既存の lan-loss-0.1（目標 10）・lan-loss-1（2）・rtt-10ms-loss-0.1（2）が PASS に。足す物: 1 つの窓に 2 つの穴（特定の frame を落とす option）で RTO 無しに回復、RTO の後の go-back の重複の ACK で偽の fast retransmit が起きない、窓 0 の probe（読み手の停止）の間隔が倍々で窓が開くと直ちに出る、slow-reader の `immediate_resends` が減る、lan-loss-1-zedbsd・slow-reader の非退行、zedBSD 同士の rtt-step（p004 §6）が収まる。

## 4. 実行した command と結果

（実装の後に書く）

## 5. T1 への依頼

（実装の後に書く）

## 6. 残りと判断

（実装の後に書く）
