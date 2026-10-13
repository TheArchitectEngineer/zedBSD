<!-- awesome-plan project=zedbsd record=ws220-p002 -->
# ws220-p002: 正しさ（sequence の剰余の比較、SND.WL1/WL2、重なりの切り取り、諦めの時間）と S2、測りの道具の直し

Status: in-progress（2026-10-13 P4: 実装・模型の確かめ・build は済み（commit c13a8413f）。QEMU は T1 の依頼（下の §6、T1-569 の置き換え）待ち。cleared は Q1 の判定）
Parent: [WS220](../ws.md)
設計の出所: [p001 §4 の p002 の行](../phase001/phase.md)、[review-1](../phase001/review-1.md) の「p002 の開始の条件」・S1・S2・S3・S8・M1・M6

## ゴール

1. sequence の比較を `tcp.c` の全体で 2^32 の剰余の比較にする（wrap の後に ACK を全部捨てて約 30 s で切れる、候補 3）。
2. 送りの窓を RFC 9293 の SND.WL1/WL2 の条件でだけ更新する（古い ACK の窓を使わない）。
3. `receive_next` の前から始まり後ろへ続く segment は、取り済みの部分を切って残りを受ける（候補 8）。
4. 諦めを RFC 1122 4.2.3.5 の R2 に合わせる（data 100 s 以上、SYN 3 分以上、ユーザーの承認済み）。
5. S2: `net.tcpstats` の sequence の 3 つの欄は superuser だけに出す（main に入っている露出の直し）。
6. 測りの道具（review の S1・S3・S8）を直し、T1 の基線の測り（T1-569 の置き換え）を新しい道具で頼めるようにする。

## 1. 決めたこと

| 項目 | 決め | 理由 |
| --- | --- | --- |
| 比較の形 | `tcp_sequence_before(a, b)`（(int32_t)(a − b) < 0）と `tcp_sequence_at_or_before` の 2 つの static 関数。macro にしない（規約: 関数は debugger の止まる所になる） | RFC 9293 3.4 |
| SEG.ACK > SND.NXT | ACK を返して segment を捨てる（`acks_outside` に数える） | RFC 9293 3.10.7.4。今までは窓だけ取り data も受けていた |
| SEG.ACK < SND.UNA（古い ACK） | retire も窓の更新もしない、data と FIN は受ける（`acks_outside` に数える） | 同上（「無視してよい」）。窓は WL1/WL2 の条件でも更新されない |
| 窓の初期値 | 能動の open（SYN-ACK）でも受動の open（handshake の ACK）でも peer_window・WL1・WL2 を入れる | 受動の open は今まで handshake の ACK の窓を取らず、最初の ACK まで「窓不明 = 1 segment」だった（副作用として受けの直後の送りが早くなる） |
| 重なりの切り取り | 前の重なりだけ切る（`packet_buf_pull(header + overlap)`、sequence を receive_next に、payload_length を減らす）。窓の右端を越える後ろは切らない | 後ろは受信 queue の上限が今も受けを決めている（窓の約束の直しは p007、review M2）。RFC の「窓の外を切る」は p007 で窓の不変条件と一緒に扱う |
| R2 | 時間で数える: 前回 peer が答えた後の最初の timeout の時刻（`retransmit_since`、新しい欄）から data 100 s、SYN_SENT・SYN_RECEIVED は 180 s。どの ACK（進まない ACK も）でも 0 に戻る（相手が答える間は諦めない、RFC 1122 4.2.2.17 の persist も同じ） | review M6（回数でなく時間で）。RTO の最小が p004 で変わっても意味が変わらない |
| backoff の上限 | 8 s → 60 s（RFC 6298 2.5 の「最大は 60 s 以上」）。`tcp_backoff_timeout(count)` で timer と net.tcpstats の rto_ms が同じ式 | 100 s・180 s の R2 を 8 s ごとの再送で埋めない |
| 諦めまでの実の時間 | data: 最後の進みから 1+2+4+8+16+32+60 = 約 123 s（模型 data-giveup で 123.5 s）。SYN: 約 183 s（模型 syn-giveup で 183.0 s）。今までは約 31 s | ユーザー「接続を諦めるまでの時間は延ばしてよいです。」 |
| SYN_RECEIVED の R2 | SYN と同じ 180 s にした（RFC 1122 の MUST は「SYN の segment」で SYN-ACK も入る）。代わりに、listener の backlog（最大 16）を偽の SYN が埋めた時の塞がりが 31 s から約 3 分に伸びる | SYN cookie や古い half-open の追い出しは今も無いので、どちらの値でも偽の SYN 16 個で塞がる（規模は変わらない）。対策は別の段（残り R1） |
| RST の検査（review M1） | **p002 に入れた**: SYN_SENT 以外では、sequence が RCV.NXT ちょうどなら reset、窓（最後に広告した窓）の内なら challenge ACK を返して接続を保つ、窓の外は無視（RFC 5961 3.2）。challenge ACK の数の制限は入れない（Linux の全体の制限は CVE-2016-5696 の side channel になった。1 つの RST に 1 つの ACK なので増幅は無い） | 剰余の比較と同じ所で小さく直せ、S2 と同じ盲目の注入の前提を閉じる |
| 同期済みの状態の SYN | **p002 に入れた**: ESTABLISHED 以後に SYN を受けたら ACK を返して捨てる（RFC 5961 4）。handshake の最後の ACK を失った相手が SYN-ACK を出し直すと、今までは何も答えず相手が諦めるまで止まった（相手が先に話す protocol、例 SSH の banner） | 小さく、実の不具合を直す |
| S2 の address・port | superuser でない読み手にも出す。sequence の 3 つの欄（send_unacknowledged・send_next・receive_next）だけ 0 にし、新しい bit `TCP_STATS_HAVE_SEQUENCE`（0x20）で「出した」を示す（0 は「出さない」の意味、UAPI の既存の約束） | Linux の /proc/net/tcp・FreeBSD の netstat（see_other_uids の既定 1）と同じく 4 つ組は誰にでも見える。sequence が無ければ盲目の注入は窓を当てる必要があり、RST は上の検査で challenge になる。in_flight_bytes などの差は秘密でないので残す |
| 試験用の ISN | `CONFIG_TCP_ISN_WRAP_BEFORE`（既定 0 = 今のまま）。0 でない build では全ての接続の最初の data byte を wrap の N byte 前に置く。試験の image の config.mk の `ZEDBSD_TEST_CPPFLAGS` だけで立てる（`plan/ws220/tests/config-amd64-tcp-wrap.mk`、16 MiB） | review S8。`CONFIG_TCP_KEEPALIVE_*` と同じ「build で短くする」形（規約 12 の test 用の環境変数ではない）。受けの向き（相手 Linux の ISN）は動かせない |

## 2. 剰余の比較に替えた所（`src/kern/net/tcp.c`、commit c13a8413f の行）

| 所 | 前 | 後 |
| --- | --- | --- |
| ACK の受理（`tcp_segment_input` の ACK の段） | `ack >= una && ack <= nxt`（符号無し） | `acknowledges_unsent = before(nxt, ack)`、`acknowledges_old = before(ack, una)` |
| 送りの窓の更新 | どの ACK でも | `tcp_send_window_update_allowed_locked`: before(WL1, seq) または（seq == WL1 かつ at_or_before(WL2, ack)） |
| retire（`tcp_retire_acknowledged`） | `(int32_t)(ack − end) < 0` | `before(ack, end)` |
| 古い segment（payload の段） | `(int32_t)(seq + len − rcv_nxt) <= 0` | `at_or_before(seq + len, rcv_nxt)` |
| 重なり（新規） | 無し（offset が負で reorder にも入らず捨てた） | `before(seq, rcv_nxt)` かつ古くない → 切り取り |
| reorder の並べ（`tcp_reorder_keep`） | `(int32_t)(seq − kept) < 0` | `before(seq, kept)` |
| reorder の吐き出し（`tcp_reorder_drain`） | `(int32_t)(front − rcv_nxt) > 0` | `!at_or_before(front, rcv_nxt)` |
| RST の窓（新規、`tcp_reset_check_locked`） | 無し | `seq − rcv_nxt < advertised_window`（符号無しの差 = 剰余の距離） |

変えなかった所（差か等しさなので wrap に正しい）: `tcp_in_flight`（nxt − una）、`tcp_reorder_keep` の offset（seq − rcv_nxt、符号無しの距離）、SYN_RECEIVED・SYN_SENT・RST（SYN_SENT）・FIN の受理の `==`、keepalive の `nxt − 1`。`(int32_t)` の cast は `tcp_sequence_before` の 1 か所だけになった。

## 3. 変えた file

- `src/kern/net/tcp.c`: 上の全部。定数 `TCP_RETRANSMIT_MAX` を外し `TCP_RETRANSMIT_TIMEOUT_MAX`・`TCP_GIVE_UP_DATA_MS`・`TCP_GIVE_UP_SYN_MS`・`CONFIG_TCP_ISN_WRAP_BEFORE`・`enum tcp_reset_check`。関数 `tcp_sequence_before`・`tcp_sequence_at_or_before`・`tcp_initial_sequence`・`tcp_backoff_timeout`・`tcp_retransmit_given_up_locked`・`tcp_reset_check_locked`・`tcp_send_window_update_allowed_locked`。`tcp_stats_copy`・`tcp_stats_fill_locked` に superuser。
- `include/kern/net/tcp-socket.h`: `send_window_sequence`・`send_window_acknowledgement`（WL1・WL2）、`retransmit_since`、`tcp_stats_copy` の引数。
- `include/uapi/sysctl.h`: `TCP_STATS_HAVE_SEQUENCE`（大きさと version は変えない）。
- `src/kern/sysctl.c`: `sysctl_tcpstats` に superuser を渡す。
- `userland/tests/tcp-stream/`（新規）: 塞がる C の送り手（connect して 64 KiB ずつ blocking の write、shutdown して相手の close を待つ）。test image だけ（`ZEDBSD_USER_PROGRAMS += tcp-stream`）。
- `plan/ws220/tests/`: 下の §4。HAL・driver・toolchain は変えていない。

## 4. 測りの道具の直し（review S1・S3・S8、L3）

- `tcp-stream-series.sh`:
  - run を `--runs`（既定 `down,down-disk,up`、症状は受けの向きなので down を先に）: `down`（fetch → /dev/null）、`down-disk`（fetch → `/root/ws220-down.bin`、毎回上書き、`--disk-bytes` 既定 256 MiB: 読み手が遅いと窓 0 → 更新 → 相手の persist の道筋）、`up`（`tcp-stream` の塞がる送り手 → host の `stream.py listen`、port 8081: `send_buffer_sleeps` の道筋、S1）、`up-ssh`（今までの sshd の dd、非塞ぎ・暗号の上限）。`--direction` は古い形として残す（both = 既定）。
  - QEMU: tap・bridge・両方の veth の TSO・GSO・GRO を `ethtool -K` で切ってから netem（S3）。tap は QEMU が開いた後にもう一度。`settings.txt` に `ethtool -k` の結果。
  - 各 run で host の側を `tcpdump -s 80`（QEMU は namespace の zbsv1、実機は route の device か `--capture-iface`。実機の host の offload は変えない）で `wire-RUN.pcap` に撮る（1 GiB の run で約 140 MB）。`ss` は transfer の port で絞る（L3）。
  - `remote()` の timeout を 300 s から `--timeout` + 120 s に（1 GiB の fetch が 300 s で切られていた）。
  - BUG-298 の直しの setup の確かめ（`net static`、ping、10 s 後の address の確かめ）はそのまま。
  - `tcp-stream-series.sh`・`analyze.py`・`stream.py`・`tcp-model/run.sh` に実行権限（T1-569 で `sh` で起動していた）。
- 要る物: host に `tcpdump`・`ethtool`（2026-10-13 P4 が `sudo apt-get install ethtool tcpdump` で入れた。無ければ script が止まって言う）。
- `analyze.py`:
  - summary は転送の行だけ（最初に data が動いた行から最後の行まで、頭の鍵交換と尻の FIN の行を数えない）、量は行の量の和（4 GiB の wrap で潰れない）、`below_1mbs_after_5s` を足した。up で sequence が無い（root でない sysctl）時は `data_bytes_out` で数えて注記。
  - `--pcap FILE --port PORT`（`--sender-port` は up-ssh 用）で `analyze: wire ...` の行: 受け手の ACK を unwrap して 100 ms の段と秒で、`longest_stall_ms`（ACK が進まなかった最長、0.5 s・2 s の停止を測る形）、`idle_100ms_steps`、1 秒の速さの最低・1 % 点・中央値、`resent_segments`（送り手が出し直した data の segment）、`crossed_wrap`。
- `stream.py`: `listen ADDRESS PORT LOG`（1 つの接続を終わりまで読んで閉じる）。
- `config-amd64-tcp.mk` に `tcp-stream`、新しい `config-amd64-tcp-wrap.mk`（ISN を wrap の 16 MiB 前に）。
- 模型（`tcp-model/`）: `--overlap-ab P --overlap-bytes K`（A の data の segment の頭を K byte 前へ伸ばし、stream の前の byte で埋める）、`--blind-at-ms T`（B に窓の内の RST・窓の外の RST・SYN を与える）、`--blackhole-at-ms T`・`--connect-blackhole`（諦めの時間）。`run.sh NAME...` で scenario を選べる。`check_tcpstats` は superuser でない読みで sequence が 0 で bit が無いこと、port は出ることを確かめる。

## 5. 受け入れの目標の決め方の改訂（review S8、p001 §5 を置き換える提案。値は基線の後に Q1 がユーザーに示す）

1. 停止は wire の `longest_stall_ms` で測る（1 秒の sample の「0 の行」では 1 s 未満が見えない）。目標 (ii) の 0.5 s・(iii) の 2 s はこの値に当てる。
2. 「最初の 5 s の後に 1 MB/s 未満の秒が無い」は wire の `below_1mbs_after_5s`（転送の頭から 5 s の後）、「どの 1 秒も中央値の 50 % 以上」は `p1_1s_mbs` ≥ 中央値の 50 %（1 % 点、host の揺れで 1 秒だけ落ちる偽の FAIL を避ける）に替える。
3. QEMU の up の「路の上限」は直した TCP 自身で決めない（自己参照の除去）: 同じ ECM の down の基線の値（受けの側は 8 KB の送りの ring に縛られない）を上限の目安にし、要るなら Linux の guest 同士の usb-net の値（ユーザーの許可が要る、Debian の guest の例外は今 WS105・108・131・109 だけ）。
4. 暫定の QEMU の down の目標は今の値以上（T1-517 の 8.4 MB/s・`tcp-receive-speed` の 9.8〜11.1 MB/s より上、例えば 0 % で 10 MB/s）。
5. wrap は 4 GB の run をせず、`config-amd64-tcp-wrap.mk` の image の up 64 MiB（毎回 wrap を越える、`crossed_wrap=1`）と模型の lan-wrap・overlap-wrap で確かめる。受けの向きの wrap（相手 Linux の ISN）は 1 GiB の run の約 1/4 で偶然越えるだけで、確定的には模型で確かめる。

## 6. T1 への依頼（T1-569 の置き換え、基線の測り、QEMU）

p002 は速さの道筋（窓・ring・RTO・損失の回復）を変えないので、p002 の kernel で取った値を p003 以後の基線にしてよい（模型の lan-clean・損失・slow-reader の数は p001 と同じ、下の §7）。

- image A（IMAGE_A は作った disk image）: commit c13a8413f 以後（Q1 の merge の SHA）の tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk build/t1-ws220-tcp`。
- image B（wrap、IMAGE_B）: 同じ tree で `plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp-wrap.mk build/t1-ws220-wrap`。
- host: `tcpdump`・`ethtool` があること（無ければ `sudo apt-get install tcpdump ethtool`）。sudo（ip・tc・namespace・ethtool・tcpdump）。QEMU は 1 つ。
- 流す物（image A、各 run は down・down-disk・up）:
  1. guest で root の `sysctl net.tcpstats | head -3`: 1 行目 `net.tcpstats: time_ms=… sockets=…`、SSH の接続の行に `features=0x20` と 0 でない `send_next`。
  2. `plan/ws220/tests/tcp-stream-series.sh IMAGE_A --bytes 1073741824 --out build/ws220-series-0`（損失 0、down 1 GiB・down-disk 256 MiB・up 1 GiB、ECM で全体 7〜10 分の見込み）。
  3. `… --loss 0.1 --bytes 268435456 --out build/ws220-series-01`。
  4. `… --loss 1 --bytes 67108864 --timeout 600 --out build/ws220-series-1`（直す前の up は損失 1 % で 0.1 MB/s 程度の見込み = 64 MiB で約 10 分。長すぎる時は `--runs down,down-disk` と `--runs up --bytes 16777216` に分ける）。
  5. （比べ、任意）`… --runs up-ssh --bytes 268435456 --out build/ws220-series-ssh`。
- 流す物（image B）: `… IMAGE_B --runs up --bytes 67108864 --out build/ws220-series-wrap`。
- 合否: 各 run が `tcp-stream-series: PASS`（全部届いた）。image B は加えて up の `analyze: wire` の行が `crossed_wrap=1`。速さは基線として記録（目標の判定はしない）。
- 返す物: 各 run の `summary.txt`（`analyze: summary`・`analyze: wire` の行、host の時計の MB/s）、`settings.txt`（offload が off か）、`series-*.csv`、失敗なら `transfer-*.txt`・`setup.txt`。pcap は path だけ（大きい）。
- 見る値（p001 §6 に足す）: wire の `longest_stall_ms`・`idle_100ms_steps`・`resent_segments`、up の `send_buffer_sleeps`（tcp-stream の道筋で 0 でないか）、down-disk の `zero_windows_out`・`window_updates_out`・`receive_refused`。
- 実機（依頼 B、Q1 か T1）: `tcp-stream-series.sh ssh:root@MACHINE --server-ip ADDRESS --bytes 1073741824`。up には機械の `/bin/tcp-stream`（image A の build の `bin/tcp-stream`、Pi 4 は rpi4 の build の物）を写しておく。

## 7. 実行した command と結果（host、QEMU は未実施）

- `git merge main`（99e3231a6）。
- 模型 `OUT=build/p4-ws220-host sh plan/ws220/tests/tcp-model/run.sh`（約 5 分、data-giveup と syn-giveup の仮想時間が長い）:

| scenario | p001（直す前） | p002 |
| --- | --- | --- |
| lan-clean | 37.28 PASS | 37.28 PASS |
| rtt-10ms | 0.78 FAIL | 0.78 FAIL（p006 の段） |
| lan-loss-0.1 | 1.04 FAIL | 1.04 FAIL（p004・p005） |
| lan-loss-1 | 0.10 FAIL | 0.10 FAIL（同） |
| rtt-10ms-loss-0.1 | 0.39 FAIL | 0.39 FAIL（同） |
| lan-loss-1-zedbsd | 6.31 PASS | 6.31 PASS |
| lan-ackloss-1 | 37.18 PASS | 37.18 PASS |
| **lan-wrap** | 0.14 FAIL（4 MiB で止まり ENOTCONN） | **37.24 PASS** |
| slow-reader | 1.71 PASS | 1.71 PASS |
| overlap（新、20 に 1 つの segment が 100 byte 重なる） | 旧 tcp.c で 0.25 MB/s FAIL（重なるたびに 1〜4 s 止まる） | 37.26 PASS（1563 個を切って受け、corrupt 0） |
| overlap-wrap（新、重なり + wrap） | 旧で 0.08 FAIL（止まり ENOTCONN） | 37.24 PASS |
| blind-segments（新、窓の内の RST・窓の外の RST・SYN） | 旧で 0.1 s で ECONNRESET、FAIL | 37.26 PASS（接続は保たれる） |
| data-giveup（新、0.5 s で線が全部落ちる） | （旧は約 31 s） | 123.5 s で諦め PASS（100〜200 s） |
| syn-giveup（新、SYN が全部落ちる） | （旧は約 31 s） | 183.0 s で諦め PASS（180 s 以上） |

  旧 tcp.c の欄は、HEAD の src・include を `build/tmp/p4-old.b4Xl` に取り出し、新しい header と模型（tcp_stats_copy の引数だけ足した旧 tcp.c）で流した。`tcpstats: ok … user_hides_sequence=1` は全ての完了した run で出る。
- `MODEL_CFLAGS=-DCONFIG_TCP_ISN_WRAP_BEFORE=4194304U` の模型: A の最初の sequence 4290772992（= 2^32 − 4 MiB）、16 MiB が全部届く。
- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p4-ws220-amd64 ZEDBSD_USER_PROGRAMS=sysctl build/p4-ws220-amd64/vmunix build/p4-ws220-amd64/bin/sysctl`（`amd64 vmunix check: PASS`）、同じ BUILD で `ZEDBSD_USER_PROGRAMS="sysctl tcp-stream" build/p4-ws220-amd64/bin/tcp-stream`（-Werror で通る）、`config/ci/config-rpi4.mk`・`build/p4-ws220-rpi4` の vmunix と sysctl（`arm64 vmunix check: PASS`）、`plan/ws220/tests/config-amd64-tcp-wrap.mk`・`build/p4-ws220-wrap` の vmunix（tcp.c の compile に `-DCONFIG_TCP_ISN_WRAP_BEFORE=16777216U` が入ることを log で確かめた）。
- `python3 plan/tools/style-check.py`: tcp.c 125 → 121、sysctl.c 17 → 17、`userland/tests/tcp-stream/main.c` 0。`git diff --check` ok。`sh -n tcp-stream-series.sh` ok。
- analyze.py の host の確かめ: loopback で 200 MB を 1.5 s 止めて読む client → `longest_stall_ms=1456`、`idle_100ms_steps=14`。合成の pcap（ACK が 2^32 − 100000 から wrap を越え、600 ms の空き、出し直し 1 つ）→ `crossed_wrap=1 longest_stall_ms=600 resent_segments=1`。合成の series（send_unacknowledged が wrap を越え、頭 3 行と尻 4 行が 0）→ 23 s・21.9 MB（頭尻を数えず、wrap で潰れない）。
- namespace の host の予行（QEMU なし）: veth の両端の tso/gso/gro を切って netem 1 %・1 ms、`stream.py listen` に 50 MiB → 36209 segment（≈ 1448 byte ずつ = MSS、GSO の塊でない）、`resent_segments=357`（≈ 1 %）、`ethtool -k` が 3 つとも off。namespace と veth は消した（`ip netns del`・`ip link del`）。

未実施: QEMU（§6 の依頼）、実機、guest で superuser でない利用者の `sysctl net.tcpstats`（模型で確かめただけ）。

## 8. 残りと判断

- R1（RFC 1122 4.2.3.5 の R1 回の後に app・IP へ知らせる）、RFC 5961 5 の「古すぎる ACK に challenge ACK」、窓の右端の外の data を切ること（p007 で）、ISN の予測可能（RFC 6528、`ticks × 1103515245 + port`）、SYN flood への備え（SYN cookie、half-open の追い出し）は p002 に入れていない。ISN と SYN flood は Q1 が Future Work か bug にするかを決める。
- `old ACK` と `ACK > SND.NXT` を同じ `acks_outside` に数える（UAPI に欄を足さない。M9 の UAPI の伸ばし方が決まるまで）。
- 受動の open で handshake の ACK の窓を取るようにしたので、受けた直後の最初の送りが 1 segment から窓まで出せる（速さの変化、正しさの直し）。
- 削除してほしい出力（Q1、P4 の worktree）: `build/tmp/p4-old.b4Xl`（旧 tcp.c の比べ）、`build/tmp/p4-analyze.88LN`・`build/tmp/p4-analyze.RPys`・`build/tmp/p4-nstest.EAKC`（analyze の host の確かめ）、`build/tmp/p4-style-old.c`・`build/tmp/p4-new.txt`・`build/tmp/p4-o.txt`・`build/tmp/p4-n.txt`（style の比べ）、`build/p4-ws220-host-isn`（ISN の模型）、`build/p4-ws220-wrap`（wrap の config の build の確かめ）、`build/p4-ws220-*.log`。`build/p4-ws220-host`・`build/p4-ws220-amd64`・`build/p4-ws220-rpi4` は次の段で使い回すので残す。
- T1-571（BUG-298 の確かめ、未実行）は同じ script を `--bytes 67108864` で使う。既定の run が down・down-disk・up になったので、down-disk が 1 本増える（合格の条件「down と up が完走」はそのまま当てられる）。
- 次: T1 の結果（§6）を Q1 が判定 → PASS なら cleared。基線の値で §5 の目標を ws.md に書き、p003 以後（review S4〜S7 を設計に入れてから）。
