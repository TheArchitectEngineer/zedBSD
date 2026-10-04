<!-- awesome-plan project=zedbsd record=ws099-p032 -->

# ws099-p032: system bar の WiFi の icon の Alt+クリックで IP address と統計の情報を出す

Status: in-progress（2026-10-05 P1 generation17 / q718-i01。設計・実装・build・host の試験まで。QEMU の試験を Q1 経由で T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: q718 / q718-i01（Q1 の投入）

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

## 設計（2026-10-05 P1）

- **開き方**: system bar の network の icon（Wi-Fi の棒、有線の木、未接続の淡い棒のどれでも）を **Alt を押しながら左クリック**すると、menu の代わりに「詳細」の panel が menu と同じ場所・同じ glass で出る。普通のクリックは今の menu のまま。有線にも同じ Alt+クリックを付ける（icon は一つなので分けない）。詳細が出ている間は、どこを押しても（icon も）・Esc でも閉じる。押した button は下へ渡さない。
- **出す物**（値の分からない行は出さない）: 題（Wi-Fi・Ethernet、未接続なら Network）、Network（Wi-Fi の SSID）、Interface、Status（Connected・Not connected・Network service not running）、IPv4 address、Subnet mask、DNS（二つまで）、MAC address、MTU、Signal（接続中の SSID の最後の scan の dBm）、Received・Sent（interface が上がってからの byte 数と、前の読みからの速さ）。
- **interface の選び方**: 接続を運んでいる物、無ければ Wi-Fi の物、無ければ有線の物、無ければ loopback でない最初の物。
- **読み方**: interface と DNS は libkeiland-backend の `kl_backend_network_get_links`・`_get_dns`（Settings の Network の頁と同じ口）を system の拡張の thread（既存の details の読み、system.c）で読む。開いた時と、出ている間は 1 秒ごと。event loop では読まない。速さは前の読みとの差（counter が戻った時・interface が変わった時は出さない）。
- **出さない物**（今の backend に口が無い。必要なら backend・kernel・networkd の追加の別 Phase）: router（default gateway）、BSSID、channel・周波数・band、PHY の rate、packet・error の数、接続してからの時間、IPv6（zedBSD に無い）。

## 実装

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/network-info.c`・`.h`（新） | 行を作る純粋な関数 `zwl_network_info_build`（state・scan・links・DNS・時刻と前の sample から）と `zwl_network_info_bytes`（B・KB・MB・GB）。描画も状態も持たないので host で試せる |
| `network.c` | `info_open` など。Alt+左クリックで `network_info_open`（題だけで開き、すぐ details の読みを頼む）、`zwl_network_tick` で出ている間 1 秒ごとに読みを頼む、`zwl_network_details` で行を作り直す（log `ZWL NETWORK info rows=N` と各行 `ZWL NETWORK info row label=… value=…`）、`network_info_draw`（menu の影と glass、題と線、label を左・値を右寄せ）、押す・Esc で `network_info_close`（`ZWL NETWORK info close via=click/key`）。`zwl_network_is_open` は詳細の間も 1 |
| `system.c` | details の読みの結果を bar に渡す所で `zwl_network_details` も呼ぶ |
| `input.c`・`zwl.h` | `zwl_input_alt_held`（左右の Alt） |
| `Makefile`・`Makefile.linux`・`Makefile.freebsd` | `network-info.c` を追加 |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make … BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| FreeBSD | **未実施**（build 環境が無い。source の一覧に 1 file 足しただけ） |
| `sh plan/tools/keiland-os-boundary/check.sh` | PASS |
| `sh plan/ws099/tests/host-network-info.sh`（ASan・UBSan） | 31 checks passed: 接続中の Wi-Fi の 12 行（DNS は 3 つのうち 2 つ、接続中の SSID の dBm）、1 秒後の速さ、戻った counter は速さ無し、有線（Network・Signal の行無し、0 の MAC の行無し、interface が変わって速さ無し）、未接続（Wi-Fi の interface、Not connected）、daemon 無し、interface 無し、capacity、単位 |
| style-check（新しい file、変えた hunk） | 指摘 0 |
| QEMU（`plan/ws099/tests/p032-guest.sh BUILD`、pen の guest） | **未実施**。T1 に依頼（Q1 経由） |
| 実機（5330 の Wi-Fi） | **未実施**（UAT） |

## QEMU の試験（T1 への依頼の内容）

- image: `plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-pen.mk BUILD`、起動 `plan/ws079/tests/pen-guest.sh start IMAGE`（QEMU の user network、USB の adapter で有線）。
- 試験: `plan/ws099/tests/p032-guest.sh BUILD [OUTDIR]`（BUILD/bin/wayland を写す）。QMP で Alt を押したまま icon をクリック、2 秒待つ、Esc、普通のクリック、Esc。
- 合格: 全行 ok（info-open、no-menu、title-ethernet、status、interface、address が `ifconfig -a` と一致、bytes、rates、esc-closes、plain-click-menu、alive、no-error）。`details.png` をユーザーに見せる。

- 2026-10-05 T1 の結果: log の確認は PASS、details.png が console の文字の画面（QMP screendump は GL の scanout を写さない）。zdesktop-p013 と同じ VNC からの撮影（`plan/ws035/tests/zdesktop-check.py`）に替えた。PNG の再撮影は T1。

## 残り

- QEMU の結果の判定。実機の Wi-Fi（SSID・Signal の行）は UAT。
- router・BSSID・channel・PHY rate・packet/error・接続時間は backend の口を足す別 Phase（必要ならユーザーの判断で）。
