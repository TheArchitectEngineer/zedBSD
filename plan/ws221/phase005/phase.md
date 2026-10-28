<!-- awesome-plan project=zedbsd record=ws221-p005 -->
# ws221-p005: TCP の MSS と jumbo の segment（SYN の広告、送りの大きさ、受けの窓の jumbo の上限）

Status: in-progress（2026-10-29 P3 q1664-i03: 実装と host・QEMU・VF2 実機の確かめは済み。残り: 5 platform の build の確認、IPv6 と gateway 経由の IPv4 の実機の確かめは未実施）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i03（P3）
設計: [p001](../phase001/phase.md) §3.4（`tcp_device_mss`・送る segment の大きさ・SYN の広告・受けの窓の jumbo の上限、S7 (c) の任意）、§9 Q4（IPv4 の gateway 経由は 1460）・Q13

依存の注: ws.md の表は「p008・p009 が main に入って cleared」を依存に置くが、Q1 の依頼（2026-10-29「順: p004 の残り 1〜7 → p005 → …」）でこの順に行った。p005 は p008 の cwnd・送りの ring と p009 の受けの window の今の形の上に作った（p008 の残りの「gateway 経由の IPv4 の MSS 1460」と `tcp_device_mss` はここで入れた）。

## 実装（d17b806f1）

| 項目 | 内容 |
| --- | --- |
| `tcp_device_mss(device, ipv6, local)` | MTU − 40/60、`net_device_frame_max` − 14 − 40/60、packet の class の storage（標準 `TCP_SEGMENT_MAX` 1920、frame が標準に入らない device は `TCP_SEGMENT_MAX_JUMBO` 9088）の最小。自分の address 宛て（loopback を回る）は標準 |
| `tcp_path_mss(endpoint, device, ipv6)` | IPv6: device の値と path MTU − 60（`ipv6_route_source`、device が NULL なら jumbo の上限から）。IPv4: device の値、gateway 経由（`route_lookup_ref` の `gateway != 0`）は `TCP_GATEWAY_MSS` 1460 まで |
| SYN の広告 | 定数 `TCP_RECEIVE_MSS`・`TCP_RECEIVE_MSS6` をやめ `tcp_path_mss` を SYN・SYN-ACK の MSS option に。広告した値を `tcp.receive_mss` に持ち、net.tcpstats の `receive_mss` も持つ値 |
| 送る大きさ | `tcp_start_sending` の上限を `tcp_path_mss` に（相手の MSS との小さい方） |
| jumbo の segment | 送りの ring の copy と、copy を持たない segment の packet を `packet_buf_alloc_for`（大きい物は jumbo の class）。timer の pass の payload の写しを network worker の静的な 9088 byte の buffer に（stack に置けない、pass を走らせる thread は 1 つ） |
| 送りの jumbo の上限 | 接続ごとに ring の jumbo の copy を `TCP_SEND_JUMBO_MAX` 8 まで（`tcp.send_jumbo`、ring から外す 3 か所で減らす）。書き手は上限で room を待つ。jumbo の packet が無い時（ENOBUFS）はその write の残りを標準の大きさ（1920）に切る（MSS 以下はいつでも合法） |
| 受けの窓の jumbo の上限 | 広告した MSS が標準より大きい接続は窓を (`TCP_RECEIVE_JUMBO_MAX` 8 − queue の jumbo) × MSS まで。窓の更新の閾値を広告した MSS で |

選ばなかった案: ring の copy を ref で持ち staging の copy をやめる（速いが ring・retransmit の経路の変更が大きい。今回は staging を静的にした）。jumbo の上限で標準の大きさに切って続ける（混ざった大きさの segment になる。待っても 8 × 8960 は LAN の往復の BDP より大きい）。

## 確かめ

| 試験 | 結果 |
| --- | --- |
| WS220 の tcp-model（`plan/ws220/tests/tcp-model/run.sh`、全 scenario） | PASS。model に `--mtu`・`--write-bytes`・`--expect-mss` と jumbo の pool の欄を足し、scenario `lan-jumbo`（117.98 MB/s、MSS 8960）・`lan-jumbo-loss`（0.1 % の損失、再送は失った数だけ、timeout 0）・`lan-1500-writes`（64 KB の write で MSS 1460）を足した。`--expect-mss` は両端の MSS、jumbo を使ったこと、jumbo の最大 ≤ 16 を見る（誤った期待で FAIL を出すことを確かめた） |
| QEMU `plan/ws221/tests/qemu-tcp.sh`（usb-net、1500） | PASS（送り 50.1・受け 53.9 Mbps、emulator の値） |
| VF2 実機、相手 centris（この host）、1500 | 受け 948.7、送り 938.4・938.9（centris の経路の入れ替わりで前から 932〜948） |
| VF2 実機、相手 5330 の Debian（Q1 の許可、flock /tmp/i915-hw.lock）、1500 | 受け 949.1（最遅 942.7）、送り 949.1・949.1（最遅 948.0）: 前の 8ea1d234c と同じ |
| 同、MTU 9000（両端、`vf2-tcp.sh --mtu 9000 --peer-if`） | 受け 987.1・987.2（最遅 984.0）、送り 990.7・990.7・990.9（最遅 988.5）。理論値 8960/9038 × 1000 = 991.4。data の接続の send_mss・receive_mss 8960、再送 0、receive_refused 0、jumbo_used_max 11/32、jumbo_alloc_failures 0 |
| `plan/ws221/tests/vf2-jumbo.sh` | PASS（ping -s 8972、UDP の jumbo の保持、pool が戻る） |
| build | VF2 の kernel・probe warning 0、amd64 の vmunix warning 0。5 platform は流している |

未実施: IPv6 の jumbo の TCP（RA の MTU と path MTU）、gateway 経由の IPv4 の 1460（実機に router の向こうの相手が無い）、lo0 の TCP の MSS 1920（自分への接続、model・QEMU では見ていない）。

## 実機の状態（2026-10-29 07:1x）

VF2: `/boot/boot0/vmunix` は d17b806f1 の tree の build（build/vf2/vmunix、sha256 dc2a0f01aef671d3…）、vmunix.old は 8ea1d234c の物、en0 は 1500 に戻し読み戻し済み。5330: enx6c1ff706148a を 1500 に戻し読み戻し済み、/tmp に perf-peer.py と結果の file（path は Q1 へ送った）。
