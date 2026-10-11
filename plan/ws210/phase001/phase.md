<!-- awesome-plan project=zedbsd record=ws210-p001 -->

# ws210-p001: Keiland の app の描画を GPU へ — 調査と設計

Parent: [WS210](../ws.md)
Status: in-progress（P1。設計の第 1 版、design-reviewer の review を Q1 に依頼）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来と入力

- 2026-10-12 ユーザー:「Keiland の app の描画を GPU に、は、新WSで進めてください。着手OKです。」方針:「PI4のような低消費電力のモバイルSoCでは、画像をCPUで触ると、体験を損ないます。すべてGPUで処理する必要があります。」（[Guardrail](../../guardrail.md)「画像の処理は GPU で」: CPU が画素を触るのは file の decode と、起動の時・内容が変わった時の一度だけの用意に限る。毎 frame は GPU）。
- [ws141-p013](../../ws141/phase013/phase.md)（P3）の調べ: Pi 4 では client の frame ごとに、app の CPU の描画、canvas の行の copy、kernel の raster→UIF の CPU の変換（submission あたり 25〜180 ms）が起きる。WS141 の ws.md（2026-10-12）: cursor の plane は 60 fps、窓の中身は約 15 fps（CPU の texture の変換が主因）。
- この Phase は調査と設計だけ。source は変えない。調べは agent/p1 = main ede36dc65 の tree。

## 今の tree の事実

### libkeiland の描画

| 所 | 事実 |
| --- | --- |
| `ui/canvas.c`（1468 行） | `struct kl_canvas`（公開、`keiland.h` 2891）は app の `uint32_t *pixels`・stride・大きさ・clip の stack（16）・polygon の 1 行の coverage を持つ。描画の口は 17: `clear`（clip の中を透明に）・`fill`・`gradient`（縦）・`round`・`round_gradient`・`round_border`・`shadow`・`circle`・`ring`（12 時から時計回りの扇）・`polygon`（nonzero、96 点まで、1 画素の行を 4 本の sub-row で走査）・`line`（丸い端を 8 点の polygon で近似）・`mask`（8 bit の coverage、glyph）・`image`（bilinear、角丸、不透明度）・`kl_image_create`/`release`/`scale`（縮小は box の平均）・`kl_color_mix`。丸い形は全て signed distance（画素の中心で 0.5 の幅の 1 画素の AA）、影は smoothstep。画素は premultiplied の 0xAARRGGBB |
| `ui/text.c`（843 行） | libtruetype で glyph を大きさごとに一度だけ 8 bit の coverage に rasterize し、`kl_text` の hash の cache に持つ（4 分の 3 が埋まったら空にする）。色の emoji は premultiplied の画素（`picture/color-glyph.c`）。描画は glyph ごとに `kl_canvas_mask`、色の glyph はその場で組んだ `kl_image` で `kl_canvas_image`。kerning・shaping は無い |
| `ui/present.c`（1296 行、`keiui_present`） | window ごとに VkInstance・device・Wayland WSI の swapchain（最小 3 枚）・LINEAR の host-visible の canvas の image を作る。frame ごとに CPU の pixels の変わった行を canvas の image へ `memcpy`（`present-copy.c`）、swapchain の image に canvas を 1 枚の quad で全面に描き、`VK_KHR_incremental_present` で変わった所を知らせ、fence を待つ（1 frame in flight） |
| `ui/present-shm.c` | Vulkan の無い所と library の小さな window（file chooser・media picker）は wl_shm の 2 枚の buffer に CPU で copy |
| 公開の契約（`keiland.h` 3670〜） | 「A frame is ordinary memory of premultiplied 0xAARRGGBB words」。app が pixels を `malloc` し `kl_canvas_init(pixels)`、描いて `kl_window_present_part(window, pixels, stride, part)`。`KL_PRESENT_VULKAN`・`_SHM`・`_NONE`（app が自分の Vulkan で描く）。今の版は `KL_VERSION 88` |
| 部分の再描画 | app は `kl_ui_take_damage` の part を clip に push して描き、残りの画素を前の frame のまま使う（BUG-221・226）。今は CPU の pixels と canvas の image がその「残り」を持つ |

### 使う側（数は `grep` の行数）

| 使う側 | 描画の作り | 毎 frame の CPU の画素の処理 |
| --- | --- | --- |
| libkeiland の widget（`ui.c`・`widgets.c`・`cards.c`・`icons*.c`・chooser・picker など） | `kl_canvas_*`・`kl_text_draw*` だけ（約 196＋44 箇所）。画素を直に触らない | canvas の全部 |
| Files・Settings・Phone・Mailer・Photos・Music・Calendar・Video Player の UI・IME の popup | `KL_PRESENT_VULKAN`（Files・Settings は薄い wrapper `files/present.c`・`settings/present.c`）。`kl_canvas_*` 約 450 箇所、`kl_text_draw*` 約 340 箇所 | canvas の全部 |
| Text Editor・PDF Viewer・Image Viewer | 自前の CPU の canvas（`te_canvas`・`pv_canvas`・`iv_canvas`: 置き換えの fill・blend・round・mask・page の raster の copy と伸縮・回した画像の bilinear）。Image Viewer は自前の Vulkan（`KL_PRESENT_NONE`）で画像の quad＋CPU の canvas の overlay | 文字と UI の全部、PDF の page の copy・伸縮 |
| Calendar の 3D（`render3d.c` 644 行） | CPU の三角形の rasterizer（supersample、bilinear の texture）。`view_keep_under` が canvas の画素を読む | page をめくる animation の間、毎 frame |
| Video Player | libswscale で decode の絵を frame の pixels へ縮小・色の変換 | 再生中、毎 frame |
| Notes | 自前の Vulkan（stencil の塗り）。toolbar と text box の overlay は CPU の canvas → LINEAR の image | overlay が変わった時 |
| Terminal・System Monitor・Browser | 自前の Vulkan（glyph の atlas・shape の SDF・instancing）。atlas は CPU で一度書く | 無し（対象の外） |
| compositor | `kl_canvas` を使わない（`kl_tr*`・`kl_settings_*`・`kl_motion_*`・`kl_mark_raster` など）。自前の Vulkan で glass・影・文字・blur を shader で描く | WS141 の範囲 |

### 既存の GPU の 2D の描画（土台の候補）

- `wayland/shaders/panel.frag`: 角丸の SDF・影・ring・文字（atlas の alpha）・画像・blur を mode で切り替え、`-DKWL_PANEL_MODE=n` の 7 つの variant も作る。bcm2711 と i915 で動いている。
- `monitor/render.c`＋`shaders/shape.frag`: 頂点あたり 16 float（corner・local・shape・color・second）の非 instancing の shape と glyph の 2 pipeline。i915 で動いている。
- `libbrowser/paint/vulkan.c`: instancing、shelf の atlas、offscreen と readback、CPU の renderer（`software.c`）と画素で合わせる設計、`plan/ws074/tests/gpu-compare.py`（host の lavapipe で GPU と CPU を比べる、channel の差 2・画素の 0.1%）。instancing・`gl_FragCoord`・early return を使うので bcm2711 では動かない。
- shader の道具: 各 package の `shaders/regenerate.py` が `glslc --target-env=vulkan1.0 -O`＋`spirv-val` で SPIR-V を作り、`shaders.h` を commit する（build に shader の compiler は要らない）。i915 の確かめは `plan/ws075/tests/guard/run.sh`、bcm2711 は `plan/ws141/tests/shader-host-test.sh`（compositor の shader だけ、Mesa の oracle を temp に要する）。共有の shader の部品は無い（SDF の関数は panel.frag と shape.frag に重複）。dev host には glslc・spirv-val・lavapipe（`lvp_icd.json`）がある。

### driver の制約（共通の部分集合を決める）

**bcm2711（Pi 4、V3D 4.2）**（`src/drivers/gpu/bcm2711/`・`src/drivers/gpu/compiler/spirv.c`、読むだけ）:

| 項目 | 事実（主な所） |
| --- | --- |
| 命令 | float の四則・min/max・比較・select・sqrt・rsq・rcp・exp2・log2・floor・fract、整数の加減・bit 演算・shift、`texture()`。`if` は両方を実行して select（`spirv.c:20-45`）。length・clamp・mix・step・smoothstep・pow は使える |
| 拒む物 | discard、loop（unroll できない物）、sin・cos・tan、atan・asin・acos（frontend が全 driver で拒む）、微分（dFdx・fwidth）、整数の乗除・剰余、整数の比較（OpSwitch・配列の動的な index を含む）、`textureLod`・bias・texelFetch・offset、vertex shader の texture、location 16 以上の入力（`gl_VertexIndex`・`gl_InstanceIndex`・`gl_FragCoord`）、location 0 以外の出力、関数の呼び出し（inline されない物）、8192 命令・64 register を超える物（spill 無し）（`shader-analyze.c`） |
| pipeline | triangle list だけ、**instancing 無し**（`inputRate` は VERTEX、instanceCount 1）、indexed draw 無し、viewport と scissor は dynamic、depth・stencil・MSAA 無し、color attachment 1 つ、blend は無しか premultiplied（ONE, ONE_MINUS_SRC_ALPHA, ADD）だけ（`vulkan-pipeline-state.c`） |
| 入力 | 頂点の属性は R32〜R32G32B32A32_SFLOAT だけ、計 32 scalar まで。varying は計 32 scalar まで。push constant 128 byte まで。descriptor は COMBINED_IMAGE_SAMPLER と UNIFORM_BUFFER、配列無し、set あたり texture 8・UBO 4、set は 4 まで |
| image | 2D、4096×4096 まで、mip 1、**R8G8B8A8・B8G8R8A8 だけ（R8 は無い）**。LINEAR も OPTIMAL も raster で持つ |
| sample | 描く時に sample する image を raster→UIF に **CPU で**変換（`native-texture.c:179`）。CPU に map されたことの無い image は buffer の世代が変わるまで変換の結果を再利用（snapshot の cache）。map された image は毎 pass 変換 |
| copy | buffer→image は「全体・1 region・64 byte の整列」だけ GPU（tile の load/store）、他は kernel の CPU の行の copy。同じ大きさの image の全体の `vkCmdCopyImage` は GPU の TLB copy（P3 の incremental: 前回から変わった tile だけ）。他の copy・blit は texture の quad（source の CPU 変換が付く） |
| render-to-texture | 動くが出力は raster。後で sample すると世代ごとに CPU 変換。TFU は起動の自己試験だけで使っていない。「GPU の出力を UIF で持つ」（Guardrail の案 (A)）は WS141 で未設計 |

**i915 native**: 上の bcm2711 の部分集合より広い（loop・discard・微分・`gl_FragCoord`・instancing・`gl_VertexIndex` は ws075-p004 以降可）。[F-023](../../future-work.md) の OpSwitch・OpCompositeInsert・triangle strip・`vkFreeDescriptorSets` は未確認なので避ける。frontend の制約（関数 1 つ、atan 無し、combined image sampler だけ）は共通。
**Venus**: guest の制約無し（host の driver が compile）。**llvmpipe・Linux・FreeBSD の Mesa**: 標準。

→ **共通の部分集合 = bcm2711 の制約**: float の属性の非 instancing の triangle list、push constant と UBO、RGBA8・BGRA8 の combined image sampler と `texture()`、直線か `if` の code（loop・discard・微分・三角関数・整数の比較なし）、premultiplied の source-over か blend 無し。panel.frag の書き方（`pixel` の varying、float の範囲の比較、mode ごとの variant）がこの範囲で動く実例。

### Linux・FreeBSD

libkeiland の `Makefile.linux`・`Makefile.freebsd` は同じ source を build し、`libvulkan.so.1`（`libvulkan-compat`: Keiland の Wayland・KMS の WSI を持ち、system の Vulkan を dlopen で chain する shim）に link する。描画は system の driver が標準の Vulkan で行うので、GPU の back end は zedBSD と同じ source で動く。OS の macro は要らない。

## 設計

### 用語

- **GPU の canvas**: window に結んだ `kl_canvas`。描画の口は今と同じで、画素を書かずに頂点の列を記録し、frame の終わりに Vulkan の command にする。
- **CPU の canvas**: 今の `kl_canvas`（app の pixels に描く）。残す（判断 G2）。
- **W（window の image）**: window の大きさの GPU の image（B8G8R8A8、OPTIMAL、color attachment と transfer src、CPU に map しない）。前の frame の内容を持ち、部分の再描画の「残り」を保つ。
- **context**: app（`kl_app`）ごとに 1 つの VkInstance・device・queue・pipeline・glyph の atlas・画像の texture の cache。window で共有する。

### A: API の形（app の変更を最小に）

A1. 描画の口（`kl_canvas_*` の 17、`kl_text_draw*`、`kl_icon_*`、widget）は名前・引数・意味を変えない。描く先が GPU か CPU かは canvas が持つ。

A2. `struct kl_canvas` に back end の pointer（GPU の canvas の状態、CPU の canvas では NULL）を足す。公開の struct の大きさが変わるので `KL_VERSION` を 89 に上げる。tree の app は全て一緒に build し直す。

A3. 新しい口（window の frame）:

```c
/* The window's canvas (on the GPU when the window has Vulkan), valid until the next resize (KL_VERSION 89). */
int kl_window_canvas(struct kl_window *window, struct kl_canvas **canvas);
/* Begins a frame: part is what the application will draw again (NULL: all); draw is what it must draw. */
int kl_window_frame_begin(struct kl_window *window, const struct kl_rect *part, struct kl_rect *draw);
/* Shows the frame drawn on the window's canvas since kl_window_frame_begin. */
int kl_window_frame_end(struct kl_window *window);
```

`draw` は普通は `part` と同じで、W が無くなった時（初めて・resize・device の失い）だけ全面になる（app は `draw` を clip に push して描く）。app の変更は resize（pixels の `malloc` と `kl_canvas_init` を `kl_window_canvas` に）と frame（`kl_window_present_part(pixels)` を `frame_begin`・`frame_end` に）の 2 箇所。`kl_window_present`・`_part`・`kl_canvas_init` は CPU の canvas と fallback のために残す（G2）。

A4. 画素を読む・書く使い方は GPU の canvas では使えない（`canvas->pixels` は NULL）。移す前に、今それをする所（D 節）を新しい口に置き換える。

A5. **画像**: `kl_image` は CPU の画素のまま（decode の結果）。GPU の canvas は初めて描く時に texture にし（upload、一度だけ）、以後は再利用する。内容を変えた app は `kl_image_changed(image)`（新規）を呼ぶ。`kl_image_release` で texture も捨てる。cache の key は `pixels` の pointer・大きさ・`kl_image_changed` の世代。`kl_image_create` で作らずに struct を組む使い方（text.c の色の glyph）は、移す時に直す（色の glyph は atlas へ、B5）。cache は byte の上限（案 64 MiB）の LRU、外れたら再び upload。4096 を超える画像は一度だけ CPU で縮めた版を作る（`kl_image_scale`）。

A6. 新しい描画の口（D 節の置き換えに要る物だけ）:
- `kl_canvas_image_part(canvas, image, source_rect, x, y, w, h, radius, opacity, flags)`: 画像の一部を、filter（bilinear・nearest）と 90 度ずつの回転（`KL_IMAGE_TURN_*`）で描く（Image Viewer・PDF Viewer）。
- `kl_canvas_fill_replace(canvas, rect, color)`: 下を置き換える塗り（PDF Viewer・Text Editor の fill）。GPU では blend 無しの pipeline。

### B: GPU の back end の作り

B1. **記録**: `kl_canvas_*` は frame の頂点の列（host-visible・coherent の buffer、frame ごとに書き、足りなければ倍に）に形ごとに 6 頂点（三角形 2 つ、instancing と index を使わない）を足す。頂点は float だけで 24 scalar 以下: 画素の位置（2）、形の局所の位置（2）、形の箱の半分の大きさ・半径・太さか softness（4）、色（4）、2 つ目の色か texture の重み（4）、texture の座標（2）、clip の矩形（4）、種類と補助の値（2）。varying も 24 以下。

B2. **pipeline**（bcm2711 では `if` の両側を必ず実行するので、種類ごとの shader を分ける。panel.frag の variant と同じ考え）:

| pipeline | 描く物 | fragment の計算 |
| --- | --- | --- |
| shape | fill・gradient・round・round_gradient・round_border・shadow・circle・ring・line | 角丸の箱の SDF 1 回（半径 0 で矩形、半径 = 半分で円）、枠は距離の帯、影は smoothstep、gradient は局所の y で色を混ぜる。ring は円の帯と扇（扇は atan を使わず、12 時からの角度を「終わりの向き」との外積・内積の符号で判定、割合 ½ を境に積と和を select）。line は線分への距離の capsule（C3） |
| texture | mask（glyph）・色の glyph・image・image_part | atlas か画像を 1 回 sample。`color × tex.a`（glyph）と `tex × opacity`（画像・色の glyph）を頂点の重みで混ぜる（両方とも安い）。角丸の画像は箱の SDF を掛ける |
| edge | polygon（C2） | 3 本の辺の直線への符号付き距離（頂点で計算し線形補間、affine なので正確）のうち、多角形の外周の辺だけで coverage を作る |
| replace | `kl_canvas_clear`・`fill_replace` | blend 無し、色をそのまま書く |

全ての shader は bcm2711 の部分集合で書く（`pixel` の varying、float の範囲の比較、early return・discard・loop 無し、出力は location 0 の 4 成分）。clip は fragment で `step` の積（両側を実行しても安い）にし、矩形の形は記録の時に quad を clip へ切り詰めて fragment を減らす。scissor は frame の `draw` の範囲だけに使う（clip の push/pop で draw を切らない）。blend は premultiplied（replace を除く）。

B3. **batch**: 記録の順に描く（blend の順を保つ）。pipeline か texture の descriptor が変わる所で draw を切る。文字の列は同じ atlas の頁なので 1 つの draw になる。descriptor set は texture ごとに作って cache（`vkFreeDescriptorSets` を使わず、pool ごと捨てる）。glyph の atlas の頁と画像は同じ binding で set を切り替える。

B4. **文字**: glyph の rasterize は今の text.c のまま CPU で一度だけ。結果を **RGBA8 の atlas の頁**（R8 が無いので coverage を α に。512×512＝1 MiB の頁、最大 16 頁＝16 MiB、shelf で詰める）に置く。頁ごとに CPU 側の写し（staging buffer、64 byte に整列した行）を持ち、新しい glyph を書いた頁は frame の pass の前に**頁の全体**を 1 region で `vkCmdCopyBufferToImage`（bcm2711 で GPU の tile copy になる形、部分の copy は kernel の CPU の行の copy になるため）。sample の時の raster→UIF の変換（今の bcm2711、CPU）は頁が変わった frame に 1 回、頁の大きさだけ（WS141 の案 (A)・TFU の後は GPU）。頁が全部埋まったら一番古い頁を空にする。色の emoji は同じ頁の形（premultiplied の RGBA）。`kl_text` の cache の slot に atlas の頁と場所を足す（`struct kl_glyph` は公開、A2 の版で）。

B5. **画像**: A5 の cache。upload は staging buffer から 1 region の全体の copy（同じ理由）で、map しない OPTIMAL の image（B8G8R8A8）へ。内容が変わった時だけ。

B6. **frame**: `frame_begin` は swapchain の image を acquire し、`draw` を返す。`frame_end` は 1 つの command buffer に、atlas の頁・画像の upload → W への render pass（LOAD、scissor = `draw`）→ W から swapchain の image への**全体の** `vkCmdCopyImage`（C1）を記録して submit・present、fence を待つ（今と同じ 1 frame in flight）。`VK_KHR_incremental_present` は今のとおり `part` を知らせる。

B7. **context**: app ごとに 1 つ（`kl_app` が持つ。`kl_app` を使わない window は window ごと）。今の window ごとの instance・device を改め、Files の desktop と窓などで atlas と画像の cache を共有する。

### C: 部分の再描画、polygon、線

C1. **部分の再描画**: W が前の frame を持つので、app は今と同じく `part` だけを描けばよい。swapchain の image へは W の全体を copy する（同じ大きさの全体の copy: bcm2711 では GPU の TLB copy で、P3 の incremental により変わった tile だけ。i915・Venus・Mesa では GPU の数 MB の copy）。swapchain の image の内容が acquire の後に残ることに頼らない（Vulkan の規格は約束しない）。全体の copy なので image ごとの履歴は要らない。
- 不採用の案: (b) W を持たず swapchain の image に直接描く（buffer age 式。app が image ごとの和を描き直す必要があり、規格に頼る）。(c) 矩形の region の copy（bcm2711 では texture の quad になり W の CPU 変換が付く）。

C2. **polygon**: bcm2711 は loop・stencil が無いので、fragment の走査（libbrowser・CPU の算法）も Notes の stencil の塗りも使えない。点の列を CPU で三角形に分け（ear clipping、geometry の計算で画素ではない）、edge の pipeline で描く。各三角形は外周の辺だけを外へ 1 画素ずらして広げ（ずらした直線の交点を新しい頂点に）、外周の辺への距離 d で coverage = clamp(0.5 + d)、内側の辺は AA せず隣の三角形と継ぎ目なく接する（rasterization の規則）。自己交差の多角形（今の tree の使い方には無い）は ear clipping が失敗するので、bounding box の fan で描き、`KL_CANVAS_POLYGON_COMPLEX` の log を 1 回出す（見た目は近似）。CPU の 4 sub-row の算法とは辺の画素で最大 1/8 程度違う（G の試験で polygon だけ許容を広げる）。

C3. **線**: 丸い端の線を capsule（線分への距離）の SDF にする。今の CPU は端を 8 点の polygon で近似するので見た目が少し違う（端が滑らかになる）。CPU の canvas も同じ capsule に変えて両者を一致させる（判断 G4）。

### D: 画素を直に触る使い方の置き換え（A4）

| 使う側 | 置き換え | Phase |
| --- | --- | --- |
| Text Editor（`te_canvas`） | `kl_canvas_*`（fill_replace・round・mask・色の glyph）へ。`round_full` の「前の画素と同じなら結果を使い回す」は不要 | p006 |
| PDF Viewer（`pv_canvas`） | page の raster（libpdf の CPU の描画、page か zoom が変わった時だけ = 一度だけの用意）を `kl_image` にし `kl_canvas_image_part` で伸縮。UI は `kl_canvas_*` | p006 |
| Image Viewer・Notes（`KL_PRESENT_NONE`、overlay の CPU の canvas） | app の pass の中に GPU の canvas の draw を記録する口（`kl_canvas_gpu_record(canvas, command_buffer)` の形、app の device を使う）を足し、overlay を GPU の canvas に | p006（口の形は G3） |
| Calendar の 3D | 三角形の列を GPU の mesh の pipeline（depth 無し: bcm2711 に depth が無いので、今の CPU と同じく奥から順に描く。texture は perspective 補正、bilinear）。`view_keep_under` は描き直しにする | G3 |
| Video Player | decode の絵（YUV の 3 面か NV12）を texture にし、GPU で色の変換と縮小。毎 frame の upload は decode の結果の受け渡し（decode そのもの）。i915 の vkvideo の decode の絵は既に GPU の image | G3 |
| file chooser・media picker（`KL_PRESENT_SHM`） | `KL_PRESENT_VULKAN` と GPU の canvas に。Vulkan の無い時は今の SHM と CPU の canvas（E1） | p005 |
| Files の hero・Calendar の page・thumbnail・System Monitor の atlas | offscreen の一度だけの絵。CPU の canvas のまま（Guardrail の範囲）。描いた `kl_image` は B5 で GPU へ | — |

### E: fallback と OS

E1. Vulkan の device が無い・pipeline が作れない時、`kl_window_canvas` は CPU の canvas（library が pixels を持つ）を返し、`frame_end` は今の present（canvas の image への copy）か SHM で出す。app の code は同じ。CPU の canvas は offscreen の一度だけの絵と host の試験の基準にも使う。
E2. Linux・FreeBSD は同じ source、libvulkan-compat の後ろの system の driver。OS の macro は増やさない（Guardrail の配置の規則）。compositor は `kl_canvas` を使わないので、`keiland-os-boundary/check.sh` の B2 の許可の表は変えない。

### Pi 4（bcm2711）で残る CPU の画素の処理と WS141 との分担

| 処理 | WS210 の後 | 消すのは |
| --- | --- | --- |
| app の描画（canvas）、canvas の行の copy | 無くなる | WS210 |
| W への描画、W→swapchain の copy | GPU（TLB copy、変わった tile だけ） | WS210（P3 の TLB copy を使う） |
| glyph の atlas の頁・画像の upload | 一度だけ（GPU の tile copy） | WS210 |
| 頁・画像の sample の時の raster→UIF の変換 | 内容が変わった時だけ CPU（kernel）| WS141（案 (A)・TFU） |
| **compositor が client の swapchain の image を sample する時の raster→UIF の変換** | **client の frame ごとに CPU（窓の大きさ）** | **WS141（案 (A)・TFU）** |

→ WS210 だけでは Pi 4 の client の frame ごとの CPU の変換（最後の行）が残る。WS210 の完了の条件の「Pi 4 で 60 fps に近い」は WS141 の案 (A) か TFU の後でないと満たせない（判断 G5）。WS210 は app の側の CPU の処理を 0 にし、render の出力の tiling に依らない作り（W の全体の copy、atlas の頁の全体の copy）にしておく。

### G: 試験

- **host の一致の試験**（新規、回帰として保ち `plan/tools/` と master.md の Tools に登録）: 同じ描画の列（17 の口と文字・色の glyph・画像・clip・部分の再描画・置き換えの塗りを全部含む数十の場面、light と dark）を CPU の canvas と GPU の canvas（dev host の lavapipe、offscreen の W を readback）で描いて比べる。合格は channel の差 2・画素の 0.1% まで（libbrowser の gpu-compare の基準）、polygon の場面だけ辺の画素で 32 まで。差の絵を `build/` に出す。
- **shader の確かめ**（host、新規）: checked-in の SPIR-V を i915 の host の compiler（ws075 の guard の形）と bcm2711 の frontend＋`shader-analyze`・`shader-lower`（kernel の source を読むだけで link、Mesa の oracle は使わない受理だけの確かめ）に通す。
- **T1 の QEMU**（WS の区切りで、p007）: Venus・llvmpipe の image で、移した app（Files・Settings・Phone・Text Editor・PDF Viewer）を開いて画面を撮り、移す前の版の PNG と比べる（文字・角丸・影・画像・glass の see-through）。frame の時間の log（`kl_window_present_times`）。
- **Pi 4 の実機**: ユーザー（Files・Settings・Phone の操作）。P3 の kernel の perf 行で、client の frame の texture の CPU の変換が窓の image の 1 つだけ（WS141 の後は 0）になったことを数で確かめる。

## Phase（案）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計と review | — | — |
| p002 | GPU の canvas の核: context、頂点の記録、shape・replace の pipeline（塗り・gradient・角丸・枠・影・ring・circle・capsule の線）、clip、offscreen の W と readback、host の一致の試験の土台、shader の確かめ（i915・bcm2711）。CPU の線を capsule に（G4） | 6 LW | p001 |
| p003 | 文字（RGBA8 の atlas の頁、色の glyph）と画像（texture の cache、`kl_image_changed`、`kl_canvas_image_part`）、edge の pipeline と polygon、`fill_replace` | 5 LW | p002 |
| p004 | window の frame（`kl_window_canvas`・`frame_begin`・`frame_end`、W と swapchain の copy、incremental present、fallback E1）、`KL_VERSION` 89、最初の app（Settings）を移す | 4 LW | p003 |
| p005 | libkeiland の widget・chooser・picker（SHM→Vulkan）と app の第 1 組（Files・Phone・Mailer・Photos・Music・Calendar の 2D・Video Player の UI・IME の popup）を移す | 4 LW | p004 |
| p006 | 自前の CPU の canvas の app（Text Editor・PDF Viewer）と `KL_PRESENT_NONE` の overlay（Image Viewer・Notes）を移す | 4 LW | p004 |
| p007 | T1 の QEMU の回帰（Venus・llvmpipe）、Pi 4 の実機の確認（ユーザー）、frame の時間の比べ | 1 LW＋待ち | p005、p006 |
| p008 | 規約の全文の見直し | 1 LW | p007 |

計 約 25 LW。G3 で Calendar の 3D・Video Player の GPU の色の変換を WS210 に入れるなら、p006 の後に各 2〜3 LW の Phase を足す（計 約 30 LW）。

## 判断（Q1 経由でユーザーへ、推し付き）

| # | 問い | 選択肢 | 推し |
| --- | --- | --- | --- |
| G1 | app の API の変え方 | (a) 描画の口は同じ、window の frame の口（A3）を足し `kl_canvas` に back end を足す（KL_VERSION 89、app は resize と present の 2 箇所の変更）、(b) app の pixels の契約を残して library の中だけで GPU にする（不可能: app が描いた画素を GPU に渡すには CPU の画素が要る） | (a) |
| G2 | CPU の back end を残すか | (a) 残す（Vulkan の無い時の fallback、offscreen の一度だけの絵、host の試験の基準）、(b) GPU だけにする（offscreen の絵も GPU、試験の基準を失う） | (a) |
| G3 | 範囲: 画素を直に触る app | (a) Text Editor・PDF Viewer・Image Viewer と Notes の overlay は WS210、Calendar の 3D と Video Player の色の変換も WS210 の後の Phase（計 約 30 LW）、(b) 後の 2 つは別の WS（Calendar・media の WS）、(c) WS210 は libkeiland と `kl_canvas` の app だけ | (a)（WS210 の目標「app の frame の経路で CPU が画素を触らない」を一つの WS で閉じる） |
| G4 | 線の端の形 | (a) GPU と CPU を capsule の SDF にそろえる（端が少し滑らかに変わる）、(b) CPU の 8 点の近似を GPU でも polygon で描く（線ごとに三角形が多く、edge の pipeline になる） | (a) |
| G5 | Pi 4 の完了の条件と WS141 | (a) WS210 の完了は「app の側の CPU の画素の処理 0、Pi 4 で WS210 の前より速い（数で）」とし、60 fps は WS141 の案 (A)・TFU（client の窓の image の CPU の変換を消す）と合わせた目標にする。Q1 が P3 と案 (A) の Phase を決める、(b) WS210 の完了を WS141 の案 (A) を待って 60 fps で判定 | (a) |

## 未確認（p002 の最初に）

- bcm2711 の compiler が B2 の shape の shader（SDF・扇の判定・clip の step の積）を 64 register の中に収めるか。収まらなければ shape を 2 つの variant（角丸の塗り・枠／影・ring・line）に分ける。
- i915 の F-023 の項目（OpSwitch・OpCompositeInsert・strip・`vkFreeDescriptorSets`）の今の状態。設計は全部を避けるので、結果は B3 の pool の捨て方だけに効く。
- 頂点の buffer の大きさ（1 frame に 2000 形で約 1.2 MB）の bcm2711 での書き込みと submit の時間。
- lavapipe の offscreen の readback を host の試験の CI の外で使えるか（libbrowser の component の試験は使っている）。
