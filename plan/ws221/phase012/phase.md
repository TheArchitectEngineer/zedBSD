<!-- awesome-plan project=zedbsd record=ws221-p012 -->
# ws221-p012: TCP の SACK（RFC 2018・2883・6675）

Status: in-progress（2026-10-29 P3 q1679-i02: 設計を書いた、受け側から実装）
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
- 32 bit の板（ring 8・reorder 16）も同じ（code は 1 つ）。

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

## 進め方

1. 交渉 ＋ 受け側の block ＋ D-SACK（§1〜§3）→ host と 10.0.10.3 の recv（ATR 有効）。
2. 送り側（§4）→ tcp-model の損失の scenario、10.0.10.3 の send、5330。

## 結果

（未）
