<!-- awesome-plan project=zedbsd record=ws037-p007 -->

# ws037-p007: 3D の投入（段 P9g: 最初の draw）

Status: in-progress（2026-10-15: 段 P9g（7a・7b・7c）は R7d の再試験 T1-656 で実機 PASS。次は desktop までの段 D1〜D7（8 節）。D1 は実装済み、R9w は T 待ち）
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

## R7d の結果（T1-646、T2、2026-10-15、RTX 2070 実機、方法 B rombar=0、main 1844a35e3）

- FAIL。P9c は再び ok（32 words of 42）、`P9g ok: 3D object on the compute channel`、program 0..3 の命令数・register 数の行は出た。
- `P9g fail: fence 0x4243 of the system memory draw did not come (semaphore…`（画面の幅で切れた）、続いて `nvrtx: rpc: firmware error 13: Graphics Exception: Shader Program Heade…`、`ESR 0x405840=0xa0000`、`ESR 0x405848=0x80000`、`P6 channel error: eng 1 chid 3 type 13`。P6 ok・P7 ok。`rm: class` の行なし。VRAM・texture の draw には届かず。
- 読み（Q1、未解析）: 3D の program の SPH（Shader Program Header、vertex・fragment の header）が GR に拒まれた。compute（P9c）は SPH を使わないので通った。次は SPH の field を NVK・NAK（MIT）の値と 1 つずつ比べる。
- PNG: `build/review/t2r7d-3.png`（Q1 の checkout）。
- 再開の条件: SPH の直しの後に R7d を再試験。担当は次に空いた P（P3 は WS236 の Video を優先、2026-10-15）。

## R7d の直し（2026-10-15、P1 の `7ce6e4019`（main の 3f87e673e）と P3 の作業中の patch を P2 が回収）

- 読み: `ESR 0x405840=0xa0000` は `NV_PGRAPH_PRI_DS_HWW_ESR`（open-gpu-doc（MIT）の gv100 `dev_graphics.ref.txt`）の SPH17・SPH19 の誤り、M_PIPE_STAGE（29:24）は 0（NONE）。`0x405848=0x80000` は `DS_HWW_ESR_2` の bit 19（gv100 の文書では未定義の bit）。誤りの番号の意味は公開の文書に無い。
- 直し 1（main に入済み、P1）: fragment の program の header の input map に position の w（IMAP_SYSTEM_VALUES_AB の bit 31）を常に立てる。NAK（MIT）`from_nir.rs` の「Required on fragment shaders, otherwise it cause a trap」と同じ。host 試験は 4 つの program の header を NAK の encode_header の値と語ごとに照合。
- 直し 2（P3 の patch、ここで回収）: draw の push を NVK に合わせた。(a) `SET_PIPELINE_SHADER(0)`（VERTEX_CULL_BEFORE_FETCH）を書かない（NVK は type 1〜5 だけを書き、type 0 は golden context のまま）。(b) `SET_POST_Z_PS_IMASK` = 0（`nvk_shader.c`、post depth coverage 無し）。(c) `SET_VARIABLE_PIXEL_RATE_SHADING_CONTROL(0..15)` = 0（`NVK_MME_SET_SHADING_RATE_CONTROL` の結果）。(d) `SET_HYBRID_ANTI_ALIAS_CONTROL` = 1（1 pass、centroid per fragment、`NVK_MME_SET_ANTI_ALIAS` の結果）。値と method の offset は clc597.h（MIT）で確かめた。
- 他に照合した物（違い無し）: SPH の version 4（NAK は sm ≥ 73 で 4）、`CHECK_SPH_VERSION` 3/3・`CHECK_AAM_VERSION` 2/2（NVK と同じ）、header の type（PS 2・VTG 1）・shader type・MRT・SASS version 1、local memory と CRS の大きさ 0（SM70 の crs_size は 0）、vertex の store request（0x1c..0x1f / 0x21）、output map（position、generic 0 の xy）。

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-sm75-host-test.sh` | `159 checks, 0 failures`（draw の push に 4 項目: type 0 を書かない、POST_Z_PS_IMASK 0、VRS 16 個 0、hybrid AA 1） |
| `make -j16 BUILD=build/p2/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p2/amd64/vmunix`（p2 worktree） | rc 0・warning 0・include check / vmunix check PASS、`git diff --check` 0 |
| 実機（R7d の再試験） | **未実施**。手順は 7 節のまま（image の語は既定 on の後 `nvrtx.gsp=1 nvrtx.gr=1 nvrtx.compute=1 nvrtx.draw=1 nvrtx.disp=0 nvrtx.resident=0`、`config-amd64-nvrtx.mk` の `NVRTX_RUN=R7d`）。FAIL で同じ SPH の誤りなら、`DS_HWW_ESR` の bit の変化（17・19 が消えたか）を記録 |

## R7d の再試験の結果（T1-656、2026-10-15、RTX 2070 実機、方法 B rombar=0、main 63d0532b0）

**PASS**（Q1 の判定）。P9c ok、P9g の 3 つの draw（system memory・VRAM・texture）が全て `65536 pixels right`、`P9g ok: three draws checked`、P6 ok・P7 ok。直し（fragment の header の w の imap、P1 `7ce6e4019`、と draw の push を NVK に合わせた P3 の分、`57c97fd24`）で SPH の Graphics Exception は消えた。段 P9g（7a・7b・7c）は実機で通った。

## 8. desktop までの段（2026-10-15 夜、P2 の洗い出し。Q1 の指示: 方式 B（passthrough）で RTX の上に Keiland の desktop が描けるまで）

### 8.1 今あるもの・欠けているもの（調べた事実）

- Keiland の compositor は描画を libvulkan だけで行い、出力は VK_KHR_display（`userland/desktop/wayland/display.c`・`compose.c`・`output-switch.c`・`cursor-plane.c`）。CPU で合成する道は無い（`wayland/` に software の renderer は無い）。
- libvulkan が node を Vulkan の physical device にする条件は `GPU_CAP_CAPSET | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | GPU_CAP_MAPPING`（`userland/desktop/libvulkan/context.c` 124 行）。display だけの node（今の nvrtx、P8c）は、最初の renderer の node と組まれて複写の present で出る（`wsi-display-nodes.c` 406〜437 行）。
- 方式 B の guest には renderer が無い（QEMU は emulated の `VGA` と RTX の vfio だけ、phase004 の 3.2）。したがって **nvrtx 自身が renderer（Vulkan の executor）を持たない限り、方式 B で compositor は起動しない**（P8c の display の口だけでは足りない）。
- zedBSD の Vulkan の形は design 8.0 のとおり、kernel の中の executor（Kei GPU command protocol、`include/uapi/gpu-op.h`。i915 は `src/drivers/gpu/i915/render/`、約 2.1 万行（video を除く）、V3D は `src/drivers/gpu/bcm2711/vulkan-*.c`）。bcm2711 は i915 の `render/codec.c` をそのまま使っている（`platform/arm64/vmunix.mk`）。driver の間で hardware に依らない層を共有する前例がある。
- compositor が記録する command は少ない（`wayland/`・`libkeiland/` の vkCmd*: BeginRenderPass・EndRenderPass・BindPipeline・BindDescriptorSets・BindVertexBuffers・PushConstants・SetViewport・SetScissor・Draw・PipelineBarrier・CopyImage・BlitImage・ClearColorImage・CopyBufferToImage・CopyImageToBuffer）。他に instance・device・memory・buffer・image・image view・sampler・descriptor・pipeline layout・shader module（SPIR-V）・graphics pipeline・render pass・framebuffer・command pool/buffer・queue submit・fence・semaphore・swapchain（display）。
- nvrtx に今あるのは boot の時の段（P9c の compute、P9g の draw）の部品: driver が持つ vaspace と GR の channel（`compute.c`）、3D の push の組み立て（`draw-push.c`、NVK の初期化と同じ順）、SM75 の compiler（P4、`compiler/`）、VRAM の allocator と BAR1（P4m）、display の口（P8c）。どれも session・job の形ではない。

- renderer の node の条件の詳細（`libvulkan/context.c` 115〜212・`instance.c` 720〜742・`device.c` 277〜281）: `GPU_GET_CAPSET` の id 4 が 156 byte 以上（wire 1、XML 1.3.269 か 1.4.343、timeline ≠ 0）、末尾の vendor の印 `0x5a424453`（"SDBZ"、byte 160/164）と flags `OPAQUE|STRICT_QUEUE|QUIESCE`（i915 は 7、`i915/render/vulkan.c` 39〜42・325〜349）、`GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY`。満たさない node は黙って飛ばされる。memory type に coherent かつ device local の物が要る（`instance.c` 1130）。submit は `GPU_COMMAND_SUBMIT`（NOTIFICATION がある時）と `GPU_COMMAND_WAIT`、`GPU_JOB_CAPACITY`・`GPU_JOB_RESERVE`。
- `ops->scanout` がある driver は roles を `query_device` だけから取る（`gpu.c` 4486〜4499）。nvrtx は今 DISPLAY だけ（`share.c` 189）なので、renderer になる時は RENDER を足す。
- 別の GPU で描いて nvrtx に出す道は**複写だけ**（zero copy の外の import は nvrtx・i915・venus のどれも `import_image`・`get_scanout_backing` が無く ENOTSUP → libvulkan は複写の道 `display_present_native` に落ちる。renderer で `vkCmdCopyImageToBuffer` → `GPU_RESOURCE_WRITE` 64 KiB ずつ → 枠なしの `GPU_DISPLAY_PRESENT`）。nvrtx は SHARED|COPY を出し storage の present を持つので、この道は今の P8c で受けられる。cursor の plane は共有の image だけなので、この道では compositor が cursor を自分で合成する。
- Venus の virtio-gpu（`src/drivers/gpu/venus/`）は stream を host へ渡すが、host の capset が SDBZ の strict の形（168 byte）でなければ JOB を落とし、libvulkan に飛ばされる（`venus.c` 612〜616、`transport.c` 1845〜1895）。**素の QEMU・virglrenderer の Venus では renderer にならない**。8.2 の「代わりの道」は zedBSD 用の host の renderer が要る。
- compositor の状態の範囲: descriptor は COMBINED_IMAGE_SAMPLER だけ、viewport・scissor は dynamic、TRIANGLE_LIST、blend は ONE / ONE_MINUS_SRC_ALPHA、filter は NEAREST・LINEAR、形式は B8G8R8A8・R8G8B8A8 UNORM、頂点は R32・R32G32・R32G32B32A32 SFLOAT、compute と dynamic rendering は無し。shader は `wayland/shaders/` の quad・panel・panel-batch・turn.vert。i915 の executor も dynamic rendering を持たない（render pass と framebuffer だけ）。

### 8.2 段（D1〜D7）。合格の印と host でできる物

| 段 | すること | 確かめ |
| --- | --- | --- |
| D0 | R8b（display を点ける）→ R8c（resident の GPU device）→ R8d（events・cursor・share）。既に実装済み、T の結果待ち | 実機（T1、ユーザーの目視） |
| D1 | **常駐の描画の channel**: P8c の node が持つ GR の channel を 1 本（P9c の compute.c の作り方を boot の段から node の部品に移す: driver の vaspace・promote・3D と compute の object・GPFIFO の ring の wrap・semaphore の fence）。BO（VRAM と system memory）をその vaspace に map・unmap する（PTE を書いて TLB を invalidate）。submit と完了の待ち（最初は polling、後で non-stall の割り込み） | host: VA の allocator・PTE の組み立て・ring の wrap の計算。実機: `nvrtx.resident=1` の後に node の channel で clear の fence が来る（印） |
| D2 | **executor の protocol の層**: i915 の `render/` の hardware に依らない層（codec・transport・reply・object 表・dispatch・forget・command pool/buffer の記録・descriptor・render pass・pipeline の decode・fence・sync・instance/device）を nvrtx に移す（共有できる file は bcm2711 と同じく共有、i915 の Gen の部分は外す）。node に `GPU_CAP_CAPSET | COMMAND | NOTIFICATION | JOB …`（design 8.1）、capset は nvrtx の vendor の識別 | host: Kei の stream を decode して object を作る試験（ws141 の `vulkan-stream-host-test.sh` と同じ作り） |
| D3 | **NVIDIA の backend（compositor の範囲だけ）**: graphics pipeline（P4 の compiler の SPIR-V → SM75、SPH と code を program の領域へ）、render pass（colour target、load の clear）、draw（vertex buffer、push constant は cbuf 0、descriptor の combined image sampler は TIC・TSC の pool、viewport・scissor）、image の copy・blit・clear と buffer↔image の copy（最初は 3D の quad か copy engine、linear の image だけ）、barrier（WFI と cache の invalidate） | host: 各 command の push を decode して clc597.h で照合（`nvrtx-sm75-host-test` の draw の照合と同じ形）。実機: `userland/tests/venus-frame` の nvrtx の node |
| D4 | **image と memory**: 全 image を pitch linear で始める（display の constraints が linear、texture も pitch の TIC で足りる）。VkDeviceMemory は VRAM の BO（host から見える物は BAR1 越し）か system memory | host: 大きさ・pitch・整列 |
| D5 | **WSI**: node の roles に RENDER を足す（今は DISPLAY だけ）。swapchain の image は VRAM の blob、present は P8c の BLOB の flip（複写なし）| 実機: `vkdemo` か `venus-frame` の display |
| D6 | **compositor**: Keiland が nvrtx の node で起動し desktop を描く。`/dev/gpu0` 固定の direct scanout（`libkeiland-backend-zedbsd/scanout-zedbsd.c`）は RTX が gpu0 でない時の扱いを Keiland 側（別 WS）に依頼 | 実機: 方式 B で RTX の monitor に desktop（ユーザーの目視） |
| D7 | 方法 A（素の起動）で同じ | 実機 |

- 依存: D1 → D2 → D3・D4 → D5 → D6。D3 の shader は P4（compiler）の SPIR-V → SM75 が前提（desktop の shader 全て）。executor と compiler の約束（program の入力・cbuf 0 の push constant の配置・texture の binding の番号・SPH）を変える時は Q1 経由で P4 とすり合わせる。
- 量の見込み: D2 は i915 の protocol の層の移植で数千行、D3 は compositor の command の範囲で数千行。ベータ2（10/16 夕）の残りでは D1・D2 の host の部分までが現実的で、desktop（D6）はベータ2 の後の見込み（Q1・ユーザーの判断）。
- 代わりの道（判断を Q1 へ）: 方式 B の guest に Venus の virtio-gpu（**zedBSD 用の host の renderer**。素の virglrenderer は上のとおり飛ばされる）を足せば、compositor は Venus で描き nvrtx の display に複写の present で出せる（D0 だけで desktop が RTX の monitor に出る）。ただし「RTX が描く」ではなく、毎 frame の CPU の複写（P8c の storage の present）になる。

### 8.3 D1 の実装（2026-10-15、P2。build と host 試験まで、実機は未）

`nvrtx.work=1`（新しい語、既定は off で既定の boot は不変）の時、P9c の後・P8 の前に段 **P9w** を走らせ、GPU の device が持つ描画の channel を作る。golden context（P5g）が要るので `nvrtx.work=1` は P5g も走らせる。P9c（compute.c）の作りを写し、boot の試験ではなく使い続ける部品にした。

| 物 | 中身 |
| --- | --- |
| `work.c`（新） | arena（P4m の allocator の BO 32 MiB、2 MiB 整列: page table・instance・USERD・自分の context buffer）、driver の vaspace（`NVRTX_WORK_VA_BASE` 0x20000000 からの 512 MiB を 1 本の DIR1 の entry で。context buffer は golden context の VA に privileged、driver の page（ring・semaphore・fence の push の 8 page）は 0x28000000、0x28100000 から上を利用者の map に heap で配る）、channel（chid 4、0xc46f）・promote・3D（0xc597）と compute（0xc5c0）の object・bind・schedule、最初の submission（subchannel 0 に 3D、1 に compute の SET_OBJECT と fence）。API: `nvrtx_work_map_vram`・`nvrtx_work_map_system`（PTE を書いて TLB の invalidate）・`nvrtx_work_unmap`・`nvrtx_work_submit`（呼び手の push の segment と driver の fence の 2 entry、ring は 512 entry で wrap、entry と fence の slot はその entry の前の fence が過ぎるまで再利用しない）・`nvrtx_work_wait`（semaphore の polling、wrap を考えた比較）。停止は nouveau の順（schedule off → 3D・compute → channel → root → space）、arena は GSP の停止の後に allocator へ返す |
| `work-records.c`・`nvrtx-work.h`（新） | hardware に触れない部分: fence の比較（`nvrtx_work_fence_passed`）、SET_OBJECT の push（`nvrtx_work_push_objects`）、layout の定数 |
| `mmu.c` | `nvrtx_mmu_gr_pte_offset`（VA の PTE の位置。page table は 2 MiB の順に並ぶので run の PTE は表をまたいで連続）・`nvrtx_mmu_gr_entries_vram`・`_system`（既存の map の関数と同じ PTE の値を 1 列に） |
| `gsp.c`・`stage.c`・`nvrtx.c`・`nvrtx-gpu.h`・`nvrtx-private.h` | `nvrtx.work=1`、P5g を伴わせる、P9w の段、stop と release の順（work → compute → display → gr、release は allocator の release の前）、`work.lock` |
| `platform/amd64/vmunix.mk` | NVRTX の ifeq の中に 2 file |
| `config-amd64-nvrtx.mk` | `NVRTX_RUN=R9w`（`nvrtx.gsp=1 nvrtx.work=1 nvrtx.disp=0 nvrtx.resident=0`） |

確認:

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-gsp-host-test.sh` | `PASS nvrtx gsp: 252 checks`（work の 18 項目: PTE の位置（先頭・利用者の範囲・最後・範囲外・非整列・範囲の検査）、256 枚の page table、VRAM と system の PTE の値、fence の比較と wrap、SET_OBJECT の push の 4 語、fence の push が slot の 64 byte に入る、ring が 1 page） |
| `make -j16 BUILD=build/p2/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p2/amd64/vmunix` | rc 0・warning 0・`kernel include check: PASS (422 objects)`・`amd64 vmunix check: PASS`、`git diff --check` 0 |
| 実機（R9w） | **未実施** |

未確定の危険: (1) 2 本目の GR の channel（chid 3 の P9c と同時でも別でも）を RM が受けるか（P9c は 1 本で実機 PASS）。(2) arena を allocator の BO（VRAM の下の方）に置くこと（P9c は上端の固定の reserve）。context buffer の位置に制約があれば promote で分かる（`rm: control … status …`）。(3) 1 MiB の page table の image を kern_malloc で一度に取る。

#### T への手順（R9w、方式 B、rombar=0 で可）

image: `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r9w NVRTX_RUN=R9w`（merge の commit）。run の前に host の reboot と `prepare.sh --check`。判定は guest の kernel の印（screendump か `dmesg | grep nvrtx`）。
合格: R7g の印（P5g の ok）に続いて `P9w begin` → `P9w arena at …, own buffers … bytes` → `P9w page tables at …` → `P9w ok: vaspace` → `P9w ok: work channel 4 on runlist N` → **`P9w ok: fence 1 after N us`** → `P8 ok: display …` → `P8b off (nvrtx.disp=0); stopping after P8a` → `P8 done; …` → `P6 ok: channel and vaspace freed`（**`P6: N work frees failed` が無い**）→ `P7 ok: region torn down` → login prompt。
FAIL の時: `P9w fail: …`（`the first fence 1 did not come (…, semaphore …)` なら channel は作れたが GPU が entry を実行していない）、`rm: class … status …`・`rm: control … status …`、`P6 channel error: …` を記録。`P7 fail` なら host を reboot。

### 8.4 D2 の案: executor の protocol の層をどこから作るか（2026-10-15、P2。Q1 の確認待ち、code は未着手）

調べた事実:
- i915 の `render/` は i915 の型（`i915_gem_object`・`i915_session`・batch）に全体が結び付いた一枚の executor（video 抜きで約 2.1 万行）。hardware に依らない層だけを取り出す境界が無い。
- bcm2711 の executor（`src/drivers/gpu/bcm2711/vulkan-*.c`、約 2.3 万行、video 3.6 千行を含む）は file が細かく分かれ、**i915 の `render/codec.c`・`codec.h`・`vulkan-codec.inc`・`internal.h` の wire の型を変えずに include して使う**（`vulkan-private.h`、arm64 の vmunix.mk は codec.c を filter-out で 1 回だけ link）。bcm2711 の file のうち V3D の名前（`bcm2711_buffer`・`v3d_`・`native_`）を含まない物が 24 file・約 8.5 千行（object・session・dispatch・command pool/buffer・record・descriptor・layout・pipeline の decode と state・barrier の decode・query・draw の記録・target・input・prepared）。V3D に結び付くのは memory・queue・stream・transfer・buffer-copy・uniform・native-*（約 6 千行）と video。

| 案 | 内容 | 他の WS への影響 | 量 |
| --- | --- | --- | --- |
| **A（推す）** | i915 の codec 層（codec.c・codec.h・vulkan-codec.inc・internal.h の wire の型）を bcm2711 と同じく**変えずに include**。protocol の層は bcm2711 の V3D に依らない file を手本に nvrtx の中に書く（`src/drivers/gpu/nvrtx/vk-*.c`、型と名前は nvrtx の物、session は nvrtx の node の session、BO は P4m の VRAM の BO と D1 の work の map）。NVIDIA の backend（D3）は nvrtx の draw-push・compute-launch・P4 の compiler を使う | **無し**（i915・bcm2711 の file は読むだけ）。build: `platform/amd64/vmunix.mk` の NVRTX の ifeq の中に nvrtx の file と、i915 が n の時のために `src/drivers/gpu/i915/render/codec.c` を `filter-out` で 1 回だけ足す（arm64 と同じ形） | protocol の層 約 6〜8 千行（compositor の範囲に絞れば少ない） |
| B | bcm2711 の V3D に依らない 24 file を中立の置き場（例 `src/drivers/gpu/vulkan/`、接頭辞も中立）へ移し、bcm2711 と nvrtx が共有 | WS141（Pi 4）の file を動かす。Pi 4 の回帰（AAT の Vulkan の試験）が要る。WS141 の担当との調整 | 移動と名前の付け替え 約 8.5 千行＋bcm2711 の呼び出し側 |
| C | i915 の `render/` を手本に書く | 無し | A より多い（i915 は層が分かれていない） |

- 推す理由: A はベータ2 の後の desktop までの道で他の WS を止めない。重複（bcm2711 と似た protocol の層が 2 つ）は、nvrtx が desktop を描けた後に B の形へ寄せる別の Phase（「executor の共有の層」）で解消する（Future Work の候補）。
- A で最初に作る範囲（compositor が使う物だけ、8.1 の表）: instance・physical device・device・queue（capset id 4、SDBZ、flags 7、`GPU_CAP_COMMAND|CAPSET|NOTIFICATION|JOB|JOB_CAPACITY`、roles に RENDER）、memory（VRAM の BO を BAR1 越しに host から見える coherent な型も）、buffer・image（pitch linear だけ）・image view・sampler、descriptor set layout/pool/set（COMBINED_IMAGE_SAMPLER）、pipeline layout・shader module・graphics pipeline（P4 の compiler へ SPIR-V を渡す口）、render pass・framebuffer、command pool/buffer と 15 種の vkCmd の記録、queue submit（D1 の work の submit と fence）、fence・semaphore。
- 確かめ: host 試験（ws141 の `vulkan-stream-host-test.sh` と同じ作り: libvulkan の `wire.c` で stream を作り、nvrtx の decoder で object が出来ること、reply の形）。
- 決定（2026-10-15 Q1）: **案 A**。i915・bcm2711 の file は動かさず include、protocol の層は nvrtx の中。着手は R8b の後。
- Future Work の候補（Q1 が `plan/future-work.md` に載せる）: 「executor の共有の層」— nvrtx が desktop を描けた後、bcm2711 と nvrtx の V3D・NVIDIA に依らない protocol の層（object・session・dispatch・command buffer の記録・descriptor・pipeline の decode など）を中立の置き場に寄せて共有する。再考の契機: D6（compositor が nvrtx で描く）の PASS。

### 8.5 D2 の実装（2026-10-15 夜、P2。build と host 試験まで）

案 A（8.4）で、i915 の codec 層を変えずに include し、protocol の層を nvrtx の中に書き始めた。

| 物 | 中身 |
| --- | --- |
| `nvrtx-vk.h`（新） | executor の session（node の session が owner、object の索引（1024 bucket の hash）と list、decode の scratch 256 KiB）、object、route と dispatcher の型、node が出す口（`nvrtx_vk_blob`: blob の CPU の view、`nvrtx_vk_device_id`・`nvrtx_vk_heap_bytes`） |
| `vk-object.c`（新） | 型付きの wire の identity の登録（publish・find・retain・release・remove・close。最後の holder が payload を壊す） |
| `vk-session.c`（新） | session の open/close、reply の record の scratch（`nvrtx_vk_arena_take`。i915 の `i915_vkc_array` は reader の残りで数を制限するので、stream の最後の command では使えない） |
| `vk-stream.c`（新） | transport: reply の stream の選択（session の blob の範囲）・移動・version の probe（最後の語を release で書く）・blob の中の stream（copy してから、深さ 4・64 MiB まで） |
| `vk-dispatch.c`（新） | opcode を持つ handler へ（今は device の route だけ） |
| `vk-device.c`（新） | instance・physical device（1 つ、NVIDIA 0x10de、discrete、PCI の product）・features（optional 無し）・memory（1 type: device-local・host-visible・coherent、heap は BAR1 の窓の大きさ）・queue family（1 つ、graphics・compute・transfer、queue 1）・format（B8G8R8A8/R8G8B8A8 の UNORM と SRGB、頂点の R32〜R32G32B32A32 SFLOAT）・image format（2D・1 level・1 sample）・device・queue・wait idle・destroy |
| `node.c`・`share.c`・`nvrtx-node.h` | node の session が自分の blob を protocol の番号で list に持つ（blob の作成と import で載せ、壊す時に外す）、open で executor の session を開き close で閉じる |
| `platform/amd64/vmunix.mk` | NVRTX の ifeq に vk の 5 file、i915 が n の時だけ `i915/render/codec.c` |

確認:

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-vk-host-test.sh` | `PASS nvrtx vk: 59 checks`（object の登録と 3000 個の索引、transport（reply・probe・blob の stream の copy・入れ子の上限・容量・不正）、libvulkan の writer で instance → 列挙 → family → format → memory → device → queue → idle → destroy） |
| vmunix（`build/p2/amd64`） | rc 0・warning 0・include/vmunix check PASS |
| 実機 | 未実施（node に CAPSET・COMMAND・NOTIFICATION・JOB はまだ付けていない: 次の D2 の残り） |

### 8.6 D2 の node の口（2026-10-16、P2。build と host 試験まで）

`nvrtx.vk=1`（新しい boot の語、既定は off: D3 が動くまで既存の machine の libvulkan が nvrtx を renderer に選ばないように）の時だけ、node が renderer になる。

| 物 | 中身 |
| --- | --- |
| `runtime.c`（新） | `get_capset`（id 4・version 0、168 byte: wire 1・XML 1.3.269・timeline の語 1・`SDBZ`・flags 7。bcm2711 と同じ配置）、`command`（同期: session の queue が空の時だけ、node の lock の下で executor）、`commands.submit/drain`（frame の copy を node の FIFO へ、worker が順に executor → `drv_gpu_complete`。flags は CONTEXT_FENCE だけ、timeline は fence の時だけ 1〜63、byte 0 は fence だけ）、`jobs`（reserve: session の queue の timeline だけ、marker 64 個、空きが無ければ EAGAIN／commit: 前の frame の後に並べる／cancel: 通常は RESERVED だけ戻す、fault は EIO で並べる／capacity: 空きの marker の数）、`recovery.fault`（以後は受けず、並んだ物は EIO）。worker は register の後に起動し、作れなければ fault |
| `nvrtx-node.h` | queue の entry・marker・`queue_lock`・`available`/`retired` の wait queue・session の `pending` |
| `vk-device.c`・`nvrtx-vk.h` | `vkGetDeviceQueue2` の timeline（1〜63）を session の `timelines` に記録（jobs の reserve/capacity が見る） |
| `node.c`・`share.c`・`stage.c` | `nvrtx.vk=1` で bind（register の前）と worker の起動（register の後）、`query_device` の roles に RENDER |

確認:

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-vk-host-test.sh` | `PASS nvrtx vk: 60 checks`（queue の timeline 3 の記録を足した） |
| vmunix（`build/p2/amd64`） | rc 0・warning 0 |
| runtime.c の host 試験 | 無し（kernel の spinlock・waitq・thread に依る。review で確かめた: GPU core の契約（JOB に NOTIFICATION と recovery.fault、cancel の後の capacity の通知は core、drain は close で pending 0 まで）、completion はちょうど 1 回） |
| 実機 | 未実施（D3 の後に vkdemo の offscreen と合わせて T1 へ） |

次は D3（memory・buffer・image・descriptor・pipeline・render pass と記録の 15 種の vkCmd、D1 の work channel へ投入）。

### 8.7 D3 の設計（2026-10-16、P2）

範囲は Keiland の compositor と libkeiland の ui が使う物だけ（`userland/desktop/wayland`・`libkeiland/ui` の vk 呼び出しの棚卸し）: memory・buffer・image（2D、1 level・1 layer・1 sample、B8G8R8A8／R8G8B8A8 の UNORM・SRGB）・image view・sampler・shader module・descriptor set layout／pool／set／update（COMBINED_IMAGE_SAMPLER）・pipeline layout（push constant）・render pass（colour 1 つ）・framebuffer・graphics pipeline・command pool／buffer・fence・semaphore・QueueSubmit、記録する vkCmd は 15 種（BeginRenderPass・EndRenderPass・BindPipeline・BindDescriptorSets・BindVertexBuffers・PushConstants・SetViewport・SetScissor・Draw・PipelineBarrier・CopyImage・CopyBufferToImage・CopyImageToBuffer・BlitImage・ClearColorImage）。

| 決め | 内容 | 理由 |
| --- | --- | --- |
| wire の手本 | i915 の executor（`i915/render/` の `objects.c`・`memory.c`・`image.c`・`descriptor.c`・`pipeline.c`・`render-pass.c`・`command.c`・`fence.c`・`sync.c`）。同じ libvulkan の wire を i915 の codec の decoder（`i915_vkc_dec_Vk*`）で読む | 同じ codec を include 済み（案 A）。bcm2711 の手読みより短い |
| memory | VkDeviceMemory は記録だけ作り、storage は libvulkan が直後に出す blob（`GPU_BLOB_CREATE` の `blob_id` = memory の identity）が来た時に node が VRAM の BO（今の blob と同じ `nvrtx_backing`）を作って memory に付ける（refs を 1 つ持つ）。付けた時に work の vaspace に map し、GPU の VA を memory に持つ。free で unmap と refs を返す | i915 と同じ「storage は blob で来る」。memory type は 1 つ（DL・HV・HC）なので必ず blob が来る。blob を display が出せる（swapchain の image を複写なしで flip、D5） |
| image | 全て pitch linear。pitch は幅×4 を 256 に揃え、offset の整列 256、大きさは pitch×高さ。`vkGetImageSubresourceLayout` はこの pitch | display の constraints（stride 64・offset 256）、TIC の pitch（32 の倍数）、render target の pitch（128 の倍数）を同時に満たす |
| 実行 | QueueSubmit の frame の中で（worker の上で）command buffer の記録を push buffer に組み、`nvrtx_work_submit` → `nvrtx_work_wait` で終わるまで待つ（同期）。後に続く job の marker は FIFO の順で終わる | 正しさが簡単（semaphore の待ちは同じ queue の順で満たされる）。速さは後で |
| draw | P9g の `draw-push.c`（実機 PASS）を広げる: render pass の colour target、viewport・scissor（dynamic）、blend（ONE / ONE_MINUS_SRC_ALPHA）、program（P4 の compiler の SM75 と SPH を program の領域へ）、vertex buffer、push constant は cbuf 0、combined image sampler は TIC・TSC の pool と cbuf 0 の texture handle（P4 の約束 `NVRTX_SM75_CB_TEXTURES`） | 既に hardware で通った手順 |
| 転送 | CopyImage・BlitImage は textured quad（nearest／linear）、ClearColorImage は 3D の clear、buffer↔image は buffer を pitch linear の texture／target として描く（offset 256・pitch 128 に揃う時）、揃わない時は CPU（BAR1 越し）で複写 | 2D／copy engine の object を増やさない。compositor の複写は image↔image（shm の staging → image）が主 |
| 試験 | host: libvulkan の本物の encoder（`resources.c`・`pipeline.c`・`commands.c` などを include、ws141 の `vulkan-client-pipeline-host.c` と同じ作り）で wire を作り、executor に通し、GPU の口は host の偽物（push を記録）で、組んだ push を `clc597.h` の値で照合 | 実機の前に wire と push の両方を確かめる |

段: D3a memory・buffer・image・view・sampler（＋node の blob の `blob_id`）→ D3b shader module・layout・descriptor・render pass・framebuffer・pipeline（compile）→ D3c command pool／buffer と記録 → D3d fence・semaphore・QueueSubmit と push の組み立て・実行。各段で build・host 試験、D3d の後に T1 へ（vkdemo の offscreen）。

### 8.8 D3 の実装（2026-10-16、P2。build と host 試験まで、実機は R9v を T へ）

8.7 の設計どおり。executor の object と記録と実行を nvrtx の中に書いた（i915 の codec を include、型・名前・構造は nvrtx の物）。

| 物 | 中身 |
| --- | --- |
| `nvrtx-vk-objects.h`（新） | memory・buffer・image（pitch linear、pitch は幅×4 を 256 に）・view・sampler・shader module・set layout・pipeline layout・descriptor pool／set・render pass・framebuffer・pipeline・command pool／buffer・record・fence の payload |
| `vk-reply.c`（新） | create の tail と reply、plain な destroy、errno → VkResult |
| `vk-memory.c`（新） | Allocate（記録だけ。storage は blob の attach で、import は既存の blob）・Free・Bind（256 境界、範囲内、1 回）・Requirements（256 単位）・Commitment |
| `vk-resource.c`（新） | Buffer・Image（2D・1 level・1 layer・1 sample・4 byte の色 format だけ、他は名前を出して FEATURE_NOT_PRESENT）・SubresourceLayout・ImageView（swizzle）・Sampler |
| `vk-layout.c`（新） | ShaderModule・DescriptorSetLayout・PipelineLayout（push の大きさ）・PipelineCache（記録しない）・DescriptorPool（Destroy／Reset で set も）・Allocate／Free／Update（combined image sampler、view と sampler を保持） |
| `vk-target.c`（新） | RenderPass（subpass 1・色 1 つ・depth なし）・Framebuffer（view を保持）・RenderAreaGranularity |
| `vk-pipeline.c`（新） | CreateGraphicsPipelines: 全 record を読み、P4 の compiler（`nvrtx_sm75_compile_stage`）で vertex と fragment を SM75 に、SPH と code を executor の GPU memory（`nvrtx_vk_gpu_alloc`）へ。vertex input（float 1〜4）・topology（triangle list／strip）・blend・dynamic viewport／scissor。compute は ENOTSUP |
| `vk-command.c`・`vk-record.c`（新） | CommandPool（Destroy／Reset で buffer も）・Allocate／Free・Begin／End／Reset、記録 15 種（＋SetLineWidth は読むだけ、PipelineBarrier は記録しない）。record は名前の object を保持、region と push は record 自身の領域 |
| `vk-execute.c`（新） | 記録 → push buffer。session ごとの 2 MiB の run（push 1 MiB・draw ごとの cbuf0 0x500・TIC／TSC pool 2048・zero page・quad の頂点）。draw ごとに target・viewport・scissor・blend・vertex input・cbuf0（push と texture handle）・program・draw。copy／blit／buffer→image（揃う時）／image→buffer（揃う時）は executor の quad（P9g の IR）で描く（unnormalized の sampler）、揃わない buffer は CPU（BAR1 越し、それまでの push を先に走らせる）。pass と転送の間は WFI と cache の invalidate |
| `exec-push.c`・`nvrtx-exec.h`（新） | NVK の値の method: 色 target（4 format）・viewport（固定小数点で float を作る、kernel に FPU なし）・scissor・blend・vertex input・draw・clear・barrier・TIC（swizzle）・TSC |
| `vk-queue.c`（新） | QueueSubmit（command buffer を順に実行して待つ、fence を signal）・Fence（作成／Reset／Status）・Semaphore（順序は 1 queue の順で満たす） |
| `vk-node.c`（新） | node の口: blob の attach（work channel へ map、backing の参照）・detach・import・executor の GPU memory・push の実行（`nvrtx_work_submit` → `nvrtx_work_wait`） |
| `node.c`・`stage.c`・`vk-session.c`・`vk-dispatch.c` | blob_id の blob を memory に attach、`get_info` の capabilities を ops の物に、`nvrtx.vk=1` は work channel も、session の close で run を返す、拒否した opcode を kernel log に（最初の 32） |
| `userland/tests/vkquad`（新）と `platform/common/userland-dynamic.mk`・`platform/amd64/vmunix.mk` | compositor の quad の shader（`wayland/shaders.h`）で offscreen に描いて読み戻す試験の道具（`VKQUAD ok device=… pixels=4096` か `VKQUAD fail <段> …`）。vkdemo と同じ dynamic の Vulkan client として登録 |
| `config-amd64-nvrtx.mk` | `NVRTX_RUN=R9v`（`nvrtx.gsp=1 nvrtx.vk=1`、vkquad） |

確認:

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-vk-d3-host-test.sh` | `PASS nvrtx vk d3: 98 checks`（libvulkan の writer と codec の encoder で作った wire を dispatcher に通す。memory・attach・bind・requirements・layout・import、view・sampler、compositor の quad.vert／quad.frag（glslc）の pipeline の compile、descriptor、pass、framebuffer、記録 13 件の command buffer の submit → push を解読して target（0xcf）・clear 2 回・draw 3 回（quad・copy・buffer→image）・program の address・blend・viewport（scale 32、copy の 8 と offset 18）・scissor・barrier・TIC の address と pitch・cbuf0 の handle と push、揃わない region の CPU の複写、fence の signal、漏れ無し） |
| `sh plan/ws037/tests/nvrtx-vk-host-test.sh` | `PASS nvrtx vk: 60 checks` |
| vmunix（`build/p2/amd64`） | rc 0・warning 0 |
| vkquad（`ZEDBSD_USER_PROGRAMS="libvulkan vkquad"`） | rc 0 |
| 実機 | **未実施**（R9w の後に R9v を T へ） |

#### T への手順（R9v、方式 B、rombar=1）

前提: R9w（work channel）が通ること（T1-658 で PASS 済み、R9v は同じ channel を使う）。image は harness 入りで（SSH の鍵と `/etc/net.conf`）`plan/tools/guest/test-image.sh plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r9v NVRTX_RUN=R9v`。QEMU は host の `~/nvrtx/start-rombar1-net.sh`（usb-net と hostfwd、T1-664 と同じ）。run の前に host の reboot と `prepare.sh --check`、run の後も host の reboot（GSP は残る）。判定は SSH で guest の中の出力と dmesg（serial・console の log では判定しない）。
1. SSH で `dmesg | grep nvrtx`: R9w の `P9w ok: fence 1 after N us` と R8c の `P8c ok: gpu device registered, …`。
2. SSH で `vkquad` → 1 行。合格は **`VKQUAD ok device=… pixels=4096`**。
3. その後の `dmesg | grep nvrtx` の全文を取る（`nvrtx: vk: …` の行、`D3 fail: …`、`P6 channel error: …`、`rpc: …` があれば記録）。
FAIL の時: vkquad の行（`VKQUAD fail <段> <VkResult>`、pixel なら座標と値）と dmesg の `nvrtx: vk: command N refused (E)`（実装の無い command）・`a stage … did not compile`・`D3 fail: the work of fence … did not finish` を記録。
