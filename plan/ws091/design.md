<!-- awesome-plan project=zedbsd record=ws091-design -->

# WS091 の設計: 画像 viewer（Image Viewer、`/bin/imageview`）

2026-09-29 ws091-p001。目標は [ws.md](ws.md)（ユーザー「画像ビューアの追加。」、デモに必須）。見た目は [Kei の基準](../ws035/kei-identity-design.md)
と Files の浮いたすりガラス（[ws071 spec](../ws071/spec.md) §1・§2・§38、`userland/desktop/files/glass.c`）、操作と部品は PDF Viewer
（`userland/desktop/pdfviewer/`）と libkeiland の motion・scroller・gesture（WS081、`include/libc/keiland.h`）に合わせる。

## 1. 名前と範囲

- program `/bin/imageview`、application ID `imageview`、画面の名前は **Image Viewer**（App Home の tile、window の title「<file 名> — Image Viewer」）。
  画面の文字に Keiland・libkeiland を出さない（「Kei」の決まり）。
- 形式: PNG（libpng-compat: 全ての colour type・深さ、interlace は不可）、JPEG（libjpeg-compat: baseline・progressive、Huffman、8 bit。CMYK・YCCK は
  RGB にする）、GIF（libgif-compat: GIF87a・89a、**動く GIF を再生する**）。拡張子は大文字小文字を無視して `.png` `.jpg` `.jpeg` `.jpe` `.gif`、
  中身の magic で形式を決める（拡張子と違っても magic が正）。
- 範囲（p002）: 表示、fit・実寸・拡大縮小・pan、touch の pinch と慣性、90° の回転（表示だけ、保存しない）、同じ folder の前後の画像、
  command の引数の file、Open（自前の file chooser）、全画面、App Home への登録。
- 範囲外（後の WS）: Files からの起動（WS093。`files/apps.c` の MIME の表は触らない）、編集・保存、slideshow、EXIF の詳細の表示、
  色管理（ICC）、その他の形式（WebP・BMP・TIFF・HEIC）。

## 2. 構成（`userland/desktop/imageview/`）

PDF Viewer と同じく app ごとに window・presenter・canvas・text を持つ形（desktop の既存の app の型）。PDF Viewer の file を元にし、
接頭辞を `iv_` にする。

| file | 役割 | 元 |
| --- | --- | --- |
| `main.c` | 引数、loop（入力・tick・frame）、`IMAGEVIEW READY/DONE/FAILED` の log | pdfviewer `main.c` |
| `imageview.h` | 型と共有の宣言（Wayland も Vulkan も知らない部分） | `viewer.h` |
| `window.c`・`window.h` | xdg toplevel、pointer・keyboard・touch、key の repeat、全画面、titlebar・menu・glass の結線 | `window.c` |
| `present.c` | Vulkan: **画像の層**（texture、§5）と **UI の canvas の層**（CPU、alpha で重ねる） | `present.c` |
| `shaders/`（`image.vert/frag`・`canvas.vert/frag`・`regenerate.py`）→ `shaders.h` | SPIR-V（`glslc` と `spirv-val` で作り tree に置く） | `shaders/` |
| `image.c` | 復号（PNG・JPEG・GIF）、EXIF の向き、大きさの上限、縮小の段（§5）、透明の市松、GIF の frame の合成 | 新規（`files/thumb.c` を参考） |
| `folder.c` | 同じ folder の画像の一覧、並び、今の位置 | 新規 |
| `view.c` | 表示の状態: scale・中心・回転・fit、animation、前後の移動と swipe、入力の解釈 | 新規（`pdfviewer/view.c` の page mode を参考） |
| `touch.c`・`touch.h` | wl_touch → `keiland_gesture`・`keiland_scroller`（pan・慣性・rubber band）・pinch | pdfviewer `touch.c` |
| `draw.c` | UI の canvas: 浮いた chip（名前・「3 / 12」・大きさ・zoom）、message、空の画面、chooser、読めない画像の card | `draw.c` |
| `chooser.c` | Open の file chooser（画像だけ） | `chooser.c` |
| `menu.c`・`titlebar.c` | system menu と浮いた titlebar の control | `menu.c`・`titlebar.c` |
| `glass.c` | keiland_glass の panel（§4） | `files/glass.c` |
| `canvas.c`・`text.c` | CPU の描画と文字 | 同名 |
| `Makefile` | `ZEDBSD_USERLAND_PACKAGE`（imageview、desktop、依存: libvulkan libwayland libkeiland libtruetype libpng-compat libjpeg-compat libgif-compat） | pdfviewer `Makefile` |

## 3. 起動と command line

```
imageview [--display=NAME] [--font=PATH] [--width=N] [--height=N] [--fullscreen] [--timeout-s=N] [FILE]
```

- FILE があれば開き、その folder を一覧にする。FILE が folder なら、その folder の最初の画像。無ければ空の画面（§4）と Open。
- 開いた画像は `keiland_recent_add(path, "imageview")` で最近の file に記録（PDF Viewer と同じ）。
- 標準 error に 1 行ずつ: `IMAGEVIEW READY`（最初の frame）、`IMAGEVIEW SHOW path=<p> index=<i> count=<n> width=<w> height=<h> frames=<f>`、
  `IMAGEVIEW DONE reason=<r>`、`IMAGEVIEW FAILED <何が>`（試験は SSH でこの log を読む。console・serial は読まない）。

## 4. 見た目

- **window**: 本体は 1 枚のすりガラスの card（`KEILAND_GLASS_CARD`、角の半径 18、window の縁から 8 px 内側）。zdesktop の浮いた titlebar
  （WS070 の CONTROLS）がその上に離れて浮く。画像は card の中に余白 16 px で置き、影や枠は付けない（画像が主役、「見た目は静か」）。
  glass を持たない compositor・see-through でない swapchain では淡い slate の不透明の地（Files と同じ判断）。
- **浮いた chip**: 画像の下端の中央に小さな glass の card（名前、「3 / 12」、「4032 × 3024」、zoom の %）。画像を替えた時・zoom を変えた時・
  pointer が動いた時に出て 2.5 秒で薄れて消える。
- **全画面**（F11・F・menu・titlebar・double click）: 黒の地、glass と titlebar 無し、chip は同じ規則。Esc で戻る。
- **空の画面**: Kei の印（`userland/desktop/artwork/mark.c` の描き方を参照、files の Home と同じ）と「Open an image」と Open の button。
- **読めない画像**: card に「Can't show this image」と理由（「Interlaced PNG is not supported」等）。前後の移動はできる。
- **透明**: alpha のある画像は薄い市松（8 px、#f2f4f7 と #e3e7ec）の上に合成する（texture を作る時に合成、§5）。
- 文字は `/usr/share/fonts/keiland.ttf`（file の名前は内部。画面には出ない）、色は slate（#334155）と淡い灰。

## 5. 描画（Vulkan、2 つの層）

- **画像の層**: 復号した画像（premultiplied の 0xAARRGGBB に揃え、透明は市松に合成して不透明にする）を device-local の sampled image に
  1 回 upload し、変換（scale・中心・90° の回転）を push constant で渡した 1 つの quad で描く。sampler は bilinear。
  拡大が 3 倍以上の時は nearest の sampler に替える（画素の粗い画像をぼかさない）。
- **縮小の段**: 縮小の aliasing を避けるため、CPU で 1/2 ずつの段（box filter）を最長辺 256 px まで作り、表示の scale に最も近い
  「その scale 以上の段」を bilinear で描く。hardware の mipmap と `textureLod` は使わない（i915 の kernel の compiler で未確認の機能を避ける。
  既存の canvas の shader と同じ範囲の命令だけを使う: `texture()`、push constant、vertex buffer の座標で `gl_VertexIndex` 無し）。
  段の memory は元の 1/3。
- **UI の層**: PDF Viewer と同じ CPU の canvas（chip・message・chooser・空の画面）を window の大きさの linear image に書き、画像の上に
  premultiplied の alpha で重ねる。UI が変わらない frame は canvas を書き直さない（pinch・慣性の間は画像の層の push constant だけが変わる）。
- **大きさの上限**: `maxImageDimension2D`（device の限界）と 64 Mpx を超える画像は、読んだ後に CPU で 1/2 ずつ縮めて収める（chip は元の
  大きさを示す）。file の大きさの上限 256 MiB（libpng-compat と同じ）。memory は今の画像と前後の 2 枚まで（§7）。
- **GIF の再生**: `DGifSlurp` の全ての frame を GCB（`DGifSavedExtensionToGCB`）の delay と disposal（none・background・previous）で
  1 枚ずつ合成し、変わるたびに texture の内容を書き換える（段は再生中は作らず、縮小表示は bilinear だけ。止めると段を作る）。
  delay 0・1（10 ms 以下）は 100 ms と読む（browser と同じ）。frame の数 × 大きさが 256 MiB を超える GIF は最初の frame だけを示す。
  Space で再生・一時停止。

## 6. 表示の状態と操作（`view.c`）

- 状態: `scale`（画像の 1 px に対する画面の px）、`center`（画面の中心に来る画像の点、画像の座標）、`rotation`（0・90・180・270）、
  `fit`（窓に合わせる状態か）。窓の大きさが変わると fit なら scale を計り直し、fit でなければ中心を保つ。
- **fit**: 画像（回転後）が余白の内側に全て入る最大の scale。ただし **fit は拡大しない**（小さい画像は 1:1 で中央、既定 J3）。
- zoom の範囲: 最小 `min(fit, 1) × 0.5`（指を離すと fit に戻る rubber band）、最大 16 倍（画素）。段は 1.25 倍ずつ。
  animation は 200 ms の ease-out（double tap・fit・1:1・key の zoom）。pinch と wheel は直接。
- **pan**: 画像が窓より大きい軸だけ動く。範囲の外は rubber band（scroller）。fit の時は pan しない。
- **前後の画像**（§7）: fit の時の横の swipe（PDF Viewer の page mode と同じく画像が指に付いて動き、1/3 を超えるか速く払えば次へ滑る）、
  titlebar の ‹ ›、key。
- key: ←・→・PageUp・PageDown・Backspace・Space（GIF では再生の切り替えなので GIF 以外）で前後、Home・End で最初・最後、`+`・`=`・`-` で zoom、
  `0` で fit、`1` で 1:1、`R`・Shift+`R` で右・左の回転、F11・`F` で全画面、Esc で全画面を出る、Ctrl+O で Open、Ctrl+W で閉じる、Ctrl+Q で終わる。
  拡大して横に pan できる時は ←・→ は pan（端に着いてからもう一度押すと前後へ）、↑・↓ は常に pan。
- **pointer**: drag で pan、wheel で pointer の位置を中心に zoom（既定 J4。画像 viewer の多くと同じ）、double click で fit ⇄ 1:1（click の位置を中心）、
  右 click で context menu（回転・全画面・Open）。
- **touch**（`touch.c`、PDF Viewer の touch.c と同じ部品）: 1 本指の drag で pan（慣性と rubber band は `keiland_scroller`）、fit の時の横の
  drag は swipe、2 本指で `keiland_gesture_pinch` の比と重心で zoom（重心の下の画像の点を指に付ける）、double tap で fit ⇄ 2 倍（tap の位置）、
  long press で context menu（`keiland_menu_popup`）。指の動きは `keiland_gesture_drag_offset` の resample で frame に合わせる。

## 7. folder と前後（`folder.c`）

- 開いた file の directory を `opendir` で読み、拡張子で画像を選び（§1）、名前で並べる（数字の並びを数として比べる「自然な順」、
  大文字小文字を無視、同じなら byte 順。既定 J5）。隠し file（`.` で始まる）は除く。
- 今の file の位置を覚え、前後は端で止まる（輪にしない。既定 J6。端では「First image」「Last image」の message）。
- 一覧は画像を開くたびでなく、folder が変わった時と、前後に移る時に directory の変更時刻が変わっていたら読み直す。
- 前後の 2 枚は、今の画像を示した後の空き時間（入力が無い frame）に 1 枚ずつ復号して持つ（同じ thread。大きな JPEG の復号の間は入力が
  少し遅れる。別 thread の復号は後の改善、既定 J7）。

## 8. 復号（`image.c`）

- PNG: `png_image_begin_read_from_file` → `format = PNG_FORMAT_RGBA` → `png_image_finish_read`（straight alpha）→ premultiply。
- JPEG: `jpeg_stdio_src`、`jpeg_save_markers(APP1)`、`out_color_space = JCS_RGB`（CMYK・YCCK は Adobe の反転を見て RGB に変換）。
  **EXIF の向き**（APP1 の `Exif\0\0` の TIFF の IFD0 の tag 0x0112、1〜8）を読み、画像を回転・反転してから表示の既定の向きにする。
- GIF: `DGifOpenFileName` → `DGifSlurp` → frame ごとの合成（§5）。透明の index、interlace は library が行を並べ直す。
- 失敗は理由の文字列と共に返し（§4 の読めない画像の card）、`IMAGEVIEW FAILED` は出さない（program は動き続ける）。

## 9. menu と titlebar

- system menu（`keiland_menu`）: **File**（Open… Ctrl+O、Close Ctrl+W、Quit Ctrl+Q）、**View**（Fit、Actual Size、Zoom In、Zoom Out、
  Rotate Right、Rotate Left、Full Screen）、**Go**（Previous Image、Next Image、First Image、Last Image）、**Help**（About Image Viewer）。
- titlebar の control（CONTROLS）: ‹（前）、›（次）、位置「3 / 12」、Zoom Out、Zoom In、Fit、Rotate、Full Screen。場所が足りなければ
  zdesktop が「...」に畳む（PDF Viewer と同じ）。

## 10. 登録（App Home）と image への組み込み — **他の担当の file**

いずれも imageview の source の外なので、最小の差分を用意して main に依頼する（この WS の worktree では変えない）。

1. **App Home**（`userland/desktop/wayland/home.c`、wayland の担当）: 組み込みの一覧に 1 行
   `home_add_app("Image Viewer", "/bin/imageview", "image picture photo viewer png jpeg gif", 0x3fa36bU, "image");`。
2. **tile の絵**（`userland/desktop/wayland/icons.c`・`icons.h`、wayland の担当）: `GLASS_ICON_APP_IMAGE`（角の丸い額の中に山と太陽の線画、
  他の app の絵と同じ線の太さ）と `icon_app_names` の `"image"`、`icon_app_ids` の `{ "imageview", GLASS_ICON_APP_IMAGE, 0x3fa36bU }`。
  絵が無い間は tile に頭文字の「I」が出る（既存の規則）ので、1. だけでも使える。
3. **デモの一覧**（`plan/ws035/demo/apps.conf`、WS035 の plan）: `Image Viewer|/bin/imageview|image picture photo viewer png jpeg gif|3fa36b|image`。
4. **image の program の一覧**（`config/ci/config-amd64.mk` 等の `ZEDBSD_USER_PROGRAMS`、main）: `imageview` を足す（libpng-compat・
   libjpeg-compat・libgif-compat は既にある）。
5. 見本の画像: `/usr/share/keiland/samples/` 等に置くかは main の判断（既定 J8: 置かない。デモの画像は main が用意）。

p002 の試験では 1〜4 を自分の guest の中だけで行う（`/etc/keiland/apps.conf` を guest に置く、program を `/bin` に写す）。

## 11. 試験（p002・p003）

- **host の試験**（`plan/ws091/tests/`、Linux で build）: `view.c` の計算（fit・zoom の中心の保存・回転・pan の範囲・swipe の判定）、
  `folder.c`（自然な順、拡張子、隠し file）、EXIF の向きの解析（8 つの値）、`image.c` の復号（host の Python の PIL で作った PNG（各 colour type、
  alpha）・JPEG（baseline・progressive・EXIF の向き 6）・GIF（静止・動く・透明）を compat の library の host の build で復号し、画素を PIL の
  結果と比べる。PIL は試験の画像を作る host の道具としてだけ使う）。
- **guest の試験**（Venus、`plan/ws035/tests/zdesktop-guest.sh` を `GUEST_RUNTIME=build/ws091-run` で、image は main の `build/ws035-sq/hdd-image.img`
  の複写に自分の `imageview` と App Home の一覧を写す。clang・libcxx を含む image は build しない）: 画面を `zdesktop-shot.py`（`zdesktop-check.py`）で
  撮り `build/ws091-shots/` へ: App Home の tile、fit の写真、1:1、拡大して pan、次の画像、回転、透明の PNG、動く GIF の 2 つの時刻、全画面、
  空の画面と Open、読めない画像。入力は QMP の pointer・key（`qmp-pointer.py`・`qmp-keys.py`）。
- **touch**: host の試験で gesture の列を `touch.c` に流して view の状態を確かめる。guest の touch の注入（`touchinject`）は
  `CONFIG_INPUT_TEST_INJECT=y` の image でしか使えないので、使えなければ未実施と書き、実機（素の 5330 の touch panel）はユーザー。
- 回帰（p003）: 全文の規約（`plan/tools/style-check.py` の新しい file 0）、build の warning 0、PDF Viewer と Files に触れていないこと、boot test。

## 12. 判断が要る点（既定を選んだ）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| J1 | 名前 | Image Viewer、`/bin/imageview`、ID `imageview` | 他の app（PDF Viewer・Files・Notes）と同じ英語の名前 |
| J2 | 本体の地 | 明るいすりガラス（全画面は黒） | Kei の見た目。写真を見る時は全画面 |
| J3 | fit は小さい画像を拡大しない | しない | 画素の粗い拡大を既定にしない（多くの viewer と同じ） |
| J4 | wheel | pointer の位置で zoom（pan は drag） | 画像 viewer の多くと同じ。前後は key・titlebar・swipe |
| J5 | 並び | 自然な順（IMG_2 < IMG_10） | 写真の連番 |
| J6 | 前後の端 | 止まる（輪にしない） | 端が分かる |
| J7 | 復号 | 同じ thread、前後を空き時間に | 単純。別 thread は後 |
| J8 | 見本の画像 | image に入れない | デモの画像は main が選ぶ |
| J9 | 動く GIF | 再生する（Space で一時停止） | 表示の範囲に GIF がある |
| J10 | 回転 | 表示だけ（保存しない） | 編集は範囲外 |
| J11 | tile の色 | 緑（0x3fa36b） | 他の app（青・橙・赤・水色）と重ならない |
