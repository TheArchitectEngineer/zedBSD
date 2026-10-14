<!-- awesome-plan project=zedbsd record=ws037-p007 -->

# ws037-p007: 3D の投入（段 P9g: 最初の draw）

Status: in-progress（2026-10-15 P3: 7a・7b・7c を実装、build と host 試験まで。実機 R7d は T 待ち（下の 7 節））
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

## 6. 実装（2026-10-15 P3、7a・7b・7c）

設計の案（1〜5 節）からの変更:

- **channel は P9c と共有**（危険 4 の「共有」の側）: 3D の object（0xc597、handle `0x97000003`）を P9c の GR の channel（chid 3）に作り、subchannel 0 に bind する（compute は subchannel 1、NVK と同じ）。arena・vaspace・promote を二重に持たないため。したがって `nvrtx.draw=1` は `nvrtx.compute=1` の後でしか走らない（P9c の結果の確かめの後に P9g）。
- **render target は system memory と VRAM の両方**（危険 2 を 1 回の run で答える）: 同じ quad を (1) system memory の pitch-linear 256×256、(2) VRAM の 256×256 に描く。VRAM の target は P1 の VRAM の allocator（P4m、main に入った `nvrtx_bo_alloc`）の buffer object で、BAR1 に map できれば CPU が全 pixel を読み、できなければ arena の最後の 1 MiB を使い register window で 9 点を読む。
- **7c も同じ stage で**: (3) texture（4×4、pitch 64、赤・緑・青・白の 4 象限、nearest・clamp）を貼った quad を system memory の target に描く。TIC は NIL の Maxwell 以降の pitch の header、TSC は NVK の sampler の欄（MIT）。
- 色: clear はシアン（0, 1, 1, 1 → `0xffffff00`）、単色の quad は黄（1, 1, 0, 1 → `0xff00ffff`）、描く前の target は `0xdeadbeef` で埋める。quad は NDC (−0.5, −0.5)〜(0.5, 0.5) で pixel 64〜191。
- 3 回の描画は ring の entry 1〜3 の別の submission で、fence は 0x4243〜0x4245（P9c の semaphore の page）。fence が来なければそこで止める。pixel の違いは全部の描画を流してから最初の失敗を返す。

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/nvrtx/nvrtx-draw.h`・`draw-push.c`（新） | hardware に触れない部分: `nvrtx_push_draw_init`（NVK の `nvk_push_draw_state_init` から macro の要る物（SET_PRIV_REG、MME の scratch）を除いた順と値）・`_constants`・`_target`（colour target 0 の pitch、残り 7 つは NVK の null target、viewport・raster・sample・depth・colour の state）・`_pipeline`（type 0〜5、vertex 1・pixel 5、group 0・4、SUBTILING など）・`_vertices`・`_textures`・`_clear`・`_triangles`（SET_INSTANCE_COUNT・BEGIN・SET_VERTEX_ARRAY_START・END）、`nvrtx_draw_texture_header`・`nvrtx_draw_sampler`、quad の頂点と fragment の IR（単色と texture） |
| `src/drivers/gpu/nvrtx/draw.c`（新） | 段 P9g（`nvrtx_draw_buffers`・`_pages`・`_start`・`_stop`・`_release`） |
| `compute.c` | draw の buffer の確保、tables への draw の page（`0x28010000` から 78 page）と VRAM の target（`0x28100000`、256 KiB）の map、P9c の確かめの後に P9g、stop で 3D の object を compute の object より先に free。ring の投入と fence の待ちを `nvrtx_compute_submit`・`nvrtx_compute_wait` に分けた（P9c の動きは同じ） |
| `mmu.c`・`nvrtx-gsp.h` | `nvrtx_mmu_gr_map_vram`（VRAM、非 privileged・書き込み可、足しただけ） |
| `nvrtx-gpu.h`・`stage.c`・`nvrtx-private.h` | `struct nvrtx_draw_state`、handle、VA の約束、`nvrtx.draw=1` |
| `platform/amd64/vmunix.mk` | NVRTX=y の時だけ 2 file |
| `plan/ws037/tests/config-amd64-nvrtx.mk` | `NVRTX_RUN=R7d`（`nvrtx.gsp=1 nvrtx.gr=1 nvrtx.compute=1 nvrtx.draw=1`） |

出典（MIT、Mesa 25.3.6、sha256）: `headers/nvidia/classes/clc597.h`（4b47ff26…e39d）、`clc597tex.h`（15f53cf3…2595）、`cl9097tex.h`（0b1bc445…1162）、`vulkan/nvk_cmd_draw.c`（1efce360…f927）、`nvk_cmd_clear.c`（2eee3340…3515）、`nvk_shader.c`（57d46463…a0）、`nvk_sampler.c`（119f8e76…cb85）、`nil/descriptor.rs`（b46b470d…369e）。file の先頭の注記に全文の hash。

### 6.1 確かめ（host と build）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-sm75-host-test.sh` | `150 checks, 0 failures`（stage C までの分に加えて: draw の push を decode して clc597.h の method の番号で 40 項目（SET_OBJECT、colour target 0 の 9 語、null target、viewport の 128.0、pipeline の type と address と group、cbuf 0、vertex stream、texture の pool、clear、BEGIN・6 頂点・END、全 header が subchannel 0 の incrementing）、TIC・TSC の欄を clc597tex.h の位置で、draw の 4 つの program を IR から compile して interpreter で走らせる（頂点の位置、texture の座標、単色、sample）、`nvrtx_mmu_gr_map_vram` の entry） |
| `sh plan/ws037/tests/nvrtx-gsp-host-test.sh` | `PASS nvrtx gsp: 222 checks`（main の取り込みの後） |
| `make -j16 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk BUILD=build/ws037-p007 build/ws037-p007/vmunix` | rc 0、`warning:` 0 行、`kernel include check: PASS`、`amd64 vmunix check: PASS`。既定 n の build は `vmunix.mk` の NVRTX の `ifeq` の中だけの変更なので変わらない |
| `git diff --check` | 出力なし |

未実施: 実機（R7d）。push の method の順と値は NVK の読みに基づくが、MME の macro を通す NVK の state（SET_PRIV_REG の 2 つ、anti-alias の macro、shadow scratch）は送っていない（危険 1）。

## 7. T への手順（R7d、方式 B、R7c の image の系列に足す）

前提: R7c（P9c）が合格。合格していなければ R7d は流さない（P9g は P9c の確かめの後でしか動かない）。

image: `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r7d NVRTX_RUN=R7d`（main の merge の commit で）。run の前に host の reboot と `prepare.sh --check`。判定は今までどおり画面の PNG（`dmesg | grep nvrtx`）。

合格の印（R7c の `P9c ok: compute wrote 32 words of 42` の後、P8 の前に）:

1. `P9g begin`、`P9g VRAM target at … (buffer object)`（P4m が無ければ `(compute arena)`）
2. `P9g ok: 3D object on the compute channel`
3. `P9g program 0..3: N instructions, M registers`（4 行）
4. `P9g ok: fence 0x4243 of the system memory draw after N us` → **`P9g ok: draw wrote the system memory target (65536 pixels right)`**
5. `P9g ok: fence 0x4244 of the VRAM draw after N us` → **`P9g ok: draw wrote the VRAM target (65536 pixels right)`**（BAR1 が無い時は `(9 points right)`）
6. `P9g ok: fence 0x4245 of the texture draw after N us` → **`P9g ok: draw wrote the texture target (65536 pixels right)`**
7. `P9g ok: three draws checked`
8. その後は R7c と同じ（P8a、`P6 ok: channel and vaspace freed`（`P6: N compute frees failed` が無い）、`P7 ok: region torn down`、login prompt）

部分合格の読み方: 4 が fail で 5 が ok なら ROP は system memory に書けない（危険 2、以後 VRAM に描いて copy）。6 だけ fail なら TIC・TSC か texture の命令。

FAIL の時に PNG に残す物: `P9g fail: … target (x, y) is X, want Y`（最初の違う pixel）と `… N of 65536 pixels wrong, middle X`（`0xdeadbeef` のままなら何も書かれていない、`0xffffff00` なら clear だけ通った）、`P9g fail: fence … did not come (semaphore …)`、`rm: class … status …`（0xc597 の確保）、`P6 channel error: …`（RC の event）。`P7 fail` なら host を reboot。
