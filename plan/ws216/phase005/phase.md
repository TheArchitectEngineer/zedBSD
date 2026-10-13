<!-- awesome-plan project=zedbsd record=ws216-p005 -->

# ws216-p005: 領域の塗りつぶし（帯の画面外の描画と読み戻し、flood fill、輪郭化と上限、重なりの順、undo・journal、消しゴム D7 (c)）

Parent: [WS216](../ws.md)
Status: in-progress（2026-10-13 P1。設計を先にこの phase.md に書き、実装と host の受け入れへ。UI（道具の選択、tap の判定、notice の表示）は p006、QEMU・実機は p007 で T1）
Phase disposition: normal
Queue: q942（2026-10-13 Q1 の依頼、P1）。承認はユーザーの再開の指示（2026-10-13「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」）と ws.md の D1〜D12

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md) の §3.4・§3.5・§4.5 と §8 の p005 の行。review は [review-1](../phase001/review-1.md)（S4・S5・M7・L3・L4）と [review-2](../phase001/review-2.md)（M-1〜M-4・M-11・L-4・L-7・L-18）。p003（塗りの stroke の形式・形・journal）と p004（塊・画面）は main に入った。
- 触る file: `userland/desktop/notes/`（新しい `fill.c`・`fill-page.c`、`notes.h`・`app.h`・`document.c`・`render.c`、3 つの Makefile）、`plan/ws216/`。
- 触らない: UI（`main.c` の道具・tap・notice の表示、`ui.c`、p006）、libpdf、HAL・toolchain。p005 は「点 (x, y) を塗る」口と結果の符号・notice の文を用意し、p006 がそれを道具と tap に結ぶ。
- 試験の方針: build（amd64・rpi4、Linux の host rules）warning 0 と host 試験（plain・ASan・UBSan）。QEMU・実機は p007（T1）。

## 1. review の指摘への決定

| # | 指摘 | 決定 |
| --- | --- | --- |
| S5・M-1 / L-1 | 帯は push constant では描けない。帯ごとに頁を組み直す時間 | 帯ごとに view（x = 0、y = −帯の上の画素、scale = R）で頁の frame を組む。帯の行と bounds（+1 pt）の交わらない stroke は入れない。時間は log（`NOTES FILL made … ms=`）で p007・p009 に測る。遅い device の手当て（R を下げる等）は p009 の数を見てから |
| M-2 | 帯の境をまたぐ膨張と flood | 頁全体の画素の表（host の malloc、1 画素 1 byte の flag と 1 byte の距離）に、読んだ帯の行を足していく。ある行は「その行 ± 閉じる半径の行が全部読まれた」時だけ flood の対象になり（ready）、それまでその行へ進む span は待ちの列に入る。待ちしか残らなければ、足りない行を含む帯を読んで続ける。膨張の結果は帯の切り方に依らないので、1 枚の raster と帯に分けた時の領域は bit で同じ（試験） |
| M-3 | 0.1 pt の潜らせが表せない | 膨らませ戻しの半径 = 閉じる半径 + 境の fringe の幅 0.75 px + 0.1 pt（R で px）。実数の半径の円で膨らませる。0.225 pt のペンの線の両側の塗りが線の向こうへ出ないことを試験で確かめる |
| M-4 | 帯の幅が 4096 を超える | `R = min(4, 4096 / 頁の幅, sqrt(16 Mi / 頁の面積))` |
| S4・M-11 | 消しゴムで塗りを消す規則 | §4 |
| M7 | 領域の爆発 | 面積 1 pt² 未満の輪郭（島・穴）を捨てる、点の数 8192 の上限と単純化の許容差 0.5 → 1 → 2 px、超えたら塗らない（`E2BIG`、notice の文は p006 が出す） |
| L3 | 頁の絵の usage | 頁の絵は変えない。帯は別の image（`COLOR_ATTACHMENT | TRANSFER_SRC`）と stencil と読み戻しの buffer を、塗りの間だけ作る |
| L4 | BGRA と RGBA | seed の画素と同じ並びで比べるので順は関係しない（4 つの channel のどれか 1 つが閾値を超えたら境） |
| L-7 | 1.5 pt は R = 4 の時だけ 6 px | 半径は全部 pt で決め、R を掛けて px にする |
| L-18 | 水彩の薄い縁の上の tap | 「見えている物」の判定から、効く alpha（色の alpha × 形の alpha）が 0.1 未満の形を除く |

## 2. 塗る領域の求め方（`fill.c`、CPU、Vulkan に依らない）

口: `notes_fill_find(頁の幅, 頁の高さ, x, y, band, context, &region)`。`band(context, top, height, pixels)` が頁の画素の行 [top, top + height) を R px/pt で描いて RGBA（または BGRA）の 4 byte の画素で返す。返り値 0、`ENOMEM`（device が確保を断った: 帯を半分にして続ける）、他の errno（失敗）。

1. **倍率と大きさ**: R は §1 M-4。頁の画素 W = ceil(頁の幅 × R)、H = ceil(頁の高さ × R)。帯の高さ = min(H, max(64, floor(4 MiB / (W × 4))))。`ENOMEM` で半分（64 行まで、それでも断られたら `ENOMEM` で失敗）。
2. **seed**: (x, y) の画素。頁の外は `EINVAL`。最初の帯は seed の行を真ん中に置く。seed の色はその帯の seed の画素。
3. **境**（RAW）: seed の画素と比べて、どれか 1 つの channel の差が 26 を超える画素。行を読んだ時に、行の中の「左右で一番近い RAW までの距離」（255 で止める）も作る。
4. **閉じる**（CLOSED）: RAW を半径 r_c = 1.5 pt × R の円で膨らませた画素。行 y の CLOSED は、|dy| ≤ floor(r_c) の各行 y + dy（頁の中）で距離 ≤ floor(sqrt(r_c² − dy²)) の画素。行が ready になった時に一度だけ作る。
5. **seed が CLOSED の中**（線の近くの tap）: seed から 1 画素ずつ広げた輪（r_c + 1 px まで）で一番近い CLOSED でない画素を seed にする。無ければ閉じる半径 0 で最初からやり直す（狭い所。隙間は閉じない）。
6. **flood**: 4 近傍の span の flood。頁の縁は境（D12）。ready でない行への span は待ちの列へ（§1 M-2）。
7. **膨らませ戻す**（FINAL）: 領域を半径 r_b = r_c + 0.75 px + 0.1 pt × R の円で膨らませる（4 と同じ作り方、距離は領域まで）。
8. **輪郭**: FINAL の画素の縁をたどる（内側を進む向きの右に置く、右下がりの座標で外は時計回り、穴は逆。対角だけで接する画素は別とする: flood の 4 近傍と同じ）。曲がる角だけを点にする。
9. **捨てる**: 面積（shoelace）が 1 pt²（R² px²）未満の輪郭。
10. **単純化**: 閉じた Douglas-Peucker、許容差 0.5 px。点の合計が 8192 を超えたら 1 px、2 px でやり直し、それでも超えたら `E2BIG`。
11. **pt に**: x / R、y / R を頁の中に clamp し、1/64 pt の格子（`notes_quantize()`）に丸め、続く同じ点を除く。3 点未満の輪郭は捨てる。最初の輪郭は外の輪郭（flood の領域は 1 つにつながるので、外の輪郭は 1 つ。穴は後）。
12. 結果: 点の列（`pdf_point`、pt）と輪郭ごとの点の数、測り（読んだ帯の数、帯の高さ、許容差の段、領域の画素の数、R）。

host の memory: 頁の画素ごとに 2 byte（A4 で 16 MB、上限 16 Mi px で 32 MB）と、輪郭をたどる時の領域の箱の 1 byte。Vulkan の確保ではない。

## 3. 頁を描く（`fill-page.c`・`render.c`）

- `notes_fill_page(renderer, document, page, x, y, color, background, &place)`（`app.h`）: 帯の frame を組んで `notes_renderer_band()` で描いて読み、`notes_fill_find()` で領域を求め、塗りの stroke（道具 FILL、色は不透明にした色、幅 1 pt、点は輪郭の角、`contours`）を作り、z の位置（§4）に undo できる変更として入れる（journal にも入る）。返り値: 0、`E2BIG`（複雑すぎる）、`EINVAL`（頁の外）、`ENOMEM`、`EIO`（device の失敗）。log: `NOTES FILL made place=P contours=C points=N bands=B band=H scale=R pixels=A ms=T`、`NOTES FILL refused reason=complex|device|memory|outside …`。notice の文は `notes_fill_notice(error)`（p006 が表示する。E2BIG は「塗る所が複雑すぎます」の英語の UI の文 "The area is too complex to fill"）。
- 帯の frame: 白い頁の矩形、背景（PDF の頁・編集のある頁で、渡された時だけ。今の背景の texture を頁の大きさ × R に広げる。解像度は頁の絵の物なので背景の境は R より粗い: 制限）、帯への clip、帯と交わる stroke（`notes_frame_stroke()`）。粒・Multiply の draw も画面と同じ（p004 の遅延の組と代わり）。
- `notes_renderer_band(renderer, frame, width, height, pixels)`: 帯の image（swapchain の format、`COLOR_ATTACHMENT | TRANSFER_SRC`、device local）、帯の大きさの stencil、framebuffer、帯の render pass（clear、終わりの layout は `TRANSFER_SRC_OPTIMAL`、window の pass と互換）、host-visible の読み戻しの buffer を、大きさが変われば作り直す。頂点は p004 の塊に入れ、1 回の submit で描いて `vkCmdCopyImageToBuffer`、fence を待って行を `pixels` へ写す。前の frame は fence で終わっている（`notes_renderer_draw()` は終わりで待つ）。`notes_renderer_band_free()` で塗りの後に全部捨てる。

## 4. 重なりの順と消しゴム（`document.c`）

- **見えている物**（D6 (a)、L-18）: 頁の stroke を上から見て、点 (x, y) が形の内側（nonzero、全部の輪郭）にある最初の stroke。効く alpha が 0.1 未満の形は見ない。
- **z**: 見えている物の直上（index + 1）。何も無ければ一番下（0）。`notes_document_add_stroke_at(document, page, place, stroke)`（今の `notes_document_add_stroke()` はこれを一番上で呼ぶ）。undo の entry は今の `NOTES_UNDO_ADD_STROKE`（place を持つ）、journal は `notes_document_insert_stroke()` が place ごと書く。
- **消しゴム**（D7 (c) と M-11 の詰め）: 一回の操作（`erase_begin`〜`erase_end`、drag でない 1 回の呼び出しはそれだけで 1 回）の最初に何かに当たった位置で決める。円が塗り以外の stroke に当たれば「線の操作」（塗りは消さない）。どの線にも当たらず、円の中心がどれかの塗りの内側なら「塗りの操作」。塗りの操作では、以後の各位置で中心を内側に含む塗りを全体ごと消し、線も今どおり消す（部分消しでも塗りは全体）。線の当たりは今の `stroke_touches()`（塗りには当たらない）。

## 5. 受け入れ

| # | 条件 |
| --- | --- |
| F1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk` の `bin/notes`、Linux の host rules の `bin/notes`）warning 0。3 つの Makefile に新しい file |
| F2 | 合成の raster（AA の縁つき、`fill.frag` と同じ coverage）で期待の領域: 閉じた円の内側、1 pt の隙間の円は閉じて内側だけ、2.5 pt の隙間は漏れる、穴のある形（穴の輪郭が逆向き、nonzero の面積）、文字の列の間、粒のある薄い鉛筆の円（粒の tile と同じ値）、alpha 0.22 の水彩の円、写真の模型（乱数の画素）、囲みの無い頁（頁全体の矩形 1 つ）、頁の角の三角 |
| F3 | 帯の高さ 64・440 と 1 枚の raster で、帯の境に 1 pt の隙間の円を置いても領域（FINAL の画素）と輪郭が bit で同じ |
| F4 | 0.225 pt のペンの線の両側の塗りが、線の向こうの画素へ出ない（塗りの輪郭と線の向こうの縁の距離 > 0） |
| F5 | 写真の模型の頁全体で 8192 点を超え `E2BIG`。1 pt² 未満の穴が捨てられる。A3 横長で R = 4096 / 1191 |
| F6 | `ENOMEM` を返す帯（4 MiB と 2 MiB を断る）で帯が半分になって同じ領域。64 行も断られたら `ENOMEM` |
| F7 | z: 何も無い所は一番下、線の内側は線の直上、塗りの上は塗りの直上、水彩の薄い縁（alpha 0.06）の上は縁を見ない。undo・redo で戻り、journal の復旧で同じ順 |
| F8 | 消しゴム: 線から始めた操作は塗りを消さない、塗りの内側（線に当たらない）から始めた操作は以後の中心の塗りを全体ごと消す、中心が線から 3 pt 外れた塗りの上（円が線に当たる）では線だけが消える（M-11）、部分消しでも塗りは全体、undo で戻る |
| F9 | renderer の模型（p004 の Vulkan の模型）で `notes_fill_page()` が帯の image・buffer を 4 MiB 以下で作り、帯の image の usage に `TRANSFER_SRC`、帯ごとに 1 つの pass と 1 回の copy、帯の frame が帯の行に交わる stroke だけを持ち、終わりに帯の物を全部捨てる |
| F10 | 2048×2048 px の flood と輪郭化、A4 の頁全体の塗りの時間を host で記録 |
| F11 | 既存の host 試験（`run-notes-render.sh`・`run-notes-tools.sh`・`run-notes-host.sh`・`run-host-notes-edit.sh`・`run-host-clean.sh`）が通る。新しい試験が plain・ASan・UBSan で通る |

試験: `plan/ws216/tests/run-notes-fill.sh`（`host-notes-fill.c`: `fill.c`・`fill-page.c`・`document.c` など Notes の model と render.c・geometry.c を p004 の Vulkan の模型で link）。出力は worktree の `build/ws216-p005-host/`。画面の見え方と実機の時間は p007・p009。

## 6. 実行の記録

（実装の後に書く）
