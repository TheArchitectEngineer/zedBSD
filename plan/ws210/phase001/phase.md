<!-- awesome-plan project=zedbsd record=ws210-p001 -->

# ws210-p001: Keiland の app の描画を GPU へ — 調査と設計

Parent: [WS210](../ws.md)
Status: in-progress（P1。設計 1.2: review-1 の指摘を全部反映（1.1）、Q1 の条件（1.2）。H1〜H3 の再確認は Q1）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来と入力

- 2026-10-12 ユーザー:「Keiland の app の描画を GPU に、は、新WSで進めてください。着手OKです。」方針:「PI4のような低消費電力のモバイルSoCでは、画像をCPUで触ると、体験を損ないます。すべてGPUで処理する必要があります。」（[Guardrail](../../guardrail.md)「画像の処理は GPU で」: CPU が画素を触るのは file の decode と、起動の時・内容が変わった時の一度だけの用意に限る。毎 frame は GPU）。
- 2026-10-12 ユーザー「承認します。」: 判断 G1〜G5 は全て推しの (a)（[WS210](../ws.md) の末尾）。
- [ws141-p013](../../ws141/phase013/phase.md)（P3）の調べ: Pi 4 では client の frame ごとに、app の CPU の描画、canvas の行の copy、kernel の raster→UIF の CPU の変換（submission あたり 25〜180 ms）が起きる。WS141 の ws.md（2026-10-12）: cursor の plane は 60 fps、窓の中身は約 15 fps（CPU の texture の変換が主因）。
- P3 の所見（2026-10-12）: bcm2711 の shader compiler の出力は約 7 割が NOP（全命令の後に 2 つの NOP、add と mul を組まない）、blend を shader の中で計算している（hardware blend を無効）。P3 が hardware blend と NOP の削減を進めている。libc の memcpy が 1 byte ずつ（[BUG-291](../../bugs/BUG-291.md)、P2）。
- この Phase は調査と設計だけ。source は変えない。調べは agent/p1 = main 9f1e501c1 の tree（第 1 版は ede36dc65）。

## 版

| 版 | 日 | 内容 |
| --- | --- | --- |
| 1 | 2026-10-12 | 第 1 版（P1 第 1 generation） |
| 1.1 | 2026-10-12 | [review-1](review-1.md) の H1〜H3・M1〜M11・L1〜L8 を反映（P1 第 2 generation）。直した点は末尾の表 |
| 1.2 | 2026-10-12 | Q1 の確認（merge 0a53708b9 の後）: polygon の coverage の cache の条件（形の key、毎 frame 変わる形を CPU で毎 frame 作らない）→ 三角形は GPU の triangle の pipeline、他は subpixel を 1/4 px に揃えた key。動く polygon の使い手を列挙。`kl_surface_canvas_*` の OS 境界・3 OS の build・exports の確かめ |

## 今の tree の事実

### libkeiland の描画

| 所 | 事実 |
| --- | --- |
| `ui/canvas.c`（1468 行） | `struct kl_canvas`（公開、`keiland.h` 2891）は app の `uint32_t *pixels`・stride・大きさ・clip の stack（16）・polygon の 1 行の coverage を持つ。app が自分の memory に置き `kl_canvas_init` が `memset` で埋める（`canvas.c:76`）。描画の口は 17: `clear`（clip の中を透明に）・`fill`（整数の矩形、AA 無し）・`gradient`（縦、`(py - y) / (h - 1)`）・`round`・`round_gradient`（`(py + 0.5 - y) / h`）・`round_border`・`shadow`（smoothstep）・`circle`・`ring`（12 時から時計回りの扇、扇の端は AA 無しの `atan2`）・`polygon`（nonzero、96 点まで、1 画素の行を 4 本の sub-row で走査）・`line`（丸い端を 8 点の polygon で近似）・`mask`（8 bit の coverage、glyph）・`image`（bilinear、角丸、不透明度）・`kl_image_create`/`release`/`scale`・`kl_color_mix`（重み `(unsigned)(amount * 256)`、channel ごとに `>> 8` で切り捨て）。色の引数は straight alpha、canvas の画素は premultiplied の 0xAARRGGBB。丸い形は signed distance（画素の中心で 0.5 の幅の 1 画素の AA） |
| `ui/text.c`（843 行） | libtruetype で glyph を大きさごとに一度だけ 8 bit の coverage に rasterize し、`kl_text` の hash の cache に持つ。4 分の 3 が埋まると `text_clear` が全 slot の bitmap を `free` して空にする（`text.c:656`・`807-809`）。色の emoji は premultiplied の画素。描画は glyph ごとに `kl_canvas_mask`、色の glyph は stack の上で組んだ `kl_image`（`text.c:283-289`）で `kl_canvas_image`。kerning・shaping は無い |
| `ui/present.c`（1296 行、`keiui_present`） | window ごとに VkInstance・device・Wayland WSI の swapchain（最小 3 枚）・LINEAR の host-visible の canvas の image を作る。swapchain の形式は surface が出す順の最初の 8 bit UNORM（`present.c:503-508`）で、libvulkan の Wayland は **R8G8B8A8 を先に**出す（`libvulkan/wsi-wayland.c:349-356`）→ 今の swapchain は R8G8B8A8。canvas の image は B8G8R8A8（`present.c:890`）。frame ごとに CPU の pixels の変わった行を canvas の image へ `memcpy`、swapchain の image に canvas を 1 枚の quad で描き、`VK_KHR_incremental_present` で変わった所を知らせ、fence を待つ（1 frame in flight）。acquire は CPU の copy の後（`present.c:241-256`） |
| libvulkan の Wayland の present | swapchain の image ごとに app の image と共有の image の 2 つの allocation（BUG-290）。present は app の image → 共有の image を GPU の `vkCmdCopyImage` の全体の copy で写し（`wsi-swapchain.c` `present_copy_shared`）、compositor は共有の image を sample する |
| `ui/present-shm.c` | Vulkan の無い所と library の小さな window（file chooser・media picker）は wl_shm の 2 枚の buffer に CPU で copy |
| 公開の契約（`keiland.h` 3670〜） | 「A frame is ordinary memory of premultiplied 0xAARRGGBB words」。app が pixels を `malloc` し `kl_canvas_init(pixels)`、描いて `kl_window_present_part(window, pixels, stride, part)`（EAGAIN は out of date・suboptimal、EIO は他の失敗）。`KL_PRESENT_VULKAN`・`_SHM`・`_NONE`。`struct kl_present_times`（copy・acquire・present・wait の ms）も公開。今の版は `KL_VERSION 88` |
| 部分の再描画 | app は `kl_ui_take_damage` の part を clip に push して描き、残りの画素を前の frame のまま使う（BUG-221・226）。今は CPU の pixels と canvas の image がその「残り」を持つ |
| DPI | libkeiland に DPI の倍率を掛ける所は無い（Guardrail「寸法の単位」: px は既定の DPI の論理 px） |

### 使う側（数は `grep` の行数）

| 使う側 | 描画の作り | 毎 frame の CPU の画素の処理 |
| --- | --- | --- |
| libkeiland の widget（`ui.c`・`widgets.c`・`cards.c`・`icons*.c`・chooser・picker など） | `kl_canvas_*`・`kl_text_draw*` だけ。画素を直に触らない | canvas の全部 |
| Files・Settings・Phone・Mailer・Photos・Music・Calendar・Video Player の UI | `KL_PRESENT_VULKAN`。`kl_canvas_*` 約 500 箇所（round 207・circle 60・round_border 60・fill 55・line 36・shadow 26・polygon 16・image 16・clear 11・round_gradient 8・gradient 7・ring 7・mask 3）、`kl_text_draw*` 約 340 箇所 | canvas の全部 |
| **画素を直に書く app の code**（review-1 M2） | Settings `ui.c:1877`・Files `ui.c:1411`・`ui-desktop.c:827` が `canvas->pixels` の行を `memset`（透明に）。Calendar `view.c:1539`（`view_keep_under`）が普段の描画で canvas の矩形を `kl_image` に読み、`view.c:634` で書き戻す | 再描画ごと |
| **mark の layer**（review-1 H2） | Settings `widgets.c:209-231`・Files `ui-home.c:604-625` が `static` な配列に `kl_mark_raster` で coverage を書き、大きさが変わると同じ配列に書き直して `kl_canvas_mask` | 大きさが変わった時 |
| **その場で書き換える `kl_image`**（review-1 M4） | Files `ui-home.c:642`（hero の絵を `kl_canvas_init(image->pixels)` で描く）、Calendar `view.c:1588`（page の絵）、Calendar の `under`。別の thread で作る・捨てる物: Settings `look.c:686`（thumbnail を worker で作る）、picker `picker-model.c` `model_make`（`kl_image_release` が worker に居る） | 内容が変わった時 |
| Text Editor・PDF Viewer・Image Viewer | 自前の CPU の canvas（`te_canvas`・`pv_canvas`・`iv_canvas`、`canvas->pixels` を直に書く）。PDF Viewer の bar・Text Editor の handles は `kl_canvas` を自前の memory に。Image Viewer は自前の Vulkan（`KL_PRESENT_NONE`）で画像の quad＋CPU の canvas の overlay | 文字と UI の全部、PDF の page の copy・伸縮 |
| Notes | 自前の Vulkan（stencil の塗り）。toolbar と text box の overlay は CPU の canvas（`box.c:372-460`）→ LINEAR の image | overlay が変わった時 |
| IME の popup（review-1 M2） | `ime/popup.c`: kl_window ではない input popup の surface に自前の wl_shm の 2 枚の buffer と `kl_canvas` | 候補が変わった時 |
| Calendar の 3D（`render3d.c` 644 行） | CPU の三角形の rasterizer（supersample、bilinear の texture） | page をめくる animation の間、毎 frame |
| Video Player | libswscale で decode の絵を frame の pixels へ縮小・色の変換 | 再生中、毎 frame |
| Terminal・System Monitor・Browser | 自前の Vulkan（glyph の atlas・shape の SDF・instancing）。atlas は CPU で一度書く | 無し（対象の外） |
| compositor | `kl_canvas` を使わない。自前の Vulkan | WS141 の範囲 |

### 既存の GPU の 2D の描画（土台の候補）

- `wayland/shaders/panel.frag`: 角丸の SDF・影・ring・文字（atlas の alpha）・画像・blur を mode で切り替え、`-DKWL_PANEL_MODE=n` の 7 つの variant。bcm2711 と i915 で動いている。
- `monitor/render.c`＋`shaders/shape.frag`: 頂点あたり 16 float の非 instancing の shape と glyph の 2 pipeline。i915 で動いている。
- `libbrowser/paint/vulkan.c`: instancing、shelf の atlas、offscreen と readback、CPU の renderer と画素で合わせる設計、`plan/ws074/tests/gpu-compare.py`（host の lavapipe で GPU と CPU を比べる、channel の差 2・画素の 0.1%）。instancing・`gl_FragCoord` を使うので bcm2711 では動かない。
- shader の道具: 各 package の `shaders/regenerate.py` が `glslc --target-env=vulkan1.0 -O`＋`spirv-val` で SPIR-V を作り、`shaders.h` を commit する。i915 の確かめは `plan/ws075/tests/guard/run.sh`、bcm2711 は `plan/ws141/tests/shader-host-test.sh`（compositor の shader、Mesa の oracle を temp に要する。`bcm2711_shader_compile` は oracle 無しで host で link できる: `spirv.c`・`shader*.c`・`qpu.c`）。dev host には glslc・spirv-val・lavapipe がある。

### driver の制約（共通の部分集合を決める）

**bcm2711（Pi 4、V3D 4.2）**（`src/drivers/gpu/bcm2711/`・`src/drivers/gpu/compiler/spirv.c`、読むだけ）:

| 項目 | 事実（主な所） |
| --- | --- |
| 命令 | float の四則・min/max・比較・select・sqrt・rsq・rcp・exp2・log2・floor・fract、整数の加減・bit 演算・shift、`OpConvertUToF`・`OpBitcast`（`spirv.c:3461`・`3498`）、`texture()`。**`if` は if-conversion で両方の block を全 fragment で実行して select**（`spirv.c:20-31`）。length・clamp・mix・step・smoothstep は使える |
| 拒む物 | discard、unroll できない loop、sin・cos・tan・atan 類、微分、整数の乗除・剰余・比較、`textureLod`・bias・texelFetch・offset、vertex shader の texture、location 16 以上の入力（`gl_VertexIndex`・`gl_InstanceIndex`・`gl_FragCoord`）、location 0 以外の出力、inline されない関数、8192 命令超、register 不足（spill 無し） |
| register | 計算に使えるのは 4〜59 の **56 本**。**入力（頂点の属性・varying）は shader の全体で register に pin される**（`shader-lower.c:184-192`・`735-740`）→ varying 24 なら計算に約 32 本。interface は計 32 scalar（`shader.h` `BCM2711_SHADER_INTERFACE_WORDS`）。2 thread の shader には register の余りが要る（`registers_used`・`threads` が binary に出る） |
| varying | flat・noperspective の修飾を受ける（`shader-lower.c:120-131`） |
| pipeline | triangle list だけ、instancing 無し、indexed draw 無し、viewport と scissor は dynamic、depth・stencil・MSAA 無し、color attachment 1 つ、blend は無しか premultiplied（ONE, ONE_MINUS_SRC_ALPHA, ADD）だけ。P3 の所見では blend は shader の中で計算（tile buffer の読み）している |
| 頂点 | **kernel が draw ごとに頂点の全ての消費される scalar を CPU で 1 つずつ写す**（`vulkan-native-draw.c:803-829`、scalar ごとに `kern_memcpy` 4 byte）。費用は 頂点数 × scalar 数 に比例 |
| 入力 | 頂点の属性は R32〜R32G32B32A32_SFLOAT だけ。push constant 128 byte まで。descriptor は COMBINED_IMAGE_SAMPLER と UNIFORM_BUFFER、配列無し |
| image | 2D、4096×4096 まで、mip 1、R8G8B8A8・B8G8R8A8 だけ（R8 は無い）。LINEAR も OPTIMAL も raster。pitch は 64 byte に切り上げ（`vulkan-resource.c:277`） |
| sample | sample する image を raster→UIF に **CPU で**変換。CPU に map されたことの無い image は世代が変わるまで変換の結果を再利用（snapshot の cache、最大 128 MiB）。TFU の code はあるが `hardware.tfu_uif_ready` を立てる所が無く使われていない |
| copy | buffer→image は「全体・1 region・行が 64 byte の倍数・offset 64 揃え」だけ GPU の tile の load/store（`vulkan-native-image.c:690-715`）、他は kernel の CPU の行の copy。image→image は「同じ形式（RGBA8 か BGRA8）・同じ大きさ・全体・pitch 64 揃え」だけ GPU の TLB copy（`copy_whole`、`vulkan-native-image.c:505-535`）。**形式が違えば TLB copy にならず texture の quad（source の CPU 変換付き）**。TLB copy は書き込みの記録（buffer ごとに直近 8 矩形、image ごとの世代）で前の copy から変わった tile だけ（P3、実機の正しさは未確認） |
| render pass | **renderArea の tile（64 px）を全部 load・store する**（`vulkan-native-pass.c:449-454`。scissor では減らない）。loadOp CLEAR は image の全体の時だけ tile の clear、**部分の renderArea の CLEAR は kernel が CPU で塗る**（`vulkan-native-execute.c:291-325`）。LOAD は CPU の処理無し |
| memory | VkDeviceMemory は **device 全体で 256 MiB**（`vulkan-memory.c` `VULKAN_MEMORY_BUDGET`、compositor の import も数える。[BUG-290](../../bugs/BUG-290.md)）。1904×1020 の窓の swapchain は 3 枚 × 2 allocation で約 47 MB |

**i915 native**: bcm2711 より広い（loop・discard・微分・`gl_FragCoord`・instancing は ws075-p004 以降可）。[F-023](../../future-work.md) の OpSwitch・OpCompositeInsert・triangle strip・`vkFreeDescriptorSets` は未確認なので避ける。frontend の制約（関数 1 つ、atan 無し、combined image sampler だけ）は共通。
**Venus**: guest の制約無し。**llvmpipe・Linux・FreeBSD の Mesa**: 標準。

→ **共通の部分集合 = bcm2711 の制約**。shader は直線の code（`if` は両側を払う）、入力の scalar を少なく。

### Linux・FreeBSD

libkeiland の `Makefile.linux`・`Makefile.freebsd` は同じ source を build し、`libvulkan.so.1`（`libvulkan-compat`）に link する。描画は system の driver が標準の Vulkan で行うので、GPU の back end は zedBSD と同じ source で動く。OS の macro は要らない。Linux の `libkeiland.so` は soname で版を分けないので、古い binary が新しい .so で動くことを前提にする（review-1 M3）。

## 設計（1.1）

### 用語

- **GPU の canvas**: window（か surface）に結んだ `kl_canvas`。library が確保する。描画の口は今と同じで、画素を書かずに命令を記録し、frame の終わりに Vulkan の command にする。
- **CPU の canvas**: 今の `kl_canvas`（app の pixels に描く）。残す（G2）。
- **W（window の image）**: window の大きさの GPU の image。**形式は swapchain と同じ**（H1）、OPTIMAL、color attachment と transfer src、CPU に map しない。前の frame の内容を持ち、部分の再描画の「残り」を保つ。
- **context**: app（`kl_app`）ごとに 1 つの VkInstance・device・queue・pipeline・atlas・画像の texture の cache。window で共有する。
- **frame**: `kl_window_frame_begin` から `kl_window_frame_end` まで。記録した命令は frame の終わりに実行され、fence の後に使った物を解放できる。

### A: API の形（G1 (a)、公開の struct は変えない: M3）

A1. 描画の口（`kl_canvas_*` の 17、`kl_text_draw*`、`kl_icon_*`、widget）は名前・引数・意味を変えない。

A2. **公開の struct は変えない**（`struct kl_canvas`・`struct kl_glyph`・`struct kl_image`・`struct kl_present_times`）。GPU の canvas は library が確保する private な struct の先頭に `struct kl_canvas` を置く:

```c
struct canvas_gpu {
	struct kl_canvas canvas;	/* the public part, first */
	float tag;			/* canvas.coverage points here */
	struct canvas_frame *frame;	/* recording state, private */
	...
};
```

GPU の canvas は `pixels == NULL` で、`coverage` が自分の `tag` を指す（`kl_canvas_init` は pixels に NULL を取らず、coverage は calloc の領域を指すので CPU の canvas と混ざらない）。描画の口は先頭で `canvas->pixels == NULL && canvas->coverage == &((struct canvas_gpu *)canvas)->tag` を見て GPU の記録へ分ける（registry も新しい field も要らない）。app は `width`・`height`・`clip` を今どおり読める。`kl_canvas_release` は GPU の canvas では何もしない。glyph の atlas の位置・画像の世代は private な表に持つ。`KL_VERSION` は新しい関数が入る Phase（p005）で次の版に上げ、新しい関数のためだけに使う。

A3. 新しい口（window の frame）:

```c
/* The window's canvas, drawn on the GPU when the window has Vulkan, else on the CPU; valid for the window's life. */
int kl_window_canvas(struct kl_window *window, struct kl_canvas **canvas);
/* Begins a frame: part is what the application will draw again (NULL: all); draw is what it must draw. */
int kl_window_frame_begin(struct kl_window *window, const struct kl_rect *part, struct kl_rect *draw);
/* Shows the frame drawn since kl_window_frame_begin. */
int kl_window_frame_end(struct kl_window *window);
/* The last frame's times and counts (a new struct; kl_present_times is unchanged). */
void kl_window_frame_times(const struct kl_window *window, struct kl_frame_times *times);
```

- `kl_window_canvas` の pointer は window の一生の間変わらない（resize・device の失い・CPU への切り替えでも、M6）。大きさは resize の後の `frame_begin` で `width`・`height`・`clip` が新しくなる。
- `frame_begin` は acquire しない（M6: CPU の記録が compositor を待たない）。戻り値 0。`draw` は普通は `part`（W の大きさで切った物）で、W が新しい時（初めて・resize・device の失いからの回復・CPU への切り替え）だけ全面。EINVAL: frame の中で再び呼んだ、canvas が無い。
- `frame_end` は記録を実行し、acquire（frame の記録の後）→ W から swapchain の image への copy → present。戻り値: 0（出た）、**EAGAIN**（swapchain が out of date・suboptimal: W には描けている。app は今の `kl_window_present_resize` と同じく大きさを確かめて次の frame を出す。resize で W が変われば次の `draw` は全面）、**EIO**（device の失い・memory の不足: library は context を作り直し、できなければ CPU の canvas に切り替える。次の `draw` は全面。`KEILAND PRESENT_FAILED` の行（BUG-290）を 1 回出す）。
- `kl_window_present`・`_part`・`kl_canvas_init` は CPU の canvas と fallback のために残す（G2）。
- **IME の popup**（M2）: kl_window ではない surface 用に同じ frame の仕組みを口にする: `kl_surface_canvas_open(struct wl_display *, struct wl_surface *, int width, int height, struct kl_surface_canvas **)`・`kl_surface_canvas_get(…, struct kl_canvas **)`・`kl_surface_canvas_resize`・`kl_surface_frame_begin`・`_end`・`kl_surface_canvas_close`（keiland.h は既に wl_display・wl_surface を宣言している）。内部は kl_window と同じ frame の code。入れる時（p008）に確かめる（Q1 2026-10-12）: OS 境界（`plan/tools/keiland-os-boundary/check.sh`、libkeiland に OS の header・macro を入れない）、`exports.py --check`（exports.map に新しい名前）、zedBSD・Linux（`make keiland-linux`）・FreeBSD の 3 つの build が warning 0、compositor の `nm -u`（B2）に出ないこと。

A4. 画素を読む・書く使い方は GPU の canvas では使えない（`canvas->pixels` は NULL）。移す前に D 節の所を置き換える。

A5. **画像**（H2・M4）:
- `kl_image` は CPU の画素のまま（decode の結果）。GPU の canvas は `kl_canvas_image` の**呼ばれた時に**、cache に無ければ画素を staging へ写し（upload を frame に予約、一度だけ）、以後は texture を使う。**呼び手の pointer を return の後に持たない**（下の不変条件）。
- cache の key は (`pixels` の pointer、width、height、stride、世代)。世代は private な表（pointer → 世代）に持つ。`kl_image_create` と `kl_image_release` が世代を進め・表から外す（free と再 malloc で同じ番地になった画像が古い texture を引かない）。内容をその場で書き換えた app は `kl_image_changed(image)`（新規）を呼ぶ（D 表の所）。
- `kl_image_release`・`kl_image_create` は別の thread から呼ばれる（look.c・picker-model.c）。表と texture の解放は mutex の下の「退役の列」に積み、atomic な epoch を進める。canvas の lookup は epoch が変わっていれば列を先に回収する（frame_begin を待たない）。texture の実の破棄は、それを使った frame の fence の後。
- debug の検出（M4）: 環境変数 `KEILAND_GPU_CHECK=1` の時、frame_end で cache にある画像の内容の hash を取り直し、upload の時と違えば `KEILAND GPU_STALE_IMAGE` を 1 回出す（debug だけの CPU の処理）。
- cache は byte の上限の LRU（M10、下の予算）。その frame で使う texture は pin し、上限を超えても frame の間は捨てない（超過は次の frame_begin で LRU から削る）。4096 を超える画像は一度だけ CPU で縮めた版（`kl_image_scale`）を texture にする。
- `kl_image_create` で作らずに struct を組む使い方: text.c の色の glyph は atlas へ（B4）。Files の hero の切り出し（`ui-home.c:443` の `view`、source の画素の一部を指す）は key の pointer・stride が別なので別の texture になる（一度だけ）。

A6. 新しい描画の口（D 節の置き換えに要る物だけ）:
- `kl_canvas_image_part(canvas, image, source_rect, x, y, w, h, radius, opacity, flags)`: 画像の一部を、filter（bilinear・nearest）と 90 度ずつの回転（`KL_IMAGE_TURN_*`）で描く（Image Viewer・PDF Viewer）。
- `kl_canvas_fill_replace(canvas, rect, color)`: 下を置き換える塗り（PDF Viewer・Text Editor）。GPU では blend 無しの pipeline。
- `kl_canvas_mark(canvas, x, y, pixels, opacity, colours)`: Settings・Files の同じ code（mark の layer、H2）を library に移す。layer の coverage は library が大きさごとに一度だけ rasterize して atlas に置く。

**不変条件（H2）**: 記録と実行の間に呼び手の memory が変わっても・解放されても結果は変わらない。(1) `kl_canvas_mask` は呼ばれた時に coverage を atlas の頁（の CPU の写し）へ写す（text.c と mark は glyph の key で cache され、2 回目からは写さない。それ以外の呼び手は呼ぶたびに写す＝毎 frame なら Guardrail に反するので、D 表で全部を置き換える）。(2) `kl_canvas_image` は cache に無ければ呼ばれた時に staging へ写す。(3) 記録は頂点・atlas の位置・texture の handle だけを持ち、呼び手の pointer を持たない。(4) その frame で使う atlas の頁と texture は fence まで pin する。

A7. **DPI**（L5）: GPU の canvas は記録の関数の 1 箇所で論理 px → device px の倍率（`scale`、この WS では 1.0 固定）を位置・大きさ・半径・太さに掛ける。glyph は device の px で rasterize する（text.c の大きさに `scale` を掛ける所も 1 箇所）。CPU の canvas は今のまま（倍率 1）。

### B: GPU の back end の作り

B1. **記録**: `kl_canvas_*` は frame の命令の列に「pipeline・texture・形の外接の整数の矩形・頂点の値」を足す。頂点は frame の終わりに host-visible・coherent の頂点の buffer（frame ごとに 2 本を交互、足りなければ倍に）へ書く。形ごとに 6 頂点（三角形 2 つ、instancing と index を使わない）。

- **quad の範囲**（L1・H3）: 形の AA の範囲（形＋1 px、影は＋softness）の外接矩形を floor/ceil で整数にし、clip（整数の矩形）と交わらせた矩形を quad にする。clip は quad の切り詰めで済むので **shader に clip の varying は無い**（clip の外の fragment は作られない、clip は整数なので正確）。局所の座標は線形なので、切った quad の頂点で計算し直すだけ。
- **頂点の scalar を少なく**（H3、kernel の写しの費用）: 色は 0xAARRGGBB の 32 bit を 1 つの float の bit に入れ（`uintBitsToFloat`）、vertex shader で `floatBitsToUint`・shift・and・`OpConvertUToF` で 4 成分に戻して flat の varying にする（属性 1 scalar）。形ごとに一定の値（半分の大きさ・半径・太さ）は flat。
- **角丸の内側を分ける**（H3）: round・round_gradient・shadow は、縁から max(半径, 1) px（影は 半径＋softness）内側の、完全に覆われる整数の矩形を fill の pipeline で、残りの帯（最大 4 つの quad）だけを round・shadow の pipeline で描く（CPU の `canvas_inner_span` と同じ考え）。800×600・半径 12 の窓の板で SDF の fragment は約 48 万 → 約 3.5 万。round_border は帯の内側に fragment が無いので、外周の帯（4 つの quad、幅 = 太さ＋1）だけ。

B2. **pipeline は形ごとに分ける**（H3、全ての shader は直線の code、`if` 無し）。varying の数は flat を含めて pin される数:

| pipeline | 描く物 | 頂点の属性（scalar） | varying | fragment の計算 |
| --- | --- | --- | --- | --- |
| fill | `fill`・`gradient`・角丸と影の内側 | 位置 2、t 1、色 2（上・下） | t、上 4・下 4（flat）= 9 | `w = floor(clamp(t) * 256)`、channel ごとに `floor((a(256-w) + b w) / 256)`（`kl_color_mix` と同じ切り捨て、L4）、premultiply。単色は上＝下 |
| round | `round`・`round_gradient`・`round_border`・`circle` の帯 | 位置 2、局所 2、半分 2、半径 1、太さ 1、t 1、色 2 | 局所 2・半分 2・半径 1・太さ 1・t 1・上 4・下 4 = 15 | 角丸の箱の SDF 1 回、`cov = clamp(0.5 - d) - clamp(0.5 - (d + 太さ))`（塗りは 太さ = 大）、色は fill と同じ式 |
| shadow | `shadow` の帯 | 位置 2、局所 2、半分 2、半径 1、softness 1、色 1 | 局所 2・半分 2・半径 1・soft 1・色 4 = 10 | SDF、`a = clamp((d + s) / 2s)`、`1 - a²(3 - 2a)` |
| ring | `ring` | 位置 2、局所 2、半径 1、太さ 1、終わりの向き 2、色 1 | 局所 2・半径 1・太さ 1・向き 2・色 4 = 10 | 円の帯と扇。扇は atan を使わず、12 時の向きと終わりの向きとの外積の符号を select（割合 ½ を境に積と和）。割合 1 は向きを使わない |
| capsule | `line`（G4） | 位置 2、局所 2、線分 2、1/|線分|² 1、半分の太さ 1、色 1 | 局所 2・線分 2・逆数 1・半分 1・色 4 = 10 | 線分への距離、`clamp(0.5 - d)` |
| atlas | `mask`（glyph・mark・polygon の coverage）・色の glyph | 位置 2、uv 2、色 1、k 1 | uv 2・色 4・k 1 = 7 | `color * tex.a + tex * k`（mask は頁に coverage を 4 成分で持ち k = 0、色の glyph は color = 0・k = 不透明度）。sampler は NEAREST（L3） |
| image | `image`・`image_part` | 位置 2、uv 2、局所 2、半分 2、半径 1、不透明度 1 | uv 2・局所 2・半分 2・半径 1・不透明度 1 = 8 | 1 回 sample、角丸の SDF を掛ける（半径 0 なら SDF は常に 1）。filter は descriptor の sampler（bilinear・nearest）、回転は uv の並べ方 |
| replace | `clear`・`fill_replace` | 位置 2、色 1 | 色 4 = 4 | 色をそのまま、blend 無し |
| triangle | 3 点の `polygon`（C2） | 位置 2、辺への距離 3、色 1（3 頂点） | 距離 3（noperspective）・色 4 = 7 | `clamp(0.5 + min(d0, d1, d2))`。辺は外へ 1 px ずらした三角形で描く（1 つの三角形なので継ぎ目の重なりは無い） |

- 4 点以上の polygon は shader を持たない（C2）。
- 頂点の大きさ（6 頂点の合計）: fill 30、round 66、shadow 60、ring 60、capsule 60、atlas 36、image 60、replace 18、triangle 18（3 頂点）scalar。2000 の形の frame で概ね 10 万 scalar（第 1 版の 6×24 = 144 の約 3 分の 1）。
- 出力は location 0 の 4 成分、premultiplied、blend は premultiplied の source-over（replace を除く）。`pixel` の varying（noperspective）、early return・discard・loop 無し。
- 命令数と register は p002 の spike で数え、収まらない・2 thread にならない物は分ける（round を塗り／枠に、など）。逆に draw の費用が大きければ fill を round に、shadow を round に寄せて pipeline を減らす（B3）。両方の向きの判断を数で行う。

B3. **batch**（M11）: 記録の順を基本に、pipeline か texture が変わる所で draw を切る。draw の数を減らすため、命令を前へ動かす並べ替えをする: 命令は、自分の外接矩形と重なる命令を越えない範囲で、同じ pipeline・texture の直近の batch に入れる（後ろから最大 N batch を見る、N は p002 の draw あたりの費用で決める、案 32）。重ならない形の順を変えても画素は同じ。descriptor set は texture ごとに作って cache（`vkFreeDescriptorSets` を使わず、pool ごと捨てる）。

B4. **atlas**（M5・M10・L2・L3）:
- 頁は **256×256 の RGBA8**（R8 が無いので coverage を 4 成分に、256 KiB）、最大 16 頁（4 MiB）、shelf で詰め、glyph の周りに 1 px の空き。sampler は NEAREST（glyph は整数の位置に描かれるので CPU の mask と同じ画素になる）。
- 頁ごとに CPU 側の写し（malloc、device memory ではない）を持ち、新しい glyph を書いた頁は frame の pass の前に**頁の全体**を 1 region で `vkCmdCopyBufferToImage`（行 1024 byte で 64 の倍数、offset 64 揃え: bcm2711 の GPU の tile copy の条件）。
- 頁の世代: その frame で使った頁は pin。空きが無ければ使われていない一番古い頁を空にする。全部が pin なら頁を 1 つ足し（16 を超えてよい、次の frame_begin で戻す）、`KEILAND GPU_ATLAS_FULL` を 1 回出す。
- 頁より大きい glyph・mark（254 px 超）は atlas に入れず単独の texture（画像の cache と同じ扱い）。
- `text_clear` が cache を空にした時は、private な表（slot → atlas の位置）も空にする（atlas の場所は LRU で回収される）。atlas の key は glyph の key（face・大きさ・太さ・glyph の番号）で、bitmap の pointer ではない。
- polygon の coverage（C2）と mark の layer も同じ頁に入る。

B5. **画像の upload**（L2・M10）: staging は (a) 1 MiB の ring（atlas の頁と小さな画像、frame ごとに使い回し）と (b) 大きな画像ごとの一時の buffer（upload の frame の fence の後に解放）。行は 16 px（64 byte）の倍数に揃え、offset 64 揃え、全体 1 region の copy で map しない OPTIMAL の image（B8G8R8A8: 0xAARRGGBB の word の memory の順）へ。

B6. **frame**（M1・M6・H1）:
- `frame_begin`: W が無ければ作る（swapchain の形式、全体の loadOp CLEAR を 1 回 = bcm2711 の tile の clear、CPU の塗りは無い）。`draw` を返す。
- `frame_end`: (1) 命令の列を batch にし頂点の buffer に書く。(2) `vkAcquireNextImageKHR`。(3) 1 つの command buffer に、atlas の頁・画像の upload → W への render pass（**renderArea = `draw` を 64 px に揃えて W で切った物**、loadOp LOAD、scissor = `draw`）→ W から swapchain の image への**全体の** `vkCmdCopyImage`（同じ形式・同じ大きさ: bcm2711 の TLB copy、変わった tile だけ）を記録。(4) acquire の semaphore を TRANSFER の stage で待って submit（render は待たない）、present、fence を待つ（今と同じ 1 frame in flight）。`VK_KHR_incremental_present` は今のとおり `draw` を知らせる。renderArea の揃えは `vkGetRenderAreaGranularity` と 64 の大きい方。
- acquire が OUT_OF_DATE・SUBOPTIMAL: (3) の copy と present を省いて upload と W への pass だけを submit し EAGAIN（W の内容は保たれ、resize が無ければ次の frame で出る）。

B7. **context**: app ごとに 1 つ（`kl_app` が持つ。`kl_app` を使わない window は window ごと）。今の window ごとの instance・device を改め、atlas と画像の cache を共有する。pipeline は形式ごとに作る（普通は 1 つの形式）。

B8. **memory の予算**（M10、Pi の 256 MiB の中）:

| 物 | 大きさ（1 app） |
| --- | --- |
| W | 窓ごとに 窓の大きさ × 4（1904×1020 で 7.8 MB）。swapchain（3 枚 × 2 allocation = 47 MB）は今と同じ |
| atlas | 頁 256 KiB × 最大 16 = 4 MiB（device）＋同じ大きさの CPU の写し |
| staging | ring 1 MiB＋大きな画像の一時（upload の frame だけ） |
| 画像の texture | 上限 Pi（V3D）16 MiB、他 128 MiB（環境変数 `KEILAND_GPU_IMAGE_MIB` で変える） |
| 頂点の buffer | 2 × 256 KiB から（倍に増える、上限 4 MiB） |

W の分だけ窓あたりの memory が今の canvas の image（同じ大きさ、LINEAR）と入れ替わるので、窓あたりの増減は ±0。増えるのは atlas＋画像の cache（Pi で約 21 MiB）。kernel の snapshot の cache（128 MiB、WS141）は別。予算は p006（Pi の計測）で実の値で直す。

### C: 部分の再描画、polygon、線

C1. **部分の再描画**: W が前の frame を持つので、app は今と同じく `part` だけを描けばよい。swapchain の image へは W の全体を copy する（bcm2711 では TLB copy で変わった tile だけ、i915・Venus・Mesa では GPU の数 MB の copy）。swapchain の image の内容が acquire の後に残ることに頼らない。copy は libvulkan の present の app の image → 共有の image の copy と合わせて frame あたり 2 回（どちらも GPU）。
- 不採用: (b) swapchain の image に直接描く（buffer age 式。規格は内容を約束しない）、(c) 矩形の region の copy（bcm2711 では texture の quad になり CPU 変換が付く）。

C2. **polygon**（M7、第 1 版の ear clipping と edge の pipeline をやめる。1.2 で Q1 の条件を入れた）:
- **3 点（三角形）は GPU の triangle の pipeline**（B2）で毎回描く。cache を使わないので、大きさ・位置が毎 frame 変わる三角形（再生の印・♥ の先・折れた角）でも CPU の画素の処理は無い。CPU の canvas の三角形も同じ式（3 辺への距離の最小、`clamp(0.5 + min)`）に変えて両者を一致させる（G4 の capsule と同じ考え、角の画素が今の 4 sub-row とわずかに違う）。
- **4 点以上は coverage を CPU で一度だけ** rasterize して atlas に置き、atlas の pipeline で色を付けて描く（Q1 の確認: Guardrail の「内容が変わった時の一度だけの用意」）。rasterize は今の CPU の算法（4 sub-row、nonzero）そのもの。**形の key**: 点の数＋点の列から外接矩形の原点を引いた値（float の bit、hash と比較）＋原点の subpixel を **1/4 px に揃えた値**＋cache の世代。原点は整数＋1/4 px に丸めて描く（CPU の canvas も同じ丸めをして一致させる）→ scroll・移動では最大 16 通りの key しか生まれず、2 回目から rasterize しない。形そのもの（点の相対の位置・大きさ）が変わった時だけ作り直す。
- **動く polygon の使い手**（今の tree の 16 箇所、2026-10-12 の grep）:

| 使い手 | 点 | 形が変わる時 | 扱い |
| --- | --- | --- | --- |
| Photos `view.c:1529`・`1772`、Music `view.c:1069`・`1113`・`1122`、picker `picker-view.c:414`、icons `icons.c:114`・`283`、Calendar `view.c:1929` | 3 | 大きさの animation があれば毎 frame | triangle の pipeline（cache 無し） |
| Phone `view.c:2463`（受話器、約 50 点） | 多 | 大きさは画面ごとに固定（18・16 px） | cache |
| Phone `view.c:1643`（山、7 点） | 7 | 絵の大きさが変わった時 | cache |
| Settings `glyphs.c:365`・icons `icons-line.c:373`（線の icon の弧の帯） | 多 | icon の大きさと固定の角度、変わらない | cache |
| Settings `page-network.c:1118`（通信量の graph の面） | 多 | 標本が来た時（約 1 秒ごと）。scroll は 1/4 px の key | cache（1 秒に 1 回の作り直し、graph の面の大きさだけ） |
| Files `ui-home.c:675`・`694`（hero の丘） | 8 | 絵を作る時だけ | offscreen の CPU の canvas（GPU の対象外） |

  毎 frame 形が変わる 4 点以上の polygon は今の tree に無い。将来の animation・spinner・drag の形で必要になったら、三角形に分けられる凸の形は triangle の pipeline の列で、それ以外は変化を離散化（大きさを 1 px 刻みの key に）する。frame あたりの rasterize の数を `kl_frame_times` に出し、p007 の移行と p011 の回帰で 4 点以上の polygon の rasterize が毎 frame 起きないことを確かめる（`KEILAND_GPU_CHECK=1` で連続 8 frame の rasterize を `KEILAND GPU_POLYGON_CHURN` として 1 回出す）。
- 不採用: ear clipping＋外周の辺の距離の AA（第 1 版）。凹の多角形で縁が隣の三角形に重なり半透明が 2 回 blend する（M7）、自己交差が近似になる。

C3. **線**: 丸い端の線を capsule の SDF にする（G4）。CPU の canvas も同じ capsule の式に変えて両者を一致させる（p003）。

### D: 画素を直に触る使い方の置き換え（A4、M2・M4）

| 使う側 | 置き換え | Phase |
| --- | --- | --- |
| Settings `ui.c:1877`・Files `ui.c:1411`・`ui-desktop.c:827`（行の `memset`） | `kl_canvas_clip_push(rect)`＋`kl_canvas_clear`（clip の中を透明に、KL_VERSION 62 からある）＋pop | Settings は p005、Files は p007 |
| Settings `widgets.c`・Files `ui-home.c` の mark の layer | `kl_canvas_mark`（A6） | p004（口）、p005・p007 |
| Files `ui-home.c:642` の hero の絵、Calendar `view.c:1588` の page の絵 | 描いた後に `kl_image_changed`（一度だけの用意、CPU の canvas のまま） | p007 |
| Calendar `view_keep_under`（`view.c:634`・`1539`） | 削り、下の領域を描き直す（W が前の frame を持つので、desk の範囲を clip にして背景から描く） | p007 |
| Text Editor（`te_canvas`） | `kl_canvas_*`（fill_replace・round・mask・色の glyph）へ。`round_full` の使い回しは不要 | p008 |
| PDF Viewer（`pv_canvas`・`bar.c`） | page の raster（libpdf の CPU の描画、page か zoom が変わった時だけ）を `kl_image` にし `kl_canvas_image_part` で伸縮。UI と bar は GPU の canvas | p008 |
| Image Viewer・Notes（`KL_PRESENT_NONE`、overlay の CPU の canvas） | app の pass の中に GPU の canvas の命令を記録する口 `kl_canvas_gpu_record(canvas, VkCommandBuffer, VkRenderPass, ...)`（app の device を使う context を app に作らせる、L7 の費用を p008 に）、overlay を GPU の canvas に | p008 |
| IME の popup（`ime/popup.c`） | `kl_surface_canvas_*`（A3） | p008 |
| file chooser・media picker（`KL_PRESENT_SHM`） | `KL_PRESENT_VULKAN` と GPU の canvas に。Vulkan の無い時は今の SHM と CPU の canvas（E1） | p007 |
| Calendar の 3D | 三角形の列を GPU の mesh の pipeline（depth 無し、奥から順、perspective 補正、bilinear） | p009 |
| Video Player | decode の絵（YUV の 3 面か NV12）を texture にし、GPU で色の変換と縮小 | p010 |
| Files の hero・Calendar の page・thumbnail・System Monitor の atlas | offscreen の一度だけの絵。CPU の canvas のまま（Guardrail の範囲）。描いた `kl_image` は B5 で GPU へ | — |
| 他の `kl_canvas_mask` の呼び手 | 今の tree には無い（text.c・mark の 2 つだけ）。新しく足す時は A6 の不変条件 (1) | — |

### E: fallback と OS

E1. Vulkan の device が無い・pipeline が作れない・device を失って作り直せない時、`kl_window_canvas` は CPU の canvas（library が pixels を持つ）を返し、`frame_end` は今の present（canvas の image への copy）か SHM で出す。app の code は同じ。pointer は同じ（A3）。
E2. **CPU の device**（L6）: 物理 device が `VK_PHYSICAL_DEVICE_TYPE_CPU`（llvmpipe・lavapipe）だけの時は既定で CPU の canvas を使う（llvmpipe の fragment は今の CPU の canvas より遅い恐れ、推測）。`KEILAND_CANVAS=gpu|cpu` で変える（host の一致の試験は `gpu` で lavapipe を使う）。QEMU の llvmpipe の回帰は CPU の canvas の経路、Venus は GPU の経路を確かめる。p006 で llvmpipe の時間を測って既定を見直す。
E3. Linux・FreeBSD は同じ source。OS の macro は増やさない。compositor は `kl_canvas` を使わないので、`keiland-os-boundary/check.sh` の B2 の許可の表は変えない（新しい関数が compositor の `nm -u` に出ないこと）。

### F: Pi 4（bcm2711）で残る CPU の画素の処理と WS141 との依存（M8）

| 処理 | WS210 の後 | 消すのは |
| --- | --- | --- |
| app の描画（canvas）、canvas の行の copy | 無くなる | WS210 |
| W への描画、W→swapchain・app→共有の copy | GPU（TLB copy、変わった tile だけ） | WS210（P3 の TLB copy を使う） |
| glyph の atlas の頁・画像の upload | 内容が変わった時だけ（GPU の tile copy） | WS210 |
| 頁・画像の sample の時の raster→UIF の変換 | 内容が変わった時だけ CPU（kernel） | WS141（案 (A)・TFU） |
| **compositor が client の共有の image を sample する時の raster→UIF の変換** | **client の frame ごとに CPU（窓の大きさ）** | **WS141（案 (A)・TFU、または中間の案）** |

- 依存の記録（M8）: WS210 の完了（G5 (a)）は WS141 に依存しない。「Pi 4 で 60 fps に近い」は WS141 の案 (A)（GPU の出力を UIF で持つ）か TFU（`tfu_uif_ready` の gate と実機の確かめ）の後の目標。**持ち主: WS141（P3）、Q1 が Phase を決める**。WS210 の p006（Pi の計測）は、TLB copy の incremental と snapshot の正しさを実機で確かめる（P3 の実機は未確認、v3d-hardware.h:103 の gate）。
- 中間の案（WS141 へ、Q1 経由で提案）: kernel は既に書き込みの記録（`bcm2711_v3d_memory_written` の矩形、buffer ごとに直近 8）を持つので、sample の時の raster→UIF の変換を「前の変換から書かれた矩形の tile だけ」にする。WS210 の後の client の frame は変わった所だけを W→swapchain→共有へ写すので、変換も変わった所だけになる。
- WS210 は render の出力の tiling に依らない作り（W の全体の copy、atlas の頁の全体の copy）にしておく。

### G: 試験（M9）

- **host の一致の試験**（新規、回帰として保ち `plan/tools/` と master.md の Tools に登録を Q1 に頼む）: 同じ描画の列（17 の口・文字・色の glyph・画像・clip・部分の再描画・置き換えの塗り・mark・polygon を全部含む数十の場面、light と dark、重ねた gradient（L4）、**半透明の凹の polygon**（M7））を CPU の canvas と GPU の canvas（dev host の lavapipe、offscreen の W を readback、W の形式を R8G8B8A8 と B8G8R8A8 の両方、H1）で描いて比べる。合格は channel の差 2・画素の 0.1% まで（libbrowser の gpu-compare の基準: GPU の unorm の丸めと blend の差）。polygon は C2 で同じ coverage を使うので同じ基準（許容を広げない）。ring の扇の端（AA 無し）だけ、端の画素で差 255 を 1 列まで許す（atan と外積の境の違い）。差の絵を `build/` に出す。
- **shader の確かめ**（host）: checked-in の SPIR-V を i915 の host の compiler（ws075 の guard の形）と bcm2711 の `bcm2711_shader_compile`（kernel の source を host で link、Mesa の oracle は使わない受理の確かめ）に通し、pipeline ごとの命令数・NOP の数・`registers_used`・`threads` を出す。基準の表を phase.md に残し、増えたら知らせる。
- **Pi の一致と時間**（新規、p002 で作り p006 で使う）: Pi の上で同じ場面を GPU の canvas（offscreen の W）と CPU の canvas に描き、readback して比べる小さな試験の program（lavapipe では bcm2711 の compiler と driver を通らないため）。同じ program が draw の数・頂点の数・pipeline ごとの時間を測る。Pi の実機は P3 が使っているので、置き換えと実行は Q1 経由（P3 かユーザー）。
- **基準を移す前に取る**（M9）: p005 の前に (1) QEMU（Venus・llvmpipe）の移す前の版の PNG（Settings・Files・Phone・Text Editor・PDF Viewer、T1）、(2) Pi の移す前の Settings の frame の時間（`kl_window_present_times` と kernel の perf の行、P3 の測り方）。
- **T1 の QEMU**（p011）: 移した app を開いて画面を撮り、基準の PNG と比べる。frame の時間（`kl_window_frame_times`）。
- **Pi 4 の実機**（p006・p011）: ユーザー（Files・Settings・Phone の操作）と P3 の kernel の perf の行で、client の frame の texture の CPU の変換が窓の共有の image の 1 つだけ（WS141 の後は 0）になったこと、app の CPU の時間が前より短いこと（G5 の数）。

## Phase（案、1.1）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計と review | — | — |
| p002 | **spike**: (1) host で B2 の 9 つの shader を bcm2711 の compiler に通して命令数・NOP・register・thread を数える（P3 の NOP の削減の前と後）。(2) Pi で draw あたりの kernel の費用・頂点の scalar あたりの写しの時間・pipeline ごとの fragment の時間・shader の compile の時間を測る小さな program（Pi の操作は Q1 経由）。(3) 結果で B2 の分け方・B3 の並べ替えの窓・B8 の予算を決め、この phase.md に書く | 1.5 LW＋Pi の待ち | p001 |
| p003 | GPU の canvas の核: context、記録、quad の切り詰め、fill・round・shadow・ring・capsule・replace の pipeline、角丸の内側の分割、batch と並べ替え、offscreen の W と readback、host の一致の試験の土台、shader の確かめ（i915・bcm2711）。CPU の線を capsule に（G4） | 5 LW | p002 |
| p004 | 文字（RGBA8 の atlas の頁、色の glyph）、画像（cache・世代の表・退役の列・`kl_image_changed`・`kl_canvas_image_part`）、polygon の coverage の cache、`kl_canvas_mark`、`fill_replace` | 4 LW | p003 |
| p005 | window の frame（`kl_window_canvas`・`frame_begin`・`frame_end`・`kl_window_frame_times`、W と swapchain の copy、renderArea、EAGAIN・EIO、fallback E1・E2）、`KL_VERSION` を次の版に、基準の PNG と Pi の基準（G 節）、最初の app（Settings、memset と mark を含む） | 4 LW | p004 |
| p006 | **Pi の計測と調整**（L8）: Settings の GPU の canvas の版を Pi で測り（Q1 経由）、pipeline の分け方・batch・予算・llvmpipe の既定（E2）を直す。TLB copy の incremental と snapshot の実機の正しさ（M8） | 2 LW＋Pi の待ち | p005 |
| p007 | libkeiland の widget・chooser・picker（SHM→Vulkan）と app の第 1 組（Files・Phone・Mailer・Photos・Music・Calendar の 2D と under・Video Player の UI）を移す | 5 LW | p006 |
| p008 | 自前の CPU の canvas の app（Text Editor・PDF Viewer）、`KL_PRESENT_NONE` の overlay（Image Viewer・Notes、`kl_canvas_gpu_record`）、IME の popup（`kl_surface_canvas_*`） | 4.5 LW | p006 |
| p009 | Calendar の 3D を GPU の mesh に（G3） | 2 LW | p007 |
| p010 | Video Player の色の変換と縮小を GPU に（G3） | 2 LW | p007 |
| p011 | T1 の QEMU の回帰（Venus・llvmpipe）、Pi 4 の実機の確認（ユーザー）、frame の時間の比べ | 1 LW＋待ち | p007〜p010 |
| p012 | 規約の全文の見直し | 1 LW | p011 |

計 約 32 LW（第 1 版 25＋G3 の 2 Phase 4＋Pi の計測と調整 2＋p007 の増し 1）。

## 判断

G1〜G5 は 2026-10-12 ユーザーの決定で全て (a)（[WS210](../ws.md)）。1.1 の直しは G1〜G5 の範囲の技術の選び方で、新しいユーザーの判断は無い。
- 2026-10-12 Q1（0a53708b9 の merge の後）: (1) polygon を CPU で一度 coverage を作って atlas に置くのは可（Guardrail の「内容が変わった時の一度だけの用意」）。条件: 形の key で cache し、毎 frame 形が変わる polygon の CPU の再生成が毎 frame にならないことを設計に書く（使い手を列挙し、GPU の edge の経路か変化の離散化）→ 1.2 の C2。(2) `kl_surface_canvas_*` は WS210 の範囲（G1 の延長）として可、OS 境界・3 OS の build・exports を確かめる → A3。design-reviewer の再確認は H1〜H3 の直しの部分だけ Q1 がかける。

## 未確認（p002 で）

- B2 の 9 つの shader の bcm2711 の命令数・register・thread（P3 の NOP の削減の前と後）。round（varying 15）が 2 thread に収まるか。
- draw あたりの kernel の CPU の用意と、頂点の scalar あたりの写しの時間（B3 の並べ替えの窓、B2 の pipeline の数の判断）。
- shader の compile の時間（app の起動で 8 pipeline × 2 stage、kernel の中の compile）。
- 色の 32 bit を float の bit で渡す属性が bcm2711 で bit のまま届くか（頂点の写しは scalar の bit の copy なので届く見込み、試験で確かめる）。
- i915 の F-023 の項目の今の状態（設計は全部を避ける）。
- lavapipe の offscreen の readback を host の試験で使えるか（libbrowser の試験は使っている）。

## 1.1 で直した点（[review-1](review-1.md) への答え）

| 指摘 | 直し | 場所 |
| --- | --- | --- |
| H1 W・pipeline の形式 | W と render pass・pipeline を swapchain の形式に合わせる（今は R8G8B8A8）。texture は B8G8R8A8（memory の順）。host の試験で W の両形式を比べる | 用語、B6、G |
| H2 記録と実行の間の memory | 不変条件 (1)〜(4)（呼ばれた時に atlas か staging へ写し pointer を持たない、pin）。cache の key に大きさ・stride・世代、世代は private な表、create・release で世代、別 thread の release は退役の列と epoch。mark は `kl_canvas_mark` で library が持つ。text.c の色の glyph は atlas、`text_clear` で表を空に | A5、A6、B4、D |
| H3 1 本の shader・pin・頂点の写し | pipeline を 9 つに分け（4 点以上の polygon は shader 無し）、clip は quad の切り詰めで varying から外し、色は 1 scalar の属性＋flat、一定の値は flat、角丸の内側は fill。頂点は 2000 形で約 10 万 scalar。p002 の最初に Pi で compile と時間 | B1、B2、C2、p002 |
| M1 renderArea と LOAD | renderArea = draw を 64 px（と granularity）に揃えた物、loadOp LOAD、W の初期化は全体の CLEAR だけ | B6 |
| M2 D 表の不足 | Settings・Files の memset（clip＋clear）、Calendar の under（描き直し）、IME の popup（`kl_surface_canvas_*`）、PDF の bar を足した | 事実の表、A3、D |
| M3 公開の struct | 変えない。library が確保する private な struct の先頭に `kl_canvas`、`pixels == NULL` と coverage の tag で見分ける。atlas の位置・世代は private な表。版は新しい関数のためだけ | A2 |
| M4 世代と書き換え | 世代は private な表、`kl_image_changed`、書き換える所を D 表に、debug の hash の検出 | A5、D |
| M5 atlas の頁 | frame の頁の pin、全部 pin なら頁を足す、頁を超える glyph は単独の texture、cache を空にした時の表 | B4 |
| M6 frame の口 | acquire は frame_end、戻り値 EAGAIN・EIO と全面の再描画、W の作成・resize・device の失いで clear、pointer は一生不変 | A3、B6、E1 |
| M7 凹の polygon | ear clipping をやめ、三角形は GPU の triangle の pipeline（CPU も同じ式）、4 点以上は CPU で一度だけ coverage（今の算法そのもの、1/4 px の key）を atlas に。動く polygon の使い手を列挙。半透明の凹の場面を厳しい基準で試験 | C2、G |
| M8 WS141 との依存 | 持ち主（WS141・P3、Q1 が Phase）、gate、実機の確かめを p006 に、中間の案（書かれた tile だけ変換）を WS141 へ | F、p006 |
| M9 試験 | Pi の上の GPU と CPU の一致の program、移す前の QEMU の PNG と Pi の基準、bcm2711 の compile の数を host で | G、p002、p005 |
| M10 memory | 予算の表、頁 256×256、staging は ring と一時、画像の上限は Pi 16 MiB | B4、B5、B8 |
| M11 draw の数 | 重ならない命令の並べ替えで batch、draw あたりの費用を p002 で測って窓を決める | B3、p002 |
| L1 quad の辺 | AA の範囲の外接矩形を floor/ceil、clip と交わり | B1 |
| L2 staging の整列 | 行 16 px の倍数、offset 64 | B4、B5 |
| L3 atlas の sampler と余白 | NEAREST、1 px の余白 | B4 |
| L4 gradient の式と切り捨て | 形ごとの t の式、`kl_color_mix` の切り捨てを shader で、重ねた場面の試験 | B2、G |
| L5 DPI | 記録の関数の 1 箇所で倍率、glyph は device の px | A7 |
| L6 llvmpipe | CPU の device では既定で CPU の canvas、環境変数で変える、p006 で測る | E2 |
| L7 時期のずれ・gpu_record の費用 | 公開の struct を変えないので版は p005 の 1 回、`kl_canvas_gpu_record` は p008 の見積に | A2、p008 |
| L8 見積 | p007 を 5 LW、Pi の計測と調整の p006（2 LW）、計 約 32 LW | Phase の表 |
