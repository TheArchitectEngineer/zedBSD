<!-- awesome-plan project=zedbsd record=ws216-p006 -->

# ws216-p006: Notes の描く道具の UI（描く道具の button と帯、Tool menu、key、色と太さの表、pen・mouse・指での選択と塗り）

Parent: [WS216](../ws.md)
Status: cleared（2026-10-18 Q1: ws.md の表に記録済みの判定に合わせた。表: cleared（2026-10-13 Q1: T1-580 の QEMU の AAT で機械の判定 PASS・画を Q1 が見て合格。Pi 4 は p009、5330 の感触は p007 の A5））（旧: in-progress（2026-10-13 P1: 設計、review-1 とユーザーの決定の反映、実装と host の受け入れ U1〜U5 済み。test-wait: 画面の操作は p007 で T1（§9 の依頼）、Pi 4 は p009））
Phase disposition: normal
Queue: q944（2026-10-13 Q1 の依頼、P1）。承認はユーザーの再開の指示（2026-10-13「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」）と ws.md の D1〜D12（UI は D3 (a)）

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md) の §7（D3 (a) の帯、action の番号、`NOTES_BUTTONS` 48、帯と notice、Tool menu、key、色と太さの表、塗りの操作）と §8 の p006 の行。review は [review-1](../phase001/review-1.md)（M6・M11・L7）・[review-2](../phase001/review-2.md)、この段の設計の review は [p006 review-1](review-1.md)（条件つきで進めてよい）。p005 の残件（[p005 §6](../phase005/phase.md)）と [p005 code-review-1](../phase005/code-review-1.md)（q944 の中で直した、記録は p005 §6.4）。
- 依存: main に入った p003〜p005 の出力（道具の種類 2〜5 の model、画面と頂点の塊、`notes_fill_page()`・`notes_fill_notice()`）。p004・p005 自体は test-wait で、その clearance は p007 を待つ（p007 は p006 の後）。
- 触った file: `userland/desktop/notes/`（`ui.c`・`main.c`・`menu.c`・`app.h`・`fill-page.c`、p005 の直しで `fill.c`・`document.c`・`render.c`・`notes.h`）、`plan/ws216/`、AAT の helper `plan/tools/aat/scenarios/helpers_notes_edit.py`（Pen を選ぶ所の追従だけ、Q1 の割り当て）。
- 触らない: libpdf、HAL・toolchain、`touch.c`（§5）。
- 試験の方針: build（amd64・rpi4、Linux の host rules）warning 0 と host 試験（plain・ASan・UBSan）。QEMU・実機は p007（T1）。

## 1. review-1 とユーザーの決定

ユーザーの決定（2026-10-13、Q1 の AskUserQuestion）:

1. 水彩の label は「今のまま」: toolbar と帯は **Water**、Tool menu は **Watercolor**。
2. 描く道具の button に帯が開く印を「付ける」: label の後ろに小さな下向きの三角（toolbar の字は ASCII だけなので、▾ の字ではなく三角を描く）。
3. Pi で数秒かかる塗りは「待つ（上限なし）」: 「Filling...」のまま最後まで塗る。時間（帯の数と背景の CPU の raster の µs）は log に出す。
4. 塗りの道具で drag した時は「Tap to fill」の status を出す。
5. 帯を開いている間は notice（card の下に出る status）が出ず、閉じた時に時間が残っていなければ失われる — ユーザーへの報告とし、直さない（制限）。

review-1 への決定（Q1: 全部案どおり、技術の範囲）:

| # | 決定 |
| --- | --- |
| S-1 | 指の書き始めの contact が FILL（と帯を閉じるだけの TOOLBAR）の時も指の contact にし、END・ABORT が届く。離しを失った FILL（`app_end_contact(…, NULL)`）と ABORT は塗らない。動いた距離は押した点から一度でも 8 px を超えたか |
| C-1 | painter を渡すかは「画面がその頁の背景を描いているか」（`app_background()` が 1 を返すか）一つで決める。描いていなければ白い紙（NULL）。EIO は画面で描けているのに帯の raster が失敗した時だけ |
| C-2 | log `NOTES FILL bands=B background_us=U`。帯ごとの CPU の PDF の raster は D5（tap の一度）の例外の中の処理。p009 で時間を測り、遅すぎれば頁の絵の解像度の texture（p005 の LINEAR）に戻す道を残す |
| C-3 | AAT の helper は Pen を key P で選ぶ（`NOTES TOOL 1 name=pen` を待つ）。toolbar の座標に依らない |
| C-4 | 塗りでは幅の欄を描かずに、その幅の余りを残す（card と button が道具の間で動かない） |
| C-5 | tap の点で見えている一番上が同じ色の塗りなら塗らずに `EEXIST`（notice "Already filled"、log `reason=same`） |
| C-6 | 道具・最後の描く道具・帯の遷移を `notes_ui_tools_press()`（ui.c、純粋な関数）にし、host で表の試験 |
| C-7 | UI の試験は tree の `userland/desktop/fonts/Mahora-Regular.ttf` を開き、face があることを確かめてから比べる（font 無しでももう一度流す） |
| C-8 | 依存を「main に入った p003〜p005 の出力」と書き、ws.md の行を直した |
| C-9 | p001 §8 との差: `touch.c` は変えず、double tap の停止は WS081 の `plan/ws081/tests/run-notestouch.sh`（Finger on の double tap は拡大しない）で確かめる。`host-notes-fill-touch.c` は作らない |
| L-1 | 消しゴムの時の色は最後の描く道具に戻す。最後の描く道具は帯・key・menu・描く道具の button で替わる |
| L-2 | 幅の点の半径は道具ごとの等間隔の組 |
| L-3 | 帯の card は影を描かない（影が絵の下端で切れて頁の上に出るので）。縁の線で分ける |
| L-4 | 描く道具の button は一番長い label の幅（＋印）に固定 |
| L-5 | painter の背景の image の確保が断られたら `ENOMEM`（帯を半分に）。painter が走った後は必ず画面の背景を描き直させる |
| L-6 | 頁の外の tap は「Filling...」を出さずに notice。成功の後は「Filling...」の時だけ status を消す。断りの notice は `MAIN_NOTICE_MS` |
| L-7 | 帯は Esc、頁の移動（action）、頁の上の press・tap（指の Finger off の tap も）で閉じる。指の scroll・pinch では閉じない |
| L-8 | 指の 8 px は p007 で測る（記録） |
| L-9 | overflow の log は process ごとに一度（`struct notes_ui` の flag、`notes_ui_open()` で 0） |
| L-10 | p005 の `notes_fill_page()` の背景の引数の変更を p005 §6.4 に記録 |
| L-11 | U2 に notice と帯、card の横の status、帯の点の hit を足した |
| L-12 | Tool menu の新しい item の id は 45〜48（50 は Fullscreen、空きは無い）。WS175 の 2 つは `run-host-notes-edit.sh`・`run-host-clean.sh` |
| L-13 | 残件: beta2 の手引き（`docs/release/zedbsd-1.0.0-beta2-guide.md`）の Notes の節を Q1 に送る |

## 2. toolbar の描く道具の button と帯（D3 (a)）

- Pen と Marker の 2 つの button を、1 つの **描く道具の button**（`NOTES_ACTION_DRAW 48`）に替えた。label は最後に選んだ描く道具の名（Pen・Brush・Pencil・Water・Marker・Fill）と下向きの三角。幅は一番長い label ＋ 印の分で固定。描く道具の時は薄い青の pill。
- 押した時（`notes_ui_tools_press()`）: 今の道具が描く道具でない（消しゴム・Select・Text）なら最後の描く道具に戻る（`NOTES TOOL n name=…`）。描く道具なら帯を開く・閉じる（`NOTES DRAWS open` / `NOTES DRAWS closed`）。他の action は帯を閉じる。
- 帯: toolbar の絵の notice の行（toolbar の帯の 2 px 下、高さ 40 px）の glass の card。左端は描く道具の button の左、窓の中（左右 8 px）。button は Pen（1）・Brush（49）・Pencil（50）・Water（51）・Marker（2）・Fill（52）、今の道具は薄い青。帯の button は `NOTES BUTTONS` の log に出る。
- 帯が開いている間は card の下の notice を描かない（card の横の status はそのまま）。
- 当たり（`app_strip_press()`）: 帯が開いている時、pen・mouse の press（`app_input()`）と指の tap（`app_touch()`）と Finger on の指の書き始めは、帯の上なら帯の button、toolbar の帯の上なら toolbar（その button が帯を閉じる）、頁の上なら帯を閉じるだけ（描かない・塗らない）。
- `NOTES_BUTTONS` は 48。表が満ちて捨てる時に一度だけ `NOTES BUTTONS overflow action=N`。

## 3. 道具ごとの色と太さ

| 道具 | 色 | 幅（pt） | 幅の点の半径（px） |
| --- | --- | --- | --- |
| Pen | ペンの 5 色 | {1.5, 3, 6} | 2・4.5・7 |
| Marker | 蛍光ペンの 5 色（alpha 0x66） | {10, 16, 24} | 3・6・9 |
| Brush | ペンの 5 色 | {3, 6, 12} | 2.5・5.5・8.5 |
| Pencil | ペンの 5 色 | {0.8, 1.4, 2.4} | 1.5・3・4.5 |
| Water | ペンの 5 色 | {8, 16, 28} | 3・6・9 |
| Fill | ペンの 5 色（不透明） | 幅の欄を出さない（余りは残す） | — |

- 色の index・幅の index は道具の間で共有。消しゴム・Select の間は最後の描く道具の表を出す（Text はペンの色）。
- 描き始めは `notes_ui_tool(action)` の種類で、その色と幅の stroke を作る（離した時に `notes_document_add_stroke()` が焼き込む）。
- pen の mark: 描く道具は色と幅の点、塗りは白い halo と輪の中の色の点。

## 4. Tool menu と key

- Tool menu: Pen・Marker・Brush・Pencil・Watercolor・Fill・Eraser・Select・Text の radio（id 45〜48）。
- key（Control なし）: B 筆、L 鉛筆、W 水彩、F 塗りつぶし、Esc は帯を閉じる（開いていれば）。text box が開いている時は box の字。
- `NOTES TOOL n name=…`: brush・pencil・watercolor・fill を足した。

## 5. 塗りつぶしの操作

- pen・mouse・Finger on の指: 塗りの道具で頁を押して離すまでに押した点から 8 px を超えなければ、押した点を塗る（`MAIN_CONTACT_FILL`）。超えたら「Tap to fill」（`NOTES FILL moved`）。頁の外は notice "Fill inside the page"。離しを失った press は塗らない（`NOTES FILL lost`）。
- 塗る時: status「Filling...」を出し、main loop でその frame を描いてから（`app_fill()`）`notes_fill_page()` を呼ぶ。上限なし（ユーザーの決定 3）。成功は `app_changed()`、断りは `notes_fill_notice()` の notice（10 s）。
- 背景: 画面がその頁の背景を描いている時（`app_background()`）だけ painter（`app_fill_background()`）を渡す（§6）。
- double tap: `touch.c` は Finger on の間 double tap の拡大を止めている（`touch_double_tap()` は `write_mode` の間 return、WS081 の試験が確かめる）。塗りの道具でも同じなので `touch.c` は変えない。Finger off なら指は今どおり頁を動かし、頁の tap は帯を閉じる。

## 6. 帯ごとの PDF の背景（p005 M-5 の続き）

- `notes_fill_page()` の背景は「帯ごとに描く関数」（`notes_fill_background(context, top, width, height, scale)`）。main.c の `app_fill_background()` が、画面の背景の display list（`app_fill()` が `app_background()` で今の頁・look の物にする）を `pdf_display_list_rasterize(list, …, scale, 0, −top)` で白の上に帯の大きさに描き、`notes_renderer_background()` の絵に写す。帯の frame はそれを帯の大きさで 1 対 1 に描く。
- 確保が断られたら `ENOMEM`（帯を半分に）、他の失敗は `EIO`（"Could not fill the area"）。painter が走った後は `app->background_page = NULL`（画面の背景を描き直させる）。
- 帯ごとの CPU の raster は D5 の「tap の一度」の例外の中。A4 の帯 1 つで約 1 Mpx。Pi の時間は p009（`background_us`）。

## 7. AAT の helper

- `plan/tools/aat/scenarios/helpers_notes_edit.py`: `Notes.pen()`（key P、`NOTES TOOL 1 name=pen` を待つ）を足し、`pdf_edit_multipage` の Pen の押しを替えた。`DRAW = 48` を足した（p007 の scenario 用）。

## 8. 受け入れ

| # | 条件 | 結果 |
| --- | --- | --- |
| U1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk`、Linux の host rules）の `bin/notes` warning 0 | 3 つとも exit 0、warning 0（§10） |
| U2 | toolbar（host、`ui.c` を直に link、font あり 1280・1080 px と font 無し 1280 px）: 描く道具の時に DRAW があり 1・2 が card に無い。6 つの描く道具の間で全部の button（幅を除く）の x と幅が同じ。塗りだけ幅が無い。消しゴムの時も DRAW と最後の描く道具の幅。帯が開くと 6 つが toolbar の帯の下・絵の中・窓の中に順に並び、`notes_ui_hit()` が各 action、`notes_ui_in_strip()` が帯の外で 0。card の下の notice は帯が無ければ描かれ、帯が開くと描かれない。button は 48 以下 | ok（64 checks × 3 variant） |
| U3 | 色と幅の表、`notes_ui_tool()` | ok |
| U3b | `notes_ui_tools_press()` の遷移の表（C-6）: DRAW で開く・閉じる、帯の道具で選んで閉じる、消しゴムから DRAW で最後の描く道具、消しゴムの色で戻る、Select の色はそのまま、頁の action で閉じる、key の道具、Text で最後の描く道具は保つ | ok |
| U4 | 塗りの背景（host、`run-notes-fill.sh`）: painter が帯ごとに帯の top と大きさで呼ばれ帯の frame に背景の draw がある、painter の失敗は `EIO` で頁が変わらず log。同じ色の塗りの上の tap は `EEXIST`。p005 code review M2 の点の tap は `ENOENT` | ok（160 checks × 3 variant） |
| U5 | 既存の host 試験: `run-notes-fill.sh`・`run-notes-render.sh`・`run-notes-tools.sh`・WS079 `run-notes-host.sh`・WS175 `run-host-notes-edit.sh`・`run-host-clean.sh`・WS081 `run-notestouch.sh`（Finger on の double tap は拡大しない） | 全部 ok / PASS |
| U6 | 画面の操作（帯の開閉・tap・key・menu・塗りの tap と 8 px・指・notice・Filling...）は p007 の T1（§9）。host では main.c を動かせない | 未実施（p007） |

## 9. p007 の T1 の依頼の文面（Q1 へ）

（§10 の後に Q1 へ送った文面と同じ）

> **T1 依頼（ws216-p007、Notes の描く道具と塗りつぶし）**
> - image: `plan/ws216/tests/config.mk`（無ければ `config/current-uat.mk` と同じ内容で作る、Q1 の判断）での build。複写する file は無い（Notes の新しい頁で描く）。PDF の上の塗りは WS175 の `make-edit-samples.py` の `edit-basic.pdf` を `--file` で home に置く。
> - scenario（新、p007 で書く。`tests/scenarios/apps/notes/draw-tools.md`・`fill-region.md`）:
>   1. draw-tools: Notes を開き、key B・L・W で筆・鉛筆・水彩を選び（`NOTES TOOL 49 name=brush` など）、それぞれ `peninject` の筆圧 0→最大の線を描く（`NOTES STROKE … tool=2|3|4`）。描く道具の button（48）を押して `NOTES DRAWS open`、`NOTES BUTTONS` の帯の Marker（2）を押して `NOTES TOOL 2 name=highlighter` と `NOTES DRAWS closed`。undo・redo。保存して開き直し、同じ画面。撮影（各道具の線、帯が開いた toolbar）。log に `NOTES PIPE made name=grain`・`name=multiply`、`NOTES PIPE fallback` が無い。
>   2. fill-region: key F で塗り（`NOTES TOOL 52 name=fill`）。ペンで閉じた円を描き、その内側を click → `NOTES FILL made place=0 …`（`NOTES FILL bands=… background_us=0`）、円の内側が塗られ線は上。もう一度同じ所を click → `NOTES FILL refused reason=same` と notice "Already filled"。塗りの上で drag → `NOTES FILL moved` と "Tap to fill"。消しゴム（key E）を円の中心に下ろす → 塗りだけが消える（`NOTES ERASE … removed=1`）、undo で戻る。`edit-basic.pdf` を開き、その頁の上の囲まれた所を click → `background_us` が 0 でない `NOTES FILL made`。撮影（塗った円、PDF の上の塗り）。
> - 合否: 上の log が出る。撮影で、鉛筆に粒・区間の継ぎ目に線が無い、水彩の重なりが濃く減法で混ざる、塗りが線の下に入り線との間に白い隙間が無い、PDF の線の内側が塗られる、ペンの線が前と同じ。QEMU の画で判定する（log と PNG）。
> - 時間: `NOTES FILL made … ms=` と `background_us` を記録（QEMU、amd64）。実機 5330 のペンの感触・`v_ref` の調整・指の 8 px はユーザーの目で別に。

## 10. 実行の記録（2026-10-13 P1、agent/p1、base main 6c703b2fc の merge 061660bd4）

### 10.1 変えた所

| file | 内容 |
| --- | --- |
| `ui.c` | 描く道具の button（固定の幅、下向きの印）、帯（`ui_strip()`、影なし）、notice を帯の間は描かない、道具ごとの幅の表と点の半径、塗りは幅の余りを残す、`notes_ui_in_strip()`・`notes_ui_tool()`・`notes_ui_tools_press()`、`ui_label_button_at()`、overflow の log |
| `main.c` | 最後の描く道具と帯の欄、`app_action()` は `notes_ui_tools_press()` で道具と帯、key B・L・W・F と Esc、`app_strip_press()`、描き始めの道具の種類、塗りの press（`MAIN_CONTACT_FILL`・8 px・Tap to fill・頁の外・lost）、指の contact に FILL と TOOLBAR、main loop の `app_fill()`（Filling... の frame の後）、`app_fill_background()`、`app_on_page()`・`app_notice()`・`app_fill_track()`、pen の mark、道具の名 |
| `menu.c` | Tool menu の Brush・Pencil・Watercolor・Fill |
| `app.h` | action 48〜52、`NOTES_BUTTONS` 48、`NOTES_TOOLS_*`・`struct notes_tools`、ui の state と帯の欄、`notes_fill_background` と `notes_fill_page()` の新しい引数 |
| `fill-page.c` | 背景の painter（帯ごと、µs の計測、ENOMEM で帯を半分に）、同じ色の塗りは `EEXIST`、`notes_fill_width()` を使う、`next_id` を進めない |
| p005 の直し | `fill.c`（空の領域は ENOENT、`notes_fill_width()`）、`document.c`（`stroke_near()` の早い打ち切り、`stroke_covered()` を円の中で）、`render.c`（帯を捨てる前に idle）、`notes.h` |
| `plan/ws216/tests/host-notes-ui.c`・`run-notes-ui.sh`（新） | U2・U3・U3b |
| `plan/ws216/tests/host-notes-fill.c` | U4、M2、C-5、新しい `notes_fill_page()` の引数 |
| `plan/tools/aat/scenarios/helpers_notes_edit.py` | `Notes.pen()`（key P）、`DRAW = 48` |

### 10.2 command と結果（host、gcc 14.2.0）

- `ZEDBSD_CONFIG=config/current-uat.mk make build/amd64/bin/notes`・`ZEDBSD_CONFIG=config/ci/config-rpi4.mk make build/arm64/bin/notes`・`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws216-linux build/ws216-linux/bin/notes`: exit 0、`warning:` 0。
- `sh plan/ws216/tests/run-notes-ui.sh` → `run-notes-ui: ok`（64 checks、0 failed × plain・ASan・UBSan）。
- `sh plan/ws216/tests/run-notes-fill.sh` → `run-notes-fill: ok`（160 checks、0 failed × 3）。
- `run-notes-render.sh` ok、`run-notes-tools.sh` ok、`plan/ws079/tests/run-notes-host.sh` ok、`plan/ws175/tests/run-host-notes-edit.sh` PASS、`plan/ws175/tests/run-host-clean.sh` PASS、`plan/ws081/tests/run-notestouch.sh build/ws216-p006-touch` → `host-notestouch: ok (54 checks)`。
- `git diff --check` 空。

### 10.3 制限

- 帯を開いている間は card の下の notice が出ない（ユーザーへの報告、決定 5）。
- 塗りは window を止める（上限なし、決定 3）。Pi の時間は p009。
- 指の 8 px は touch の tap の slop より狭い見込み（推測、p007 で測る）。
- 帯の CPU の背景は頁の display list を帯ごとに全部組み直す（raster.c）。文字の多い PDF で遅い見込み（p009）。

### 10.4 未実施・残り

- 画面の操作と画（U6）は p007 の T1（§9）。Pi 4 は p009（帯の stencil・uncached の読み戻し・時間、p005 code review 6）。
- FreeBSD の native の build。
- beta2 の手引きの Notes の節（L-13、Q1 へ）。
- 規約の全文の見直しは p008。
