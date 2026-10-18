<!-- awesome-plan project=zedbsd record=ws216-p005 -->

# ws216-p005: 領域の塗りつぶし（帯の画面外の描画と読み戻し、flood fill、輪郭化と上限、重なりの順、undo・journal、消しゴム D7 (c)）

Parent: [WS216](../ws.md)
Status: cleared（2026-10-18 Q1: ws.md の表に記録済みの判定に合わせた。表: cleared（2026-10-13 Q1: T1-580 の QEMU の AAT で機械の判定 PASS・画を Q1 が見て合格。Pi 4 は p009、5330 の感触は p007 の A5））（旧: in-progress（2026-10-13 P1: 設計、review-1 の反映、実装と host の受け入れ F1〜F11 済み。test-wait: 画面・実機は p007 で T1、Pi 4 は p009。UI（道具の選択、tap の判定、notice の表示）は p006））
Phase disposition: normal
Queue: q942（2026-10-13 Q1 の依頼、P1）。承認はユーザーの再開の指示（2026-10-13「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」）と ws.md の D1〜D12

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md) の §3.4・§3.5・§4.5 と §8 の p005 の行。review は [review-1](../phase001/review-1.md)（S4・S5・M7・L3・L4）と [review-2](../phase001/review-2.md)（M-1〜M-4・M-11・L-4・L-7・L-18）、この段の設計の review は [p005 review-1](review-1.md)（2026-10-13、条件つきで進めてよい）。p003（塗りの stroke の形式・形・journal）と、main に入った p004 の出力（頂点の塊・遅延の組・`notes_frame_stroke()`、p004 自体は test-wait）を使う。
- 触る file: `userland/desktop/notes/`（新しい `fill.c`・`fill-page.c`、`notes.h`・`app.h`・`document.c`・`render.c`、3 つの Makefile）、`plan/ws216/`（p004 の Vulkan の模型に copy・layout の確かめを足し、p004 の試験の sampler の数を直す）。
- 触らない: UI（`main.c` の道具・tap・notice の表示、`ui.c`、p006）、libpdf、HAL・toolchain。p005 は「点 (x, y) を塗る」口と結果の符号・notice の文を用意し、p006 がそれを道具と tap に結ぶ。
- 試験の方針: build（amd64・rpi4、Linux の host rules）warning 0 と host 試験（plain・ASan・UBSan）。QEMU・実機は p007（T1）。

## 1. review の指摘への決定

| # | 指摘 | 決定 |
| --- | --- | --- |
| S5・M-1 / L-1 | 帯は push constant では描けない。帯ごとに頁を組み直す時間 | 帯ごとに view（x = 0、y = −帯の上の画素、scale = R）で頁の frame を組む。帯の行と bounds（+1 pt）の交わらない stroke は入れない。時間は log（`NOTES FILL made … ms=`）で p007・p009 に測る。遅い device の手当て（R を下げる等）は p009 の数を見てから |
| M-2 | 帯の境をまたぐ膨張と flood | 頁全体の画素の表（host の malloc、1 画素 1 byte の flag と 1 byte の距離）に、読んだ帯の行を足していく。ある行は「その行 ± 閉じる半径の行が全部読まれた」時だけ flood の対象になり（ready）、それまでその行へ進む span は待ちの列に入る。待ちしか残らなければ、足りない行を含む帯を読んで続ける。膨張の結果は帯の切り方に依らないので、1 枚の raster と帯に分けた時の領域は bit で同じ（試験） |
| M-3 | 0.1 pt の潜らせが表せない | 膨らませ戻しの半径 = 閉じる半径 + 境の fringe の幅 0.75 px + 0.1 pt（R で px）。実数の半径の円で膨らませる。0.225 pt のペンの線の両側の塗りが線の向こうへ出ないことを試験で確かめる |
| M-4 | 帯の幅が 4096 を超える | `R = min(4, 4096 / 頁の幅, sqrt(16 Mi / 頁の面積))` |
| S4・M-11 | 消しゴムで塗りを消す規則 | §4（p005 review-1 S-2 の後、ユーザーの決定） |
| M7 | 領域の爆発 | 面積 1 pt² 未満の輪郭（島・穴）を捨てる、点の数 8192 の上限と単純化の許容差 0.5 → 1 → 2 px、超えたら塗らない（`E2BIG`、notice の文は p006 が出す） |
| L3 | 頁の絵の usage | 頁の絵は変えない。帯は別の image（`COLOR_ATTACHMENT | TRANSFER_SRC`）と stencil と読み戻しの buffer を、塗りの間だけ作る |
| L4 | BGRA と RGBA | seed の画素と同じ並びで比べるので順は関係しない（4 つの channel のどれか 1 つが閾値を超えたら境） |
| L-7 | 1.5 pt は R = 4 の時だけ 6 px | 半径は全部 pt で決め、R を掛けて px にする |
| L-18 | 水彩の薄い縁の上の tap | 「見えている物」の判定から、効く alpha（色の alpha × 形の alpha）が 0.1 未満の形を除く |

### 1.1 p005 review-1 の指摘への決定（Q1 の振り分けとユーザーの決定、2026-10-13）

| # | 指摘 | 決定 |
| --- | --- | --- |
| S-1 | Pi の driver は pass の finalLayout に TRANSFER_SRC を受けない | 帯の pass は COLOR_ATTACHMENT_OPTIMAL で終え、pass の後の image barrier で TRANSFER_SRC_OPTIMAL に移して copy（§3）。模型が pass の finalLayout と copy の時の layout を確かめる |
| S-2 | D7 (c) の規則で塗りが消せない・見えない物が消える | **ユーザーの決定**（2026-10-13、Q1 の AskUserQuestion「見えている物を優先 (Recommended)」）。§4 |
| M-1 | 読み戻しの同期 | 帯ごとに `vkDeviceWaitIdle`（present に失敗した frame は fence を待たずに返るため）、pass の外への依存（COLOR_ATTACHMENT_OUTPUT・WRITE → TRANSFER・READ）、layout の barrier、copy の後の buffer barrier（TRANSFER_WRITE → HOST_READ）、HOST_COHERENT の memory、fence の待ち |
| M-2 | 線の上に入った塗りが線の縁を覆う | 見えている物が紙でない（z > 0）時は、膨らませ戻しを閉じる半径だけにする（境の fringe の手前で止まる）。D6 (a) の結果として記録 |
| M-3 | 外の輪郭が捨てられた時 | 外が 1 pt² 未満か 3 点未満なら穴も全部捨てて `ENOENT`（`NOTES FILL refused reason=empty`、notice "The area is too small to fill"） |
| M-4 | 輪郭をたどる memory と時間 | 輪郭を 1 つたどるごとに面積の判定・単純化・pt への変換を行い、たどった角の合計が 1 Mi を超えたら直ちに `E2BIG`（許容差を上げても変わらないので打ち切り） |
| M-5 | PDF の背景の精度 | 背景の sampler を LINEAR にする（画面の 1 対 1 の表示は変わらない）。帯ごとに PDF を CPU で raster にする案は p006 との境界（display list を渡す口）が要るので残件 |
| M-6 | 点の上限 8192 | **ユーザーの決定**（2026-10-13「8192 のまま (Recommended)」）。測った数は §6 |
| M-7 | 帯の大きさが 4096 を超える | 帯の高さと W を 4096 で切る |
| L-1 | 「見えている物」の閾値 | 境と同じ「白い紙からの色の差が 26/255 を超える」で決める（`shape_seen()`） |
| L-2・L-6・L-7 | 単純化の許容差の段、GPU の丸め、Pi の uncached の読み | 制限として記録（§6.3） |
| L-3 | 狭い所で閉じる半径 0 に落ちる | 閉じる半径を 1.5 → 0.75 → 0.375 → 0 pt と段で下げる |
| L-4 | seed が AA の fringe | seed の周り 5×5 で、色の近い画素の多い色（同じなら seed に近い色）を seed の色にする |
| L-5 | callback の W と R | `notes_fill_band` の注釈に書いた（`notes_fill_scale()` と W = 頁の幅 × R、4096 まで） |
| L-8 | UI の thread を止める | p006 で「塗っています」の表示か時間の上限（残件） |
| L-9 | 失敗の経路などの試験 | F2〜F9 に足した（途中の EIO、断った時に頁が変わらない、線の上の tap、薄い橙の鉛筆、A0、細長い頁） |
| L-10 | 他の WS の試験の source の一覧 | document.c は fill.c を呼ばないので不要（WS079・WS175 の試験はそのまま通った） |
| L-11 | 塗り替えは上に重ねる | Future Work の候補（置き換えは製品の選択）。Q1 が future-work.md に足すかを決める |
| L-12 | 記録 | ws.md の p005 の行を直した。開いている text box の overlay と drag の preview は帯に入らない（制限） |

## 2. 塗る領域の求め方（`fill.c`、CPU、Vulkan に依らない）

口: `notes_fill_find(頁の幅, 頁の高さ, x, y, band, context, &region)`。`band(context, top, height, pixels)` が頁の画素の行 [top, top + height) を R px/pt で描いて RGBA（または BGRA）の 4 byte の画素で返す。返り値 0、`ENOMEM`（device が確保を断った: 帯を半分にして続ける）、他の errno（失敗）。

1. **倍率と大きさ**: R は §1 M-4。頁の画素 W = min(4096, ceil(頁の幅 × R))、H = ceil(頁の高さ × R)。帯の高さ = min(H, 4096, max(64, floor(4 MiB / (W × 4))))。`ENOMEM` で半分（64 行まで、それでも断られたら `ENOMEM` で失敗）。読んでいない行は、その下の行が読んであれば（flood が上へ進む時）その行で終わる帯、そうでなければその行から始まる帯で読む。
2. **seed**: (x, y) の画素。頁の外は `EINVAL`。最初の帯は seed の行を真ん中に置く。seed の色は周り 5×5 の画素から選ぶ（§1.1 L-4）。
3. **境**（RAW）: seed の画素と比べて、どれか 1 つの channel の差が 26 を超える画素。行を読んだ時に、行の中の「左右で一番近い RAW までの距離」（255 で止める）も作る。
4. **閉じる**（CLOSED）: RAW を半径 r_c = 1.5 pt × R の円で膨らませた画素。行 y の CLOSED は、|dy| ≤ floor(r_c) の各行 y + dy（頁の中）で距離 ≤ round(sqrt(r_c² − dy²)) の画素。行が ready になった時に一度だけ作る。
5. **seed が CLOSED の中**（線の近くの tap）: seed から r_c + 1 px までで一番近い CLOSED でない画素を seed にする。無ければ閉じる半径を 0.75・0.375・0 pt と下げてやり直す（読んだ行と RAW は保つ）。
6. **flood**: 4 近傍の span の flood。頁の縁は境（D12）。ready でない行への span は待ちの列へ（§1 M-2）。
7. **膨らませ戻す**（FINAL）: 領域を半径 r_b の円で膨らませる（4 と同じ作り方、距離は領域まで）。塗りが線の下に入る（z = 0）時は r_b = r_c + 0.75 px + 0.1 pt × R、上に入る時は r_b = r_c（§1.1 M-2）。画素は 0.25 pt なので、下に入る塗りの縁は線の縁に（±0.125 pt）、上に入る塗りは線の AA の縁の手前で止まる。0.1 pt の潜らせは画素の量子化より細かく、それ自体は表れない（M-3 の目標の制限、塗り自身の AA の fringe が残りを覆う）。
8. **輪郭**: FINAL の画素の縁をたどる（内側を進む向きの右に置く、右下がりの座標で外は時計回り、穴は逆。対角だけで接する画素は別とする: flood の 4 近傍と同じ）。曲がる角だけを点にする。
9. **捨てる**: 面積（shoelace）が 1 pt²（R² px²）未満の輪郭。外の輪郭なら `ENOENT`（§1.1 M-3）。輪郭は 1 つずつ、たどる → 捨てる → 単純化 → pt に、と流れで処理し、たどった角の合計が 1 Mi を超えたら `E2BIG`（M-4）。
10. **単純化**: 閉じた Douglas-Peucker、許容差 0.5 px。点の合計が 8192 を超えたら 1 px、2 px でやり直し、それでも超えたら `E2BIG`（上限はユーザーの決定で 8192 のまま、M-6）。
11. **pt に**: x / R、y / R を頁の中に clamp し、1/64 pt の格子（`notes_quantize()`）に丸め、続く同じ点を除く。3 点未満の輪郭は捨てる。最初の輪郭は外の輪郭（flood の領域は 1 つにつながるので、外の輪郭は 1 つ。穴は後）。
12. 結果: 点の列（`pdf_point`、pt）と輪郭ごとの点の数、測り（読んだ帯の数、帯の高さ、許容差の段、領域の画素の数、R）。

host の memory: 頁の画素ごとに 2 byte（A4 で 16 MB、上限 16 Mi px で 32 MB）と、輪郭をたどる時の領域の箱の 1 byte。Vulkan の確保ではない。

## 3. 頁を描く（`fill-page.c`・`render.c`）

- `notes_fill_page(renderer, document, page, x, y, color, background, &place)`（`app.h`）: まず z の位置（§4）を決め、帯の frame を組んで `notes_renderer_band()` で描いて読み、`notes_fill_find()` で領域を求め（z = 0 なら線の下へ、§2 の 7）、塗りの stroke（道具 FILL、色は不透明にした色、幅 1 pt、点は輪郭の角、`contours`）を作り、undo できる変更として入れる（journal にも入る）。返り値: 0、`E2BIG`（複雑すぎる）、`ENOENT`（小さすぎる）、`EINVAL`（頁の外）、`ENOMEM`、`EIO`（device の失敗）。断った時は頁・undo・journal を変えない。log: `NOTES FILL made place=P contours=C points=N bands=B band=H scale=R pixels=A tolerance=S ms=T`、`NOTES FILL refused reason=complex|empty|outside|failed …`、`NOTES FILL band refused rows=H error=E`。notice の文は `notes_fill_notice(error)`（p006 が表示する。"The area is too complex to fill"・"The area is too small to fill"・"Fill inside the page"・"Could not fill the area"）。`background` は「renderer の背景の絵がその頁の PDF の頁を画面どおりに持つ」時に 1（p006 が `app_page_frame()` と同じ条件で渡す）。
- 帯の frame: 白い頁の矩形、背景（渡された時だけ。今の背景の texture を頁の大きさ × R に広げ、LINEAR で sample。解像度は頁の絵の物なので背景の境は R より粗い: 制限、§1.1 M-5）、頁への clip、帯の行（± 1 pt）と bounds の交わる stroke（`notes_frame_stroke()`）。粒・Multiply の draw も画面と同じ（p004 の遅延の組と代わり）。
- `notes_renderer_band(renderer, frame, width, height, pixels)`: `vkDeviceWaitIdle` の後、帯の image（swapchain の format、`COLOR_ATTACHMENT | TRANSFER_SRC`、device local）、帯の大きさの stencil、framebuffer、帯の render pass（clear、終わりの layout は COLOR_ATTACHMENT_OPTIMAL、外への依存は TRANSFER の READ、window の pass と互換）、host-visible・coherent の読み戻しの buffer を、大きさが変われば作り直す。頂点は p004 の塊に入れ、1 回の submit で描き、image barrier（COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL）、`vkCmdCopyImageToBuffer`、buffer barrier（TRANSFER_WRITE → HOST_READ）、fence を待って行を `pixels` へ写す。`notes_renderer_band_free()` で塗りの後に全部捨てる。host の写した絵の layout の移し（toolbar・背景・overlay・粒）は `render_pictures_ready()` に出し、frame と帯の両方が使う。

## 4. 重なりの順と消しゴム（`document.c`）

- **見えている物**（D6 (a)、L-18、p005 review-1 L-1）: 頁の stroke を上から見て、点 (x, y) が形の内側（nonzero、全部の輪郭）にある最初の stroke。白い紙の上での色の差（効く alpha × (255 − 色)、鉛筆は粒の平均の濃さ）がどの channel も 26 以下の形は見ない（境と同じ測り）。
- **z**: 見えている物の直上（index + 1）。何も無ければ一番下（0）。`notes_document_add_stroke_at(document, page, place, stroke)`（今の `notes_document_add_stroke()` はこれを一番上で呼ぶ）。undo の entry は今の `NOTES_UNDO_ADD_STROKE`（place を持つ）、journal は `notes_document_insert_stroke()` が place ごと書く。
- **消しゴム**（D7 (c)。**ユーザーの決定 2026-10-13「見えている物を優先」**: 線を消すのは中心が線の見える半幅＋約 2 pt の中にある時、当たった所で上の不透明な塗りに覆われている線は数えない、塗りは中心で見えている一番上の塗りだけを消す。代わりに線のすぐ近くでは線でなく背景の塗りが消えることがある、は受け入れ済み）。実装（`document.c`）:
  - 一回の操作（`erase_begin`〜`erase_end`、drag でない 1 回の呼び出しはそれだけで 1 回）の種類を、最初に何かに当たった位置で決める（`erase_decide()`）: (1) 中心が線（塗り以外）の見える半幅（`notes_stroke_reach()`）＋ 2 pt の中にあり、その線が中心に一番近い所で上の塗りに覆われていなければ「線の操作」、(2) そうでなく中心がどれかの塗りの内側なら「塗りの操作」、(3) そうでなく円が覆われていない線に当たれば「線の操作」（塗りの無い所の今の消しゴム）。
  - 線の操作: 円が当たり（今の `stroke_touches()`）、当たった所で覆われていない線を消す（部分消しは今どおり切る）。塗りは消さない。
  - 塗りの操作: 中心の一番上の塗りを全体ごと消す（部分消しでも全体）。同じ操作で消した塗りの内側に中心がある位置では、下の塗りを消さない（重なった下の塗りは残る）。線は中心が見える半幅＋ 2 pt の中にあって覆われていない物だけを消す。
  - 一つの位置で消す物は、どれかを消す前に全部判定する（上の塗りを消した位置で、その下に隠れていた線を同じ位置で消さない）。
  - 解釈の注記（Q1 へ）: 「線を消すのは中心が半幅＋ 2 pt の中」を、操作の種類の判定と塗りの操作の中の線に当てた。塗りの無い所の線の消しゴム（円の半径 10 pt で当たる）は今のまま（WS079 の振る舞いを変えない）。

## 5. 受け入れ

| # | 条件 |
| --- | --- |
| F1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk` の `bin/notes`、Linux の host rules の `bin/notes`）warning 0。3 つの Makefile に新しい file |
| F2 | 合成の raster（AA の縁つき、`fill.frag` と同じ coverage）で期待の領域: 閉じた円の内側（線の縁まで ±0.25 pt）、1 pt・2.5 pt の隙間の円は閉じて内側だけ、4.5 pt の隙間は漏れる（閉じる半径 1.5 pt で約 3.4 pt まで閉じる）、穴のある形（穴の輪郭が逆向き、nonzero の面積）、文字の列の間、粒のある薄い鉛筆の円（粒の tile と同じ値）、alpha 0.22 の水彩の円、写真の模型（乱数の画素）、囲みの無い頁（頁全体の矩形 1 つ）、頁の角の三角 |
| F3 | 帯の高さ 64・440 と 1 枚の raster で、帯の境に 1 pt の隙間の円を置いても領域（FINAL の画素）と輪郭が bit で同じ |
| F4 | 0.225 pt のペンの線の両側の塗りが、線の向こうの画素へ出ない。上に入る塗り（z > 0）は線の縁を覆わない（M-2） |
| F5 | 写真の模型の頁全体で 8192 点を超え `E2BIG`。1 pt² 未満の穴が捨てられる。A3 横長で R = 4096 / 1191 |
| F6 | `ENOMEM` を返す帯（4 MiB と 2 MiB を断る）で帯が半分になって同じ領域。64 行も断られたら `ENOMEM` |
| F7 | z: 何も無い所は一番下、線の内側は線の直上、塗りの上は塗りの直上、水彩の薄い縁（alpha 0.06）の上は縁を見ない。undo・redo で戻り、journal の復旧で同じ順 |
| F8 | 消しゴム（ユーザーの決定）: 線の上から始めた操作は塗りを消さない、線から離れた塗りの上から始めた操作は塗りを全体ごと消し、続けて中心の下の線を消す、半幅＋ 2 pt の中（3 pt 外れ）では線が消える、6 pt 外れでは塗りが消え線は残る、20 pt の円の塗りを消せる、線を塗り替えた塗り（線の上の塗り）を消せて隠れた線は残る、重なった塗りは上だけ消える（drag を続けても下は残る）、部分消しでも塗りは全体、undo で戻る |
| F9 | renderer の模型（p004 の Vulkan の模型）で `notes_fill_page()` が帯の image・buffer を 4 MiB 以下で作り、帯の image の usage に `TRANSFER_SRC`、帯ごとに 1 つの pass と 1 回の copy、帯の frame が帯の行に交わる stroke だけを持ち、終わりに帯の物を全部捨てる |
| F10 | 2048×2048 px の flood と輪郭化、A4 の頁全体の塗り、A4 に手書き約 200 語の模型の頁全体の塗りの点の数と時間を host で記録（M-6） |
| F11 | 既存の host 試験（`run-notes-render.sh`・`run-notes-tools.sh`・`run-notes-host.sh`・`run-host-notes-edit.sh`・`run-host-clean.sh`）が通る。新しい試験が plain・ASan・UBSan で通る |

試験: `plan/ws216/tests/run-notes-fill.sh`（`host-notes-fill.c`: `fill.c`・`fill-page.c`・`document.c` など Notes の model と render.c・geometry.c を p004 の Vulkan の模型で link。模型の copy は試験の raster を帯の buffer に写す）。出力は worktree の `build/ws216-p005-host/`。画面の見え方と実機の時間は p007・p009。

## 6. 実行の記録（2026-10-13 P1、agent/p1、base main cae6db34f の merge a3215c5f7）

### 6.1 変えた所

| file | 内容 |
| --- | --- |
| `fill.c`（新） | §2: 頁の表、帯の読み（上へ進む時は行で終わる帯）、境・距離・閉じる・ready、seed の色（5×5）と移し、段の閉じる半径、待ちの列のある span の flood、膨らませ戻し（z で半径）、輪郭の流れの処理（たどる・捨てる・単純化・pt、角の上限） |
| `fill-page.c`（新） | §3: z を先に決め、帯の frame、`notes_renderer_band()`、塗りの stroke、`notes_document_add_stroke_at()`、log、`notes_fill_notice()` |
| `render.c` | 帯の pass（COLOR_ATTACHMENT_OPTIMAL で終わる、外への依存は TRANSFER）、`notes_renderer_band()`・`notes_renderer_band_free()`・`render_band_make()`・`render_band_record()`（layout の barrier、copy、buffer の barrier）、`render_pictures_ready()` に host の絵の layout の移しを出した、背景の LINEAR の sampler、close で帯を捨てる |
| `document.c` | `notes_document_add_stroke_at()`（`add_stroke` はこれを一番上で呼ぶ）、`notes_document_seen_at()`・`notes_stroke_contains()`・`shape_seen()`・`shape_winding()`、消しゴムの規則（§4: `erase_reached()`・`erase_decide()`・`erase_fill()`・`erase_hits()`・`stroke_near()`・`stroke_covered()`・`erase_parts_cut()`） |
| `notes.h` | `NOTES_ERASE_*`・`erase_mode`、`NOTES_FILL_WIDTH`・`NOTES_FILL_THRESHOLD`、`notes_fill_band`・`struct notes_fill_region`、fill.c・document.c の新しい関数 |
| `app.h` | renderer の帯の欄と `linear_sampler`、`notes_renderer_band*`、`notes_fill_page()`・`notes_fill_notice()` |
| 3 つの Makefile | `fill.c`・`fill-page.c` |
| `plan/ws216/tests/host-notes-fill.c`・`run-notes-fill.sh`（新） | §5 の受け入れ |
| `plan/ws216/tests/host-vulkan-mock.[ch]` | `vkCmdCopyImageToBuffer`（試験の hook）、image barrier の layout の追跡、pass の finalLayout の確かめ |
| `plan/ws216/tests/host-notes-render.c` | sampler が 1 つ増えた（LINEAR）ので数の期待を直した |

### 6.2 受け入れの結果（host、gcc 14.2.0）

`sh plan/ws216/tests/run-notes-fill.sh` → `run-notes-fill: ok`（plain・ASan・UBSan とも 154 checks、0 failed、出力 `build/ws216-p005-host/`）。

| # | 結果 |
| --- | --- |
| F1 | amd64（`ZEDBSD_CONFIG=config/current-uat.mk make build/amd64/bin/notes`）・rpi4（`ZEDBSD_CONFIG=config/ci/config-rpi4.mk make build/arm64/bin/notes`）・Linux の host rules（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws216-linux build/ws216-linux/bin/notes`）とも exit 0、warning 0。FreeBSD の native の build は未実施（`Makefile.freebsd` に足しただけ） |
| F2 | 閉じた円は内側だけ、縁は線の縁から 0.25 pt 以内（874 角、3 帯）。1・2.5 pt の隙間は閉じ、4.5 pt は漏れる。輪の中の輪は外（正）と穴（負）、面積は 3% 以内。o の内側だけ。薄い黒の鉛筆（alpha 0.30、粒）と水彩（0.22、Multiply）の円が閉じる。白い頁は頁の 4 角の 1 輪郭。頁の角の三角。頁の外は EINVAL。線のそばの tap は輪の内側、2 本の線の間の狭い所は閉じる半径を下げてその帯だけ。2 pt の溝は溝だけ（L-3）。AA の縁の tap も塗れる、線の上の tap は線（L-4）。薄い橙の鉛筆（0.8 pt、alpha 0.30）の円も閉じる |
| F3 | 200×150 pt の頁を 1 帯と 75 行の帯で、A4 を 440 行と 110 行の帯で（seed 3 か所、帯の境に隙間の円）、領域の画素の数と輪郭が bit で同じ |
| F4 | 0.225 pt の線: 下に入る塗りは両側とも線の中で止まり向こうへ出ない、上に入る塗りは線の縁（299.8875）の手前で止まる |
| F5 | 6 pt ごとの点の頁は 55,444 角で `E2BIG`（log `reason=complex points=55444`）。0.25 pt の点の穴は捨てられ輪郭 1。A3 横長の R = 4096 / 1190.551、A4 は 4。A0 は R < 4 で幅 4096 以下、50×2000 pt の頁の帯は 4096 行以下。小さすぎる輪の中は `ENOENT` か外のある領域 |
| F6 | 4 MiB・2 MiB の帯を断る模型で 110 行に半分にして同じ領域、64 行未満まで断る模型は `ENOMEM`、途中で `EIO` を返す device は `EIO`（ASan で漏れなし） |
| F7 | 紙は何も見えない、ペン・塗り・水彩の芯は見える、水彩の薄い縁は見えない。`add_stroke_at` の place 1、undo・redo で同じ place、頁の外の place は EINVAL。`notes_fill_page()`: 輪の中の紙は place 0、筆の上は筆の直上（place 3）、undo・redo、journal の復旧で同じ id と順 |
| F8 | §5 の F8 の全部（消しゴムと部分消しの両方） |
| F9 | 帯の image と buffer の確保は全部 4 MiB 以下、帯ごとに pass 1 つと copy 1 つ、copy の時の image の layout は TRANSFER_SRC_OPTIMAL（barrier の後）、全部の pass の finalLayout が Pi の受ける layout、帯の draw は頁 1 つと帯の行に交わる stroke の shape ごとに 3 つ、塗りの後に帯の物が残らない、断った塗り（複雑・頁の外・device の拒否）で頁の stroke の数が変わらない |
| F10 | host（amd64 の開発機、-O1、plain）: 2048×2048 の円 88 ms、A4 の白い頁全体 250 ms、**A4 に手書き 200 語の模型の頁全体 5,782 角・輪郭 330・430 ms**（8192 に収まる。上限はユーザーの決定で 8192 のまま、M-6）。Pi 4 の時間は p009 で測る |
| F11 | `run-notes-render.sh` ok、`run-notes-tools.sh` ok、`plan/ws079/tests/run-notes-host.sh` ok、`plan/ws175/tests/run-host-notes-edit.sh` PASS、`plan/ws175/tests/run-host-clean.sh` PASS |

`git diff --check` は空。

### 6.3 制限

- 画素は 0.25 pt（A4、R = 4）。塗りの縁は線の縁に ±0.125 pt で合い、0.1 pt の潜らせは表れない（塗り自身の AA の fringe が縁を覆う見込み、画は p007）。許容差を 1〜2 px に上げた時は 0.25〜0.5 pt ずれうる。輪郭ごとの単純化で穴が外と交わると、その所は nonzero で塗られうる（review-1 L-2）。
- 背景（PDF の頁）は頁の絵の解像度の texture を LINEAR で広げて描くので、PDF の細い線の境は R より粗い（M-5、残件）。
- GPU の画素は帯ごとの頂点の丸めで帯の分け方により 1 画素揺れうる（F3 は CPU の raster だけ、L-6）。Pi の読み戻しの buffer は uncached で、CPU の読みは遅い見込み（L-7、時間は p009）。
- 塗りは UI の thread を止める（L-8、p006 で表示か上限）。開いている text box の overlay と drag の preview は帯に入らない（L-12）。
- 塗り替えは古い塗りの上に重ねる（L-11、Future Work の候補）。

### 6.4 code review-1 の直し（2026-10-13 P1、q944 の中で、[code-review-1](code-review-1.md)）

| # | 直し |
| --- | --- |
| M2 | 最後の閉じる半径でも narrow か領域の画素が 0 なら、膨らませ戻しの前に `ENOENT`（小さな点の tap で box が逆になり uint32 で桁あふれした大きさを確保しに行っていた）。試験: 100 pt 角の頁の 3×3 px の黒い点の中心の tap が `ENOENT`（plain・ASan・UBSan） |
| M1 | `stroke_near()` に線の box ± 2 pt の早い打ち切り（結果は変わらない） |
| 軽微 1 | 帯の幅を `notes_fill_width()` 1 か所で決め、`fill.c` と `fill-page.c` の両方が使う（4096 で切る） |
| 軽微 3 | `notes_renderer_band_free()` は帯の物がある時 `vkDeviceWaitIdle` の後に捨てる（fence の timeout・失敗の時も） |
| 軽微 4 | `stroke_covered()` は、中心に一番近い所に加えて、円（半径 + 線の見える半幅）の中の線の点のどれかが上の塗りに覆われていなければ「見えている」（ユーザーの決定の「当たった所」に合わせた） |
| 軽微 5 | `fill_page_stroke()` は `next_id` を進めない（`notes_document_insert_stroke()` が入れた時に進める） |
| 軽微 2 | 記録だけ: `render_band_record()` は host の絵の ready を submit の前に 1 にする（submit が失敗した時は画面の frame と同じく device lost の扱い） |
| 軽微 6 | p009 の残件: bcm2711 は depth・stencil の format を報告しないので、Pi では帯の stencil が作れず塗りは `EIO` の見込み（読み戻し先の uncached とあわせて） |

p006 で `notes_fill_page()` の背景の引数を `int background` から帯ごとの painter（`notes_fill_background`、context つき）に替えた（p006 review-1 L-10）。同じ色の塗りの上の tap は `EEXIST`（"Already filled"、p006 review-1 C-5）、log に `NOTES FILL bands=B background_us=U` を足した（p006 review-1 C-2）。

### 6.5 未実施・残り

- QEMU・実機の画と時間は無い（p007 で T1、Pi 4 は p009）。p007 の T1 の依頼は p006 の UI の後にまとめる。
- M-5 の続き: 帯ごとに PDF の display list を帯の大きさに CPU で raster にして背景に使う → p006 で入れた（[p006 §6](../phase006/phase.md)）。
- FreeBSD の native の build。
- 規約の全文の見直しは p008。
