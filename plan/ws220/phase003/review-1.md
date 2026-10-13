# ws220-p003 review-1（a82c9ba7b の TCP の変更の敵対的 review、2026-10-13、Q1 が起動した review の担当）

読むだけ、build なし。行番号は a82c9ba7b の src/kern/net/tcp.c。

**判定: このままでは main に merge しない。** 重大 1 件を直してから。中 2 件も同じ機会に直すのが望ましい。

lock を外して device に渡す部分に、use-after-free・send_flushing の立ちっぱなし・取りこぼし・二重の持ち主は見つからなかった。辿った筋道:
- 持ち主が lock を外している間の close・reset・RST・timer の再送・ACK の受理、reset の直後の再 connect。
- 確保の場所の使い回し（ABA）: tcp_unmark_sent_locked と取り戻しの判定はどちらも pointer を比べるだけで、指す先は読まない。
- 不変条件 3 つ（send_unsent ≤ send_count、retransmit_deadline ≠ 0 ⇔ 送った segment がある、output_retry_* は待ちがある時だけ 0 でない）は全部の経路で保たれる。

## 致命的

なし。

## 重大

**G1. 隣の address が解決しない（ARP）と、未送信の SYN・SYN-ACK・data が永久に残り、その間 10 ms ごとに ARP の broadcast が出続ける。**

- 根拠: ipv4.c:313-327（待たない出力は解決できないと毎回 arp_request を出して EAGAIN、負の cache は無く EHOSTUNREACH にならない）、arp.c:217（arp_request に回数の制限なし）、tcp.c:2258（EAGAIN は WAIT、10 ms の retry）、tcp.c:2174・2361-2387（拒まれた segment は未送信に戻り RTO の期限が消える）、tcp.c:600-602・641（送った segment が 0 だと R2 の判定 tcp_retransmit_given_up_locked まで行かない）、tcp.c:608（keepalive は send_count != 0 の間は動かない）。
- 筋書き:
  - 同じ subnet の止まった host への connect で SYN（3109）が未送信のまま。TCP_GIVE_UP_SYN_MS（180 s）が効かず、SO_SNDTIMEO が無ければ塞がる connect は永久に戻らない。1 秒に 100 回 ARP。旧 code は control の segment の EAGAIN を「送った」とみなし、RTO・backoff・R2 で ETIMEDOUT に終わっていた。
  - listener の子の SYN-ACK（4090）も同じ。half-open の子は R2 でしか刈られないので、on-link の偽の送り元から SYN を 16 個送るだけで backlog（TCP_LISTEN_BACKLOG_MAX 16）が永久に塞がる（DoS）。ARP は 1,600 回/s。
  - 確立した接続でも、全部が ACK された後で相手の ARP が消えると、新しい write は未送信のまま。R2 も keepalive も効かず永久に止まる。
  - 送った segment の再送が EAGAIN で拒まれると backoff を戻すので 10 ms ごと（716-728）。R2 の 100 s の間に約 1 万回 ARP。
- 案: (a) EAGAIN は「線の上で失った」と同じに扱い、送った印のまま RTO・backoff・R2 に任せる（旧 code の control の segment と同じ）。(b) 未送信の最古が最初に拒まれた時刻を R2 の起点にして output retry の側でも諦める。(c) WAIT の retry の間隔を指数で伸ばす。最小でも SYN・SYN-ACK は (a)。
- **Q1 の決め: (a) を SYN・SYN-ACK・data・timer の再送の全部に。WAIT は pool の不足（ENOMEM）だけ。**

## 中

**M1. まとめて出し直す pure ACK が偽の重複の ACK になる。** tcp_send_ack（2733、増やすのは 2764）は receive_next が同じなら種類を問わず最大 3 まで数える（窓の更新 3460、challenge ACK、acks_outside への答え、古い data への ACK も）。出し直し（2641〜）はその時の同じ窓で同じ ACK を 3 つ続ける。受けの向きの転送で device が満杯の間に窓の更新が 3 回拒まれると、後で同じ ack・同じ窓の ACK が 3 つ出て、相手は重複の ACK 3 つと数え、偽の fast retransmit で cwnd を半分にする。案: 数を持つのは順序外の segment への ACK だけ、他は 1 にまとめる。

**M2. timer の再送が device に拒まれた時は 1 ms ごとに試し直し、伸ばさない。** tcp.c:724-728 は常に tcp_refusal_delay(refusal)。output retry の側の「進まなければ 10 ms」が無い。完了を知らせない・詰まった device では R2（100 s）の間 socket ごとに worker が 1 kHz で起きる。案: 一度拒まれた後は WAIT の間隔か progressed と同じ扱い。

## 軽微

- L1. tcp_output_refused_locked（2401）は socket に 1 つの on_pass・期限を上書きする。device に拒まれた未送信（on_pass 1、1 ms）の後で ACK が pool に拒まれると on_pass 0・10 ms になり、未送信が device の完了で出られず 10 ms 遅れる。書き手の「期限 now」も同時の ACK の拒否で遅れる。性能の問題。案: 早い期限を残し on_pass は OR。
- L2. 窓が開いた時のすぐの再送（4607）と持ち主の拒否が重なる: 持ち主が印を付けてまだ device に渡していない X を timer が同時に再送し、timer の送りが通って持ち主の送りが拒まれると、X は線に出たのに未送信に戻る。相手の ACK は「送った端を越える」で捨てられ（窓の情報も失う）、X は retry で再送されて回復。retransmit_segments も 1 つ余計。自然に回復するので軽微。
- L3. 持ち主が lock を外している間の pure ACK は、印を付けただけの segment の後ろを sequence に使う（tcp_sent_end_locked）。device が拒むと窓 0 の相手はこの ACK を捨てる。一時のことで軽微。
- L4. reset の後に tcp_send_ack_pending や窓の更新の拒否が戻ると ack_pending が復活する。閉じた接続に ACK が 1 つ出るか hard error で捨てられる。害は小さい。
- L5. coding-style: tcp_refusal_of の最後の return の注記「Succeeded: any other error…」は成功ではない（2261 付近）。TCP_REFUSAL_NONE が「通った」と「固い error」を兼ねる（TCP_REFUSAL_HARD を分ける案）。

## 確かめて問題が無かった物

- ACK の受理は送った端（4523〜）。FIN_WAIT_1・LAST_ACK の「全部 ACK された」は積んだ端（4778）で、ACK は送った端を越えないので正しい。
- keepalive は send_count == 0 の時だけ動き、send_next - 1 は送った端と同じ。
- SYN_SENT・SYN_RECEIVED が send_next で ACK を照らすのは ring に SYN しか無いので問題ない。
- net.tcpstats の send_next・in_flight_bytes は送った端から。
- 32 bit の wrap は tcp_sequence_before と引き算。数の上限、count - sent の引き算にも誤りなし。
- 書き手の待ち: retire・窓の再開・socket_set_error のどれも send_waitq を起こす。tcp_sendto の EAGAIN の continue と tcp_send_reliable は同じ条件で入場を判定するので空回りしない。
- connect の SYN の固い error: ring の最後で未送信 0 なので取り戻され、error が connect に返る。
- 計画の側（S5-1）: p006(b) を後にしたのは phase.md:20 に Q1 の指示として記録済みで、merge を止める理由ではない。
