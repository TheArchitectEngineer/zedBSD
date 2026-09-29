<!-- awesome-plan project=zedbsd record=ws089-p003 -->

# ws089-p003: Network・Wi-Fi・Ethernet の頁

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ（ユーザーの方針「ネットワークを中心に」）

接続の状態、Wi-Fi の一覧と接続・切断（新しい network への鍵の入力を含む）、Wi-Fi の入り切り、Ethernet、address・netmask・MAC・DNS、
通信量（見本の Network の頁に近い内容）。

## 設計の決定

- **networkd の protocol は変えない**。調べると、`net wifi set-key` と同じ流れ（user の credential store に鍵を書く → `WIFI_PROFILES_CHANGED` →
  `WIFI_CONNECT`）で新しい network に入れる。案は [proposed/libkeiland-network-link.md](../proposed/libkeiland-network-link.md)（改訂済み）。
- 制約（networkd の既存の振る舞い）: 鍵は WPA の 8〜63 文字。open の network は store に入らないので「later version」と出す。
  Wi-Fi の owner が別の account のとき JOIN は EPERM（頁に「Wi-Fi を入れ直す」と出す）。

## 実装（main の許可 D4 の範囲）

- libkeiland（`KEILAND_VERSION` 10 → **11**。適用の直前に main の最新が 10 であることを確かめた。merge で main が番号を調整する）:
  - `include/libc/keiland.h`: `KEILAND_NETWORK_REQUEST_PROFILES`、`struct keiland_network_link`、`keiland_network_get_links`・`_get_dns`・
    `_save_key`・`_get_saved`。exports.map は既存の `keiland_network_*` で足りる（変更なし）。
  - 新しい `userland/desktop/libkeiland/network-link.c`（socket の ioctl、`/etc/resolv.conf`、`wifi-store`）。`network.c` に PROFILES の case 1 つ。
    `Makefile` に network-link.c と `userland/base/net/wifi-conf.c`・`wifi-store.c`（networkd・net と同じく source を共有）。
- settings: `network.c`（backend: watch・scan・join・鍵の join の 3 段・disconnect・switch、1 秒ごとの interface と通信量、5 秒ごとの DNS と
  保存済み）、`page-network.c`（Network・Wi-Fi・Ethernet の頁）、`widgets.c`（switch・button・点・signal・byte の表記・text field）、
  `ui.c`（頁が key を先に取る、control の位置の log `ZSETTINGS CONTROL`）、`pages.c`（3 頁を ready に）、`main.c`（network の poll と待ち時間）。
- 試験の stand-in（WS035 の試験 program、最小の追加）: `userland/base/tests/network-probe/main.c` が `WIFI_PROFILES_CHANGED` に答え、
  その後は "Neighbor 5G" にも profile があるとする（既存の zdesktop-p013 の振る舞いは変わらない）。
- 試験: `plan/ws089/tests/host-network.c`（host の偽の backend）、`host-render.c`（`--network=`・`text=`・`control=`）、
  `config-amd64-settings.mk` に network-probe、`settings-p003.sh`（guest）。

## 確認（2026-09-29）

- build（worktree の `build/amd64`、-Werror）: libkeiland・settings・image → exit 0、warning 0。規約: style-check 0（settings・network-link.c・
  network-probe）。`git diff --check` 0。全文の手の照合は p006。
- host: Network（wifi・absent 860x640）・Wi-Fi（鍵の行）・Ethernet を描いた（`build/ws089-shots/host/net*.png`・`wifi-key.png`・`eth.png`）。
- **QEMU（Venus の guest）**: `plan/ws089/tests/settings-p003.sh` → **PASS**。
  1. 本物の networkd（有線 ue0、Wi-Fi 無し）: Network の頁に Connected・ue0・10.0.2.15 /24・DNS 10.0.2.3、Details で Ethernet（MAC・MTU・送受の量）、
     Wi-Fi は「no Wi-Fi radio」。
  2. stand-in（network-probe、QEMU だけの偽物）: scan 3 件、保存済みの Kei Lab を押して join（op=35、Connected）、Neighbor 5G で鍵の行 →
     鍵を打って Enter → `save-key ok`（`/etc/wifi.conf` に SSID）→ op=37 → op=35 → Connected、Disconnect（op=36）、switch off（op=33）・on（op=32）。
  3. zdesktop の log に ERROR 無し、networkd の socket を戻して `net show` が online。
- 画面（目で確かめた）: `build/ws089-shots/p003/network-real.png`・`ethernet.png`・`wifi-absent.png`・`wifi-list.png`・`wifi-joined.png`・
  `wifi-key.png`・`wifi-key-joined.png`・`wifi-off.png`・`network-probe.png`。
- 起動の確認: `plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS（`build/ws089-boot-test/login.png`）。
- 実機: 未実施（Wi-Fi の実機の join は実機で確かめる必要がある。QEMU の Wi-Fi は stand-in の偽物）。

## 残り

- 実機の Wi-Fi（5330 の AX211 等）での scan・join・鍵の保存の確認（未実施）。
- 通信量の graph は窓を開いてからの 2 分。過去 24 時間は daemon の記録が要る（Future）。
- touch の drag の scroll は p006。
