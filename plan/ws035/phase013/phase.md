<!-- awesome-plan project=zedbsd record=ws035p013 -->

# ws035-p013: システムバーの network の表示と操作（libkeiland 経由）

Phase ID: `ws035-p013`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「ws035-p013: the system bar shows WiFi/network state and lets the user act on it」）

## 範囲

システムバーの右の status（前は 4 本の棒のハリボテ）を本物にする。接続の有無、有線か Wi-Fi（SSID）かを表示し、
クリックで menu を開いて network を選ぶ・Wi-Fi を入り切りする。状態は **libkeiland を通して**取る（p042。zdesktop は
networkd の protocol を話さない）。networkd に足りないものは最小限に足す。QEMU には Wi-Fi が無いので、有線の状態は本物の
networkd、Wi-Fi の一覧と操作は偽の networkd（試験用）で確かめる。

## 実装（2026-09-28）

- **networkd**（`userland/base/networkd/main.c`）: SUBSCRIBE の frame（`net show` と同じ interface の行）の最後に Wi-Fi の行
  `wifi state=NAME interface=IF ssid=HEX radios=N` を足した（`watch_state`）。SSID は managed の接続が join 中・接続中・再接続中の
  ときだけ、byte 列なので 16 進（無いときは `-`）。`radios` は WLAN の interface の数（無線の無い機械と Wi-Fi が off の機械を分ける）。
  `net watch` の出力にもこの行が加わる（加えるだけ）。
- **libkeiland**（`userland/desktop/libkeiland/network.c`、`include/libc/keiland.h`、`KEILAND_VERSION` 8）:
  `keiland_network_open/close/update/get_state/get_scan/request/get_request`。SUBSCRIBE の接続を保って状態を読み、要求
  （scan = `WIFI_LIST`、join = `WIFI_CONNECT`、disconnect、Wi-Fi on/off）は 1 本ずつ別の接続で送る。**待たない**: `update` は
  poll 0 で届いたものだけを読む。daemon が居ない・切れたときは 1 秒ごとに watch を作り直す。状態は reachable・connected・
  kind（none・wired・wifi）・interface・wired（有線で address のある interface）・wifi（absent・off・searching・connecting・
  connected・disconnected）・ssid。scan は SSID ごとに一番強い AP、強い順（最大 24）。networkd の protocol を知るのはこの file だけ
  （`userland/base/net/protocol.c` を library に入れ、exports.map で `keiland_network_*` だけを出す）。
- **zdesktop**（`userland/desktop/wayland/network.c`、`shell.c`・`seat.c`・`zwl.h`・`glass.h`、link に libkeiland）:
  - icon: 有線は 3 つの箱の木、Wi-Fi の接続は強さの棒（scan にあればその強さ）、それ以外は薄い棒、Wi-Fi off は棒に横線。
  - icon のクリックで menu（System Menu の popup と同じすりガラス）: Wi-Fi の switch と状態の行（Connected to …・Joining …・
    Searching…・Not connected・Wi-Fi is off・No Wi-Fi hardware・Network service not available）、network の一覧（接続中に check、
    鍵の要るものに南京錠、強さの棒）、有線の行、接続中は「Disconnect from …」、失敗の行（profile の無い join は
    「Could not join X: no saved profile」）。開いたときに scan を頼む。行のクリックで join・switch・disconnect、外のクリックと
    Esc で閉じる（外のクリックは下の窓へ渡さない。System Menu と同じ）。menu が開いている間は look を still にしない（damage）。
  - 試験のための log: `ZWL NETWORK state|icon|open|close|scan|request|done|menu|row`。
- **試験の道具**: `userland/base/tests/network-probe`（package の既定は n）: networkd の socket で答える偽の daemon（wlan0 と
  3 つの network「Kei Lab」「Cafe Guest」「Neighbor 5G」、最後は profile 無しで ENOENT）。`plan/ws035/tests/config-amd64-network.mk`・
  `build-network-image.sh`（files の image に network-probe）、`zdesktop-p013.sh`。

## 検証（amd64、Venus の guest、2026-09-28）

- `plan/ws035/tests/zdesktop-p013.sh`（新）PASS（image `build/ws035-d2`、`build-network-image.sh`）:
  1. 本物の networkd（QEMU の有線 ue0、無線なし）: `state ... kind=wired interface=ue0 wifi=absent`、icon は有線の木、menu は
     「No Wi-Fi hardware」「Wired (ue0): connected」、外のクリックで閉じる。
  2. 偽の networkd（**QEMU だけの偽装**。networkd の socket を脇へ移し、終わりに戻して `net show` が答えることを確認）:
     scan で 3 件、「Kei Lab」の join で `kind=wifi interface=wlan0 wifi=connected ssid=Kei Lab`（icon が Wi-Fi の棒、check、
     Disconnect の行）、「Neighbor 5G」は error で失敗の行、switch で `WIFI_DISABLE` と `wifi=off`。zdesktop の log に ERROR なし。
- 画面: `build/ws035-shots/p013-20260928-wired.png`・`-wired-menu.png`・`-list.png`・`-joined.png`・`-failed.png`・`-off.png`。
- 回帰 PASS: zdesktop-p062（システムバーへの docking、同じ shell.c の bar）。
- 規約: style-check は新しい file（zdesktop/network.c、libkeiland/network.c、network-probe/main.c）が 0、networkd の足した
  関数（`watch_state`・`watch_ssid_known`）も 0。build warning 0（新しい code）。
- 実機: 未実施（本物の Wi-Fi の radio での表示・join は未確認）。

## 残り（follow-up）

- profile の無い network の join（password の入力の欄と profile の保存）。今は「no saved profile」と言うだけ。
- session の user は networkd の socket（`network` 群、0660）に入れない。greeter の session の user を `network` 群に入れるか、
  SHOW・SUBSCRIBE を誰にでも許すかの判断が要る（試験は root の zdesktop）。
- 失敗の行が長いと切れる（menu の幅 300）。menu の keyboard 操作（矢印・Enter）は無い。
- watch の fd を zdesktop の poll に入れていない（10 ms の tick で読む）。
- icon の強さは scan の値（接続中の RSSI を networkd が出していない）。
- 音量（p026）は別の Phase。
