<!-- awesome-plan project=zedbsd record=ws216-p006 -->

# ws216-p006: Notes の描く道具の UI（描く道具の button と帯、Tool menu、key、色と太さの表、pen・mouse・指での選択と塗り）

Parent: [WS216](../ws.md)
Status: in-progress（2026-10-13 P1。設計を先にこの phase.md に書き、Q1 が design-reviewer に回す。実装と host の受け入れへ。QEMU・実機は p007 で T1）
Phase disposition: normal
Queue: q944（2026-10-13 Q1 の依頼、P1）。承認はユーザーの再開の指示（2026-10-13「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」）と ws.md の D1〜D12（UI は D3 (a)）

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md) の §7（D3 (a) の帯、action の番号、`NOTES_BUTTONS` 48、帯と notice、Tool menu、key、色と太さの表、塗りの操作）と §8 の p006 の行。review は [review-1](../phase001/review-1.md)（M6・M11・L7）と [review-2](../phase001/review-2.md)。p005 の残件（[p005 §6.4](../phase005/phase.md)・L-8・M-5）。
- p003〜p005 の出力（道具の種類 2〜5 の model、画面、`notes_fill_page()`・`notes_fill_notice()`）は main に入った。
- 触る file: `userland/desktop/notes/`（`ui.c`・`main.c`・`menu.c`・`app.h`・`fill-page.c`）、`plan/ws216/`、AAT の helper `plan/tools/aat/scenarios/helpers_notes_edit.py`（Pen を押す所の追従だけ、Q1 の割り当て）。
- 触らない: libpdf、HAL・toolchain、model・画面・塗りの中身（p003〜p005）、`touch.c`（§5: 変える要が無い）。
- 試験の方針: build（amd64・rpi4、Linux の host rules）warning 0 と host 試験（plain・ASan・UBSan）。QEMU・実機は p007（T1）。

## 1. toolbar の描く道具の button（D3 (a)）

- Pen と Marker の 2 つの button を、1 つの **描く道具の button**（`NOTES_ACTION_DRAW 48`）に替える。label は今の描く道具（最後に選んだ描く道具）の名: Pen・Brush・Pencil・Water・Marker・Fill。label の幅が変わっても後ろの button が動かないよう、一番長い label の分の余りを後ろに残す（今の Eraser と同じ作り、`ui_eraser_slack()`）。
- 押した時:
  - 今の道具が描く道具でない（消しゴム・Select・Text）: 最後に使った描く道具に戻る（`NOTES TOOL n name=…`）。
  - 今の道具が描く道具: 帯を開く（開いていれば閉じる）。log `NOTES DRAWS open` / `NOTES DRAWS closed`。
- 描く道具の時は button を選んだ色（今の Pen・Marker と同じ薄い青の pill）。
- `NOTES_BUTTONS` を 48 にし、`ui_add_button()` は表が満ちて捨てる時に一度だけ `NOTES BUTTONS overflow action=N` を出す（review-1 M6）。

## 2. 帯（6 つの描く道具）

- 帯は toolbar の絵の notice の行（toolbar の帯の下の 44 px）に描く glass の小さな card。toolbar の card と同じ見た目（`ui_glass()`、影）で、高さ 40 px、button は 36 px。左端は描く道具の button の左に合わせ、窓の中（左右 8 px）に収める。
- button の並び: Pen（1）・Brush（49）・Pencil（50）・Water（51）・Marker（2）・Fill（52）。今の描く道具は薄い青の pill。action の番号は p001 §7 のとおり（Pen・Marker は今の番号）。
- 帯が開いている間は notice（status の pill が card の下に出る時）を出さない。閉じた時にまだ時間が残っていれば出る（帯が優先、review-1 L7）。status が card の横に出る時はそのまま。
- 閉じる: 帯の button を選ぶ（その道具になって閉じる）、描く道具の button をもう一度押す、帯の外を押す（toolbar の別の button はその button の働きもする。頁の上の押しは閉じるだけで描かない・塗らない）、道具を他の手段（menu・key）で替える。
- 帯の button は `NOTES BUTTONS` の log に出る（帯が開いた時の toolbar の描き直しで出る。AAT はこの行から押す場所を取る）。
- 当たり: pen・mouse の押しは `app_input()` で、指の tap は `app_touch()` で、toolbar の当たりより先に帯の矩形を見る（帯は toolbar の帯の下、頁の上にあるので）。Finger が on の時の指の書き始めも帯の上なら帯の button。

## 3. 道具ごとの色と太さ（p001 §7 の表）

| 道具 | 色 | 幅（pt） |
| --- | --- | --- |
| Pen | ペンの 5 色 | {1.5, 3, 6}（今） |
| Marker | 蛍光ペンの 5 色（alpha 0x66） | {10, 16, 24}（今） |
| Brush | ペンの 5 色 | {3, 6, 12} |
| Pencil | ペンの 5 色 | {0.8, 1.4, 2.4} |
| Water | ペンの 5 色（形の alpha は p003 の式） | {8, 16, 28} |
| Fill | ペンの 5 色（不透明） | 幅の欄を出さない |

- 色の index と幅の index は道具の間で共有（今どおり）。`notes_ui_color()`・`notes_ui_width()` が道具の種類（`NOTES_TOOL_*`）で表を選ぶ。
- 描く: `app_input()` の描き始めは、action から道具の種類を選び（`app_draw_kind()`）、その色と幅で stroke を作る。描いている途中の形は p003 の式（焼き込む前）で、離した時に `notes_document_add_stroke()` が焼き込む（今の経路のまま）。
- pen の mark（hover）: 筆・鉛筆・水彩は今のペンと同じく色と幅の点。塗りつぶしは色の点（半径 5 px）に白い輪。
- `NOTES STROKE … tool=N` は今どおり種類の番号を出す。

## 4. Tool menu と key

- Tool menu: Pen・Marker の後に Brush・Pencil・Watercolor・Fill の radio（Eraser・Select・Text の前）。radio の印は今の道具。
- key（Control なし）: B 筆、L 鉛筆、W 水彩、F 塗りつぶし（今の P・M・E・T と同じ。text box が開いている時は box の字、今どおり）。Control+W は今どおり Close。
- `NOTES TOOL n name=…` の名: pen・highlighter・brush・pencil・watercolor・fill（今の名は変えない）。

## 5. 塗りつぶしの操作

- pen・mouse: 塗りつぶしの道具で頁を押して、離すまでに押した点から 8 px 以内なら、押した点を塗る（`MAIN_CONTACT_FILL`）。それ以上動いたら何もしない（`NOTES FILL moved` を log）。
- 指: Finger が on の時だけ、書き始めから終わりまで 8 px 以内の指の線（Notes には書く指の BEGIN・END として来る）で塗る。Finger が off なら指は今どおり頁を動かす。
- double tap の拡大: `touch.c` は Finger が on の間すでに double tap の拡大を止める（`touch_double_tap()` は `write_mode` の間 return、WS081）。塗りの道具でも同じなので変えない（review-1 M11 は今の振る舞いで満たす）。
- 塗る前に status「Filling...」を出した frame を一度描き（`app_draw()`）、それから `notes_fill_page()` を呼ぶ（p005 L-8: 塗りは UI の thread を止めるので、止まっている間に「塗っています」が見える）。時間の上限は設けない（止めると半端な塗りは作れない。Pi の時間は p009 で測る）。終わったら status を消すか、断られた時は `notes_fill_notice()` の文を出す。
- 背景（p005 §3）: 頁が PDF の頁の上（`origin == NOTES_ORIGIN_OVER`）か編集がある時は、背景の painter（§6）を渡す。そうでなければ NULL（白い紙）。
- 成功: `app_changed()`。頁の絵は `reshaped`（下に入った時）か上に足す経路で描き直る。

## 6. 帯ごとの PDF の背景（p005 M-5 の続き）

- p005 は背景を頁の絵の解像度の texture のまま広げていた。p006 は `notes_fill_page()` の背景を「帯ごとに描く関数」にする: `notes_fill_background(context, top, width, height, scale)`。main.c の関数が、今の背景の display list（`app_background()` が作って持つ物。無ければ作る）を `pdf_display_list_rasterize(list, pixels, …, scale, 0, −top)` で帯の大きさに CPU で描き、`notes_renderer_background()` の絵（host の linear image）に写す。帯の frame はその絵を帯の大きさで描く（1 対 1）。
- 塗りの後、画面の背景の絵は帯の物になっているので、`app->background_page = NULL` にして次の frame で描き直させる。
- 失敗（display list が作れない、image が作れない）は塗りの失敗（`EIO`、notice "Could not fill the area"）。白い紙と見なして塗ると PDF の線を越えるので。
- CPU の raster の時間は帯ごとに加わる（A4 の帯 1 つで約 1 Mpx）。Pi の時間は p009。

## 7. AAT の helper の追従

- `plan/tools/aat/scenarios/helpers_notes_edit.py` の Pen を押す所（`notes.press(PEN, …)`）を、描く道具の button（48）を押し、`NOTES TOOL 1 name=pen` が出れば終わり、`NOTES DRAWS open` が出たら帯の Pen（1）を押して `NOTES TOOL 1 name=pen` を待つ、に替える。`DRAW = 48` を足す。
- 新しい AAT の scenario（`tests/scenarios/apps/notes/draw-tools.md`・`fill-region.md`）は p007 の T1 の依頼で扱う（文面は §9）。

## 8. 受け入れ

| # | 条件 |
| --- | --- |
| U1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk`、Linux の host rules）の `bin/notes` warning 0 |
| U2 | toolbar（host、`ui.c` を直に link）: 描く道具の時、button に DRAW（48）があり 1・2 は無い。帯が開いた状態で 1・2・49〜52 の 6 つが card の下（y が `NOTES_TOOLBAR_HEIGHT` 以上、絵の中）に並び、窓の中。描く道具の label が変わっても後ろの button の x が同じ。塗りつぶしでは幅の button が無い。1080 px の窓でも帯の button が窓の中。button の数が 48 以下。overflow の log が表の満ちた時に一度 |
| U3 | 色と幅の表（host）: 各道具の `notes_ui_color()`・`notes_ui_width()` が §3 の表 |
| U4 | 塗りの背景（host、p005 の試験に足す）: 背景の painter を渡した `notes_fill_page()` で、帯ごとに painter が帯の top と大きさで呼ばれ、帯の frame に背景の draw があり、painter の失敗は `EIO` で頁が変わらない |
| U5 | 既存の host 試験（`run-notes-fill.sh`・`run-notes-render.sh`・`run-notes-tools.sh`・WS079 の `run-notes-host.sh`・WS175 の 2 つ）が通る |
| U6 | 画面の操作（帯の開閉・tap・key・menu・塗りの tap と 8 px・指・notice）は p007 の T1（§9 の依頼）。host では main.c を動かせないので未実施と書く |

試験: `plan/ws216/tests/run-notes-ui.sh`（`host-notes-ui.c`: `ui.c` と libtruetype を、keiland の theme の小さな代わりと link）と `run-notes-fill.sh` の追加。出力は worktree の `build/ws216-p006-host/`。

## 9. p007 の T1 の依頼の文面（案、実装の後に Q1 へ）

（実装の後に書く）

## 10. 実行の記録

（実装の後に書く）
