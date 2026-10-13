# ws220-p001 設計の敵対的 review（第 1 回）

対象: [phase.md](phase.md)（第 1 版、main f9077504e。行番号は 508d33db9 と同じ: その後 `src/kern/net`・`include/kern/net`・`include/uapi/sysctl.h` に変更なし）。
照合した物: `src/kern/net/tcp.c`・`socket.c`・`socket-file.c`・`packet-buf.c`・`net-device.c`・`core.c`、`include/kern/net/tcp-socket.h`・`packet-buf.h`、
`include/uapi/sysctl.h`、`src/kern/sysctl.c`・`syscall.c`（sysctl の口）、`userland/base/sysctl/main.c`、`src/drivers/usb/usb-cdc-ecm.c`・`usb-cdc-ncm-net.c`、
`src/drivers/platform/rpi4/rpi4-ethernet.c`（GENET）、`src/kern/sched.c`、`plan/ws220/tests/`（tcp-model・tcp-stream-series.sh・analyze.py）、
`plan/bugs/BUG-222.md`、`plan/ws221/ws.md`、RFC 793/1122/5681/6298/6582/7323/2018/9293（記憶による照合、本文は開いていない）。
review の担当は文書と source を変えていない。

## 判定

- **致命的: なし。** §1 の現状の記録の file:行は確かめた範囲で全部正しく、模型も再現した（下の「確かめた点」）。
- **重大: 8 件**（S1〜S8）。主に (a) 症状の向きと道筋が未確認のまま、up の試験（sshd）では候補 1 の仕組みがそもそも起きない、(b) 損失の試験の netem が GSO の塊ごとに落とす、
  (c) net.tcpstats が一般の利用者に他人の接続の sequence 番号を出す（既に main にある）、(d) p003・p004・p005・p006 の設計の穴と段の順。
- **p002 を始めてよいか: 条件つきで可。** p002（sequence の剰余の比較・SND.WL1/WL2・重なりの切り取り・諦めの時間）は基線にもユーザーの判断にも
  依らない正しさの直しで、模型の lan-wrap で確かめられる。条件は末尾の「p002 の開始の条件」。**ただし T1 の依頼 A（基線）は S1・S3・S8 を直してから流す**
  （今のまま流すと損失の基線が歪み、候補 1 は観測できず、0.5 s の停止の目標は測れない。QEMU の枠を無駄にする）。p003 以後は S4〜S7 を設計に入れてから Queue にする。

## 重大

### S1. 症状の向き・道筋が未確認のまま原因の順位を決めている。up の試験（sshd）は候補 1 の「10 ms 眠る」に入らない

- 根拠:
  - ユーザーの原文（ws.md）は向きも道具も言っていない。これまでに記録された遅さは **受けの向き**（BUG-222: 「SCPで他のPCからファイルを送った」= zedBSD が受ける、
    950 KB/s → 4.9 MB/s、`plan/bugs/BUG-222.md:11,201`）。phase.md §2 は「最も合うのは送りの向きの 1 と 2」と書くが、根拠は模型（送りの向きだけを模す）である。
  - 候補 1 の 10 ms の眠りは**塞がる書き手だけ**の経路（`tcp.c:2302〜2329`）。非塞ぎの file は `MSG_DONTWAIT` になり（`socket-file.c:274〜277`）、
    `tcp_sendto` は `-EAGAIN` を返す（`tcp.c:2307〜2308`）。OpenSSH の sshd は接続の socket を O_NONBLOCK にする（推測: OpenSSH の packet 層の既定の動作、
    zedBSD の port で変えていないことは未確認）。このとき `tcp_poll` は ring に空きがあれば POLLOUT を返す（`tcp.c:2812〜2816`）ので、sshd は眠らずに
    poll と write を回し続ける（空回り）。§3.2 の up は `ssh … dd`（`tcp-stream-series.sh:148`）なので、**依頼 A では候補 1 の仕組みが起きず、
    `send_buffer_sleeps` は 0 のまま**で、`send_refused` だけが増える。模型の書き手は塞がる書き手を模している（`model.c:617〜625`）。
  - 同じく、窓が足りない時（`tcp_sendto` の窓の条件、`tcp.c:2259〜2261`）も POLLOUT は立つので、非塞ぎの書き手は窓が閉じている間 CPU を回す。
- 起こる状況: 基線を取っても「候補 1 が原因か」を判定できず、p003 の効果も QEMU で確かめられない。受けの向きの症状（ユーザーが実際に見た物かもしれない）は
  候補 5・6・7 しか持たず、模型にも無い。
- 修正案:
  1. ユーザーに再現の条件を聞く（向き、道具（scp・fetch・その他）、相手の OS、路（5330 の NCM・Pi 4・QEMU）、書き先（USB stick の disk か /dev/null か）、
     何秒で落ちるか）。§8 の判断の一覧に足す。
  2. up の試験に**塞がる書き手**を足す（sshd の暗号の上限も外れる）。zedBSD の userland に nc は無い（`userland/base/` の一覧）ので、`plan/ws220/tests/` に
     小さな C の送り手（connect して write を続けるだけ）を置き、config.mk で build に入れる（AGENTS.md「試験の image の作り方」の範囲）。
  3. 下りに「disk へ書く」run（`fetch -o /root/x`、実機は scp で disk へ）を足す。読み手が遅いと窓 0 → 窓の更新 → 相手の persist の経路になり、BUG-222 の形に近い。
  4. 「見る値」に「up が sshd なら `send_buffer_sleeps` は 0 で正常、`send_refused` と sshd の CPU を見る」と書く。

### S2. net.tcpstats が一般の利用者に全ての接続の sequence 番号を出す（既に main にある）

- 根拠: `sysctl_tcpstats`（`src/kern/sysctl.c:880〜911`）は superuser を確かめず、`tcp_stats_fill_locked` は `send_unacknowledged`・`send_next`・
  `receive_next`、両端の address と port を全ての socket について写す（`tcp.c:1245〜1269`）。
- 起こる状況: 一般の利用者が root の SSH の接続の 4 つ組と sequence 番号を読み、外の協力者が窓の内の data を差し込める（盲目の注入の前提が揃う）。
  RST は今でも sequence を見ずに受ける（M1）ので、さらに容易。Linux の /proc/net/tcp・inet_diag は絶対の sequence 番号を出さず、FreeBSD の
  pcblist は他の uid の接続を `see_other_uids` で隠す（記憶による比較）。
- 修正案（小さく、p002 と一緒に直せる）: superuser でなければ sequence の 3 つの欄を 0 にする（`features` に「sequence を出した」の bit を足すか、
  0 を「出さない」と UAPI の注記に書く）。試験は root で読むので困らない。入れた後の直しなので、Q1 に「main に入っている」ことを伝える。

### S3. netem を veth に付けると GSO の塊ごとに落ちる: 損失の試験の基線と受け入れが歪む

- 根拠: `tcp-stream-series.sh:106〜108` は namespace の `zbsv1`（host → guest、下りの data）と `zbsv0` に netem を付けるが、offload を切っていない。
  Linux の veth は TSO/GSO を持つので、namespace の TCP の送りは 64 KB までの GSO の skb のまま `zbsv1` の qdisc を通り、bridge から tap へ出る時に
  分けられる（推測: Linux の既定の動作。この host で `ethtool -k` は見ていない）。netem の loss は skb ごとなので、**0.1 % の損失が「窓の数十 segment を
  一度に失う」形**になる。
- 起こる状況: 下りの 0.1・1 % の基線と目標 (ii)(iii)（§5）が、RFC の想定する独立の損失でなく塊の損失を測る。Linux の NewReno（SACK 無し）は
  塊を 1 往復に 1 つずつ埋めるので、下りの数字が実際より大きく悪く出る。WS033 の `tcp-loss-speed.sh` の過去の数字（T1-516・T1-517）も同じ歪みの
  可能性がある（未確認）。
- 修正案: namespace の側で `ip netns exec zbs ethtool -K zbsv1 tso off gso off gro off`、host の側で `ethtool -K zbsv0 …`・bridge・tap も同じく切ってから
  測る。`settings.txt` に `ethtool -k` の結果を残す。

### S4. p003 の設計が足りない（順序の崩れ、timer の経路の流用、眠りの期限、空回り、既存の二重送信）

- 根拠と起こる状況:
  1. **順序**: 「device が拒んだ segment は ring に残して印を付け、空いた時に出す」とあるが、その後の write の segment は今の経路
     （`tcp_send_reliable` の `tcp_send_segment_at`、`tcp.c:1732`）でそのまま出る。拒まれた segment より後ろが先に出ると相手には穴に見え、
     重複の ACK が出る。p005 の fast retransmit が入ると**偽の再送と cwnd の半減**になる。
  2. **timer の流用**: 「再送の経路で出す」の timer は ring の**最古だけ**を送り（`tcp.c:562`）、送るたびに `retransmit_count` を増やして
     backoff を倍にする（`tcp.c:583〜588`）。拒まれた segment が最古でない時は出ず、最古の時は「再送」と数えられ、次の本当の損失の RTO が 2 s 以上になる。
     p004 の Karn の規則も誤って効く（一度も出ていない segment は再送ではない）。
  3. **眠りの期限**: `tcp_timer_next_deadline` は ring が空なら keepalive の期限しか見ない（`tcp.c:661〜664`）。p003(b) の「ACK が要る」印を
     ring が空の時に残しても、worker はそれを知らずに眠る。
  4. **空回り**: pool の不足（`packet_buf_alloc` の失敗）は device の完了で知らされないので、「次の tick（1 ms）にやり直す」は pool が尽きている間
     毎 ms の空回りになる。
  5. **既存の二重送信**: 拒まれた data の segment を取り戻せるのは ring の最後の時だけで、取り戻せなくても error を返す（`tcp.c:1754〜1772`）。
     同じ socket に 2 つの thread が書く（または書きの間に close の FIN が入る）と、取り戻されなかった segment が ring に残ったまま書き手が同じ data を
     もう一度送り、**stream に同じ byte が 2 回入る**。模型は 1 thread なので見えない。
  6. 再送の timer の送りの失敗（`tcp.c:597`、ENOBUFS・EAGAIN）は次の deadline（2 s 以上）まで待つ。これも p003 の「device が空いたら」の対象に入れるべき。
- 修正案: p003 の設計に次を書く。
  - 「送った位置」（snd_max）と「送る位置」を分け、拒まれた segment から後ろは全部「未送信」として順に出す（新しい write は未送信の後ろに積むだけ）。
  - 未送信の送り出しは RTO の経路と別の関数にし、backoff・再送の数・RTT の計り始めを動かさない（RTT の計り始めは実際に device が受けた時刻）。
  - `tcp_timer_next_deadline` に「未送信あり」「ACK が要る」の期限を入れる。device の完了（`net_device_schedule_poll` → worker → `tcp_timer_run`、
    `usb-cdc-ecm.c:823`、`usb-cdc-ncm-net.c:887`、`core.c:602`）で即時に、pool の不足は 10 ms 程度の時間で再試行（1 ms ごとの空回りをしない）。
  - 取り戻せない時は error でなく 0（ring が持つ）を返す。複数の書き手の試験（模型に 2 つの書き手）を足す。
  - 「ACK が要る」は数も持つ（重複の ACK を 1 つにまとめると相手の fast retransmit の 3 つに届かない）。

### S5. 段の順と依存の誤り（二度手間、zedBSD 同士の退行、WS221 との依存）

- 根拠と起こる状況:
  1. **p006(b) が p003・p005 の土台を置き換える**: p003（未送信の印）と p005（NewReno の partial ACK、persist）は今の 8 個の packet の ring の上に作る設計だが、
     p006(b) で ring を byte の送りの buffer に替える。p003・p005 の code と模型の確かめを p006 でもう一度作り直すことになる。未送信の扱い（S4）は byte の
     buffer の方が自然に書ける（snd_una・snd_nxt・snd_max の 3 つの位置）。
  2. **p005 が p007(a) より前**: 「窓が開いた時の再送」（`tcp.c:3479〜3483`）は zedBSD の受け手が「1 packet = 1460」の約束を破って segment を拒んだ時
     （候補 5、`tcp.c:3560〜3576`）の唯一の速い回復である（`tcp_recvfrom` の注記 `tcp.c:2411〜2413` がそれを前提にしている）。p005 でこれを外し、
     p007(a) で約束を直す前の間は、zedBSD 同士の転送の拒否が RTO 待ちになる。§3.3 の表も「lan-loss-1-zedbsd は窓が動くので偶然効く」と書いている。
     p005 の確かめの scenario に lan-loss-1-zedbsd・slow-reader の非退行が入っていない（入っているのは lan-loss-* の Linux 相当と slow-reader の
     `immediate_resends` 0 だけ）。
  3. **WS221（Jumbo frame）との依存が未記録**: WS221 は「TCP の MSS（WS220）が MTU から決まる」と書き（`plan/ws221/ws.md:19`）、packet の storage
     （2 KB、`packet-buf.h:30`）も変える。p006 の「相手の MSS・device の MTU − 40」と p007 の「受けの queue を byte で数える」「pool の内で受けの buffer
     の上限」は MTU が 9000 になると前提が変わる。
- 修正案: 順を p002 → **p006(b) の送りの buffer の作り直し（振る舞いは今と同じ: 実効の窓 8 KB のまま、cwnd 無し）** → p003 → p004 → p005 →
  p006(a)(c) の cwnd・窓の拡大・Nagle → p007 → p008 にするか、p005 の「窓が開いた時の再送」の除去を p007(a) と同じ段に移す。どちらでも p005 の
  確かめに lan-loss-1-zedbsd・slow-reader の非退行を入れる。WS220 の ws.md と WS221 の ws.md に依存（WS221 の TCP の部分は WS220 の p006・p007 の後、
  p006・p007 の設計は MTU を定数にしない）を書く（Q1 の仕事）。

### S6. p004 の RTO の最小 200 ms は「Linux・FreeBSD と同じ」ではなく、遅延 ACK の相手で偽の timeout を出す

- 根拠: §4 p004 は「RTO = SRTT + 4·RTTVAR、最小 200 ms（Linux・FreeBSD と同じ）」。Linux は RTO = SRTT + max(4·RTTVAR, rto_min = 200 ms)
  で、**常に SRTT より 200 ms 以上長い**。FreeBSD は max(rexmit_min 30 ms, SRTT + 4·RTTVAR) に slop 200 ms を足す（どちらも記憶による。source は開いていない）。
  単純な「200 ms の床」は LAN では RTO ≈ 200 ms になり、遅延 ACK が 200 ms の相手（Windows の既定、macOS）や、Linux の遅延 ACK の最大 200 ms
  （TCP_DELACK_MAX）に当たると、1 segment だけ出ている時に ACK が RTO と同時か後に来る。RFC 6298 2.4 の G（clock の粒度）の項も式に無い。
- 起こる状況: 偽の timeout → p006 の後は cwnd = 1 と go-back で速さが崩れる。F-RTO（RFC 5682）や取り消し（Eifel）の計画が無いので戻せない。
  USB の adapter の queue で RTT が負荷の下で 24 ms に伸びる（BUG-222 T1-516）時の RTTVAR の急変でも起きうる。
- 修正案: Linux と同じ形 RTO = SRTT + max(G, 4·RTTVAR, 200 ms)（最大 60 s）にし、ユーザーの判断の文面を「RFC 6298 の 1 s の SHOULD から外して
  Linux と同じ形にする」と正す。RTT の sample は実際に device に渡った時刻から（S4）、SYN を再送した時は RFC 6298 5.7 の 3 s。p006 の前に偽の
  timeout の検出（F-RTO か、再送の後すぐ来た ACK で cwnd を戻す）を入れるかを決める。clock は amd64・arm64 は 1 ms だが i386・sparcv9・m68030 は 10 ms
  （`include/hal/arch/i386.h:17` ほか `HAL_TIMER_FREQUENCY` 100）なので、「clock は 1 ms の tick」は全体には成り立たない。

### S7. p006 の危険が書かれていない（再送の stack の buffer、close の後の data の喪失、記憶、POLLOUT）

- 根拠と起こる状況:
  1. **再送の stack の buffer**: timer は `uint8_t payload[TCP_MSS]`（`tcp.c:425`）に写し、`length > sizeof(payload)` なら接続を **EIO で壊す**
     （`tcp.c:566〜577`）。p006 で segment を 1460 にして timer を直し忘れると、最初の再送で全ての接続が切れる。`TCP_MSS` は他にも窓 0 の 1 segment
     （`tcp.c:1691`・`2258`）、write の上限（`tcp.c:2244`）、stats（`tcp.c:1251`）に使われている。
  2. **close**: endpoint は最後の参照で ring ごと解放され（`tcp.c:2645〜2647`）、close は最大 10 s だけ待つ（`TCP_CLOSE_DRAIN_MS`、`tcp.c:2686〜2768`）。
     今は ring が 8 KB なので足りるが、256 KB の送りの buffer では遅い相手・窓 0 の相手で close の後の data が黙って捨てられ、相手には RST が返る
     （`tcp_send_reset`）。close する process も最大 10 s 塞がる。
  3. **記憶**: 既定 256 KB を socket ごとに確保すると、socket の多い時に kernel の heap を食う（32-bit の board は特に）。
  4. **POLLOUT**: 今は「ring に空き」で立つ（`tcp.c:2812〜2816`）。byte の buffer では「空きが下限以上」（send low-water）に変える必要がある。
- 修正案: p006 の設計に、(1) `TCP_MSS` を使う全ての所の一覧と timer の buffer の大きさ（または buffer から直接作る）、(2) close の後の orphan の扱い
  （data が全部 ACK されるか R2 まで endpoint を残す、または close の待ちを buffer の量と速さから決める）と、その間の参照の持ち方、(3) buffer は必要に
  応じて伸ばす（最初から 256 KB を取らない）、arch ごとの上限、(4) POLLOUT の下限、を書き、模型に「close の直後に全部届くか」の scenario を足す。

### S8. 受け入れの目標と道具が測れない・自己参照になっている

- 根拠と起こる状況:
  1. 目標 (ii)「最長の 0 の続きが 0.5 s 未満」と (iii)「2 s 未満」を、guest の約 1 秒ごとの sample（`tcp-stream-series.sh:139`）の
     「0 の行の長さ」（`analyze.py:137〜144`）で測る。1 秒の間に 1 byte でも進めば 0 にならないので、**1 s 未満の停止は見えない**。
  2. analyze.py は転送の前（SSH の鍵の交換・認証の間、slow start）と後（`sleep 2` の間に接続が FIN の状態で残る、`tcp-stream-series.sh:154`）の行も数える。
     目標 (i)「最初の 5 s の後に 1 MB/s 未満の秒が無い」に対して、頭と尻の 0 の行で偽の FAIL になる。
  3. up の総量は `(last - first) % 2^32`（`analyze.py:136`）なので、目標 (iv) の 4 GiB の転送では 0 に近い値になる（行ごとの量は正しい）。
  4. QEMU の up の「路の上限」を「p006 の後に ring を大きくした値」で決める（§5 の 2）のは、直した TCP 自身で上限を決める自己参照で、80 % の目標が必ず通る。
  5. 暫定の QEMU の down ≥ 6 MB/s（§5 の 4）は、既に測られた今の値（T1-517: 損失 0 % で 8.42 MB/s、`tcp-receive-speed` 9.8〜11.1 MB/s、BUG-222）より低く、
     改善の目標にならない。
  6. 「どの 1 秒も中央値の 50 % 以上」は、host の負荷・guest の sampler の CPU で 1 秒だけ落ちる揺れでも FAIL になり、不安定な判定になる。
  7. 4 GB の QEMU の run（目標 (iv)）は ECM の約 8 MB/s で 1 向き約 9 分、両向きで 18 分以上かかり、試験の方針（1 回を長くしすぎない）に合わない。
- 修正案:
  - 停止は kernel で測る: tcpstats に「ACK が進まなかった最長の時間（ms）」「RTO の発火の数」（今の `retransmit_segments` は即時の再送も含む）を足すか、
    host で `tcpdump -w`（namespace の veth）を取って analyze で ACK の間隔を出す。
  - analyze は最初の data の行から最後の data の行までに切り、頭の 5 s の扱いを目標と同じにする。総量は行の量の和にする。
  - QEMU の up の上限は TCP の外で決める: 同じ usb-net（ECM）を付けた Linux の guest 同士の値（Debian の guest の例外は WS105・108・131・109 だけなので、
    WS220 でも使うならユーザーの許可が要る）、または UDP の送りの最大の速さ。
  - 暫定の下りの目標は今の値以上（例: 0 % で 10 MB/s 以上）にし、「秒ごとの 50 %」は百分位（例: 99 % の秒）にする。
  - wrap は test 用の config（例 `CONFIG_TCP_ISN_TEST_OFFSET`、試験の image の config.mk だけで立てる）で ISN を wrap の手前にし、64 MB の転送で
    確定的に越える。4 GB の run はしない。

## 中

### M1. §1 の「全記録」から RST・SYN の受け入れの検査と ISN が抜けている

- 根拠: ESTABLISHED 以後の RST は sequence を見ずに受けて接続を閉じる（`tcp.c:3292〜3331`、`tcp_reset_error_locked` は SYN_SENT 以外で
  常に ECONNRESET、`tcp.c:3123〜3124`）。窓の内の SYN の扱いも無い。ISN は `ticks × 1103515245 + port`（`tcp.c:2068`・`3032`）で予測できる
  （RFC 6528 の外）。どちらも速さの原因ではないが、RFC 9293 3.10.7.4・RFC 5961 の正しさで、p002 が入れる剰余の比較の macro で直せる。
- 修正案: §1 の表に足し、p002 で RST の窓の内の検査（RCV.NXT ちょうどなら閉じる、窓の内なら challenge ACK）を入れるか、別の WS・bug に記録する
  （範囲の判断は Q1・ユーザー）。

### M2. 候補 5 は「窓の右端が後ろへ縮む」こと。p007(a) の 2 つ目の案は縮みを作り、ACK と窓が不揃いに作られる

- 根拠: 窓 = min(byte の残り, 空き packet × 1460)（`tcp.c:2465〜2474`）。packet の数で決まる時に 1024 byte の segment を 1 つ受けると、
  receive_next は 1024 進み窓は 1460 減るので、**右端（RCV.NXT + RCV.WND）が 436 byte 後ろへ下がる**（RFC 1122 4.2.2.16・RFC 9293 3.8.6.2.2 の
  SHOULD NOT）。p007(a) の「窓 = 空き packet × 実際に受けた segment の大きさ」は segment の大きさが変わるたびに右端を下げる。さらに
  `tcp_output_segment` は socket の lock を持たずに `receive_next` を読み（`tcp.c:1378`）、後で窓を数える（`tcp.c:1391`、lock 無しは注記 `tcp.c:2450〜2452`）。
  その間に input の経路が segment を queue に入れると、ACK は古く窓は新しい = 右端が一時的に下がる segment が出る。`advertised_window` の書き込みも
  lock の外（`tcp.c:1392`）で、窓の更新の判断（`tcp.c:2417〜2421`）と reorder の範囲（`tcp.c:3807`）がそれを読む。
- 修正案: p007 の不変条件として「広告した右端は下げない（一度広告した範囲は queue に入れきる。足りなければ packet の上限を越えて受ける）」を書き、
  (a) は 1 つ目の案（tail の残りを埋めて次の packet へ）に決める。ACK の番号と窓は socket の lock の下で同時に決め、`advertised_window` も lock の下で書く。

### M3. NewReno の詳細と既存の規則の衝突が書かれていない

- 根拠と起こる状況:
  - RFC 6582 の `recover`（回復に入った時の最大の送った sequence）と、RTO の後に `recover` を置く規則が無いと、go-back の再送で来る重複の ACK で
    2 度目の fast retransmit が起きる。
  - 進まない ACK はどれも `retransmit_count` を 0 に戻す（`tcp.c:1557〜1559`）。fast recovery の間の重複の ACK が backoff の数と諦めの数を同時に戻す。
    p002(d) の R2 と p005 の persist の「相手が答える間は諦めない」（RFC 1122 4.2.2.17）のために、**backoff の段と諦めの数を別の変数にする**必要がある。
  - 重複の ACK の定義は「窓が同じ」を要する（`tcp.c:3465〜3471`、RFC 5681 2）。zedBSD の受け手は読み手が読むたびに窓を変えるので、zedBSD 同士では
    重複の ACK が数えられず fast retransmit が遅れる（推測。模型の lan-loss-1-zedbsd で確かめられる）。
  - 書く量が少なく 3 つの重複の ACK が来ない時のため、RFC 3042（limited transmit）を入れるか決める。
- 修正案: p005 の設計にこの 4 点を書き、模型の scenario（2 つの穴の窓、zedBSD 同士の損失）で確かめる。

### M4. 模型の妥当性の限りが直しの段で効いてくる

- 根拠: (1) 2 つの host が 1 つの pool と registry を共有する（§3.3 に記載）。p007(c) で受けの buffer を大きくすると B の queue と reorder が pool を
  2 倍の速さで食い、実際より早く尽きる。(2) `--peer-window 65535` は B（zedBSD）の窓を書き換えるだけで、B の queue の実の大きさは変わらない
  （`model.c` の rewrite_window）。p006 で A が 64 KB を出せるようになると、B は書き換えた窓の分を受けきれず拒み（候補 5）、Linux の相手には無い
  拒否が模型にだけ出る。(3) 書き手の chunk は 1024 byte（`model.c:587`）で、p006 の byte の buffer の大きな write を試さない。(4) 1 thread なので
  S4-5 の二重送信、M2 の lock の外の読みは見えない。(5) 非塞ぎの書き手（S1）、同時の両向き（data と ACK が同じ device の queue を取り合う = 候補 1 の
  引き金）、Linux の送り手（受けの向き）は模していない。
- 修正案: p006 の前に (1) pool を host ごとに分け、(2) 「Linux の相手」を「B の受けの queue を大きくして窓をそれに合わせる」に替え（書き換えをやめる）、
  (3) 書き手の chunk を引数に、(5) 同時の両向きと非塞ぎの書き手の scenario を足す。S4 の二重送信は模型に 2 つの書き手を置けば確かめられる。

### M5. 候補 6 は driver の送りの吐き出し方から量が出る（受けの向きの実機、推測を含む）

- 根拠: ECM・NCM の送りの完了の callback は印を付けて poll を予約するだけで（`usb-cdc-ecm.c:798〜824`、`usb-cdc-ncm-net.c:870〜888`）、次の frame の
  送り出しは network worker の poll の中で行う（ECM の poll の送りの部分 `usb-cdc-ecm.c:1540〜1569`）。worker は 1 回の loop で poll の後に最大
  32 個の入力を処理する（`NET_WORK_BUDGET`、`core.c:41`・`606〜619`）。受けた segment ごとに ACK を出す（`tcp.c:3582`）ので、NCM が 1 つの NTB で
  最大 32 frame を渡す時（`usb-cdc-ncm-net.c:56〜58`、BUG-222 の直し）、1 回の loop の ACK が queue 8 + 転送中 1 を越え、残りは捨てられる
  （`usb-cdc-ecm.c:1111〜1116`・NCM `1207`）。ECM（QEMU）は 1 回の poll で 1 frame しか受けないので起きにくい（推測、測っていない）。
- 起こる状況: 5330（NCM）の下りで ACK が大量に捨てられ、窓 0 の後の窓の更新を失うと相手の persist の backoff を待つ。模型でも読み手 2 MB/s・
  ACK の損失 1 % で平均 1.71 → 1.31 MB/s、最長の停止 2.0 s（下の「確かめた点」）。
- 修正案: 候補 6 の確度の根拠にこの仕組みを書き、実機の下りで `ifconfig` の TX dropped と `segment_send_failures` を必ず見る。p003(b)・p007(d)
  （受けの batch の終わりに 1 つの ACK）を下りの向きの最初の直しとして早める価値がある。窓の更新の送りの失敗だけを数える counter を足す。

### M6. p002(d) の諦めの時間はユーザーに見える変更、§1 の「23 s」は誤り

- 根拠: 今は再送 5 回の後さらに 8 s 待って切るので、最後の進みから約 31 s（1 + 2 + 4 + 8 + 8 + 8、`tcp.c:532`・`583〜588`。模型の lan-wrap は 29 s で
  ENOTCONN）。§1・候補 3 の「1+2+4+8+8 = 約 23 s」は 5 回目の再送までの時間。p002(d) の SYN の 3 分は、応えない相手への connect(2) を 3 分塞ぐ
  （Linux の既定 tcp_syn_retries = 6 は約 127 s）。
- 修正案: 数を直し、SYN の R2 は「Linux と同じ約 2 分」か「今のまま」をユーザーに示して決める。R2 は回数でなく時間で決める（RTO の最小が変わると
  回数の意味が変わる）。

### M7. pool の予算が p007(c) の前提になっているのに数が無い

- 根拠: pool は stack 全体で 256 個（`packet-buf.h:25`）、接続ごとに受け 44 + reorder 44（+ 今は送りの写し 8 と device の queue の写し）。
  window scale で受けの buffer を 64 KB より大きくするには、接続ごとの packet が増え、2 本の転送か NCM の受けの NTB（最大 32 frame）で尽きる。
  alloc と free は 256 個の線形の探索を irq を止めた lock の下で行う（`packet-buf.c:101〜125`・`183〜199`）ので、pool を大きくするとこの時間も伸びる。
- 修正案: p007(c) の設計の前に「pool を何個にするか（64-bit で 1024 個 = 2 MB など）、接続ごとの受けの上限、free list に替えるか」を決める。
  受けの queue を byte の ring に写す形（pool を持たない）も比べる。

### M8. Nagle の既定と IW10 は振る舞いの変更で、決めの記録が要る

- 根拠: p006(c) は Nagle を入れ、p007(d) は遅延 ACK（最大 40 ms）を入れる。両方が既定で有効だと、TCP_NODELAY を付けない要求・応答の app で
  「Nagle + 遅延 ACK」の 40 ms（相手が Windows なら 200 ms）の待ちが出る。今の zedBSD の app はこの遅れを経験していない（`tcp_setsockopt` の注記
  `tcp.c:837〜842` は「遅れは無い」と約束している）。IW10（RFC 6928）は Experimental で、RFC 5681 の IW は約 3 segment。
- 修正案: どちらも設計の選択として記録し（Linux と同じ既定にする理由）、TCP_NODELAY の注記と man page の扱いを p006 に入れる。

### M9. net.tcpstats の UAPI の伸ばし方

- 根拠: userland の sysctl は `element_size != sizeof(*entry)` と `version != TCP_STATS_VERSION` を拒む（`userland/base/sysctl/main.c:776〜786`）。
  p004〜p008 で欄を足す（S8 の停止の時間など）と、version を上げるか古い sysctl が読めなくなる。`fast_retransmits` は UAPI にあるが
  `struct tcp_counters` に無く常に 0。port の byte の順（host の順）が UAPI の注記に無い（address は network の順と書いてある）。
  ILP32・LP64 の大きさの一致は `_Static_assert`（`sysctl.h` の末尾）で確かめた: header 48、entry 312、u64 の前の offset 136 は 8 の倍数で i386 の
  4 byte 整列でも同じ。
- 修正案: 読み手は `element_size >= sizeof` と `version >= 1` を受け、知らない後ろの欄を無視する、と UAPI の注記に書き、末尾に足す規則にする。
  port の順を注記に書く。

## 軽微

- L1. §1「1024 は 536 未満の相手では超える」: RFC 9293 3.7.1 / RFC 1122 4.2.2.6 は、MSS option が無い相手には 536（IPv6 は 1220）を仮定すると定める。
  問題は「536 未満の相手」でなく「option を送らない相手」。
- L2. `data_segments_out`・`data_bytes_out` は ring に積んだ時に数え（`tcp.c:1720〜1723`）、取り戻した後の書き手のやり直しでもう一度数える。
  「最初の送り」の意味と合わない（up の量は `send_unacknowledged` で測っているので analyze は正しい）。
- L3. 実機の down では、`fetch` を走らせる SSH の接続も `ss -tin dst MACHINE` に出る。analyze の host の側は「ss の最初の socket」を取る
  （`analyze.py:62〜85`）ので、その SSH の接続の cwnd を読むことがある。port 8080 で絞る。
- L4. `tcp_stats_copy` は registry の lock（irq を止めた spin lock）の下で全 socket を写す（`tcp.c:706〜748`）。socket が千を越えると irq を止める
  時間が伸びる（`tcp_timer_next_deadline` と同じ形なので今は許容）。1 MB の出力の上限（`syscall.c:102`）で約 3360 socket を越えると ENOMEM が続く。
- L5. `plan/ws221/ws.md:18` の driver の path（`src/drivers/ethernet/usb-cdc-ecm.c`）は実際は `src/drivers/usb/usb-cdc-ecm.c`（Q1 への情報）。
- L6. pcat の link の失敗（`drv_xhci_isoch_*` の未定義）は本 Phase と無関係だが、bug として記録されているかを Q1 が確かめる。
- L7. GENET は送りの ring 256（`rpi4-ethernet.c:83`、`1160〜1164`）で、候補 1（device の queue 9）は USB の adapter に限る。§2 の表の候補 1 に
  「Pi 4 の GENET では起きにくい」と書くと、実機の結果の読みが楽になる。

## p002 の開始の条件

1. p002 の範囲に、sequence の比較を `tcp.c` の全体で剰余の比較に替えること（ACK の受理 `tcp.c:3437`、ACK の窓 `tcp.c:3451〜3454` を受理の内に入れる、
   `> send_next` の ACK への ACK）を明記し、M1 の RST の検査を入れるか別に記録するかを決める。
2. S2（sequence の露出）を p002 と一緒に直すか、別の小さな段にするかを決める（既に main にあるので早い方がよい）。
3. M6 の SYN の R2 の値をユーザーに示す（決まるまでは data の R2 だけを直してよい）。
4. 確かめは模型の lan-wrap（全部届く、`acks_outside` 0）、重なりの scenario、既存の scenario の非退行。build は amd64・rpi4 の vmunix の warning 0。
5. T1 の依頼 A は S1（塞がる書き手、disk への down、ユーザーへの再現の条件の確認）・S3（offload を切る）・S8（停止の測り、analyze の頭と尻）を
   直した後に流す。p002 の実行はその間に並べてよい。

## 確かめた点

- §1・§2 の file:行は全部、該当の code に当たることを確かめた（例: `tcp.c:54`・`110`・`125`・`126`・`532`・`562`・`580〜590`・`1557〜1580`・
  `1681〜1700`・`1745〜1770`・`2244`・`2258`・`2328`・`2456〜2480`・`3437`・`3454`・`3479`・`3510`・`3810`、`tcp-socket.h:37`・`53`、
  `socket.c:687`、`usb-cdc-ecm.c:54`・`823`、`usb-cdc-ncm-net.c:69`・`887`、`core.c:602`、`packet-buf.h:25`）。
- 符号無しの ACK の比較（`tcp.c:3437`）で、送りの sequence が 2^32 を跨いでいる間の ACK が全部捨てられ、`tcp_retire_acknowledged` が呼ばれないので
  `retransmit_count` も戻らず、約 30 s で切れることを code で確かめた。`tcp_retire_acknowledged`（`tcp.c:1538`）と受けの側（`tcp.c:3512`・`3824`・`3890`）は
  符号つきの比較で正しい。
- 受けの窓の「1 packet = 1460」の約束と tail への継ぎ足しの条件（`socket.c:705〜721`）: 1024 byte の segment は tail の残り（約 900 byte）に入らず
  1 packet を取る。1460 byte の segment は 1 packet に 1 つで約束どおり。
- sysctl の出力は kernel の buffer（`syscall.c:1043〜1048` の `kern_malloc`）に書くので、spin lock の下の memcpy で fault は起きない。lock の順は
  registry → socket で、`tcp_timer_next_deadline` と同じ。`tcp_send_segment_at` の新しい lock は、全ての呼び手が socket の lock を持たないことを
  確かめた（`tcp.c:524`・`596`・`1457`・`1732`・`2427`・`3367`・`3404`・`3529`・`3548`・`3582`・`3615`・`3619`）。
- UAPI の大きさ（48・312）と ILP32 での整列（上の M9）。
- 起きた thread は同じか高い優先度なら走っている thread を奪う（`sched.c:2027〜2034`）ので、CPU を使い切る user の process が network worker を
  10 ms の quantum だけ待たせる、という仮説は成り立たないと判断した。
- 模型を自分の build の directory で流した: `OUT=build/ws220-review sh plan/ws220/tests/tcp-model/run.sh`（約 2 分以内）。§3.3 の表の 9 つの数字と
  同じ結果（lan-clean 37.28、rtt-10ms 0.78、lan-loss-0.1 1.04、lan-loss-1 0.10、rtt-10ms-loss-0.1 0.39、lan-loss-1-zedbsd 6.31、lan-ackloss-1 37.18、
  lan-wrap 0.14・4194304 byte で止まり 29 s、slow-reader 1.71、`tcp-model: FAIL`）。追加: `--reader-kbps 16000 --loss-ba 1` で 1.31 MB/s・最長の停止
  1978 ms（M5 の根拠）、`--reader-kbps 16000 --queue 3` で 0.29 MB/s。
- 作った物: `build/ws220-review/`（模型の object と binary）。消すのは Q1。

## 確かめていない点

- QEMU と実機での実行、guest の `sysctl net.tcpstats` の表示（どれも未実施）。
- zedBSD の sshd が実際に O_NONBLOCK で書くこと（S1、OpenSSH の既定からの推測）。
- この host の veth・bridge・tap の offload の状態と、netem が GSO の skb を丸ごと落とすこと（S3、Linux の既定からの推測）。
- Linux・FreeBSD・Windows の RTO と遅延 ACK の値（S6・M8、記憶による）。RFC の節の番号も記憶による照合で、本文は開いていない。
- NCM の下りで ACK が実際に捨てられる量（M5、code からの推測）。
- `ipv4_output` の ARP の解決中の扱い（送りの失敗として返るか、黙って捨てるか）は読んでいない。
- `inet-socket.c` は TCP に関わる所（socket の作成、ifconfig の `ifi_oqdrops`）だけを見た。
