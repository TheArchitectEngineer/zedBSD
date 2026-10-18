<!-- awesome-plan project=zedbsd record=ws220-p003 -->
# ws220-p003: 送りの packet の不足（device が拒んでも書き手が 10 ms 眠らない、未送信を順に出す、ACK の出し直し）

Status: cleared（2026-10-18 Q1: ws.md の表に記録済みの判定に合わせた。表: cleared（2026-10-13 Q1 判定: 同じ session の基線と比べる基準（T1-581）で k2＝p003＋p004 が k1＝p002 より全部の値で良い、損失のある時は T1-577 で大きく改善。T1-577 の T1-574 との差は環境））（旧: in-progress / test-wait（2026-10-13 P3: 実装・模型・build（amd64・rpi4、warning 0）は済み。review-1（重大 G1・中 M1・M2・軽微）を同じ attempt の中で直した（§7）。QEMU は §5 の T1 の依頼待ち（番号は Q1 が付ける）。cleared は T1 の結果を Q1 が判定してから））
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
| 拒まれ方の分類 | `tcp_output_segment` が自分の packet の確保に失敗した = **pool**（`ENOMEM` を返すように変えた、今までは `ENOBUFS`）。`EAGAIN` = 隣の address が未解決（ARP・ND）。この 2 つは **待つ**: retry は 10 ms（`TCP_SEND_RETRY_MS`）の期限だけ。`ENOBUFS`・`EBUSY` = **device** の queue が満杯（ECM・NCM・GENET）: retry は worker の次の一巡（driver の送りの完了は `net_device_schedule_poll` で worker を回す、usb-cdc-ecm.c:823・usb-cdc-ncm-net.c:887）と、完了を知らせない driver のための 1 ms（最低 1 tick）の期限。他の error は**固い**（route が無い等） | S4-4（pool の間は 1 ms ごとに空回りしない）。ECM の queue の満杯は pool と同じ `ENOBUFS` なので、pool は TCP の側で区別する。ARP の未解決で 1 ms ごとに試すと `arp_request` が 1 ms ごとに出るので待つ側に入れた | **review-1 G1 で改訂**: `EAGAIN`（隣の未解決）は待たず「線の上で失った」として送った印のまま RTO・backoff・R2 に任せる。待つのは pool（`ENOMEM`）だけ（§7）。
| 固い error | 送った印のまま（線の上で失ったのと同じ）にし、RTO の再送が決める（今の timer は再送の固い error で接続を閉じる）。ただし書き手の道で自分の segment が固い error で、まだ ring の最後なら、今と同じく取り戻して error を返す（connect の SYN の `ENETUNREACH`、write の error が今と同じに見える）。最後でなければ（後ろに他の書き手が積んだ）取り戻さず 0 を返す | S4-5: 取り戻せない segment に error を返すと、書き手が同じ data をもう一度送り stream に 2 回入る |
| RTO の timer | 最古の segment が**送った印**になった時に張る（`retransmit_deadline = now + RTO`、count・since を 0）。最古が未送信の間は RTO は無い（timer は送った segment の数 `send_count − send_unsent` が 0 なら再送しない）。retire の後に新しい最古が未送信なら deadline 0。窓が開いた時の直ちの再送・「回復した時に次を直ちに再送」も送った segment がある時だけ | S4-2: 一度も出ていない segment を「再送」と数えない（backoff・Karn・R2 を誤って進めない）。p004 の RTT の計り始めは「送った印」の時刻にできる |
| timer の再送が拒まれた時 | 再送の数（`retransmit_count`）を 1 つ戻し、期限を device なら 1 tick 後・待つなら 10 ms 後にする。net.tcpstats の `retransmit_segments` は device が受けた時だけ数える | S4-6（今は次の期限 = 2 s 以上を待つ） | **review-1 M2 で改訂**: 続けて拒まれたら 10 ms 後（新しい欄 `retransmit_refused`）。隣の未解決は拒みでなく失った再送として backoff を進める（§7）。
| pure ACK | 全ての pure ACK を `tcp_send_ack()` に通す（送った端を sequence に）。device・pool に拒まれたら `ack_pending` に数え（同じ `receive_next` の間は最大 3、`receive_next` が進んだら 1 から）、retry で出す。出す時、拒まれた後に `receive_next` が進んでいれば 1 つだけ（新しい ACK が前の全部を含む）、進んでいなければ数だけ（重複の ACK は相手の fast retransmit の 3 つに数えられる） | p001 §4 p003(b)、S4「数も持つ」。最大 3 で、進んだ ACK の後に余計な重複の ACK が相手の閾値 3 に届かない | **review-1 M1 で改訂**: 数を持つのは順序外の segment への ACK（本当の重複）だけ、他は 1 つにまとめる。閉じた接続には持たない（L4、§7）。
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

- `src/kern/net/tcp.c`:
  - 新しい関数: `tcp_refusal_of`（error → 送り直しの分類）、`tcp_refusal_delay`、`tcp_sent_count_locked`・`tcp_sent_end_locked`（送った数と送った端）、`tcp_mark_sent_locked`・`tcp_unmark_sent_locked`（未送信の先頭を送った印にする・拒まれたら戻す）、`tcp_output_refused_locked`・`tcp_output_retry_settle_locked`・`tcp_output_retry_due_locked`（retry の状態）、`tcp_output_retry`（worker の各 socket の一巡の先頭）、`tcp_send_unsent`（持ち主が未送信を順に送る）、`tcp_send_ack_pending`・`tcp_send_ack`（pure ACK）。`tcp_send_segment` は使う所が無くなったので外した。
  - `tcp_send_reliable`: 積んだ segment は未送信として入り、唯一の未送信で持ち主がいなければ書き手が持ち主になって送る。拒まれたら未送信に戻して 0、固い error は ring の最後の時だけ取り戻して error。持ち主の間に他の書き手が積んだら retry を「次の一巡、期限は今」にする。最後に必ず `net_worker_wakeup()`。
  - `tcp_sendto`: `EAGAIN`（他の書き手が先に空きを取った）は ring・窓の空きを `send_waitq` で待ち直す。10 ms の眠り（`send_buffer_sleeps`）は写しの packet が無い `ENOBUFS` だけ。
  - `tcp_timer_run`: 各 socket で先に `tcp_output_retry`。RTO は送った segment がある時だけ。再送が拒まれたら `retransmit_count` を戻し期限を 1 tick（device）か 10 ms（pool・隣）後に、`retransmit_segments` は device が受けた時だけ数える（前の世代の書きかけを確かめて残した）。
  - `tcp_timer_next_deadline`: retry の期限も見る。ただし持ち主（`send_flushing`）がいる間は見ない（過ぎた期限で worker が眠らずに回り続けないため。持ち主は終わりに worker を起こす）。
  - ACK の受理: `SEG.ACK > 送った端` を範囲外にする。`tcp_retire_acknowledged`・重複の ACK の数え・窓が開いた時の再送・timer の期限は送った segment の数で判断する。`tcp_retransmit_reset` は未送信・ACK の待ち・retry も消す。
  - pure ACK（重複の ACK、窓の更新、範囲外への答え、RST・SYN の challenge ACK、handshake の ACK、FIN の ACK）は全部 `tcp_send_ack()`。
  - `tcp_output_segment`: 自分の packet が pool から取れない時は `ENOMEM`（device の満杯の `ENOBUFS` と分ける）、packet に収まらない時は `EMSGSIZE`（固い error）。
  - net.tcpstats: `send_next` と `in_flight_bytes` は送った端から、`retransmit_in_ms` は送った segment がある時だけ。
- `include/kern/net/tcp-socket.h`: 欄 `send_unsent`・`send_flushing`・`ack_pending`・`ack_pending_receive_next`・`output_retry_on_pass`・`output_retry_deadline` と ring の注記（前の世代の 398bbdebb のまま）。
- `include/uapi/sysctl.h`: 注記だけ（`send_next` = 送った端、`send_segments` は未送信を含む、`send_refused`・`send_buffer_sleeps`・`retransmit_segments` の新しい意味）。欄・大きさ・version は変えていない。
- `plan/ws220/tests/tcp-model/model.c`・`run.sh`: 下の §3。
- HAL・driver・toolchain・他の WS は変えていない。

§1 からの補い（実装で決めた 2 点）:

| 項目 | 決め | 理由 |
| --- | --- | --- |
| device の拒みの期限 | 書き手・input の道の最初の拒みは 1 ms（`TCP_OUTPUT_RETRY_DEVICE_MS`、最低 1 tick）。worker の retry が 1 つも送れずにまた拒まれたら 10 ms（`output_retry_on_pass` は立てたまま、device の完了の一巡では直ちに試す） | 完了で worker を回さない device や詰まった device に 1 ms ごとに試し続けない（S4-4 を device にも） |
| 持ち主がいる間の期限 | `tcp_timer_next_deadline` は持ち主がいる socket の retry の期限を数えない。`tcp_output_retry` は先に、待つ物が無い retry の残りを消す | 過ぎた期限が残ると worker が眠らずに回る（1 CPU で書き手が持ち主のまま止められると worker が回り続ける恐れ） |

## 3. 模型の新しい scenario

模型（`plan/ws220/tests/tcp-model/`）の変更:
- device の送りの完了で network worker の一巡を回す（ECM・NCM・GENET の driver が送りの完了で `net_device_schedule_poll` を呼ぶのと同じ）。前は 1 ms の tick だけで回していた。
- `--second-writer`: A の device に data の segment を渡す間（書き手か worker がその答えを待つ間）に、2 つ目の書き手が同じ socket に 1 つ書く（2 つの thread の書き手、review M4・S4-5）。
- `--expect-in-order`: 損失の無い線で B に「穴の前に来た segment」（`out_of_order_in`）があれば `check: FAIL`。
- `--unresolved-at-ms T --unresolved-ms D`: 転送の T から D の間、A の segment を IPv4 が隣の未解決として拒む（`EAGAIN`）。summary の `unresolved_refusals` が拒んだ数。
- summary に `b_out_of_order`・`unresolved_refusals` を足した。`run.sh` は `check: FAIL` の行も FAIL にする。

足した scenario（`run.sh`）:

| scenario | 引数 | 目標 | p003 の前（同じ模型で旧 tcp.c） | p003 |
| --- | --- | --- | --- | --- |
| queue-7 | `--bytes 64Mi --queue 7` | 30 MB/s | 0.68 | 37.28 PASS |
| queue-5 | `--queue 5` | 30 | 0.49 | 37.28 PASS |
| queue-2 | `--queue 2` | 30 | 0.20 | 37.28 PASS |
| queue-1 | `--bytes 32Mi --queue 1` | 30 | 0.10 | 37.26 PASS |
| two-writers | `--bytes 32Mi --queue 3 --second-writer --expect-in-order` | 30、byte の誤り 0、順の乱れ 0 | 0.39、**corrupt 33422555**（同じ byte が 2 回、S4-5）、out_of_order 16384（S4-1） | 37.26 PASS（corrupt 0、out_of_order 0） |
| queue-1-loss-1 | `--bytes 16Mi --queue 1 --loss-ab 1` | 2 | 0.09 | 2.47 PASS |
| unresolved | `--bytes 16Mi --unresolved-at-ms 100 --unresolved-ms 500` | 5、拒み（= ARP の要求）≤ 12 | 17.21（拒み 50、書き手が 10 ms ごとに試す） | 11.17 PASS（review-1 の後: 拒み 8 = ring の segment ごとに 1 回、停止 999.7 ms = RTO 1 回）。a82c9ba7b では 17.03・拒み 51（10 ms ごと、G1 の形） |
| unresolved-giveup（review-1） | `--bytes 1Gi --unresolved-at-ms 500 --unresolved-ms 1000000 --limit-s 400` | R2 で諦める（100.5〜200 s）、拒み ≤ 20 | — | 123.5 s で諦め、拒み 14 PASS（a82c9ba7b なら諦めず 10 ms ごとに拒み続ける） |
| connect-unresolved（review-1） | `--connect-unresolved` | SYN の R2 で諦める（180〜300 s）、拒み ≤ 20 | — | 183.0 s で諦め、拒み 8 PASS |

what-if（`MODEL_CFLAGS=-DCONFIG_TCP_SEND_QUEUE_MAX=44`、device の queue 9）: p001 の 0.88 MB/s → **110.62 MB/s**（線の上限）。p006 で ring を広げる前提（device の queue を溢れても崩れない）が満たされた。

## 4. 実行した command と結果

- `git merge main`（63f5be5d9、衝突なし）。
- 模型の全部: `OUT=build/p3-ws220-host sh plan/ws220/tests/tcp-model/run.sh`（log `build/p3-ws220-model-all2.log`）。

| scenario | p002（前の模型） | p003（新しい模型） |
| --- | --- | --- |
| lan-clean | 37.28 PASS | 37.28 PASS |
| rtt-10ms | 0.78 FAIL（p006） | 0.78 FAIL（p006） |
| lan-loss-0.1 | 1.04 FAIL（p004・p005） | 1.04 FAIL（同） |
| lan-loss-1 | 0.10 FAIL（同） | 0.10 FAIL（同） |
| rtt-10ms-loss-0.1 | 0.39 FAIL（同） | 0.39 FAIL（同） |
| lan-loss-1-zedbsd | 6.31 PASS | 2.47 PASS（下の注） |
| lan-ackloss-1 | 37.18 PASS | 37.18 PASS |
| lan-wrap | 37.24 PASS | 37.24 PASS |
| slow-reader | 1.71 PASS | 1.91 PASS（新しい模型の旧 tcp.c も 1.91） |
| overlap・overlap-wrap・blind-segments | 37.26・37.24・37.26 PASS | 37.26・37.24・37.22 PASS |
| data-giveup・syn-giveup | 123.5 s・183.0 s PASS | 123.5 s・183.0 s PASS |
| 新しい 7 つ（§3） | — | 全部 PASS |

  `tcp-model: FAIL` は p004〜p006 の 4 つ（p001・p002 と同じ）だけ。
  注（lan-loss-1-zedbsd）: 2.47 は模型の変更（device の完了の一巡）で損失の乱数の当たり方が変わったためで、tcp.c の差ではない。確かめ: 新しい模型 + 旧 tcp.c も 2.47（seed 1〜5 で新旧の tcp.c が全部同じ値: 2.47・2.47・32.50・3.56・32.11）、前の模型 + 新しい tcp.c は p002 の表の 11 の scenario が全部 p002 と同じ値（lan-loss-1-zedbsd 6.31 を含む）。損失 1 % の値は seed で 2.4〜32 MB/s に散る（RTO の 1〜2 s の停止の当たり方、p004・p005 の範囲）。
- 旧 tcp.c の比べ: `git show 1e0e3c3be:src/kern/net/tcp.c` を `build/tmp/p3-old.ko63/` に取り出し、新しい header・模型で build（`tcp-model-old`）。前の模型（`git show 1e0e3c3be:plan/ws220/tests/tcp-model/model.c`）と新しい tcp.c も同じ所で build（`tcp-model-oldmodel-newtcp`）。
- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-ws220-amd64 ZEDBSD_USER_PROGRAMS=sysctl build/p3-ws220-amd64/vmunix build/p3-ws220-amd64/bin/sysctl`（`amd64 vmunix check: PASS`）、`make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p3-ws220-rpi4 ZEDBSD_USER_PROGRAMS=sysctl build/p3-ws220-rpi4/vmunix`（`arm64 vmunix check: PASS`）。log の `warning:` は 0（`build/p3-ws220-amd64*.log`・`build/p3-ws220-rpi4*.log`）。
- 模型の build は `-Wall -Wextra -Werror`（host の clang）。
- `python3 plan/tools/style-check.py src/kern/net/tcp.c`: 121（p002 の後）→ 111。新しく増えた指摘は 0（旧 tcp.c の指摘と内容で比べた）。`model.c`: 増えた指摘は option の解き方の 6 つ（`--second-writer`・`--expect-in-order` の `strcmp` を条件に書く、既存の `--quiet`・`--connect-blackhole` と同じ形。p009 で解き方の全体を直す）。`git diff --check` ok。

未実施: QEMU（§5 の依頼）、実機、guest での net.tcpstats の読み。

## 5. T1 への依頼（Q1 が基線 T1-574 の後に頼む）

依頼の文面（Q1 が `plan/agents/T1/requests.md` に積む）:

- 何: ws220-p003（TCP の送りの packet の不足）の QEMU の確かめ。基線 T1-574（p002 の kernel）と同じ測りを p003 の kernel で流し、比べる。
- image A: p003 を merge した main の SHA の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk build/t1-ws220-p003`。image B（wrap）: 同じ tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp-wrap.mk build/t1-ws220-p003-wrap`。過去の build の成果は使わない。
- 流す物（T1-574 と同じ引数、出力先だけ変える）:
  1. guest で root の `sysctl net.tcpstats | head -3`: SSH の接続の行に `features=0x20`。
  2. `plan/ws220/tests/tcp-stream-series.sh IMAGE_A --bytes 1073741824 --out build/ws220-p003-series-0`（損失 0、down・down-disk・up）。
  3. `... --loss 0.1 --bytes 268435456 --out build/ws220-p003-series-01`。
  4. `... --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-p003-series-1`（T1-574 で run を分けたなら同じに分ける）。
  5. image B: `... IMAGE_B --runs up --bytes 67108864 --out build/ws220-p003-series-wrap`。
- 合否:
  - 各 run が `tcp-stream-series: PASS`（全部届いた）、image B の up は `analyze: wire` の行が `crossed_wrap=1`。
  - up（zedBSD が送る、損失 0）: 終わりの net.tcpstats で `send_buffer_sleeps` が T1-574 より少ない（device の満杯では眠らない。0 でなければ pool の不足の数として記録）、mean の MB/s が T1-574 の 90 % 以上。
  - down・down-disk（zedBSD が受ける）: mean の MB/s が T1-574 の 90 % 以上、wire の `longest_stall_ms` が T1-574 の 1.5 倍以下（ACK の出し直しで悪くならない）。
  - 速さの比べは同じ host・同じ損失の T1-574 の値と。損失 0.1・1 % の up の速さは p004・p005 の範囲なので記録だけ（PASS の判定に使わない、完走は要る）。
- 返す物: 各 run の `summary.txt`（`analyze: summary`・`analyze: wire` の行、host の時計の MB/s）、終わりの net.tcpstats の zedBSD の側の行（`send_refused`・`send_buffer_sleeps`・`segment_send_failures`・`retransmit_segments`）、`settings.txt`、失敗なら `transfer-*.txt`・`setup.txt`。pcap は path だけ。
- 見積もり: T1-574 と同じ（ECM で 1 回 7〜10 分 × 損失 3 つ + wrap）。

## 6. 残りと判断

- Status は test-wait: T1 の結果を Q1 が判定するまで cleared にしない。
- 削除してほしい出力（Q1、P3 の worktree）: `build/tmp/p3-cmp.D1j7`（review-1 の後の旧 tcp.c・旧模型・style の比べ）、`build/p3-ws220-host44`（ring 44 の what-if）、`build/p3-ws220-*.log`（build と模型の log、本 file に要点を写した）。`build/tmp/p3-old.ko63` は Q1 が消した。`build/p3-ws220-host`・`build/p3-ws220-amd64`・`build/p3-ws220-rpi4` は次の段で使い回すので残す。
- 模型の option の解き方（`strcmp` を条件に書く形、既存の物を含む）は p009（規約の全文）で直す。
- 模型が見ないこと: 本当の複数の CPU（`send_flushing` の取り合い、lock の外の並び）、pool の不足（模型の pool は共有の 256 で尽きない。`ENOMEM` の道は隣の未解決の `EAGAIN` と同じ待ちの分類で確かめた）、IPv6。
- 範囲の外のまま: cwnd・byte の送りの buffer（p006）、RTT・RTO（p004）、fast retransmit と「窓が開いた時の再送」の除去（p005）、受けの窓（p007、slow-reader の `receive_refused` は残る）。

## 7. review-1 への対応（2026-10-13、a82c9ba7b の review、全文は [review-1.md](review-1.md)）

Q1 の判定「merge は保留、重大 1・中 2 を直してから」。同じ q941 の attempt の中で直した。

| 指摘 | 直し |
| --- | --- |
| G1 隣の未解決（EAGAIN）で未送信の SYN・SYN-ACK・data が永久に残り、10 ms ごとに ARP | Q1 の決め (a) のとおり。`enum tcp_refusal` を NONE（通った）・DEVICE・WAIT（pool の `ENOMEM` だけ）・**LOST**（`EAGAIN`）・**HARD**（固い error）に分けた。DEVICE・WAIT だけが未送信に戻る（`tcp_refusal_retries`）。LOST は送った印のまま RTO・backoff・R2 に任せる: 書き手の道（SYN・SYN-ACK・FIN・data）は 0 を返し、未送信の送り出しは次へ進み（ring の segment ごとに ARP 1 回まで）、timer の再送は backoff を戻さない（固い error として接続を閉じもしない）、pure ACK は失ったものとして持たない。模型: unresolved の期待を「拒み ≤ 12、RTO 1 回の停止」に、`unresolved-giveup`（R2 の 123.5 s で諦め、ARP 14 回）と `connect-unresolved`（SYN の R2 の 183 s、ARP 8 回）を足した |
| M1 出し直す pure ACK が偽の重複の ACK 3 つになる | `tcp_send_ack(endpoint, duplicate)`。数を増やすのは順序外の segment への ACK（`tcp_reorder_keep` の後、duplicate 1）だけ。窓の更新・challenge ACK・範囲外への答え・古い data への ACK・handshake・FIN の ACK は duplicate 0 で、同じ receive_next の待ちがあれば数を増やさない（無ければ 1） |
| M2 timer の再送が device に拒まれると 1 ms ごとに試し続ける | `tcp_socket` に `retransmit_refused`。最古の再送が拒まれたら立て、続けて拒まれたら期限を TCP_SEND_RETRY_MS（10 ms）後にする。再送が通るか失ったら、また timer が始め直したら（retire・reset・印・取り戻し）0 |
| L1 retry の on_pass・期限の上書き | `tcp_output_refused_locked` は on_pass を OR（立てるだけ）、期限は早い方を残す |
| L4 reset の後に ack_pending が復活する | `tcp_send_ack` と `tcp_send_ack_pending` は、lock を取り直した時に接続が CLOSED なら数を持たない |
| L5 `tcp_refusal_of` の最後の注記、NONE が成功と固い error を兼ねる | HARD を分け、注記を直した |
| L2・L3 | 記録だけ（自然に回復する、review のとおり）。L2: 持ち主の送りと timer の再送が重なり持ち主が拒まれると、線に出た X が未送信に戻り、相手の ACK は送った端を越えて捨てられ、X は出し直されて回復する。L3: 持ち主が lock を外している間の pure ACK は印だけの segment の後ろを sequence に使う |

確かめ（直しの後）:
- 模型の全部（`build/p3-ws220-model-all3.log`）: FAIL は p004〜p006 の 4 つだけ（前と同じ）。p003 の scenario は全部 PASS（queue-7/5/2/1 37.28・37.28・37.28・37.26、two-writers 37.26・誤り 0・順の乱れ 0、queue-1-loss-1 2.47、unresolved・unresolved-giveup・connect-unresolved は上の表）。前の模型 + 直した tcp.c で p002 の 11 の scenario は全部 p002 と同じ値。
- build: amd64・rpi4 の vmunix、warning 0、vmunix check PASS（`build/p3-ws220-amd64-5.log`・`build/p3-ws220-rpi4-5.log`）。
- `python3 plan/tools/style-check.py src/kern/net/tcp.c`: 111（基線 1e0e3c3be の 121 と内容で比べ、新しい指摘 0）。`git diff --check` ok。
- 模型が見ないこと（M1 の偽の重複は模型の相手が fast retransmit を持たないので数として見えない、code を読んで確かめた）。
- §5 の T1 の依頼は変えない（隣の未解決は QEMU の網では起きない）。

### 測りの道具の直し（2026-10-13 Q1 の依頼、T1-574 の報告）

- 症状: `tcp-stream-series.sh` が毎 run の終わりで止まる。`sudo kill -INT "$capture_pid"` の pid は capture を起こした background の shell（`$!`）で、background の sh は SIGINT を無視するので tcpdump に届かず `wait` が戻らない。`stream.py serve` も同じ形で止まらず、10.77.0.1:8080 で待つ物が run ごとに残った（Q1 が 16 個止めた）。ss の sampler も sudo の下で残っていた。
- 直し（`plan/ws220/tests/tcp-stream-series.sh`）: tcpdump・HTTP server・ss の sampler は、それぞれ自分の pid を `$out` の file（`wire-RUN.pid`・`server.pid`・`ss-RUN.pid`）に書いて `exec` する（`sh -c 'echo $$ > FILE; exec …'`）。新しい `stop_pidfile FILE` が SIGTERM を送り、10 s まで終わるのを待ってから SIGKILL。各 run の終わりに capture と sampler を止め、`teardown`（trap EXIT）で capture・sampler・server を必ず止める。rm は使わない（pid の file は出力の folder に残る）。
- `plan/ws220/tests/stream.py`: `serve` は SIGTERM を SIGINT と同じに扱い、log の `done` の行を書いて終わる。
- 確かめ（QEMU なし、host だけ）: 一時の namespace `p3t` の lo で、script と同じ形で server・sampler・capture を起こし、4 MiB を fetch して、pid の file で止めた（`build/tmp/p3-cmp.D1j7/harness.sh`、使い捨て）。pid の file の pid は `tcpdump`・`python3`・`sh` 自身、capture は止めてから 0.22 s で終わり起こした shell も終わった、namespace に残った process 0、8080 相当の port で待つ物 0、pcap は閉じられ（124 packets captured）、server の log に `done … bytes=4194304`。`sh -n` ok。QEMU の run（guest を含む全体）は T1 の次の run で確かめる。

## 8. T1-577 の「退行」の調べ（2026-10-13、q947、P3）

承認: q947（ユーザー「main に残し、直しを急ぐ」）。結論: **p003・p004 の退行ではなく、測りの環境（host）の差**。code の直しはしない。

- 症状（T1-577、bac387218 = p003＋p004、損失 0）: 基線 T1-574（0047ea2 = p002、別の session）と比べ wire の longest_stall_ms 2〜8 → 178〜226 ms、平均 89〜93 %。
- pcap（tshark）で見たこと: 止まりは TCP の timer ではなく guest の userland の thread が走らないこと。up は全部 ACK 済み・窓は開き・ring は空なのに tcp-stream が 184 ms 書かない。down は fetch が 12〜36 ms 読まず（その間 network worker は ACK を続ける）、窓が 0 になり、更新は 20〜180 ms 後か Linux の zero window probe（約 200 ms、我々の RTO ではない）の後。`segment_send_failures`・`send_refused`・再送は 0、ACK の数 = data の数で、p003 の拒み・retry の道は一度も走っていない。2 ms を超える wire の隙間は T1-574 の up 6・down 3 に対し T1-577 の up 324・down 703。
- 読み: 0047ea2..bac387218 の src・include の差は tcp.c・tcp-socket.h・sysctl.h・sandbox.h だけ。down の道（data の受け、ACK、recvfrom）の差は `tcp_send_ack` への置き換えだけで実質同じ。`tcp_timer_next_deadline` の過ぎた期限での worker の空回りは模型（本物の tcp.c、pass ごとに `tcp_timer_next_deadline() <= now` を数える一時の計装、scratchpad）で 0。ECM は rx_urb・tx_urb が 1 つずつで、完了の処理と re-arm は全部 network worker の poll（usb-cdc-ecm.c）で行うので、USB の 1 frame ごとに worker の起床が要る。wire の止まりは worker か userland の thread が CPU を得ないことで、host の負荷に敏感。
- A/B（T1-581、同じ 1 つの session で順に、`--runs up,down-disk --bytes 268435456`、証拠 /home/awe/zedBSD-worktrees/t1/build/review/t1-581/）:

| kernel | up MB/s | up stall | up の隙間 >2 ms / >30 ms | down-disk MB/s | down-disk stall | 隙間 >2 ms / >30 ms |
| --- | --- | --- | --- | --- | --- | --- |
| k1 0047ea2（p002、T1-574 と同じ） | 3.00 | 196 ms | 893 / 70 | 3.57 | 146 ms | 547 / 13 |
| k2 bac387218（p003＋p004、T1-577 と同じ） | 3.41 | 56 ms | 142 / 7 | 3.69 | 84 ms | 417 / 7 |
| k3 e7ccb1a1c（p005） | 3.46 | 4 ms | 4 / 0 | 4.60 | 30 ms | 107 / 0 |

  同じ session では p002 の kernel も T1-577 と同じ止まりを出し、k2 は k1 より全ての値で良い。k1→k2→k3 は時刻の順でもあり、host の負荷が下がっていった可能性があるので、k3 の改善も code の効果とは言い切れない（Q1 の読み: 21 時以後に別の Codex のセッションが WS074 の build・試験をしていた形跡）。k3 の `features=0x32`。
- 判定の基準の見直し（提案、Q1 が決める）: この共有の host では、別の session の基線との比（mean 90 %、longest_stall 1.5 倍）は環境の揺れで外れうる。p003〜p005 の合否は、同じ session で基線の kernel（0047ea2）を先か後に流した値と比べる。
- T1 の cputimes の採り方（`sysctl hw.cputimes`・`top -b`）は guest.sh の状態と series の guest が別で採れなかった（k*-cpu/*.txt）。
- 未実施: gdbstub での CPU の持ち主の確認（環境と分かったので不要と判断）、実機。
- T1-581 の追記（Q1 の読み、2026-10-13）: down-disk の `zero_windows_out` は k1・k2・k3 で 2・97・695（再送・`send_refused` などは全 run で 0）。Q1 の数えた 2 ms 超の隙間は本表と少し違う（k3 down-disk 213、k1 905/721、k2 143/546。数え方の違い、傾向は同じ）。`hw.cputimes` は k3 だけ採れた: up で **cpu0 の system がほぼ 100 %**（他の CPU は約 15 %）、down-disk でも cpu0 の system 49 s / 54 s。`top -b` は process ごとの値が 0 で使えなかった。
- 残件（次の Phase の候補、Q1 が決める）: cpu0 の system 100 % の持ち主が network worker か（ECM は rx_urb・tx_urb が 1 つずつで、完了の処理と re-arm を worker の poll で行い、USB の 1 frame ごとに worker を起こす）を、kernel の thread ごとの CPU 時間か gdbstub で確かめる。worker なら、ECM の複数の URB（受け・送りの transfer を複数並べる）か完了の処理の割り込みの側への移しで、usb-net の速さ（今 3.5〜4.6 MB/s）と環境の揺れへの強さが上がる見込み。WS220 の TCP の範囲の外（driver）なので、別の WS か WS220 の新しい Phase として計画する。down-disk の `zero_windows_out` の増え（k3 695）は受けの窓（p007、受け側の SWS 回避）で見る。
