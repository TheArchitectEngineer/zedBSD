# WS179 の設計: UI のアクセントカラー（ws179-p001・p002）

2026-10-07、P1。範囲は [ws.md](ws.md) と Q1 の ACK（p001: libkeiland・compositor の UI・Settings・Files、p002: Calendar・PDF Viewer・Phone・Mailer・Notes・Image Viewer、App Home の app の tile の色は従わせない）。

## 1. 今の形

- 明暗: compositor の設定の key `appearance.dark`（settings-keys の `KL_SETTINGS_RESOLVER_COMPOSITOR`）→ `wayland/settings.c` の `settings_apply_appearance` → `server->dark` と `kwl_theme_changed`（`wayland/theme.c`）→ protocol `kl_theme_v1`（version 1、event `appearance(u)`、`libwayland/theme-protocol.c`）→ libkeiland の `appearance.c`（`appearance_told`: `keiui_theme_set` で `theme_now` を入れ替え、watch の `changed` を呼ぶ）→ app は描き直し（`kl_app` は `KL_APP_THEME`）。
- accent の値 `0x2f7cf6` が直に書かれている所:
  - libkeiland `ui/theme.c`（light・dark の `accent`・`selection`）。白い文字を accent の上に描く所は各部品の定数（`widgets.c` の主の button、`list.c`・`views.c`・`chooser-view.c` の選んだ行と pill、`views.c` の「accent の上の二次の文字」）。
  - compositor: `titlebar-shell.c`（control の checked、field の縁、progress、もう 1 か所。`{0.18,0.49,0.96}`）、`keyboard.c`（OSK の強調 8 か所）、`shell.c`・`home.c` の別の青 `{0.25,0.52,0.98}`（page の点の今の物、drop の線、glow、選択の縁）。
  - Settings `palette.c`（`accent`・`selection`）、Files `palette.c` と `ui-*.c`（約 10 か所）。p002 の app は各自の定数。
- accent でない青（変えない）: zedBSD の mark の層（`glass.c` の `bar_colours` など）、App Home の app の tile の色（`home.c` の `home_add_app`）、Files の folder の青（`theme->folder`）。

## 2. 色の表

8 色、index は保存の値（0 が既定）。各色は light と dark の 2 つの値を持ち、それぞれに accent・accent の上の文字（`accent_ink`）・selection（accent に α）を決める。

| index | 名 | light accent | dark accent | 文字（ink） |
| --- | --- | --- | --- | --- |
| 0 | blue（既定） | `0x2f7cf6` | `0x2f7cf6` | 白 |
| 1 | purple | `0x8655f6` | `0x8655f6` | 白 |
| 2 | pink | `0xdb2777` | `0xdb2777` | 白 |
| 3 | red | `0xdc2f3c` | `0xdc2f3c` | 白 |
| 4 | orange | `0xd4690b` | `0xe8730c` | 黒（`0x16191f`） |
| 5 | yellow | `0xa48207` | `0xf5c518` | 黒 |
| 6 | green | `0x1e9a53` | `0x1f9d55` | 黒 |
| 7 | graphite | `0x5b6472` | `0x677180` | 白 |

- 基準: (1) ink と accent の contrast 4.5 以上（WCAG 1.4.3、button の 13px の文字）、(2) accent と window の ground（light: `0xffffff`・`0xeef2f7`・`0xe6ebf3`、dark: `0x23272f`・`0x1b1f26`・`0x16191f`）の contrast 3.0 以上（1.4.11、switch・slider・focus の輪・caret）。各色の色相を保ち、明るさだけを基準を満たす最も近い値に寄せた（計算は §6 の host 試験で固定）。
- **blue は例外**: 今の `0x2f7cf6` と白の文字は 3.94（4.5 に届かない）。今の見た目（ユーザーが満足している既定）を変えないため、blue だけは今の値と白のまま、基準 (1) の例外として記録する（(2) は light 3.50・dark 3.80 で満たす）。4.5 にするなら `0x1b6ff5`（白で 4.53）に暗くする案がある（§7 の判断）。
- selection: light は accent の α 40、dark は α 70（今の値と同じ）。selection_inactive・hover は灰色のまま（accent に従わない）。

## 3. 伝え方

- 設定の key `appearance.accent`: `KL_SETTINGS_RESOLVER_COMPOSITOR`、`KL_SETTINGS_TYPE_INT`、0〜7、既定 0、`KL_SETTINGS_KEY_KEPT`（`appearance.dark` の隣）。
- compositor: `wayland/settings.c` の名前の分岐に `appearance.accent` を足し、`settings_apply_accent`（範囲外は 0）で `server->accent` を変え、`server->dirty = 1`、起動中でなければ `kwl_theme_changed`。
- protocol `kl_theme_v1` を version 2 に: event 1 `accent(u index)`。compositor は global を version 2 で広告し、bind した version が 2 以上の client にだけ、`appearance` の後に `accent` を送る（bind の時と、どちらかが変わった時）。version 1 で bind した client（古い libkeiland）は今と同じ。`libwayland/theme-protocol.c` と `zed-theme-v1-client-protocol.h` に event と listener の field を足す（listener の struct は末尾に足すだけ）。
- libkeiland `appearance.c`: `min(2, search.version)` で bind（`keiui_global_search` は広告の version を持つ。古い compositor の version 1 では accent は来ず blue のまま）。listener に `accent_told`: 範囲外は 0、program の accent（`accent_program`）が変われば `keiui_theme_set(appearance, accent)`、watch の accent が変われば `changed(data, appearance)` を呼ぶ（明暗が同じでも呼ぶ。app は今の KL_APP_THEME と同じに描き直す）。
- 順序: compositor は 1 回の変更で appearance と accent を続けて送るので、app の描き直しは最大 2 回（同じ値の event は呼ばない）。

## 4. KL の API（KL_VERSION 57）

```c
#define KL_ACCENT_BLUE		0U
#define KL_ACCENT_PURPLE	1U
#define KL_ACCENT_PINK		2U
#define KL_ACCENT_RED		3U
#define KL_ACCENT_ORANGE	4U
#define KL_ACCENT_YELLOW	5U
#define KL_ACCENT_GREEN		6U
#define KL_ACCENT_GRAPHITE	7U
#define KL_ACCENTS		8U

unsigned kl_accent_get(void);                          /* the program's accent told last (KL_ACCENT_BLUE before any) */
kl_color kl_accent_color(unsigned accent, unsigned appearance);  /* an accent's colour in an appearance, for Settings' swatches */
```

- `struct kl_theme` の末尾に `kl_color accent_ink;`（accent の上の文字）を足す。既存の field の並びと意味は変えない（app は `kl_theme_default()` の pointer を読むだけで、struct を自分で作らない。2026-10-07 に grep で確かめた: `struct kl_theme` の実体は libkeiland の `theme.c` の 3 つだけ）。
- `keiui_theme_set(appearance, accent)`: light・dark の基の theme を写し、`accent`・`selection`・`accent_ink` を表から上書きする。`keiui_theme_of(appearance)` は試験用に基の theme を返すまま、新しい `keiui_theme_with(appearance, accent, out)` を試験に使う。
- libkeiland の部品の白の定数を `theme->accent_ink` に替える（主の button の文字、選んだ行・pill の文字と icon、`views.c` の二次の文字は `accent_ink` の α）。

## 5. 従わせる所

### p001

- libkeiland: §4（`theme.c`・`appearance.c`・`widgets.c`・`list.c`・`views.c`・`chooser-view.c`・`cards.c`・`text-touch.c` は `theme->accent` を既に読むので、ink だけ）。
- compositor: `kwl.h` に `int32_t accent`、`theme.c` に `kwl_accent_rgba(const struct kwl_server *, float alpha, float out[4])` と `kwl_accent_ink`（表は libkeiland と同じ値。compositor は libkeiland を link しているので `kl_accent_color(server->accent, server->dark)` を使い、表を 2 つ持たない）。置き換え: `titlebar-shell.c` の `accent`（4 関数）、`keyboard.c` の `blue`（8 か所）、`shell.c`・`home.c` の `{0.25,0.52,0.98,α}`（今の page の点・drop の線・glow・選択の縁、α は保つ）。描く関数が server を持たない所は引数で accent の色を渡す。
- Settings: `palette.c` の `se_palette_set` は基の palette を `palette_now` に写して `accent`・`selection` を `kl_theme_default()` のものに上書きする（`se_palette` は `palette_now` を指す）。Appearance の page に「Accent colour」の行: 8 つの丸（直径 24、間 12）、選んだ丸に白い check と輪、click と keyboard（左右）で選び `se_look_set_number(app, "appearance.accent", index, 0)`。名前は丸の tooltip ではなく行の下の小さな文字で選んだ色の名（`kl_tr`）。
- Files: `palette.c` の `fm_palette_set` を同じ形に。`ui-*.c` の直の `0x2f7cf6` を `FM_COLOR_ACCENT` に。

### p002

- Calendar・PDF Viewer・Phone・Mailer・Notes・Image Viewer の直の accent を `kl_theme_default()->accent`（と ink）に。各 app が appearance の変化で描き直すことを確かめる（しない app は KL_APP_THEME か `kl_appearance` の changed を足す）。

## 6. 試験

- host（`plan/ws179/tests/host-accent.c`、libkeiland の `theme.c` と表を link）: 8 色 × 2 の各組で §2 の基準 (1)（blue を除く）と (2) を計算し、外れたら FAIL。`keiui_theme_with` の結果の accent・selection・ink が表と一致。protocol の decode（version 2 の event を libwayland の message の表で）。
- host の描画（`plan/ws090/tests/host-widgets.c` の形、新しい `host-accent-widgets`）: 主の button・switch・slider・選んだ list の行・focus の輪を 8 色 × 2 で描き PNG に（目で見る用、判定は ink の色の画素）。
- build: libkeiland・compositor・Settings・Files（target の clang、warning 0）。
- T1（p001 の後）: AAT の image で Settings > Appearance、8 色を順に選び、各色で Settings・Files・bar・titlebar（control と field）・OSK を撮影、dark でも 2 色。期待: 開いている Files が描き直され、App Home の tile の色と mark は変わらない。

## 7. 人の判断

- blue の文字の contrast（§2）: 今の `0x2f7cf6` と白（3.94、今の見た目のまま、例外として記録）か、`0x1b6ff5`（4.53、少し暗い）か。設計の既定は前者（今の見た目を変えない）。

## 8. 範囲外

自由な色、app ごとの accent、accent に合わせた壁紙・mark の色、selection_inactive・hover の色。

## 9. review の反映（2026-10-07、design-reviewer、第 2 版。上の §2〜§6 をこのとおり読み替える）

blocking 4・should-fix 10・minor 6。人の判断は §10。

| 指摘 | 反映 |
| --- | --- |
| B1 compositor が libkeiland の `kl_accent_color` を呼ぶと境界の規則 D4（`plan/guardrail.md`）と `keiland-os-boundary/check.sh` の B2 に反する | 表は共有の source `userland/desktop/artwork/accent.c`・`accent.h`（`settings-keys.c`・`artwork/mark.c` の前例）に置き、libkeiland と compositor の 3 つの Makefile（`Makefile`・`Makefile.linux`・`Makefile.freebsd`）に compile して入れる。関数は `ka_accent_values(index, appearance, struct ka_accent *out)`（`kl_` でない名、check の B2 に掛からない）。libkeiland の公開の `kl_accent_values` はそれを包む |
| B2 compositor の dark の色の変換（`glass.c` の `glass_dark_color`、彩度 0.25 未満の明るさを反転）で ink と graphite が変わる | accent・ink・accent_text の shape は `shape.light = 1`（変換を通さない、`glass.c` の前例）で描き、値は**描く面の地**で選ぶ: 地が明るい面（light の外観の窓の UI、`theme_bound == 0` の client の light の面）は light の値、地が暗い面（dark の外観、どちらの外観でも dark glass の system bar と、その popup）は dark の値。関数 `kwl_accent(server, int dark_ground, float alpha, struct kwl_accent_rgba *out)`。T1 の dark の撮影は yellow と graphite |
| B3 Files の desktop mode は外観を watch しない（`files/main.c` の `if (!options.desktop)`） | desktop mode でも watch を開き、明暗は light のまま、accent だけを `fm_palette` に取る（callback を分ける）。desktop の icon の pill・band・drop の枠が accent に従う（§10 の (c) の推奨） |
| B4 Files の accent の上の白（`ui-overlay.c` の check、`ui-desktop.c` の `DESKTOP_PILL_TEXT`、`ui-drag.c` の badge の数字） | `fm_palette` に `accent_ink` を足して置き換える |
| S1 compositor の一覧の漏れ | 置き換えの一覧: `titlebar-shell.c` の 7 か所（`accent` 4・`lit_ground`・`selection`・drop の `edge`）、`keyboard.c` 8、`shell.c`・`home.c` の `{0.25,0.52,0.98}`、`menu-shell.c`・`network.c`・`volume.c`・`switcher-shell.c` の同じ青と、それらの上の白（ink に）。greeter（login・lock）は従わせない（§10 (c)） |
| S2 compositor の青 `{0.25,0.52,0.98}`（≒`0x4085fa`）は `0x2f7cf6` と違い、置き換えで既定の見た目が変わる | §10 (b)。推奨は統一（index 0 でも `0x2f7cf6` になる、差は小さい） |
| S3 4.5 は静止の状態だけ、hover の陰で割る | 主の button の hover・押下の陰は ink から遠ざかる向きに付ける（ink が白なら黒へ、黒なら白へ 8%）。基準は「静止と陰の両方で 4.6 以上」（余裕 0.1）。無効の主の button の文字 `KL_RGBA(0xffffff,220)` は `KL_RGBA(accent_ink, 220)`（無効の control は WCAG の対象外、色だけ揃える） |
| S4 地の色が足りない（control・card）、accent を文字に使う所の基準が無い | 地に card（α150 を ground_top の上）と control（α225）を加える。accent を文字に使う所（`list.c` の今の場所、Settings の `ui.c`、Files の `ui-tabs.c`・`ui-grid.c`・`ui-home.c`）は新しい `accent_text`（accent の色相で、地と selection の上で 4.6 以上）を使う |
| S5 色の丸の check が白固定、各色の ink を返す API が無い | §9 B1 の `ka_accent_values` が accent・ink・accent_text・selection を返す。丸の check はその色の ink |
| S6 protocol の細部 | `theme-protocol.c` の interface の version 2・event 2（`accent` の signature は `"2u"`、version 1 の proxy への event は libwayland の wire が拒む）。compositor の `protocol.c` の global を 2 に、`theme_send` は `object->version >= 2` の時だけ opcode 1。log は `KWL THEME appearance=N accent=M`（両方を 1 行） |
| S7 ws.md に p002 と規約の見直しが無い | ws.md の Phase の表に p002（6 app）と p003（規約の全文の見直し）を足す |
| S8 試験の不足 | 追加: index 0 の結果が今の `theme_light`・`theme_dark`・Settings と Files の palette の accent・selection と同じ（回帰）、compositor の `theme_send` が version 1 の object に opcode 1 を送らない（host、wire の記録）、`kwl_accent` が `shape.light = 1` を立てる。T1 は log（`KWL THEME … accent=N`、Settings・Files の `ACCENT index=N` の行）で判定し、撮影は 3 色（blue・yellow・graphite）× light・dark に絞り、Files の desktop の icon も撮る。AAT のシナリオ（`tests/scenarios/desktop/appearance/`）に accent を 1 本足すのは p003 の前に。色の丸の control の index は `LOOK_ACCENT_FIRST = 90`〜97（`LOOK_DARK=2` と、`index >= LOOK_PICTURE_FIRST(100)` の壁紙の範囲の手前） |
| S9 Settings の Network の graph の色（`page-network.c`） | data の色として従わせない（記録） |
| S10 red・green が danger・good と近い | §10 (a) |
| M1 | `exports.py` で `exports.map` を作り直し、`keiland.h` の KL_VERSION の説明に 57、`struct kl_theme` の説明（「One theme exists so far」）と `kl_appearance_fn` の説明（accent の変化でも呼ぶ）を直す |
| M2 | Settings: `look.c` で `appearance.accent` を読み `se_look` に `accent`、丸の列は左右の key で選び `kl_ui` の focus に入れる、色の名は `locale/settings.keys` に `msg` を足して `kl_tr`、行の名は「Accent color」（UI の英語の綴りに合わせる） |
| M3 | 置き換える白の定数: `widgets.c` 134・152、`list.c` 232、`views.c` 33〜34、`chooser-view.c` 576。focus の輪 `widgets.c` 419 の `KL_RGBA(0x2f7cf6,150)` は `KL_RGBA(theme->accent,150)` に |
| M4 graphite の selection と selection_inactive が区別できない | graphite の selection の α を light 60・dark 90 に上げる（他の色は 40・70） |
| M5 | Files の α の計算の所は `KL_RGBA(FM_COLOR_ACCENT, a)` |
| M6 | KL_VERSION 57 は未使用（確認済み） |

### 9.1 色の表（第 2 版、§2 の表を置き換える）

基準: (1) accent と accent_ink が静止と hover・押下の陰（ink から遠ざかる向き 8%）の両方で 4.6 以上、(2) accent が地（light: `0xffffff`・`0xeef2f7`・`0xe6ebf3`・card・control、dark: `0x23272f`・`0x1b1f26`・`0x16191f`・card・control）の全てに 3.0 以上、(3) accent_text が地と selection の上で 4.6 以上。色相を保ち、明るさだけを最も近い値に（scratchpad の計算、host-accent.c で固定）。

| index | 名 | light: accent / ink / text | dark: accent / ink / text |
| --- | --- | --- | --- |
| 0 | blue（既定） | `0x2f7cf6` / 白 / `0x2f7cf6`（今のまま、例外 §10） | `0x2f7cf6` / 白 / `0x2f7cf6`（今のまま、例外） |
| 1 | purple | `0x8553f5` / 白 / `0x6526f2` | `0x9367f7` / 黒 / `0xc0a6fa` |
| 2 | pink | `0xdb2676` / 白 / `0xa91c5a` | `0xe1488c` / 黒 / `0xee95bc` |
| 3 | red | `0xdc2f3c` / 白 / `0xaf1d28` | `0xe1505a` / 黒 / `0xee999f` |
| 4 | orange | `0xd4690b` / 黒 / `0x934908` | `0xe8730c` / 黒 / `0xf7a65e` |
| 5 | yellow | `0xa48207` / 黒 / `0x735b05` | `0xf5c518` / 黒 / `0xf6cc32` |
| 6 | green | `0x1e9a53` / 黒 / `0x156b3a` | `0x1f9d55` / 黒 / `0x29cf70` |
| 7 | graphite | `0x5b6472` / 白 / `0x4d5460` | `0x798494` / 黒 / `0xb9bfc7` |

黒は `0x16191f`。text の列は実装の時に、selection を card・control の上にも重ねた地で 4.6 を満たすよう取り直した（graphite の selection は α 60・90、host-accent.c が固定）。dark の外観では地の control・card が明るいので、purple・pink・red・graphite も accent が明るくなり ink は黒になる（§10 (e)）。

## 10. 人の判断（第 2 版、§7 を置き換える）

| | 問い | 推奨 |
| --- | --- | --- |
| 1 | blue（既定）: 今の値（`0x2f7cf6`・白、ink 3.94、文字に使う所 3.24/2.72）のまま例外にするか、基準に合わせるか（light・dark とも `0x337ff6`・黒の文字・文字用 `0x0955cd`/`0x7cadf9`、今の見た目が変わる） | 今のまま（例外として記録） |
| (a) | red・green が danger（`0xe5484d`）・good（`0x2fb45a`）と近い。そのままか、別の色相に寄せるか | そのまま（危険の button は文字と位置でも分かる） |
| (b) | compositor の青 `0x4085fa`（App Home の点・drop の線・glow・bar の popup）を既定でも accent（`0x2f7cf6`）に揃えてよいか | 揃える |
| (c) | login・lock の画面（greeter）と、Files の desktop の icon を accent に従わせるか | desktop の icon は従う、greeter は従わない（user の設定の前の画面を兼ねる） |
| (d) | yellow の light `0xa48207`（暗い芥子色）と dark `0xf5c518` の違いを「yellow」として出してよいか | よい（light の地の上で 3.0 を満たす最も明るい黄） |
| (e) | dark の外観で purple・pink・red・graphite の主の button の文字が黒になってよいか（白にすると accent が暗くなり dark の control・card の上で 3.0 を割る） | よい |

### 10.1 ユーザーの回答（2026-10-07、Q1 経由のクリック）

全部推奨どおり: 1 既定の blue は今のまま（例外として記録）、(a) red・green はそのまま、(b) compositor の `0x4085fa` は accent に揃える、(c) Files の desktop の icon だけ従い greeter は既定の青、(d) yellow は light `0xa48207`・dark `0xf5c518`、(e) dark の purple・pink・red・graphite の主の button の文字は黒。

### 10.2 実装で決めたこと（技術の範囲）

- compositor の dark の写像の外し方: shape を直に作る所は `shape.light = 1`、`glass_draw_*` を通す所は `kwl_accent_as_is`・`kwl_accent_done`（`server->keep_colours` を退避して立てる）で accent の図形とその上の ink だけを囲む。ground の選び方は `server->dark`、App Home と Wiseview は暗い地（1）。
- Files の desktop mode は外観を watch しない（libkeiland の theme が dark になり、desktop の rename の欄が暗くなるため）。代わりに `kl_settings` で `appearance.accent` を読み watch し、light の値を `fm_palette_take` で取る。
- 主の button の hover の陰: 主と危険の button は ink から遠ざかる向き（`widgets_shade`）、他の button は今のまま `theme->text` へ。blue の dark の hover は前（明るく）から暗くなる向きに変わる。
- Settings の色の丸の keyboard の操作（左右）は今回入れない（click だけ、backlog）。
