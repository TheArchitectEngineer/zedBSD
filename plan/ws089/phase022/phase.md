<!-- awesome-plan project=zedbsd record=ws089-p022 -->

# ws089-p022: Settings の Ethernet の頁で設定を読み書きできるように（compositor 経由、libkeiland-backend が net の command で設定）

Status: in-progress（実装済み、T1 待ち。MTU の範囲は朝のユーザーの判断待ち）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q728（P2、2026-10-05）

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「SettingsでEthernetタブで、設定ができない。Waylandコンポジタ経由で設定を取得・設定でき、libkeiland-backendがnetコマンドで設定するのがいい。設定項目は、 IPv4: DHCP-or-Static/Address/Mask/Router, IPv6: Auto-or-Static/Address/Router, MTU, DNS 1/2. MACアドレス等はread only」

## 範囲

1. Settings の Ethernet の頁で、有線の interface ごとに次を表示・変更できるようにする。
   - IPv4: DHCP か Static、Address、Mask、Router
   - IPv6: Auto か Static、Address、Router
   - MTU
   - DNS 1・DNS 2
   - 読むだけの項目: MAC address など（link の状態・速度・interface の名前は設計で決める）
2. 経路（ユーザーの案）: Settings → libkeiland（`kl_system_*`）→ compositor の拡張（`kl_system_manager_v1`）→ libkeiland-backend → **`net` の command**（networkd の CLI）で設定する。取得も同じ経路。app は OS の口を直接持たない（Guardrail の「配置」「app と設定」）。backend の OS ごとの実装（zedBSD は `net`、Linux・FreeBSD は設計で決める。対応しない OS では読むだけか、非対応の表示）。
3. 入力の検査（address・mask・router の形、MTU の範囲、DNS）、適用の失敗の表示、DHCP に戻す操作。設定の永続化は networkd の設定（netconf）に任せる。
4. 権限: 有線の設定の変更を誰に許すか（WiFi の `network` group の規則（Guardrail 2026-10-02）と揃える案）を設計で決める。

## ユーザーの決定（2026-10-04 夜）

「IPv6はまだないので、zedBSDでは設定できない項目にします。Linux, FreeBSDでは設定できてよいと思います。」→ IPv6 の欄は zedBSD では設定できない項目（灰色・非対応の表示、値が取れれば読むだけ）。Linux・FreeBSD の backend では IPv6 も設定できる（各 OS の仕組みで）。

## 依存と注意

- IPv6 の Static と Auto: zedBSD の IPv6 は [WS130](../../ws130/ws.md)（計画だけ、実装はベータ2 以降）。zedBSD では上の決定のとおり設定できない項目にする。
- 後挿しの USB LAN（[BUG-168](../../bugs/BUG-168.md)・[BUG-169](../../bugs/BUG-169.md)、q685）と同じ有線の経路。
- C の全文の規約、build warning 0、OS の境界の checker、QEMU（virtio-net・usb-net）は T1、実機は UAT。

## 設計（2026-10-05、P2。Q1 の回答 (a)(b) を含む）

- **経路**: Settings の Ethernet の頁 → libkeiland `kl_system_network_configure_wired`（KL_VERSION 29）→ compositor の `kl_system_network_v1` の request 5 `configure_wired`（**manager の version 6**、WS113 の displays は 7 に繰り下げ（Q1））→ libkeiland-backend `kl_backend_network_configure_wired` → **networkd の新しい op `NETWORKD_OP_LAN_CONFIGURE`（50）**。同じ op を `net lan set IF dhcp [--dns A[,B]]` / `net lan set IF static ADDRESS NETMASK [ROUTER] [--dns A[,B]]` も送る（「net コマンドで設定」）。backend は desktop で networkd の protocol を知る唯一の場所のまま。
- **権限**（Q1 (a)、ユーザーへの確認は master の pending）: Wi-Fi と同じく network の group の member と root。networkd は全ての field を厳しく検査する（`userland/base/networkd/lan-configure.c`: interface は小文字と数字 1〜15 字で lo・wlan でない、さらに networkd が存在し loopback でも radio でもないことを確かめる。address・netmask・router・DNS は dotted IPv4 の厳密な形（先頭の 0・符号・空白を拒否）、netmask は連続の /1〜/30、address は機械の持てる address で subnet の自身・broadcast でない、router は同じ subnet の別の address、DNS は 2 つまで、DHCP は netmask・router を持たない）。確認の取引（confirmed commit）の間は EBUSY。結果は全て syslog（`LAN_CONFIGURE ... result=ok` / `result=error errno= reason=`）。
- **永続化**: networkd が net の `netconf.c` で `/etc/net.conf` のその interface の entry だけを書き換え（無ければ足す）、writer の lock の下で atomic に保存する。default route は「その interface の subnet（前と今）に router がある default route」だけを消し、static の router があればそれに置き換える（他の interface の route は残す）。DNS は名前があれば static、無ければ DHCP の mode。
- **適用**: networkd の有線の policy の該当 interface を置き換え（`networkd_lan_set_interface`、cable があれば再設定）、LAN の管理が有効なら背景の worker が address を設定、無効なら直ちに ifconfig か dhcpc。router が指定されれば default route を置き換え、DNS が指定されれば resolver を書く。
- **表示**: backend の link 読みが有線の interface ごとに設定の mode（net.conf、無い interface は DHCP: networkd の既定と同じ）と default route の router を足し、compositor が details の link の後に event 9 `wired(name, mode, router)` を送る。
- **Settings**: Ethernet の頁で各有線の interface の下に IPv4 の card（Configured by・Router・IPv6「Not available on Kei yet」、Edit、static なら Use DHCP）。Edit で DHCP/Static の選択、IPv4 address・Subnet mask・Router・DNS 1・DNS 2 の欄（DHCP では DNS だけ有効、他は「From DHCP」）、IPv6 は灰色、Apply・Cancel、Tab・Enter・Esc。欄は数字と点だけ、15 字まで。Apply の前に Settings 側でも検査（`wired.c`）。結果の文言（成功・network group でない・値の拒否・非対応・busy・service 無し）。
- **IPv6**（ユーザーの決定）: zedBSD では設定できない項目（灰色）。Linux・FreeBSD の backend は ENOTSUP の stub（ベータ1 の規則: Linux・FreeBSD の backend は後）。
- **MTU**（Q1 (b)）: 読むだけ（link の card に表示）。zedBSD の stack に MTU を設定する口（SIOCSIFMTU・driver）が無いため、設定は別の WS の候補。要望は「設定できる」なので、範囲を減らす判断として朝にユーザーに確かめる（Q1）。**この点が決まるまで p022 は cleared にしない。**

## 確認（2026-10-05）

- host: `plan/ws089/tests/run-host-lan-configure.sh` PASS（ASan・UBSan、良い構成 5、不正な入力の拒否 35（interface の名前 7、address 9、netmask 5、subnet 2、router 4、DHCP の netmask・router 2、DNS 4 ほか）、net.conf の編集（新しい entry、static の address と default route、router 無しで自分の route を消す、他の interface の route を残す・自分の router で置き換える、DHCP で自分の route を消す、DNS の mode）、書き出しと読み戻し、netconf_validate）。`run-host-wired.sh` PASS（Settings の検査と Apply・答え・入力の制限）。`plan/ws131/tests/host-system.sh` PASS（configure_wired の全 field が backend に届く、答え、拒否、不正な mode）。`plan/ws089/tests/host-slot.sh` PASS、`make managed-lan-host-test` PASS、`plan/ws033/tests/host-bug169.sh` PASS。settings-render で Ethernet の頁の card と editor（不正な router の message）を描いて目視。
- build（warning 0）: zedBSD の networkd・net・wayland・settings・files、Linux の Keiland（-Werror）。`keiland-os-boundary/check.sh` PASS。style-check: 新しい file は違反 0、変えた既存の file に新しい違反 0。
- QEMU（T1 に依頼）: `plan/ws089/tests/settings-p022.sh`（QMP で 2 つ目の usb-net を足し、root・network の member・非 member の `net lan set`、不正な入力の拒否、net.conf・resolv.conf・syslog、Settings の Use DHCP が compositor・backend 経由で networkd に届き net.conf が dhcp に、PNG 2 枚）。
- 未実施: 実機（UAT）、FreeBSD の Keiland の build（Linux の build で stub を確認）。
- 既知の制限: DHCP に DNS を名前で指定した時、後の dhcpc が resolver を書き換える可能性（dhcpc の DNS の扱いは未確認）。router を指定しない static への変更では、今動いている default route は消さない（net.conf からは自分の subnet のものを消す: 次の起動で揃う）。

## T1-155 の FAIL と直し（2026-10-05、P2）

- `resolv.conf names the server`: networkd は書いたが、直後の有線の worker の `apply_network_preference` が ue0 の lease の resolver で上書きした（実装の不足）。→ networkd が LAN_CONFIGURE で指定された DNS を覚え（`lan_static_dns`）、network preference が lease の resolver を選んだ後にも書く（指定が無い構成で解除）。
- `a member of the network group may` ほか: Settings の image に `su` が無かった（`sh: su: not found`、試験の前提）。→ `plan/ws089/tests/config-amd64-settings.mk` に `su` を足した。`bad input changed nothing`（.21 を期待）と Settings の `wired-link ... address=10.0.9.21` はこの連鎖。
- Settings の Use DHCP が届かない: ue1 の IPv4 の card が窓の下（ethernet-static.png で ue0 の card だけ見える）で、click が窓の外だった（試験の誤り）。→ 試験は wheel で頁を送り、Use DHCP（34N）が窓の中に入ってから click する。
- 加えて: 2 つ目の usb-net の netdev を `net=10.0.9.0/24` にした（Use DHCP の後に ue1 が ue0 と同じ 10.0.2.0/24 を取り SSH を乱さないように）。
- 確認: networkd の build（warning 0）、style-check の新しい違反 0。QEMU は T1 の再試験（image の作り直しが要る: config に su）。
