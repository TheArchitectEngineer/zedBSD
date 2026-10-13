<!-- awesome-plan project=zedbsd record=ws216-p001-review-1 -->

# ws216-p001 設計 1 の敵対的 review（review 1）

- 対象: [phase.md](phase.md) 版 1（2026-10-13 P1）
- 照合した tree: main `bacb081f8`（設計の調べは `e162b0267`。その後 BUG-296 の直し `b26996be7` が main に入っている）
- review の担当: design-reviewer（2026-10-13）。source と設計文書は変えていない。build・試験・QEMU は走らせていない（読むだけ）
- 重大度: 致命的（このまま実装すると機能が壊れる・target で動かない）／重大（受け入れ条件・互換・速さを満たさない見込みが高い）／中（設計の穴、実装の段で事故になりやすい）／軽微

## 要約

| # | 重大度 | 指摘 |
| --- | --- | --- |
| C1 | 致命的 | D8（content の Flate）で、今の reader の content hash が `ENOTSUP` になり、保存した頁が全部「他の program が変えた」扱いになる。§5.3 の「hash の比べ方は変わらない」は source と違う |
| C2 | 致命的 | Pi 4 の bcm2711 の Vulkan は blend を premultiplied の source-over だけ受け、sample する image も RGBA8/BGRA8 だけ。Multiply の pipeline と R8 の粒の texture は作れず、Notes の pipeline の初期化ごと失敗する。WS141 への依存が抜けている |
| S1 | 重大 | 鉛筆の粒を 48 pt の tile の `Do` で並べると、libpdf の reader が `Do` ごとに画像を decode し直し、display list の画素の上限（64 Mi px、tile 約 1024 個）で頁の残りが描かれなくなる。clip ごとに頁全体の mask を `calloc` するので遅い |
| S2 | 重大 | 部分消しで切れ端を作ると、筆・水彩・鉛筆の**消していない所の形**が変わる（弧長・平滑・揺らぎの seed・段の履歴が切れ端の始まりと新しい id から決まり直す） |
| S3 | 重大 | 頁の描き直しの頂点は 1 つの vertex buffer（i915 では 1 allocation 16 MiB、約 52 万頂点）に入る。水彩（形 3 つ）と塗り（穴の多い polygon）で超えやすく、BUG-296 と同じく `vkAllocateMemory` が断って 5330 で Notes が終わる（推測、数は未測定） |
| S4 | 重大 | D7 (a)「塗りの縁に触れたら塗り全体を消す」は、塗りの縁が設計上いつも線の下にあるので、囲みの線を消すと塗りも消える。逆に塗りだけを消す所が見えない |
| S5 | 重大 | 塗りの領域を「今の倍率の頁の絵」から取るので、精度が tap した時の拡大に依る。「線の下へ 1 px 潜らせる」は px の規則で、筆圧の軽いペン（0.225 pt）・鉛筆の線より太く、外へはみ出す。読み戻しの「最大約 24 MB」は i915 の 16 MiB を超える |
| M1〜M11 | 中 | writer の `q/Q` と ExtGState の状態の追跡、鉛筆の継ぎ目の角、筆の係数が libpdf の clamp で切れる、粒の周期性と mipmap、PDF の tile の継ぎ目、AAT の helper の破損、塗りの多角形の爆発、`TOOL` の重複の鍵、古い Notes の保存で編集 data が失われる、依存の表の古さ、指の塗りと double tap |
| L1〜L10 | 軽微 | 行番号のずれ、式の記法、usage flag、BGRA/RGBA、notice の行の競合など |

D1〜D9 の推奨の評価は §4。足すべき問い（D10〜D12）は §5。

## 1. 致命的

### C1. D8 の Flate で content hash が取れず、保存した notebook が開き直すと編集できなくなる

- 根拠:
  - `userland/base/libpdf/reader.c:511-575` `pdf_document_page_content_hash()` は各 content stream に `check_unfiltered()` を掛ける。`reader.c:3697-3717` `check_unfiltered()` は `/Filter` があれば `ENOTSUP` を返す（関数の注釈は「decoded」と書くが、実装は decode しない）。
  - `userland/desktop/notes/save.c:1120-1154` `page_unchanged()`: hash が取れない頁は「変わった」（注釈「a page the reader cannot hash (for example compressed by another program) has changed」）。
  - `plan/ws079/design-pdf.md:150`「filter つきは `ENOTSUP`（p006 で Flate）」— 予定であって、今の tree は `ENOTSUP` のまま。
  - `libpdf/writer.c:1911-1915` は content を `/Length` だけで書く（無圧縮）。
- 起こる状況: p002 が §5.3 の「hash は decode した bytes なので、hash の比べ方は変わらない」を信じて writer だけを Flate にすると、**新しい Notes 自身**が自分の保存した file を開くたびに全頁を CHANGED と判定し、stroke が背景になって編集できなくなる。reader 側を直しても、**今の Notes（beta の image）**は新しい Notes が保存した 1.1・2.0 の file（新しい道具の無い、ペンだけの notebook も含む）の全頁を CHANGED として開く。D9 が守ろうとする「1.1 の file は今の Notes でも今どおり」が D8 で壊れる。
- 直し方（どれか）:
  1. Flate は major 3 の file（新しい道具がある時）だけにする。1.1・2.0 は今どおり無圧縮。D8 の選択肢をこの形に書き直す。
  2. p002 の範囲に「`pdf_document_page_content_hash()` が Flate を decode してから hash する（decode 後の大きさの上限つき）」を足し、受け入れ試験に「Flate の content の file を保存 → 開く → `NOTES_OPENED_NOTES`」と「今の reader の模型（filter を `ENOTSUP`）で 1.1 の file が CHANGED にならない」を足す。
  3. §5.3 の文を「今の reader は filter つきの content の hash を `ENOTSUP` で断る（`reader.c:3715`）。p002 で decode して hash するよう直す。今の Notes は Flate の頁を変更として扱う」に直す。

### C2. Pi 4 の driver は Multiply の blend と R8 の texture を受けない

- 根拠:
  - `src/drivers/gpu/bcm2711/vulkan-pipeline-state.c:211-243` `blend_state()`: blend が有効なら `srcColor=ONE`・`dstColor=ONE_MINUS_SRC_ALPHA`・`ADD`（alpha も同じ）でないと `ENOTSUP`。
  - Notes 自身も `userland/desktop/notes/render.c:1930-1934` の注釈で「this is the only blend the Raspberry Pi 4's driver takes」と書いている。
  - sample する image の format は R8G8B8A8・B8G8R8A8 の UNORM だけ（`bcm2711/vulkan-native-draw.c:376-377,1022`、`vulkan-record-validate.c:324`）。R8_UNORM の扱いは bcm2711 の source に見つからなかった。
  - `render.c:187` は初期化で全部の pipeline を作る（`render_pipelines`）。1 つ失敗すれば renderer の初期化が失敗する。
- 起こる状況: §6.1 の `FRINGE_MULTIPLY`・`COVER_MULTIPLY`（`ZERO, SRC_COLOR`）は Pi で `vkCreateGraphicsPipelines` が失敗し、WS141 p017 で stencil が直っても、**ペンだけを使う人も含めて Notes が Pi で起動しない**。粒の texture を R8 で作ると同じく失敗する。§4.3「Multiply は blend の係数の違いだけ」は i915・Venus の話で、Pi では成り立たない。source-over（`ONE, ONE_MINUS_SRC_ALPHA`）では色ごとの掛け算（`dst × cs`）を表せない（alpha は 1 つの値なので灰色の掛け算にしかならない）。
- 直し方:
  1. §9 に「WS141（bcm2711 の kernel の driver）: blend の係数 `ZERO`・`SRC_COLOR`（Multiply）の受け入れ」を依存として足し、WS141 の担当に頼む項目を書く。HAL の API ではないが kernel の driver で、WS141 の担当（P3）の範囲。
  2. Notes は pipeline を作る前に作れるかを見て、作れない device では水彩の pipeline を作らない（水彩を NORMAL の近似で描く、か水彩を出さない）。初期化の失敗で Notes ごと落とさない。
  3. 粒の texture は RGBA8（4 channel に同じ値）にするか、`vkGetPhysicalDeviceFormatProperties` で `SAMPLED_IMAGE` を確かめて選ぶ。
  4. 受け入れに「Pi の pipeline の作成が通る（WS141 p017 の後）」を入れる。Pi の判断は D10（§5）。

## 2. 重大

### S1. 粒の tile を `Do` で並べる PDF は libpdf の reader で頁が切れ、遅い

- 根拠:
  - `libpdf/content.c:3666-3717` `draw_image()`: `Do` のたびに `pdf_image_decode()` で画素を作り、`pdf_display_add_image()` に足す。同じ object の decode を使い回す仕組みは見つからなかった。
  - `libpdf/display.c:195` と `libpdf/internal.h:148` `PDF_DISPLAY_PIXELS_MAX` = 64 Mi px。256×256 の tile は 65,536 px なので、1 頁の `Do` は約 1024 回で上限になり、以後は `PDF_DISPLAY_LIMITED`（頁の残りが描かれない、`content.c:28-32`）。
  - `libpdf/raster.c:580-643` `push_clip()`: clip のたびに raster 全体の大きさの mask を `calloc`。鉛筆の区間ごとに 1 回。
  - `raster.c:1140-1210`: 画像の各画素で最大 4×4 の sample。
- 起こる状況: §5.2 の見積もり（1 本で数個〜数十、斜めで数百）で、鉛筆を数十本書いた頁は zedBSD の PDF Viewer と「他の PDF」の背景（今の Notes が 3.0 を開いた時、D9 が頼る経路）で下の方が欠ける。描ける頁でも、区間の数 × 頁の大きさの memset と tile の sample で、Pi の CPU では数秒〜数十秒の見込み（推測、未測定）。§5.2 は「libpdf の reader も今のまま描く」としか書いていない。
- 直し方（p002 で選ぶ。D11 として問う）:
  1. tiling pattern（PaintType 1、cell は粒の画像 1 枚）を fill の色にし（`/Pattern cs /GrainK scn` → 輪郭の `f`）、libpdf の reader に tiling pattern を足す（cell を一度描いて敷き詰める）。clip も `Do` の列も要らず、file も小さい。古い zedBSD の reader では鉛筆が消える（今の設計がこれを避けた理由）が、古い Notes は 3.0 をどのみち「他の PDF」として開くので、影響は古い PDF Viewer だけ。
  2. tile のままにするなら、p002 に reader の画像の decode の cache（object 番号で 1 回、display list で画素を共有）と、clip の mask を path の箱の大きさだけにする直しを足す。受け入れ試験に「鉛筆 200 本の頁で `PDF_DISPLAY_LIMITED` が立たない」「その頁の raster の時間（Pi 相当）」を足す。
- 他の viewer の tile の継ぎ目は M5。

### S2. 部分消しで、消していない所の筆・水彩・鉛筆の形が変わる

- 根拠: `document.c:703-806`（部分消し）、`document.c:1665-1690` `piece_add()`（切れ端は tool・色・幅だけを写した新しい stroke）、`document.c:1696-1735` `piece_close()`（時刻を切れ端の最初の点から数え直す）、`document.c:776-777`（切れ端は新しい id）。
- 起こる状況（設計の式による）:
  - 筆: 抜きは `min(4w, 0.3L)` の L（stroke の長さ）に依る（§3.1）。切れ端の L は短いので、`CUT_START` を付けても**切っていない側の端**の抜きの長さが変わる。速さの 30 ms の指数平滑も切れ端の始まりで初期化し直すので、切り口の近くの幅が変わる。
  - 水彩: 縁の揺らぎは「弧長の value noise、seed は stroke の id と縁の番号」（§3.3）。切れ端は新しい id と 0 からの弧長なので、**残った縁の形が全部変わる**。
  - 鉛筆: 段の切り替えは「2 pt 以上続いた時だけ」の履歴（§3.2）なので、切れ端の始まりが変わると区間の切れ目と濃さが変わりうる。
  - §5.4 の「形は保存した点 + id + 式の版だけから決まる」は保たれるが、部分消しの前後で同じ ink が同じ形であることは保たれない。消しゴムの円の外の ink が動いて見える。
- 直し方:
  1. 切れ端に「元の stroke の文脈」を持たせる: 元の id（揺らぎの seed）、元の弧長の始まりの位置、元の全長 L、切り口での平滑の値（または段）を stroke の field と編集 data（3.0 の stroke の任意 field）に持ち、式はそれを使う。
  2. か、新しい道具の切れ端は点ごとの幅の係数（と鉛筆の段）を焼き込んだ形で持つ（点の列に「係数」の列を足す flag）。
  3. p003 の受け入れに「筆・水彩・鉛筆を部分消しした時、消しゴムの円（+幅の半分）の外の輪郭の角が前と同じ（許容差 0.01 pt）」の host 試験を足す。

### S3. 頁の描き直しの vertex buffer が i915 の 16 MiB を超える

- 根拠:
  - `render.c:326-342`: 頁の frame と window の frame の頂点を 1 つの vertex buffer に入れ、足りなければ倍にして作り直す。`render.c:1690-1735` `render_vertices()`: host-visible の 1 allocation。上限の確かめは無い。
  - `geometry.c:380-414` `frame_reserve()`: 頂点の数に上限が無い。1 つの polygon は fan の `(n−2)×3` と fringe の `n×6` で約 9n 頂点（n は角の数）、1 頂点 32 byte（`app.h` `NOTES_VERTEX_FLOATS` 8）。
  - `src/drivers/gpu/i915/memory.h:87` `I915_MAX_RESOURCE_BYTES` 16 MiB、`userland/desktop/libvulkan/memory.c:1083` が超える確保を断る（BUG-296 の原因と同じ）。16 MiB は 524,288 頂点。
- 起こる状況（推測、数は未測定）: 水彩は 1 本で形 3 つ（角は芯の約 1.2〜1.4 倍が 2 つ）、長い wash の一筆で 10 万頂点の桁。塗りの「囲まれていない所は頁全体」（§3.4 の 6）は全部の線の周りに穴を持つ polygon になる。描き直し（頁の移動・zoom の終わり・塗りの挿入）で 52 万頂点を超えると、倍にした 32 MiB の確保が断られ、`notes_renderer_draw` が失敗して Notes が終わる（BUG-296 の前の振る舞い）。今のペンでも密な頁で近いかもしれない（推測）。
- 直し方: §4.2 に「頁の描き直しは頂点を 16 MiB 未満の塊に分けて複数の submit（または 1 つの command buffer の中で塊ごとに buffer を替える）」を足し、p004 の範囲と受け入れ（i915 の模型で、52 万頂点を超える頁の描き直しが確保の断りなしに通る host 試験。BUG-296 の `host-notes-zoom-limit.c` の device の模型を使える）に入れる。今のペンの頁の最大の頂点の数を 5330 で一度測る。

### S4. D7 (a) は線を消すと塗りも消え、塗りだけを消せない

- 根拠: §3.4 の 2（領域を線の下へ膨らませ戻す）と §3.5（縁に触れたら塗り全体を消す）。`document.c:617-689` `notes_document_erase_at()` は円が触れた stroke を**全部**消す（一番上だけではない）。`document.c:1368-1410` `stroke_touches()` は点の列の線分との距離。
- 起こる状況: 塗りの縁は設計上いつも囲みの線の下（1 px 内側〜線の中）にある。消しゴム（stroke の消しゴム）で囲みの線をなぞると、同じ円が塗りの縁にも触れるので塗りも消える。部分消しで線の一部を消すと（`erase_parts_at`）、§3.5 の規則で塗り全体が消える。逆に、塗りだけを消したい人は、線に隠れた縁を狙うしかない（線ごと消える）。「内側を消しゴムでなぞっても消えない」は利点として書かれているが、塗りを消す自然な操作が無くなる。
- 直し方: D7 に選択肢 (c)「見えている所で決める: 消しゴムの円の中心で一番上に見えているのが塗りなら塗り全体を消す。線が上にある所では線だけを消す」を足し、推しを (c) にする。`stroke_touches()` の塗りの判定は輪郭の内側（nonzero）の判定と z の順で行う。§3.5 の当たりの式（輪郭ごとの線分、閉じる辺を含み、輪郭の間をつながない）も明記する（今の `stroke_touches()` は点の列を一続きの線分として見るので、`CONTOURS` の輪郭の間に偽の線分ができる）。

### S5. 塗りの精度が tap の時の倍率に依り、細い線の外へはみ出す。読み戻しの大きさが 16 MiB を超えうる

- 根拠:
  - §3.4 の 1:「頁の絵を最新にし、GPU で長い辺 2048 px 以下に縮めて」。頁の絵は今の倍率で作られ（`main.c` `app_picture`、`render.c` `notes_renderer_page`、`NOTES_PICTURE_MAX` 4096）、BUG-296 の直しで 5330 では約 2.25 倍が上限（`plan/bugs/BUG-296.md`）。
  - §3.4 の 2:「同じ幅と AA の 1 px だけ膨らませ戻す（線の下に潜らせ）」。
  - ペンの最も細い線は 1.5 pt × 0.15 = 0.225 pt（`outline.c:57-61`、`ui.c` の幅 1.5）。鉛筆は 0.8 pt × 下限 0.6 = 0.48 pt（§3.2）。
  - §4.4「RGBA で最大約 24 MB」。i915 は 1 allocation 16 MiB（S3 の根拠）。
- 起こる状況:
  - 頁を全体表示（5330 で約 1.24 px/pt、推測）で tap すると 1 px ≈ 0.8 pt。「1 px 潜らせる」は 0.225〜0.5 pt の線を越えて**外へはみ出す**。縮小して tap すると更に粗い。拡大して見ると縁の段差（量子化 1 px + Douglas-Peucker 0.5 px ≈ 1.2 pt）が見える。同じ囲みでも tap した時の倍率で結果が変わり、試験で再現しにくい。
  - 正方形の頁（他の PDF）で 2048×2048×4 = 16 MiB ちょうど、横長・大きな頁で中間の image と host の buffer の 2 つの確保が要る。24 MB という数は上限を超える（A4 なら 2048×1448×4 ≈ 11.9 MB で、24 MB の根拠が不明）。
- 直し方:
  1. 塗りの領域の計算は、表示の倍率に依らない決まった倍率（例 4 px/pt）で頁を画面外の image に GPU で描き直して読む。16 MiB を超えないよう、tap の点を含む領域を帯（strip）に分けて描いて読む（flood fill が帯の端に届いたら次の帯）。
  2. 膨らませ戻しは pt で決め、「境の画素の線の幅の半分を超えない」に抑える（境の画素がどの stroke かは分からないので、少なくとも 0.1 pt 程度の固定値で試す）。
  3. 読み戻しの確保は BUG-296 の `app_picture` と同じく `VK_ERROR_OUT_OF_DEVICE_MEMORY` を受けて小さくする経路を持つ。§4.4 の数を直す。
  4. 頁の絵の image の usage に `TRANSFER_SRC` が無い（`render.c:1113`）。足すと i915 の配置が変わるかを確かめる（L3）。

## 3. 中

### M1. writer の状態（alpha・blend）を `q/Q` と ExtGState の鍵で追う

- 根拠: `libpdf/writer.c:191-249` `pdf_writer_set_fill_color()` は `writer->current_alpha` が同じなら `gs` を書かない。ExtGState は alpha だけで重複を除く（`writer.c:1552-1580` `find_or_add_alpha`）。
- 起こる状況: 鉛筆の区間（`q /GSn gs … Q`）の後、PDF の状態は `Q` で戻るのに writer は `current_alpha` を区間の値のまま持ち、次の同じ alpha の stroke で `gs` を省く → 不透明度が違う。水彩の `/BM /Multiply` の後のペンで Normal に戻す `gs` が要る。
- 直し方: §5.2 の writer に足す口に「`q/Q` で writer の (fill color, alpha, blend) を積んで戻す」「ExtGState の鍵は (ca, BM)」「Normal に戻す ExtGState」を明記し、p002 の host 試験に「`q/Q` の後の同じ alpha の fill が正しい `gs` を持つ」を足す。

### M2. 鉛筆の区間の平らな切り口と丸い join の角

- 根拠: `outline.c:20-29`（外側は corner の周りの弧、内側は corner の点そのものを通り、nonzero の和で塗る）、`outline.c:115-128`（gentle な turn だけが 1 つの middle の法線を持つ）。`geometry.c:244-276`（fringe は各辺の ±1 px の帯、端の延長なし、`render.c:1916-1921` で write mask 0 なので重なりは二度 blend）。
- 起こる状況: 区間の境が曲がり角の頂点にあると、前の区間の終わりの法線と次の区間の始まりの法線が違い、外側に扇の隙間か重なりができる。内側は台形が重なっているので、二つの区間の形が重なる所が二度塗られて濃い点になる（§3.2 の「隙間も重なりも無く」が成り立たない）。継ぎ目の両端では、側の辺の fringe の帯の端が隣の区間の中へ斜めに入りうる（細い濃い点、軽い）。
- 直し方: §5.1 の範囲の輪郭に「区間の境は gentle な頂点（middle の法線を持つ）だけに置き、切り口はその middle の法線の上。gentle でなければ次の gentle な頂点へずらす」を足す。p002 の受け入れの「境の角が隣と同じ値」に「二つの区間の polygon の面積の和が全体の polygon の面積と許容差で一致（重なり・隙間なし）」を足す。

### M3. 筆の係数が 1 を超え、libpdf の clamp で切れる。時刻の差 0

- 根拠: `outline.c:296-302` `keep_samples()` は筆圧を 0..1 に clamp。`outline.c:557-600` 補間も端の値の間に抑える。§3.1 の速さの項は `clamp(…, 0.55, 1.15)`。
- 起こる状況: F = 筆圧 × 速さ × 入り抜きは、ゆっくり描くと 1 を超え、1 に切られる（遅い時に太くなる効果が出ない）。`MAIN_SAMPLE_STEP`（0.25 px、`main.c` `app_sample`）の点は同じ `time_ms` を持ちうるので v = 距離 / 0 になる。
- 直し方: F を 1.15 で割ってから渡す（style の幅を 1.15 倍）か、style に「係数の上限」を持たせる。速さは同じ時刻の点をまとめた弧長の差で求め、dt = 0 を除く式にする。§3.1 に書く。

### M4. 粒の tile: 周期性、縮小、format

- 根拠: §3.2（256×256、48 pt ごと、5.3 px/pt、REPEAT）。`render.c:1441-1443` の今の sampler は CLAMP_TO_EDGE で mipmap 無し（`render.c:1113` の頁の絵も `mipLevels = 1`）。
- 起こる状況: value noise の格子が 256 で周期的でないと、画面（REPEAT）でも PDF（tile の並び）でも 48 pt ごとに継ぎ目が出る。画面は約 1.2 px/pt（Pi の全体表示、推測）で tile を約 4 分の 1 に縮めて引くので、mipmap が無いと粒が aliasing し、倍率で見た目が変わる（PDF の viewer は平均するので画面と PDF が違って見える）。R8 は C2。
- 直し方: 「noise の格子は 256 で wrap する（周期的）」「粒の texture は mipmap を GPU（`vkCmdBlitImage`）で一度作り、sampler は LINEAR_MIPMAP」を §3.2・§6.1 に足す。noise の生成は整数の hash だけで作り、amd64 と arm64 で同じ bytes（FMA の縮約の差を避ける）にし、p004 の host 試験の hash を両方の build で比べる。

### M5. 他の viewer で 48 pt の tile の継ぎ目が出る

- 根拠: §5.2 の鉛筆（tile ごとに `q cm Do Q`）。§5.2 の制限は区間の継ぎ目だけを挙げる。
- 起こる状況: poppler・pdf.js は隣り合う画像の縁を別々に AA するので、拡大で鉛筆の線に 48 pt の格子の薄い線が出ることがある（推測、未確認）。WS の完了の条件 5「他の viewer でも近い形」に効く。
- 直し方: S1 の tiling pattern を選べば消えやすい。tile のままなら p002 の受け入れに `pdftoppm -r 600` での格子の線の検査を足し、残れば制限として記録する。粒の SMask は色の間で 1 つの object を共有する（色ごとに要るのは一色の RGB だけ）。

### M6. D3 で既存の AAT と toolbar の上限が壊れる

- 根拠: `plan/tools/aat/scenarios/helpers_notes_edit.py:86-104,523`（`NOTES BUTTONS` の action で Pen の button を押し、`NOTES TOOL 1 name=pen` を待つ）。`ui.c:1101-1114` `ui_add_button()` は `NOTES_BUTTONS`（32、`app.h:50`）を超えた button を黙って捨てる。
- 起こる状況: Pen の button が「描く道具」の button になり、描く道具の時に押すと帯を開くので、helper の `press(PEN)` は Pen を選ばない（今の道具が描く道具の時）。Text の道具の control（`ui.c` `ui_text_controls`）が出ている時に帯の 6 つを足すと、32 を超えた分が押せなくなりうる（今の数は未確認）。
- 直し方: p006 の範囲に「既存の AAT の helper・`tests/scenarios/apps/notes/` の scenario の追従」「帯の button の action 番号」「button の数の上限の確かめ（超えたら log）」を足す。D3 の説明に「Pen ↔ Marker の切り替えが 1 回から 2 回の押しになる」を書く。

### M7. 塗りの領域が爆発する（粒の鉛筆、薄い水彩、写真、頁全体）

- 根拠: §3.4 の 2（各 channel 10% の許容差）・6（囲まれていなければ頁全体）、§3.2（鉛筆は alpha 0.30〜1.0 × 粒）、§3.3（縁 2 は alpha 0.06 の Multiply）。
- 起こる状況: 軽い筆圧の鉛筆の線は粒で白に近い画素が多く、10% の許容差では線が「穴だらけ」で flood fill が漏れる。水彩の薄い縁は紙と同じ色と見なされる。他の PDF の写真の上では、まだらの小さな島が数千の輪郭になる。頁全体の塗りは全部の線・文字の周りの穴を持つ polygon になり、S3 の頂点の数、編集 data・journal の大きさ、消しゴムの当たりの計算が重くなる。
- 直し方: 小さな島と穴を面積の下限で捨てる、輪郭の角の数の上限（超えたら許容差を上げて単純化、それでも超えたら塗らずに notice）、頁全体の塗りは確かめてから行う（または穴を作らず一番下に頁の矩形として入れる）を §3.4 に足す。p005 の合成の raster の試験に「粒のある鉛筆の円」「水彩の円」「写真の上」を足す。

### M8. 編集 data 3.0 の細部

- 根拠: `encode.c` の `tools_find`（道具の表は (種類, 色, 幅) で重複を除く）、`encode.c:374-375`（曲線の id と flags は 0 を書く）、`encode.c:536-539`（major は 1 か 2）、`encode.c:590-592`（IMAG・EDIT は `major == ENCODE_MAJOR_EDITS` の時だけ）、`journal.c:565-570`（版はちょうど 2）。
- 起こる状況: 「形の式の版」を `TOOL` に持つなら、同じ (種類, 色, 幅) で式の版の違う stroke が一つの道具にまとめられる。major 3 で `IMAG`・`EDIT` を読むには 590 行の条件を `>=` にする必要がある。journal の 2 を復旧した後に同じ file へ版 3 の record を足すと header と中身が食い違う。
- 直し方: §5.4 に「`tools_find` の鍵に式の版を入れ、stroke が式の版を持つ」「major 3 は 2 の chunk を全部読む」「版 2 の journal を復旧したら snapshot から版 3 の journal を作り直す」を書く。「知らない式の版は一番新しい式で描き、保存で版の値を保つ」は、保存で PDF の content を違う形で書き直すので、知らない版を持つ文書は「他の PDF」扱い（major の規則と同じ考え）を推す。

### M9. D9: 古い Notes が 3.0 の file を保存すると新しい道具の編集 data が失われる（推測）

- 根拠: `encode.c:535-539`、`save.c` の open（major を読めない edit data は使わず「他の PDF」）。2.0 の時の振る舞いと同じ（`encode.c:33-36` の注釈）。
- 起こる状況（推測、古い Notes の保存の経路は読んでいない）: 今の Notes が 3.0 の file を「他の PDF」として開いてペンを 1 本足して保存（autosave 5 s、`notes.h` `NOTES_AUTOSAVE_IDLE_MS`）すると、自分の 1.1 の編集 data で添付を書き、3.0 の編集 data は使われなくなる。以後は新しい Notes でも新しい道具の stroke は背景になり、編集できない。
- 直し方: D9 の説明に「今の Notes で開いて保存すると、新しい道具の stroke は以後どの Notes でも編集できなくなる（2.0 と同じ）」を書く。p003 の試験で古い経路の保存を模して確かめる。

### M10. 依存と Phase の表が古い・抜けている

- BUG-296 の直しは main に入っている（`b26996be7`、`main.c` に `app_picture`、`touch.c` に `notes_touch_picture_limit`）。§9・ws.md の「p003〜p006 は BUG-296 の merge の後」は既に満たされている。ws.md の p003 の依存から外し、§1 の `main.c`・`render.c` の行を `bacb081f8` で取り直す（L1）。
- WS141 への依存（C2 の blend・format、p017 の stencil）が §9 に無い。p007 の Pi の確かめは WS141 p017（ws141/ws.md:82、planning・未投入）の後で、WS216 の完了がいつまでも閉じない。D10 で決める。
- p002 の範囲に reader の直し（C1 の hash の decode、S1 の pattern か decode の cache）が無い。p002 の見積 2 LW は reader を含めると足りない見込み。
- p005 は頁の絵の読み戻し（`render.c` の usage・blit・copy）で `render.c` を触る。§9 の WS210 p008 との重なりは p004 だけでなく p005 にもある。
- p003 が D7 の塗りの消しゴム（塗りは p005 で入る）を含む。塗りの消しゴムは p005 に移す。

### M11. 指の塗りと double tap の拡大

- 根拠: `touch.h:8-33`（double tap で拡大）、§7「書く指の begin〜end が同じ条件なら塗る（double tap の拡大と衝突させない）」。
- 起こる状況: Finger が on の時の 1 回目の tap で塗り、2 回目の tap で拡大（か二度目の塗り）になる。どう衝突させないかが書かれていない。
- 直し方: 塗りの道具で Finger が on の間は double tap の拡大を止める、か 1 回目の tap を double tap の時間だけ待ってから塗る、を決めて §7 に書く。`plan/ws081/tests/host-notestouch.c` に試験を足す（WS081 の試験なので Q1 に依頼）。

## 4. D1〜D9 の推奨の評価

| # | 推奨 | 評価 |
| --- | --- | --- |
| D1 | (a) vector | 妥当。ただし鉛筆の粒の出し方は S1・M5 の通り直しが要る。粒の出し方を D11 として別に問う |
| D2 | (a) 8 段の区間 | 妥当（libpdf の reader は Gouraud の shading を描かないので、連続の濃さは PDF で表しにくい）。M2 の角の規則が前提 |
| D3 | (a) 一つの button と帯 | 方向は妥当。Pen ↔ Marker の操作が増えることと AAT の追従（M6）、notice の行との競合（L7）を選択肢の説明に書く |
| D4 | (a) 静的な縁 | 妥当 |
| D5 | (a) 見えている絵 | 方式は妥当。ただし「今の倍率の頁の絵」ではなく決まった倍率の画面外の描画で（S5）。Guardrail の例外の確認は要る。選択肢に (c)「GPU だけで（fragment の pass を繰り返す flood）」を参考に挙げてよい（重いので推しにはしない） |
| D6 | (a) 見えている物の直上 | 妥当。tap の点が半透明の物（蛍光ペン・水彩）の上の時の決め方を書く |
| D7 | (a) 縁で全体 | **反対**。S4 の通り、線を消すと塗りも消える。(c)「見えている所に触れたら消す」を推す |
| D8 | (a) Flate | **このままでは反対**。C1。major 3 の file だけ Flate、か reader の hash を直した上で今の Notes への影響を示して問い直す |
| D9 | (a) 3.0 | 妥当。M9 の説明を足す |

## 5. 足すべき問い

- D10: Pi 4 の受け入れ。WS216 の完了に Pi の確かめを含めるか（含めるなら WS141 p017 の stencil と bcm2711 の Multiply の blend・format が前提になり、WS216 は WS141 を待つ）。含めないなら Pi の確かめを WS141 か後の Phase に移すことをユーザーが決める。Guardrail「5330 で速くても Pi 4 で確かめる」に関わるのでユーザーの判断。
- D11: 鉛筆の粒の PDF の形。(a) 48 pt の tile の画像を `Do` で並べ、libpdf の reader に decode の cache を足す (b) tiling pattern を使い、libpdf の reader に tiling pattern を足す（古い zedBSD の PDF Viewer では鉛筆が消える） (c) 粒なし（濃さだけ）。S1・M5。
- D12: 囲まれていない所の塗り。(a) 頁全体（穴の多い polygon） (b) 何もしない（notice） (c) 頁の背景の色として一番下に矩形で入れる。M7・S3。

## 6. 軽微

- L1: §1 の行番号は `e162b0267` の物で、`bacb081f8` では `main.c`（例 描き始め 1827-1842 → 1836-1848）・`render.c`（stencil 1883-1920 → 1907-1943、sampler 1417-1419 → 1441-1443）がずれている。BUG-296 の merge による。
- L2: §6.1 の Multiply の式「`1 − a·c + a·c·cs`（a は alpha × coverage）」は c が何か不明（a に coverage を含めるなら `1 − a + a·cs`）。PDF の `ca` 付き Multiply は `Cb × (1 − ca + ca·Cs)` なので、その形で書く。alpha の blend は `ZERO, ONE` と明記する。
- L3: 頁の絵の image（`render.c:1113`）は `COLOR_ATTACHMENT | SAMPLED` だけ。読み戻しに `TRANSFER_SRC` を足す。i915 の image の配置・大きさが変わるかを確かめる。
- L4: 頁の絵の format は swapchain の format（`render.c:1105`、BGRA か RGBA、`render.c:726`）。読み戻しの flood fill の channel の順を両方で扱う。
- L5: §6.1 の GRAIN の push constant は今 vertex の stage だけ（`render.c:1771`、`render.c:2179` の `VK_SHADER_STAGE_VERTEX_BIT`）。fragment で読むなら layout の stage を変える（全 pipeline が同じ layout）。
- L6: p004 の受け入れ「5330 の build で warning 0」は amd64 と rpi4 の build（BUG-296 の記録と同じ形）と書く。
- L7: 帯を notice の 44 px の行（`app.h:36`）に描くと、notice が出ている間に帯と重なる。どちらを優先するかを書く。
- L8: 筆の抜きを「今の終わり」に毎 frame 付けると、描いている間 tail の最後の `min(4w, 0.3L)`（筆 12 pt で最大 48 pt）がいつも細くなり、描き進むと太り戻る。見え方として書いておく（筆圧の抜きと二重に細くなる）。
- L9: libpdf の API の追加は `include/libc/pdf.h` と `userland/base/libpdf/exports.map` を変える（WS079 の library）。§9 に exports.map を書く。
- L10: 塗りの点は「時刻なし」だが、今の encode は `TIME` を常に書く（`encode.c:434-437`）。塗りだけ書かない、を §5.4 に明記する。

## 7. 確かめた点

- `notes.h:40-42,118-159`、`document.c:205-268,419-553,617-806,1206-1410,1455-1735`、`main.c:1640-1670,1815-1850,1935-2015,2360-2400`、`render.c:250-342,1100-1120,1441-1443,1690-1735,1900-1944`、`geometry.c:1-300,380-414`、`shaders/fill.frag`、`app.h:30-55,120-145`、`ui.c:120-145,1101-1114`、`encode.c:1-60,310-445,995-1030`、`journal.c:20-60,550-600`、`save.c:629-667,1120-1154` と libpdf の呼び出しの一覧。
- libpdf: `outline.c:1-600`（定数・clamp・補間）、`writer.c:170-260,400-520,740-790,1900-1915`、`reader.c:503-575,3697-3717`、`content.c:1-40,1860-1945,3666-3717`、`raster.c:40-50,580-670,1140-1210`、`display.c:195`、`internal.h:146-148`、`update.c:2479-2510`。
- driver: `src/drivers/gpu/i915/render/state.c:181-208`（Multiply の係数を持つ）、`i915/memory.h:87`、`bcm2711/vulkan-pipeline-state.c:211-243`、`bcm2711/vulkan-native-draw.c:376-382`、`bcm2711/vulkan-query.c:20,321`、`i915/render/instance.c:340`（push constant 128 byte）。
- 計画: `plan/guardrail.md`「画像の処理は GPU で」、`plan/ws079/design-pdf.md:26,46-70,150`、`plan/bugs/BUG-296.md`、`plan/ws141/ws.md:82`、`plan/tools/aat/scenarios/helpers_notes_edit.py`、git の `b26996be7`（BUG-296 の merge）。
- host に `pdftoppm`・`gs`・`qpdf` がある（p002 の受け入れの道具は使える）。

## 8. 確かめていない点

- 実測は何もしていない（頂点の数・raster の時間・読み戻しの時間・Pi の数）。S1・S3 の数は source の上限と設計の見積もりからの推測。
- 古い Notes が「他の PDF」として開いた file を保存する時の添付の扱い（M9）は source を読んでいない。
- poppler・pdf.js の tile の継ぎ目（M5）は試していない。
- bcm2711 の R8 の未対応は grep で見つからなかったことからの判断（meta の下の source は見ていない）。
- i915 の image の usage を変えた時の配置（L3）、i915 の `vkCmdBlitImage` の縮小の filter の対応（`i915/render/blit.c` に LINEAR はある）は細部を見ていない。
- 今の toolbar の button の数（M6 の 32 の上限との差）は数えていない。
- `plan/master-design-policy.md` は GPU・Notes に関わる節を grep で探したが、この設計に効く規則は見つからなかった（全文は読んでいない）。
