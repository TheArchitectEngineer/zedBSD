<!-- awesome-plan project=zedbsd record=ws216-p001 -->

# ws216-p001: Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）— 設計

Parent: [WS216](../ws.md)
Status: in-progress（設計 2、2026-10-13 P3。review-2 の待ち。D1〜D12 はユーザーの決定「ではあとは推奨でお願いします」で決まった、[§10](#10-決定d1d12)）
Phase disposition: normal
Queue: 設計 1 は [P1 の lane](../../agents/P1/queue.md)、設計 2 は [P3 の lane](../../agents/P3/queue.md)（Q1 の依頼）

## 第 1 版との差分の要約

review-1（[review-1.md](review-1.md)、致命的 2・重大 5・中 11・軽微 10）の全部に応じた。対応の一つずつは [§11](#11-review-1-の指摘への対応)。大きな変更:

1. **Pi 4 で Notes を起動しなくさせない（C2）**: 新しい pipeline（粒・Multiply）は使った時に初めて作る（遅延）。作れない device では Notes を終わらせず、水彩は Normal の近似、鉛筆は粒なしで描き、一度だけ log を出す。粒の texture は R8 をやめ、今の texture と同じ `B8G8R8A8_UNORM`・mip 1 段（Pi の driver の制約）。Pi の driver の対応（blend の係数 `ZERO`・`SRC_COLOR`）は WS141 への新しい依頼として [§9](#9-依存と衝突) に書き、Pi の確かめは新しい段 p009（WS141 p017 とその依頼の後）にした。
2. **i915 の 1 確保 16 MiB に当たらない（S3・S5）**: 頂点は 4 MiB 以下の塊（chunk）の列に入れ、draw は三角形の境で塊をまたいで分ける（同じ render pass の中なので stencil は保たれる）。塗りつぶしは表示の倍率に依らない決まった倍率（4 px/pt 以下）で、頁を 4 MiB 以下の帯（strip）に分けて画面外に描いて読む。どちらも断られたら半分にして作り直す（BUG-296 と同じ考え）。
3. **部分消しで形が変わらない（S2）**: 筆・鉛筆・水彩は、pen を離した時に点ごとの幅の係数（と鉛筆の濃さの段）を stroke に焼き込み、編集 data に保存する。形は焼き込んだ値と libpdf の局所的な輪郭だけで決まり、切れ端は元の値をそのまま写すので、切り口の近くより外の形は元と同じ。第 1 版の `CUT_START`・`CUT_END`・「形の式の版で式を選ぶ」はやめた。
4. **Flate（C1、D8 の決定）**: content の Flate は major 3 の file だけ。libpdf の reader は Flate を decode してから hash を取る（試験つき）。1.1・2.0 は今どおり無圧縮。
5. **鉛筆の粒の PDF（S1・M5、D11）**: 48 pt の tile の `Do` の列と clip をやめ、tiling pattern にした。区間ごとに「薄い単色の下塗り」と「粒の pattern」の 2 回の fill で、画面の式と同じ合成になる。libpdf の reader に tiling pattern を足す（p002）。古い reader は pattern を飛ばして下塗りだけを描く（鉛筆が薄い単色の線になる、消えない）。
6. **消しゴムと塗り（S4、D7 (c)）**: 消しゴムの一回の操作（押してから離すまで）で最初に当たった物が塗りなら、その操作は塗りも消せる。最初に線に当たった操作は塗りを消さない。塗りの当たりは輪郭の内側（nonzero）の判定。
7. **囲まれていない所（D12）**: 頁の縁を境として、つながった同じ色の所を塗る（頁全体でも）。形が複雑すぎる時（輪郭の点 8192 を超え、単純化を強めても収まらない）は塗らずに notice。
8. 段: p002 に reader の直し（hash の decode、tiling pattern）を足して見積を増やした。塗りの消しゴムを p003 から p005 へ。BUG-296 の依存を外した（merge 済み）。Pi の確かめを新しい p009 にした。計 約 18 LW。

## 由来と入力

- 2026-10-13 ユーザーの UAT:「Notesアプリに、筆、鉛筆、領域塗りつぶし、水彩を追加してほしいです。」（原文は [ws.md](../ws.md)）。同じ UAT で「Notesアプリでペンの感度が有効であることを確認しました。」→ 筆圧の経路（WS079 p003 の tablet）は実機で動いている。
- 2026-10-13 ユーザー「ではあとは推奨でお願いします」→ D1〜D12 は推奨で決定（[§10](#10-決定d1d12)）。
- [Guardrail](../../guardrail.md)「画像の処理は GPU で」（2026-10-12 ユーザー）: app は描画の毎 frame の経路で画素を CPU で触らない。CPU が画素を触るのは file の decode と、起動の時・内容が変わった時の一度だけの用意に限り、結果は GPU の側で使い回す。5330 で速くても Pi 4 で確かめる。
- main の決定（2026-09-28、[design-pdf.md](../../ws079/design-pdf.md) §1）: `pdf_outline_stroke()` が stroke の形の唯一の元で、画面と PDF が同じ polygon を塗る。
- [BUG-296](../../bugs/BUG-296.md)（i915 の 1 確保 16 MiB。Notes の頁の絵の確保が断られたら倍率を下げる `app_picture`）は main に merge 済み。この設計はその上限を前提にする。
- この Phase は設計だけ。source は変えない。設計 2 の調べは main `a9c627d6b` の tree（agent/p3 に merge）。

## 版

| 版 | 日 | 内容 |
| --- | --- | --- |
| 1 | 2026-10-13 | 第 1 版（P1） |
| 2 | 2026-10-13 | review-1 の全部の指摘への対応、D1〜D12 の決定の記録（P3） |

## 1. 今の tree の事実（main `a9c627d6b`）

### 1.1 stroke の型と形

| 所 | 事実 |
| --- | --- |
| `notes.h:41-42` | 道具の種類は `NOTES_TOOL_PEN 0`・`NOTES_TOOL_HIGHLIGHTER 1` の 2 つだけ |
| `notes.h:118-`・`135-` | `struct notes_point`（x・y は 1/64 pt の格子、`pressure` 0..65535、`tilt_x/y` 1/100 度、`time_ms`）。`struct notes_stroke`（id・tool・color 0xRRGGBBAA・width pt・has_tilt・start_ms・点の列・bounds・`outline` の polygon 1 つ） |
| `document.c:205` `notes_stroke_outline()` | 点を `pdf_stroke_point`（x・y・pressure の 3 つ、`include/libc/pdf.h:95`）に直して `pdf_outline_stroke(samples, n, width, …)`（`pdf.h:261`）を呼ぶ。蛍光ペンだけ筆圧 1.0。点が変わるまで cache |
| `libpdf/outline.c` | 幅 = `width × (0.15 + 0.85 × p^0.7)`。centripetal Catmull-Rom、0.05 pt の誤差で区間に切る、丸い join と cap、nonzero の 1 つの polygon。補間は前後の点（i−1..i+2）だけを使う（局所的）。`keep_samples()` は筆圧を 0..1 に clamp |
| `document.c:1368` `stroke_touches()` | 消しゴムの当たりは点の列の線分と円の距離（`radius + width/2`）。点の列を一続きの線分として見る |
| `document.c:617` `notes_document_erase_at()` | 円が触れた stroke を**全部**消す（一番上だけではない） |
| `document.c:703`・`1665` `piece_add()`・`1696` `piece_close()` | 部分消し: 切れ端は tool・色・幅だけを写した新しい id の stroke。境の点は補間、時刻は切れ端の最初の点から数え直す |
| `document.c:419`・`525` | `notes_document_insert_stroke(page, place)` は途中への挿入もできる（`reshaped`、頁の絵の描き直し）。`notes_document_add_stroke()` は undo を積んで最上に入れる |
| `main.c:1839`・`2715` | 描き始めの道具の決め方（蛍光ペンか、他はペン） |
| `main.c:1947` `app_sample()`・`105` | 0.25 px 未満の動き（`MAIN_SAMPLE_STEP`）は捨てる。同じ `time_ms` の点がありうる |
| `ui.c:137,140` | 幅はペン {1.5, 3, 6} pt・蛍光ペン {10, 16, 24} pt。色はペン 5 色（不透明）と蛍光ペン 5 色（alpha 0x66） |

### 1.2 描画（Notes 自身の Vulkan）

| 所 | 事実 |
| --- | --- |
| `render.c:187`・`1756` `render_pipelines()` | 初期化で 5 つの pipeline（STENCIL・FRINGE・COVER・PLAIN・TEXTURE）を全部作る。1 つ失敗すれば renderer の初期化が失敗する |
| `render.c:1900-1950` | stencil は 8 bit の INCREMENT/DECREMENT_AND_WRAP。blend は premultiplied の `ONE, ONE_MINUS_SRC_ALPHA` の 1 種類（注釈「Pi 4 の driver が受ける唯一の blend」）。STENCIL は `colorWriteMask = 0` |
| `render.c:1762-1777`・`2179` | push constant は 16 byte（vec4、window の大きさ、z・w は未使用）、stage は VERTEX だけ |
| `render.c:1441` | sampler は CLAMP_TO_EDGE の 1 つ |
| `render.c:1113` | 頁の絵の usage は `COLOR_ATTACHMENT | SAMPLED`（`TRANSFER_SRC` なし）。format は swapchain の format（`render.c:726`、BGRA か RGBA の UNORM） |
| `render.c:1554,1600` | 他の texture は `B8G8R8A8_UNORM` |
| `render.c:311-345`・`1690` `render_vertices()` | 頁の frame と window の frame の頂点を 1 つの host-visible の vertex buffer に入れ、足りなければ倍にして作り直す。上限の確かめは無い |
| `geometry.c:174` `notes_frame_polygon()`・`380` `frame_reserve()` | 1 つの polygon は fan の `(n−2)×3` と fringe の `n×6` で約 9n 頂点、1 頂点 32 byte（8 float）。頂点の数に上限は無い |
| `shaders/draw.vert` | 値は全部 vertex buffer から（i915 の native compiler は `gl_VertexIndex` を取らない） |
| `main.c:2492` `app_picture()`（BUG-296） | 頁の絵の確保が `VK_ERROR_OUT_OF_DEVICE_MEMORY` で断られたら画素の 3/4 を上限に倍率を下げて作り直す（最大 8 回） |
| `app.h:28,36,50,56` | toolbar の band 68 px、その下の notice の行 44 px、`NOTES_BUTTONS 32`、`NOTES_PICTURE_MAX 4096` |

### 1.3 device の上限

| 所 | 事実 |
| --- | --- |
| `src/drivers/gpu/i915/memory.h` `I915_MAX_RESOURCE_BYTES`・`libvulkan/memory.c` | i915 は 1 つの `vkAllocateMemory` が 16 MiB まで。超えると `VK_ERROR_OUT_OF_DEVICE_MEMORY`。app には見えない（`VK_KHR_maintenance3` なし） |
| `bcm2711/vulkan-pipeline-state.c:211-247` `blend_state()` | blend は「無し」か premultiplied の source-over（色も alpha も `ONE, ONE_MINUS_SRC_ALPHA, ADD`）だけ。`colorWriteMask` は RGBA 全部だけ |
| `bcm2711/vulkan-pipeline-state.c:38-40` | `pDepthStencilState != NULL` は ENOTSUP（Notes の stencil。WS141 p017） |
| `bcm2711/vulkan-query.c:386` | sample する format は `R8G8B8A8_UNORM`・`B8G8R8A8_UNORM` だけ。R8 は無い |
| `bcm2711/vulkan-resource.c:256` | image の `mipLevels` は 1 だけ |
| `bcm2711/vulkan-input.c:488` | sampler の REPEAT・MIRRORED_REPEAT・CLAMP_TO_EDGE は受ける。`vkCmdCopyImageToBuffer` と `TRANSFER_SRC` の image もある（`vulkan-buffer-copy.c`、`vulkan-resource.c:23`） |
| `bcm2711/vulkan-query.c:20` | push constant は 128 byte |

### 1.4 PDF と編集 data

| 所 | 事実 |
| --- | --- |
| `save.c:629-` `save_strokes()` | 各 stroke の outline を `pdf_writer_set_fill_color(r, g, b, a)` と `pdf_writer_fill_outline()`（nonzero の `f`）で一度ずつ塗る |
| `libpdf/writer.c:191-249`・`1552-1580` | 不透明度は ExtGState の `/ca` だけ（alpha の値で重複を除く）。writer は `current_alpha` を覚えて同じなら `gs` を省く。`/BM`・clip・`q/Q`・pattern の口は無い |
| `libpdf/writer.c:1911-1915` | 頁の content は無圧縮。画像は Flate（`writer.c:1334,1370`）。添付（編集 data）は無圧縮 |
| `libpdf/reader.c:511-575`・`3697-3717` | `pdf_document_page_content_hash()` は各 content stream に `check_unfiltered()` を掛け、`/Filter` があれば `ENOTSUP`（decode しない） |
| `save.c:1120-1154` `page_unchanged()` | hash の取れない頁は「変わった」と見なす |
| `libpdf/content.c:15-26` | reader は ca・CA、blend の Normal と Multiply、clip、image の SMask、form、軸と放射の shading（塗る所に clip した画像として）を描く。**tiling pattern は飛ばす**（`PDF_DISPLAY_SKIPPED`） |
| `libpdf/content.c:3666-3717`・`display.c:195`・`internal.h:148` | `Do` のたびに画像を decode し直す。display list の画素は 64 Mi px まで、超えると以後は描かない（`PDF_DISPLAY_LIMITED`） |
| `encode.c:48,50,327-330,539,590-592` | 編集 data の major は 1（基本）と 2（PDF の object の編集あり）。`major` が 1 か 2 でなければ拒む。`IMAG`・`EDIT` は `major == 2` の時だけ読む |
| `encode.c` `tools_find`・`TOOL` | 道具の表は (種類, 色, 幅) で重複を除く。`TOOL` の「曲線の id」・flags は今 0 を書き、読む側は読み捨てる。種類は検査しない |
| `encode.c` の stroke の flags | `TILT 0x01`・`TIME 0x02`。時刻は常に書く |
| `journal.c:53,570,768` | journal の版 2。header の版がちょうど 2 でないと復旧しない |
| `save.c:290-330` の open | 編集 data の decode が失敗した file は「他の PDF」（base）として開く。保存は base の bytes を変えずに 1 つの revision（その上の stroke と自分の編集 data、base の大きさつき）を足す |

### 1.5 入力と UI

| 所 | 事実 |
| --- | --- |
| `notes.h`・`tablet.c` | 入力は pointer・pen・eraser の 3 源。pen は筆圧 0..1・傾き。pointer の筆圧は一定 0.5 |
| `touch.h` | 指は頁を動かす（1 本で scroll、2 本で zoom、double tap で拡大）。Finger が on の時だけ 1 本の指が書く |
| `ui.c:405-` | toolbar: Pen・Marker・Eraser・Select・Text・Finger・（Select・Text の時の control 最大 4）・5 色・3 幅・Undo・Redo・頁・+ Page・Save。`ui_add_button()`（`ui.c:1101`）は `NOTES_BUTTONS` を超えた button を黙って捨てる |
| `app.h:75-122` | action の番号は 1〜19・20〜24（色）・30〜32（幅）・40〜47 を使っている |
| `plan/tools/aat/scenarios/helpers_notes_edit.py:86-104,523` | AAT の helper は `NOTES BUTTONS` の log から Pen の button を押し、`NOTES TOOL 1 name=pen` を待つ |

## 2. 方針の骨組み: 焼き込んだ値から決まる「形の列」を画面と PDF が塗る

今の原則（stroke の点 → polygon → 画面も PDF も同じ polygon）を次のように広げる。

- **新しい道具の stroke は、点ごとの「幅の係数」を持つ**。描いている間は Notes が式（§3）で毎 frame 計算し、pen を離した時に最後の計算の値を stroke に焼き込む（`factors`、鉛筆は濃さの段 `steps` も）。以後、形は「点・焼き込んだ値・道具の定数」と libpdf の輪郭だけで決まる。保存・開き直し・journal の復旧・部分消しの切れ端・undo で同じ形。
- 式（速さ・入り抜き・揺らぎ）は保存されないので、後で式を調えても古い stroke の形は変わらない。変わりうるのは道具の定数（alpha の表、粒の下限、水彩の 3 つの alpha）だけで、その版を `TOOL` に持つ（§5.4）。
- 形の列:

```text
struct notes_shape（document.c が作り、点・焼き込んだ値が変わるまで cache）
  polygon    角の列（pt）。複数の輪郭（contour）を持てる。nonzero で塗る
  contours   輪郭ごとの角の数
  alpha      色の alpha に掛ける値（0..1）
  paint      SOLID | GRAIN
  blend      NORMAL | MULTIPLY
  seam_start この形の始めの切り口が隣の形と共有の辺か（鉛筆の区間、fringe を出さない）
  seam_end   同じく終わり
```

| 道具 | 形の列 |
| --- | --- |
| ペン・蛍光ペン | 今のまま 1 つ（SOLID・NORMAL）。**輪郭は今と bit 同一**（`pdf_outline_stroke()` を変えない） |
| 筆 | 1 つ（SOLID・NORMAL）。幅は焼き込んだ係数 |
| 鉛筆 | 濃さの段ごとの区間の形が 1〜数個（GRAIN・NORMAL、alpha は段の濃さ）。継ぎ目は共有の辺 |
| 水彩 | 3 つ（芯と 2 つの縁、MULTIPLY、alpha は小さい）。縁の揺らぎは焼き込んだ係数 |
| 塗りつぶし | 1 つ（SOLID・NORMAL、複数の輪郭で穴） |

- 画面: 各 shape を今の stencil の 3 pass で塗る（輪郭が複数なら全部の fan を数えてから全部の辺の fringe、cover は 1 回）。
- PDF: 各 shape を 1 回の fill（複数の輪郭は subpath）で書く。alpha と blend は ExtGState。GRAIN は下塗りと pattern の 2 回の fill（§5.2）。
- CPU は形（polygon）を作るだけで画素を触らない。例外は塗りつぶしの tap の一度の読み戻し（D5、ユーザーの決定）と、起動の時の粒の tile の一度の用意（§3.2）。

## 3. 各道具の描き方

### 3.1 筆（筆圧と速さで太さ、入りと抜き）

- 幅 = `w × F`。F = 筆圧の項 P × 速さの項 V × 入り抜きの項 T。libpdf には「下限 0、指数 1、係数の上限 2.0」の style（§5.1）で渡すので、F は 1 を超えてよい（M3）。
  - P = `max(0.05, p)`（p は 0..1 の筆圧）。
  - V: 点 i の速さ v = `(s_j − s_k) / (t_j − t_k)`。k は `t_k ≤ t_i − 8 ms` の一番近い点（無ければ最初の点）、j は `t_j ≥ t_i + 8 ms` の一番近い点（無ければ最後の点）、s は弧長。`t_j = t_k`（同じ時刻の点だけ、M3）なら v = 0（遅い扱い）。V = `clamp(1.15 − 0.35 × v / v_ref, 0.55, 1.15)`。v_ref と係数は p007 で 5330 のペンで調える初期値。
  - T: 始めから `min(3w, 0.25L)`、終わりから `min(4w, 0.3L)` の弧長（L は stroke の長さ）で smoothstep に 0.08 → 1。半径の下限 0.05 pt（libpdf の style）。
- 描いている途中も同じ式（今の終わりを終わりとして）。見え方（L8）: 描いている間は先の `min(4w, 0.3L)`（筆 12 pt で最大 48 pt）がいつも細く、描き進むと太り戻る。速さの窓の後ろの 8 ms も描き進むと少し変わる。どちらも pen を離した時の形と同じ式なので、離した時に形は跳ばない。
- pen を離した時に F を u16（0..65535 が 0..2.0）で焼き込む。部分消しの切れ端は元の点の F を写す（§3.5）。切り口は今のペンと同じ丸い cap（入り抜きは付かない）。
- 傾きは使わない（将来の案）。

### 3.2 鉛筆（細い、紙の粒、筆圧で濃さ）

- 幅: {0.8, 1.4, 2.4} pt、F = `0.6 + 0.4 × p^0.7`。
- 濃さ: 段 q = `round(7 × p^0.8)`（0..7）、alpha = `0.30 + 0.70 × q / 7`。段の変化は新しい段が弧長 2 pt 以上続いた時だけ採る（それより短い変化は前の段のまま）。pen を離した時に段を点ごとに u8 で焼き込む。
- 区間: 焼き込んだ段の変わる点を区間の境の候補にし、libpdf の範囲の輪郭（§5.1）が境を「gentle な頂点」（middle の法線を 1 つ持つ頂点）へ後ろへずらして、その法線の上の平らな切り口で切る（M2）。隣の区間と境の 2 つの角を共有するので、重なりも隙間も無い（試験: 区間の polygon の面積の和が全体の polygon の面積と許容差で一致）。
- 画面: 区間は別々の shape。継ぎ目の辺には fringe を出さない（`seam_*`）。
- 紙の粒（M4）:
  - tile は 256×256、頁の座標に固定（48 pt ごとに繰り返す、約 5.33 texel/pt）。値 h（0..1）は整数の hash の value noise（格子 1 pt 相当、格子は 256 で wrap して周期的）と整数の contrast の表から作る。**浮動小数を使わない**ので amd64 と arm64 で同じ bytes。
  - 起動の時に CPU で一度作る（Guardrail の「起動の時の一度だけの用意」）。縮小の 2 段（128×128・64×64、同じ 48 pt に貼る）も同じ時に整数の 2×2 の平均で作る。Pi の driver は mip 1 段だけなので、3 つは別々の texture（`B8G8R8A8_UNORM`、4 channel に同じ値。R8 は使わない、C2）。頁の絵を描く時の倍率で、1 画素あたりの texel が 1.5 以下になる一番細かい段を選ぶ（縮小での aliasing を避ける。PDF の viewer の平均と見た目が近くなる）。sampler は REPEAT・LINEAR。
  - 合成の式: 区間の alpha を a、粒の下限 `g_min = 0.35`（道具の定数）として、画素の alpha は `α = 1 − (1 − a·g_min)(1 − a·h)`。これは「alpha `a·g_min` の単色」の上に「alpha `a·h` の同じ色」を重ねた source-over と同じ値で、PDF の下塗りと pattern の 2 回の fill（§5.2）と一致する。
  - 画面と PDF の粒は同じ bytes（level 0）から来る（host 試験で hash を比べる）。

### 3.3 水彩（半透明、重ねると濃く、縁がにじむ）

- 3 つの形を同じ点から作る: 芯 `C = 0.85 + 0.15 p`（alpha 0.22）、縁 1 `E1 = 1.18 × C × (1 + 0.08 × n1(s))`（0.10）、縁 2 `E2 = 1.36 × C × (1 + 0.08 × n2(s))`（0.06）。n は弧長 s の 1 次元の value noise（周期 約 2w、−1..1）、seed は stroke の id と縁の番号。数は p007 で調える初期値。
- pen を離した時に C・E1・E2 を点ごとに u16 で焼き込む（3 列）。切れ端は元の値を写すので、新しい id・0 からの弧長で揺らぎが変わることは無い（S2）。
- 3 つとも Multiply。同じ shape は一度だけ塗る（自己交差で濃くならない）。別の stroke と重なると掛け算で濃くなり、減法で混ざる。
- 時間で広がる滲みは入れない（D4）。
- Multiply の pipeline を作れない device（今の Pi 4、§6.1）では Normal の近似で描く（色 `cs`・alpha a の source-over）。重ねて濃くはなるが色は減法で混ざらない。PDF は Multiply のまま。

### 3.4 領域の塗りつぶし（D5 (a)・D6 (a)・D12）

見えている絵の色で決める（D5 (a)）。表示の倍率に依らない決まった倍率で画面外に描いて読む（S5）。

1. **倍率**: `R = min(4 px/pt, sqrt(16 Mi px / 頁の面積))`。A4 は 4 px/pt で 2380×3368（約 8 Mpx）。1 画素 0.25 pt なので、最も細いペン（0.225 pt）も AA の濃さ約 0.9 で境として見える。
2. **帯**: 頁を横長の帯に分ける。帯の色の image は 4 MiB 以下（A4 で 2380×440 px、約 8 本）。format は頁の絵と同じ（同じ render pass と pipeline を使う）、usage は `COLOR_ATTACHMENT | TRANSFER_SRC`（頁の絵の usage は変えない、L3）。stencil の image も帯の大きさ。読み戻しの host-visible の buffer も 4 MiB 以下。帯は tap の点を含む物から描き、flood fill が帯の上か下の端に届いたら隣の帯を描く（遅延）。読んだ帯は 1 画素 1 bit の「境か」に直して host の memory（malloc、Vulkan の確保ではない）に持つ。確保が `VK_ERROR_OUT_OF_DEVICE_MEMORY` で断られたら帯の高さを半分にして作り直す（64 px まで。それでも断られたら塗らずに notice）。
3. **描く物**: 頁の背景（PDF の頁の texture、紙の白）と全部の stroke を、今の頁の絵と同じ経路で帯の座標に描く（push constant の原点と倍率を替える）。背景の texture の解像度は頁の絵の物なので、背景の境は R より粗いことがある（制限）。
4. **境**: seed（tap の点の色）との差が各 channel 26/255（約 10%）を超える画素を境とする。format の BGRA・RGBA の順は format で見分ける（L4）。
5. **隙間を閉じる**: 境を 1.5 pt（R で 6 px）膨らませてから、seed から 4 近傍の scanline の flood fill。頁の縁も境として扱う（D12）。求めた領域を同じ 1.5 pt と 0.1 pt（線の下へ潜らせる分、pt で固定、S5）だけ膨らませ戻す。
6. **輪郭**: marching squares で輪郭を取り、面積 1 pt² 以下の島と穴を捨て（粒・AA の点、M7）、Douglas-Peucker で 0.5 px（0.125 pt）以内に単純化、1/64 pt の格子に丸める。外の輪郭と穴は逆の向き。
7. **上限**（M7・S3）: 輪郭の点の合計が 8192 を超えたら単純化の誤差を 1 px・2 px と倍にする。それでも超えたら塗らずに notice「塗る所が複雑すぎます」（`NOTES FILL refused reason=complex points=N`）。
8. **z の位置**（D6 (a)）: tap の点で一番上に見えている物（形の内側の判定、nonzero）の直上。何も無ければ（紙か背景の PDF）stroke の列の一番下。一番上が半透明（蛍光ペン・水彩）でもその直上（塗りがそれを覆う、塗り替えと同じ）。
9. 結果は道具「塗りつぶし」の stroke（形 1 つ、複数の輪郭、alpha 1）として page に入れる。undo で戻せる。後で線を消しても塗りは追従しない（静的な形）。
10. **囲まれていない所**（D12）: 頁の縁を境とするので、つながった所を全部塗る（頁全体でもよい）。複雑すぎる時は 7 の notice。

### 3.5 消しゴムと新しい道具

- 筆・鉛筆・水彩（stroke の消しゴム・部分消し）: 今の当たり（点の列の線分と `radius + 幅の半分`、幅は筆・鉛筆は焼き込んだ係数の最大、水彩は芯）。部分消しの切れ端は、元の点の焼き込んだ値（F・段・C・E1・E2）をそのまま写す。境の点の値は両隣の値の線形の補間。切れ端の形は、消しゴムの円（+ 幅の半分 + 点 2 つ分の間隔）の外では元の形と同じ（libpdf の輪郭は前後 2 点だけで決まる。試験は §8 p003）。
- 塗り（D7 (c)、S4）: 「見えている所に触れたらその物を消す」。
  - 消しゴムの一回の操作（押してから離すまで）で、最初に何かに当たった時、円の中心で一番上に見えている物（nonzero の内側の判定と z の順）が塗りなら、その操作は「塗りも消す操作」になる。以後その操作で中心が内側に入った塗りを全体ごと消す（穴は開けない）。線も今どおり消す。
  - 最初に線（塗り以外）に当たった操作は、塗りを消さない。囲みの線を消しても、線を消した跡をなぞり続けても塗りは残る。
  - 塗りの当たりは `stroke_touches()` の点の列の線分では見ない（輪郭の間に偽の線分ができるので）。塗りは輪郭の内側の判定だけ。
  - 部分消し（`erase_parts_at`）でも同じ規則で、塗りは全体を消す。

## 4. 速さと確保の上限（Pi 4・5330）

数は推測（実測は p007・p009）。

### 4.1 描いている途中（毎 frame の CPU）

- 今のペンも毎 frame 点の列の全体から輪郭を作り直す（O(n)）。筆・鉛筆は 1 回と係数の計算、水彩は 3 回。Pi の Cortex-A72 で長い水彩の一筆（2000 点）が 1 frame（16 ms）を超えるかは未測（p009）。
- 超えた時の手当て（p009 で測ってから選ぶ）: (1) 確定した前の部分の輪郭を持ち、後ろの数区間だけ作り直す（輪郭は局所的なので可能）、(2) 途中は芯だけを描き、離した時に縁を足す。CPU の仕事は画素ではなく形なので Guardrail には当たらない。

### 4.2 頂点の塊（S3）

- render.c の vertex buffer を「塊の列」にする。1 つの塊は 4 MiB（131,072 頂点）以下の host-visible の buffer で、必要な数だけ持ち、frame の間で使い回す（一番多かった frame の数まで増える）。
- 頁の frame・window の frame の頂点を順に塊に詰める。1 つの draw が塊の端をまたぐ時は**三角形の境（3 頂点ごと）で分けて**、次の塊で続ける。stencil の fan・fringe の帯は三角形ごとに独立なので分けても同じ結果。cover は箱の 6 頂点。全部が 1 つの command buffer の 1 つの render pass の中なので、stencil の値は塊をまたいで保たれる（`vkCmdBindVertexBuffers` を塊ごとに）。
- 塊の確保が `VK_ERROR_OUT_OF_DEVICE_MEMORY` で断られたら、塊の大きさを半分にして作り直す（256 KiB まで）。それでも断られたら今と同じく失敗。
- 今のペンの密な頁も同じ上限に当たりうるので、この直しはペンにも効く。5330 で密な頁の頂点の最大の数を一度 log で測る（p007、`NOTES FRAME vertices=N chunks=M`）。
- 帯の描画（§3.4）も同じ塊を使う。

### 4.3 頁の絵の描き直し（GPU）

- 新しい stroke は頁の絵の上に足すだけ。描き直し（移動・zoom の終わり・下の stroke の undo・塗りの挿入）は全部の形を塗る。
- 今の fan は最初の角から全ての辺へ張るので、長い stroke では bounds の数倍の面積を塗る。水彩は形 3 つ。Pi（V3D 4.2）で、大きな wash の多い頁の描き直しは 0.3〜0.6 s の見込み（推測）。5330 は 1 桁速い見込み。
- 手当て（p009 の実測で要れば）: cover を中心線の区間ごとの小さな箱の列にする。さらに要れば stencil を輪郭の素片から数える。

### 4.4 粒と Multiply

- 粒は cover と fringe の shader の texture の 1 回の sample。Multiply は blend の係数の違いだけ（i915・Venus）。Pi では Multiply は WS141 の依頼の後（§9）。

### 4.5 塗りつぶし（tap の一度）

- A4 で最大 8 本の帯、各 4 MiB 以下の読み戻し。flood fill・膨張・marching squares は CPU で、Pi で 8 Mpx の頁全体の塗りに 100〜300 ms の見込み（推測、p009 で測る）。囲まれた小さな所は帯 1〜2 本で済む。毎 frame ではなく tap の一度（D5 の決定）。結果の polygon を GPU が使い回す。

## 5. PDF への出し方と編集 data の版

### 5.1 libpdf の輪郭の拡張（p002）

`pdf_outline_stroke()` は今のまま（ペンの出力を bit 同一に保つ、golden の host 試験）。新しい関数（形は p002 で決める。`include/libc/pdf.h` と `userland/base/libpdf/exports.map` に足す、L9）:

- 点: x・y と係数（0..max）。
- style: 下限（0）、指数（1）、係数の上限（2.0、M3）、半径の下限（0.05 pt）。係数は 1 に clamp しない。
- 範囲: 点の列全体を渡し、境の候補の点の index の列を渡す。libpdf は各候補を後ろの一番近い gentle な頂点へずらし（M2）、実際の境の index を返す。各区間の輪郭は、境で middle の法線の上の平らな切り口、両端の区間の外の端は丸い cap。spline は全体の点で作るので、隣の区間と境の角が同じ計算で同じ値になる。間引き（`keep_samples`）は全体に一度だけ。

### 5.2 PDF の中の形（全部 vector、粒だけ小さな画像の pattern）

| 道具 | PDF の content | 他の viewer・古い reader |
| --- | --- | --- |
| 筆 | `r g b rg` と輪郭の `f`（今のペンと同じ） | どこでも同じ |
| 水彩 | 3 つの `f`、各々 `/GSn gs`（`/ca a /BM /Multiply`） | ca と Multiply は PDF 1.4 の透明で、poppler・Ghostscript・pdf.js・Preview・Acrobat・libpdf の reader が描く |
| 鉛筆 | 区間ごとに (1) `/GSu gs`（`/ca a·g_min`）`r g b rg` 輪郭 `f`、(2) `/GSp gs`（`/ca a`）`/Pattern cs /GrainK scn` 輪郭 `f` | (1)(2) を描く viewer では画面と同じ合成（§3.2）。tiling pattern を飛ばす reader（今の libpdf）は (1) だけを描く: 鉛筆は粒の無い薄い単色の線になる（消えない、頁の残りも欠けない） |
| 塗りつぶし | 輪郭ごとの subpath と `f`（nonzero） | どこでも同じ |

- 粒の pattern（D11）: 色ごとに 1 つの tiling pattern（PatternType 1・PaintType 1・TilingType 1、BBox と XStep・YStep は 1、Matrix は頁の既定の座標で 48 pt の大きさと頁の左上を原点とする向き）。cell は 1 つの画像（256×256、DeviceRGB の一色、SMask は粒の h、Flate）を単位の正方形に描くだけ。粒の SMask は文書で 1 つの object を色の間で共有する。pattern の空間は頁の既定の座標なので、content の `cm` に依らず、画面と同じ頁の点に同じ texel が来る（向きと原点は p003 の host 試験で `pdftoppm` の画素と画面の式を比べて確かめる）。
- writer に足す口（p002）:
  - ExtGState の鍵は (ca, BM)。同じ組は共有。Normal に戻す ExtGState（M1）。
  - `q`・`Q` で writer の状態（塗りの色・alpha・blend）を積んで戻す。writer が覚えた状態と PDF の状態が `Q` の後もずれない（M1）。
  - tiling pattern（画像 1 枚の cell）の object と、pattern を色にした fill。
  - content の Flate（major 3 の時だけ、§5.3）。新しく作る notebook（writer）と、他の PDF の上の revision（`update.c`）の両方。
- reader に足す物（p002）:
  - `pdf_document_page_content_hash()` は Flate の content を decode してから hash を取る（decode 後の大きさの上限 64 MiB、超えたら今どおり `ENOTSUP`）。他の filter は今どおり `ENOTSUP`（C1）。
  - tiling pattern（PaintType 1。cell の内容を pattern object ごとに一度だけ描いて持ち、塗る所に頁の座標の繰り返しで貼る。cell の画素は display list の画素の数に一度だけ数える。cell の大きさの上限を置き、超えたら今どおり飛ばす）（S1）。PaintType 2 は今どおり飛ばす。
- 鉛筆の区間の継ぎ目・pattern の継ぎ目: 他の viewer は fill ごとに AA するので、高い倍率で継ぎ目に薄い髪の毛ほどの線が見えることがある（画面には出ない）。p002 の試験で `pdftoppm -r 600` の継ぎ目の線を数え、残れば制限として記録する（M5）。
- raster（stroke や頁を画像にして埋める）を選ばない理由（D1）: 解像度が固定で拡大・印刷で粗くなる、file が大きい、保存が重い、画面と PDF が別の作りになる。

### 5.3 file の大きさと Flate（D8 の決定、C1）

- content の Flate は **major 3 の file だけ**（新しい道具の stroke がある時）。1.1・2.0 の file は今どおり無圧縮なので、今の Notes（filter つきの content の hash を `ENOTSUP` で断る、`reader.c:3715`）でも今どおり開ける。
- 新しい Notes の reader は Flate を decode してから hash を取る。保存の時に記録する hash も decode した bytes（= writer が圧縮する前の bytes）の hash なので一致する。試験（p002・p003）: (a) Flate の content の major 3 の file を保存 → 開く → `NOTES_OPENED_NOTES`（頁が CHANGED にならない）、(b) 1.1・2.0 の file の content に `/Filter` が無い、(c) filter を `ENOTSUP` にする今の reader の模型で 1.1 の file が CHANGED にならない。
- 水彩は 1 本でペンの約 3 倍、鉛筆は区間ごとに fill 2 回。Flate で数の text は 3〜5 分の 1 の見込み。
- 編集 data（添付、無圧縮）は焼き込んだ値の分だけ増える: 筆・鉛筆は点あたり 2〜3 byte、水彩は 6 byte。添付の Flate は reader の添付の読み（`reader.c:3939` も `check_unfiltered`）を変えるので、この WS では入れない。
- 粒の SMask は 256×256 で Flate で数十 KB（noise はあまり縮まない）、文書で一度。一色の RGB の画像は Flate でほぼ 0。

### 5.4 編集 data の版（D9 の決定）

- 新しい道具の種類: `NOTES_TOOL_BRUSH 2`・`PENCIL 3`・`WATERCOLOR 4`・`FILL 5`。種類の一覧の正本は [design-input-notes.md](../../ws079/design-input-notes.md)（WS079）なので、この節へのリンクを足すことを Q1 に頼む。
- **major 3**: 新しい種類の stroke が一つでもある文書は major 3 で書く。3 は 2 の上位で、`IMAG`・`EDIT` も持てる（読む条件を `major >= 2` にする、M8）。書き手は表せる一番低い版を選ぶ（新しい道具なし・編集なし → 1.1、編集あり → 2.0、新しい道具あり → 3.0）。新しい Notes は 1.1・2.0・3.0 を全部読む。
- 今の Notes（1・2 を読む）は 3 を拒み、file を「他の PDF」として開く。見た目は PDF の content のまま（水彩の ca・Multiply は描く。鉛筆は下塗りだけの薄い単色、§5.2）。**今の Notes でその file に書いて保存すると**（autosave を含む）、今の Notes は 3.0 の file を base にして自分の 1.1 の編集 data の revision を足す（`save.c` の open と保存の経路、§1.4）。以後は新しい Notes でもその file の新しい道具の stroke は base の content（背景）になり、編集できない（2.0 の時と同じ、M9）。base の中の 3.0 の編集 data は bytes として残るので、将来それを読み直す案を Future Work の候補として Q1 に送る。
- `TOOL`: 「曲線の id」の byte を「道具の定数の版」にする（新しい種類は 1、ペン・蛍光ペンは 0 のまま）。`tools_find` の鍵は (種類, 色, 幅, 定数の版)（M8）。知らない定数の版を持つ文書は、知らない major と同じく編集 data を使わず「他の PDF」として開く（保存で違う見た目に書き直さないため）。
- stroke の flags（3.0 で足す）:
  - `FACTORS 0x04`: 点の後に係数の列の数 k（u8、筆・鉛筆 1、水彩 3）と、点ごとに k 個の u16。
  - `STEPS 0x08`: 点ごとに u8 の濃さの段（鉛筆）。
  - `CONTOURS 0x10`（塗り）: 点の数の後に輪郭の数（u16）と輪郭ごとの点の数（u32）。塗りの点は筆圧 0・傾きなし・**時刻なし**（塗りだけ `TIME` を立てない、L10）。
- journal: 版 3（stroke の record は同じ符号化で新しい flags を持つ）。新しい Notes は版 2 と 3 の journal を復旧する。版 2 の journal を復旧したら、復旧した文書の snapshot から版 3 の journal を作り直してから新しい record を足す（header と中身を食い違わせない、M8）。

## 6. 画面の描画の変更（GPU だけ）

### 6.1 pipeline と shader（C2）

- 今の 5 つ（STENCIL・FRINGE・COVER・PLAIN・TEXTURE）は今どおり初期化で作る。
- 新しい 4 つ（FRINGE_GRAIN・COVER_GRAIN・FRINGE_MULTIPLY・COVER_MULTIPLY）は**最初に使う frame で作る**（遅延）。作るのに失敗したら、その組を「使えない」と覚えて一度だけ `NOTES PIPE fallback name=grain|multiply error=E` を log に出し、以後は代わりで描く: GRAIN → SOLID（粒なし、tile の平均 h̄ で `α = 1 − (1 − a·g_min)(1 − a·h̄)` の単色）、MULTIPLY → NORMAL の近似（§3.3）。Notes は終わらない。ペンだけを使う人には何も起きない（新しい pipeline は作られない）。
- Multiply: 色の blend は `src × ZERO + dst × SRC_COLOR`、alpha は `src × ZERO + dst × ONE`（dst のまま）。shader は色を `1 − a + a·cs`（a = alpha × coverage、L2）で出す。頁の絵は不透明の白から始まるので、PDF の `Cb × (1 − ca + ca·Cs)` と同じ式。UNORM の値の上の blend なので PDF の DeviceRGB と一致する。
- GRAIN: 新しい vertex shader（`grain.vert`）が頁の座標から粒の uv を varying で出す。push constant を 32 byte（vec4 2 つ: window の大きさ、粒の原点と texel/px）にし、stage は VERTEX のまま（全部の pipeline が同じ layout、L5）。fragment は今の fill の coverage に `α = 1 − (1 − a·g_min)(1 − a·h)` を掛ける。粒の texture は今の descriptor の形（combined image sampler 1 つ）で、REPEAT の新しい sampler と組の descriptor set を別に持つ（layout は変えない）。
- shader は今の fill と texture の範囲の命令だけ使う（i915 の native compiler の制約）。SPIR-V は `shaders/regenerate.py` で作り直す。
- 複数の輪郭: `notes_frame_polygon()` を輪郭の列に広げる（全ての fan → 全ての辺の fringe → 1 回の cover）。`seam_*` の辺には fringe を出さない。

### 6.2 頁の絵との関係

- 新しい道具の stroke も「上に足す」で頁の絵に足せる。塗りの途中への挿入は今どおり全部の描き直し。
- 描いている途中の stroke は window の frame で頁の絵の上に描く（Multiply の dst は頁の絵の画素）。

## 7. 道具の欄の UI（D3 (a)）

- 今の toolbar はすでに約 1100 px あり、狭い window・縦長（WS217）・Pi の小さな画面で右がはみ出す。4 つの button を足すと悪くなる。
- **描く道具を 1 つの button にまとめる**（新しい action `NOTES_ACTION_DRAW 48`）。button は今の描く道具の名前（Pen・Brush・Pencil・Water・Marker・Fill）を出す。
  - 他の道具（消しゴム・Select・Text）の時に押すと、最後に使った描く道具に戻る。
  - 描く道具の時に押すと、帯（glass の小さな card、6 つの button）を開く。帯の button の action は Pen `NOTES_ACTION_PEN 1`・Marker `NOTES_ACTION_HIGHLIGHTER 2`（今の番号のまま）・Brush 49・Pencil 50・Watercolor 51・Fill 52。
  - 帯は toolbar の絵の notice の行（44 px）に描く。帯が開いている間は notice を出さず、閉じた時にまだ時間が残っていれば出す（帯が優先、L7）。選ぶ・帯の外を押す・ペンを頁に下ろすと閉じる（閉じるための下ろしでは描かない）。
  - Marker も帯に入れる → toolbar の button は今より 1 つ減る。Pen ↔ Marker の切り替えは 1 回から 2 回の押しになる（key の P・M は 1 回）。
  - タッチ・ペン・mouse の全部で同じ（帯の矩形を toolbar の当たりに足す）。
- `NOTES_BUTTONS` を 32 から 48 にし、`ui_add_button()` は上限を超えたら捨てる前に一度 `NOTES BUTTONS overflow action=N` を log に出す（M6）。
- `NOTES BUTTONS` の log に帯の button も出す。`NOTES TOOL` の log に新しい名前（brush・pencil・watercolor・fill）。
- 色と太さ: 道具ごとに表（色の index・幅の index は今どおり道具の間で共有）。

| 道具 | 色 | 幅（pt） |
| --- | --- | --- |
| 筆 | ペンの 5 色 | {3, 6, 12} |
| 鉛筆 | ペンの 5 色 | {0.8, 1.4, 2.4} |
| 水彩 | ペンの 5 色（alpha は §3.3 の形の値） | {8, 16, 28} |
| 塗りつぶし | ペンの 5 色（不透明） | 幅の欄を出さない |

- System Menu の Tool に Brush・Pencil・Watercolor・Fill の radio。key（Control なし）: B 筆、L 鉛筆、W 水彩、F 塗りつぶし。
- 塗りつぶしの操作: pen・mouse は押して離すまで 8 px 以内なら押した点で塗る（それ以上動いたら何もしない）。指は Finger が on の時だけ、同じ条件の tap で塗る。**塗りつぶしの道具で Finger が on の間は double tap の拡大を止める**（pinch の拡大・2 本の指の scroll は今どおり、M11）。Finger が off なら指は今どおり頁を動かす。
- 既存の AAT の helper（`plan/tools/aat/scenarios/helpers_notes_edit.py`、Pen を押す所は DRAW を押して帯の Pen を押す）と `tests/scenarios/apps/notes/` の scenario を p006 で追従させる（path は Q1 が p006 の担当に割り当てる）。
- beta2 の利用の手引き（`docs/release/zedbsd-1.0.0-beta2-guide.md`）の Notes の節は、実装の後に Q1 か WS129 の担当が直す。

## 8. 段の分け方と受け入れ・試験

見積の単位は LW（目安）。試験の方針（AGENTS.md 2026-10-03）: 担当は build（warning 0）と変えた所の短い host 試験まで。QEMU は T1、実機 5330 のペンはユーザー、Pi 4 は p009。build の warning 0 は amd64（`config/current-uat.mk`）と rpi4（`config/ci/config-rpi4.mk`）の両方（L6）。試験の image は `plan/ws216/tests/` に config.mk と個別の file の複写だけで作る（AGENTS.md 2026-10-04）。

| Phase | 内容 | 依存 | 受け入れ | 試験 | 見積 |
| --- | --- | --- | --- | --- | --- |
| p002 | libpdf: 係数の輪郭（style・範囲・gentle な境）、writer（ExtGState の (ca, BM)、`q/Q` の状態、tiling pattern、content の Flate）、reader（Flate の content の hash、tiling pattern） | p001 | ペンの輪郭が今と bit 同一。範囲の輪郭の境の角が隣と同じ値、区間の面積の和が全体と一致。`q/Q` の後の同じ alpha の fill が正しい `gs` を持つ。書いた PDF を `qpdf --check` が通し、`pdftoppm`・Ghostscript・libpdf の raster が互いに近い（画素の差の閾値）。Flate の content の hash が圧縮前の bytes の hash と一致。鉛筆 200 本相当の pattern の頁で `PDF_DISPLAY_LIMITED` が立たず、raster の時間を記録。`pdftoppm -r 600` の継ぎ目の線を数えて記録 | host（plain・ASan・UBSan）: `plan/ws216/tests/host-pdf-tools.c`（WS079 の `host-pdf-outline.c`・`host-pdf-writer.c` の形を手本に） | 3.5 |
| p003 | Notes の model と形式: 道具の種類 2〜5、shape の列、筆・鉛筆・水彩の係数の式と焼き込み、鉛筆の区間、ZNOT 3.0（flags・`TOOL` の鍵・`major >= 2`）、journal 3（2 の復旧と作り直し）、undo・部分消し（焼き込んだ値の写し）、`save.c` の書き出し（pattern・Multiply・major 3 の Flate）、塗りの stroke の encode/decode（作るのは p005） | p002 | encode/decode の往復で model が一致（新しい flags・焼き込んだ値・輪郭）。1.1・2.0 の file を今と同じに読み、今と同じ bytes で書く。版の選び方（1.1/2.0/3.0）。2 の reader の経路は 3 を拒む。知らない定数の版は「他の PDF」。journal の 2 と 3 の復旧。**部分消しで、消しゴムの円 + 幅の半分 + 点 2 つ分の外の輪郭の角が前と同じ（許容差 0.01 pt）**（筆・鉛筆・水彩、S2）。今の Notes の保存の経路の模型で 3.0 の file を base にした時の振る舞い（M9）。major 3 の保存 → 開くで `NOTES_OPENED_NOTES`。粒の向き（PDF の画素と画面の式の比較） | host: `plan/ws216/tests/host-notes-tools.c`（`plan/ws079/tests/host-notes.c` の形）。保存した PDF を `pdftoppm` で描き目視の PNG | 3.5 |
| p004 | 画面: 頂点の塊、遅延の pipeline と代わり、粒の texture 3 段（RGBA8）、`grain.vert`、Multiply、複数の輪郭、継ぎ目の fringe なし、push constant 32 byte | p003 | build（amd64・rpi4）warning 0。SPIR-V を作り直せる。粒の tile の level 0 の bytes が PDF の SMask と同じ（hash）。**16 MiB を超える確保を断る device の模型で、60 万頂点の frame が塊に分かれ、draw が三角形の境で分かれ、頂点の数が保たれる**（S3）。pipeline の作成が失敗する模型で代わりに切り替わり、初期化が失敗しない（C2） | host: `plan/ws216/tests/host-notes-render.c`（geometry と塊の分け方を直に link。device の模型は `plan/ws079/tests/host-notes-zoom-limit.c` の形）。画面は p007 で T1 | 2.5 |
| p005 | 領域の塗りつぶし: 帯の画面外の描画と読み戻し、境・隙間を閉じる・flood fill、輪郭化と上限、z の位置、undo・journal、消しゴムの規則（D7 (c)） | p003、p004 | 合成の raster（閉じた円、1 pt の隙間の円、穴のある形、文字の列、粒の鉛筆の円、水彩の円、写真の模型、囲みの無い頁、頁の角の三角）で期待の領域と z の位置。上限を超える形で notice。消しゴムの操作の規則（線から始めた操作は塗りを消さない、塗りから始めた操作は消す）。2048 px 角の flood fill と輪郭化の時間を host で記録 | host: `plan/ws216/tests/host-fill.c`（flood・膨張・marching squares・単純化・消しゴムの判定を Notes の file から直に link） | 3 |
| p006 | UI: 描く道具の button と帯、action の番号、`NOTES_BUTTONS` 48 と overflow の log、帯と notice、Tool menu、key、色と太さの表、pen・mouse・指での選択と塗り（double tap の停止）、AAT の helper と scenario の追従 | p004、p005 | build warning 0。`NOTES BUTTONS` の log に帯の button が出る。狭い window（1080 px）でも帯が押せる。塗りの道具・Finger on で double tap が拡大しない | host: `plan/ws216/tests/host-notes-fill-touch.c`（`touch.c` を直に link。WS081 の試験は変えない） | 1.5 |
| p007 | T1 の QEMU（AAT の scenario を 1 つの起動で）、実機 5330 のペン（ユーザー） | p002〜p006 | QEMU: 各道具で描く（`peninject` の筆圧 0→最大）→ 画面の PNG、undo/redo、部分消し、保存 → 開き直しで同じ画面、PDF Viewer で開いて近い見た目、1.1・2.0 の file の読み、塗りの帯の log。5330: 筆の入り抜き・鉛筆の濃さ・水彩の重ね・塗りの見た目（ユーザーの目）、`NOTES FRAME vertices=` の最大、v_ref などの定数の調え | AAT: `tests/scenarios/apps/notes/draw-tools.md`（新、道具ごとの節）と `fill-region.md`（新）。T1-xxx | 2 |
| p008 | 規約の全文の見直し | p007 | WS の全ての C の変更を `plan/coding-style.md` の全文で読む。p009 で code が変われば、その範囲をやり直す | conformance | 1 |
| p009 | Pi 4 の確かめ | p007、WS141 p017、WS141 の Multiply の blend の依頼（§9） | Pi で Notes が起動し、`NOTES PIPE fallback` が出ない（grain・multiply とも作れる）。各道具で描ける。描いている途中の frame の時間・頁の描き直しの時間・塗りの時間（数）。粒の tile の hash が amd64 と同じ（`NOTES GRAIN hash=`）。要れば §4 の手当てを入れる | T1（Pi の実機の手順）とユーザーの目 | 1 |

計 約 18 LW。

- p004〜p006 は同じ `main.c`・`render.c` を触るので同じ担当が順に行う。
- p009 は WS141 を待つ。WS216 は p009 が cleared になるまで completed にならない（D10）。p002〜p008 はそれを待たずに cleared にできる。

## 9. 依存と衝突

| 相手 | 関係 |
| --- | --- |
| BUG-296 | merge 済み。依存ではない。§4.2・§3.4 は同じ「断られたら小さくする」の考えを頂点と帯に使う |
| WS141 p017（bcm2711 の stencil） | Notes は今 Pi で起動しない（`pDepthStencilState`・`colorWriteMask 0` を driver が断る）。p009 の前提 |
| **WS141 への新しい依頼**（Q1 が WS141 に Phase を立てる） | bcm2711 の `blend_state()` が Multiply の係数を受ける: 色 `srcColor = ZERO`・`dstColor = SRC_COLOR`・`ADD`、alpha `ZERO`・`ONE`・`ADD`。kernel の driver で HAL の API ではない。V3D の blend は GL の係数を持つので、driver の受け入れと CL への落とし込みの範囲。p009 の前提。それまで Pi の水彩は Normal の近似（§3.3） |
| WS210 p008（Notes の overlay を GPU へ） | `render.c` を触る可能性。WS216 の p004・p005 と同じ時期に重ねない（順は Q1） |
| WS217（画面の回転、縦長） | 依存ではない。§7 の帯は縦長の幅（約 1080 px 以下）でも押せることを目標にする |
| WS079・libpdf | libpdf（`userland/base/libpdf/`、`include/libc/pdf.h`、`exports.map`）は WS079 の library。p002 の担当に該当の file を割り当てることを Q1 に頼む。`design-input-notes.md` の種類の一覧へのリンクも Q1 に頼む。HAL・toolchain には触れない |
| WS081（Notes の指） | `touch.c` の double tap の停止は p006 で WS216 の範囲として直し、試験は `plan/ws216/tests/` に置く。WS081 の `host-notestouch.c` は変えない |
| AAT（`plan/tools/aat/`、`tests/scenarios/apps/notes/`） | p006 の担当に path を割り当てることを Q1 に頼む |
| Guardrail「画像の処理は GPU で」 | 塗りつぶしの読み戻しだけが CPU の画素の処理（tap の一度、D5 のユーザーの決定）。粒の tile は起動の時の一度の用意 |

## 10. 決定（D1〜D12）

2026-10-13 ユーザー「ではあとは推奨でお願いします」→ 全部推奨で決まった。D7・D8 は review-1 を受けて変えた推奨、D10〜D12 は設計 2 で設計者（P3）が review-1 の材料から決めた推奨を採った。

| # | 問い | 決定 | 理由 |
| --- | --- | --- | --- |
| D1 | PDF への出し方 | (a) vector の近似（形は polygon、alpha・Multiply、鉛筆の粒だけ小さな共有の画像の pattern） | 拡大・印刷で粗くならない、file が小さい、画面と同じ作り。水彩の縁は不規則な縁の重ねの近似 |
| D2 | 鉛筆の筆圧 → 濃さ | (a) 8 段の濃さの区間 | libpdf の reader は Gouraud の shading を描かないので連続の濃さは PDF で表しにくい。境は gentle な頂点（M2） |
| D3 | 道具の欄 | (a) 描く道具を 1 つの button と帯にまとめる（Marker も帯へ） | 狭い window ではみ出さない。Pen ↔ Marker が 2 回の押しになる、AAT の追従が要る（§7） |
| D4 | 水彩の滲み | (a) 揺らぐ縁を 2 枚重ねた静的な形 | 時間で変わる形は保存と undo の扱いが増える（将来の案） |
| D5 | 塗りの領域の決め方 | (a) 見えている絵の色（tap の一度の読み戻し、Guardrail の「内容が変わった時の一度だけの用意」として扱う） | 閉じた線の内側と同じ色の続く所を一つの規則で満たし、背景の PDF の上でも見たとおりに塗れる。倍率は表示に依らない決まった倍率（S5） |
| D6 | 塗りの重なりの順 | (a) tap した所に見えている物の直上（紙なら一番下） | 線の囲みを塗ると線の下に入り、塗り替えはその上に入る。半透明の物の上でも直上 |
| D7 | 消しゴムと塗り | (c) 見えている所に触れたらその物を消す（一回の操作の最初の当たりで決める、§3.5） | (a) は線を消すと塗りも消え、塗りだけを消せない（S4） |
| D8 | file の大きさ | Flate は major 3 の file だけ。reader は decode してから hash を取る（試験つき） | 1.1・2.0 を Flate にすると今の Notes が全頁を CHANGED として開く（C1） |
| D9 | 古い Notes との互換 | (a) 新しい道具のある文書は major 3（今の Notes は「他の PDF」として見た目を保って開く） | 1.x のままだと古い Notes が新しい道具をペンの線として描き、保存でその見た目に書き直す。今の Notes で保存した後は新しい道具の stroke が編集できなくなる（M9、§5.4） |
| D10 | 受け入れに Pi 4 を含めるか | **含める**。Pi の確かめは別の段 p009（WS141 p017 と Multiply の blend の依頼の後）にし、p002〜p008 は待たない。WS216 は p009 まで completed にならない | Guardrail「5330 で速くても Pi 4 で確かめる」はユーザーの規則で、WS の中で緩めない。Pi で Notes を壊さないこと（遅延の pipeline と代わり）は p004 の host 試験で先に確かめる |
| D11 | 鉛筆の粒の PDF の形 | **(b) tiling pattern**（libpdf の reader に tiling pattern を足す）に、**単色の下塗り**を組み合わせる（§3.2・§5.2） | (a) tile の `Do` の列は reader の画素の上限で頁の残りが欠け（S1）、viewer で格子の継ぎ目が出やすい（M5）。(b) は file が小さく clip も要らない。下塗りがあるので、pattern を飛ばす古い reader でも鉛筆は消えず薄い単色の線になる。(c) 粒なしはユーザーの「紙の粒のような質感」を満たさない |
| D12 | 囲まれていない所を塗った時 | **(a) 頁の縁を境として、つながった所を塗る**（頁全体でも）。形が上限（輪郭の点 8192）を超え、単純化を強めても収まらない時は塗らずに notice | WS の目的の「同じ色の続く所」と一般の塗りの道具の振る舞いに合う。頁の角を線で区切った所も同じ規則で塗れる（(b)・(c) は「囲まれていない」の判定に頁の縁を使えず、角の所を誤る）。爆発は上限と notice で止め、頂点は §4.2 の塊で i915 の上限に当たらない |

設計者が決めた点（問いにしない）: 各道具の数（幅・alpha・段・揺らぎ・v_ref・g_min）は p007 で 5330 で見て調える初期値。係数は pen を離した時に焼き込む。key は B・L・W・F。新しい action の番号は 48〜52。

## 11. review-1 の指摘への対応

| # | 指摘 | 対応 | 所 |
| --- | --- | --- | --- |
| C1 | Flate で content hash が `ENOTSUP` | 直した。Flate は major 3 だけ（D8）。reader は Flate を decode してから hash（上限 64 MiB）。試験 3 つ。§5.3 の誤った文を直した | §5.2・§5.3、p002・p003 |
| C2 | Pi は Multiply の blend と R8 を受けない、初期化ごと失敗 | 直した。新しい pipeline は遅延で作り、失敗したら代わりで描いて log（Notes は終わらない）。粒は `B8G8R8A8_UNORM`・mip 1 段・別々の 3 つの texture。WS141 への新しい依頼（blend の係数）と p017 を依存に書き、Pi の確かめを p009 にした（D10） | §3.2・§3.3・§6.1・§9、p004・p009 |
| S1 | tile の `Do` の列で reader の頁が欠け、遅い | 直した。tiling pattern と下塗り（D11）。reader に pattern を足し、cell は一度だけ描いて数える。clip も `Do` の列も無い。試験に 200 本の頁 | §5.2、p002 |
| S2 | 部分消しで消していない所の形が変わる | 直した。係数（筆・水彩）と段（鉛筆）を pen を離した時に焼き込み、切れ端は元の値を写す。形は局所的な輪郭と焼き込んだ値だけで決まる。試験（許容差 0.01 pt） | §2・§3.1〜§3.3・§3.5・§5.4、p003 |
| S3 | 描き直しの vertex buffer が 16 MiB を超える | 直した。4 MiB 以下の塊の列、draw は三角形の境で分ける、断られたら半分。i915 の模型の host 試験。5330 で最大の頂点の数を測る | §4.2、p004・p007 |
| S4 | D7 (a) は線を消すと塗りも消える | 直した。D7 (c)、一回の操作の最初の当たりで決める。塗りの当たりは輪郭の内側だけ（輪郭の間に偽の線分を作らない） | §3.5、p005 |
| S5 | 塗りの精度が倍率に依る、1 px 潜らせてはみ出す、読み戻しが 16 MiB を超える | 直した。決まった倍率（≤ 4 px/pt、≤ 16 Mi px）の帯（≤ 4 MiB）の画面外の描画、遅延の帯、断られたら半分、潜らせは pt（0.1 pt）。第 1 版 §4.4（今は §4.5）の数を直した | §3.4・§4.5、p005 |
| M1 | writer の `q/Q` と ExtGState の鍵 | 直した。鍵 (ca, BM)、`q/Q` で状態を積む、Normal に戻す ExtGState、試験 | §5.2、p002 |
| M2 | 鉛筆の区間の境の角の重なり・隙間 | 直した。境は gentle な頂点へずらし middle の法線で切る。面積の和の試験 | §3.2・§5.1、p002 |
| M3 | 係数が libpdf の clamp で 1 に切れる、dt = 0 | 直した。style に係数の上限 2.0（clamp しない）。速さは ±8 ms の窓で、同じ時刻だけなら v = 0 | §3.1・§5.1 |
| M4 | 粒の周期性・縮小・format | 直した。整数の hash で 256 で wrap、3 段の縮小を起動の時に作り倍率で選ぶ（Pi は mip 1 段なので別の texture）、RGBA8、整数だけなので amd64 と arm64 で同じ bytes（p009 で hash を比べる） | §3.2・§6.1、p004・p009 |
| M5 | 他の viewer の tile の継ぎ目 | 直した。pattern にした（D11）。`pdftoppm -r 600` の継ぎ目の線を数え、残れば制限として記録。粒の SMask は色の間で共有 | §5.2、p002 |
| M6 | AAT の helper と button の上限 | 直した。新しい action `DRAW 48`（Pen・Marker は帯で今の番号）、helper と scenario の追従を p006 に、`NOTES_BUTTONS` 48 と overflow の log。Pen ↔ Marker が 2 回の押しになることを D3 に書いた | §7・§9、p006 |
| M7 | 塗りの領域の爆発 | 直した。1 pt² 以下の島と穴を捨てる、点の上限 8192 と単純化の段、超えたら notice。試験に粒の鉛筆・水彩・写真 | §3.4、p005 |
| M8 | 3.0 の細部（`TOOL` の鍵、IMAG・EDIT、journal 2 → 3） | 直した。鍵に定数の版、`major >= 2` で読む、2 の復旧の後に 3 で作り直す、知らない定数の版は「他の PDF」 | §5.4、p003 |
| M9 | 古い Notes の保存で新しい道具の編集 data が使われなくなる | 書いた（source で確かめた: 今の Notes は 3.0 の file を base にして自分の revision を足す）。D9 の説明に入れ、p003 で模型の試験。base の中の 3.0 の編集 data を読み直す案は Future Work の候補として Q1 へ | §1.4・§5.4・§10 |
| M10 | 依存の表が古い | 直した。BUG-296 を依存から外した、WS141（p017 と新しい依頼）を足した、p002 に reader を足し見積を 3.5 に、p005 の `render.c` と WS210 p008 の重なり、塗りの消しゴムを p005 へ | §8・§9、ws.md |
| M11 | 指の塗りと double tap | 直した。塗りの道具で Finger が on の間は double tap の拡大を止める。試験は `plan/ws216/tests/` | §7、p006 |
| L1 | 行番号のずれ | 直した。§1 を main `a9c627d6b` で取り直した | §1 |
| L2 | Multiply の式の記法 | 直した。`1 − a + a·cs`、alpha は `ZERO, ONE` | §6.1 |
| L3 | 頁の絵に `TRANSFER_SRC` が無い | 頁の絵の usage は変えない。読み戻すのは帯の画面外の image だけ（`TRANSFER_SRC` つき） | §3.4 |
| L4 | BGRA・RGBA | 帯の image は頁の絵と同じ format、flood は format で順を見分ける | §3.4 |
| L5 | push constant の stage | VERTEX のまま 32 byte、粒の uv は varying | §6.1 |
| L6 | build の warning 0 の形 | amd64 と rpi4 の両方と書いた | §8 |
| L7 | 帯と notice の行の競合 | 帯が優先、notice は閉じた後に出す | §7 |
| L8 | 描いている途中の抜きの見え方 | 書いた | §3.1 |
| L9 | `pdf.h`・`exports.map` | §5.1・§9 に書いた | §5.1・§9 |
| L10 | 塗りの点の時刻 | 塗りだけ `TIME` を立てない、と書いた | §5.4 |

## 12. 残る問い（推奨つき）

ユーザーの判断は要らない（D1〜D12 で決まった）。Q1 に頼む事:

1. WS141 に「bcm2711 の blend の係数（Multiply）」の Phase を立てる（§9）。推奨: WS141 p017 と同じ担当・同じ時期（pipeline の state の decode を両方が触る）。
2. p002 の担当に libpdf（`userland/base/libpdf/`・`include/libc/pdf.h`・`exports.map`）を、p006 の担当に AAT の helper・`tests/scenarios/apps/notes/` を割り当てる。
3. `plan/ws079/design-input-notes.md` の道具の種類の一覧に §5.4 へのリンクを足す。
4. Future Work の候補「今の Notes が保存した 3.0 の file の base の中の編集 data を新しい Notes が読み直す」（M9）。推奨: 候補として記録し、利用者が古い image を使い続ける報告が来たら考える。

## 13. 残りと再開の条件

- review-2（Q1 が回す）→ 指摘の反映。
- p002 の Queue（Q1）。p002 は Notes の source に触れないので、すぐ始められる。
- 未実施: 実測は全て未実施（§4 の数は推測）。Pi の Notes は起動しない（WS141 p017）。
