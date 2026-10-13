<!-- awesome-plan project=zedbsd record=ws216-p001 -->

# ws216-p001: Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）— 設計

Parent: [WS216](../ws.md)
Status: in-progress（P1。設計 1、design-reviewer の review とユーザーの判断 D1〜D9 の待ち）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来と入力

- 2026-10-13 ユーザーの UAT:「Notesアプリに、筆、鉛筆、領域塗りつぶし、水彩を追加してほしいです。」（原文は [ws.md](../ws.md)）。同じ UAT で「Notesアプリでペンの感度が有効であることを確認しました。」→ 筆圧の経路（WS079 p003 の tablet）は実機で動いている。
- [Guardrail](../../guardrail.md)「画像の処理は GPU で」（2026-10-12 ユーザー）: app は描画の毎 frame の経路で画素を CPU で触らない。CPU が画素を触るのは file の decode と、起動の時・内容が変わった時の一度だけの用意に限り、結果は GPU の側で使い回す。Pi 4 で確かめる。
- main の決定（2026-09-28、[design-pdf.md](../../ws079/design-pdf.md) §1）: `pdf_outline_stroke()` が stroke の形の唯一の元で、画面と PDF が同じ polygon を塗る。
- この Phase は設計だけ。source は変えない。調べは agent/p1 = main e162b0267 の tree。`userland/desktop/notes/` は P3 が BUG-296（pinch の拡大で落ちる）で直している最中なので読むだけにした（実装の段は BUG-296 の merge の後、[§9](#9-依存と衝突)）。

## 版

| 版 | 日 | 内容 |
| --- | --- | --- |
| 1 | 2026-10-13 | 第 1 版（P1） |

## 1. 今の tree の事実

### 1.1 stroke の型と形

| 所 | 事実 |
| --- | --- |
| `notes.h:40-42` | 道具の種類は `NOTES_TOOL_PEN 0`・`NOTES_TOOL_HIGHLIGHTER 1` の 2 つだけ（edit data の tool type） |
| `notes.h:118-159` | `struct notes_point`（x・y は 1/64 pt の格子、`pressure` 0..65535、`tilt_x/y` 1/100 度、`time_ms` stroke の始まりからの ms）。`struct notes_stroke`（id・tool・color 0xRRGGBBAA・width pt・has_tilt・start_ms・点の列・bounds・`outline` の polygon 1 つ） |
| `document.c:205-268` `notes_stroke_outline()` | 点を libpdf の `pdf_stroke_point` に直して `pdf_outline_stroke(samples, n, width)` を呼び、polygon と bounds を持つ。蛍光ペンだけ筆圧を 1.0 に固定する。点が変わるまで cache |
| `userland/base/libpdf/outline.c:57-67,183-250` | 幅 = `width × (0.15 + 0.85 × p^0.7)`（下限 15%、指数 0.7 は定数）。centripetal Catmull-Rom で全点を通し、0.05 pt の誤差で区間に切り、丸い join と丸い cap。nonzero で塗る 1 つの閉じた polygon。同じ入力に同じ出力。区間の補間は前後の点（i-1..i+2）を使う |
| `document.c:1368-1410` `stroke_touches()` | 消しゴムの当たりは点の列の線分と円の距離（`radius + width/2`）。polygon は bounds の箱の検査だけに使う |
| `document.c:703-`・`1657`・`1716-1725` | 部分消しは点の列を円で切り、境の点を補間で足す（時刻も補間）。切れ端の時刻は切れ端の最初の点から数え直す |
| `document.c:524-553` | `notes_document_add_stroke()` は undo の `NOTES_UNDO_ADD_STROKE` を積んで最上に入れる。undo の entry は place を持ち、`notes_document_insert_stroke(page, place)`（`document.c:419`）は途中への挿入もできる（`reshaped` が増え、頁の絵が描き直しになる） |
| `main.c:1827-1842` | 描き始め: `app->tool` が蛍光ペンなら `NOTES_TOOL_HIGHLIGHTER`、他は `NOTES_TOOL_PEN`。色と幅は `notes_ui_color/width(tool, index)` |
| `main.c:1940-2010` `app_sample()` | 0.25 px 未満の動きは捨てる（`MAIN_SAMPLE_STEP`）。筆圧・傾き・時刻を量子化して点を足し、毎 frame 描き直す |
| `ui.c:127-140` | 色はペン 5 色（不透明）と蛍光ペン 5 色（alpha 0x66）、幅はペン {1.5, 3, 6} pt・蛍光ペン {10, 16, 24} pt。色と幅の index は道具の間で共有 |

### 1.2 描画（Notes 自身の Vulkan、libkeiland の canvas ではない）

| 所 | 事実 |
| --- | --- |
| `notes.h:16-25`・`app.h:8-12` | Notes は自分の Vulkan（`render.c`）で頁と stroke を描く。toolbar は CPU で描いた絵（`ui.c`、状態が変わった時だけ）、text box の overlay だけ libkeiland の widget。**WS210（libkeiland の canvas を GPU へ）の経路ではない**。WS210 p008 の対象は Notes の overlay（text box）と IME の popup だけ |
| `geometry.c:8-27,174-` | stroke は stencil の 3 pass: (1) 最初の角からの fan で winding を stencil に数える、(2) 縁の外側 1 px の fringe を stencil 0 の所に alpha の勾配で（AA、multisample なし）、(3) bounds の箱を stencil ≠ 0 の所に塗り stencil を 0 に戻す。一画素は一度だけ塗られ、半透明の自己交差が濃くならない |
| `render.c:1883-1920` | stencil は 8 bit の INCREMENT/DECREMENT_AND_WRAP。blend は premultiplied の `ONE, ONE_MINUS_SRC_ALPHA` の 1 種類だけ。pipeline は STENCIL・FRINGE・COVER・PLAIN・TEXTURE の 5（`app.h:125-130`） |
| `render.c:1417-1419` | sampler は CLAMP_TO_EDGE の 1 つ。texture は toolbar・page・background・overlay の 4 つ（`app.h:141-145`） |
| `render.c:805,1530` | swapchain と頁の絵は 8 bit UNORM（sRGB の format ではない）→ blend は保存された値の上で行われ、PDF の DeviceRGB の合成と同じ式になる |
| `shaders/fill.frag`・`draw.vert` | fill は `color.a × (1 − smoothstep(|extra.x|))` の premultiplied。vertex は x・y・u・v・rgba の 8 float、push constant は window の大きさの vec4（z・w は未使用）。「i915 の native compiler が gl_VertexIndex を取らない」ので値は全部 vertex buffer から |
| `main.c:2364-2395,2482-2560` | 頁の絵（GPU の image、頁全体を今の倍率で、`NOTES_PICTURE_MAX` 4096 まで）に完成した stroke を描き、新しい stroke は上に足すだけ（`add`）。途中の挿入・削除・倍率の変化は全部描き直し（`full`）。描いている途中の stroke は window の frame に頁の絵の上から描く |
| WS141 の ws.md（p017） | Pi 4 では Notes が起動しない（stencil の format で失敗、`stencil format failed -11`、master 564 行）。Pi の確かめは WS141 p017 の後 |

### 1.3 PDF と編集 data

| 所 | 事実 |
| --- | --- |
| `save.c:629-667` `save_strokes()` | 各 stroke の outline を `pdf_writer_set_fill_color(r, g, b, a)` と `pdf_writer_fill_outline()`（nonzero の `f`）で一度ずつ塗る |
| `libpdf/writer.c:184-240` | 不透明度は ExtGState の `/ca` だけ（同じ値は共有）。blend mode（`/BM`）・clip・`q/Q`・画像を一度持って何度も描く口は Notes の stroke 用には無い（画像を運ぶ口は ws175-p003 の editor 用にある、`writer.c:425-`） |
| `libpdf/writer.c:1914` | 頁の content stream は無圧縮（画像は Flate、`update.c:2479-2506` の `compress2`、ws175-p006） |
| `libpdf/content.c:8-25,1869-1940` | reader は ExtGState の `ca`・`CA`・blend の Normal と Multiply、clip（`W W* n`）、image XObject の SMask、form、軸と放射の shading を描く。**tiling pattern は描かず飛ばす**（`PDF_DISPLAY_SKIPPED`）。zedBSD の PDF Viewer と Notes の「他の PDF」の背景はこの reader |
| `encode.c:8-37,48-51,320-332` | edit data `ZNOT`。版 1.1（基本）と 2.0（PDF の object の編集があるとき）。書き手は表せる一番低い版を選ぶ。2.0 は 1 の reader が拒み、その file を「他の PDF」として開く（見た目は PDF の content のまま） |
| `encode.c:370-381`・`1010-1020` | `TOOL` は種類 u8・色 4 byte・幅・「筆圧の曲線の id」u8・flags u8。後の 2 つは今は 0 を書き、読む側は読み捨てる。**種類は検査しない**（知らない種類の stroke は `notes_stroke_outline()` でペンとして描かれる） |
| `encode.c:428-437,635-643` | stroke の flags は `TILT 0x01`・`TIME 0x02`。時刻は常に書く |
| `journal.c:28-36,53,565-570` | journal の版 2。header の版が**ちょうど 2** でないと復旧しない（古い版も読まない） |
| `design-pdf.md` §2.1 | 「major が読めない値なら Notes は編集 data を使わず §3 の『他の PDF』として開く」「minor の増加は chunk の追加だけ」 |

### 1.4 入力と UI

| 所 | 事実 |
| --- | --- |
| `notes.h:392-407`・`tablet.c` | 入力は pointer・pen・eraser の 3 源。pen は筆圧 0..1・傾きを持つ（WS079 p003）。pointer の筆圧は一定 0.5 |
| `touch.h:8-33` | 指は頁を動かす（1 本で scroll、2 本で zoom、double tap で拡大）。toolbar の Finger が on の時だけ 1 本の指が書く。pen が近い間と離れて 500 ms は新しい指を手のひらとして取らない |
| `ui.c:384-470`・`ui_draw` | toolbar は 1 枚の glass の card に Pen・Marker・Eraser・Select・Text・Finger・5 色・3 幅・Undo・Redo・頁・+ Page・Save を一列に並べる。card が window より広いと左端から描き、はみ出した button は押せない。`NOTES_BUTTONS 32`（`app.h:50`）。絵は band（68 px）の下に 44 px の notice の行を持つ（`app.h:36`） |
| `menu.c:101-106` | System Menu の Tool に Pen・Marker・Eraser・Select・Text の radio |
| `main.c:1647-1660` | key: P ペン、M 蛍光ペン、E 消しゴム、T 文字（Control なし） |

## 2. 方針の骨組み: 「一つの形の列」を画面と PDF が塗る

今の原則（stroke の点 → 一つの polygon → 画面も PDF も同じ polygon を塗る）を広げる。**stroke は点の列から決まる「形の列」（shape の列）を持ち、画面と PDF が同じ列を同じ順に塗る**。

```text
struct notes_shape（案、document.c が作り、点が変わるまで cache）
  polygon      角の列（pt）。複数の輪郭（contour）を持てる（塗りの穴）。nonzero で塗る
  contours     輪郭ごとの角の数（1 つの輪郭なら 1 個）
  alpha        色の alpha に掛ける値（0..1）
  paint        SOLID | GRAIN（紙の粒の texture を alpha に掛ける）
  blend        NORMAL | MULTIPLY
  seam_start   この形の始めの切り口が隣の形と共有の辺か（鉛筆の区間、fringe を出さない）
  seam_end     同じく終わり
```

| 道具 | 形の列 |
| --- | --- |
| ペン・蛍光ペン | 今のまま 1 つ（SOLID・NORMAL、alpha 1）。**輪郭は今と bit 同一**（古い file の見た目を変えない） |
| 筆 | 1 つ（SOLID・NORMAL）。幅の関数だけが違う |
| 鉛筆 | 筆圧の段ごとの区間の形が 1〜数個（GRAIN・NORMAL、alpha は段の濃さ）。区間の継ぎ目は共有の辺 |
| 水彩 | 3 つ（芯と 2 つの縁、MULTIPLY、alpha は小さい）、縁は揺らぐ |
| 塗りつぶし | 1 つ（SOLID・NORMAL、複数の輪郭で穴を表す） |

- 画面: 各 shape を今の stencil の 3 pass で塗る（輪郭が複数なら全部の fan を同じ stencil に数えてから fringe と cover を 1 回）。新しい pipeline は blend と paint の組で増える（§6.1）。
- PDF: 各 shape を 1 回の fill（`f`、複数の輪郭は subpath）で書く。alpha と blend は ExtGState（`/ca`・`/BM /Multiply`）。GRAIN は clip と粒の画像（§5.2）。
- どちらも CPU は形（polygon）を作るだけで画素を触らない（塗りつぶしの領域の計算だけが例外、§4.4・D5）。

## 3. 各道具の描き方

### 3.1 筆（筆圧と速さで太さ、入りと抜き）

- 幅の係数 F(点) = 筆圧の項 × 速さの項 × 入り抜きの項 を Notes が点ごとに計算し、`pdf_outline_stroke` の拡張（§5.1）に「下限 0、指数 1」の style で筆圧の代わりに渡す（形の元は libpdf の輪郭のまま）。
  - 筆圧の項: `p^1.0`、下限 0.05（ペンの 0.15 より幅の差が大きい）。
  - 速さの項: 点の時刻（常に保存されている、`encode.c:635-643`）から速さ v（pt/ms）を出し、30 ms 程度の指数平滑で揺れを抑え、`clamp(1.15 − 0.35 × v / v_ref, 0.55, 1.15)`（速いほど細い）。v_ref と係数は実装の段で 5330 のペンで調える（数は案）。
  - 入り抜き: 始めから `min(3w, 0.25L)`、終わりから `min(4w, 0.3L)` の弧長（L は stroke の長さ、w は幅）で smoothstep に 0.08 → 1。端は細い点になる（半径の下限 0.05 pt で縮退を避ける）。
- 描いている途中も同じ式（今の終わりを終わりとして抜きを付ける）で描く → pen を離した時に形が跳ばない。
- 部分消しで切った切れ端は、切り口に入り抜きを付けない（stroke の flags に「始め・終わりが切り口」を持つ、§5.3）。そうしないと消した所に尖った先が生える。
- 傾きは使わない（ユーザーの指示に無い。将来の案）。

### 3.2 鉛筆（細い、紙の粒、筆圧で濃さ）

- 形: 細い（幅 {0.8, 1.4, 2.4} pt）、筆圧で幅は少しだけ（下限 0.6、指数 0.7）。
- 濃さ: 筆圧を **8 段**の濃さ（alpha 0.30〜1.0、`p^0.8` を段に丸める）にし、同じ段が続く所を 1 つの区間の形にする。段が変わる所は 2 pt 以上続いた時だけ切る（ちらつく継ぎ目を減らす）。区間の境は一つの中心線の上の同じ断面で、隣の区間と同じ角を共有する（libpdf の範囲の輪郭、§5.1）。
- 画面: 区間は別々の shape だが、継ぎ目の辺には fringe を出さない（`seam_*`）。共有の辺は向きが逆の同じ線分なので、GPU の rasterize の規則で画素は隙間も重なりも無く一方にだけ入る。段の差だけが見える。
- 紙の粒: 256×256 の 8 bit の粒の tile を Notes が決まった式（hash の value noise と contrast の曲線、seed は定数）で起動の時に一度作る。tile は頁の座標に固定（48 pt ごとに繰り返す、約 5.3 px/pt）。紙の目なので頁と一緒に拡大・縮小し、全ての鉛筆の stroke で同じ所に同じ粒が出る。
  - 画面: GRAIN の cover・fringe の shader が tile を REPEAT の sampler で頁の座標から引き、alpha に掛ける。
  - PDF: 同じ bytes の tile を画像として一度持ち、区間の形で clip して tile を並べる（§5.2）。画面と PDF の粒は同じ画素から来る。

### 3.3 水彩（半透明、重ねると濃く、縁がにじむ）

- 3 つの形を同じ点から作る: 芯（幅 w、alpha 0.22）、縁 1（1.18w、0.10）、縁 2（1.36w、0.06）。数は案で、実装の段で見て調える。
- 縁の揺らぎ: 縁の幅の係数に、弧長の 1 次元の value noise（周期 約 2w、振幅 ±8%、seed は stroke の id と縁の番号）を掛ける → 縁が不規則に広がって見える。決まった式なので開き直しても同じ形。
- 重ね: 3 つとも **Multiply**。同じ stroke の中は一つの形を一度だけ塗る（自己交差で濃くならない、実物の一筆と同じ）。別の stroke と重なると掛け算で濃くなり、色が減法で混ざる（青と黄で緑）。PDF の背景の頁の上でも同じ。
- 時間で滲み広がる動き（濡れた紙の上の広がり）は入れない（D4）。

### 3.4 領域の塗りつぶし

二つの方式を比べた:

| 方式 | 利点 | 欠点 |
| --- | --- | --- |
| (a) 見えている絵の色で決める: 頁の絵（GPU）を読み戻し、tap の点から同じ色の続く所を flood fill、輪郭を polygon にする | 「閉じた線の内側」と「同じ色の続く所」の両方を一つの規則で満たす。背景の PDF の図形・画像・他の道具の上でも見たとおりに塗れる。隙間を閉じるのが容易（膨張） | tap の一度だけ CPU が画素を読む（Guardrail の例外の確認、D5）。結果の精度は読み戻しの解像度 |
| (b) 線の幾何で決める: stroke の輪郭の polygon の boolean で囲まれた領域を求める | 解像度に依らない | 隙間・自己交差・数千の stroke で計算が重く脆い。PDF の背景の図形・画像を境に使えない |

**推し: (a)**（D5）。手順:

1. tap の時に頁の絵を最新にし、GPU で長い辺 2048 px 以下に縮めて（`vkCmdBlitImage`）host の buffer に写す。
2. tap の点の色から許容差（各 channel 10% 程度）の中で同じ色の続く所を、4 近傍の scanline の flood fill で求める。境の色（線の画素）は先に「隙間を閉じる」幅（1.5 pt）だけ膨らませ、求めた領域を同じ幅と AA の 1 px だけ膨らませ戻す（線の下に潜らせ、縁に白い隙間を残さない）。
3. 領域の輪郭を marching squares で取り、Douglas-Peucker で 0.5 px 以内に単純化し、pt の 1/64 の格子に丸める。外の輪郭と穴の輪郭は逆の向き（nonzero で穴になる）。
4. 結果は道具「塗りつぶし」の stroke（形 1 つ、複数の輪郭）として page に入れる。**後で線を消しても塗りは追従しない**（静的な形）。
5. z の位置（D6 推し）: tap の点で一番上に見えている stroke（形の内側の判定）の直上。何も無ければ（紙か背景の PDF）stroke の列の一番下。→ 白い紙の上の線の囲みを塗ると線の下に入り、既にある塗りを塗り替えるとその上に入る。
6. 囲まれていない所を tap すると頁全体（つながった所全部）を塗る。undo で戻せる。

### 3.5 消しゴムと新しい道具

- 筆・鉛筆・水彩: 今の当たり（点の列の線分と `radius + width/2`）のまま。水彩の当たりは芯の幅。
- 塗りつぶし（D7 推し）: 消しゴムの円が塗りの縁（輪郭の線分、閉じる辺を含む）に触れたら、stroke の消しゴムでも部分消しでも塗り全体を消す。内側を消しゴムでなぞっても消えない（塗りの上の線を消す時に塗りが消えないように）。塗りに穴を開ける部分消しは入れない。

## 4. 速さ（Pi 4・5330）

数は全て推測（実測は p002・p005・p007）。

### 4.1 描いている途中（毎 frame の CPU）

- 今のペンも毎 frame 点の列の全体から輪郭を作り直す（O(n)、stroke の長さに比例）。筆・鉛筆は 1 回、水彩は 3 回（芯と縁）作るので約 3 倍。Pi の Cortex-A72 で長い水彩の一筆（2000 点）が 1 frame の予算（16 ms）を超えるかは未測。
- 超えた時の手当て（p005 で測ってから選ぶ）: (1) 確定した前の部分の輪郭を持ち、後ろの数区間だけ作り直す、(2) 途中は芯だけを描き、離した時に縁を足す（見た目が離した時に変わる）。

### 4.2 頁の絵の描き直し（GPU）

- 新しい stroke は頁の絵の上に足すだけなので、一筆の費用は一度。描き直し（頁の移動、zoom の終わり、下の stroke の undo、塗りの挿入）は全部の形を塗る。
- 今の stencil の fan は最初の角から全ての辺へ三角形を張るので、長い・曲がった stroke では bounds の面積の数倍を塗る。cover も bounds の箱全体。水彩は 3 つの形で、大きな wash が多い頁では、Pi（V3D 4.2）で描き直しに 0.3〜0.6 s かかる見込み（A4 を約 1.2 px/pt、大きな wash 100 本の推測）。5330 は 1 桁速い見込み。
- 手当て（Pi の実測で要れば p004 の中で）: cover を bounds の箱ではなく中心線の区間ごとの小さな箱の列にする（stencil の 0 に戻す規則で重なりは二度塗らない）。さらに要れば stencil を fan ではなく libpdf の輪郭の素片（台形・扇・cap の和、outline.c の注釈の性質）から数える。
- draw の数: 1 つの形に 3 draw。鉛筆の区間で形が増える。Pi の draw あたりの費用は WS210 p002 の spike の数を使う。

### 4.3 粒と Multiply

- 粒は cover と fringe の shader の texture の 1 回の sample。Multiply は blend の係数の違いだけ。どちらも費用はほぼ増えない。

### 4.4 塗りつぶし（tap の一度）

- 読み戻しは長い辺 2048 px 以下（RGBA で最大約 24 MB、Pi の画面の倍率なら 3 MB 程度）。flood fill と輪郭化は CPU で 10〜100 ms の見込み（Pi）。毎 frame ではなく tap の一度。結果の polygon を GPU が使い回す。Guardrail の「内容が変わった時の一度だけの用意」に当たると判断したが、ユーザーに確かめる（D5）。

## 5. PDF への出し方と編集 data の版

### 5.1 libpdf の輪郭の拡張

`pdf_outline_stroke()` は今のまま（ペンの出力を bit 同一に保つ、golden の host 試験）。次を足す（API の形は p002 で決める）:

- style: 幅、下限、指数（ペンは 0.15・0.7）。半径の下限（0.05 pt）。
- 範囲: 点の列全体を渡し、区間 [first, last] の輪郭だけを作る。spline は全体の点（前後の点）で作るので、隣の区間と境の断面が同じ計算で同じ値になる。境の端は丸い cap ではなく平らな切り口（鉛筆の区間）。間引き（`keep_samples` の 0.001 pt）は全体に一度だけ掛ける。
- 筆・水彩の幅の係数（速さ・入り抜き・揺らぎ）は Notes が点ごとに計算して筆圧の代わりに渡すので、libpdf は「点ごとの係数で幅が決まる」ことだけを知る。

### 5.2 PDF の中の形（全部 vector、粒だけ小さな画像）

| 道具 | PDF の content | 他の viewer |
| --- | --- | --- |
| 筆 | `r g b rg` と輪郭の `f`（今のペンと同じ） | どこでも同じ |
| 水彩 | 3 つの `f`、各々 `/GSn gs`（`/ca a /BM /Multiply`） | ca と Multiply は PDF 1.4 の透明で、poppler・Ghostscript・pdf.js・Preview・Acrobat が描く。libpdf の reader も描く（`content.c:1869-1940`） |
| 鉛筆 | 区間ごとに `q /GSn gs <輪郭> W n` → 区間の箱にかかる粒の tile ごとに `q <48 pt の行列> cm /GrainK Do Q` → `Q`。粒の画像は色ごとに 1 つ（DeviceRGB の一色と SMask の粒、Flate）を文書で共有 | clip と SMask の画像はどこでも描く。libpdf の reader も今のまま描く。**tiling pattern は使わない**（libpdf の reader が飛ばすので、zedBSD の PDF Viewer と古い Notes で鉛筆が消える） |
| 塗りつぶし | 輪郭ごとの subpath と `f`（nonzero） | どこでも同じ |

- writer に足す口（p002）: ExtGState の `/BM`（同じ (ca, BM) は共有）、輪郭での clip、`q`・`Q`、一度持った画像を行列で描く（ws175-p003 の「運ぶ画像」の口を stroke にも使えるように）。
- 鉛筆の区間の継ぎ目: 他の viewer は fill ごとに AA するので、高い倍率で継ぎ目に薄い髪の毛ほどの線が見えることがある（画面には出ない）。8 段・2 pt の規則で継ぎ目を少なくする。残る制限として記録する。
- 粒の tile の数: 区間の形の箱にかかる tile だけ。1 本の鉛筆の線で数個〜数十。頁を斜めに横切る線は区間の箱ごとに数えるので数百まで（上限を置く）。

**raster（stroke や頁を画像にして埋める）を選ばない理由**（D1）: 解像度が固定で拡大・印刷で粗くなる。file が大きい（鉛筆 500 本の頁で数 MB〜十数 MB の見込み）。編集のたびに CPU で画像を作り直す（Guardrail の毎 frame の禁止には当たらないが、保存が重い）。画面は GPU の vector で描くので、PDF だけが別の作りになり見た目がずれる。

### 5.3 file の大きさ

- 水彩は 1 本でペンの約 3 倍、鉛筆は区間の数の分と tile の `Do`。content stream は今無圧縮（`writer.c:1914`）。**content stream を Flate にする**（D8 推し）と、数の text は 3〜5 分の 1 に縮む見込み。libpdf の reader は Flate の content を読む（p006）。編集 data の content の hash は decode した bytes なので、hash の比べ方は変わらない。
- 粒の画像は 256×256 の SMask と一色の RGB で、Flate で 1 色あたり数十 KB（noise はあまり縮まない）、文書で一度。

### 5.4 編集 data の版（古い file を読める）

- 新しい道具の種類: `NOTES_TOOL_BRUSH 2`・`PENCIL 3`・`WATERCOLOR 4`・`FILL 5`。種類の一覧の正本は [design-input-notes.md](../../ws079/design-input-notes.md)（WS079）にあるので、この設計の §5.4 へのリンクを足すことを Q1 に頼む（他の WS の plan は P1 が直さない）。
- **版 3.0**（D9 推し）: 新しい種類の stroke が一つでもある文書は major 3 で書く。3 は 2 の上位（`IMAG`・`EDIT` も持てる）。書き手は今と同じく表せる一番低い版を選ぶ（新しい道具なし・編集なし → 1.1、編集あり → 2.0、新しい道具あり → 3.0）。今の Notes（1・2 を読む）は 3 を拒み、file を「他の PDF」として開く → 背景として PDF の content がそのまま見え（ca・Multiply・clip・画像は libpdf の reader が描く）、新しい道具の stroke は編集できないが見た目は保たれる。新しい Notes は 1.1・2.0・3.0 を全部読む（今ある file は今のまま開く）。
  - 代わりの案「1.x のまま、知らない種類はペンとして描く」（今の reader の振る舞い）を選ばない理由: 古い Notes が鉛筆・水彩・塗りを黒いペンの線（塗りは輪郭の線）として描き、保存するとその見た目で PDF を書き直してしまう。
- 3.0 での stroke の追加:
  - `TOOL` の「曲線の id」の byte を「形の式の版」にする（新しい種類は 1）。後で筆や水彩の式を調えても、古い stroke は保存した時の式で描ける。知らない版は知っている一番新しい式で描き、版の値は保存で保つ。
  - stroke の flags に `CUT_START 0x04`・`CUT_END 0x08`（切り口で入り抜きを付けない）と `CONTOURS 0x10`（塗り: 点の数の後に輪郭の数と輪郭ごとの点の数、点は筆圧 0・傾きなし・時刻なし）。
- journal: 版 3（stroke の record は同じ符号化で新しい flags を持つ）。新しい Notes は版 2 と 3 の journal を復旧する（今は版がちょうど一致した時だけ、`journal.c:570`）。
- 筆・水彩の形は「保存した点 + id + 式の版」だけから決まる（live だけの状態を使わない）→ 保存・開き直し・journal の復旧で同じ形。

## 6. 画面の描画の変更（GPU だけ）

### 6.1 pipeline と shader

- 増える pipeline: FRINGE と COVER の (blend: NORMAL・MULTIPLY) × (paint: SOLID・GRAIN) のうち、使う組 NORMAL+GRAIN（鉛筆）と MULTIPLY+SOLID（水彩）の 4 つ（FRINGE_GRAIN・COVER_GRAIN・FRINGE_MULTIPLY・COVER_MULTIPLY）。STENCIL は共通。5 → 9。
- Multiply: blend を `src × ZERO + dst × SRC_COLOR`（alpha は dst のまま）にし、shader は `1 − a·c + a·c·cs`（a は alpha × coverage）を出す。頁の絵は不透明の白から始まるので、PDF の Multiply（不透明の backdrop）と同じ式。UNORM の値の上の blend（§1.2）なので PDF の DeviceRGB と一致する。
- GRAIN: shader が頁の座標（frag の座標と push constant の頁の原点・倍率）から tile を引く。push constant を vec4 2 つにする（128 byte の保証の内）。tile は 5 つ目の texture（R8、REPEAT の新しい sampler）。
- i915 の native compiler の制約（`gl_VertexIndex` なし）に合わせ、shader は今の fill と texture の範囲の命令だけ使う。SPIR-V は `shaders/regenerate.py` で作り直す。
- 複数の輪郭: `notes_frame_polygon()` を輪郭の列に広げる（全ての fan → 全ての辺の fringe → 1 回の cover）。`seam_*` の辺には fringe を出さない。

### 6.2 頁の絵との関係

- 新しい道具の stroke も「上に足す」で頁の絵に足せる（Multiply も上に重ねる順のまま）。塗りの途中への挿入は今の規則どおり全部の描き直し（`reshaped`）。
- 描いている途中の stroke は window の frame で頁の絵の上に描く（Multiply の dst は頁の絵の画素）。

## 7. 道具の欄の UI

- 今の toolbar はすでに約 1100 px あり（§1.4 から数えた見積もり）、狭い window・縦長（WS217）・Pi の小さな画面では右がはみ出して押せない。4 つの button を足すと悪くなる。
- **推し（D3）: 描く道具を一つの button にまとめる**。左端の button は今の描く道具の名前（Pen・Brush・Pencil・Water・Marker・Fill）を出す。
  - 他の道具（消しゴム・Select・Text）の時に押すと、最後に使った描く道具に戻る（一度で描ける）。
  - 描く道具の時に押すと、その下に帯（glass の小さな card、6 つの button）を開く。帯は toolbar の絵の band の下の行（今の notice の 44 px）に描く。選ぶ・帯の外を押す・ペンを頁に下ろすと閉じる（閉じるための下ろしでは描かない）。
  - Marker もこの帯に入れる → toolbar の button は今より 1 つ減る。
  - タッチ（指の tap）・ペン・mouse の全部で同じ（toolbar の押しは今と同じ経路、帯の矩形を toolbar の当たりに足す）。
- 色と太さ: 道具ごとに表を持つ（色の index・幅の index は今どおり道具の間で共有）。

| 道具 | 色 | 幅（pt） |
| --- | --- | --- |
| 筆 | ペンの 5 色 | {3, 6, 12}（最大の幅） |
| 鉛筆 | ペンの 5 色 | {0.8, 1.4, 2.4} |
| 水彩 | ペンの 5 色（alpha は §3.3 の形の値） | {8, 16, 28} |
| 塗りつぶし | ペンの 5 色（不透明） | 幅の欄を出さない |

- System Menu の Tool に Brush・Pencil・Watercolor・Fill の radio を足す。key（Control なし）: B 筆、L 鉛筆、W 水彩、F 塗りつぶし（今の P・M・E・T と重ならない。W は Control つきの Close だけが使う）。
- 塗りつぶしの操作: pen・mouse は押して離すまで 8 px 以内なら押した点で塗る（それ以上動いたら何もしない）。指は Finger が on の時だけ、書く指の begin〜end が同じ条件なら塗る（off の時は今どおり頁を動かす。double tap の拡大と衝突させない）。
- beta2 の利用の手引き（`docs/release/zedbsd-1.0.0-beta2-guide.md`）の Notes の節は、実装の後に Q1 か WS129 の担当が直す（P1 の範囲外）。

## 8. 段の分け方と受け入れ・試験

見積の単位は他の WS と同じ LW（目安）。試験の方針（AGENTS.md 2026-10-03）: 担当は build（warning 0）と変えた所の短い host 試験まで。QEMU は WS の終わりに T1、実機 5330 のペンはユーザー。

| Phase | 内容 | 受け入れ | 試験 | 見積 |
| --- | --- | --- | --- | --- |
| p002 | libpdf: 輪郭の style と範囲（平らな切り口）、writer の `/BM`・clip・`q/Q`・運ぶ画像の行列の描画、content の Flate（D8） | ペンの輪郭が今と bit 同一。範囲の輪郭の境の角が隣と同じ値。書いた PDF を `qpdf --check` が通し、poppler（`pdftoppm`）・Ghostscript・libpdf の raster で描いた絵が互いに近い（画素の差の閾値） | host（plain・ASan・UBSan）: `plan/ws216/tests/host-pdf-tools.c`（WS079 の `host-pdf-outline.c`・`host-pdf-writer.c` の形を手本に） | 2 |
| p003 | Notes の model と形式: 道具の種類 2〜5、shape の列、筆の幅の関数、鉛筆の区間、水彩の縁、ZNOT 3.0・journal 3、undo・消しゴム、`save.c` の書き出し | encode/decode の往復で model が一致（新しい flags を含む）。1.1・2.0 の file を今と同じに読む。版の選び方（1.1/2.0/3.0）。2 の reader の経路は 3 を拒む。journal の 2 と 3 の復旧。筆の切れ端に入り抜きが無い | host: `plan/ws079/tests/host-notes.c` の形の新しい試験（`plan/ws216/tests/host-notes-tools.c`）。保存した PDF を `pdftoppm` で描き目視の PNG | 3 |
| p004 | 画面: pipeline 4 つ、粒の texture（PDF と同じ bytes）、Multiply、複数の輪郭、継ぎ目の fringe なし、描いている途中の stroke | 5330 の build で warning 0。host で shader の SPIR-V を作り直せる。粒の tile の bytes が画面と PDF で同じ（host で hash） | host（tile の生成と hash、geometry の頂点の数）。画面は p007 で T1 | 2.5 |
| p005 | 領域の塗りつぶし: 頁の絵の読み戻し、flood fill（許容差・隙間を閉じる）、輪郭化、z の規則、undo・journal、消しゴムの規則 | 合成の raster（閉じた円、1 pt の隙間の円、穴のある形、文字の列、囲みの無い頁）で期待の領域と z の位置。2048 px の flood fill と輪郭化の時間を 5330 で記録 | host: `plan/ws216/tests/host-fill.c`（flood・膨張・marching squares・単純化を Notes の file から直に link） | 2.5 |
| p006 | UI: 描く道具の button と帯、Tool menu、key、色と太さの表、pen・mouse・指での選択と塗り | build warning 0。`NOTES BUTTONS` の log に帯の button が出る。帯が狭い window でも押せる | host なし（UI の log の確かめは p007） | 1.5 |
| p007 | T1 の QEMU（AAT の scenario を 1 つの起動で）、実機 5330 のペン（ユーザー）、Pi 4 の速さ（WS141 p017 の後） | QEMU: 各道具で描く（`peninject` の筆圧 0→最大）→ 画面の PNG、undo/redo、保存 → 開き直しで同じ画面、PDF Viewer で開いて近い見た目、旧形式の file の読み。5330: 筆の入り抜き・鉛筆の濃さ・水彩の重ね・塗りの見た目（ユーザーの目）。Pi: 描いている途中の frame の時間と頁の描き直しの時間（数）、必要なら §4 の手当てを入れる | AAT: `tests/scenarios/apps/notes/draw-tools.md`（新、道具ごとの節）と `fill-region.md`（新）。T1-xxx | 2 |
| p008 | 規約の全文の見直し | WS の全ての C の変更を `plan/coding-style.md` の全文で読む | conformance | 1 |

計 約 14.5 LW。

- p002 は Notes の source に触れないので、BUG-296 の merge を待たずに始められる。p003〜p006 は `userland/desktop/notes/` を変えるので BUG-296 の merge の後。
- p004 と p005 は p003 の model に依る。p006 は p003 に依る（p004・p005 と並べられるが、同じ `main.c` を触るので同じ担当が順に行う）。
- 試験の image は `plan/ws216/tests/` に config.mk と個別の file の複写だけで作る（AGENTS.md 2026-10-04）。台本の file（peninject）は `plan/ws216/tests/` に置く。

## 9. 依存と衝突

| 相手 | 関係 |
| --- | --- |
| BUG-296（P3、Notes の pinch） | `userland/desktop/notes/`（`touch.c`・`main.c`）を直している。p003〜p006 はその merge の後 |
| WS141 p017（Pi の Notes の stencil） | Pi の確かめ（p007 の Pi の部分）はその後。それまで Pi の数は推測のまま |
| WS210 p008（Notes の overlay を GPU へ） | `render.c` を触る可能性。順は Q1 が決める（WS216 の p004 と同じ時期に重ねない） |
| WS217（画面の回転、縦長で Notes） | 依存ではない。§7 の帯は縦長の幅（約 1080 px 以下）でも押せることを目標にする |
| WS079・libpdf | libpdf（`userland/base/libpdf/`）は WS079 の library。p002 の担当に libpdf の該当 file を割り当てることを Q1 に頼む。HAL・toolchain には触れない |
| Guardrail「画像の処理は GPU で」 | 塗りつぶしの読み戻しだけが CPU の画素の処理（tap の一度）。D5 で確かめる |

## 10. 決めてほしい点（推し付き）

| # | 問い | 選択肢 | 推し |
| --- | --- | --- | --- |
| D1 | PDF への出し方 | (a) vector の近似（形は polygon、alpha・Multiply、鉛筆の粒だけ小さな共有の画像） (b) 鉛筆・水彩を stroke ごとの画像（raster）で埋める | **(a)**。拡大・印刷で粗くならない、file が小さい、画面と同じ作り。水彩の縁は不規則な縁の重ねの近似で、本物の滲みの画素の質感ではない |
| D2 | 鉛筆の筆圧 → 濃さ | (a) 8 段の濃さの区間（他の viewer で継ぎ目に髪の毛ほどの線が出ることがある） (b) 1 本の線の中は一定（筆圧の平均） | **(a)** |
| D3 | 道具の欄 | (a) 描く道具を 1 つの button と帯にまとめる（Marker も帯へ） (b) 全部の道具を toolbar に並べる（狭い window で右がはみ出す） | **(a)** |
| D4 | 水彩の滲み | (a) 不規則に揺らぐ縁を 2 枚重ねた静的な形 (b) (a) に加えて、描いた後に時間で縁が広がる動き | **(a)**（(b) は形が時間で変わり、保存と undo の扱いが増える。将来の案） |
| D5 | 塗りつぶしの領域の決め方 | (a) 見えている絵の色で決める（tap の一度だけ頁の絵を読み戻して CPU で flood fill、結果は vector の polygon。Guardrail の「内容が変わった時の一度だけの用意」の例外として扱う） (b) 線の幾何（polygon の boolean） | **(a)** |
| D6 | 塗りの重なりの順 | (a) tap した所に見えている物の直上（白い紙なら一番下、線の下に入る） (b) 常に一番上（線の内側半分を覆う） (c) 常に一番下 | **(a)** |
| D7 | 消しゴムと塗り | (a) 塗りの縁に触れたら塗り全体を消す（内側をなぞっても消えない、穴は開けない） (b) 内側のどこに触れても消す | **(a)** |
| D8 | file の大きさ | (a) 頁の content stream を Flate で縮める (b) 今どおり無圧縮 | **(a)** |
| D9 | 古い Notes との互換 | (a) 新しい道具のある文書は編集 data の版 3.0（今の Notes は「他の PDF」として見た目を保って開く、編集はできない） (b) 版 1.x のまま（今の Notes は新しい道具をペンの線として描き、保存でその見た目に書き直す） | **(a)** |

設計者が決めた点（問いにしない）: 各道具の数（幅・alpha・段の数・揺らぎの振幅）は実装の段で 5330 で見て調える初期値。筆の切れ端に入り抜きを付けない。形の式の版を `TOOL` に持つ。key は B・L・W・F。

## 11. 残りと再開の条件

- design-reviewer の review（Q1 が回す）→ 指摘の反映（版 1.1）。
- ユーザーの D1〜D9 の判断 → p002 の Queue（Q1）。
- 未実施: 実測は全て未実施（§4 の数は推測）。Pi の Notes は起動しない（WS141 p017）。
