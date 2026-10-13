# ws220-p005 review-1（phase.md 6923b94e6 の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

照らした code は main 51f753f27 の tcp.c・tcp-socket.h・sysctl.h、tcp-model・analyze.py・sysctl の main.c（行番号は 51f753f27 の tcp.c）。build・QEMU・模型なし。Linux・FreeBSD は記憶による推測。開示: 最初に一度 worktree の未 commit の run.sh を読んだが、指摘はそれに頼らない。

**判定: 致命的 0・重大 2・中 8・軽微 6。実装は進めてよいが、G1・G2 を §1 に入れ直すまで T1 への依頼も merge もしない。** 大筋は RFC 5681 3.2・6582 3.2 の 5・6582 4 と合う。lock と UAPI の大きさ・版の不変も問題なし。誤りの中心は「窓が開いた時の再送」を外した後の閉じた窓・受け手の拒否との組み合わせと、recover を回復の外でも比べ続けること。

## 重大

**G1. 窓が開いても、閉じた窓に出した segment を出し直す道が無くなる。** 根拠: 重複の ACK の数え方（tcp.c:5013-5024）は窓 0 の ACK も重複に数える（窓が前と同じなら）。窓が開いた時の再送（5026-5037）を外す。probe の印は最初の送り（tcp_mark_sent_locked、2682-2715）でだけ付く。窓 0 は TCP_MSS 扱い（2415-2420、3698-3703）。tcp_recvfrom の注記（3851-3862）は窓の更新の ACK で送り手が最古を出し直す前提で、窓が「ほぼ閉じていた」時にしか更新を出さない。
筋書き: zedBSD の受け手（候補 5、slow-reader）が queue の満杯で順番の segment X を拒む（X は送った時点で窓の中なので probe の印なし）→ 後ろが順序外で保たれ ACK はどれも窓 0 → 2 つ目以降が重複に数えられ 3 つ目で fast retransmit が窓 0 のまま X を出し再び拒まれる → 読み手が読んで窓が開いても X は probe でないので出し直されず RTO（≥ SRTT + 200 ms、backoff 付き）を待つ。後ろの segment が 3 つ無い時も同じ。§1 の「後ろの segment が起こす重複の ACK の fast retransmit になる」はこの形では成り立たない（p001 review S5-2 の退行）。小さい write では窓 0 でも in_flight + length ≤ 1024 まで受け入れるので probe が複数でき、窓が開いた時に出し直されるのは最古だけで 2 つ目以降は RTO（回復の中でなければ partial ACK も効かない）。
案: (a) probe の印は「最古の segment の直近の送り（最初・timer の再送・fast retransmit・partial ACK の再送のどれでも）の終わりがその時の SND.UNA + 窓を越えていた」とし送るたびに付け直す。(b) 出し直しの判定は window_opened の時だけでなくどの ACK の後でも（最古が probe で SND.UNA + 窓 ≥ その終わり）。(c) 最古の終わりが SND.UNA + 窓を越えている間（窓が閉じている間）の重複の ACK は fast retransmit の数に入れない（FreeBSD は persist の間は数えない、推測）。(d) tcp_recvfrom の注記を直す。模型で slow-reader と lan-loss-1-zedbsd の retransmit_segments・停止・平均を p004 と比べ、窓 0 の重複の ACK が fast retransmit を起こさないことを確かめる。別の道は除去を p007(a) と同じ段へ移すこと。

**G2. recover を回復の外でも比べると 2 GiB ごとに fast retransmit が止まる。** 比べは tcp_sequence_before（1811-1826）の 2^31 の剰余。設計の条件「recover が無いか at_or_before(recover, ACK)」で recover を消す時が書かれていない。回復の後も recover が残ると ACK − recover が 2^31 を越えた所で偽になり次の 2 GiB は fast retransmit が起きず RTO だけ。1 GB 級の転送（WS の受け入れの条件）・長い接続・同じ socket の新しい connect（ISS が変わる）で起きる。回復は ACK ≥ recover でちょうど終わるので回復の外の比べは wrap 以外は常に真で意味が無い（RFC 6582 4 の守りは RTO で回復に入りその間の重複を無視することで得られている）。案: 回復の外では recover と比べない、か full ACK・tcp_retransmit_reset・tcp_rtt_forget_locked（新しい connect）で recover を無効に。模型の lan-wrap に損失（--wrap-before と --loss-ab）か recover の 2^31 後の単体の確かめ。

## 中

**M1. RTO の後の go-back で遅れて来る重複の ACK は recover を越えるので守りが効かない。** 偽の timeout（RTT が RTO を超えた、p004 の rtt-step・遅延 ACK）で、元の ACK が届くたびに partial ACK として次の最古を出し直し最大 7 つの偽の再送、その受け手の ACK（ack = 受け手の RCV.NXT = recover）が full ACK の後に新しい data が出ている時に届くと重複に数えられ 3 つで偽の fast retransmit。新しい data の ACK と交互なら数が戻るので毎回ではない（推測）、ACK が塊で届く USB の adapter で起きやすい。p006 で cwnd が入ると偽の timeout のたびに cwnd が 2 回半分。案: RTO で入った回復が終わった後は ACK = その recover の重複を ACK が recover を越えるまで数えない、か RFC 6582 4.1 の ACK heuristic。模型の rtt-step（--peer-window 無しも）と delayed-ack-200 で fast_retransmits = 0 を合否に。

**M2. Karn の「変えない」は p005 で根拠を失い、fast retransmit の後の sample が膨らむ。** p004 が即時の再送で計りを捨てる範囲を「その segment」だけに絞った理由は窓が開いた時の再送が毎往復来ることで、p005 はそれを外す。穴の後ろで計っている segment は full ACK で sample（RTT + 重複の検出 + 再送の往復、約 2〜3 RTT）。FreeBSD は fast retransmit で t_rtttime = 0（推測）。案: fast retransmit と partial ACK の再送では計りを捨てる。

**M3. 窓 0 の probe を rto_backoff で倍にすると窓が開いた後も backoff が残る。** 窓 0 が長いと指数は 60 s まで進み、窓が開いた時の出し直しは計りを捨てるので sample が無く指数は戻らない。その出し直しが失われると（1 つの write の segment で重複の ACK も無い）次の期限は tcp_backoff_timeout(rto, rto_backoff) で最大 60 s（2276-2289・750-752）。FreeBSD は persist を抜ける時に t_rxtshift = 0、Linux は tcp_ack_probe で icsk_backoff = 0（推測）。案: persist の指数を別に持つか、probe が窓の開きで出し直される時や ACK された時に rto_backoff を 0。net.tcpstats の backoff の注記も合わせる。

**M4. 拒まれた fast retransmit が重複の ACK の連続で RTO に落ちる。** timer の再送が DEVICE か WAIT で拒まれた時の「まだ最古か」の判定は retransmit_count == backoff（810-814）だが、進まない ACK はどれも retransmit_count = 0（2235-2238）で、fast retransmit の前後には残りの重複（4〜7 個目）が必ず来るので偽になり、即時の印も短い retry も戻らず期限は RTO のまま（queue-1-loss-1 のような device の満杯）。案: 判定を重複の ACK で戻らない世代の数（再送の通番）に。§1 の R2・帳簿の行に書く。

**M5. partial ACK の判定に p004 の retransmit_count != 0（2253-2254）を残すと NewReno がほぼ働かない。** fast retransmit の後の重複が count を 0 にし、2 つ目の穴の partial ACK で再送されず RTO。案: partial ACK は recovery ≠ 0 かつ before(ACK, recover) だけで判定し retransmit_count を見ない、と §1 に明記。2 つの穴の scenario は穴の後ろの segment が 4 つ以上で確かめる。

**M6. 受け入れの基準の誤りと欠け（§3・§5）。** rtt-10ms-loss-0.1 の目標 2 MB/s は ring 8 × 1024 B と RTT 10 ms で上限 0.82 MB/s なので届かない（rtt-10ms 自体が 0.78 で FAIL、p006）、§3 の「PASS に」は誤り。p001 §4 の「slow-reader の immediate_resends 0」は §1 の数え方（probe・partial ACK・fast retransmit を含む）では満たせない、「減る」に変えるなら p001 の目標を変えた記録を。§5 の QEMU の合否が空: up の損失 0.1 % で fast_retransmits > 0・wire の longest_stall_ms が p004（T1-579）より短い・retransmit_segments が損失の数の近く、損失 0 の up・down・down-disk の平均が p004 の 90 % 以上、features = 0x32。analyze.py の COUNTS（40-42）に fast_retransmits が無い。struct tcp_counters（tcp-socket.h:74-96）にも無いので足す（p001 review M9）。

**M7. 「相手が答える間は諦めない」と ring の packet の持ち続け。** ring の写しは共有の pool（標準 2 KB × 256）の packet で、読まない相手（窓 0 に答え続ける）は接続ごとに少なくとも 1 つ（候補 5 の拒否なら最大 8）を無期限に握らせ、多数の接続で pool が尽き host 全体の網が止まる（Sockstress 型）。p002 から既にある性質だが p005 が設計として固定する。close の後は TCP_CLOSE_DRAIN_MS で解放（4134-4166）。案: p006(b)（byte の buffer は pool を持たない）で解くと §6 に。暫定の上限を置くかは資源と安全の兼ね合いでユーザーに。

**M8. cwnd の無い段の NewReno。** 損失の後も送る量を減らさないので p006 までは RFC 5681 の MUST（ssthresh の半減）に違反、8 KB の ring だから害は小さいという前提。CONFIG_TCP_SEND_QUEUE_MAX を広げると（what-if の 44）崩れる。案: 「ring ≤ 8 の間だけ許す」を §6 に、ring の既定を広げるのは p006(a) の後に限ると ws.md に。

## 軽微

- L1. 重複の判定は send_unacknowledged = acknowledgement（4980-4983）の後に「ACK == una」を見る（5019）ので、segment の途中までの ACK（BSD の受け手が probe を窓に切る時）も una を進めたのに重複に数える。RFC 5681 の (d) は「前の una と同じ」。前の una を取っておいて比べ、途中までの ACK は進む ACK（数を 0、partial ACK として扱う）と §1 に。
- L2. 「続けて来た数」の意味が未定義。窓の変わる ACK や data を持つ ACK で 0 に戻すかを書く。戻すと zedBSD 同士で 3 つに届かなくなる。戻さず数えもしない形を勧める。
- L3. recover を回復に入った時の送った端にした後、持ち主の送りが device に拒まれて未送信に戻ると（tcp_unmark_sent_locked）送った端が recover より手前に下がり、後で初めて送った segment が partial ACK で偽に出し直される。未送信に戻す時に recover = min(recover, 送った端)。
- L4. probe の印を struct tcp_pending に置くと p006(b) の byte の buffer で作り直しになる。意味があるのは最古だけなので socket の 1 つの欄（probe の終わりの sequence）に。
- L5. backoff の上限 60 s と R2（100 s）で、60 s の probe が 2 回続けて答えを失うと約 120 s で諦める。Linux（probe 15 回）・FreeBSD より短い（推測）。§6 に記録。
- L6. 最初の probe を窓 0 で直ちに出すのは既存で RFC 9293 SHLD-29（最初の probe は RTO の後）から外れる。probe を segment 1 つで出すのは「少なくとも 1 octet」の範囲で問題なし。§1 に記録だけ。

## 判断が要る点

M6（p001 の受け入れの目標の変更）、M7（窓 0 の相手で資源を無期限に握られる）、M8（p006 までの間損失の後も送る量を減らさない）、p001 §4 の「persist timer を置く」→「RTO の timer で兼ねる」（技術判断でよいが M3 の直しとあわせて ws.md に記録）。

## 問題が無かった点

fast retransmit を即時の再送の道（timer の一巡）で出す（network worker は入力の後に期限を見直す、core.c:600-650）。回復の中の重複を無視（cwnd が無い間は正しい）。full ACK・partial ACK の境と RFC 6582 の「送った最大」の対応。R2 を相手の ACK で戻す（p002）まま persist が RFC 1122 4.2.2.17 を満たす。UAPI の TCP_STATS_HAVE_FAST_RETRANSMIT 0x10（sysctl.h:325）と fast_retransmits（451）は既にあり大きさ（header 48、entry 312）と版は不変、WS221 p002 とぶつかるのは注記だけ、sysctl の表示は既に fast_retransmits を出す（main.c:821）。HAL の変更不要、新しい lock 不要。p003・p004 が T1 待ちでも scoped-output の依存として冒頭に記録済み。
