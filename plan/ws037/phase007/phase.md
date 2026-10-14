<!-- awesome-plan project=zedbsd record=ws037-p007 -->

# ws037-p007: 3D の投入（段 P9g: 最初の draw）

Status: planning（2026-10-15 P3: 設計の案。実装は Q1 の指示の後）
Disposition: normal
Parent: [WS037](../ws.md)
依存: [p008](../phase008/phase.md) の段 E（頂点・fragment の program と SPH、2026-10-15 実装・host 済み）、段 P9c（compute の投入。2026-10-15 実機 R7c PASS: GR の channel・promote・QMD・fence の道が実機で動いた）、P5g（golden context）
範囲: 最初は **段 P9g**（boot の語 `nvrtx.draw=1`）だけ。`drv_gpu_ops` への統合・Venus の executor（Keiland が Vulkan で描く道）は、この段が実機で通った後の別の段にする。

## 1. 目標と合格

compositor の quad（`userland/desktop/wayland/shaders/quad.vert`・`quad.frag`）と同じ形の program を nvrtx の compiler で作り、GR の channel の 3D の class（TURING_A 0xc597）で、**system memory の pitch-linear の render target（256×256、A8B8G8R8）** に次の 2 つを描いて CPU が読み戻す。

1. `CLEAR_SURFACE` で全面を単色（例 0xff204080）に。
2. texture を使わない単色の quad（fragment が push constant の色を出す program）を中央 128×128 に。
3. （次）texture を貼った quad: texture header pool・sampler pool を作り、sysmem の小さい texture（4×4）を sample する。

合格: 読み戻した render target の中央が quad の色、外側が clear の色、境界が viewport の計算どおり（左上の原点、pixel の中心は半整数）。印 `P9g ok: draw wrote …`。

GOP の framebuffer（P1 の P8b の surface）に描くのは、この後（P1 と相談）。

## 2. 流れ（NVK の 3D の state の初期化と draw と同じ順、MIT）

P9c と同じ GR の channel の作り方（外部所有の vaspace、promote、BIND・SCHEDULE）に、compute の object の代わりに 3D の object（0xc597）を作る（P9c の channel を共有して両方の object を作ることもできるが、最初は分ける）。push buffer（subchannel 0 に 3D、NVK と同じ）:

| 順 | method（clc597.h、MIT） | 値 |
| --- | --- | --- |
| 1 | SET_OBJECT | 0xc597 |
| 2 | NVK の `nvk_push_draw_state_init` の最小の部分: SET_RENDER_ENABLE_C（TRUE）、SET_CT_SELECT（1 target）、SET_SHADE_MODE、SET_API_VISIBLE_CALL_LIMIT、SET_L1_CONFIGURATION、CHECK_SPH_VERSION（3/3）・CHECK_AAM_VERSION、SET_ATTRIBUTE_DEFAULT、SET_BLEND_STATE_PER_TARGET、SET_COLOR_CLAMP、SET_POINT_SIZE、SET_VIEWPORT_PIXEL（半整数）、SET_WINDOW_ORIGIN（左上）、SET_VIEWPORT_CLIP_CONTROL、scissor・window clip を無効 | NVK の値。MME の macro は使わない（NVK は draw を MME で出すが、我々は BEGIN・SET_VERTEX_ARRAY_START・DRAW_VERTEX_ARRAY・END を直に書く） |
| 3 | SET_COLOR_TARGET_A..G(0)（0x800〜）、SET_SURFACE_CLIP_HORIZONTAL・VERTICAL、SET_ZT_SELECT 0 | render target の address・幅・高さ・format（A8B8G8R8）・pitch の layout |
| 4 | SET_VIEWPORT_SCALE_X/Y/Z・OFFSET_X/Y/Z(0)（0xa00〜） | NVK の `nvk_flush_vp_state` の計算 |
| 5 | SET_CONSTANT_BUFFER_SELECTOR_A/B/C（0x2380）、BIND_GROUP_CONSTANT_BUFFER(group)（0x2410 + 32g） | cbuf 0（push constant と texture handle、[p008](../phase008/phase.md) 3.2 と 10 節の約束）を頂点と fragment の group に |
| 6 | SET_PIPELINE_SHADER(i)（0x2000 + 64i）、SET_PIPELINE_PROGRAM_ADDRESS_A/B(i)（0x2014）、SET_PIPELINE_REGISTER_COUNT(i)（0x200c）、SET_PIPELINE_BINDING(i)（0x2010） | 頂点（type VERTEX、i = 1: VERTEX_CULL_BEFORE_FETCH は 0 で無効、NVK の index の割り当てに従う）・fragment（type PIXEL）。program の address は SPH の先頭（code は +0x80） |
| 7 | SET_VERTEX_STREAM_A_FORMAT・LOCATION_A/B(0)（0x1c00〜）、SET_VERTEX_ATTRIBUTE_A(0)（0x1160） | quad の 6 頂点の corner（vec2 の float）を sysmem の vertex buffer から |
| 8 | CLEAR_SURFACE（0x19d0）と SET_COLOR_CLEAR_VALUE（0xd80〜） | 1 の clear |
| 9 | BEGIN（0x1618、TRIANGLES）、SET_VERTEX_ARRAY_START（0xd74）= 0、DRAW_VERTEX_ARRAY（0xd78）= 6、END（0x1614） | draw |
| 10 | host の fence（P5・P9c と同じ semaphore の release と WFI） | — |

texture の段（1 の 3）では SET_TEX_HEADER_POOL_A..C（0x1574）・SET_TEX_SAMPLER_POOL_A..C（0x155c）と、NVK の `nil` の texture header（TIC）・sampler（TSC）の形（`src/nouveau/nil/`、MIT）を足す。

## 3. 置き場所（P9c と同じ考え）

- VRAM の arena（P9c の下か、P9c の arena を広げる）: page table・instance・USERD・context buffer。
- system memory（volatile）: ring・push・semaphore・program（SPH＋code、頂点と fragment）・cbuf 0・vertex buffer・render target（256×256×4 = 256 KiB、64 page）。render target を sysmem に置くのは CPU が読み戻しやすいため（ROP が sysmem の pitch に書けるかは未確認、危険 2）。

## 4. 実装の単位

| 単位 | 中身 | 確かめ |
| --- | --- | --- |
| 7a | host の部品: 3D の push の組み立て（`nvrtx_push_draw_init`・`_target`・`_viewport`・`_program`・`_vertex`・`_clear`・`_draw`）、単色の fragment の IR | host 試験（method の header と値の語の照合、clc597.h の位置で読む） |
| 7b | 段 P9g（`draw.c`、`nvrtx.draw=1`）: channel・3D の object・program の upload・draw・fence・読み戻し | build、実機（T、R7d = R7c + `nvrtx.draw=1`） |
| 7c | texture の quad（TIC・TSC の pool） | 同 |

## 5. 危険と未確認

1. NVK は draw の前に多くの state を MME の shadow で持つ。直の method で足りない state（例 zcull、anti-alias、sample mask）は、実機で何も描かれない時に NVK の init を全部写して切り分ける。
2. ROP が system memory の pitch-linear の render target に書けるか（NVK は VRAM と block-linear が普通）。だめなら VRAM に置き、copy engine（P5e の道）で sysmem へ写して読む。
3. 頂点の program の index（SET_PIPELINE_SHADER の i）は NVK の割り当て（VERTEX_CULL_BEFORE_FETCH 0、VERTEX 1、…、PIXEL 5）に合わせる。
4. GR の channel を P9c と分けるか共有するか（共有なら compute と 3D の subchannel を NVK と同じく 1 と 0 に）。
