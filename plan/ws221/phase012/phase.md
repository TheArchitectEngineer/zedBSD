<!-- awesome-plan project=zedbsd record=ws221-p012 -->
# ws221-p012: TCP の SACK（RFC 2018・2883・6675）

Status: cleared（2026-10-30 P3 q1679-i02: 受け側・D-SACK・ACK 8 segment ごと（bbc43c185）、送り側（fe31561e8）、tcp-model・QEMU qemu-tcp inet,inet6・5330 の非退行 PASS、10GbE の recv 4897〜5282（Linux 5458 の 90〜97 %、ATR 有効）、規約の全文の見直し済み。RACK-TLP は Future Work の候補）
Parent: [WS221](../ws.md)
Queue: q1679 / q1679-i02（P3）
依存: p008〜p011（送りの ring・NewReno の回復・受けの並べ替えの queue）、WS311（CUBIC）

## 由来

- ユーザー（原文、2026-10-29）:「P3は、SACKの実装を優先してください。ATRを有効にした評価で進めます。」
- p010 の所見（[phase010](../phase010/phase.md) の i02）: 10.0.10.3 の recv は centris の ixgbe の ATR が有効の時 2875〜3729 Mbit/s、ATR を止めると 4819〜5286。ATR は zedBSD の ACK（と相手の data）の順を入れ替え、SACK の無い相手（Linux）は入れ替わりと損失を見分けられず偽の fast retransmit で cwnd を半分にする。Linux は SACK があれば入れ替わりを測って閾値を上げ（tp->reordering）、D-SACK で偽の再送を取り消す（TCPDSACKUndo）。
- Future Work の F-WS311-sack（SACK と RFC 6675 の回復、PRR か RFC 6582 §3.2 の (1)、HyStart++）のうち SACK と回復をここで行う。PRR・HyStart++ は範囲の外。

## 範囲（設計）

### 1. 交渉（RFC 2018 §2）

- 能動の open の SYN は常に SACK-Permitted（kind 4、長さ 2）を付ける。受け身の SYN-ACK は相手の SYN が付けた時だけ付ける。両方が付けた時 `sack_on`。
- SYN の option: MSS（4）、NOP NOP SACK-Permitted（4）、NOP Window Scale（4）。window scale と同じく SYN を読む所（能動は SYN-ACK、受け身は `tcp_passive_syn`）で決める。
- 32 bit の板（ring 8、並べ替えの queue の実効は `TCP_REORDER_LIMIT` 4: pool が 256 未満）も同じ（code は 1 つ）。

### 2. 受け側の SACK block（RFC 2018 §3・§4）

- `sack_on` の間、並べ替えの queue（`reorder[]`、sequence の順）に segment がある時、data の無い segment（純粋な ACK・window update・重複の ACK）は SACK option（NOP NOP、kind 5、長さ 2 + 8n）を運ぶ。timestamp を使わないので n は最大 4（36 byte）。
- block は queue の連続した範囲をまとめた物。1 つ目は最後に queue に入った segment を含む block（RFC 2018 §4 の「最も新しい」）、残りは列の後ろ（大きい sequence）から順に。
- data を運ぶ segment には付けない（payload は MSS で切られ、option を足すと MTU を超える。RFC 2018 の SHOULD からの逸れ、双方向の大量の data でだけ効く）。
- 受けの window の外の segment・queue の満杯で捨てた segment は block に入らない（持っていない物は言わない）。

### 3. D-SACK（RFC 2883）

- receive_next より前に収まる segment（`old_payload`）への ACK は、その範囲を 1 つ目の block（D-SACK）として運ぶ。2 つ目以降は §2 の block。並べ替えの queue の同じ始まりの segment の重複も、その範囲を D-SACK にする。
- 1 つの D-SACK は 1 回の ACK だけ（次の ACK は運ばない）。

### 4. 送り側（RFC 6675 の保守的な形）

- 相手の ACK の SACK block（`sack_on` の時だけ読む、send_unacknowledged と送った端の間に収まる物だけ）を送りの ring の各 entry の `sacked` に記す（entry が block に全部入る時だけ）。累積の ACK で ring から外れた entry は印ごと消える。
- 損失の判定（IsLost、§4）: ある entry より上に SACK された byte が DupThresh（3）× MSS 以上ある時、その entry は失われた。回復の始まり（§5 の (4)）は今の 3 つの dupack か、最古の entry が IsLost になった時。cwnd と ssthresh の下げ方は今の Reno・CUBIC の loss の口（`tcp_cwnd_loss_locked`）のまま。
- 回復の間の再送（NextSeg の rule 1）: SACK されていない、IsLost の、この回復でまだ再送していない最も古い entry。pipe（§4 SetPipe: 送って未確認の byte − SACK された byte − 失われて再送していない byte ＋ 再送した byte）が cwnd より小さい間、ACK ごとに送る。rule 2（新しい data）は今の送りの経路に任せる。部分の ACK の扱い（RFC 6582）は SACK の無い接続でそのまま。
- timeout（§5.1）: SACK の印を全部消す（reneging への備え、RFC 2018 §8）。go-back-N の今の形を保つ。
- 回復の終わり: recover に届く累積の ACK（今の形）。

### 5. 数え

- net.tcpstats の UAPI（WS220 の layout）は変えない。確かめは相手の Linux の TcpExt（TCPSACKRecovery・TCPSACKReorder・TCPDSACKUndo・TCPFastRetrans・TCPSackRecoveryFail）、centris の tcpdump（`ack-order.py` に SACK block の数え）、tcp-model の両端（zedBSD 同士）で。

### 6. 確かめ

- host: tcp-model（WS220 の全 scenario PASS、損失の scenario で SACK の有無を比べる）、option の組み立てと読みの host 試験。
- build: amd64・pcat・rpi4・rpi5・zybo・RTL8127 の config の vmunix の warning 0、style-check の件数を増やさない。
- 実機: 5330（RTL8156、相手 centris）の非退行（VF2 は 2026-10-29 から使わない）、10.0.10.3 の RTL8127 の recv・send を ATR 有効で、Linux（WS235 の send 6808・recv 5458）と比べる。
- QEMU の `qemu-tcp.sh "" 15 inet,inet6`（相手は QEMU の slirp、SACK を使うかは slirp 次第）。

## review-1（design-reviewer、2026-10-29、致命 0・重大 7・中 10・軽 9）と扱い

| 指摘 | 要旨 | 扱い |
| --- | --- | --- |
| S1 | 送り側の IsLost だけで回復を始めると、ATR の入れ替わりで偽の回復が今より増える（D-SACK の undo も閾値の適応も無い） | 回復の始まりは今の 3 つの dupack のまま（IsLost だけでは始めない）。D-SACK による undo（RFC 3708・4015）と DupThresh の適応（RFC 4653・8985）は範囲の外（Q1 の判断 2026-10-30: send は Linux の 92 %・再送 0 で、送り側は IsLost だけで回復を始めないので偽の回復は増えない） |
| S2 | 穴の途中の entry を再送する経路が無い（今の即時の再送は最古だけを timer の経路で） | 送り側の設計に足す: 入力の処理で lock の中で entry を選び、その copy の packet の参照を取って lock を外し、`tcp_send_segment_cut` で送る（参照があるので累積の ACK が先に ring から外しても use-after-free にならない）。拒まれたら印を戻す。1 つの ACK で送る数に上限。Karn: 再送の後は timing を止める。`retransmit_segments` に数える |
| S3 | 「この回復で再送した」の印が無く、部分の ACK の再送と二重になる。末尾の損失は RTO 待ち | entry に `retransmitted` を足し、全ての再送が立てる。部分の ACK の即時の再送は最古が未再送の時だけ。NextSeg の rule 3 は入れる、rule 4（rescue）は入れない（理由: 末尾の損失は今の部分の ACK の再送と RTO で拾う、ATR の入れ替わりで偽の再送を増やさない） |
| S4 | 同じ始まりの重複への D-SACK が持っていない byte を申告する | 直した: D-SACK の長さを持っている長さとの小さい方に |
| S5 | ACK の番号より下の SACK block（偽の D-SACK）を出す競合 | 直した: block と receive_next を同じ lock の保持で読み、ACK はその値、ACK 以下の block は捨て、跨ぐ物は切る（`tcp_sack_block_add`） |
| S6 | 受け側の reneging（満杯の queue が SACK 済みを捨てる） | 直した: `sack_on` の時は queue の物を追い出さず新しい方を捨てる。送り側は累積の ACK が SACK 済みの entry の始まりで止まった時を reneging とみて印を消す（送り側で） |
| S7 | rewound の entry が SACK の範囲から外れる、timeout の回復と NextSeg が二重 | 送り側で: SACK を読む範囲を [SND.UNA, send_max]、NextSeg は fast recovery の時だけ（timeout の回復では動かさない） |
| M1 | pipe の式の二重の数え | 送り側で RFC 6675 の SetPipe のとおりに（SACK されていない byte ごとに、IsLost でなければ 1、再送していれば 1） |
| M2 | SACK のある接続の dupack は「新しく SACK する ACK」 | 送り側で採る |
| M3 | IsLost による開始が dupack_guard を通らない | S1 で IsLost だけの開始をしないので起きない。NextSeg も recovery の中だけ |
| M4 | 純粋な ACK ごとに socket の lock | 直した: queue が空で D-SACK も無い時は lock を取らない |
| M5 | D-SACK の 2 つ目の block を包む block に | 直した: queue の中の重複は sack_recent をその segment にし、包む block が 2 つ目になる |
| M6 | D-SACK が送られずに消える | 一部直した: SACK block のある ACK は news_only でも送る。device・pool の拒否で失われる物はそのまま（次の重複でまた付く） |
| M7 | scoreboard の計算量 | 送り側で: SACK option のある ACK だけ、上から 1 回の走査で上の SACK 済みの byte を累計（O(n)） |
| M8 | 印の初期化 | 送り側で: 新しい entry で 0、`tcp_retransmit_reset`・RTO で消す |
| M9 | 試験が誤りを見つけられない（tcp-model に入れ替わりの口が無く WS220 の directory） | tcp-model を直す・plan/tools へ移すのは Q1 に諮る。pcap で「D-SACK 以外の block が ACK より上」「D-SACK が受けた範囲の中」を数える。相手の TcpExt に TCPSACKReneging・TCPSACKDiscard・TCPDSACKIgnoredOld・TCPDSACKIgnoredNoUndo・TCPSpuriousRTOs を足して見る |
| M10 | 受けの改善の理屈が未確かめ | 確かめた: 受け側の SACK だけでは recv は 2946〜3125（相手の再送は 0 に）。recv を上げたのは ACK の数を減らす事（下の「測り」） |
| L1〜L9 | IsLost の閾値の書き方、overlap の D-SACK（入れた）、block の順（RFC 2018 §4 の繰り返しとの違い）、順の外の FIN、送りの FIN の entry の判定、受けた block の modulo の比べと捨て方、`tcp_pending` の大きさ、SYN の option（問題無し）、32 bit の数字（直した） | 送り側の実装で扱う。L3 の block の順は今の形（最近、大きい sequence から）を保つ |

## 進め方

1. 交渉 ＋ 受け側の block ＋ D-SACK（§1〜§3）→ host と 10.0.10.3 の recv（ATR 有効）。
2. 送り側（§4）→ tcp-model の損失の scenario、10.0.10.3 の send、5330。

## 結果

### 段 1: 交渉・受け側の block・D-SACK、ACK の間隔（2026-10-29〜30、ATR 有効、10.0.10.3 の RTL8127 ← centris、20 s）

| kernel | recv Mbit/s | send | 相手の RetransSegs |
| --- | --- | --- | --- |
| p010 の終わり（354116a79 の形、SACK 無し、ACK は 2 segment ごと） | 2875〜3729（i02 の 18 回） | 6159〜6280 | 0〜903 |
| 受け側の SACK だけ（ACK 2） | 3125・3114・2946 | 6261 | 0・0・0 |
| SACK ＋ ACK 4 segment ごと | 4637・4703 | 6252 | 0 |
| SACK ＋ ACK 8 segment ごと | 5282・5100、review の直しの後 5007・4897 | 6286・6281 | 0 |
| SACK ＋ ACK 16 segment ごと | 4714・4817 | 5760 | 0 |
| Linux（同じ機械、WS235） | 5458 | 6808 | — |

- 受け側の SACK で相手の偽の再送は 0 になったが、recv は上がらなかった（review の M10 の確かめ）。recv を上げたのは ACK を減らす事: 2 segment ごとの ACK（1 秒に約 20 万）は、ATR が受けの CPU を散らす相手の送りの CPU を食っていた。Linux の受けは GRO で segment をまとめて ACK するので、もっと疎。
- 決め（P3）: 64 bit の機械（pool 1024 以上）は 8 segment ごと、32 bit の板は 2 のまま（`CONFIG_TCP_ACK_SEGMENTS`）。どちらも network worker の batch の終わりの flush で残りを ACK するので、止まった flight は待たない。RFC 5681 §4.2 の「2 segment ごと」からの逸れ（GRO・LRO の受けと同じ形）。
- 10GbE の recv は Linux の 90〜97 %（4897〜5282）、send は 92 %（6281〜6286）。
- 非退行: 5330（RTL8156、CDC NCM、相手 centris、30 s、`NO_VF2=1 5330-ncm.sh`）send 272.0、recv **1224.8**（前 555〜696: ACK が減って NCM の 1 本の転送の取り合いが減った）、RX errors 0、ping 1472 通る（mtu_max − 28 の 1476 は centris の MTU 1500 を超えるので通らない、想定どおり）。
- host: tcp-model PASS（scenario の値は前と同じ）、amd64・pcat・rpi4・rpi5・zybo・RTL8127 の config の vmunix の build warning 0、style-check tcp.c 113 → 111。

### 段 2: 送り側（RFC 6675 の保守的な形、2026-10-30）

実装（review-1 の扱いのとおり）:
- `tcp_pending` に `sacked`・`resent`・`resend_order`、接続に `resend_serial`。新しい entry で 0。
- `tcp_sack_take_locked`: SACK のある接続で、ACK の SACK block（modulo 2^32、左端 ≥ 右端は捨て、[SND.UNA, send_max] に切る）が全部を含む送った entry に `sacked`。新しく印を付けた数を返す（option が無ければ −1）。
- dupack（M2）: SACK の option があって新しく印が付かない ACK は dupack に数えない（D-SACK だけの ACK）。回復の始まりは今の 3 つの dupack（S1）。
- `tcp_sack_resend`: fast recovery の中（timeout の回復でない）で、ACK ごとに最大 4 つ、最古を除く「SACK されていない・この回復で未再送・上に (DupThresh − 1) × MSS を超える SACK 済みの byte」の最も低い entry を、pipe（SetPipe: SACK されていない byte を、IsLost でなければ 1、再送していれば 1）が cwnd に収まる間だけ再送する。copy の packet の参照を取って lock を外して送る。拒まれたら印を戻す。Karn で timing を止める。
- 最古: fast retransmit と部分の ACK が timer の経路で送る（印 `resent`）。部分の ACK は最古がこの回復で未再送の時だけ即時に再送する（S3）。毎回再送する形（前の NewReno と同じ）は lan-loss-1 の 24 seed の平均 47.4 MB/s と良かったが、SACK の経路で送った穴を重ねて送り、WS220 の「再送 ≤ 損失」の check を破った（lan-loss-1 66 > 64、window-lost 12 > 8）ので採らない。再送した最古の待ちを 1.25 RTT にする形・再送の順で判断する形も試したが平均は変わらなかった（40.1）。
- 失われた再送の検出（Linux の lost retransmit と同じ考え方）: 後に再送した segment が SACK されて前に再送した物がされていなければ、前の再送も失われた: 印を戻し、最古なら即時に再送。
- reneging（S6）: 累積の ACK が SACK 済みの entry の始まりで止まったら全ての印を消す。timeout（§5.1）でも消す。回復の終わりと fast retransmit の始まりで `resent` を消す。
- 範囲の外: NextSeg の rule 2〜4（新しい data は今の送りの経路、rescue は入れない）、PRR。

tcp-model（host、zedBSD 同士、bbc43c185（段 1）→ 段 2）:

| scenario | 前 MB/s | 後 MB/s |
| --- | --- | --- |
| two-holes | 90.44 | 99.43 |
| two-holes-zedbsd | 90.44 | 99.43 |
| window-lost | 58.89 | 91.06 |
| reno-bottleneck | 5.67 | 7.42 |
| reno-two-flows | 3.98 | 4.36 |
| lan-loss-1（seed 1） | 24.79 | 24.01（seed ごとの比べは下の注） |
| lan-loss-1-zedbsd | 24.19 | 25.40 |
| queue-1-loss-1 | 32.80 | 34.06 |
| rtt-step-zedbsd | srtt ≥ 500 ms で PASS | srtt 494 ms で前の check の外: D-SACK だけの ACK を dupack に数えないので偽の fast retransmit が無くなり（fast 1 → 0）、SRTT が 600 ms に近づく前に終わる（9.30 s、19 sample、330 → 494 ms と上がり続けていた）。Q1 の許可で bound を 450000:700000 に |

- lan-loss-1 の比べ: model の損失の乱数は送りの順で決まるので、版が違うと同じ seed でも損失の並びが違う（seed ごとの対は比べられない）。seed 1〜24 の平均（`--loss-ab 1 --peer-window 65535`）: 前（bbc43c185）40.42 MB/s・timeout 計 16 回、後（この形）40.08 MB/s・20 回。1 % の散った損失では SACK の得は無く（NewReno でも 1 つの穴は 1 RTT で直る）、timeout が少し増えた。残りの timeout は失われた再送と flight の末尾の損失で、RACK-TLP（RFC 8985）の timer が要る（範囲の外、Future Work の候補）。
- 試した形（seed 1〜24 の平均・timeout）: 部分の ACK で毎回再送 47.44・16（check を破る）、同じで D-SACK だけの ACK も dupack に数える 49.46・14（check を破る）、未再送の時だけ（採った形）40.08・20、それに 1.25 RTT の待ち 40.09・20、再送の順の比べ 40.08・20。
- 確かめ（採った形）: tcp-model 全 scenario PASS（rtt-step-zedbsd の bound を 450000:700000 に、Q1 の許可、`plan/ws220/tests/tcp-model/run.sh` に理由の comment）、amd64・pcat・rpi4・rpi5・zybo・RTL8127 の config の vmunix の build warning 0、style-check tcp.c 111（bbc43c185 と同じ）。QEMU の qemu-tcp inet,inet6 PASS（送り側の途中の版で、guest send/recv inet 46.6/45.2・inet6 40.8/41.6 Mbit/s）。実機（10.0.10.3）の送りは損失が無く SACK の送り側は働かない（測らない、Q1）。

## 規約の全文の見直し（2026-10-30、conformance-reviewer、p010 i02・p012・p013 の hunk）

- 対象: 6a178f697 からの tcp.c・socket.c・socket.h・tcp-socket.h・usb-cdc-ncm.c・usb-cdc-ncm-net.c・usb-cdc-ncm.h・ncm-ntb-frames-test.c の hunk。書き方だけ（直接の `return f()`、条件の中の bool 式と 3 つ以上の clause、段落の comment と空行、臨界区間の空行、試験の C の前方宣言・入れ子の宣言・comment）。動作は変えていない（P3 が diff の comment 以外の行を読んで同じ意味を確かめた）。
- style-check（6a178f697 → 見直しの後）: tcp.c 115 → 113、socket.c 27 → 27、usb-cdc-ncm.c 31 → 31、usb-cdc-ncm-net.c 83 → 81、ncm-ntb-frames-test.c 10（見直しの前）→ 0。hunk の外の既存の指摘（`ws220-p005` などの comment、`ranges_overlap` の条件の中の呼び出し、`ncm_binding_parse` の `?:`）は範囲の外。
- 確かめ: amd64・zybo・5330 の config の vmunix の build warning 0、`run-ncm-ntb-frames-test.sh` PASS、tcp-model PASS、`git diff --check` 無し。p014 の HCD の変更の hunk は、その実装の後に短く見直す。
