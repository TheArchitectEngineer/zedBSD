<!-- awesome-plan project=zedbsd record=ws312-p001 -->

# ws312-p001 調べと設計（IPv6 の fragment の受け取りと再組立て）

- 親: [WS312](../ws.md)。Queue: q1672 / q1672-i01（担当 P6、2026-10-29）。
- Status: in-progress（設計の版 1、design-reviewer の前）
- 出典: 2026-10-29 ユーザー「IPv6のfragment対応は、今P6を立ててやっていいです。」

## 目的・受け入れ

受けた IPv6 の fragment を再組立てして上の層に渡す設計を書き、design-reviewer の敵対的レビューを受けて反映する。範囲は受けだけ
（送りの fragment・IPv4 の再組立ては範囲の外）。

## 1. 今の code（基点 e4b28311e）

- `src/kern/net/ipv6.c` の `ipv6_input` が header・宛先を確かめ、`packet_buf_trim` で frame の詰め物を切り、`l3_offset`・`l3_length` を置いて
  `ipv6_extensions(packet, &next_header, &offset, &field)` で extension header を歩く。fragment header（44）に当たると packet を捨てる（`EINVAL`）。
  `field` はその header を名指した byte の位置（IPv6 header なら 6、extension なら その header の先頭）。
- `icmp6_error(packet, type, code, data)` は `packet->storage + l3_offset` の IPv6 header と `l3_length` を引用し、packet を消費する
  （秒あたりの数の上限あり。宛先が group の時・ICMPv6 の error への error は出さない）。
- 時間: network の worker が `ipv6_timer_run()`（中で `nd6_timer_run(now)`）と `ipv6_timer_next_deadline()`（ms を集めて ticks で返す）を呼ぶ。
  device があれば 1 秒ごとに carrier を見るので、timer は 1 秒の粒度で回る。
- packet の pool（`include/kern/net/packet-buf.h`）: 64 bit の機械は標準 2048 byte × 1024 と jumbo 9216 byte × 32（8 は driver の受けの予約）、
  32 bit の board は標準 32 だけ。packet は storage が一続きで、上の層（UDP・ICMPv6・TCP）は `packet->data` の一続きの byte を読む。
  driver は受けた frame の長さで packet を取る（1500 の frame は標準の packet）。
- UDP（`udp6_input`）は `udp_length <= packet->length` と pseudo header の checksum を見る。ICMPv6 の echo は要求の長さの packet を取り直して返す。

## 2. 方針

| 項目 | 決め |
| --- | --- |
| 置き場所 | 新しい `src/kern/net/ipv6-frag.c`（再組立ての全部）。`ipv6.c` は fragment の所だけ変える（§6）。宣言は `src/kern/net/ipv6.h`。Makefile の kernel の source の一覧に 1 行。HAL・`include/` は変えない |
| 同じ datagram の鍵 | 送り元・宛先・Identification（RFC 8200 §4.5）と、受けた device（判断 D2） |
| 持ち方 | 受けた fragment の packet をそのまま持つ（`packet->data` を fragment の data に進めて、IPv6 header は storage に残す）。揃った時に 1 つの packet へ写す。datagram ごとの記述子の配列（offset・長さ・packet）を offset の順に保つ |
| 再組立ての大きさ | 1 つの packet に収まる所まで: `PACKET_BUF_MAX_STORAGE_SIZE - PACKET_BUF_DEFAULT_HEADROOM`（64 bit 9152 byte、32 bit 1984 byte の IPv6 packet の全体）。RFC 8200 §5 の 1500 は満たす（判断 D1） |
| 時間切れ | 最初に着いた fragment から 60 秒。offset 0 の fragment を受けていれば、それを引用して ICMPv6 Time Exceeded code 1 |
| 重なり | RFC 5722: 重なりを見たら datagram の全部を捨て、その後に来る同じ鍵の fragment も黙って捨てる（「死んだ」記録を時間切れまで残す）。ICMP は出さない。全く同じ offset・長さ・中身の重複だけはその 1 つを捨てて続ける（RFC 8200 §4.5 の「may」） |
| atomic fragment | offset 0・M=0 は RFC 6946 どおり他と切り離して、fragment header を飛ばしてそのまま上へ（記録を引かない・作らない） |
| 資源の上限 | 組立て中の datagram の数、全体で持つ fragment の数、1 datagram の fragment の数（§5）。溢れたら古い物から捨てる |
| 入れ子 | 再組立て・atomic の後の walk で 2 つ目の fragment header に当たったら捨てる |
| ND | fragment で来た（再組立て・atomic の）Neighbor Discovery の message（type 133〜137）は捨てる（RFC 6980 §5） |

## 3. 受けの手順（`ipv6_fragment_input`）

入口: `ipv6_extensions` が fragment header に当たった時。packet の `data` は IPv6 header、`offset` は fragment header の位置、`field` はそれを名指した byte。

1. fragment header（8 byte）が packet に収まらなければ捨てる。fragment の data の長さ `length = packet->length - (offset + 8)`、
   `fragment_offset = (offset_flags & 0xfff8)`、`more = offset_flags & 1`、`identification`。予約の bit は見ない。
2. **atomic**（`fragment_offset == 0 && !more`）: `next_header` を fragment header の next header に、`offset += 8` にして 0 を返す（packet は同じ）。
3. **長さの検査**（RFC 8200 §4.5）:
   - `more` で `length % 8 != 0`: Parameter Problem code 0、pointer 4（Payload Length の field）で packet を消費。
   - `(offset - 40) + fragment_offset + length > 65535`: Parameter Problem code 0、pointer `offset + 2`（Fragment Offset の field）。
   - `more` で `length == 0`: 黙って捨てる。
4. **最初の fragment の header の鎖**（RFC 7112 §5、`fragment_offset == 0` の時）: fragment の data の中で、fragment header の next header から
   extension header（0・43・60）の長さを辿り、上の層の header に着くまでが data に収まり、上の層が UDP なら 8 byte、TCP なら 20 byte、ICMPv6 なら 4 byte が
   収まる事。収まらなければ Parameter Problem code 3（新しい定数 `ICMP6_PARAMETER_FIRST_FRAGMENT`）、pointer 0。鎖の中の 44 は入れ子として黙って捨てる。
5. **記録を引く**（lock の中）: 鍵の一致する記録。無ければ作る（空きが無ければ §5 の追い出し）。死んだ記録なら黙って捨てる。
6. **大きさの上限**: `fragment_offset + length` と、offset 0 の fragment が分かっていればその unfragmentable part の長さ（無ければ 40）の和が
   §2 の大きさを超えたら、記録を死なせて（持っていた fragment を放し）捨てる。
7. **最後の fragment との矛盾**: M=0 の fragment は終わり `end = fragment_offset + length` を決める。既に終わりが分かっていて違う、
   または持つ fragment の終わりが `end` を超える時、M=1 の fragment の終わりが分かっている終わりを超える時は、記録を死なせる（重なりと同じ扱い）。
8. **重なり**: offset の順の配列で、前の fragment の終わりが新しい物の始まりを超える、または新しい物の終わりが次の始まりを超えたら、
   同じ offset・長さ・中身（`kern_memcmp`）ならその 1 つを捨てて終わる。それ以外は記録を死なせる。
9. **持つ**: 1 datagram の fragment の数と全体の数の上限を確かめ（§5）、配列に挿す。`packet->data` を fragment の data へ進める
   （`packet_buf_pull(offset + 8)`）。`l3_offset`・`l3_length` は fragment の packet の全体のまま（時間切れの引用のため）。
   offset 0 の物なら、その fragment header の位置（unfragmentable part の長さ）・next header・「ICMPv6 の error message か」（next header が 58 で data の先頭の byte < 128）を記録に残す。
10. **揃ったか**: 終わりが分かり、offset 0 があり、持つ data の和が終わりに等しい（重なりが無いので、和が等しければ隙間も無い）。揃わなければ「持った」を返す（packet は消費済み）。
11. **組み立て**（lock の外）: 記録から記述子を取り出して記録を空ける。`packet_buf_alloc_for(PACKET_BUF_DEFAULT_HEADROOM, unfragmentable + end, 0, &error)`
    （jumbo の受けの予約は取らない）。offset 0 の packet の IPv6 header から fragment header の前までを写し、Payload Length を `unfragmentable - 40 + end` に、
    `field` の byte を offset 0 の fragment header の next header に書き換える。各 fragment の data を offset の位置に写す。
    `device` は offset 0 の物を参照を取って、`protocol`・`flags`・`l2_offset` も写す。`l3_offset`・`l3_length` を新しい packet の物にする。
    fragment の packet を全部放す。取れなければ捨てる。
12. 返り: 新しい packet を `*packet` に、`next_header` を offset 0 の fragment header の next header、`offset` を unfragmentable の長さにして 0。
    `field` はそのまま。walk はそこから続く（hop-by-hop は先頭でないので Parameter Problem、2 つ目の 44 は §2 の入れ子で捨てる）。

RFC 8200 §4.5 の「fragment ごとに違ってよい物」: fragment header より前の header はそれぞれの fragment で処理済み（walk が先に見る）、
再組立てに残すのは offset 0 の物の header と next header だけ。

## 4. 時間切れ（`ipv6_fragment_timer_run(now_ms)`・`ipv6_fragment_deadline()`）

- `ipv6_timer_run` の最後（`nd6_timer_run(now)` の隣）で呼ぶ。lock の中で、`expire_ms <= now` の記録を空け、持つ fragment を手元の list へ。
  生きた記録で offset 0 があり、それが ICMPv6 の error message でなければ、offset 0 の packet を Time Exceeded の list へ。
- lock の外で、Time Exceeded の packet ごとに `icmp6_error(packet, ICMP6_TIME_EXCEEDED, 1, 0)`（`data` を引用の前に戻す必要は無い。
  icmp6_error は `storage + l3_offset` から `l3_length` を引用する）、他は `packet_buf_free`。
- `ipv6_fragment_deadline()` は使われている記録の一番早い `expire_ms`（無ければ 0）。`ipv6_timer_next_deadline` の ms の候補に加える。
- 死んだ記録も同じ時間で空く（ICMP は出さない）。

## 5. 資源の上限

| 定数 | 64 bit（jumbo あり） | 32 bit | 意味 |
| --- | --- | --- | --- |
| `IPV6_FRAGMENT_DATAGRAMS` | 16 | 2 | 組立て中（と死んだ）記録の数 |
| `IPV6_FRAGMENT_PIECES` | 64 | 4 | 1 datagram で持つ fragment の数 |
| `IPV6_FRAGMENT_HELD` | 128 | 4 | 全体で持つ fragment の packet の数（標準 pool 1024・32 の 1/8） |
| 再組立ての大きさ | 9152 | 1984 | `PACKET_BUF_MAX_STORAGE_SIZE - PACKET_BUF_DEFAULT_HEADROOM` |
| 時間 | 60 秒 | 60 秒 | RFC 8200 §4.5 |

選び方は `PACKET_BUF_JUMBO_COUNT` が 0 か（packet-buf.h と同じ機械の分け方）。
- 記録の空きが無い: 死んだ記録の一番古い物、無ければ生きた記録の一番古い物（`expire_ms` の一番小さい物）を空ける（ICMP は出さない）。
- 全体の数が上限: 自分以外の生きた記録を古い順に空けて空きを作る。自分しか無ければ自分を死なせる。
- 1 datagram の数が上限: その記録を死なせる。
- 追い出した・死なせた packet は lock の外で放す（lock の中では list に繋ぐだけ）。

## 6. ipv6.c の変更（fragment の所だけ）

- `ipv6_extensions` の引数を `struct packet_buf **packet` にし、fragment header で `ipv6_fragment_input(packet, next_header, offset, field)` を呼ぶ。
  非 0 なら packet は消費済みでそのまま返す。2 つ目の fragment header（walk の中の印）は捨てる。
- walk の終わりに、fragment を通った（印あり）で上の層が ICMPv6、その type が 133〜137 なら捨てる（RFC 6980 §5）。
- `ipv6_input` は `&packet` を渡す。`ipv6_init` で `ipv6_fragment_init()`。`ipv6_timer_run`・`ipv6_timer_next_deadline` に §4 の 2 つ。
  `ipv6_purge_device` で `ipv6_fragment_purge_device(device)`（その device の記録を空け、持つ packet の device の参照を早く返す）。
- file の先頭の説明の「a fragment is dropped: no reassembly, decision H6」を直す（開発の管理情報の「decision H6」もこの時に消す）。
  `wire.h` の `ipv6_fragment_wire` の comment「only recognised, to be dropped」を直す。
- 触らない: `tcp.c`（P3 の WS311）、`udp.c`、`icmp6.c`（定数は ipv6.h）、sysctl。

## 7. lock と文脈

- 新しい spin lock（`LOCK_RANK_NETWORK`、"IPv6 fragments"）が記録の全部を守る。lock の中では packet の pool も device も触らず
  （`packet_buf_free`・`net_device_ref`・`icmp6_error` は lock の外）、記録と packet の list の付け替えだけ。
  `ipv6_lock` と同時に持たない（ipv6.c は ipv6_lock を放してから呼ぶ）。
- 入力は network の worker が呼ぶ。timer も同じ worker。lock は loopback などの別の文脈の入力に備える。

## 8. 試験

- **host の単体**（`plan/ws312/tests/frag-host-test.c` と `run-frag-host-test.sh`、ASan・UBSan、64 bit と 32 bit の 2 つの形）:
  `ipv6-frag.c` と本物の `packet-buf.c` を host で build、`icmp6_error`・`net_device_ref/release`・spin lock・`ipv6_now_ms` は試験の側の stub
  （時計は試験が進める）。項目: 順、逆順、ばらばら、atomic（記録を作らない、同じ鍵の組立て中の物に影響しない）、重なり（全部を捨て、後の fragment も捨てる、ICMP なし）、
  全く同じ重複、中身の違う重複、欠け（揃わない）、時間切れ（offset 0 ありで Time Exceeded code 1 が 1 回、無しで 0 回、ICMPv6 の error の fragment は 0 回）、
  長さの誤り（8 の倍数でない → Parameter Problem code 0 pointer 4、65535 超え → pointer offset+2）、最初の fragment の鎖の欠け（code 3）、
  大きさの上限（9152 を超える → 捨てる、死んだ記録）、数の上限（datagram・全体・1 datagram）と追い出し、終わりの矛盾、
  unfragmentable part（destination options）を持つ fragment の組み立て（next header の書き換え・Payload Length）、device の purge、
  毎回 `packet_buf_in_use()` が 0 に戻る（漏れ無し）。
- **QEMU**（p003、`plan/ws312/tests/qemu-frag.sh`、1 本の script）: guest の 2 つ目の usb-net を host の tap に繋ぎ、bridge と veth の先の network namespace
  （P6 用の名前 `p6ws312`、`p6br0`・`p6tap0`・`p6v0/p6v1`）に `fd00:312::1/64`、guest に `fd00:312::2/64`。相手は namespace の中の host の Linux。
  1. `ping -6 -s 1400`（namespace の route に `mtu 1280` を付けて Linux に 1280 で分けさせ、返りは 1448 byte で link の 1500 に収まる）: 全部の返りが full の長さ。
  2. 大きい UDP: guest の受け手（`plan/ws312/tests/udp6-recv.c`、zedbsd-clang で build して image に `--file`）に 1400・4000・8000 byte の datagram、
     受けた長さと中身の hash が送った物と一致。12000 byte は上限で届かない事（制限の確かめ）。
  3. 手で作った fragment（namespace の中で `AF_PACKET` で Ethernet の frame を送る python）: 逆順で届く、重なりで届かない、atomic で届く、
     offset 0 を含む欠けで約 60 秒後に Time Exceeded code 1 が返る、offset 0 の無い欠けで返らない。
  4. `ping -6 -s 3000`（Linux が 1500 で分ける）: guest は受けるが、返りは 3048 byte で MTU を超え、送りの fragment が範囲の外なので返らない。判定しない（記録だけ）。
  判定は SSH の出力と namespace の中の道具の出力だけ（QEMU・serial の log は使わない）。
- build: amd64・rpi4・riscv64 の `$(BUILD)/vmunix` が warning 0。

## 9. 判断（人の判断が要るかもしれない点、推しで進める）

- **D1 再組立ての大きさの上限**: 1 つの packet（64 bit 9152 byte、32 bit 1984 byte）まで。65535 byte までには packet の鎖を上の層（UDP の受けの列など）が
  扱う大きな変更が要り、範囲を広げる。推し: 9152 まで（EDNS の DNS の 1232〜4096 byte、普通の大きい UDP は入る）。上は Future Work の候補。
- **D2 鍵に device を入れる**: RFC 8200 の鍵は送り元・宛先・Identification だけ。zedBSD の入力は device ごとに受け入れを決め、組み立てた packet は 1 つの device の物として
  上へ渡すので、device も鍵に入れる。違う interface から同じ datagram の fragment が来る経路（host で非対称の multipath）は組み立たない。推し: 入れる。
- **D3 ping6 -s 3000 の返り**: 受けは直るが返りは送りの fragment が要り範囲の外。QEMU の試験では返りを見る ping は 1400 byte（route の mtu 1280）で行う。

## design-reviewer

- 未実施。

## 未実施・制限

- RFC 7112 の上の層の header の長さの検査は UDP・TCP・ICMPv6 の最小の長さだけ（TCP の option の長さは見ない）。
- 統計の数（RFC 4293 の ReasmReqds などの類）は数えない（sysctl は P3 が触っているので今は足さない）。
