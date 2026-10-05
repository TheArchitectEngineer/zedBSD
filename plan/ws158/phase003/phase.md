<!-- awesome-plan project=zedbsd record=ws158-p003 -->

# ws158-p003: compositor・greeter・lock の文を翻訳の口に通す、最初の日本語の catalog と install の規則

Status: in-progress（2026-10-05 夜、P2 g17。実装と host の試験まで。QEMU は T1）
Disposition: normal
Parent: [WS158](../ws.md)
Queue: Q1 の指示（2026-10-05 夜「p003 に進んでよい」）
依存: [p002](../phase002/phase.md)（libkeiland の kl_tr_*）

## 範囲

compositor（system bar の時計・音量の popup・network の menu と詳細・Wiseview・窓の題・App Home の Lock Screen と Log Out）、greeter（login の画面）、lock の画面の文。言語の決め方（session は `ui.language`、login の前は system の言語）。catalog `ja/wayland.tr` と、zedBSD・Linux・FreeBSD の install の規則。各 app は p004、日本語の review と用語集は p005。

## 作った物

- `userland/desktop/wayland/language.c`・`language.h`:
  - `zwl_language_set(server, setting)`: `settings.c` が `ui.language` を適用する時に呼ぶ（起動の時と変更の時）。`kl_tr_open("wayland", 言語)` で catalog を読み、画面を描き直す。log `ZWL LANGUAGE language= from=setting error=`。
  - `zwl_language_system(server)`: login の画面（`--greeter`、settings を持たない）は `KEILAND_SYSCONFDIR/keiland/language`（zedBSD は `/etc/keiland/language`）の 1 行（`en`・`ja`）。無ければ英語（p001 の判断 ④: system の既定。管理者が Settings で変える口は p004 の Settings で）。
  - `zwl_language_date(local, form, out, size)`: 曜日・月の名前（`kl_trc("weekday", …)`・`kl_trc("month", …)`）と並び（`kl_trc("short date", "{1} {2} {3}  {4}")`、`kl_trc("long date", "{1}, {2} {3}")`）。system bar の時計（`strftime "%a %b %e  %H:%M"` の置き換え）と greeter の日付（`"%A, %B %e"`）。日本語は `10月5日(月)  14:05`・`10月5日 月曜日`。
- 文を口に通した所: `greeter.c`（Restart・Shut Down・Starting session...・Checking...・Use your password/PIN・Password・PIN・Shutting down...・Restarting...・欄の下の 8 つの message）、`shell.c`（Window・`{1} (not responding)`・Wiseview・`Wiseview  -  {1} window(s)`（`kl_trn`）・No other windows・Swipe down…）、`home.c`（icon の名前を `kl_tr(app->name)` で描く。検索と log は英語のまま）、`volume.c`（Sound・Muted・Mute・2 つの状態）、`network.c`（menu の行、状態の行、失敗の文（`{1}` で SSID と errno の文）、`Connecting...`、詳細の見出しと値を描く時に `kl_tr`、要求の名前は `network_request_phrase` で `kl_trc("network request", …)`）。log の行（`ZWL …`）は英語のまま（試験と AAT が読む）。
- buffer: network の行 80→160、失敗 96→192、Wiseview の見出し 48→96、時計 48→64、greeter の message 64→128 byte（日本語の UTF-8）。
- catalog `userland/desktop/locale/ja/wayland.tr`（117 項目、全部訳した）と `userland/desktop/locale/wayland.keys`（変数を通す文: App Home の 2 つ、network の詳細の見出しと値。`network-info.c` は host の試験が libkeiland 無しで build するので、そこでは訳さない）。
- `tools/i18n/tr.py`: SOURCE に `.keys` の file を取る（変数を通す文の一覧）。
- install: zedBSD は libkeiland の package の data に `userland/desktop/locale/*/*.tr` → `/usr/share/keiland/locale/…`、Linux・FreeBSD は `share/keiland/locale/…`（`keiland-linux.mk`・`keiland-freebsd.mk`）。
- 試験: `plan/ws158/tests/tr-host-test.sh` に出荷する catalog の `check --strict`（source とつき合わせ、未訳・place の欠け・余りで失敗）を足した。QEMU の試験 `plan/ws158/tests/tr-p003-guest.sh`（image `plan/ws158/tests/config-amd64-tr.mk`）。シナリオ `tests/scenarios/desktop/language/compositor-japanese.md`（draft、T1 の後に active）。

## 判断（P2）

- 画面 keyboard（WS102）の tool の文は、もともと日本語（前の app・編集・候補…）なので、この catalog の対象にしない（英語の UI で日本語のまま。p005 の用語集で扱いを決める）。
- menu の shortcut の名前（Ctrl+・Shift+・Enter…）、`strerror` の文、key の表は訳さない。App の名前（Files など）は catalog に入れない（訳すかは p005 の用語集）。
- 失敗の文は作った時の言語で残る（言語を変えた直後の古い 1 行は、次の操作で消える）。

## 確かめ（host）

- `sh plan/ws158/tests/tr-host-test.sh` → `tr-host-test: PASS`（`catalog userland/desktop/locale/ja/wayland.tr: ok` を含む）。
- build: zedBSD の `bin/wayland`・`dynamic/libkeiland.so`（`config-amd64-zdesktop.mk`、BUILD=build/p2-b194）と `make keiland-linux` は rc 0・warning 0。Linux の build は `share/keiland/locale/ja/wayland.tr` を置く。zedBSD の image の file の一覧（`make -n -p`）に `/usr/share/keiland/locale/ja/wayland.tr` がある。
- style-check: 新しい file（language.c・language.h）は 0。変えた行の周りの findings は前からの 1 つ（network.c の `return` の前の空行）だけ。`git diff --check` OK。
- `check-scenarios.py` PASS。

## 未実施

- QEMU（T1）: `plan/ws158/tests/tr-p003-guest.sh`（下）。
- greeter の system の言語（`/etc/keiland/language`）の QEMU の確かめ（session の管理者が要るので UAT か AAT の image で）。
- FreeBSD の build。
- 実機。

## T1 への依頼（Q1 経由）

image: main（この Phase の merge の後）で `plan/tools/guest/test-image.sh plan/ws158/tests/config-amd64-tr.mk BUILD --file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png`。起動 `plan/tools/files/files-guest.sh start BUILD/hdd-image.img`。試験 `plan/ws158/tests/tr-p003-guest.sh OUTDIR`。合格: 最後の行 `tr-p003: status=0`、PNG（ja-bar・ja-volume・ja-network・ja-network-details・ja-wiseview・ja-lock）に日本語が描かれている（Q1 が見る）。
