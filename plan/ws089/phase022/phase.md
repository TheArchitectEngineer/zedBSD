<!-- awesome-plan project=zedbsd record=ws089-p022 -->

# ws089-p022: Settings の Ethernet の頁で設定を読み書きできるように（compositor 経由、libkeiland-backend が net の command で設定）

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定

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

## 依存と注意

- IPv6 の Static と Auto: zedBSD の IPv6 は [WS130](../../ws130/ws.md)（計画だけ、実装はベータ2 以降）。networkd・kernel が IPv6 の設定を受けられるかを p001 の調べで確かめ、出来ない間は IPv6 の欄を読むだけか非対応の表示にする（範囲の決めはユーザーに確かめる）。
- 後挿しの USB LAN（[BUG-168](../../bugs/BUG-168.md)・[BUG-169](../../bugs/BUG-169.md)、q685）と同じ有線の経路。
- C の全文の規約、build warning 0、OS の境界の checker、QEMU（virtio-net・usb-net）は T1、実機は UAT。
