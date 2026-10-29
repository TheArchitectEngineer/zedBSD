<!-- awesome-plan project=zedbsd record=ws221-p010 -->
# ws221-p010: 10GbE での TCP の stack の改善（RDNA 機の RTL8127 ↔ centris、限界まで）

Status: in-progress（2026-10-29 P3 q1679-i01）
Parent: [WS221](../ws.md)
Queue: q1679 / q1679-i01（P3）
依存: WS235（RTL8127 の driver、completed）、p002〜p009、WS311（CUBIC、completed）

## 由来と目標

- ユーザー（原文、2026-10-29）:「WS221には、10GbEでのTCPスタックの改善を次のフェーズとして作成してお願いしたいです。…それが完成したら、WS221に戻って、10GbEの限界を、RDNA機で試す流れにしたいです。」
- ユーザー（原文、2026-10-29）:「このマシンはPCIeの帯域の制約があり、7Gbit/sくらいしか出ないです。」→ 目標は約 7 Gbit/s と同じ機械の Linux の値（WS235: send 6808・recv 5458 Mbit/s）。
- Q1 の判断（2026-10-29）: checksum・TSO の offload の stack の口（packet-buf・net-device の flag）は p010 で設計する。
- 引き継ぎ（WS235）: driver は律速でない（profile）、律速は stack。基線 send 4743〜5642・recv 3310〜3364 Mbit/s。

## 道具（plan/ws221/tests/）

| 道具 | 内容 |
| --- | --- |
| `rdna-tcp.sh`（新） | rtl-link.sh の LANTEST=2 の流れを 1 本に: NVMe の kernel を保存して入れ替え、BootNext で zedBSD、rge0 を上げ ue0 と接続の route を落とす、tcp-perf・sysctl を /tmp へ、`--runs` の各 run の前後に hw.cputimes・driver の profile の行・net.tcpstats（run の間 2 秒ごと）、centris の RetransSegs、PC sampler の kernel なら `pcprof-summary.py`。終わりに Debian に戻し保存した kernel を戻して照合、RTX の driver を読む。出力は build/ws221-rdna/NAME |
| `pcprof-summary.py`（新） | 一時の PC sampler（commit しない差分 build/tmp/pcprof-amd64.patch: 割り込みの frame の RIP を tick で数え、割り込みを止めた区間は spin_unlock_irqrestore の呼び手に寄せる、hw.cputimes の読みで dmesg に出して空にする）の行を kernel の symbol で関数に。CPU ごとの user/kern/idle、thread ごと、CPU 0 と他の CPU の上位の関数 |

## 測り（10.0.10.3 の RTL8127 → centris の 10GbE、tcp-perf 20 s、MTU 1500、profile の kernel）

| kernel | send Mbit/s | recv Mbit/s | 所見 |
| --- | --- | --- | --- |
| 基線 b51eb69a8 | 5406・4440 | 3337・3724 | send: 書き手が CPU 0 で 99 %（memcpy 16 %・checksum 15 %・rtl_transmit 10 %・tcp_send_reliable 10 %、IPI の send_icr 5 %）、network worker が別の CPU で 82 % を空回り。recv: 誰も飽和しない、読み手の read(2) の pin・bounce が CPU 0 の約 60 % |
| 受けの窓 768 KB（試し、pool 4096。commit しない） | 4804 | 191・168 | 崩れた: NIC の取りこぼし（missed 2553）→ 入れ替わり 6.2 万・保った 1.9 万（reorder 128 の外は捨てる）、相手の再送 5.2 万。SACK が無いので相手の回復が遅い。窓は 128 KB のまま |
| 7034cea3a（書き手が worker を起こすのは新しい deadline の時だけ、checksum の 4 語の展開は未測） | 6254・6223 | 3250 | send は Linux 6808 の 92 %、PCIe の上限 7 Gbit/s の近く。CPU の飽和無し（書き手 約 50 %）。recv は変わらず（読み手の pin・bounce） |

- send の segment の数え: 536k frame/s、centris の RetransSegs 0〜1（相手の向き）。zedBSD の送りの retransmit_segments 61〜394・fast_retransmits 25〜171（入れ替わりか switch、未確かめ）。
- recv: segments_in 5.1M・segments_out 3.2M（delayed ACK と読み手の window update 56 万）。window update を減らす案（Linux の「窓が 2 倍になったら」）は模型 lan-loss-1 で余計な再送 2 が出たので入れない。

## 変えた事

| commit | 内容 |
| --- | --- |
| 7034cea3a | `tcp_send_reliable`: 書き手は segment ごとに `net_worker_wakeup` を呼ばず、新しい deadline の時だけ（最初の in flight で retransmit timer を張った・TCP_MAXUNACKTIME の始まり・output retry・persist・device の拒否）。`checksum_add`: 4 語ずつ 2 つの和に。`rdna-tcp.sh`・`pcprof-summary.py` |
| c47894277 | read(2) の直の経路: socket の op `recv_user`、`socket_file_reads_user`・`socket_file_read_user`、`sys_read` は stream の socket を pin せずに渡す。TCP は最初の packet が来てから、その packet と queue の byte が埋める分だけ pin し、packet から pin へ直に copy（bounce の kern_malloc と 2 度目の copy を無くす） |
| c47894277 | checksum の offload（下の設計）と RTL8127 の TX（IPv4 header・TCP）・RX（IPv4 header・TCP・UDP、IPv4・IPv6）の checksum |

## checksum の offload の stack の口（設計、2026-10-29）

- device の能力: `net_device.capabilities` の `NET_DEVICE_CAP_TX_CSUM_IPV4_TCP`（IPv4 の header と TCP の checksum を device が計算、MTU までの frame）。driver が attach で立てる。
- packet の flag（`packet_buf.flags`）:
  - 送り: `PACKET_BUF_TX_CSUM_TCP`（TCP の checksum の field に pseudo-header の畳んだ和、補数にしない: `net_checksum_pseudo_partial`。l4_offset）、`PACKET_BUF_TX_CSUM_IPV4`（IPv4 の header の checksum を device が、field は 0）。TCP は route の device が能力を持つ時だけ立て、IPv4 の出力（`ipv4_header_checksum`）が実際に出る device（loopback に替わる時を含む）を見て、能力が無ければ `packet_buf_checksum_finish` で software で仕上げる。送り直しの copy は毎回 flag を消してから決め直す。
  - 受け: `PACKET_BUF_RX_CSUM_IPV4`・`PACKET_BUF_RX_CSUM_L4`（device が確かめて正しかった物だけ）。IPv4 の header、TCP（IPv4・IPv6）、UDP（IPv4・IPv6）の確かめを飛ばす。正しくない・確かめていない物は印を付けず stack が確かめる。IPv6 の再組立ての後は L4 の印を消す。
- driver が offload できない frame（RTL8127 では 60 byte 未満の短い frame、padding するため）は `packet_buf_checksum_finish` を呼ぶ。
- IPv6 の TX checksum（RTL の v2 の descriptor は transport の offset が要る）、UDP の TX、jumbo の frame の TX checksum は今は入れない（RTL8127 の driver の MTU は 1500）。
- TSO（大きい segment を device が MSS に切る）: 口だけの案。packet は 1 つの連続した storage（標準 2 KB・jumbo 9 KB × 32）なので、TSO の 64 KB の segment には scatter-gather（複数の storage の packet）か大きい class が要る。send は TSO 無しで Linux の 92 % に来ており、PCIe の上限の近くで TSO の伸びは小さいと見る。今の Phase では実装しない（Future Work の候補、Q1 へ）。

## 未実施・残り

- c47894277 で確かめた事: tcp-model PASS（`plan/ws220/tests/tcp-model/run.sh`、shim に uaccess の stub）、checksum の host 試験 PASS（`run-copy-sum-host-test.sh`、64・32 bit）、QEMU `qemu-tcp.sh "" 15 inet,inet6` PASS（read(2) の直の経路を tcp-perf と sshd が通る。QEMU の NIC は offload の能力が無いので software の経路）、amd64・pcat・rpi5・zybo・rpi4 と plan/tools/rtl8127/config-amd64-rtl8127.mk の vmunix の build warning 0、style-check の件数は変えた file で増えない。
- 未実施: RDNA 機での c47894277 の測り（read の直の経路・checksum の offload の効き、RTL8127 の checksum の実地。次の枠、P5 の後）、VF2・5330 の非退行、規約の全文の見直し（p007 の後の hunk）。
- SACK が無いことが大きい窓・10GbE の損失の回復を律する（F-WS311-sack に関係、Q1 が記録）。
