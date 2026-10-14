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
