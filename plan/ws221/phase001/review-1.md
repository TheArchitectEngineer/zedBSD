# ws221-p001 review-1（design-reviewer、2026-10-13）

対象: [phase.md](phase.md)（WS221 Jumbo frame の設計 第 1 版）。照合した tree: `/home/awe/zedBSD-claude1` main `0992c5791`。
phase.md の行番号は `4f5cbfcdb` の物で、その後 WS220 p002 が `src/kern/net/tcp.c` を変えたので tcp.c の行はずれている（軽微 L1）。
.internal/ と GPL の source（Linux の cdc_ncm・cdc_ether・r8152、QEMU の source）は読んでいない。文書と source は直していない。

## 判定

- 致命的 0、重大 8、中 11、軽微 12。
- **p002（kernel）: 条件つきで開始してよい。** 条件: S1（MTU の変更と open の競合）、S2（`syscall.c` の sendmsg・recvmsg の上限の漏れ）、M1（下げる変更の可否の矛盾）、M2（p002 の QEMU 試験が p004 の `ping -s` に依存）を phase.md に直してから。S7（jumbo の slot の枯渇の方針）は p002 の pool の API に効くので、p002 の開始前に Q2 と一緒に決める。S3・S4 は p005（と WS220 p006）の要件として今の版に書き足す。
- **p004（設定）: まだ始めない。** S6（起動の経路は reconcile でなく `LAN_ENABLE` の text record、その形式と networkd の decode、Settings の LAN の設定の経路）で §3.7 の設計の前提が違う。M5（Q8）・M8・M10 と合わせて §3.7 を書き直してから。
- p003 の前に S8（RTL8156 の CDC の configuration の `wMaxSegmentSize` と NTB parameter を読む）を済ませることを強く勧める。値が 1514 なら USB LAN 9000 の目標は今の driver の構成（CDC だけ）では届かず、ユーザーの判断が要る。

## 重大

### S1. MTU の変更が open と競合する（状態遷移の穴）

- 根拠: §3.2 の手順は「2. UP なら EBUSY」→「3. `net_device_ioctl` と同じ admission で `ops->set_mtu`」→「4. `device->mtu = mtu`」。しかし `net_device_ioctl` の admission（`src/kern/net/net-device.c:1057〜1113`）は `ioctl_active` を数えるだけで、`net_device_open`（net-device.c:835〜913）は `opening`・`closing` だけを見て `ioctl_active` を見ない。`SIOCSIFFLAGS` も「flag を見てから open」の check-then-act（`inet-socket.c:653〜666`）。
- 起こる状況: networkd の `lan_configure_steps`（`ifconfig IF up`）や管理者の `ifconfig up` が、別の process の `ifconfig IF mtu N` の 2 と 4 の間に入る。driver の open は古い `device->mtu` で受信の URB・`frame_size` を決め、その後 `device->mtu` が新しい値になる。ECM なら受けの検査（open の `frame_size`）と IP の層（`device->mtu`）が食い違い、大きい frame を EMSGSIZE で落とし続ける。§3.9 の「driver の set_mtu は adapter の lock で opened == 0 を確かめる」は driver の内側の確かめで、4 の書き込みは lock の外なので塞げない。
- 直し方: `device_lock` の下で「`open_count == 0 && !opening && !closing && !mtu_changing`」を確かめて `mtu_changing = 1` を立て、`net_device_open` は `mtu_changing` の間 EBUSY を返す。`ops->set_mtu` の後、`device_lock` の下で `device->mtu` を書いてから flag を下ろす。gone・shutdown_all は `mtu_changing` を join の対象に入れる。host 試験（packet-buf-host-test と別の net-device の host 試験、または p002 の試験）に「set_mtu の間の open は EBUSY」を入れる。

### S2. datagram の大きさの上限の 4 か所のうち 2 か所が漏れている（ping6 の受け入れが誤って通る）

- 根拠: 設計（§1.1、§3.1）は `syscall.c:1844〜1847`（sendto）と `:1961`（recvfrom）だけを直す。`src/kern/syscall.c:2075`（sendmsg: `request.data_length > PACKET_BUF_STORAGE_SIZE` で EMSGSIZE）と `:2245〜2246`（recvmsg: `buffer_capacity` を 2048 に切る）も同じ定数を使う。
- 起こる状況: UDP・raw の `sendmsg` は 2048 を超えると EMSGSIZE のまま。`recvmsg` は 2048 で切る。`ping6` は `recvmsg` で受ける（`userland/base/ping/main.c:592`）ので、§8.3 の 6（`ping6 -s 8952`）の応答は 2048 byte に切られ、それでも type・identifier・sequence は先頭にあるので **ping6 は応答として数え「2048 bytes from」を出して成功に見える**（試験で検出できない誤り）。
- 直し方: 4 か所とも同じ規則にする（S7 の判断次第で family ごと: AF_UNIX は 2048 のまま、L9）。受け入れでは ping の出す長さが送った長さと一致することを判定にする。`ping` の `packet[2048]`（main.c:60・432）と `echo[64]` は `-s` で動的にする（M2）。

### S3. `tcp_device_mss = device->mtu − 40` は lo0（65535）と 32-bit の board で packet の slot を超える

- 根拠: lo0 は `mtu = 65535`（`src/kern/net/core.c:468`）。§3.4 は SYN の MSS と送る segment を `device->mtu − 40/−60` にする。最大の slot は 9216（64-bit）・2048（32-bit）。WS220 の p006 の設計（`plan/ws220/phase001/phase.md:154`「大きさは min(相手の MSS, device の MTU − 40)」）と `plan/ws220/ws.md` 末尾の Q1 の依頼文も同じ式。
- 起こる状況: 自分の address（loopback 経由）への TCP で両端が MSS 65495 を広告し、segment の packet の alloc（tcp.c:1408 の `packet_buf_alloc` + append、送りの写し tcp.c:1911）が失敗し続け、localhost の TCP が止まる。32-bit の board では MTU 1500 でも slot（2048 − 64）に収まるが lo0 は同じく壊れる。WS220 p006 が先に入るので、壊すのは WS221 より先に WS220 になる。
- 直し方: `tcp_device_mss` に packet の上限を入れる: `min(mtu − hdr, PACKET_BUF_MAX_STORAGE_SIZE − PACKET_BUF_DEFAULT_HEADROOM − hdr − TCP の option の最大)`。この要件を WS220 p006 の設計にも入れる（Q1 経由、ws220/ws.md 末尾の依頼文を「MTU と packet の class の上限の小さい方」に直す）。tcp-model に lo0（65535）の scenario を足す。

### S4. IPv6 の TCP の segment は path MTU で切らないと止まる。IPv4 の経路越しの black hole を過小評価している

- 根拠: IPv6 の送りは `icmp6_path_mtu(destination, link.mtu)` を超えると EMSGSIZE（`src/kern/net/ipv6.c:294〜303`）。`link.mtu` は RA で下がり（ipv6.c:604〜605）、Packet Too Big でも下がる（icmp6.c:392〜420）。§3.4 の segment の大きさは `device->mtu − 60` と相手の MSS だけで決め、path MTU を見ない。IPv4 は DF が常に立ち（ipv4.c:346）ICMP の Fragmentation needed を読まない（§1.3）。
- 起こる状況: (a) device 9000・RA が 1500 の link で、相手が 8940 を広告すると zedBSD は 8940 の segment を作り、`ipv6_output` が毎回 EMSGSIZE → 再送も同じ大きさで永久に止まる。PTB で 1280 を覚えた経路も同じ。(b) IPv4: 両端が jumbo の LAN にいて間に 1500 の router がある時（別の VLAN・別の subnet）、相手の MSS 8960 で送った segment は router で落ち、ICMP を読まないので接続が黙って止まる。§3.3 の「TCP は相手の MSS で切るので混在の LAN で困らない」は相手の MTU が 1500 の場合だけ正しい。
- 直し方: (a) TCP の送る segment の上限を「その時の path MTU − 60」にし、送る前（または PTB の受け取りの時）に `ipv6_route` の `*mtu`（ipv6.c:407〜415 が既に path MTU を返す口を持つ）で取り直す。(b) Q4 の推奨を変える: 最小の安全策として「gateway 経由の IPv4 の宛先には MSS を 1460（device の MTU − 40 との小さい方）に、on-link の宛先だけ device の MTU から」（BSD の古い既定と同じ考え）を p005 に入れるか、TCP の ICMP type 3 code 4 の処理だけを入れる。どちらもしないなら制限としてユーザーに示して決めてもらう（S8 の完了の条件 3 と同じ判断）。

### S5. ECM の受信の URB を `14 + mtu` で arm すると、device が大きい frame を送った時に USB の転送が壊れる

- 根拠: §3.5「受信の URB は open で `14 + device->mtu` の長さで arm」。今も `ECM_FRAME_SIZE` 1514 で arm している（`src/drivers/usb/usb-cdc-ecm.c:1047・1350`）。ECM の device は自分の `wMaxSegmentSize` までの frame を host に渡し、frame の終わりは short packet（max packet の倍数なら ZLP）だけで示す。
- 起こる状況: RTL8156 などが ECM の configuration で `wMaxSegmentSize` 9014 を持ち、zedBSD の MTU が 1500（既定）で、LAN に 9000 の相手がいる時（混在の試験 §8.3 の 3 そのもの）。URB の長さが max packet の倍数でなければ最後の packet が溢れて babble（xHCI の error、`pci-xhci.c:7931` の経路で endpoint の回復、ECM の再 arm の retry は 3 回 `ECM_REARM_RETRY_MAX`）。倍数なら frame の途中で URB が終わり、残りが次の URB の先頭に来て frame の境が崩れる（どちらも推測を含むが USB の bulk の意味から確か）。今の 1514 の arm でも同じ危険がある（既存の不具合の可能性、実機で未確認）。
- 直し方: 受信の URB は常に attach の `frame_max`（descriptor の値と kernel の上限の小さい方）を max packet の倍数に切り上げ、`frame_max` 自身が倍数なら 1 packet 足した長さで arm する（ZLP を同じ URB で受ける）。MTU を超える frame は driver か `net_device_receive` で落とす（NCM の §3.6 と同じ考え）。これで URB の長さは MTU に依らなくなり、Q1 の理由（URB の取り直し）は弱くなる（Q1 の推奨自体は単純さのために保ってよい）。

### S6. 起動の経路は reconcile ではない。networkd の wired の policy は `LAN_ENABLE` の text record で、形式の拡張が要る（p004 の設計の前提違い）

- 根拠: `net startup` は `lan_send_policy`（`userland/base/net/main.c:2116〜2204`、`startup_command` 2496）を呼び、interface ごとに `"IF disabled"`・`"IF dhcp N"`・`"IF static A M"` の record を `NETWORKD_OP_LAN_ENABLE` で送る。address も dhcp も無い enabled の interface は record を送らない（main.c:2178〜2184）。networkd は `lan_policy_decode`（`userland/base/networkd/main.c:1421〜1508`）で word の数を厳密に見る（dhcp は 3、static は 4、上限 5 word、record 128 byte）。
- 起こる状況: §3.7 の「初回（起動）は interface が down なので `MTU` → `UP` の順」（reconcile の emit）は起動では走らない。§3.7 の「policy は net.conf の `mtu` から（場所は p004 で確かめる）」は、この record に mtu を足す形式の変更そのもので、`"IF static A M mtu N"` は 6 word になり今の decode は EINVAL で **wired の policy 全体を拒む**。mtu だけの interface には record が無い。§8.2 の 7（再起動の後に 1400）は今の設計では通らない。加えて Settings の LAN の設定（`net lan IF set` → `NETWORKD_OP_LAN_CONFIGURE` → `userland/base/networkd/lan-configure.c:157〜240` が net.conf を編集し `lan_configure_policy` で policy を作る）でも mtu を保ち policy に入れる必要がある。
- 直し方: p004 の設計に (1) record の形式（例 `"IF dhcp 10 mtu 9000"`、word の上限を 7 に、`mtu` の値の検査）、(2) networkd の decode と `struct networkd_lan_policy` の `mtu`、(3) dhcp/static の無い interface の扱い（mtu だけの record を送るか、送らないなら制限として書く）、(4) LAN_CONFIGURE の経路、(5) reconcile の `MTU` op と policy の更新の関係（op の handler が policy の値も変えるか、`LAN_ENABLE` の再送に任せるか）を書く。host 試験は `lan_policy_decode` と `lan_send_policy` の record の往復を含める。

### S7. jumbo の slot 32 は容易に枯れ、非特権の利用者でも枯らせる（Q2 の根拠の不足）

- 根拠: datagram の socket は受けの queue に 8 message・64 KB まで持つ（`include/kern/net/socket.h:44〜45`、`socket.c:643〜647`）。lo0 は MTU 65535 なので、§3.1 の `alloc_for` を UDP に使うと、利用者は自分の UDP socket へ 127.0.0.1 で 9 KB の datagram を送って jumbo の slot を取れる（読まない socket 4〜5 個で 32 個を使い切る）。ECM・NCM の送りの queue は 8（`ECM_TX_QUEUE_MAX`・`NCM_TX_QUEUE_MAX`）、TCP の送りの写しは接続ごとに 8（`CONFIG_TCP_SEND_QUEUE_MAX`、`include/kern/net/tcp-socket.h:36〜39`）、受けは 44 + reorder 44（64-bit の `TCP_REORDER_MAX` は 44: tcp-socket.h:52〜56。§1.4 の「reorder 16」は 32-bit の値）。
- 起こる状況: jumbo が尽きると NIC の 2048 を超える frame は全部落ちる（標準の class へ逃げない設計）。WS220 p006 の後に 4 本の bulk の TCP（各 8 の写し）で枯れる。§10 の「8 slot/接続、許容」は 1 接続の時だけ。p005 の後は 1 接続の受けだけで 88 > 32。
- 直し方（どれかを選んで Q2 に書く）: (a) driver の受け（`alloc_for` の呼び手の種類）に jumbo の数 slot を予約し、socket 側の alloc（UDP・raw・copy）は予約を取れない。(b) socket の受けの queue に入る jumbo の数を socket ごとに上限（例 2）。(c) TCP は jumbo が少ない時に標準の slot に入る大きさ（≤ 1984 − hdr）で切る（MSS 以下ならいつでも合法）。(d) lo0 の datagram は標準の class に限る（lo0 の UDP は今も 1984 byte 付近までなので振る舞いは変わらない）。少なくとも (a)+(d) を推奨。`packet-buf-host-test` に「jumbo を socket が使い切っても driver の受けは取れる」を入れる。

### S8. 目標の実現性（RTL8156 の descriptor）を確かめる前に段を進めている。ws.md の完了の条件がユーザーの決定・設計と食い違う

- 根拠: §0・§4 の通り zedBSD の USB LAN は CDC ECM・NCM だけで、上限は configuration の `wMaxSegmentSize` と NTB parameter で決まる。RTL8156 の値は未確認（BUG-222 にも記録が無い。ECM の attach の log は `segment=` を出すが NCM は出さない: `usb-cdc-ncm-net.c` は値を保たない）。Pi 4 の adapter は不明。`plan/ws221/ws.md` の完了の条件 2 は「amd64 の実機の有線（5320・5330 で使う物）」、3 は「断片化・Path MTU」、4 は「QEMU と実機（Pi 4 の GENET、5330）で MTU 9000」と書くが、ユーザーの決定（GENET 1500）、設計（QEMU は 1514 固定、IPv4 PMTU は Future Work、断片化しない）、§4 の一覧（5320 が無い）と合わない。
- 起こる状況: CDC の configuration が 1514 を広告する chip（Realtek の CDC の mode が vendor の mode より小さい値を出すことはありうる: **推測**）なら、p002〜p005 の後に p006 で初めて「9000 は不可」と分かる。vendor の driver（r8152 相当）を書くのは新しい範囲で、GPL を読まずに作る大きな仕事。
- 直し方: p003 の前（できれば p002 と並行で、ユーザーの手元の Linux に挿して `lsusb -v -d 0bda:8156` 相当、または 5330 の zedBSD で NCM の attach に `segment=` を出す一行の log を先に入れる）に descriptor・NTB parameter・`bmNetworkCapabilities` を読む。§9 に「Q11: 完了の条件 2〜4 の書き直し（QEMU は口と回帰だけ、jumbo の受け入れは実機の USB LAN、5320 の扱い、IPv4 PMTU と断片化の扱い）」を足してユーザーに決めてもらう。

## 中

- **M1. 下げる変更の可否が §2 と §3.2 で矛盾。** §2「GENET・WiFi・ne2000・lgy98 は … `SIOCSIFMTU` は今の値以外を EINVAL」、§3.2 の 1・3「`mtu > mtu_max` で EINVAL、`set_mtu` が無ければ範囲の検査だけ」（= 下げるのは全 device で通る）。p002 の QEMU 試験 (2)（ECM を 1400 に）は p003 の前で ECM に `set_mtu` が無いので、§2 なら EINVAL で落ちる。直し: どちらかに決める。下げるのは全 device で許す（IP の層だけが使う。driver は frame の大きさを MTU から取っていない: GENET は定数 `GENET_MAX_PACKET`、rpi4-ethernet.c:86・1145）を推奨し、§2 を直す。WiFi（`wlan.c`）の MTU の使い方は p002 で読む。
- **M2. p002 の QEMU 試験 (3) が p004 の `ping -s` に依存。** §7 の p002 の受け入れは §8.2 の (1)〜(4)、(3) は `ping -s`、`-s` は p004。さらに ping は sendto の失敗を出さない（main.c:246〜256、ping6 は 494）ので (3) の「EMSGSIZE」は観測できない。直し: `ping -s` と送りの失敗の表示（`ping: sendto: Message too long`）を p002 に移すか、(3) を p004 に移す。
- **M3. `set_mtu` が RA の MTU を忘れる。** §3.2 の 5・§3.8 は `link.mtu = device->mtu` に戻し「RA が次に来れば再び下がる」。RA の間隔（数分〜30 分）の間、RA が 1500 と言う link に 9000 で送る。直し: interface に `ra_mtu`（0 = 無し）を持ち、`link.mtu = ra_mtu ? min(device->mtu, ra_mtu) : device->mtu`。ipv6.c:604 の「値 ≤ device->mtu」の条件で捨てていた RA の値（device が 1500 の時に RA が 9000）も保てる。
- **M4. 最小 MTU の規則に穴。** IPv6 は既定で全 interface に有効（ipv6.c:1625 `enabled = 1`）。IPv6 を切って MTU 576 にした後で `SIOCSIFINET6` で有効にすると `link.mtu` < 1280。直し: 最小を常に 1280 にする（推奨、単純）か、`SIOCSIFINET6` が MTU < 1280 の interface で有効化を EINVAL にする。
- **M5. Q8「`no mtu` で戻さない」は動いている状態と設定を食い違わせる。** `no mtu` の直後は 1400 のまま、再起動すると driver の既定 1500 になる。§8.2 の 6 はこの食い違いを期待の振る舞いにしている。直し: kernel が `mtu_default`（create の時の値）を持ち、`SIOCSIFMTU` の `ifr_mtu == 0` を「既定に戻す」にし、reconcile は key が消えた interface に `MTU IF 0` を出す（net は driver の既定を知らなくてよい）。
- **M6. net.tcpstats は binary の UAPI。** §3.1 の「sysctl の text なら末尾に足すだけ」は違う: `struct tcp_stats_header`（`include/uapi/sysctl.h:342〜353`、`_Static_assert` 48 byte、`struct_size` の欄）。直し: header を伸ばす（`struct_size` 48 → 64、version は 1 のまま読み手は `struct_size` を使う: `userland/base/sysctl/main.c:783・802`）、static assert と `uapi-abi-layout-check`（Makefile の check の一覧）、sysctl の表示、WS220 の `tcp-model/model.c:972〜977` を同時に直す。WS220 の所有の UAPI なので Q1 経由で WS220 と順を決める。あわせて `packet_buf_in_use`（`src/kern/resource.c:85` の資源の snapshot、漏れの検出に使う）に jumbo を含める。
- **M7. `tcp_device_mss` の所有が二重。** ws220/ws.md 末尾は WS220 p006・p007 の設計が `tcp_device_mss(device)` を使うと書き、phase.md §5 は WS221 p005 の interface に挙げる。WS220 p006 が先なので、作るのは WS220 p006、WS221 p005 は SYN の広告と受けの上限だけ、と所有を書き分ける（S3 の上限もそこで）。
- **M8. net.conf の互換の影響を狭く見ている。** net.conf を parse するのは `net`・networkd（`ipv6.c:613`、`lan-configure.c` は編集して書き戻す）・compositor の `libkeiland-backend-zedbsd/network-link-zedbsd.c:455`・`userland/tests/monitor-probe`。未知の key は file 全体の失敗（netconf.c:1379）。古い image に戻すと `mtu` のある file で **網の設定全体**が読めなくなる。同じ v1 のまま key を足した前例（ws130 の `ipv6` の節）があるので方針としては受け入れられるが、§3.7 に「rollback では網の設定全体が読めない」「`mtu` は設定した時だけ書く」と全ての読み手を書く。
- **M9. QEMU で NCM の jumbo を流す代替を検討していない。** host の Linux の USB gadget（`dummy_hcd` + configfs の NCM function）を host の USB device として作り、QEMU の `usb-host` で guest の xHCI に渡せば、実機なしで NCM の codec・binding・jumbo の data の経路を試せる可能性がある（Linux の f_ncm の `max_segment_size` の設定の口と上限、Debian 13 の kernel の `dummy_hcd` の有無は**記憶・未確認**）。sudo の利用は許可済み。§8 の「jumbo は実機だけ」の判断の前に可否を調べる価値がある（Q として足す）。
- **M10. networkd の DOWN は address を外す。** `NETWORKD_OP_DOWN` は `lan_hold_down`（`networkd/main.c:4057〜4072`、BUG-293）で、MTU の変更の commit（DOWN → MTU → UP）は address と SSH を含む全接続を落とす。§8.2 の 2・6 を SSH の中で行うなら、serial（`plan/tools/guest/serial.py`）で行うか、再接続を前提に判定の手順を書く。利用者への見え方（commit で接続が切れる）を `net` の文書と §9 に書く。
- **M11. `net_device_receive` の検査の置き場。** 関数は `core.c` でなく `src/kern/net/net-device.c:1155`。lo0（`loopback_transmit` が `net_device_receive` を呼ぶ、core.c:443〜454）と WiFi（`src/kern/net/wifi/wlan.c:1082・1433`）も通る。検査は「Ethernet の frame で `length > 14 + mtu`」に限り、`rx_dropped` でなく別の counter（または `rx_errors`）にして既存の「queue が満杯」の意味と分ける。

## 軽微

- L1. 行番号の古さ: tcp.c は WS220 p002 の merge で `payload[TCP_MSS]` 425→467、SYN の mss 1343〜1347→1396〜1400、`TCP_MSS` の切り 2244→2504、受けの window 2456→2732。Makefile の `managed-lan-host-test` は 471〜478→464〜470、`phase20-net-config-host-test` は今の main の Makefile に無い（§7 の注は消す）。
- L2. `usb-cdc-ncm.h` は `include/drivers/usb/usb-cdc-ncm.h`。callback の失敗で delivery が止まる約束は 131〜138 行（§3.10 の 103〜109 は control request の struct）。
- L3. §3.2 の 6「`kern_log`（net-device.c:1547 と同じ形）」の 1547 は `device_post_event`（system event に `ifindex=%u mtu=%u` を出す）。MTU の変更でこの event を出せば Settings（`network-link-zedbsd.c:316〜319` は `SIOCGIFMTU` を読むだけ）が更新の契機を得る。§3.2 の「通知は無し」と合わせて決める。
- L4. `protocol.h` は `userland/base/net/protocol.h`（op 21・field 24 は空き、確認済み）。networkd の op の権限の分類（変更の op は superuser か network group か）に `MTU` を入れることを書く。
- L5. Q10: 確かめた（`build/amd64/vmunix`）: `.net_bss` は `.data` と `.bss` の間の orphan の PROGBITS 0x8e000 byte、data の PT_LOAD に入る。amd64 の ASSERT（vmunix.ld:74）は `__kernel_phys_end`（`.bss` を含む）で数えるので、NOLOAD に移しても 16 MiB の予算は減らない（file の大きさだけ）。arm64 は `.vfs_bss`（5 MB）・`.kernel_heap` も orphan の PROGBITS（`build/arm64/kernel.elf`）なので、同じ直しをするなら一緒に。
- L6. NCM: `SET_MAX_DATAGRAM_SIZE` の後に `GET_MAX_DATAGRAM_SIZE`（0x87）で読み戻し、device が黙って小さくした値を `mtu_max` に使う。bit 3 の無い device で `wMaxSegmentSize` が 9014 を超える時、上限超えの datagram 1 つで NTB 全体の parse が失敗する（codec は NTB 全体を先に検査する）ので、`max_datagram_size` は bit 3 が無ければ `wMaxSegmentSize`（≤ 65535）まで受けて core で落とす。
- L7. `packet_buf_alloc_for` は「大きすぎる」（EMSGSIZE 相当）と「尽きた」（ENOBUFS）を呼び手が区別できる形に（例 out 引数か 2 つの関数）。UDP（udp.c:689〜699）は今も両方 ENOBUFS で、利用者の retry が無限になりうる。
- L8. free の配列の選び方は `pool_class` より pointer の範囲（`packet_pool` か `packet_jumbo_pool` の内か）の方が、memset 済みの packet や壊れた値に強い。
- L9. recv の staging（syscall.c:1961・2245）を 9216 にすると、全ての datagram の socket（AF_UNIX の syslog などを含む）の受けが `KERNEL_LARGE_THRESHOLD`（8 KB、`src/kern/entry.c:55`）を超えて page の確保になり、塞がって待つ間ずっと持つ。family ごとに（AF_UNIX は 2048）。
- L10. NCM の 32 KB の受けの URB は submit ごとに 32 KB の coherent DMA を取る（`pci-xhci.c:3736〜3746`、transfer reservation を使っていない）。Pi 4 での確保の失敗・遅さは**未確認**。
- L11. `ipv6_device_mtu_changed` を `net-device.c` から呼ぶと core が IPv6 に依存する。`SIOCSIFMTU` の handler（inet-socket.c）か weak の hook で呼ぶ（packet-buf.c の weak の作りと同じ）。最小 MTU の判定（IPv6 の有無）も同じ層で。
- L12. §8.2 の 3 の `ping -s 1372 10.0.2.2`: slirp が大きい ICMP の echo に応えるかは**未確認**。相手は tap の ue1 の host にする方が確か。§8.2 の 8 の SYN の MSS は ue1 の tap で取る、と書く。

## §9 の推奨の妥当性

| # | 評価 |
| --- | --- |
| Q1 | 推奨（down の時だけ）は保ってよいが、理由（URB の取り直し）は S5 の直しで消える。S1 の競合の直しが前提。M10 の「commit で接続が落ちる」を利用者への影響として書く |
| Q2 | 32 の根拠が不足（S7）。数より予約・上限の方針を先に決める |
| Q3 | M4 の穴。常に 1280 を推奨 |
| Q4 | S4 の通り「混在の LAN で困らない」は条件つき。完了の条件 3 と衝突するのでユーザーの判断が要る（Q11） |
| Q5 | 妥当 |
| Q6 | 妥当（ifconfig の文は「the interface's limit」で足りる） |
| Q7 | 妥当。ただし p002 の試験が使うので p002 へ（M2）、ping の送りの失敗の表示と動的な buffer も |
| Q8 | M5 の食い違い。kernel の既定に戻す口を推奨 |
| Q9 | 妥当。S8 の descriptor の確認を足す |
| Q10 | 妥当（L5 の注つき）。WS221 は依存しない |
| 欠落 | Q11（完了の条件の書き直し、S8）、Q12（QEMU の gadget の代替、M9）、Q13（jumbo の予約の方針、S7）、5320 の扱い |

## 確かめた点（source の事実、main `0992c5791`）

- packet-buf.h:24〜30・packet-buf.c の alloc・free・copy・copy_region・pool_usage は §1.1 の通り。`PACKET_BUF_STORAGE_SIZE` の使い手は syscall.c の 4 か所（1845・1961・2075・2245）と unix-socket.c:1850。
- net-device.h:34〜90（ops に MTU の callback 無し、`mtu` だけ）、net-device.c の create（154）・open（835〜913）・close（927〜978）・ioctl の admission（1057〜1113）・receive（1155〜1203）。
- inet-socket.c の SIOCSIFFLAGS（653〜666）・SIOCGIFMTU（673）・query の allow-list（1320〜1344）、UAPI の `struct ifreq` の `ifr_mtu`（int）、`0x8922` 未使用。
- ipv4.c:259〜265・346、icmp.c の echo の応答は受けた packet をその場で使う（678〜705、ICMPv4 の echo の応答は alloc しない）、raw の配送は copy_region（628）、icmp6.c の path MTU と purge、ipv6.c の link.mtu（1629）・RA（604〜605）・既定で有効（1625）、udp.c:682〜699、packet-socket.c:258〜275・142。
- ECM: 46〜47・54・182〜189・383〜388・597・1047・1101・1350・1626〜1640・1832〜1833・1861・1970。NCM: codec の 140・321〜322・441・412〜425、binding の 47〜61・407〜415・1354〜1384・2068〜2070・2195〜2199。GENET: 85〜86・431・1039・1145・1363。
- xHCI の bulk は per-submit の coherent bounce か reservation（3640〜3750）、`q`（4236〜4247）は control transfer の経路（推測を確かめた）。
- net.conf の読み手・書き手、`lan_send_policy`・`lan_policy_decode`・`lan_configure_steps`・networkd の UP/DOWN（`lan_hold_down`）、protocol.h の番号の空き。
- ping の buffer（main.c:60・432）と ping6 の recvmsg（592）、sendto の失敗を出さないこと。
- net.tcpstats の UAPI（sysctl.h:299〜446）と読み手（sysctl/main.c:759〜802）。
- amd64・arm64 の build 済みの kernel の section の配置（readelf、build/ は読むだけ）。

## 確かめていない点

- RTL8156 と Pi 4 の adapter の descriptor・NTB parameter・`bmNetworkCapabilities`（実機）。CDC NCM 1.0・ECM 1.2 の仕様の文言（記憶で書いた所: ZLP、SetMaxDatagramSize と bit 3）。
- ECM の受けの URB の溢れ（S5）を xHCI の driver が babble としてどう回復するか（7931 の経路の詳細は読んでいない）と、今の 1514 の arm で既に起きているか。
- `wlan.c` の frame の長さと MTU の使い方、`inet6` の socket の ioctl の経路（§3.2 の注の通り p002 で）。
- Linux の gadget（f_ncm の jumbo の上限）と Debian の `dummy_hcd`（M9、記憶）。
- slirp の大きい ICMP の扱い（L12）。radvd・家庭の router の RA の MTU option の既定（§3.3 の推測、radvd は既定で出さないと記憶）。
- 試験・build・QEMU は走らせていない（review だけ）。
