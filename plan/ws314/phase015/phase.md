<!-- awesome-plan project=zedbsd record=ws314-p015 -->

# ws314-p015 kl_app に system の言語の変更を知らせる event（`KL_APP_LANGUAGE`）

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §13。Queue: q1690 / q1690-i02（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。build・host・QEMU（zedBSD）で確かめた）
- Disposition: normal
- ユーザーの決定（ws.md の原文）: 12「kl_appに、システムの言語の変更を通知するイベントを追加してほしいです。」、
  13「kl_appの言語変更イベントは、変更された言語名も通知するのがいいです。連続する変更が短時間にあると、kl_settingsの読みが最新とは限らないからです。」

## 目的・受け入れ

- `KL_APP_LANGUAGE`（KL_VERSION 101）: desktop の言語が変わると app の event が来て、`event.language` が変わった先の言語の名前
  （"en"・"ja"、`kl_tr_open` が取る形）を運ぶ。続けて変われば event も同じ順に来て、各 event はその変更の言語（決定 13）。
  `kl_app_language(app)` は app が最後に聞いた言語。
- build warning 0（zedBSD 3 arch・Linux）、host の試験、zedBSD の compositor の下で続けて 3 回変えて 3 つの event が順に来ること。
- 解説への記述は p009（翻訳の読み直しの 2 つの形、design §13.3）。

## 実装

- 公開: `keiland_app.h`（`KL_APP_LANGUAGE` 7、`kl_app_event.language[KL_TR_NAME_MAX + 1]`、`kl_app_language`、`<keiland/keiland_tr.h>` を
  include）、`keiland_version.h`（101）、exports.map（`kl_app_language`）。
- `ui/app.c`: `kl_app_open` が app の display で `kl_settings_open(display, NULL)` を 1 つ持ち（`kl_appearance` と同じく app が持つ）、
  `ui.language` の今の値を言語の名前にして持ち、watch する。watch の callback は**変わった値を引数で受け**（`kl_settings_get` を読み直さない）、
  名前に直して `kl_app_language` の値を更新し、`KL_APP_LANGUAGE` を `keiui_app_post_event` で積む（app の thread で queue が満ちていれば
  post の list で待つ。捨てるのは list の node の malloc が失敗した時だけ）。`kl_app_dispatch` は wayland の event の後と待つ前に
  `kl_settings_dispatch` を呼ぶ（settings は library の自分の queue なので、他の読み手が読んだ分も拾う）。`kl_app_close` が settings を閉じる。
  compositor が settings の拡張を持たなければ event は来ず、言語は "en"。
- `kl_tr_follow` は kl_app を使わない program のために残した。Files・Settings の look・Installer の `kl_tr_follow` は移さない（範囲を
  広げない。kl_app の program は `KL_APP_LANGUAGE` を使える）。

## 確かめ（2026-10-30）

- Linux（all、`build/ws314-p015-linux`）: make 0・warning 0。
- zedBSD（`build/ws314-p015-build-all.sh`）: amd64（desktop 19＋tests 9）・arm64・riscv64（19）・installer の image の installer: make 0・warning 0。
- host の試験 `plan/ws314/tests/language-test.c`（新、`sh plan/ws314/tests/host-test.sh language build/ws314-p015-linux`）: settings の拡張の
  無い server で "en"、変更が聞こえない。PASS。post・process・launch・instance も PASS。
- QEMU（2026-10-30、`sh plan/ws314/tests/guest.sh build/p2-q/run9 build/ws314-img`、image は
  `plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-guest.mk build/ws314-img`（files・textedit を足した）、compositor は
  `/bin/wayland --testing --glass` 1280x800、判定は SSH で取った各試験の結果の行と app の log（console は読まない））: `ws314-guest: PASS`。
  language-test: 起動時 `language=en setting=0`、自分の kl_settings で 1・0・1 と続けて変えて `events=3 ja en ja now=ja`（各 event がその変更の
  言語、順のとおり、`kl_app_language` は最後）、元へ戻して `en` の event。PASS。1 回目（run8）は試験の誤り（`kl_app_language` の pointer を
  保ったまま比べた）で FAIL、library の event は同じ `ja en ja` だった。
