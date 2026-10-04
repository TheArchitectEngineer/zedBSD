<!-- awesome-plan project=zedbsd record=ws099-p032 -->

# ws099-p032: system bar の WiFi の icon の Alt+クリックで IP address と統計の情報を出す

Status: planning
Disposition: normal
Parent: [WS099](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、原文）

「画面右上通知領域のWiFiアイコンをAlt＋クリックすると、IPアドレスや統計情報が出るとうれしいです。」

## 範囲

1. 右上の通知領域の WiFi の icon を **Alt を押しながらクリック**すると、普段の menu（AP の一覧）の代わりに詳しい情報の popup を出す（macOS の Option+クリックに当たる）。
2. 出す情報の案（設計で決める）: interface の名前、SSID・BSSID、IPv4 の address・mask・router・DNS、IPv6（zedBSD ではまだ無い）、MAC address、channel・周波数・band、信号の強さ（dBm）、送受信の速さ（PHY の rate）、送受信の packet・byte の数と error の数、接続してからの時間。
3. 経路: compositor の中の system bar → libkeiland-backend（OS ごとの network の情報の口、zedBSD は networkd・`net`・kernel の統計）。Settings の Network の頁の情報（[ws089-p022](../../ws089/phase022/phase.md) の Ethernet の読むだけの項目）と同じ口を使う。値の更新（popup を開いている間は 1 秒ごとなど）。
4. 有線の icon（ある場合）にも同じ Alt+クリックを付けるかは設計で決める。

## 受け入れ（案）

- Alt+クリックで情報の popup が出て、値が `net`・`ifconfig` 相当の出力と合う（QEMU は T1、実機は UAT）。普通のクリックは今の menu のまま。
- C の全文の規約、build warning 0、OS の境界の checker。
