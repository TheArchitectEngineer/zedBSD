<!-- awesome-plan project=zedbsd record=ws035p104 -->

# ws035-p104: graphical な session の user を `network` の group に

Phase ID: `ws035-p104`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 1。p013 の残り）

## 範囲

2026-09-28 ユーザー:「セッションユーザはnetworkグループに追加してOKです。」→ sessiond が起こす graphical な session の user を
`network` の group に入れ、システムバーの network の menu（p013）が root でない user でも動くようにする。networkd の socket は
`root:network` の 0660 で、SHOW・SUBSCRIBE と Wi-Fi の操作を socket の member に許している（networkd の `operation_allowed`）。

## 実装（2026-09-28）

- `userland/desktop/sessiond/session.c`: `session_child` の `initgroups` の後に `session_network_group()`。`getgrnam("network")`
  で group を引き（無い system では何もしない）、`getgroups` の一覧に無ければ足して `setgroups`。失敗は syslog の警告だけで、
  login は止めない（menu が使えないだけ）。その後に `setgid`・`setuid`。`/etc/group` は変えない（session の process だけが
  group を持つ。console や ssh の login の user には与えない）。
- 試験: `plan/ws035/tests/config-amd64-graphical-network.mk`（graphical な login の image に network-probe）、
  `build-login-image.sh` の第 2 引数 `graphical-network`、`plan/ws035/tests/zdesktop-p104.sh`（新）。

## 検証（amd64、Venus の guest、graphical な login の image `build/ws035-d3`、2026-09-28）

- `zdesktop-p104.sh` PASS: guest に普通の user `kei`（uid 1000、`id kei` は `groups=1000(kei)` だけ、空の password）を足し、greeter を
  起こし直して kei で login（`SESSIOND SESSION start user=kei uid=1000`）。
  1. 本物の networkd（QEMU の有線、無線なし）: session の zdesktop（uid 1000）が `ZWL NETWORK state reachable=1 connected=1
     kind=wired ... wifi=absent`、menu に「Wired (ue0): connected」。
  2. 偽の networkd（**QEMU だけの偽装**。network-probe の socket を networkd と同じ `root:network` 0660 にした）: kei の session で
     scan 3 件、「Kei Lab」の join（`WIFI_CONNECT`）、Wi-Fi の switch（`WIFI_DISABLE`）。終わりに本物の socket を戻し `net show` が答える。
- 画面: `build/ws035-shots/p104-20260928-wired-menu.png`・`-list.png`・`-joined.png`・`-off.png`。
- 回帰 PASS: zdesktop-p102（sessiond の session: login・lock・解除、root）。
- build warning 0（sessiond）。`style-check.py userland/desktop/sessiond/session.c` 0 件。
- 未実施: group が無いときに menu が「Network service not available」になる否定の確認（guest に su が無い）。実機。

## 残り

- なし（p013 の「session の user は networkd の socket に入れない」はこの Phase で解消）。
