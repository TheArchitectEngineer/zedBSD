<!-- awesome-plan project=zedbsd record=ws074p014 -->

# ws074-p014: 窓（Wayland と Vulkan）と display list の GPU の描画

Phase ID: `ws074-p014`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-27）
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
