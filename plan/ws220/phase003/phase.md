<!-- awesome-plan project=zedbsd record=ws220-p003 -->
# ws220-p003: 送りの packet の不足（device が拒んでも書き手が 10 ms 眠らない、未送信を順に出す、ACK の出し直し）

Status: in-progress（2026-10-13 P3: 設計（下の §1）を決めて実装中）
Parent: [WS220](../ws.md)
設計の出所: [p001 §4 の p003 の行](../phase001/phase.md)、[review-1](../phase001/review-1.md) の S4（1〜6）・S5、[p002](../phase002/phase.md) の結果（main に入った c13a8413f 以後の tcp.c）、ws.md の Q1 の記録とユーザーの決定（症状は受けの向き）

## ゴール

1. data の segment を device か packet の pool が拒んだ時、書き手は 10 ms 眠らない: segment は送りの ring に「未送信」として残り、device の送りが空いた時（driver の完了 → network worker の一巡）か、retry の期限に出る。書き手の write は成功を返す（ring が data を持つ）。
2. 未送信は順に出す: 拒まれた segment の後ろの segment は、その segment より先に出ない（review S4-1）。
3. 未送信の送り出しは RTO の経路と別にする: backoff・再送の数・R2 の時刻を動かさない。RTO の timer は実際に device に渡った最古の segment から始まる（S4-2）。
4. `tcp_timer_next_deadline` は「未送信がある」「ACK が要る」の retry の期限も見る（S4-3）。
5. pool の不足の間は 1 ms ごとに空回りしない（10 ms の期限だけで試す、S4-4）。
6. 取り戻せなかった segment に error を返さない（複数の書き手で同じ byte が 2 回入る、S4-5）。
7. timer の再送が device・pool に拒まれた時は 2 s 以上待たない（S4-6）。
8. pure ACK（窓の更新・重複の ACK）が拒まれたら数を持って出し直す（p001 §4 p003(b)、S4 の「数も持つ」）。

範囲の外: cwnd・byte の送りの buffer（p006）、RTT・RTO（p004）、fast retransmit・「窓が開いた時の再送」の除去（p005）、受けの窓（p007）。
review S5 の「p006(b) の byte の buffer を先に」は採らない（Q1 の指示で p003 を今の ring の上で行う）。下の設計は ring の上の 3 つの位置（una・送った端・積んだ端）で、p006(b) で byte の buffer に替える時もそのまま SND.UNA・SND.NXT・積んだ端に写せる。

## 1. 決めたこと（S4 の直し方）

| 項目 | 決め | 理由 |
| --- | --- | --- |
| 3 つの位置 | `send_unacknowledged`（SND.UNA）、**送った端**（新しい `tcp_sent_end_locked()` = 最初の未送信の segment の sequence、未送信が無ければ `send_next`）、`send_next`（積んだ端 = ring の最後の segment の後ろ、書き手が次に取る sequence）。ring の末尾の `send_unsent` 個が未送信。未送信は常に末尾に連なる（順に出すので） | S4「送った位置（snd_max）と送る位置を分ける」。RFC 9293 の SND.NXT（次に新しく送る sequence）は「送った端」に当たる。`send_next` の名は既存の多くの所で「書き手が次に取る sequence」として使われているので変えない（BSD の snd_max ≥ snd_nxt とは向きが逆になる点を tcp-socket.h の注記に書く） |
| 各所の位置 | pure ACK の sequence・ACK の受理（`SEG.ACK > 送った端` は範囲外）・net.tcpstats の `send_next`・`in_flight_bytes` = 送った端。窓と ring の入場の判定（`tcp_in_flight`）・FIN_WAIT_1・LAST_ACK の「全部 ACK された」= 積んだ端 | 相手が受けていない sequence を ACK に載せない（窓 0 の相手は RCV.NXT 以外の sequence の segment を捨てる）。入場は未送信も約束した量に数える |
| 順序 | 送り出しは 1 つの socket に 1 人だけ（`send_flushing`）。持ち主は socket の lock の下で「最初の未送信を送った印にする」→ lock を外して device へ → lock の下で結果、を未送信が尽きるか拒まれるまで繰り返す。持ち主がいる間に積んだ書き手は未送信を残して戻り、持ち主がそれを拾う（持ち主は「未送信 0 を見る」と「持ち主をやめる」を同じ lock の中で行うので取りこぼしは無い） | S4-1。2 つの書き手が lock の外で別々に device へ渡すと、後ろの segment が先に出る（今の code にもある並び替え） |
| 書き手の道 | 積んだ segment が唯一の未送信で持ち主がいなければ、書き手がそのまま持ち主になり、自分の `data`（呼び手の kernel の buffer）から送る（今と同じ 1 回の写し）。前に未送信があるか持ち主がいれば未送信を残して成功を返す（持ち主か retry が出す）。書き手の持ち主が送った後にまだ未送信があれば（他の書き手が後ろに積んだ）、retry を「次の一巡」にして worker を起こす | 古い未送信の payload の写しには 1 KB の stack の buffer が要る（ring の packet は reset で lock の外で解放されうる）。書き手の stack（16 KB）に 1 KB を足さず、worker の `tcp_timer_run` が既に持つ再送の buffer を使う |
| 拒まれ方の分類 | `tcp_output_segment` が自分の packet の確保に失敗した = **pool**（`ENOMEM` を返すように変えた、今までは `ENOBUFS`）。`EAGAIN` = 隣の address が未解決（ARP・ND）。この 2 つは **待つ**: retry は 10 ms（`TCP_SEND_RETRY_MS`）の期限だけ。`ENOBUFS`・`EBUSY` = **device** の queue が満杯（ECM・NCM・GENET）: retry は worker の次の一巡（driver の送りの完了は `net_device_schedule_poll` で worker を回す、usb-cdc-ecm.c:823・usb-cdc-ncm-net.c:887）と、完了を知らせない driver のための 1 ms（最低 1 tick）の期限。他の error は**固い**（route が無い等） | S4-4（pool の間は 1 ms ごとに空回りしない）。ECM の queue の満杯は pool と同じ `ENOBUFS` なので、pool は TCP の側で区別する。ARP の未解決で 1 ms ごとに試すと `arp_request` が 1 ms ごとに出るので待つ側に入れた |
| 固い error | 送った印のまま（線の上で失ったのと同じ）にし、RTO の再送が決める（今の timer は再送の固い error で接続を閉じる）。ただし書き手の道で自分の segment が固い error で、まだ ring の最後なら、今と同じく取り戻して error を返す（connect の SYN の `ENETUNREACH`、write の error が今と同じに見える）。最後でなければ（後ろに他の書き手が積んだ）取り戻さず 0 を返す | S4-5: 取り戻せない segment に error を返すと、書き手が同じ data をもう一度送り stream に 2 回入る |
| RTO の timer | 最古の segment が**送った印**になった時に張る（`retransmit_deadline = now + RTO`、count・since を 0）。最古が未送信の間は RTO は無い（timer は送った segment の数 `send_count − send_unsent` が 0 なら再送しない）。retire の後に新しい最古が未送信なら deadline 0。窓が開いた時の直ちの再送・「回復した時に次を直ちに再送」も送った segment がある時だけ | S4-2: 一度も出ていない segment を「再送」と数えない（backoff・Karn・R2 を誤って進めない）。p004 の RTT の計り始めは「送った印」の時刻にできる |
| timer の再送が拒まれた時 | 再送の数（`retransmit_count`）を 1 つ戻し、期限を device なら 1 tick 後・待つなら 10 ms 後にする。net.tcpstats の `retransmit_segments` は device が受けた時だけ数える | S4-6（今は次の期限 = 2 s 以上を待つ） |
| pure ACK | 全ての pure ACK を `tcp_send_ack()` に通す（送った端を sequence に）。device・pool に拒まれたら `ack_pending` に数え（同じ `receive_next` の間は最大 3、`receive_next` が進んだら 1 から）、retry で出す。出す時、拒まれた後に `receive_next` が進んでいれば 1 つだけ（新しい ACK が前の全部を含む）、進んでいなければ数だけ（重複の ACK は相手の fast retransmit の 3 つに数えられる） | p001 §4 p003(b)、S4「数も持つ」。最大 3 で、進んだ ACK の後に余計な重複の ACK が相手の閾値 3 に届かない |
| retry の状態 | socket ごとに 1 つ: `output_retry_on_pass`（device の拒み: worker の次の一巡で試す）と `output_retry_deadline`。未送信も ACK の待ちも無くなったら両方 0。`tcp_timer_run` は socket ごとに、未送信か ACK の待ちがあり、持ち主がいなくて、一巡の印か期限が来ていれば先に送り出す（RTO の判断の前） | S4-3・S4-4 |
| worker を起こすか | 書き手・読み手（窓の更新）・input の道で拒まれて retry を置いた時は `net_worker_wakeup()`（眠っている worker が新しい期限を知るため）。`tcp_timer_run` の中の retry の失敗では起こさない（起こすと device が満杯の間 worker が自分を回し続ける） | 空回りの防止 |
| 書き手の待ち | `tcp_sendto` は ring か窓に空きが無い（`EAGAIN`）と、10 ms 眠らずに ring の空きを `send_waitq` で待つ（他の書き手が先に空きを取った時も）。10 ms の眠りは ring の写しの packet が pool から取れない時（`ENOBUFS`）だけ（`send_buffer_sleeps`） | ゴール 1。pool の不足は本当の資源の不足で、塞がる書き手が空回りしないための今の待ちのまま |
| 数の意味（UAPI の注記だけ、欄と大きさは変えない） | `send_refused` = device か pool に拒まれて未送信に残った data の segment の数（今までは「取り戻した数」）。`send_buffer_sleeps` = 写しの packet が無くて書き手が 10 ms 眠った数。`retransmit_segments` = device が受けた再送 | `include/uapi/sysctl.h` の注記 |

不変条件（実装の確かめに使う）:
- `send_unsent ≤ send_count`。未送信は ring の末尾の `send_unsent` 個で、sequence の順に連なる。
- 送った端は una と積んだ端の間。ACK は送った端を越えない（越える ACK は範囲外として答えて捨てる）。
- `retransmit_deadline ≠ 0` ⇔ 送った segment がある（`send_count − send_unsent > 0`）。
- 持ち主（`send_flushing`）は lock の外で device に渡す間だけ立ち、その間に送った端を動かせるのは持ち主だけ。
- `output_retry_on_pass`・`output_retry_deadline` は未送信か ACK の待ちがある時だけ 0 でない。

## 2. 変えた file

（実装の後に書く）

## 3. 模型の新しい scenario

（実装の後に書く）

## 4. 実行した command と結果

（実装の後に書く）

## 5. T1 への依頼（Q1 が基線 T1-574 の後に頼む）

（実装の後に書く）

## 6. 残りと判断

（実装の後に書く）
