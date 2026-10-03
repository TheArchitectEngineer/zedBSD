<!-- awesome-plan project=zedbsd record=ws135-p004 -->
# ws135-p004: Settings を `kl_settings_*` へ、watcher と `keiland_preferences_*` の除去

Status: in-progress（q656-i01、P2 generation7。実装・host 試験・build 済み・T2 の QEMU 待ち）
Disposition: normal
Parent: [WS135](../ws.md)
Queue: q656 / q656-i01

## 実装

- Settings（`settings/look.c`・`sound.c`・`settings.h`・`page-look.c`・`page-input.c`・`main.c`）: `kl_settings_open(display, NULL)`、表示は `kl_settings_get_int`・`get`
  （壁紙は DEFAULT の flag で既定を空に）、選んだ時は常に `kl_settings_set`（既定の値でも set。壁紙の既定の絵だけ `reset`）、他の process・system bar の変更は
  watch（`look_changed`、drag の間は無視、log `LOOK changed key=…`）。1 秒ごとの reload を除いた。`se_look_poll` は `kl_settings_dispatch` と result（失敗を頁の
  message に）。`writable`（compositor の拡張がある）で slider・switch を有効に、無い時は「These settings cannot be changed on this desktop.」。
  Sound の頁: 初めの値は `kl_settings`（sound.volume・muted）、送りは `kl_settings_set_int`（compositor が audiod に頼む）、拡張の無い desktop では今の
  `keiland_audio_set_volume`。audiod の状態の表示と feedback の音は `keiland_audio_*` のまま（WS131 p011 で拡張へ）。
- compositor: `wayland/preferences.c`（watcher の thread・`PREFERENCES_CHECK_MS`）を削除、`zwl_preferences_*`・`server->preferences`・`zwl_settings_follow`・
  `zwl_settings_store_follow` を除いた。log `ZWL PREFERENCES key=… applied` は不変、`ZWL PREFERENCES open` は無くなり `ZWL SETTINGS open present=…` に。
- libkeiland: `libkeiland/preferences.c` と `keiland_preferences_*`（keiland.h・exports.map・3 つの Makefile）を削除。
- 試験: `plan/ws089/tests/host-build.sh` は `preferences.c` の代わりに `settings-cache.c`・`settings-keys.c` と Wayland 無しの stand-in
  `host-settings-stub.c`（新）。`host-preferences.{c,sh}` を削除（`plan/ws135/tests/host-store`・`host-settings` が引き継ぐ）。`host-render.c` は
  `se_look_open(&app, NULL)`。`settings-p007.sh` を「probe で拡張から変えて即時に適用、reset で既定、開始の file は look の前に」へ書き換え（WS135 の image が要る）。
  `settings-p004.sh`・`p005.sh` の session の間の `cat $conf`（診断の表示）を除いた。`ws100` の volume の試験は file を session の間に読むが書かない（`volume-bug153.sh`
  の手での編集は「追わない」の確かめで残す）。
- `plan/tools/keiland-os-boundary/check.sh` に S1（`desktop.conf` の文字列は `wayland/settings-store.c` の中だけ）。

## 検証

- host: `host-store.sh` 43/43（follow の 2 項目を除いた）、`host-settings.sh` 26/26、`plan/ws089/tests/host-build.sh` OK、`host-wallpaper.sh` PASS、`host-slot.sh` PASS。
- build: zedBSD（`config-amd64-settings.mk` の libkeiland・compositor・Settings・probe）warning 0、`make keiland-linux` warning 0。
- `ZEDBSD_CONFIG=plan/ws135/tests/config-amd64-settings.mk sh plan/tools/keiland-os-boundary/check.sh` → PASS（S1 を含む）。
- QEMU（T2 に依頼）: `settings-p003.sh`・`settings-p007.sh`（書き換え）・`settings-p004.sh`・`settings-p005.sh`・`settings-pages.sh`・`plan/ws100/tests/volume-p005.sh`
  （Settings の音量の頁が拡張経由）。未実施。
- 未実施: FreeBSD の native build（WS137 の guest の道具の後に T）。
