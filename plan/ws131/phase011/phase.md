<!-- awesome-plan project=zedbsd record=ws131-p011 -->

# ws131-p011: Settings を拡張の経路へ、毎秒の監視の除去、libkeiland の OS を 0 に

Status: in-progress（q659、P2、2026-10-04。実装・host の試験・zedBSD と Linux の build・checker 済み。FreeBSD の build と QEMU は試験の担当に依頼）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q659（Q1 の「p011 へ」、2026-10-04。WS135 で済んだ設定の部分を除く）
依存: p010 cleared。D8 の単独走行で番号の順（p010 の次）。判断 D6（承認済み）。**この Phase の cleared の後に、Q1 がユーザーに進み具合を報告する区切り**（D8）
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/settings/`（`look.c`・`network.c`・`sound.c`・`page-home.c`・`page-input.c`・`page-network.c`）、`userland/desktop/wayland/preferences.c`（監視の除去）、`userland/desktop/libkeiland/`（`system-compat.c`・`preferences.c` の除去、`keiland.h`、exports.map、Makefile 3 本）、`plan/ws131/`。（Q1 の委任が要る: Q1 が Phase の前に委任を記録する）: `plan/ws089/tests/settings-p007.sh`・`host-network.c`・`host-slot.c`・`host-preferences.*`、`plan/ws100/tests/host-audio.*`、`plan/tools/keiland-linux/network-probe.c`・`audio-probe.c`・`lib-smoke.c`

## 目的と結果

Settings の設定・WiFi・音量を `kl_system_*` に替え、**同じ Phase で** compositor の毎秒の `stat` の監視（`wayland/preferences.c:237-360`、BUG-125 の原因）を除く（review 1）。libkeiland から旧 `keiland_network_*`・`keiland_audio_*`・`keiland_preferences_*` と backend の同居を除き、compositor が libkeiland から使う物を描画の層と motion だけにする（D4、link は残してよい）。

## 範囲

1. Settings: `look.c` → `kl_system_settings_*`、`network.c`・`page-network.c` → `kl_system_network_*`（詳細は `query_details` の非同期）、`sound.c`・`page-home.c`・`page-input.c` → `kl_system_audio_*`（`service`）。Keiland 以外の compositor では該当の頁を「この desktop では使えない」に（design.md §4.1 の 6）。
2. compositor: 監視の thread と `PREFERENCES_CHECK_MS` を除く。`settings-p007.sh` を「拡張で変えて数秒で適用」に書き換える（委任）。
3. libkeiland: 旧 API・`system-compat.c`・`preferences.c` を除く。backend の同居を外す。B3 を強める。
4. 道具の向け直し（review 6、委任）: `network-probe.c`・`audio-probe.c` は backend の API を直接使う形に、`lib-smoke.c` は `kl_system_*` の有無に、`host-network.c`・`host-slot.c`・`host-preferences.*`・`host-audio.*` は backend と compositor の store の試験に。
5. 書き手の Guardrail の文を Q1 に渡す（design.md §3.7）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- app に `keiland_network_`・`keiland_audio_`・`keiland_preferences_` の呼び出しが 0。compositor が使う libkeiland の symbol が D4 の許可の表の中だけ（B2、3 OS の `nm -u`）。`preferences.c` に周期の監視が無い。
- zedBSD: `settings-regress.sh`、書き換えた `settings-p007.sh`、`volume-p004.sh`・`volume-p005.sh`、C9 の `zdesktop-p076.sh` を単独で 20 回（結果は Q1 が BUG-125 に追記）、boot-test。Linux: Settings の起動と WiFi の状態の表示（hwsim）、向け直した `network-probe`・`audio-probe`・`lib-smoke`。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS089（`settings/page-*.c`）・P1（`settings/network.c`）と取り合う。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## 実施（q659、P2、2026-10-04）

### 範囲の扱い（Q1 の指示）

- WS135 で済んだ設定の部分は除いた: 毎秒の監視の除去、`keiland_preferences_*` の除去、`settings-p007.sh` の書き換え。Settings の設定は WS135 の `kl_settings_*` のまま。
- review 7 のうち system bar の同期の `save_key` と `get_saved` の非同期化を含めた（Q1 の指示）。

### 実装

- Settings（`userland/desktop/settings/`）:
  - `system.c`（新しい file）: window の display で `kl_system_open` する。main loop の毎 round に一度 dispatch し、変化の bit を `app->system_changed` に置く。答えは network に渡し、network の物でなければ log に出す。
  - `network.c`: `kl_system_network_*` に替えた。
    - state と scan は compositor の通知で受ける。
    - links・dns・saved は `query_details` で毎秒受ける（活動の graph の 1 秒の sample を保つ）。
    - 鍵の join は `save_key` の一つの要求にした（鍵は form に残し、送る時に渡す）。
    - Settings 自身の要求が出ている間の slot は今までどおり。
    - compositor が busy を返した時（system bar の要求が出ている）は、slot で 500 ms 待って送り直す。
    - join の失敗の文（no key・key refused・out of reach・EPERM）は保った。
  - `sound.c`: `kl_system_audio_*`（状態・左右の音量・feedback）にした。音量を `kl_settings` の `sound.volume` で送る経路は除いた。
  - `page-network.c`・`page-home.c`・`page-input.c`: `kl_*` の型と定数にした。hardware の address は文字列で受ける。拡張が無い desktop では「not available on this desktop」と出す（design.md §4.1 の 6）。
  - `settings.h`: 型・`SE_NETWORK_NONE`・`SE_NETWORK_SAVE_KEY`・`SE_JOIN_KEY` を足した。3 つの Makefile に `system.c` を足した。
- compositor:
  - `wayland/system.c`:
    - details を daemon の要求の slot から外した。待っている object の一覧を持ち、network の thread が空いた時に一度読み、全員に送る。鍵の保存を先に行う。
    - 鍵の保存は network の thread が空くまで待つ（`QUEUED`）。
    - network の結果に `no_key`・`refused`・`unreachable` を足した。
    - system bar 用に `zwl_system_bar_save_key`（鍵の保存 → PROFILES → JOIN。join の答えは system bar の自分の join として `network.c` が受ける）と `zwl_system_bar_saved`（saved の一覧を thread で読む）を足した。
    - power の最初の状態の修正（phase010 の T1-061 の 2 点を参照）。
  - `wayland/network.c`:
    - `network_key_submit` は `zwl_system_bar_save_key` を呼ぶ。
    - `network_key_saved` は thread が読んだ一覧（menu を開く度に読み直す）を見る。
    - `join_after_profiles` を除いた。
    - 失敗は `zwl_network_key_failed`、一覧は `zwl_network_saved` で受ける。
    - event loop での `kl_backend_network_save_key` と `get_saved` は 0 になった（review 7 の残りが済んだ）。
- protocol（`keiland/kl-system-protocol.h`）: 結果 8・9・10 と、details の約束の説明を足した。design.md §4.1 の 3 にも書いた。
- libkeiland:
  - `system-compat.c`・`audio-compat.c` を削除した。
  - `keiland.h` から `keiland_network_*`・`keiland_audio_*` を除き、`KEILAND_VERSION` を 22 にした。
  - `exports.map` から旧 API を除いた。
  - 3 つの Makefile から backend の同居（zedBSD の `KL_BACKEND_ZEDBSD_COMPAT_SOURCES`、Linux と FreeBSD の `libkeiland-backend.a`）を外した。
  - `kl_system_take_result` は結果 8〜10 を `ENOENT`・`EACCES`・`ENETUNREACH` に戻す。
- `libkeiland-backend-zedbsd/sources.mk`: compat の変数を除いた（compositor だけが使う）。
- checker（`plan/tools/keiland-os-boundary/check.sh`）:
  - B3 を強めた。例外の 2 file を除き、さらに compositor と backend 以外の Makefile が backend の source や `userland/base/net/` を build・link しないことも見る（top-level の backend の Makefile の include は除く）。
  - B2 を新しく足した: compositor の binary の `nm -u` のうち libkeiland の物が motion・scroller・gesture・version だけ。zedBSD と Linux の build がある時に見る。FreeBSD は `B2_BINARIES` で渡す。
- 道具の向け直し（委任の範囲）:
  - `plan/tools/keiland-linux/network-probe.c` と `audio-probe.c` は backend の API を直接使う（`libkeiland-backend.a` に link。README も直した）。
  - `lib-smoke.c` は version 22 と `kl_system_*` を見る。
  - `plan/ws089/tests/host-slot.c` は偽の kl_system に対する試験にした（busy の送り直し・save_key・join の失敗の文の case を足して 9 case にした）。
  - `host-network.c` は `kl_*` にした。
  - `host-build.sh` は audio-compat の代わりに `host-kl-system.c`（新しい、system の無い stand-in）を使う。
  - `plan/ws100/tests/host-audio.*` は既に backend を使っていたので変更していない。
- 範囲外の 1 件（Q1 の判断が要る）: `plan/ws005/tests/bug149-build.sh` は削除した `system-compat.c` を link していた。`bug149-keiland.c` を `kl_backend_network_*` に替え、build から `system-compat.c` を除いた。この build は p011 の前から失敗していた（`kl_backend_session_tick` が未定義）ので、session-none と seat-unsupported も足した。

### 確認（host と build、P2）

- host の試験:
  - `plan/ws131/tests/host-system.sh` PASS（gcc と clang、ASan と UBSan）。次の case を足した: details が scan と並んで答わること、system bar の鍵（PROFILES → JOIN、join の答えは bar の物）、保存の失敗、bar の saved の一覧、結果 8〜10 の errno。
  - `plan/ws089/tests/host-slot.sh` PASS（9 case）。
  - `plan/ws089/tests/host-build.sh` は build でき、host-render で wifi・network・ethernet・sound の頁を描いた。sound は「Not available on this desktop」、ethernet の hardware の address は文字列で出た（目視）。
  - `plan/ws100/tests/host-audio.sh` は 14/14、`plan/tools/settings/host-settings.sh` は 38 passed。
- build:
  - zedBSD amd64（`ZEDBSD_CONFIG=plan/ws131/tests/config-amd64-system.mk BUILD=build/p2-q652` の wayland・libkeiland.so・settings・keiland-system）は exit 0、warning 0。
  - Linux の `make keiland-linux` は gcc と clang で exit 0、warning 0。
  - Linux の libkeiland.so は `keiland_network_*`・`keiland_audio_*` を出さず、NEEDED は libwayland-client・libm・libc だけ。
- checker:
  - `keiland-os-boundary/check.sh` PASS（B2 は zedBSD と Linux の binary で PASS。compositor が使う libkeiland の symbol は `keiland_motion_*` だけ）。
  - `makefile-sync.sh` PASS、`header-check.sh` PASS（370 sources）、`gpu-boundary/v1-check.sh` PASS。
  - `style-check.py` の新しい指摘は 0（mutex の誤検出を除く）。
- 向け直した道具:
  - `network-probe` と `audio-probe` は host で compile と link ができた（guest では未実施）。
  - `lib-smoke` は host で PASS（compositor 無し）。
  - `bug149` の probe は build できた（guest では未実施）。
- app に `keiland_network_`・`keiland_audio_`・`keiland_preferences_` の呼び出しは 0（`userland/` を grep）。

### 未実施（試験の担当に依頼）

- FreeBSD の native build と `native-build-audit.py`。
- zedBSD の QEMU:
  - `settings-regress.sh`
  - `volume-p004.sh`・`volume-p005.sh`
  - `settings-p003.sh` の network の部分（`NETWORK open links=`、`save-key ok`、Connected）
  - p010 の probe の再確認（power の最初の状態）
  - C9 の `zdesktop-p076.sh` を単独で 20 回（受け入れの条件。BUG-125 への追記は Q1）
  - boot-test
- Linux: Settings の起動と WiFi の状態の表示（hwsim）、向け直した `network-probe`・`audio-probe`・`lib-smoke`、p010 の probe の power の最初の状態。
- 実機は未実施。

### 書き手の Guardrail（design.md §3.7、p011 の merge で Q1 が適用）の案

Q1 への merge の依頼に添えた（下の文）:

> **app と設定**（WS131、2026-10-04）: app は WiFi・network・音量・電源・PnP を libkeiland の `kl_system_*`（拡張 `kl_system_manager_v1`）でだけ扱い、desktop の設定を `kl_settings_*` でだけ扱う。libkeiland は daemon に接続せず、system の file を読まない（例外は app 自身の設定の file `~/.config/keiland/<app>.conf`）。`desktop.conf` と、WiFi の鍵の store の書き込みは compositor だけが行う（鍵は compositor の thread が保存する）。compositor が libkeiland を link する時は、描画の層と motion・scroller・gesture・version だけを使い、Wayland の client の部分を使わない（D4）。確かめは `plan/tools/keiland-os-boundary/check.sh` の B2（compositor の `nm -u`）と B3（backend を使うのは compositor だけ）。

## Resume

試験の担当の結果を Q1 が判定する。FAIL なら P2 が直す。
