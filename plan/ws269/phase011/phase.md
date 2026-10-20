# ws269-p011 — Keiland が使う Vulkan の機能の不足を driver 側で埋める

- 親: [ws269](../ws.md)。Queue: q1392 / q1392-i01（P2、2026-10-20、前半: 突き合わせの表と P1 の file の外の不足の実装）。
- 状態: in-progress。host 試験と VF2 の build まで。実機は未実施。desktop の目視は WS270 p006。

## 読んだ範囲

- Keiland: `userland/desktop/wayland/`（compositor）、`userland/desktop/libkeiland/`（app の UI の present と canvas）、`userland/desktop/libkeiland-backend-zedbsd/`。
  - `libkeiland-backend/dmabuf/` は Linux・FreeBSD の backend なので外した。そこにだけ `vkGetPhysicalDevice(Image)FormatProperties2KHR` がある。
- libvulkan の WSI: `wsi-*.c`・`sync.c`・`external-fence.c`・`memory.c`。どの命令が executor へ送られるかは `include/uapi/gpu-op.h` の「Sent by・Not used by zedBSD」で決めた。
- driver: `src/drivers/gpu/powervr/vk-*.c` の route（`case GPU_OP_*`）、`render-draw.c`・`render-pass.c`・`vk-pass.c`・`vk-device.c`・`image-state.c`、P1 の `render-job.c`・`vk-queue.c`。

## 突き合わせの表

### 命令

| Keiland・libvulkan が使う物 | executor | 判定・file |
| --- | --- | --- |
| instance・physical device の問い合わせ（Properties・Features・MemoryProperties・QueueFamily・FormatProperties） | route あり | 対応（vk-device.c） |
| `vkGetPhysicalDeviceProperties2`（libvulkan の identity、memory.c:187・761） | route あり（p010l） | 対応（vk-device.c） |
| `vkEnumerate*ExtensionProperties`・`vkGetDeviceQueue`・`vkMapMemory`・`vkUnmapMemory`・`vkWaitForFences` | libvulkan だけで済む（executor へは送らない） | 対応の要なし |
| WSI（surface・swapchain・display・`vkQueuePresentKHR`・`PRESENT_REGIONS`・`DISPLAY_PRESENT_INFO`・display control・fence fd） | libvulkan の中。executor には readback（copy）か shared の道の命令として届く | 対応（readback は p010k1・k2、shared は p010l。shared の切り替えは off のまま） |
| device・queue（`CreateDevice`・`GetDeviceQueue2`・`QueueSubmit`・`QueueWaitIdle`・`DeviceWaitIdle`） | route あり | 対応（vk-device.c・vk-queue.c） |
| memory・buffer・image（Allocate・Free・Bind・Requirements・`GetImageSubresourceLayout`、import の fd → `IMPORT_RESOURCE`、export） | route あり | 対応（vk-memory.c・vk-image.c、p010l） |
| view・sampler・shader module・descriptor（layout・pool・allocate・update）・pipeline layout・graphics pipeline・render pass・framebuffer | route あり | 対応（vk-image.c・vk-layout.c・vk-pass.c・vk-draw.c） |
| fence・semaphore（Create・Destroy・Reset・GetStatus） | route あり | 対応（vk-queue.c） |
| command pool・buffer（Allocate・Free・Begin・End・Reset） | route あり | 対応（vk-command.c） |
| `vkCmdBeginRenderPass`・`EndRenderPass`・`BindPipeline`・`BindDescriptorSets`・`BindVertexBuffers`・`PushConstants`・`SetViewport`・`SetScissor`・`Draw`・`PipelineBarrier` | route あり | 対応（vk-pass.c・vk-draw.c） |
| `vkCmdCopyBufferToImage`・`CopyImageToBuffer`・`CopyImage`・`BlitImage`・`ClearColorImage` | route あり | 対応（vk-transfer.c、P1） |

### 1 つの render pass の中の draw の数

| 物 | 前 | 今 |
| --- | --- | --- |
| 1 つの render job に入る draw | 16（`POWERVR_RENDER_DRAWS`）。17 番目は E2BIG で submit が失敗した | **実装した**。満ちた job は次の job に続く（`powervr_render_job_continue`、render-pass.c）。前の job はすべての target を STORE し、次の job は LOAD して、pass が頼んだ store にする。vk-pass.c の `powervr_vk_render_jobs` が使う |
| 1 つの command buffer の job | 8（`QUEUE_JOBS_MAX`、vk-queue.c） | 不足（**P1**）: 8 job × 16 draw = 128 draw まで。Keiland の 1 frame の draw がそれを越えると E2BIG |

### struct の chain

| chain | 判定 |
| --- | --- |
| `EXTERNAL_MEMORY_IMAGE_CREATE_INFO`（gpu-buffer-zedbsd.c の import） | 対応（p010l） |
| `IMPORT_MEMORY_FD_INFO_KHR`・`MEMORY_DEDICATED_ALLOCATE_INFO`・`EXPORT_FENCE_CREATE_INFO` | libvulkan が変換するか取り除く（allocate は `IMPORT_RESOURCE` 1 つにする）。executor は 1 つの record だけを受ける | 対応 |
| `PRESENT_REGIONS_KHR`・`DISPLAY_PRESENT_INFO_KHR`・`DEVICE_EVENT_INFO_EXT` | libvulkan の中 | 対応の要なし |

### feature・format・limit・pipeline の state

| 物 | Keiland の使い方 | 判定 |
| --- | --- | --- |
| device の feature | `pEnabledFeatures` なし | 対応 |
| image の format | B8G8R8A8_UNORM・R8G8B8A8_UNORM。optimal は COLOR_ATTACHMENT・BLEND・SAMPLED・FILTER_LINEAR・TRANSFER・BLIT（turn.c の TURN_FEATURES） | 対応（image-state.c） |
| linear の image | shm.c（TRANSFER_SRC、PREINITIALIZED）、present.c の canvas と gpu-buffer-zedbsd.c の import（SAMPLED）。作成は `image_check` が format の linear の全部の features と比べるので通る。問い合わせは切り替え off で linear の SAMPLED を出さないが、Keiland はこの 3 か所で問い合わせない | 対応。linear の view は image-state.c にある |
| vertex の format | R32_SFLOAT・R32G32_SFLOAT・R32G32B32A32_SFLOAT | 対応（R32_SFLOAT は p010p で追加） |
| descriptor | COMBINED_IMAGE_SAMPLER（fragment） | 対応 |
| sampler | NEAREST・LINEAR、CLAMP_TO_EDGE、MIPMAP NEAREST | 対応 |
| blend | ONE・ONE_MINUS_SRC_ALPHA・ADD、RGBA の write mask | 対応（render-draw.c の blend の key、compiler） |
| pipeline | TRIANGLE_LIST、CULL NONE、FILL、1 sample、dynamic の VIEWPORT・SCISSOR | 対応（render-draw.c） |
| render pass | LOAD・CLEAR・DONT_CARE、STORE・DONT_CARE、layout の PRESENT_SRC・SHADER_READ・TRANSFER・GENERAL・COLOR_ATTACHMENT | 対応（render-pass.c、load は U8888 の render-load） |
| limit | `maxImageDimension2D`（compose.c:1643）、push constant（128 byte の中）、memory type の HOST_VISIBLE\|HOST_COHERENT | 対応（vk-device.c、4096・128・HOST の type） |
| shader | compositor の quad・panel（8 つの mode）・panel-batch・turn、libkeiland の canvas.vert・canvas.frag | 対応。canvas の 2 つは compiler の host 試験に足した（前は compositor の物だけ） |

## 実装と確認

- `src/drivers/gpu/powervr/render-pass.{c,h}`: `powervr_render_job_continue`。
- `src/drivers/gpu/powervr/vk-pass.c`: job が満ちたら続ける。room が無ければ E2BIG。
- `plan/ws269/tests/p010/pass-host-test.c`: 満ちていない job は続けない。満ちた job は STORE になり、次は LOAD で pass の store を保ち、draw 0・同じ area・同じ surface。
- `plan/ws269/tests/p010-compiler/compile-host-test.c`: libkeiland の canvas.vert・canvas.frag を足した。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| p010 の host 試験 | `sh plan/ws269/tests/p010/run.sh build/p011/p010` | 全部 0 failed（render-pass 18 passed） |
| compiler | `sh plan/ws269/tests/p010-compiler/run.sh build/p011/comp` | 396 passed・0 failed、group 20 passed |
| executor の frame | `sh plan/ws269/tests/p009/run.sh build/p011/p009` | 175 passed、0 failed |
| VF2 の kernel | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p011/vf2 vmunix -j16` | rc 0、warning 0 |

- 未実施:
  - 17 個以上の draw を持つ command buffer を `powervr_vk_render_jobs` に通す host 試験（vk-pass の frame の試験が無い）。
  - 実機。

## P1 の file に要る物（Q1 経由で P1 へ）

1. `vk-queue.c` の `QUEUE_JOBS_MAX`（8）。1 つの command buffer の render pass と、続きの job で 8 を越えると E2BIG になる。room を広げるか、job を 8 ずつ順に kick する。Keiland の frame は draw が多い。
2. ~~`render-job.c` の `job_check` が depth の load・store を ENOTSUP にする件~~。q1388（c449248de）で既に外れていた（Q1、2026-10-20）。
3. `render-job.c` の `job_check` の colour は 1 つ・4 byte。Keiland はこれで足りるので、今は不要。
- 片付け（Q1 に依頼）: `build/p011/`。

## 後半（q1394-i01）: Keiland に似た frame の host 試験

p009 の executor の host 試験（Venus の frame）に `frame_keiland`・`replies_keiland`（`plan/ws269/tests/p009/frame-host.c`）を足した。command の順と wire の形は compose.c と libvulkan の encoder（pipeline.c の `pipeline_encode_graphics`、commands.c の render pass の begin と clear の color）に合わせた。frame の中身は次のとおり。

- 64x64 の BGRA8 の target・view・pass（CLEAR・STORE）・framebuffer。vertex buffer。
- compositor の quad.vert・quad.frag と libkeiland の canvas.vert・canvas.frag の module。
- combined image sampler の set layout。push constant が 32 byte と 16 byte の 2 つの pipeline layout。
- 2 つの graphics pipeline（R32G32 と R32G32B32A32 の vertex、premultiplied の blend、dynamic の viewport・scissor）。
- image の frame の texture と sampler の set。
- command buffer A: CopyBufferToImage → 20 draw の pass（quad）→ 17 draw の pass（canvas）→ present の前の ClearColorImage → submit・fence。
- command buffer B: 200 draw の pass → submit。
- draw ごとに PushConstants と Draw(6)。

host の stub（`vk-host-test.c`）は transfer と render job の kick を順に記録する。記録するのは種類、draw の数、colour の load・store、continued。

### 分かったことと直したもの

- **graphics pipeline がすべて FEATURE_NOT_PRESENT になっていた。**
  - 原因: `vk-draw.c` の `powervr_vk_graphics_decode` が、shader module の `status` が 0 であることを求めていた。`status` は compute の entry が読めたかどうかだけを表すので（vk-layout.c）、vertex・fragment の module は必ず 0 でない。
  - 直し: `status` の検めを外し、2 つの stage は compile の結果で判断する（compile に失敗すれば今までどおり ENOTSUP）。あわせて、vk-layout.c が graphics の module のたびに出していた「compute entry が無い」の log を、ENOTSUP 以外のときだけにした。
  - 影響: 実機で vkdemo や Keiland の pipeline を作る前に必ず当たる不具合だった。実機ではまだ確かめていない。
- **render job の continued**: `struct powervr_render_job` に `continued` を足した（`powervr_render_job_continue` が立てる）。1 つの instance の job は、最初の job とその後の continued の job になる。
- **今の挙動（vk-queue.c、P1）**:
  - command buffer A の kick の順は「T（copy）T（clear）R16 R4c R16 R1c」で、clear が 2 つの pass より先に走る。
  - 原因: `build_command_buffer` は job の数が begin の数と同じときだけ begin の位置で job を走らせる。continuation があると数が合わず、job がすべて command buffer の最後に回る。
  - 試験はこの今の順を期待にしている。P1 が直したら「T R16 R4c R16 R1c T」に変える。
  - load・store は期待どおり: 最初の job は CLEAR・STORE、continued は LOAD・STORE。
- **B（200 draw → 13 job）**: QUEUE_JOBS_MAX（8）を越えて E2BIG になる。submit は INITIALIZATION_FAILED で、kick は無い。試験はこれを期待にしている。P1 が room を広げたら変える。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| executor の frame（切り替え 0・1） | `sh plan/ws269/tests/p009/run.sh build/p011b/p009-0 0`（と `1`） | どちらも 240 passed、0 failed |
| p010 の host 試験 | `sh plan/ws269/tests/p010/run.sh build/p011b/p010` | 全部 0 failed |
| VF2 の kernel | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p011b/vf2 vmunix -j16` | rc 0、warning 0 |

### P1 の file に要る物（後半で増えた分）

4. `vk-queue.c` の `build_command_buffer`: begin の位置で、その instance の job（最初の job と続く `continued` の job）を順に走らせる。今は continuation があると、job がすべて command buffer の最後に回り、copy・clear との順が崩れる。
5. （1. と同じ）`QUEUE_JOBS_MAX`。

- 片付け（Q1 に依頼）: `build/p011b/`。
