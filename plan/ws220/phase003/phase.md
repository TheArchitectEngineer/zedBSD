<!-- awesome-plan project=zedbsd record=ws220-p003 -->
# ws220-p003: 送りの packet の不足（device が拒んでも書き手が 10 ms 眠らない、未送信を順に出す、ACK の出し直し）

Status: in-progress / test-wait（2026-10-13 P3: 実装・模型・build（amd64・rpi4、warning 0）は済み。QEMU は §5 の T1 の依頼待ち（番号は Q1 が付ける）。cleared は T1 の結果を Q1 が判定してから）
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
| unresolved | `--bytes 16Mi --unresolved-at-ms 100 --unresolved-ms 500` | 10、拒み ≤ 55（10 ms ごと） | 17.21（拒み 50、旧は書き手が 10 ms 眠る） | 17.03 PASS（拒み 51 = 10 ms ごと、停止 509.7 ms） |

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
- 削除してほしい出力（Q1、P3 の worktree）: `build/tmp/p3-old.ko63`（旧 tcp.c・旧模型の比べと style の比べ）、`build/p3-ws220-host44`（ring 44 の what-if）、`build/p3-ws220-*.log`（build と模型の log、本 file に要点を写した）。`build/p3-ws220-host`・`build/p3-ws220-amd64`・`build/p3-ws220-rpi4` は次の段で使い回すので残す。
- 模型の option の解き方（`strcmp` を条件に書く形、既存の物を含む）は p009（規約の全文）で直す。
- 模型が見ないこと: 本当の複数の CPU（`send_flushing` の取り合い、lock の外の並び）、pool の不足（模型の pool は共有の 256 で尽きない。`ENOMEM` の道は隣の未解決の `EAGAIN` と同じ待ちの分類で確かめた）、IPv6。
- 範囲の外のまま: cwnd・byte の送りの buffer（p006）、RTT・RTO（p004）、fast retransmit と「窓が開いた時の再送」の除去（p005）、受けの窓（p007、slow-reader の `receive_refused` は残る）。
