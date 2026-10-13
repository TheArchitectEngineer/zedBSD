# ws220-p004 review-1（phase.md a52a81575 の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

照らした code は main 282f46038 の tcp.c・tcp-socket.h・sysctl.h（行番号はこの tcp.c）。未 commit の source・build・QEMU なし。Linux・FreeBSD の挙動は記憶による推測。

**判定: 致命的 0・重大 2・中 5・軽微 6。実装は進めてよいが、重大 2 件を設計に入れ直すまで merge しない。** 式・計る segment の選び方・p003 の分類との組み合わせ・lock・UAPI の大きさには大きな誤りなし。誤りは backoff を戻す規則（設計の「今のまま、FreeBSD・Linux と同じ」は事実と違う）。

## 重大

**G1. backoff を新しい data の ACK で戻すと Karn の規則に反し、RTT が RTO を超えて伸びると sample が永久に取れない（§1「backoff を戻す時」、ゴール 5）。**
根拠: tcp.c:1992-2007（進む ACK で retransmit_count = 0、基本の RTO で張り直す）、2400-2409（ring が空の後の新しい印でも 0）、704-712（timer の再送で count++）、2001-2003・4705-4708（回復・窓が開いた時の即時の再送も timer の道で count++）。RFC 6298 §5 の注記: 倍にした RTO が縮むのは新しい RTT の計りが取れた時だけ（Karn の後半）。推測: Linux の tcp_ack_update_rtt「RFC6298: only reset backoff on valid RTT measurement」、FreeBSD の t_rxtshift = 0 は tcp_xmit_timer の中。
筋書き: RTT が SRTT + 200 ms を超えて伸びる（Wi-Fi・VPN・WAN・queue の膨張・経路の変化）と、ring 8 segment の ACK の空白が RTO を超えて毎往復 timeout、再送で計りが捨てられ、次の計りも次の timeout で捨てられる。backoff は進む ACK ごとに 0 に戻り、RTO は約 200 ms のまま毎往復に偽の再送。p006 の後はそのたびに cwnd = 1。backoff を保てば 400・800 ms と伸びて sample が取れて回復するはず。
案: 再送の数（retransmit_count: R2・still_oldest・recovering の帳簿）と RTO の backoff の指数（新しい欄）を分ける。指数は有効な sample の時だけ 0 に戻し、本当に期限が切れた timeout だけで増やす（窓が開いた時・回復した時の即時の再送は計りを捨てるが指数は進めない。進めると slow-reader で 60 s まで積もる）。net.tcpstats の backoff（sysctl.h:361-362）をどちらにするか決めて注記を直す。模型に「途中で片道の遅延を 0.1 ms → 300 ms」を足し、偽の再送が収まって srtt_us が追うことを確かめる。

**G2. 進まない ACK（重複・窓 0）でも backoff を 0 に戻している。設計の「今のまま（新しい data の ACK で）」は code と違い、p004 で窓 0 の相手への再送が約 5 倍になる。**
根拠: tcp.c:1970-1981（!progressed で retransmit_count・since・refused を全部 0。R2 の時刻を戻すのは p002 の決めで正しいが backoff まで戻す）、2136-2140（窓 0 でも 1 segment 出す）、persist timer は p005 まで無い、RFC 9293 3.8.6.1（窓 0 の probe は指数で伸ばす SHOULD）。
筋書き: 窓 0 の相手に timer が RTO ごとに再送し、窓 0 の ACK で backoff が 0 に戻り間隔が伸びず、R2 も戻るので終わりも無い。今 1 s ごとが p004 後は約 200 ms ごとに 1 KB を無期限。偽の再送への重複の ACK でも戻り G1 の直しを打ち消す。p005 の受け入れの基線も変わる。
案: 進まない ACK では retransmit_since（R2）だけ戻し指数は戻さない。§1 の「今のまま」を code に合わせる。slow-reader と --peer-window の scenario で retransmit_segments が p003 から増えないことを受け入れに。

## 中

**M1. 固い error で segment を取り戻した時に計りが残り、後の sample が巨大になる。** tcp.c:2216-2241（書き手の segment が固い error で ring の最後なら取り戻し send_next = sequence）は設計の「捨てる」一覧に無い。USB の adapter を抜く・route が消える → write が error、計りは古い rtt_start のまま → 同じ sequence の後の write は「既に計っている」で計り直さない → R が何十秒の sample（R = 30 s で RTTVAR 7.5 s・RTO 約 34 s）。uint32 の μs なら 4295 s で回り込む。案: 取り戻しでも計りを捨てる。不変条件 1 を「計っている segment は ring にあり送った印がある」に強め外れる道を数え上げる。R を 60 s で頭打ち、計算は 64 bit。

**M2. 「最初の sample」の見分けが無い。** rto = 0 を sample 前とし 5.7 で rto = 3 s を置く。LAN では R = 0 が普通。rto ≠ 0 を「sample あり」とすると 3 s の接続の最初の sample が 2 回目の式で計算され SRTT は R/8・RTTVAR は R/4（RFC 6298 2.2 違反）。srtt == 0 を「sample 無し」とすると R = 0 の後が毎回「最初」。net.tcpstats の srtt_us = 0 も区別できない（M5）。案: 「sample を持つ」の印か sample の数の欄を別に持ち、5.7 の 3 s と独立に。

**M3. 5.7 の判定に retransmit_count ≠ 0 を使うのは脆く、SYN_RECEIVED の重複 SYN への SYN-ACK の出し直しが Karn から漏れる。** tcp.c:4517-4528・4549-4556（確立で tcp_retransmit_reset が count を 0）、739-752（拒まれた timer の再送は count-- で SYN の再送を pool が拒むと 0 に戻るが timer は切れている）、4500-4511・4534-4536（重複 SYN に timer を通らず tcp_send_segment_at で SYN-ACK を出し直し、count も計りもそのまま）。SYN-ACK が失われて相手が SYN を出し直すと、出し直しへの ACK が元の計りで sample（Karn 違反、長めに出る）、5.7 も効かない。pool に拒まれた SYN の再送も 3 s にならない。案: 「SYN（SYN-ACK）の timer が切れた／出し直した」の印を独立に（SYN_SENT・SYN_RECEIVED の timeout と重複 SYN への出し直しで立てる）。確立の処理で tcp_retransmit_reset の前に sample と 5.7 の判定（順を設計に書く）。重複 SYN への出し直しでも計りを捨てる。

**M4. 受け入れの基準が phase.md に無く、p001 の基準は統計的に脆い。** p001 の「lan-loss-1 の最長の停止 < 0.5 s」: run.sh:74 は 8 MiB・損失 1 %（約 82 回）、同じ segment の再送も失う確率は 1 回あたり 1 %、82 回のどこかで約 56 %、起きると RTO + 2·RTO ≈ 0.6 s 超で正しい実装でも seed しだいで FAIL。模型の相手は遅延 ACK 無し・HZ 1000 だけで S6（遅延 ACK の相手で偽の timeout）と 10 ms の tick を検出できない。analyze.py:43 の STATE に srtt_us・rttvar_us が無い。案（§3・§5 に）: 基準を「二重の損失を除いた停止 ≤ RTO + 1 往復」か 99 パーセンタイルにするか、seed を固定して二重の損失の数も出す。scenario: 相手の遅延 ACK（40・200 ms）、途中の RTT の段差（G1）、SYN の喪失 → 3 s → 最初の sample で SRTT = R（M2・M3）、固い error の取り戻しの後の write（M1）、窓 0 の相手の再送の間隔（G2）、HZ = 100 の build。analyze.py に srtt_us・rttvar_us。T1 の依頼に usb-net の上りで損失 0・0.1・1 % の rto_ms・srtt_us の時系列（rto_ms がおよそ 200〜230 ms）と retransmit_segments が p003 から増えないこと。

**M5. UAPI の「bit が立つ欄の 0」の意味が既存の注記と食い違う。** sysctl.h:306-309「a field whose bit is clear is 0 and means not kept」に対し、設計は sample 前も HAVE_RTT を立て 0 = sample 前。R = 0 で SRTT = 0 にもなる。tcp.c:1444 は features = 0 から作る。道具（sysctl の表示 main.c:805-813、analyze.py）が区別できない。案: 大きさと版は変えない。entry の reserved（sysctl.h:402）を「RTT の sample の数」にするか sample の後だけ立つ bit を足す。注記に「HAVE_RTT が立ち sample の数が 0 なら srtt_us・rttvar_us は意味を持たない」。WS221 p002 も同じ sysctl.h（header 48 → 72 byte）を変えるので merge の順を Q1 に。

## 軽微

- L1. p004 だけの段では RTO が約 200 ms になる分、偽の timeout の後の連鎖（tcp.c:1992-2003）と窓が開いた時の再送（4700-4709）の重複の送りが増える。p005 で外す前提。p004 の確かめで immediate_resends・retransmit_segments の増えを記録し §6 に。
- L2. 遅延 ACK 200 ms の相手（Windows・macOS、記憶）には LAN で RTO ≈ 200 ms と ACK が同時刻で偽の timeout がありうる（review-1 S6）。F-RTO は p006 の前に決める。危険を §6 に。
- L3. ws.md は「単純な 200 ms の床でなく式に最小の項を入れる形にし、ユーザーに訂正を報告する」（Q1 の記録）だが報告と了承の記録が無い。phase.md は式の形を「ユーザーの決定」と書く。報告の記録を ws.md に。
- L4. 式の G の項は 200 ms がいつも勝つので効かない（G ≤ 10 ms）。注記にそう書く。RTO の単位、rtt_start の型（uint64）、|SRTT − R| の符号を設計に。32 bit でも socket の lock の下だけなのでちぎれない。
- L5. retire の中で sample → RTO の計算 → 期限の張り直し（tcp.c:2004-2007、TCP_INITIAL_RTO を置き換え）の順を書く。逆だと古い RTO で張る。
- L6. 依存: p004 は p003 の出力（tcp_mark_sent_locked・tcp_unmark_sent_locked と refusal の分類、main にある）に scoped-output で依存、p003 は T1 待ち。p003 の QEMU の結果で分類が変わったら §1 を見直す、と書く。

## 問題が無かった点

RTTVAR を古い SRTT で先に更新（2.3）、最大 60 s（2.5）・sample 前 1 s（2.1、2.4 の SHOULD からの外れは記録済み）、計り始めを印の時刻に（p003 の L2 の競合でも timer の再送が計りを捨てる）、未送信に戻った segment の計りを捨て印を付け直して計り直す（send_flushing で 1 socket に 1 つ）、LOST の segment は timer の再送で捨てられるまで残るが ACK が来ないので誤った sample は出ない、sched_ticks() は uint64、sequence は剰余の比較、新しい lock 無し（timer 612・704-712、retire、印、net.tcpstats 1466-1476 は socket の lock の下）、R2 は時間で数える（p002）、HAL の変更不要、UAPI の欄（srtt_us・rttvar_us・rto_ms、sysctl.h:419-421）と HAVE_RTT（0x02）は既にあり大きさと版は不変。
