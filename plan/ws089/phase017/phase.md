<!-- awesome-plan project=zedbsd record=ws089-p017 -->

# ws089-p017: accent の色・dark の外観（D2）

Status: in-progress（p017a、2026-10-05 夕 P2 g15、q766）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q741（Q1、2026-10-05、P2）
依存: p010。zdesktop と各 app の固定の色を preferences から読む変更（他の WS の source）
目安: 4h 以上（複数 Phase の見込み）（1 Queue）。実行者の目安: phase-runner
所有 path: Q1 が決める（settings・compositor・libkeiui の theme・各 app）

## 範囲

Appearance の accent の色の選択と dark の外観。zdesktop・libkeiui の theme・Files・Settings ほかが preferences の色を読む。

## 受け入れ

（採用されたら分割して決める）

## 検証の方法と範囲

QEMU の Venus。 やっていない確認は「未実施」と書く。

## 未決の判断

ベータ1 に入れるか（ユーザー、計画エージェントの案: 入れない）。

## Event

2026-10-02 / ws089-beta1-plan: fg019 の計画で新設。

## 設計（2026-10-05、P2、q741）

- 今の accent は各所に固定の色（`FM_RGB(0x2f7cf6)` など）で 15 file に散っている（compositor の `home.c`・`icons.c`、Files 9 file、Settings、Notes、libkeiland の `ui/theme.c`・`ui/widgets.c`）。dark の外観は無い（白い glass と slate の文字が前提）。
- 案:
  1. settings の key `appearance.accent`（色の名前の列挙: blue・purple・pink・red・orange・green・graphite、既定 blue）と `appearance.dark`（bool、既定 off、後で「自動」）。compositor が持つ（Guardrail の「app と設定」）。
  2. libkeiland に theme の口（`kl_theme_accent()`・`kl_theme_color(KL_THEME_TEXT)` など）を置き、`kl_settings_watch` で変わったら app に描き直しを求める（WS158 の言語の通知と同じ形）。
  3. 段: (a) accent だけ（固定の色を口に置き換える。15 file）、(b) dark（glass の色・文字・線・影の組を 2 つ持つ。全 app の描画に及ぶ大きな変更）。
  4. Appearance の頁に accent の色の丸 7 つと Dark の switch。
- **判断が要る点**（ユーザー）: (1) ベータ2 に入れるか（計画エージェントの案は入れない、2026-10-02）、(2) 入れるなら accent だけか dark もか、(3) 各 app の source に及ぶので担当の分け方（Q1）。
- 判断が出るまで実装しない。

## ユーザーの決定（2026-10-05 夕、Q1 経由）

- 「p017はベータ2に入れます。ただし、従来の調整を壊さないようなデフォルト値で開始できるようにします。」
- 「ライトモードとダークモードだけでいいです。アクセントカラーの変更は不要です。」→ accent の選択は作らない。`appearance.dark`（既定 off = 今の見た目）だけ。
- 「アプリにテーマ変更による再描画を通知するためのWayland拡張がほしいですね。libkeilandを通じて利用します。」→ compositor が theme の変化を伝える内部の拡張、libkeiland に取得と callback の口。app は settings を直接見張らない。
- 段の組み直し: p017a（key・拡張・口・Settings の切り替え、libkeiland ui・compositor・Settings・Files の dark）→ p017b（残りの app の dark）。

## 設計（p017a、2026-10-05 夕）

- **設定**: `appearance.dark`（compositor の key、bool、既定 0、KEPT）。書くのは Settings（kl_settings_set）、持つのは compositor。
- **拡張 `keiland_theme_v1`**（別の global、version 1。system 拡張に入れない理由: system 拡張は compositor の利用者の client にだけ見せる口で、theme は全ての client が知ってよく、軽い一つの値なので独立の global が単純）:
  - request 0 `destroy`
  - event 0 `appearance(uint mode)`: 0 light、1 dark。bind の時と、変わるたびに全ての object へ。未知の値は light として扱う（後の版で値を足せる）。
- **libkeiland**（KL_VERSION 35。34 は WS163 の PIN と重なったので 35 にした）: `kl_appearance_open(display, changed, data)`・`kl_appearance_get`・`kl_appearance_close`（`KL_APPEARANCE_LIGHT`・`_DARK`）。callback は display の default queue の dispatch の中で呼ばれる。process の今の appearance を libkeiland が覚え、`kl_theme_default()` は light か dark の theme を返す（app が持つ pointer は同じ物で、中身が替わる）。`kl_app` は開く時に自分で appearance を見張り、変わったら `KL_APP_THEME` の event を積む（app はそれで描き直す）。
- **compositor**: `server->dark`（`appearance.dark` から）。変わったら全ての `keiland_theme_v1` に event。描画は `glass_shape_draw` の一か所で、dark の時に色を写す: 灰色に近い（彩度の低い）色は明るさを反転（白い glass の地は暗い glass に、暗い文字は明るい文字に）、色の付いた色（accent・app の印の色）はそのまま、影と画像（窓の中身・壁紙・app の絵）は変えない。白い glass を持ち上げる shader の処理（`GLASS_LEAST_LUMA`）は白の地にだけ効くので、暗い glass には効かない。
- **Settings**: Appearance の頁に「Light・Dark」の切り替え（`appearance.dark` を set）。自分の色（`SE_COLOR_*`）は light と dark の 2 組の表を持ち、`kl_appearance` の callback で替えて描き直す。
- **Files**: `FM_COLOR_*` を同じく 2 組の表に、`kl_appearance` の callback で替える。
- **既定で見た目は変わらない**: 既定 off、light の表は今の値そのまま。

## 実施（p017a、2026-10-05 夕、P2 g15、q766）

- **compositor**（`userland/desktop/wayland/`）: `theme.c`（新規、`keiland_theme_v1` の request・bind の event・`zwl_theme_changed` の broadcast、log `ZWL THEME appearance=N`）、`protocol.c`（global 27・dispatch・bind）、`zwl.h`（`ZWL_THEME`、`server->dark`、`client->theme_bound`）、`settings.c`（`appearance.dark` → `settings_apply_appearance`: dirty と broadcast）、`glass.c`（`glass_shape_draw` で dark の色の写し: glass・solid・ring・text の彩度 0.25 未満の色の明るさを反転、glass は shader の mode -1）、`glass.h`（`shape.light`、`GLASS_DARK_SATURATION`）、`panels.c`（`keiland_theme_v1` を bind していない client の窓の glass は light のまま: dark を知らない app の暗い文字が暗い glass に載らない）、`shaders/panel.frag`・`shaders.h`（dark glass: 明るい壁紙の上で luma 0.15 まで暗くする。light の `GLASS_LEAST_LUMA` と対称。`regenerate.py` で再生成、変更前の再生成は差分 0 を確認）。
- **libwayland**: `theme-protocol.c`・`zed-theme-v1-client-protocol.h`（新規）、Makefile 3 つ、`exports.map`。
- **libkeiland**（KL_VERSION 35。34 は WS163 の PIN と重なったので 35 にした）: `appearance.c`（新規、`kl_appearance_open/get/close`。bind は library の queue で、最初の値は roundtrip で取り、default queue へ移す）、`ui/theme.c`（`theme_dark` の表、`kl_theme_default` は同じ pointer で中身を替える、`keiui_theme_set`・`keiui_theme_of`）、`ui/app.c`・`ui/window.h`（`kl_app` が自分で見張り `KL_APP_THEME` を積む）、`keiland.h`・`keiland-ui.h`、`exports.map`（`exports.py` で再生成、`--check` ok）、Makefile 3 つ。
- **settings-keys**: `appearance.dark`（compositor、bool、既定 0、KEPT）。
- **Settings**: `palette.c`（新規、light は今の値そのまま・dark の 2 組、`SE_COLOR_*` は `se_palette->` を読む）、固定の色 18 か所を palette へ（track・rail・control・field・faded・pressed・title・tile_hover）、Appearance の頁に「Dark appearance」の switch（`appearance.dark` を set、「Accent colours and a dark look are coming…」の note は削除）、`main.c` で `kl_appearance_open` と callback（log `ZSETTINGS APPEARANCE appearance=N`）。
- **Files**: `palette.c`（新規、同じ形、`FM_COLOR_*` は `fm_palette->`）、固定の色 16 か所を palette へ（button・button_lit・inner・tile・rail・panel_rim・title）、`main.c` で窓のとき（desktop の icon は除く）`kl_appearance_open`。
- **試験**: `plan/ws089/tests/host-dark.c`・`run-host-dark.sh`（新規、light・dark の theme・Settings・Files の文字と地の 72 組の対比が 4.5 以上。glass は light の最暗 0.85・dark の最明 0.15 で最悪を取る）、`c7-either.py`（新規、暗い文字も明るい文字も測る C7）、`settings-p017.sh`（新規、QEMU: light の C7 → `keiland-settings set appearance.dark 1` → compositor・Settings の log と dark の C7（時計・Settings の 6 箇所）→ Files の dark の C7 → zdesktop の再起動で dark が保たれる → reset で light）、`config-amd64-settings.mk` に `keiland-settings`、`host-render.c` に `--dark`、`host-kl-system.c` に p026 の stand-in 2 つ（host-build が link できなかった既存の不足）。

### 確認（host）

- build: zedBSD の `config-amd64-settings.mk` で `bin/wayland`・`bin/settings`・`bin/files`・`bin/keiland-settings`（warning 0、-Werror）、`make keiland-linux`（gcc、warning 0）。
- `sh plan/ws089/tests/run-host-dark.sh`: `HOST-DARK pairs=72 failures=0`、`PASS`。最小は light の sidebar の text_secondary 4.77（今の値）、dark の最小は Settings の tile の text_secondary 6.68。
- `plan/tools/style-check.py`: 新しい file は findings 0、変えた file は増減なし（glass.c の既存 4 件のまま）。
- host の Settings（`plan/ws089/tests/host-build.sh` → `settings-render --dark --page=appearance`）: dark の Appearance の頁を目視（暗い地・明るい文字・switch）、`control=2` で `LOOK set key=appearance.dark value=1 error=0`。light の頁は switch の card が増えただけ。
- 既存の観察（範囲外、Q1 へ報告）: `plan/tools/settings/host-store.sh` は本変更の前から 10 件 FAIL（`pointer.speed` の試験が p024 の `mouse.speed` への移行に追従していない）。

### 未実施

- QEMU（T1）: `settings-p017.sh` と、既定の見た目が変わらないことの回帰（C7 の `plan/ws099/tests/c7-contrast.sh`）。compositor の chrome（system bar・title bar・App Home・menu）の dark の見た目はこの試験の PNG で判断する。
- 実機: 未実施。
- p017b（残りの app の dark と `KL_APP_THEME` での描き直し）。

## 実施（p017b、2026-10-05 夜、P2 g15、q766）

Q1（2026-10-05）「BUG-171 の判断待ちの間は、WS089 p017b（残りの app の dark と KL_APP_THEME の描き直し）を先に進めてください（ユーザーが UAT で見たい見込みの物を優先）。」

- **libkeiland**: `kl_theme_choose(light, dark)`（keiland-ui.h、KL_VERSION 35 に含める: 35 はまだ release 前）。program 自身の色を appearance で選ぶ。widgets の固定の色を dark でも合うように: field の地（`theme->panel`）、card の rim と chip（cards.c）、faded（widgets.c）、chooser の warning の地と file の紙。exports 再生成。
- **kl_app の app**（Phone・Mailer・Calendar・Video Player・kuidemo）: `KL_APP_THEME` で `dirty`（theme の pointer は同じで中身が替わる）。Phone・Mailer・Calendar の固定の地・card・sidebar・glass の veil を `kl_theme_choose` に（白い文字・accent・avatar・絵は変えない。全面の白い地は `*_COLOR_SURFACE` に分けた）。
- **Text Editor**: `DRAW_*` 20 色を light・dark の組に。`kl_appearance_open`、変わったら描き直し（log `APPEARANCE appearance=N`）。
- **Notes**: toolbar の veil・rim・文字、机の wash（夜の色）。紙（page）は白のまま。`kl_appearance_open`（log `NOTES APPEARANCE appearance=N`）。
- **PDF Viewer**: 枠の 14 色を light・dark の組に（page は白のまま）。draw.c は host 試験で libkeiland なしに build されるので、`pv_draw_set_dark` の局所の flag で選ぶ（main.c が appearance から設定）。
- **Image Viewer**: 文字・card・chip・message と不透明の地。絵はそのまま。
- Terminal と Monitor は元から暗い配色なので変えない（appearance を bind しないので、compositor の glass も light のまま = 今と同じ）。
- 試験: `plan/ws089/tests/config-amd64-dark-apps.mk`（Settings の image に textedit・notes・imageview・phone・calendar・mailer）、`settings-p017b.sh`（各 app を light で起動 → `keiland-settings set appearance.dark 1` → dark の PNG と log → reset）。

### 確認（host）

- build: zedBSD の phone・mailer・calendar・videoplayer・kuidemo・textedit・notes・pdfviewer・imageview・settings（warning 0）、`make keiland-linux`（warning 0）、exports `--check` ok。
- host: `plan/ws079/tests/run-notes-host.sh` ok、`plan/ws081/tests/run-notestouch.sh` ok（52）、`plan/ws079/tests/run-pdfviewer-host.sh` ok。
- style-check: 変えた file の件数は増やしていない。
- 範囲外の観察（Q1 へ）: `plan/ws081/tests/run-pdftouch.sh` は今回の前から壊れている（`chooser.c` が無い、`keiland-ui.h`・`keiui.h` の link が無い、host-pdftouch.c が今の touch の API と合わない）。小さくないので直していない。

### 未実施

- QEMU（T1）: `settings-p017b.sh`（PNG を目で判断）。
- 実機: 未実施。
