<!-- awesome-plan project=zedbsd record=ws090-design -->

# WS090 の設計: widget・control の共有 library（`libkeiui`）

2026-09-29 ws090-p001。目標は [ws.md](ws.md)（ユーザー「スクロールやボタンなど、ウィジェットやコントロールを共有ライブラリにする。独自のものでよい。
慣性スムーズスクロールは少なくともライブラリにして再利用したい。」）と、ユーザーの「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」
（master の決定の表、2026-09-29 夜）。この Phase は設計だけで source を変えない。

## 1. 今の姿（調べた事実、main a85ea4cc）

### 1.1 app ごとに重なっているもの

| 層 | 持っている app（行数） | 違い |
| --- | --- | --- |
| **窓の土台**: Wayland の toplevel・seat・入力の queue（`window.c`）、CPU の絵を Vulkan の swapchain で見せる（`present.c`）、glass の panel（`glass.c`）、titlebar の control（`titlebar.c`） | Files（window 1219・present 1212・glass 152・titlebar 453）、Settings（1344・1218・152・358）、Text Editor（1280・1212・112・313）、Image Viewer（1222・1699・191・369）、PDF Viewer（1145・1212・—・321） | `present.c` は Files・Text Editor・PDF Viewer・Settings で同じ骨格（1212〜1218 行。接頭辞をそろえると Files と Text Editor の差は 94 行）、Image Viewer は画像の texture を足した 1699 行。window は key の repeat・surface の扱い・clipboard の有無が少しずつ違う |
| **自分の Vulkan で描く app** | Terminal（`render.c`: cell を glyph の atlas から描く）、Notes（`render.c`: stroke を stencil で描く） | CPU の canvas を使わない。窓（Wayland・入力）と scroll の model だけが重なる |
| **touch**（`keiland_gesture`・`keiland_scroller` の結線） | Files 666、Image Viewer 828、PDF Viewer 819、Text Editor 392、Terminal 611、Notes 1216 | 同じ型（press で scroller、drag を frame ごとに resample、lift で glide、tap・long press）を app ごとに書いている |
| **canvas**（CPU の premultiplied ARGB） | Files 1396（最も多い: clip の stack・gradient・影・多角形・画像）、Text Editor 322、Image Viewer 283、PDF Viewer 350、libkeiland の内部の `paint.c` 540（file chooser） | 4 つとも中身が違う（md5 が全て違う）。Settings は Files の `canvas.c`・`text.c`・`icons.c` を **source のまま** compile（ws089-p002、F-038） |
| **text**（libtruetype、glyph の cache、fallback の font） | Files 741（fallback・行の折り返し）、Text Editor 718（Files の型）、Image Viewer 295、PDF Viewer 277（fallback なし）、libkeiland の内部の `paint-text.c` 636 | cache・fallback の有無が違う |
| **icon** | Files `icons.c` 359（線の絵）、Settings `glyphs.c` 419、libkeiland の chooser の place の絵 | 同じ意味の絵（home・folder・documents…）を 3 か所で描く |
| **key の文字**（US の配列の表、zdesktop は keymap を送らない） | Files `ui-field.c`、Text Editor `keys.c`、Terminal `keys.c`、PDF Viewer `view.c`、libkeiland `chooser-model.c` | 同じ表の写し |
| **wheel の滑らかな scroll** | Text Editor `app.c` だけ（時定数の glide）。他は wheel の量で即座に動く | — |
| **file chooser** | libkeiland の `keiland_file_chooser_*`（KEILAND_VERSION 12、wl_shm の窓）、Image Viewer `chooser.c` 228・PDF Viewer `chooser.c` 235（app の中に描く古い型） | 3 つ |

### 1.2 部品の使われ方

| 部品 | Settings（`widgets.c`・`ui.c`） | Files（`ui-*.c`） | Text Editor | Image Viewer・PDF Viewer | file chooser |
| --- | --- | --- | --- | --- | --- |
| button（主・普通・危険） | `se_button_draw` | 情報の card・overlay | dialog の 3 つの button | — | Cancel・Open・Save・Replace |
| switch（toggle） | `se_toggle_draw` | — | — | — | — |
| slider | 無い（preference の `pointer.speed`・`keyboard.repeat.*` は数値で、次の頁で要る） | — | — | zoom（titlebar の外） | — |
| text field（1 行） | `se_field_key`（Wi-Fi の key） | `ui-field.c`（rename・path） | 検索は titlebar（zdesktop の field） | — | 名前・path |
| list（行・列・選択・hover・key の移動） | 項目の pane の行 | `ui-list.c`・`ui-grid.c` | — | — | list |
| sidebar（section と場所の行） | 左の pane | `ui.c` の sidebar | — | — | sidebar |
| card・row（label と値）・header | `se_card_*`・`se_row_value`・`se_page_header` | 情報の card | dialog の card | — | 確認の card |
| scroll view（慣性・wheel・key・rubber band・scroll bar） | 右の pane | list・grid・sidebar | 本文 | 画像の pan（2 軸） | list |
| dialog（問いと 2〜3 の button） | — | 削除の確認 | 未保存の確認 | — | 上書きの確認 |
| message の chip（数秒で消える） | — | 状態の chip | 保存した・見つからない | 読めない等 | 拒否の理由 |
| progress | — | copy の task | — | — | — |
| menu・titlebar の control・検索の field | zdesktop が描く（`keiland_menu`・`keiland_titlebar`）。**library では作らない** | 同 | 同 | 同 | — |

## 2. 方針

1. **新しい共有 library `libkeiui`**（`/lib/libkeiui.so`、header `<keiui.h>`、接頭辞 `kui_`、版 `KUI_VERSION`）を作る。libkeiland は広げない。
   - 理由: libkeiland の役は「OS への唯一の入口と zdesktop の拡張の wrapper」（`keiland.h` の冒頭）で、zdesktop・xserver・probe も link する。
     部品の library は Vulkan と窓を持つので、libkeiland に入れると probe や zdesktop が要らない依存を負う。層の向きは **libkeiui → libkeiland**（一方向）。
   - `keiland_scroller`・`keiland_gesture`・`keiland_motion` は zdesktop も使うので **libkeiland に残し**、libkeiui の scroll view がそれを包む。
   - 名前の決まり: Keiland・keiui は内部の名前で、画面の文字には出さない（master の Kei の決まり）。
2. **即時の描画（immediate mode）と app が持つ小さな状態**: 今の app は全て「入力で状態を変え、変われば frame 全体を CPU で描き直す」型。
   部品は `kui_button(ui, id, rect, label, flags)` のように **描きながら入力を消費し、結果（押された等）を返す**関数にする。
   選択・文字列・scroll の位置のような状態は app が持つ小さな struct（`struct kui_field`・`struct kui_scroll`・`struct kui_list`）に置く。
   部品の木や生存の管理（retained）は作らない（C で所有権が複雑になり、今の app の型から遠い）。
3. **見た目は 1 か所**: Files の色・角・行の高さ・文字の大きさ（ws071 spec）を `struct kui_theme`（Kei の light の 1 つ）に集め、全部品がそこから描く。
   dark・accent の変更は今は作らない（preferences の key が来たら theme を差し替える口だけ残す）。
4. **窓の土台も library に**（重なりの大半、1 app あたり約 3000 行）。**窓（Wayland と入力）と見せ方（present）を分ける**。`kui_window` は Wayland の toplevel・seat（自分の surface 以外の event は
   無視する、ws092-p003 の教訓）・key の repeat（`wl_keyboard.repeat_info`、zdesktop は preference の `keyboard.repeat.*` から送る）・US の配列の文字・
   touch（gesture）・glass の panel・frame の間引きを持つ。見せ方は 3 つ: CPU の canvas を **Vulkan の swapchain で見せる**（既定。ws035 の合成の
   設計でユーザーが承認した「GPU の経路が主、wl_shm は補助」）、**wl_shm**（Vulkan を持たない小さな副の窓: file chooser・dialog の窓、Vulkan が
   使えない時の代わり）、**無し**（Terminal・Notes のように app が `kui_window_surface()` の上に自分の Vulkan で描く。窓と入力だけを使う）。
5. **移行は app ごとに、見た目を変えずに**。1 つの Phase で 1 つの app を移し、移す前後の画面を並べて比べる。

## 3. library の構成（`userland/desktop/libkeiui/`）

| 層 | file | 中身 | 元 |
| --- | --- | --- | --- |
| 描画 | `canvas.c` | `kui_canvas`: clip の stack、fill・gradient・角丸・角丸の縁・影・円・環・線・多角形・mask・画像（premultiplied） | Files `canvas.c`（上位集合）に libkeiland `paint.c` の縁と線を合わせる |
| | `text.c` | `kui_text`: 主と fallback の font、glyph の cache、幅・fit（省略記号）・折り返し（`break`）・行の測り | Files `text.c`（fallback と break を持つ） |
| | `icons.c` | `kui_icon_draw(canvas, KUI_ICON_*, x, y, size, color)`: home・folder・file・documents・downloads・desktop・computer・recent・trash・up・close・chevron・check・search… | Files `icons.c`・Settings `glyphs.c`・chooser の place の絵を 1 つに |
| | `theme.c` | `struct kui_theme` と既定の `kui_theme_default()` | Files の `FM_COLOR_*`・寸法 |
| 入力 | `input.c` | `kui_ui` への入力（pointer・key・文字・touch の gesture・wheel・時刻）、key の文字（US の表 1 つ）、IME の commit と preedit の口（WS095） | 各 app の keys 表、textedit の preedit の口 |
| 部品 | `ui.c` | `kui_ui` の frame（begin・end）、id と hot・active・focus、hit の記録、Tab の順 | Files `fm_ui_hit`、Settings の hit |
| | `scroll.c` | `kui_scroll`: 1〜2 軸、`keiland_scroller`（touch の慣性・rubber band）＋wheel の glide（textedit の時定数）＋key（行・page・端）＋scroll bar（細く、動く時だけ濃く）＋「見える所へ」 | textedit `touch.c`・`app.c`、各 app の touch.c |
| | `button.c`・`switch.c`・`slider.c` | button（主・普通・危険・無効）、switch（Settings の toggle）、slider（連続・段、key と drag と tap） | Settings `se_button_*`・`se_toggle_draw` |
| | `field.c` | 1 行の text field（UTF-8、選択、←→・Home・End・Backspace・Delete・Ctrl+A、clipboard は window 経由、IME の preedit を下線で） | Files `ui-field.c`・chooser の field・Settings `se_field_*` |
| | `list.c` | list（行の高さ、列、icon、選択（1 つ／複数）、hover、↑↓・PageUp/Down・Home/End・文字で移動、double click と tap、scroll を内に持つ） | Files `ui-list.c`、chooser の list |
| | `sidebar.c`・`card.c` | sidebar（section の title と場所の行、今の場所の強調）、card・row（label と値）・page の header | Files `ui.c`、Settings `widgets.c` |
| | `dialog.c`・`chip.c`・`progress.c` | dialog（題・本文・2〜3 の button、Enter と Esc）、message の chip（時間で消える）、status の chip、progress の bar | textedit の dialog・message、chooser の確認 |
| 窓 | `window.c`・`present.c`・`present-shm.c` | `kui_window`（§5）、Vulkan の present（Files の `present.c`）、wl_shm の present（chooser の `chooser.c` の buffer の部分） | 各 app の window・present、libkeiland `chooser.c` |
| | `clipboard.c` | clipboard と PRIMARY（text だけ、drag and drop は除く） | textedit `clipboard.c`・`primary.c`（Terminal の型） |
| 部品（大） | `chooser*.c` | `kui_file_chooser_*`（§7） | libkeiland の `chooser*.c` を移す |

依存: libc、libtruetype、libwayland-client、libvulkan、libkeiland（NEEDED）。host の試験は `canvas.c`・`text.c`・`icons.c`・`theme.c`・`input.c`・`ui.c`・
部品の file を Linux で build する（Wayland と Vulkan を知らない層に分けておく。chooser の model と同じ作り）。

## 4. API の形（抜粋。全体は p002 以降に header で決める）

```c
/* 描画 */
struct kui_canvas;   /* app が持つ pixels の上の canvas（kui_window が frame ごとに渡す） */
struct kui_text;     /* 主と fallback の font */
struct kui_rect { int x, y, width, height; };
void kui_canvas_round(struct kui_canvas *canvas, const struct kui_rect *rect, float radius, uint32_t color);
int kui_text_draw_fit(struct kui_text *text, struct kui_canvas *canvas, int x, int baseline, const char *utf8, unsigned pixels, unsigned weight, int width, uint32_t color);

/* frame の文脈: 入力を受け、部品を描き、結果を返す */
struct kui_ui;
struct kui_ui *kui_ui_create(struct kui_text *text, const struct kui_theme *theme);
void kui_ui_begin(struct kui_ui *ui, struct kui_canvas *canvas, uint64_t now_ms);   /* 前の frame の hit で入力を解く */
int kui_ui_end(struct kui_ui *ui);                                                   /* 1: 動きが続く（次の frame が要る） */

/* 部品（id は app が決める 0 以外の数。同じ frame で重ならない） */
int kui_button(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, const char *label, unsigned flags);   /* 1: 押された */
int kui_switch(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, int *on);                           /* 1: 変わった */
int kui_slider(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, double minimum, double maximum, double step, double *value);
int kui_field(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, struct kui_field *field, unsigned flags); /* KUI_FIELD_CHANGED・_SUBMITTED・_CANCELLED */
void kui_scroll_begin(struct kui_ui *ui, uint32_t id, const struct kui_rect *viewport, struct kui_scroll *scroll, double content_width, double content_height);
void kui_scroll_end(struct kui_ui *ui, struct kui_scroll *scroll);                                            /* scroll bar を描く */
int kui_list(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, struct kui_list *list, const struct kui_list_source *source); /* KUI_LIST_SELECTED・_ACTIVATED */

/* 窓 */
struct kui_window *kui_window_open(const struct kui_window_options *options, const struct kui_window_listener *listener, void *data);
int kui_window_fd(struct kui_window *window);            /* app の poll に入れる */
int kui_window_dispatch(struct kui_window *window, int timeout_ms);
void kui_window_redraw(struct kui_window *window);       /* 次の frame で listener の draw が呼ばれる */
```

- 返り値の約束は keiland.h と同じ（失敗する呼び出しは 0 か errno）。部品は「何が起きたか」の bit か 0・1 を返す。
- **id**: 部品の同一性は app が渡す id（frame をまたいで同じ部品に同じ id）。list の行・sidebar の場所は **`(id, index)` の組をそのまま鍵にする**
  （hash にまとめない。衝突しない）。同じ frame で同じ鍵を 2 回使うのは app の誤りで、debug の build で報告する。
- **入力の解き方**: pointer の press は前の frame に記録した hit の矩形で「active」の部品を決め、release が同じ部品の上なら押されたとする（Kei の他の
  部分と同じく release で決める）。touch の tap は press と release の組として同じ道を通る。drag は active の部品（slider・scroll）が取る。
  hover は pointer の動きで決める。**前の frame の hit の矩形で先に判定し、hover の部品が変わった時だけ次の frame を求める**（pointer が動くたびに
  全体を CPU で描き直さない）。
- **部品の外の入力**: pointer の press・touch の down が前の frame のどの部品の矩形にも当たらなければ、`kui_window` の listener の `pointer`・`touch`
  へ渡す（本文・画像・PDF の頁のような app 独自の描画）。scroll view の中の drag は scroll view が取り、その中の部品は tap だけを受ける。
- **focus と key**: Tab・Shift+Tab は部品を描いた順。Enter・Space は focus の button・switch を押す。focus の枠は keyboard で動かした時だけ描く。
- **IME**（WS095）: field は focus を得たら `kui_window` に text input の enable と cursor の矩形を伝え、commit を挿入、preedit を下線で描く。
  結線は WS095 の libkeiland の helper（ws095-p006）の上に作る（WS095 の p006 が前提。それまでは US の配列の文字だけ）。

### 4.1 p005 で決めたこと（上の抜粋との違い）

- 部品は `struct kui_style`（canvas・text・theme・glass）を受ける（`kui_ui` は入力だけを持ち、描く道具は持たない）。
- **key の宛先**: key は押された時に focus のあった部品に宛てて frame まで待つ（`struct ui_press` の target）。frame の間に Tab が来ても、Tab の前の
  key は前の部品、後の key は次の部品が取る（QEMU で「Tab の前の文字が app に落ちる」誤りを見つけて直した）。部品が要らない key と、focus の
  無い時の key は app の `KUI_EVENT_KEY`。部品は自分の要る key だけを取る（field の Ctrl は A だけ、slider・list は Ctrl・Alt の無い矢印など）。
- **focus**: 押した部品が keyboard を取らなければ、その下の同じ id の取る部品が取る（list の行 → list 自身。行は Tab の止まり所にならない）。
  dialog の記録は `KEIUI_MODAL` で、Tab はその後に描いた部品（dialog の button）だけを回る。focus の枠は Tab で動かした時だけ描く。
- **drag の終わり**: slider のような pointer に従う部品は、放した frame にも held と見る（frame の間の最後の動きと release を失わない）。
- **hover**: 各 frame の終わりに止まった pointer の下の部品を求め直す（wheel で list が動いた時の光る行）。
- dialog は `labels[0]` が主（右、Enter）、最後が取り消し（Esc）。button は `(id, index)`、dialog 自身は `(id, 0xffffffff)`。

## 5. 窓の土台（`kui_window`）

- options: 題、app_id、大きさ（最小を含む）、見せ方（`KUI_PRESENT_VULKAN` 既定、`_SHM`、`_NONE`: app が `kui_window_surface()` に自分で描く）、glass を使うか、親の toplevel（dialog の窓）、
  titlebar の presentation と menu は **app が今までどおり `keiland_titlebar`・`keiland_menu` で持つ**（`kui_window_toplevel()` で xdg_toplevel を渡す）。
- listener: `draw(data, canvas, now)`（frame ごとに 1 回、app は `kui_ui_begin`〜`end` で描く）、`resize`、`close`（close の button）、
  `key`（部品が使わなかった key。app の shortcut）、`pointer`・`touch`（部品の外、app 独自の描画の上の入力: 本文・画像・PDF）、`focus`。
- 入力の正規化: 自分の surface 以外の enter・key・touch は捨てる（file chooser のような副の窓がある時の約束）。key の repeat は `repeat_info` の
  値で library が作る。modifier の bit は zdesktop の値から `KUI_MOD_*` へ。
- frame: 描き直しは要る時だけ（入力・`kui_window_redraw`・部品が「動きが続く」と返した時）、Vulkan は present の後、shm は `wl_surface.frame` で間引く。
  app は `kui_window_fd` を自分の poll に入れる（Terminal の pty、Files の task のような他の fd と一緒に待てる）。
- glass: `kui_window_set_panels()`（`keiland_glass` を包む）。see-through の swapchain が無ければ theme の不透明の地で描く（今の各 app の判断と同じ）。
- **Image Viewer・PDF Viewer の画像**: 今の Image Viewer の present（画像の texture、1699 行）のように CPU の canvas の下に GPU の画像の層を置く口
  （`kui_window_set_layer()`）は、Image Viewer を移す Phase で決める（それまで Image Viewer は自分の present を保つ）。

**実装での変更（ws090-p004、2026-09-30）**: 入力は listener（callback）ではなく **event の queue**（`kui_window_take`）にした。今の app は全て
`te_window_take` のような queue で入力を読み、BUG-111（key の repeat は dispatch の後）の順序を app の loop で決めているため。大きさの変更と close の
要求も event（`KUI_WINDOW_RESIZE`・`KUI_WINDOW_CLOSE`）。menu・titlebar の選択の serial を selection に使うため `kui_window_set_serial`、context menu の
ため `kui_window_seat` を足した。library は app の log を出さない（app が呼び出し口で出す）。

## 6. scroll view（ユーザーの最低限の要望）

- 1 つの `struct kui_scroll` が 1 つの scroll する領域の状態: 位置（x, y）、内容の大きさ、`keiland_scroller`（touch の drag・慣性・rubber band・
  glide 中の press で止める）、wheel の glide（目標の位置へ時定数 70 ms で近づく、textedit の `APP_GLIDE_MS` と同じ。始めた時刻からの式で、時刻だけで決まる。Ctrl+wheel は app に渡す）、key（↑↓ 1 行、
  PageUp/Down は viewport−1 行、Home・End。list と本文の field が focus の時はそちらが先）、scroll bar（右と下、細い、動いた後 1 秒で薄れる、
  drag できる）、`kui_scroll_reveal(scroll, rect)`（選択を見える所へ）。
- 描画は `kui_scroll_begin` が canvas の clip と原点を移し、中の部品は内容の座標で描く。`kui_scroll_end` が clip を戻して scroll bar を描く。
- 動いている間（glide・rubber band）は `kui_ui_end` が 1 を返し、窓は次の frame を描く（app に timer の義務は無い）。
- Terminal（行の単位の scroll）と Notes（ペンと 2 本指の区別）は自分の Vulkan で描くので、scroll の model（`kui_scroll` の位置と慣性、描画なし）だけを使う。

## 6.1 文字を編集する view の touch（`kui_text_touch`、2026-09-29 ユーザー）

ユーザーの判断（main 経由）「スクロールは2本指にするのと、共通部品にしましょう。」→ 文字を編集する view（Text Editor の本文、text field）では、
**1 本指の drag は選択、scroll は 2 本指**。これを libkeiui の共通の部品にする。Files・Image Viewer・PDF Viewer・list の 1 本指の pan は変えない（main の解釈）。

| 指の動き | 意味 |
| --- | --- |
| tap | caret を置く（選択を消す）。glide 中の tap は止めるだけ |
| double tap | 語を選ぶ |
| 1 本指の drag | 押した所から選択を広げる。view の上端・下端（field は左右の端）から 24 px の内に入ると、その距離に比例して自動で scroll |
| 2 本指の drag | scroll（`kui_scroll` の慣性と rubber band、2 本の重心）。2 本指の判定は drag の始まり（8 px 動いた時）の指の数。1 本指で選択を始めた後に 2 本目が来ても選択のまま |
| long press（500 ms、動かない） | context menu（今の Text Editor と同じ）。その後に動かしても何もしない |
| つまみ | **作る**（決定）。touch で選択した時だけ、選択の両端に小さな丸（直径 12 px、accent）を出し、その丸（当たりは 44 px の円）を drag すると端を動かす。pointer か key で選択を変えると消える。1 本指の drag が選択になったので、範囲を後から直す手段として要る |

- 部品の形: `struct kui_text_touch` を view が持ち、view は「点 → 文字の位置」「位置 → caret の矩形」の 2 つの関数を渡す（Text Editor の layout と
  field の両方が満たせる形）。結果は caret・選択の範囲・context menu の要求・scroll の量として返す。
- 作る Phase: p003（入力の層と scroll と一緒に）。Text Editor へ入れるのは p004（窓の移行と一緒に）、text field（`kui_field`）は p005。
- design.md §2.1 の旧 J10（textedit の「選択のつまみは作らない」、plan/ws092/design.md）はこの判断で置き換わる。

## 7. file chooser の移動

- `keiland_file_chooser_*` を `kui_file_chooser_*` として libkeiui に移す（API の形は同じ、options と listener も同じ意味）。中身は部品
  （sidebar・list・field・button・dialog・scroll）と `kui_window`（shm の backend）で作り直す。見た目は今と同じ（Files に揃えた）。
- libkeiland から `keiland_file_chooser_*` と内部の `paint*.c`・`chooser*.c` を取り除き、**KEILAND_VERSION を上げる**（その時の main の最新の次。
  今は 13 → 14 の見込み。版の注記に「12 の file chooser は libkeiui へ移った」と書く）。**関数を取り除く破壊的な変更**で、`KEILAND_VERSION` で
  古さを見る程度の約束（keiland.h の冒頭）では守れない。使い手は in-tree の textedit だけで、同じ Phase・同じ build で移すので許す。
- Image Viewer・PDF Viewer の app の中の `chooser.c` は、それぞれの app を移す Phase で `kui_file_chooser` に替える。
- 試験: `plan/tools/keiland/host-chooser.*` は model の試験なので、移した model に合わせて `plan/tools/keiui/` に移す（main の Tools 節も）。
- p006 で決めたこと: chooser の窓は app の接続の上の `kui_window`（`keiui_window_open_shared`: 自分の queue で global を探し、app の default queue
  に移す。shm の見せ方。自分の xdg_wm_base を bind して閉じる時に destroy する。BUG-112 の回避の「binding を持ち続ける」は外した）。窓は入力を
  queue に積んだ後に `wl_display.sync` で chooser を起こす（`keiui_window_set_notify`）。描画は frame callback で間引く。答えは窓を消した後の
  sync で伝える（今までと同じ）。model（`chooser-model.c`）は選択と scroll を `kui_list`、名前と path を `kui_field` で持ち、view（`chooser-view.c`）
  が部品の報告と部品の取らなかった key で model を動かす。
- key の順（p006 で直した）: 部品が要らない key に当たると、その部品はその後の key を取らない。部品の取らなかった一番古い key が app の
  `KUI_EVENT_KEY` になり、その後の key は次の frame を待つ（「z のあと Enter」が 1 frame に来ても z の選択の後に Enter が開く）。
- `KUI_HIT_TOUCHED`・`KUI_LIST_TOUCHED`（KUI_VERSION 5）: 指の tap での click（chooser の folder は 1 回の tap で入る）。
- Text Editor の dialog は `kui_dialog`、message は `kui_chip`（Ln/Col の status の chip は今のまま）。dialog の間は pointer と key を `kui_ui` に渡す。

## 8. 版（version）

- `KUI_VERSION` は 1 から。部品や関数を足した Phase ごとに 1 つ上げ、header の冒頭に「版: 何が入ったか」を並べる（keiland.h と同じ書き方）。
  in-tree の app だけが使うので、互換の維持より「一緒に build される app が全て通る」ことを受け入れにする。
- `KEILAND_VERSION` は libkeiland を変える時だけ（file chooser を取り除く p006）。適用の直前に branch を main に合わせ、main の最新の次にする
  （WS089・WS095 も上げるので、報告に番号を明記）。

## 9. 移す順番と理由

| 順 | 対象 | 理由 |
| --- | --- | --- |
| 1 | （library の本体 p002〜p006） | Settings の描画の層は当初 1 番の予定だったが、WS089 が作業中のため p007 へ回した（2026-09-29 main） |
| 2 | Text Editor | 最も新しく小さい Vulkan の app（WS092 完了）。窓の土台・scroll view・dialog・chip・file chooser を一通り使う。移した後の回帰は `plan/tools/textedit/host-core.sh` と QEMU |
| 3 | Settings（描画・部品・窓） | Files の source の共有（F-038）を替え、button・switch・field・card・row・sidebar の主な使い手になる。WS089 の完了の後 |
| 4 | PDF Viewer・Image Viewer | 自前の chooser を `kui_file_chooser` へ、touch と present。Image Viewer は画像の層（§5）を決める |
| 5 | Files | 最大（2.9 万行）で固有の UI（grid・tabs・preview・drag and drop）が多い。canvas・text・icons・scroll・field・list から段階に |
| 6 | Terminal・Notes | 窓（見せ方は「無し」）と scroll の model だけ（描画は自分の Vulkan） |

**デモ（10/17）との関係**: master の決定では 10/10 ごろまで新規実装、その後は bug の修正と実機の調整だけ。Files・Text Editor・Image Viewer は
デモの app なので、移行は **10/10 までに移し終えたものだけ残し、間に合わないものは移さない**（移行の Phase は app ごとに独立で、途中で止めても
他の app は今のまま動く）。判断 J5。

## 10. Phase（案）

| Phase | 目的 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| ws090-p001 | 設計（この文書） | 設計の見直しを終えた文書 | — |
| ws090-p002 | libkeiui の骨組みと描画の層（canvas・text・icons・theme）、build の規則、host の試験（描画を PPM に書いて比べる、Files の実装と同じ絵になること）。**Settings の書き換えは p007 へ**（2026-09-29 main: WS089 が Settings の source を触っている間は変えない） | host 試験（Files の実装との一致と期待の絵）、`libkeiui.so` の build warning 0 | p001 |
| ws090-p003 | scroll view（`kui_scroll`）と入力の層（`kui_ui`・input）と文字の view の touch（`kui_text_touch`、§6.1）、host の試験（wheel の glide・key・慣性の時刻の列、1 本指と 2 本指の区別・つまみ） | host 試験（慣性は libkeiland の scroller と frame ごとに同じ位置）。Text Editor への組み込みと QEMU は p004（image への登録と一緒に。2026-09-29 の実行中に移した） | p002 |
| ws090-p004 | 窓の土台（`kui_window`、Vulkan・shm・無しの見せ方、clipboard）。Text Editor の窓・present・touch（`kui_text_touch`: 1 本指で選択、2 本指で scroll）・clipboard を移す（dialog・chip は p005 の部品ができてから p006 で） | Text Editor の QEMU の確認（ws092-p005 と同じ項目: touch・PRIMARY・clipboard・Files から開く）、host-core 34/34 | p003 |
| ws090-p005 | 部品（button・switch・slider・field・list・sidebar・card・row・header・dialog・chip・progress）と見本の program（`/bin/kuidemo`、画面の試験用） | 部品ごとの host 試験、見本の画面、QEMU で pointer・key・touch | p003 |
| ws090-p006 | file chooser を libkeiui へ（部品と shm の窓で作り直す）、libkeiland から取り除き KEILAND_VERSION を上げる、Text Editor の chooser・dialog・chip を部品に替える | 移した host-chooser の試験、QEMU で Open・Save As・上書き・取り消し | p004・p005 |
| ws090-p007 | Settings を libkeiui へ: 描画の層（Files の source の共有を替える、p002 から移した）、部品と窓の土台（WS089 が終わってから） | Settings の画面が前と同じ（QEMU の画面を並べる）、WS089 の試験 | p005、WS089 の完了 |
| ws090-p008 | PDF Viewer と Image Viewer を移す（chooser、touch、窓。Image Viewer の画像の層） | 各 app の QEMU の確認 | p006 |
| ws090-p009 | Files（その 1）: 描画の層（canvas・text・icons・theme）と scroll view を libkeiui に替える | Files の既存の試験（`plan/tools/files/`）と画面が前と同じ | p003 |
| ws090-p010 | Files（その 2）: field・list・sidebar・dialog・chip を部品に、窓の土台を `kui_window` に | 同上、QEMU で pointer・key・touch | p009、p005、p004 |
| ws090-p011 | Terminal と Notes: 窓（見せ方は「無し」）と scroll の model | 各 app の試験 | p004 |
| ws090-p012 | 規約の全文との照合と回帰（boot test） | style-check 0、build warning 0、host 試験、boot test | 全て |

p007〜p011 は app ごとに独立（J5 のとおり、デモの前に止めてよい）。p002〜p006 が library の本体。

## 11. 試験

- host: 描画の層は決まった入力で PPM を描き、期待の PPM と比べる（期待の絵は最初の実行で作り、目で確かめて commit。浮動小数の丸めの違いを
  許すため、各 channel の差 2 以下を同じとみなす）。部品は入力の列
  （press・release・key・tap・時刻）を `kui_ui` に与え、返り値と状態を確かめる。scroll は時刻を与えて位置の列を確かめる（`keiland_scroller` の
  既存の試験の数式と合わせる）。chooser は今の 75 件を移す。
- QEMU: 見本の program と移した app の画面（`build/ws090-shots/`）、入力は QMP と注入の touch（`build/main-pen/hdd-image.img` の複写）。
  判定は画面と app の log と SSH（console・serial の log は読まない）。
- 回帰: 移した app の既存の試験（`plan/tools/textedit/host-core.sh`、`plan/tools/files/`、WS089 の試験）、各 Phase の最後に boot test。

## 12. 範囲外（今は作らない）

- 表示の倍率（HiDPI の scale）: zdesktop は今 scale 1 だけ。来たら canvas と theme の寸法を倍率で掛ける。
- 右から左の文字・複雑な shaping・読み上げ（accessibility）: libtruetype に shaping が無い。
- 複数の thread からの呼び出し: 全て 1 つの thread（app の main loop）から。
- menu・titlebar の control（zdesktop の役、J8）、drag and drop（Files の固有、p010 で決める）。

## 13. 判断が要る点（既定を選んだ）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| J1 | 置き場 | 新しい `libkeiui`（libkeiland は広げない） | 役の分離、probe と zdesktop に Vulkan と部品の依存を負わせない。層は libkeiui → libkeiland の一方向 |
| J2 | API の型 | 即時の描画＋app の小さな状態 | 今の app の「変われば全体を描き直す」型に合い、C で木の所有権を持たない |
| J3 | 窓の土台を含めるか | 含める（Vulkan が主、shm が補助） | 重なりの大半。ws035 の合成の設計（GPU が主） |
| J4 | theme | Kei の light の 1 つ（Files の値）、切り替えの口だけ | dark・accent の要望はまだ無い |
| J5 | デモとの関係 | 10/10 までに移し終えた app だけ移す。p002〜p006 を先に | デモの app を壊さない（master の決定） |
| J6 | chooser の API | `kui_file_chooser_*` に名前を変えて移し、libkeiland から消す | 使い手は textedit だけ。二重に持たない |
| J7 | 名前 | `libkeiui`・`<keiui.h>`・`kui_`（内部の名前、画面に出さない） | Kei の決まり（Keiland は内部だけ） |
| J8 | menu・titlebar | 作らない（zdesktop が描く `keiland_menu`・`keiland_titlebar` のまま） | System Menu と浮いた titlebar は zdesktop の役 |
