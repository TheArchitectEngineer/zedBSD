<!-- awesome-plan project=zedbsd record=ws074-p238 -->

# ws074-p238: GPU の paint を Pi 4（V3D）の driver が受ける形にする（BUG-320 の (3)）

Status: in-progress（2026-10-18 Q1: Status の行が無かった。本文の「host の確認まで cleared 相当（QEMU・実機は未実施）」から、QEMU・実機が済むまで cleared にしない）

B1 (2026-10-15)。Q1 の割り込み（ユーザーの判断「A: browser の描き方を合わせる」）。Status: host の確認まで cleared 相当（Pi 実機・QEMU は未実施、T2 に依頼）。

## 変更（`userland/desktop/libbrowser/paint/`）

Pi の V3D の kernel の Vulkan が `vkCreateGraphicsPipelines failed (-8)` で断る 4 点を、全 GPU 共通の形に変えた。

1. `shaders/display.frag` の `gl_FragCoord` → vertex shader が出す `noperspective` の varying `pixel_position`（vertex の pixel 座標、線形補間で fragment の中心）。
2. `texelFetch` → nearest の sampler の `texture()`。atlas の texel の中心 `(index+0.5)/2048`（2 の冪なので正確）を読み、同じ texel を得る。atlas の大きさは shader の定数で、`vulkan.c` に `_Static_assert(PAINT_GPU_ATLAS_SIZE == 2048U)` を置いた。
3. `VK_VERTEX_INPUT_RATE_INSTANCE` を廃止。`vulkan.c` の `gpu_vertices` が item ごとに 6 頂点（corner＋同じ item）を 1 本の vertex buffer に書き、`vkCmdDraw(count*6, 1)`。corner 用の buffer（`corners`・`gpu_corners`）は無くなった。
4. fragment の出力を premultiplied（`rgb*alpha, alpha`）にし、blend を ONE / ONE_MINUS_SRC_ALPHA に。
- `shaders.h` は `shaders/regenerate.py`（glslc・spirv-val）で再生成。

## 確かめ（host、lavapipe）

- build warning 0。bcm2711 の compiler: `sh plan/ws141/tests/shader-host-test.sh`（Mesa の oracle は p3 の worktree の `plan/ws141/temp/mesa` を複写）で "browser paint" が refused でなくなり、frag 147・vertex 255・coord 170 命令で native に compile、timing model の 1 レーンの結果が scalar の source と一致、全体 PASS。そのため `plan/ws141/tests/shader-host-test.c` の表の "browser paint" の refused の印を 1→0 に変えた（WS141 の file、Q1 は merge 時に確認を）。
- lavapipe の GPU 描画（`--render-gpu`）の前後を 97 頁（`plan/ws074/tests/pages/*.html` と Acid3 の reference）で比較: 画素が違うのは 16 頁で、うち button-center・input-radius は p236・p237 の変更による期待どおりの差。ほか 14 頁は半透明の縁の画素で最大 ±2（多くは ±1）、Acid3 の reference を含む残り 81 頁は完全一致。差の理由は、UNORM8 の出力に premultiplied を量子化して渡すため（straight の SRC_ALPHA では float で混ぜていた）。完全に同一にはならない。
- Acid3 score 100・差 0（CPU）、Acid2 差 873（不変）。
- 未実施: Pi 4 実機、i915・Venus での描画（lavapipe のみ）。Q1 経由で T2 に依頼。
