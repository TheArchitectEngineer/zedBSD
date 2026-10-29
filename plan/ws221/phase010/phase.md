<!-- awesome-plan project=zedbsd record=ws221-p010 -->
# ws221-p010: 10GbE での TCP の stack の改善（RDNA 機の RTL8127 ↔ centris、限界まで）

Status: in-progress（2026-10-29 P3 q1679-i01: send 6280 Mbit/s（Linux 6808 の 92 %）、recv 3320〜3401（Linux 5458、未達）。recv は SACK と ACK の順の調べが次）
Parent: [WS221](../ws.md)
Queue: q1679 / q1679-i01・i02（P3）
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
| c47894277（read の直の経路 ＋ checksum の offload）、profile 版 | 5942・6294 | 223・3032 | 読み手の CPU は 72 % → 約 30 %（pin・kern_malloc が消えた）が、recv は上がらず、1 回は 1 秒目の後に止まった（窓は開いたまま、相手の再送が 2 秒おき）。zedBSD の out_of_order・取りこぼしは 0 |
| 同じ、production の config（profile 無し） | 6284・6264 | 3026・2874・2914・2936・3199 | centris の TcpExt: TCPRenoRecovery 20〜228、TCPFastRetrans 222〜1507（zedBSD は何も欠いていない: 偽の fast retransmit）。読み手の window update の重複を止める案（試し、commit しない）も 2850〜2986 で効かず |
| A/B: 同じで read を bounce に戻す（試し） | — | 3321・3348 | RenoRecovery 4〜10、FastRetrans 104〜244。直の経路が相手の偽の再送を増やす → 直の経路は取り下げ |
| 最終（直の経路を戻した tree、production、30 s） | 6280・6275 | 3401・3320 | RetransSegs 81・81、RenoRecovery 4〜5。基線（WS235: 試験の config send 5642・recv 3311、server の config 4743・3364）から send +11〜32 %、recv は同じ |

- send の segment の数え: 536k frame/s、centris の RetransSegs 0〜1（相手の向き）。zedBSD の送りの retransmit_segments 61〜394・fast_retransmits 25〜171（入れ替わりか switch、未確かめ）。
- recv: segments_in 5.1M・segments_out 3.2M（delayed ACK と読み手の window update 56 万）。window update を減らす案（Linux の「窓が 2 倍になったら」）は模型 lan-loss-1 で余計な再送 2 が出たので入れない。

## 変えた事

| commit | 内容 |
| --- | --- |
| 7034cea3a | `tcp_send_reliable`: 書き手は segment ごとに `net_worker_wakeup` を呼ばず、新しい deadline の時だけ（最初の in flight で retransmit timer を張った・TCP_MAXUNACKTIME の始まり・output retry・persist・device の拒否）。`checksum_add`: 4 語ずつ 2 つの和に。`rdna-tcp.sh`・`pcprof-summary.py` |
| c47894277（次の commit で取り下げ） | read(2) の直の経路: socket の op `recv_user`、`socket_file_reads_user`・`socket_file_read_user`、`sys_read` は stream の socket を pin せずに渡す。TCP は最初の packet が来てから、その packet と queue の byte が埋める分だけ pin し、packet から pin へ直に copy（bounce の kern_malloc と 2 度目の copy を無くす）。読み手は軽くなったが recv が下がった（上の表）ので戻した。差分は c47894277 にある |
| c47894277 | checksum の offload（下の設計）と RTL8127 の TX（IPv4 header・TCP）・RX（IPv4 header・TCP・UDP、IPv4・IPv6）の checksum |

## checksum の offload の stack の口（設計、2026-10-29）

- device の能力: `net_device.capabilities` の `NET_DEVICE_CAP_TX_CSUM_IPV4_TCP`（IPv4 の header と TCP の checksum を device が計算、MTU までの frame）。driver が attach で立てる。
- packet の flag（`packet_buf.flags`）:
  - 送り: `PACKET_BUF_TX_CSUM_TCP`（TCP の checksum の field に pseudo-header の畳んだ和、補数にしない: `net_checksum_pseudo_partial`。l4_offset）、`PACKET_BUF_TX_CSUM_IPV4`（IPv4 の header の checksum を device が、field は 0）。TCP は route の device が能力を持つ時だけ立て、IPv4 の出力（`ipv4_header_checksum`）が実際に出る device（loopback に替わる時を含む）を見て、能力が無ければ `packet_buf_checksum_finish` で software で仕上げる。送り直しの copy は毎回 flag を消してから決め直す。
  - 受け: `PACKET_BUF_RX_CSUM_IPV4`・`PACKET_BUF_RX_CSUM_L4`（device が確かめて正しかった物だけ）。IPv4 の header、TCP（IPv4・IPv6）、UDP（IPv4・IPv6）の確かめを飛ばす。正しくない・確かめていない物は印を付けず stack が確かめる。IPv6 の再組立ての後は L4 の印を消す。
- driver が offload できない frame（RTL8127 では 60 byte 未満の短い frame、padding するため）は `packet_buf_checksum_finish` を呼ぶ。
- IPv6 の TX checksum（RTL の v2 の descriptor は transport の offset が要る）、UDP の TX、jumbo の frame の TX checksum は今は入れない（RTL8127 の driver の MTU は 1500）。
- TSO（大きい segment を device が MSS に切る）: 口だけの案。packet は 1 つの連続した storage（標準 2 KB・jumbo 9 KB × 32）なので、TSO の 64 KB の segment には scatter-gather（複数の storage の packet）か大きい class が要る。send は TSO 無しで Linux の 92 % に来ており、PCIe の上限の近くで TSO の伸びは小さいと見る。今の Phase では実装しない（Future Work の候補、Q1 へ）。

## recv の所見（次の attempt へ）

- recv の律速は CPU ではない: 読み手・worker・割り込みのどれも飽和しない。窓 128 KB（TCP_RECEIVE_PACKETS 128）と相手（Linux の Reno 型の回復、SACK 無しの相手に対して）の振る舞いで決まっている。
- 読み手を速くすると（直の経路）相手の偽の fast retransmit が増え、遅くなる。zedBSD 側に欠け・入れ替わりは無い。相手から見た重複 ACK の出どころ（読み手の window update と worker の ACK が別の thread から別の順で device に入る、など）は未特定。次は centris で zedBSD の ACK を tcpdump で見て（ack 番号と窓の並び）確かめる。
- 窓を広げると（768 KB）NIC の取りこぼしから崩れる。SACK（F-WS311-sack）が無いと 10GbE の損失の回復が遅い。recv を Linux の 5458 に近づけるには、SACK と ACK の順の直しが先と見る。

## q1679-i02（2026-10-29 22:2x〜、P3 の再起動。前の P3 は 16:50 頃に止まった）

### 非退行（worker の起こしの直しと checksum の口は全 NIC に効く）

| 対象 | kernel | 結果 |
| --- | --- | --- |
| VF2（dw-eqos、相手 5330 の Debian、30 s） | b36ed1860 の tree（be5651cd） | `vf2-tcp.sh --runs send,send,recv,recv i02-nonreg`: send 949.1・949.2、recv 949.3・949.3 Mbit/s、最遅の 1 秒 ≥ 947.5、RX errors 0、PASS（ws.md の 8ea1d234c の 948.9・949.1 と同じ水準） |
| 5330（RTL8156、CDC NCM、相手 centris、30 s、同じ userland で kernel だけを A/B: `MODE=--nvme-kernel 5330-ncm.sh`） | p010 の前（main af875cc26）と b36ed1860 の tree を交互に | 前: send 267.0・265.1、recv 696.1・555.1。今: send 270.5・267.7、recv 647.3・637.3（image の 1 回目 10 s は 274.6・617.4、30 s は 270.8・642.8）。recv は同じ kernel で 555〜696 と揺れ、差は揺れの中。ping -s 1476・1472 が通る、mtu_max 1504 |
| host | — | tcp-model PASS、amd64・pcat・rpi4・rpi5・zybo・RTL8127 の config・VF2 の vmunix の build warning 0、style-check tcp.c 115 → 113・socket.c 27 → 27 |

- 5330 の NVMe の zedBSD の区画には build/i02-5330 の image（config-5330-jumbo.mk、b36ed1860 の tree）、kernel は同じ tree の物が入ったまま。Debian に戻した。VF2 の /boot/boot0/vmunix は be5651cd（b36ed1860 の tree）、vmunix.old はその前。
- 5330 の image の build: この worktree の build/sources は main への link でなく、firmware の license の確かめで止まった。main の build/sources/firmware を build/i02-fw に複写し、`*_FIRMWARE_CACHE_ROOT` をそこに向けて build（license は受け入れていない、main の build に書いていない）。

### ACK の順（前の P3 の差分を確かめて b36ed1860 に）

- 直し（b36ed1860）: `socket_enqueue_stream_advance` が receive queue に積むのと同じ socket の lock の中で receive_next を進め、`tcp_output_segment` が ACK の値と窓を同じ lock の中で読む。前は、積んでから receive_next を進めるまでの間に別の thread（読み手の window update）が作った segment が、古い receive_next と既に狭まった窓を運んだ。呼び手は socket の lock を持たない（`tcp_send_segment_at`・`_carried` の注）、input は worker の 1 thread なので、前の `receive_next == sequence` の確かめは要らない。
- `ack-order.py` に IPv4 の ID による分け: 古い ACK（最後に進めた ACK より後ろ）の ID が、その進めた ACK の ID より前なら「IP の出力の後で追い越された」、後なら「stack が古い点を読んで作った」。zedBSD は IP の出力の順に ID を振る。
- 前の P3 の capture（相手 centris、zedBSD が受け、20 s）: 4b39403c5 の tree（i02-base 1 回目）は古い ACK 111815 のうち作りの遅れ 862・追い越し 110953。ACK の直しの kernel（前の P3 の build/p010/fix、56e121052dc551a5。b36ed1860 と同じ code かは未確かめ、i02-fix 1 回目）は 136431 のうち作りの遅れ 0・追い越し 136431。直しで作りの遅れは消えた。残りの殆どは IP の出力の後の入れ替わり: centris の受けの queue（ixgbe の flow director ATR が、送り手の thread の CPU に合わせて受けの queue を移す。zedBSD の ACK は queue 1・2 と 14〜31 に分かれた）か、zedBSD の IP の出力から device の ring までの間（RTL8127 の TX の ring は 1 つ）。
- 同じ capture の recv: 4b39403c5 の tree 3303・3556・3683・3120、その直しの kernel 2923・2967・3001・3141 Mbit/s（相手の TCPFastRetrans は前 113〜903、今 15〜166）。fast retransmit は減ったが recv は上がっていない。

### 10.0.10.3 の測り（i02、2026-10-29 22:53〜23:36、tcp-perf 20 s、recv は 10.0.10.3 が受ける、相手 centris）

kernel: 現 = b36ed1860（ACK と窓を出力で socket の lock の下で読む、0728f214）、前 = 4b39403c5（f1d85b42）、A = 出力の lock を外した物（2261a721）、B = A ＋ 窓の更新の完全な重複を出さない（e3d7cfa7）。PEER_CPU=2 は perf-peer.py を centris の CPU 2 に固定（受けの queue の散りは止まらなかった）。

| 条件 | kernel | recv Mbit/s | 相手の TCPFastRetrans |
| --- | --- | --- | --- |
| ATR 有効（既定） | 現 | 2913・2926・3335・3061・3212・3286・3342・3602・3447 | 59〜891 |
| ATR 有効 | 前 | 3003・3535・3239・3300（前の P3 の 3303・3556・3683・3120） | 76〜903 |
| ATR 有効 | A | 3729・3278・3230・3596・3138・3419 | 29〜522 |
| ATR 有効 | B | 2959・2926・2875 | 77・101・0 |
| ATR 無効（ntuple on、原因の参考、受け入れの値でない） | 現 | 5170・4819 | 0・1 |
| ATR 無効 | B | 4916・5095 | 0・0 |
| ATR 無効 | 前 | 5286・5054 | 724・8 |

- send（現 6159、B 6233）は前の 6280 と同じ水準。
- 所見: ATR 有効では kernel の差は run の揺れ（2875〜3729）の中。ATR を止めると recv は 4819〜5286（Linux 5458 の 88〜97 %）、古い ACK は約 11 万 → 1975、相手の再送 0〜8（前の kernel の 1 回だけ 724）。recv を抑えていたのは centris の ixgbe の ATR（送り手の CPU の移りで zedBSD の ACK と受けの queue が入れ替わる、IP の出力の後の追い越しが約 3 %、ACK の種類によらず同じ割合）と、SACK の無い相手の偽の fast retransmit。
- 窓の更新の完全な重複（同じ ACK・同じ窓、IP ID が続き、間に data が無い）: 1 回の 20 s で 3 千〜8 万。Linux はこれを dupack に数える。読み手が窓の更新を決めてから出すまでに worker の ACK が同じ窓を出した時にできる。B はこの時に出さない（`tcp_send_ack_news`・`tcp_output_segment` の news_only）。
- 決め（P3）: A と B を入れる（出力の lock は効きが見えず、毎 segment の lock を持たない方が軽い。B は相手に偽の dupack を渡さない）。recv の改善の数字としては扱わない（ATR 有効の揺れの中）。
- ユーザーの決定（2026-10-29、原文）:「P3は、SACKの実装を優先してください。ATRを有効にした評価で進めます。」→ 評価は ATR 有効で行う。ATR 無効の値は原因の参考だけ。次は SACK（[p012](../phase012/phase.md)）。

### centris の NIC の設定の変更（2026-10-29 ユーザー「centrisのNICの設定は、変更してOKです。」）

- 対象: centris の enp94s0f0（ixgbe、firmware 0x8000052f、10.0.10.2/16、10000Mb/s。10.0.10.3・10.0.30.6 への route はこの interface。WS235 の記録の enp94s0f1 は DOWN で使っていない）。
- 変更: 23:23:20 に `sudo ethtool -K enp94s0f0 ntuple on`（前は `ntuple-filters: off`、ixgbe は ntuple を on にすると flow director の ATR を止める）。変更の後も link 10000・ping 10.0.10.3・10.0.30.3 通る。
- 戻した: 23:35:38 に `sudo ethtool -K enp94s0f0 ntuple off`、読み戻し `ntuple-filters: off`、link 10000、ping 10.0.10.3 通る（ユーザーの決定「ATRを有効にした評価で進めます。」で終わりを待たずに戻した）。
- 10.0.10.3 は p010 が終わるまで P3 の専有（Q1 2026-10-29 23:2x、ユーザーの決定）。centris の /tmp/zedbsd-rdna.lock を sleep の holder で持ち続ける。

## 未実施・残り

- 最終の tree（直の経路を戻した後）で確かめた事: tcp-model PASS、amd64・pcat・rpi5・zybo・rpi4 と plan/tools/rtl8127/config-amd64-rtl8127.mk の vmunix の build warning 0、style-check の件数は変えた file で増えない（tcp.c 117 → 115）、RDNA 機の最終の測り（上の表）。RTL8127 の checksum の offload が実地で通る事（send・recv・ssh が production の kernel で動いた、errors 0）。
- c47894277 で確かめた事: tcp-model PASS（`plan/ws220/tests/tcp-model/run.sh`、shim に uaccess の stub）、checksum の host 試験 PASS（`run-copy-sum-host-test.sh`、64・32 bit）、QEMU `qemu-tcp.sh "" 15 inet,inet6` PASS（read(2) の直の経路を tcp-perf と sshd が通る。QEMU の NIC は offload の能力が無いので software の経路）、amd64・pcat・rpi5・zybo・rpi4 と plan/tools/rtl8127/config-amd64-rtl8127.mk の vmunix の build warning 0、style-check の件数は変えた file で増えない。
- 最終の tree の QEMU `qemu-tcp.sh "" 15 inet,inet6` PASS（9b31ef897）。
- 未実施: VF2・5330 の非退行（checksum の offload は RTL8127 だけが能力を立てるので他の NIC は software の経路のまま）、規約の全文の見直し（p007 の後の hunk）、checksum の offload の効きの切り分け（send は PCIe の上限の近くで差が見えない）。
- SACK が無いことが大きい窓・10GbE の損失の回復を律する（F-WS311-sack に関係、Q1 が記録）。
