<!-- awesome-plan project=zedbsd record=ws074p014 -->

# ws074-p014: 窓（Wayland と Vulkan）と display list の GPU の描画

Phase ID: `ws074-p014`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲（2026-09-27 に分けた）

表の p014（窓・GPU の描画・CONTROLS の titlebar の URL・scroll・link・比較の試験）は 1 つの Phase には大きいので、着手の時に分けた:

- **この Phase（p014）**: zdesktop の窓（xdg toplevel、pointer の wheel と key での scroll、大きさの変更で layout をやり直す、題名は
  page の `<title>`）、Vulkan の GPU の描画（`paint/vulkan.c`: display list を 1 本の pipeline の instance の四角で描く。矩形は CPU の
  参照の描画と同じ画素の面積の被覆、glyph は BGRA8 の atlas から pen の位置を画素に丸めて置く）、Wayland の WSI の swapchain での
  提示（`shell/`）、`--render-gpu --output=OUT.ppm`（offscreen の image に描いて読み戻す）、GPU と CPU の描画の比較の試験
  （host の lavapipe と、guest の Venus）。guest の zdesktop の窓に実際の page を出して撮る。
- **p045（新規、p014 の直後）**: CONTROLS の titlebar の URL の欄、link の click（`file:`）、戻る・進む・再読み込み。

角丸の SDF・clip の scissor・画像の texture は、それを使う primitive を display list に足す Phase で shader に足す。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check（新しい file）0 件。shader は `paint/shaders/regenerate.py` で `glslc`・`spirv-val`
   を通した生成物の header を commit する（zdesktop-files と同じ方式）。
2. host: `--render-gpu`（lavapipe）と `--render` の画像の差が channel ≤ 2、超える画素が 0.1% 以下（design.md §8.2）。golden と
   ASan は前の Phase と同じ。
3. guest（Venus）: 同じ比較、zdesktop の窓に page が出る（画面を撮る）、wheel・key で scroll する。
4. boot test。

## 結果（2026-09-27）

cleared。

- 書いたもの:
  - `paint/gpu.h`・`paint/vulkan.c`: GPU の描画。display list の各矩形と各 glyph が単位正方形の 1 つの instance（正確な矩形、straight の
    色、atlas の位置と種類）。vertex shader が矩形の触る画素の範囲へ広げ、fragment shader が CPU と同じ被覆（矩形は画素の正方形との
    重なりの面積、glyph は atlas の texel）を掛けて straight alpha で重ねる。pass は canvas の画素（`paint_canvas_pixel`、CPU と共通）で
    clear。instance は list の順に 1 回の draw で描く（Vulkan は primitive の順に blend する）。glyph の atlas は 1024×1024 の linear
    BGRA8（host が書く。zdesktop-files の canvas と同じ方式）、shelf で詰め、glyph の bitmap の pointer で引く表。満杯なら次の frame で
    やり直す。`gl_VertexIndex` は使わない（i915 の compiler のため、files と同じ）。
  - `paint/shaders/display.{vert,frag}`・`regenerate.py` → 生成物の `paint/shaders.h` を commit（`glslc`・`spirv-val` を通す）。
  - `--render-gpu --output=OUT.ppm`: 自分の instance と device を作り、offscreen の image に描いて buffer へ copy して読む。
  - `shell/{internal.h,window.c,present.c,shell.c}`: xdg の toplevel（zdesktop-files の window.c と同じ作り、wheel・key の repeat）、
    Wayland WSI の swapchain（B8G8R8A8/R8G8B8A8 の UNORM、FIFO、不透明）に GPU の描画で直接描く、大きさの変更で layout と swapchain を
    作り直す。scroll: wheel（1 単位 3 px）、↑↓（40 px）、PageUp/PageDown/Space（窓の高さ − 40 px）、Home・End。Ctrl+Q・Ctrl+W で
    閉じる。題名は `<title>`（`page_title`、空白を畳む）、無ければ file の名前。`file://` の URL を受ける。test 用の行
    `ZBROWSER READY`・`ZBROWSER FRAME scroll=…`（標準出力）。
  - build: `platform/amd64/vmunix.mk` の browser の link に libvulkan・libwayland-client を足した（files と同じ検査）。host の build は
    host の libvulkan を link する（lavapipe で GPU の描画を host でも試せる）。
- 試験:
  - host（lavapipe）: GPU と CPU の画像の差は `first.html` で最大 2、`blocks.html` で最大 1、2 を超える画素 0（800x600）。ASan
    （UBSan 込み）でも同じで、leak は 0（`--render-gpu`）。golden（dom・style・layout・paint）8/8（plain・ASan）。
  - guest（Venus、QEMU、`plan/ws074/tests/browser-p014.sh`、zdesktop は desktop の試験と同じ `--glass` と wallpaper。main の指摘で
    最初の plain の zdesktop から直した）: zdesktop の窓に `first.html` が出て、zdesktop が titlebar（`<title>` の題名、最小化・最大化・
    閉じる）を描く。guest の `--render-gpu` と `--render` の差は最大 2・1、超える画素 0。`blocks.html` で End → scroll 224 px、wheel を
    1 notch 上 → 179 px、Home → 0。Ctrl+Q で終わる。titlebar の drag で窓が (100, 60) 動く（`ZWL GLASS moved`）、閉じるの button で
    終わる（`ZWL GLASS close`）。zdesktop の log に ERROR 無し。
  - amd64 の build: warning 0。boot test: PASS。
  - i915 の実機: 未実施（WS075 の範囲の後に確かめる）。
- 画像（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）: `p014-20260927-window-first.png`（zdesktop の窓の page）、
  `p014-20260927-window-blocks-scrolled.png`（End の後）、`p014-20260927-window-moved.png`（titlebar の drag の後）、`p014-20260927-venus-gpu-vs-cpu-{first,blocks}.png`（GPU | CPU | 差 ×32）、
  `p014-20260927-boot-login.png`。
- commit: `857c9b8e`（GPU の描画）、`4ba0f68b`（窓と提示）と、この記録の commit。

## 後回し（follow-up）

- titlebar の CONTROLS（戻る・進む・URL の欄）は p045。今は zdesktop の既定の titlebar（題名と窓の button）。
- 窓の中の hover・click・選択、cursor の形（p045 で link から）。
- 角丸の SDF、clip の scissor、画像の texture、opacity の layer（使う primitive を足す Phase で）。scroll の layer と damage。
- 文書が変わらない frame は描かない（今は scroll ごとに全部を描く）。frame callback での描画の機会（design.md §8.3）。
- atlas の満杯の時の frame の中のやり直し（今はその frame の残りの glyph を落とし、次の frame で作り直す）。
