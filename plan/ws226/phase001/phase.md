<!-- awesome-plan project=zedbsd record=ws226-p001 -->

# ws226-p001: 調べと設計 — Khronos の Vulkan 1.3 の header をそのまま使い、libvulkan で 1.3 の API を実装しきる

Parent: [WS226](../ws.md)
Status: planning（第 1 版、2026-10-13 P4（kernel-and-driver-designer の役）。design-reviewer の review-1 待ち。コードは書いていない）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: Q1 の指示（2026-10-13、P4「WS226 の p001 調べと設計、コードは書かない、GPL は読まない」）
Purpose / goal: ws.md の完了の条件 1〜6 を実装の段に分けられるだけの事実・設計・決めてほしい点をそろえる。
Prerequisites: 無し（調べの段）。

## 0. 要点（design-reviewer・Q1 向け）

- **今の libvulkan は 1.0 の 137 command を全部持ち、1.1〜1.3 の core の 78 command のうち 18 は拡張の名前（KHR）でだけ持ち、60 は無い**（§2、Khronos の vk.xml 1.3.269 と `api-commands.tsv` を機械的に照らした）。1.3 の core の全部は 215 command。
- **不整合は 12 件**（§3）: Vulkan Video を 1.0 の device で名乗る（`VK_KHR_video_queue` の依存は `VK_VERSION_1_1+VK_KHR_synchronization2`）、ycbcr の拡張無しの NV12、OPTIMAL の画像の `vkGetImageSubresourceLayout`、**header の版の混在**（core は 1.3.269 を選んで整形、video・display_control と `vk_video/` は Debian の 1.4.309 から）、native の backend（i915・bcm2711）が libvulkan の検査を通すためだけに `apiVersion = 1.1` を名乗る（中身は 1.0 の最小の limit も満たさない）、など。
- **device の見込み**（§4）: **venus（QEMU）は 1.3 に届く**（host の driver が 1.3、Venus の wire は 1.3.269 の全部の command を運ぶ。足りないのは libvulkan の符号化だけ）。**i915 と bcm2711 は hardware は 1.3 を出せる**（同じ hardware で Mesa の ANV と v3dv が 1.3）が、**zedBSD の native の executor は 1.0 の必須（`robustBufferAccess`、limit の最小値）にも届いていない**。1.3 の必須の 17 の feature と SPIR-V 1.6 の compile は kernel の executor と compiler の大きな仕事。
- **header の形**（§5）: Vulkan-Headers の **v1.3.269**（commit `374f9fd97520f6dd1b80745de09208d878ab4a52`、tarball の sha256 `1637f36a…5367`）の C の header を**改変せず**に `include/libc/vulkan/` と `include/libc/vulkan/vk_video/` へ写す（推奨、D1）。zedBSD の public な宣言は今 1 つも無いので、zedBSD だけの header は作らない（作るなら `vulkan_zedbsd.h` の別 file）。取得・検証は外部 package の規則に倣った台帳（`include/libc/vulkan/UPSTREAM`、file ごとの sha256）と更新の script。
- **libvulkan の設計**（§6）: instance は 1.3 を名乗り、physical device はそれぞれ「実際に満たす版」を名乗る（推奨、D2）。command は (a) libvulkan だけで済む物、(b) 1.0 の command に下ろせる物、(c) backend が要る物に分け、backend の能力の印で選ぶ。dispatch は版と拡張の両方で gate する。
- **試験**（§7）: VK-GL-CTS は Apache-2.0。外部の取得物（glslang・SPIRV-Tools・amber・jsoncpp・zlib・libpng・vk_video_samples など）の license の監査と、zedBSD への port（platform 層、C++17 の runtime）が要る。最初は QEMU の venus で `dEQP-VK.api.*`・`dEQP-VK.info.*` から。
- **段**（§8）: p002 header の差し替え（1.0 のまま、ABI 不変を確かめる）→ p003 版と dispatch の土台と不整合の直し → p004 1.1 → p005 1.2 → p006 1.3（command と pNext の符号化、venus で 1.3）→ p007 CTS の取得・port・build → p008 確かめ（venus で CTS の subset、app の回帰）→ p009 規約。**native の i915・bcm2711 の 1.1〜1.3 の feature は別の WS に分ける**（推奨、D3）。
- **決めてほしい点**: D1〜D7（§9、どれも推奨つき）。

## 1. 調べた事実（agent/p4 `6aeb50143` = main の tree）

### 1.1 header と provenance

| 事実 | 所在 |
| --- | --- |
| public の header は `vulkan.h`（23 行、zedBSD の wrapper で `vulkan_core.h`・`vulkan_video.h`・`vulkan_display_control.h` を常に、`vulkan_wayland.h` を `VK_USE_PLATFORM_WAYLAND_KHR` の時に include）、`vk_platform.h`（27 行、zedBSD の物）、`vulkan_core.h`（5720 行）、`vulkan_wayland.h`（56）、`vulkan_external.h`（585）、`vulkan_video.h`（774）、`vulkan_display_control.h`（148）、`vk_video/` の 3 file、`API-PROVENANCE.md`、`LICENSE-API` | `include/libc/vulkan/` |
| `vulkan_core.h` は Khronos v1.3.269 の `include/vulkan/vulkan_core.h`（sha256 `8cb01233…8798287`、手元の取得で一致を確かめた）から 1.0 の 137 command と WSI の 18 command を Noct の tool で選んで整形した物 | `API-PROVENANCE.md:1-40`、`userland/desktop/libvulkan/tools/maintain-api.noct` |
| `vulkan_video.h`・`vulkan_display_control.h` は Debian の `libvulkan-dev` 1.4.309 の `vulkan_core.h` から選んだ。`vk_video/` の 3 file は 1.4.309 の物で、1.3.269 の物とは著作権の行の年（2023 と 2025）だけが違う（手元で diff を取った） | `API-PROVENANCE.md` の ws083-p002・ws113-p003 の節 |
| `vulkan_wayland.h` は 1.3.269 の物（sha256 `3728578b…88d12`）と照らして選んだ | `API-PROVENANCE.md` の q309 の節 |
| Khronos の 1.3.269 の C の header の SPDX は `Apache-2.0`、`registry/vk.xml` は `Apache-2.0 OR MIT`（手元の取得で確かめた）。repository の `LICENSE.md` は「Apache-2.0 と MIT、MIT の file（`parse_dependency.py` を除く）は Apache-2.0 でもある」 | 取得物 `build/tmp/ws226-p001/h269/` |
| header は sysroot の作成（`toolchain/llvm/sysroot.mk:93-101` は `include/libc` を `find` で拾う、`:149-177` は header ごとに `-nostdinc -Iinclude/libc` で compile して確かめる）と kernel（`src/drivers/gpu/i915/render/*.c` などが `vulkan/vulkan_core.h` を include。guardrail「kernel が include してよい libc の header は `libc/vulkan/*` だけ」）が使う。Khronos の `vk_platform.h` は `<stddef.h>`・`<stdint.h>` を `VK_NO_STDDEF_H`・`VK_NO_STDINT_H` の無い時に include する（kernel の source も `<stdint.h>` を既に include している） | — |
| target の C は `-std` の指定無し（clang の既定）、host 試験は `-std=gnu89`（`//` の comment は GNU の拡張で通る） | `Makefile`、例 `plan/ws113/tests/host-output-switch.sh` |
| `<vulkan/vulkan.h>` を include する所: compositor（7 file）、vkdemo（3）、vkvideo-probe（2）、libkeiland・backend（4）、xserver・terminal・settings・notes・monitor・libmedia・libegl・libbrowser・libvulkan-compat ほか。`vulkan_external.h` を直に include する所が 1、`vulkan_wayland.h` が 2。tree の中に自前の `PFN_vk`・`Vk` の struct の宣言は無い | `grep` |

### 1.2 libvulkan の作りと今の範囲

| 事実 | 所在 |
| --- | --- |
| libvulkan は userland の共有 library。command を Venus の wire（command の番号は virglrenderer 1.1.0 の venus-protocol と同じ、zedBSD だけの番号は `0x10000` から）に符号化し、kernel の GPU の node へ送る。backend は 4 種: QEMU の Venus（host の virglrenderer 1.1.0、xml 1.3.269）、WINQ-EMU の Venus（xml 1.4.343）、native の i915 の executor、native の bcm2711 の executor | `README.md`、`context.c:26-27, 164-176`、`include/uapi/gpu-op.h:8-40` |
| command の一覧 `api-commands.tsv`（197 行: 1.0 の 137、拡張 60）、dispatch の表 `dispatch-table.inc`、export の `exports.map`、符号化 `commands-generated.inc`・`resources-generated.inc`・`codec.c`（5167 行）。生成は Noct の tool（`tools/maintain-*.noct`）で、header から選ぶ | `userland/desktop/libvulkan/` |
| `vkCreateInstance` は `apiVersion` が 1.0 以外なら `VK_ERROR_INCOMPATIBLE_DRIVER`（1.0 の実装として正しい） | `instance.c:59-66` |
| physical device: backend の `apiVersion` が 1.1 未満なら `VK_ERROR_INCOMPATIBLE_DRIVER`（「Venus の queue の経路は native の 1.1 が要る」）、その後 `apiVersion = 1.0` に書き換える。limit は `minMemoryMapAlignment = 64` だけ書き換え、features は backend の値をそのまま | `instance.c:1069-1084` |
| 名乗る拡張: instance 7（surface・display・wayland_surface・get_physical_device_properties2・external_memory_capabilities・external_fence_capabilities・display_surface_counter）、device 13（swapchain・display_swapchain・incremental_present・dedicated_allocation・get_memory_requirements2・external_memory・external_memory_fd・external_fence・external_fence_fd・synchronization2・video_queue・video_decode_queue・video_decode_h264・display_control。video と sync2 は capset の video の印のある native の i915 だけ） | `instance.c`（`available[...]`）、`device.c:322-443`、`README.md` の ws083 の節 |
| 拡張の依存の検査は device の作成で自前に持つ（swapchain → surface、video → sync2 → properties2 など） | `device.c:398-443` |
| `vkGetInstanceProcAddr` は 1.0 の core を常に、拡張の command は有効な時だけ返す。`vkGetDeviceProcAddr` は device の command だけ、拡張は有効な時だけ | `dispatch.c:74-158` |
| synchronization2 の 6 command は 1.0 の command に訳す（1.0 に無い stage は ALL_COMMANDS、access は MEMORY_READ\|WRITE に広げる） | `sync2.c`、`README.md` |
| host から見える memory は 256 MiB の aperture で有限 | `README.md` |

### 1.3 kernel の backend（native の executor）

| 事実 | 所在 |
| --- | --- |
| i915 の executor は `apiVersion = 1.1`（注記「library が native の 1.1 を要る」）、limit は `standardSampleLocations`・copy の alignment 2 つ・`nonCoherentAtomSize` だけを書き、他は 0（注記「XXX: Gen12 が楽に満たす値、hardware の最大の写しではない」）。features は `vertexPipelineStoresAndAtomics`・`logicOp`・`shaderInt16`・`geometryShader` の 4 つだけ（`robustBufferAccess` も無い） | `src/drivers/gpu/i915/render/instance.c:280-330, 450-500` |
| bcm2711 の executor は `apiVersion = 1.1`、limit は一部を書く: `maxImageArrayLayers 1`、`maxColorAttachments 1`、`maxFragmentOutputAttachments 1`、`maxFramebufferLayers 1`、`framebufferColorSampleCounts` は 1 だけ、`maxPerStageResources 20`、`maxDrawIndirectCount 0` ほか（Vulkan 1.0 の最小は 256・4・4・256・1\|4・128・1） | `src/drivers/gpu/bcm2711/vulkan-query.c:290-375` |
| 共有の SPIR-V の前段は `OpCapability`・`OpExtension`・`OpMemoryModel` を読み飛ばし、訳せない命令を断る（能力の宣言で断るのではなく、命令で断る） | `src/drivers/gpu/compiler/spirv.c:1600-1650` |
| venus の kernel の driver は command の stream を中を見ずに運ぶ（private の framing の検査だけ） | `src/drivers/gpu/venus/transport.c:591`、`share.c:427` |
| i915 の executor の opcode の振り分けは範囲で行う（file の中の名前の grep では数えられない）。bcm2711 は opcode の名前で振り分ける（grep の目安で 83 の opcode、indexed の draw・compute の dispatch・query の名前が見当たらない。数は p003 で道具を作って測る） | `i915/render/dispatch.c:1-60`、`bcm2711/*.c` |

### 1.4 外の事実（取得した物と公開の情報）

- Vulkan-Headers v1.3.269: tag の object `693adcd4c4c280bc5f4ff2926699600977271dce`、commit `374f9fd97520f6dd1b80745de09208d878ab4a52`、GitHub の tarball の sha256 `1637f36a023bd148315f66efb7974861adf22cd1f6d690bdf00ee15ce91d5367`（2026-10-13 取得）。v1.3.296（1.3 の最後）: commit `29f979ee5aa58b7b005f805ea8df7a855c39ff37`、tarball `e204e0b3c19f622d197df945737f5db913d6621830999b8578d34e80a7c90585`。GitHub の tarball は作り直されると hash が変わり得るので、台帳は commit と file ごとの sha256 を正とする。
- Vulkan 1.3 の必須の feature（Vulkan-Docs v1.3.269 `chapters/features.adoc` の「Feature Requirements」、7196 行から）: 1.0 の `robustBufferAccess`、1.1 の `multiview`、1.2 の `uniformBufferStandardLayout`・`subgroupBroadcastDynamicId`・`imagelessFramebuffer`・`separateDepthStencilLayouts`・`hostQueryReset`・`timelineSemaphore`・`shaderSubgroupExtendedTypes`、1.3 の `vulkanMemoryModel`・`vulkanMemoryModelDeviceScope`・`bufferDeviceAddress`・`robustImageAccess`・`inlineUniformBlock`（と descriptorIndexing がある時は `descriptorBindingInlineUniformBlockUpdateAfterBind`）・`pipelineCreationCacheControl`・`privateData`・`shaderDemoteToHelperInvocation`・`shaderTerminateInvocation`・`subgroupSizeControl`・`computeFullSubgroups`・`synchronization2`・`shaderZeroInitializeWorkgroupMemory`・`dynamicRendering`・`shaderIntegerDotProduct`・`maintenance4`。加えて limit の最小値と必須の format の表（p003 の道具で照らす）。
- VK-GL-CTS: 最新の 1.3 の tag は `vulkan-cts-1.3.10.0`。`LICENSE` は Apache-2.0。`external/fetch_sources.py`（1.3.10.0）が取る物: zlib 1.2.13（tarball と sha256）、libpng 1.6.27、renderdoc の `renderdoc_app.h`、SPIRV-Tools・glslang・SPIRV-Headers・Vulkan-Docs・Vulkan-ValidationLayers・amber・jsoncpp・vk_video_samples（Igalia の fork）を commit で固定。
- 同じ hardware の他の実装: Raspberry Pi 4 の V3D 4.2 で Mesa の v3dv が Vulkan 1.3 を出し、Pi 4 と Pi 5 で 1.3 の CTS を全部通した（Mesa 24.3）。Intel の Gen12（5320 の Tiger Lake、5330 の Alder Lake）は Mesa の ANV が 1.3。どちらも MIT の実装だが、この設計は中身を参照しない（hardware で可能という事実だけを使う）。

## 2. 1.3 の core の command と今の差（vk.xml 1.3.269 と `api-commands.tsv` の機械的な照らし、`build/tmp/ws226-p001/cmds-1.3.269.txt`）

| 版 | core | 実装あり（core の名前） | 拡張の名前でだけ実装 | 無い |
| --- | --- | --- | --- | --- |
| 1.0 | 137 | 137 | 0 | 0 |
| 1.1 | 28 | 0 | 12 | 16 |
| 1.2 | 13 | 0 | 0 | 13 |
| 1.3 | 37 | 0 | 6 | 31 |
| 計 | 215 | 137 | 18 | 60 |

- **1.1 の無い 16**: `vkEnumerateInstanceVersion`、`vkEnumeratePhysicalDeviceGroups`、`vkGetPhysicalDeviceExternalSemaphoreProperties`、`vkBindBufferMemory2`、`vkBindImageMemory2`、`vkGetDeviceGroupPeerMemoryFeatures`、`vkCmdSetDeviceMask`、`vkCmdDispatchBase`、`vkTrimCommandPool`、`vkGetDeviceQueue2`、`vkCreateSamplerYcbcrConversion`、`vkDestroySamplerYcbcrConversion`、`vkCreateDescriptorUpdateTemplate`、`vkDestroyDescriptorUpdateTemplate`、`vkUpdateDescriptorSetWithTemplate`、`vkGetDescriptorSetLayoutSupport`。
- **1.1 の拡張の名前だけの 12**: `vkGetPhysicalDevice{Features,Properties,FormatProperties,ImageFormatProperties,QueueFamilyProperties,MemoryProperties,SparseImageFormatProperties}2`（KHR_get_physical_device_properties2）、`vkGet{Buffer,Image,ImageSparse}MemoryRequirements2`（KHR_get_memory_requirements2）、`vkGetPhysicalDeviceExternal{Buffer,Fence}Properties`。
- **1.2 の無い 13**: `vkCreateRenderPass2`、`vkCmdBeginRenderPass2`、`vkCmdNextSubpass2`、`vkCmdEndRenderPass2`、`vkCmdDrawIndirectCount`、`vkCmdDrawIndexedIndirectCount`、`vkResetQueryPool`、`vkGetSemaphoreCounterValue`、`vkWaitSemaphores`、`vkSignalSemaphore`、`vkGetBufferDeviceAddress`、`vkGetBufferOpaqueCaptureAddress`、`vkGetDeviceMemoryOpaqueCaptureAddress`。
- **1.3 の無い 31**: `vkGetPhysicalDeviceToolProperties`、`vkCreatePrivateDataSlot`、`vkDestroyPrivateDataSlot`、`vkSetPrivateData`、`vkGetPrivateData`、`vkCmdCopyBuffer2`、`vkCmdCopyImage2`、`vkCmdCopyBufferToImage2`、`vkCmdCopyImageToBuffer2`、`vkCmdBlitImage2`、`vkCmdResolveImage2`、`vkCmdBeginRendering`、`vkCmdEndRendering`、`vkCmdSetCullMode`、`vkCmdSetFrontFace`、`vkCmdSetPrimitiveTopology`、`vkCmdSetViewportWithCount`、`vkCmdSetScissorWithCount`、`vkCmdBindVertexBuffers2`、`vkCmdSetDepthTestEnable`、`vkCmdSetDepthWriteEnable`、`vkCmdSetDepthCompareOp`、`vkCmdSetDepthBoundsTestEnable`、`vkCmdSetStencilTestEnable`、`vkCmdSetStencilOp`、`vkCmdSetRasterizerDiscardEnable`、`vkCmdSetDepthBiasEnable`、`vkCmdSetPrimitiveRestartEnable`、`vkGetDeviceBufferMemoryRequirements`、`vkGetDeviceImageMemoryRequirements`、`vkGetDeviceImageSparseMemoryRequirements`。
- **1.3 の拡張の名前だけの 6**: `vkQueueSubmit2`、`vkCmdPipelineBarrier2`、`vkCmdSetEvent2`、`vkCmdResetEvent2`、`vkCmdWaitEvents2`、`vkCmdWriteTimestamp2`（KHR_synchronization2、1.0 の command への訳）。
- command の他に、1.1〜1.3 の core の struct（`VkPhysicalDeviceVulkan11/12/13Features`・`…Properties`、各 promoted の拡張の feature・property の struct、`VkRenderingInfo`、`VkDependencyInfo` ほか）の pNext の符号化と、`Properties2`・`Features2` の返事の chain の復号が要る（数は p003 の道具で vk.xml から数える）。

## 3. 不整合の一覧（1.0 を名乗りながら 1.1 以降を前提にしている所、と周辺）

| ID | 不整合 | 仕様の根拠 | 直し方（段） |
| --- | --- | --- | --- |
| I1 | device の `apiVersion` は 1.0 なのに `VK_KHR_video_queue` を名乗る（README の N1） | vk.xml の `VK_KHR_video_queue depends="VK_VERSION_1_1+VK_KHR_synchronization2"`（1.1 が必須、拡張では代わりにならない） | video を名乗るのは device が 1.1 以上の時だけ（p004）。それまでの i915 の video は D4 |
| I2 | `VK_KHR_sampler_ycbcr_conversion` 無しに NV12（`G8_B8R8_2PLANE_420_UNORM`）と `PLANE_0/1` の aspect を使う（N2） | multi-planar の format と plane の aspect は ycbcr の拡張か 1.1 の core | 1.1 で `samplerYcbcrConversion` の feature と command（p004、backend が無ければ feature を 0 で、format の feature も出さない） |
| I3 | OPTIMAL の decode の画像に `vkGetImageSubresourceLayout` が plane の offset・pitch を答える（N3、私的な約束） | 1.0 の valid usage は LINEAR か DRM の modifier の tiling に限る | 仕様に無い約束として README に残すか、`TRANSFER_SRC` の copy で置き換える（D5） |
| I4 | header の版の混在: core の選択は 1.3.269、video・display_control・`vk_video/` は 1.4.309 | — | p002 で 1 つの版（1.3.269）の file を改変せずに置く |
| I5 | 1.3.269 の header で `VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR` は beta の encode の値なので、parameter set の誤りを `VK_ERROR_INITIALIZATION_FAILED` で返す | video の仕様の誤りの値 | 1.3.269 のままなら今のまま（記録）。v1.3.296 を選べば正しい値（D1） |
| I6 | native の i915・bcm2711 は libvulkan の「backend は 1.1 以上」の検査を通すためだけに `apiVersion = 1.1` を名乗る。中身は 1.0 の最小の limit も満たさない | Vulkan の `apiVersion` は device が満たす版 | backend の wire の能力（queue の経路）と Vulkan の版を別の値にする（capset か property の私的な語、p003） |
| I7 | i915 の limit の多くが 0、bcm2711 の limit の一部が 1.0 の最小値より小さい、両方とも `robustBufferAccess` が無い。libvulkan は検べずにそのまま返す | 1.0 の「Limit Requirements」と「Feature Requirements」 | libvulkan が backend の値を表と照らして log に出す（p003）。直すのは backend の仕事（D3） |
| I8 | instance の `apiVersion` に 1.0 以外を断る | 1.1 以降の実装は任意の `apiVersion` を受ける | p003 で instance を 1.3 に（D2） |
| I9 | 1.1〜1.3 で core に入った command を拡張の名前でだけ export し、core の名前の symbol と proc addr が無い | core の command は core の名前で引ける | p004〜p006 で core の名前の entry を足し、同じ関数を指す |
| I10 | `vkGetDeviceProcAddr` の gate が拡張だけで、版を見ない（1.0 のままでは問題が無い） | device の版を超える core の command は NULL | p003 で entry に「core に入った版」を持たせる |
| I11 | synchronization2 を 1.0 の command に広げて訳す。1.3 では `synchronization2` が必須の feature で、`VK_PIPELINE_STAGE_2_*` の細かい stage（copy・blit・clear など）の意味を保つ必要がある | 1.3 の feature の要件 | 訳しは広げる方向なので正しさは保つ（遅いだけ）。backend が sync2 の opcode を持てばそのまま送る（p006） |
| I12 | host から見える memory が 256 MiB の aperture で有限なのに、`maxMemoryAllocationSize`（1.1 の最小 2^30）を報告していない（1.0 では property が無い） | 1.1 の limit の最小値 | 1.1 で 2^30 以上を報告し、足りない時は allocation の失敗（`VK_ERROR_OUT_OF_DEVICE_MEMORY`）で返す（p004、仕様どおり） |

## 4. device ごとの 1.3 の見込み

| device | 今 | 1.3 に要る物 | 判断（推奨） |
| --- | --- | --- | --- |
| **venus（QEMU、host の Intel の driver）** | host の版と feature を libvulkan が 1.0 に絞る | libvulkan の 60 command と pNext の struct の符号化（Venus の wire は全部の command の番号を持つ）、host の 1.3 の feature の素通し、`maxMemoryAllocationSize` の扱い | **1.3 を名乗る最初の device**（p006）。WINQ-EMU の Venus（xml 1.4.343）も同じ経路 |
| **i915（5320・5330、native）** | 1.0 の最小にも届かない（§1.3） | executor: limit・feature の正しい報告、timeline semaphore、dynamic rendering、descriptor の inline uniform block と update-after-bind、BDA（64 bit の address を shader に）、query の host reset、render pass 2、dynamic state。compiler: SPIR-V 1.6、Vulkan の memory model、subgroup（size の制御と full subgroup）、demote・terminate、整数の dot product、workgroup の 0 初期化 | hardware は可能。**この WS では「満たす版を正しく名乗る」まで**（1.0 を名乗り、足りない物を log と記録に）。1.1〜1.3 の中身は別の WS（D3） |
| **bcm2711（Pi 4、native）** | limit の一部が 1.0 の最小より小さい、opcode の範囲も狭い（目安） | i915 と同じ種類の物（V3D 4.2 の制約の中で） | 同上（D3）。v3dv が同じ hardware で 1.3 を通したので不可能ではない |

1.3 を名乗れない device は「満たす最も高い版」を名乗る（推奨 D2: instance は 1.3、physical device はそれぞれ）。満たさないまま 1.0 を名乗る今の native の状態（I7）は、この WS で直さず記録し、別の WS で直す（D3）。

## 5. header の取り込みの形

1. **取得**: `include/libc/vulkan/UPSTREAM`（新、zedBSD の file、Zlib）に、repository（`https://github.com/KhronosGroup/Vulkan-Headers`）、tag `v1.3.269`、commit `374f9fd97520f6dd1b80745de09208d878ab4a52`、写す file の一覧と file ごとの sha256、取得の日を書く。更新の script `userland/desktop/libvulkan/tools/fetch-headers.sh`（新）は `build/tmp/` の新しい directory に tarball か `git archive` を取り、file ごとの sha256 を台帳と照らし、一致した時だけ写す（rm を使わない、出力は新しい directory）。外部 package の規則（tarball を取得・検証）に倣う。tarball の hash は GitHub が作り直すと変わるので、正は file ごとの sha256。
2. **写す file（改変なし）**: `include/vulkan/` の C の header の全部（`vulkan.h`・`vk_platform.h`・`vulkan_core.h`・`vulkan_wayland.h`・`vulkan_beta.h` と他の platform の header。`vulkan.h` が `#ifdef VK_USE_PLATFORM_*` で include するので、片方だけ写すと他の定義で壊れる）、`include/vk_video/` の全部（h264・h265 の decode・encode と common の 7 file）。C++ の物（`*.hpp`・`vulkan.cppm`）、loader の interface（`vk_icd.h`・`vk_layer.h`）、`registry/` は写さない（vk.xml は tool の入力として `build/tmp/` に取得するだけ、または `third_party` に置くかは D6）。
3. **zedBSD だけの物**: 今の public の header に zedBSD の独自の宣言は無い（`vulkan_external.h`・`vulkan_video.h`・`vulkan_display_control.h` は Khronos の宣言の選択で、`vulkan_core.h` に全部入っている）。よって消える。private の wire の番号は `include/uapi/gpu-op.h` のまま。将来 zedBSD の独自の拡張を足す時は `include/libc/vulkan/vulkan_zedbsd.h`（zedBSD の表示、Zlib）にし、Khronos の file は触らない。`vulkan_external.h` を直に include する 1 か所と、`vulkan_wayland.h` の 2 か所は `<vulkan/vulkan.h>` の platform の define に直す（p002）。
4. **表示と license**: 写した file は Khronos の表示のまま（ws.md の完了の条件 1、WS224 の 2 つの block の形は使わない）。`LICENSE-API` は Apache-2.0 の全文のまま、`API-PROVENANCE.md` を「改変なしの写し」の説明に書き直し、`plan/ws032/provenance.md` と `plan/tools/packages/audit-licenses.sh` の inventory に `include/libc/vulkan/` の行を足す（Q1 が master・guardrail へ）。
5. **tool の扱い**: `tools/maintain-api.noct`・`maintain-external.noct`・`maintain-video.noct`・`maintain-display.noct` は header を作らなくなる（廃止）。`maintain-dispatch.noct`・`maintain-commands.noct`・`maintain-codec.noct`・`maintain-resources.noct` は入力を「写した `vulkan_core.h` と、実装する command の一覧（`api-commands.tsv` に core に入った版の列を足す）」にする。
6. **ABI の不変**: p002 は header だけを替え、library の export（155 と拡張）と構造体の layout が変わらないことを確かめる（`check` の ELF の照合、sysroot の header の compile の確かめ、全部の consumer の build）。

## 6. libvulkan の設計

### 6.1 版

- instance: `vkEnumerateInstanceVersion` は 1.3（`VK_HEADER_VERSION` は 269）。`vkCreateInstance` は任意の `apiVersion` を受け（1.1 以降の規則）、instance の版は min(要求, 1.3)。
- physical device: `apiVersion` = min(libvulkan が実装しきった版, backend が本当に満たす版)。backend の値は I6 の直しの後の物。venus は host の値（host の 1.3 以上なら 1.3）。native は executor が書く本当の値（p003 の後は 1.0、D3 の WS で上がる）。
- device の版（command の gate）は min(instance の版, physical device の版)。

### 6.2 command の 3 つの種類

| 種類 | 例 | 実装 |
| --- | --- | --- |
| (a) libvulkan だけ | `vkEnumerateInstanceVersion`、`vkEnumeratePhysicalDeviceGroups`（device ごとに 1 つの group）、`vkGetDeviceGroupPeerMemoryFeatures`、`vkCmdSetDeviceMask`（mask 1 だけ）、`vkTrimCommandPool`（何もしない、仕様で可）、`vkGetDeviceQueue2`、`vkGetPhysicalDeviceToolProperties`（0 個）、private data の 4 つ、descriptor update template（1.0 の `vkUpdateDescriptorSets` に展開）、`vkGetPhysicalDeviceExternalSemaphoreProperties`（semaphore の外部の handle は無い、0） | library の中 |
| (b) 1.0 に下ろせる | `vkBindBufferMemory2`・`vkBindImageMemory2`（pNext が無い時）、render pass 2 の 4 つ（2 だけの機能が無い時）、copy・blit・resolve の `…2`（pNext が無い時）、`vkCmdDispatchBase`（base が 0 の時）、synchronization2（今の訳）、`vkGetDevice*MemoryRequirements`（一時の object を作って問う） | 下ろせない入力は backend の opcode へ、backend が無ければ feature を出さない |
| (c) backend が要る | timeline semaphore の 3 つ、BDA の 3 つ、`vkResetQueryPool`、indirect count の 2 つ、ycbcr、dynamic rendering、dynamic state の 13、`vkGetDescriptorSetLayoutSupport` | Venus の番号でそのまま送る。native の backend は capset の印で opcode を持つと告げた時だけ |

backend が (c) を持つかは capset の印（command ごとか版ごと）で知る。持たない command がある版は、その device の版として名乗らない。

### 6.3 dispatch・export・符号化

- `api-commands.tsv` に「core に入った版」と「元の拡張」の列を足す。dispatch の entry は、core の名前は版で、拡張の名前は拡張の有効で gate する（promoted な拡張は、どちらかが満たせば同じ関数）。
- export: core の 1.1〜1.3 の全部（loader を通さない app が直に link できるように）。拡張の名前の export は今のまま。
- 符号化: `maintain-codec.noct` を pNext の struct の全部（1.1〜1.3 の core の struct と promoted の拡張の struct）に広げる。Venus の wire は struct の種類の番号で chain を運ぶので、host が知る 1.3.269 の struct の範囲で足りる。
- feature と property: `vkGetPhysicalDeviceFeatures2` の chain は backend の値を device の版と libvulkan の実装で絞る（実装していない機能は 0）。`VkPhysicalDeviceVulkan11/12/13Features/Properties` を正しく埋める。

### 6.4 不整合の直し（§3）の置き場所

I6・I7・I8・I10 は p003、I1・I2・I12 は p004（1.1）、I9 は版ごとの段、I4 は p002、I11 は p006、I3・I5 は D5・D1。

## 7. 試験

- **VK-GL-CTS**: Apache-2.0。取得は tag（推奨 `vulkan-cts-1.3.10.0`、または header に近い 1.3.7 の系列）の tarball と、`external/fetch_sources.py` が commit で固定する外部の物。**外部の物の license は取得して照らす**（zlib・libpng は寛容な license、SPIRV-Tools・SPIRV-Headers・Vulkan-Docs・Vulkan-ValidationLayers・amber は Apache-2.0 系、jsoncpp は MIT か public domain、vk_video_samples は fork 元の license。**glslang は file によって license が分かれ、生成された parser の一部に GPL 系の物が含まれると知られている。GPL の本文は読まずに、`LICENSE.txt` の一覧で対象の file を確かめ、CTS の build に要るか（prebuilt の SPIR-V で避けられるか）を p007 で決める**）。CTS は試験の image にだけ入れ、配布の image には入れない。
- **build の可否**: CTS は C++17・CMake・Python で、platform 層（framework/platform）に zedBSD が無い。Linux の「surfaceless」系の platform を土台に zedBSD の platform を足す（CTS の側の patch、外部 package の規則で tarball に patch）。build は zedBSD の target の clang・libcxx の package（`userland/packages/lang/clang`・`devel/libcxx`）で行う。共有の toolchain は変えない（package の build は T1・Q1 の許可の範囲、2026-10-03 の Q1 の判断）。libvulkan は `dlopen` で `libvulkan.so`（SONAME は `libvulkan.so`、Linux の `libvulkan.so.1` ではない）を開くので、CTS の library の名前の設定を合わせる。
- **流す範囲**: 全部の mustpass は QEMU では長すぎるので、最初は `dEQP-VK.info.*`・`dEQP-VK.api.*`（version_check・feature_info・device_init・smoke・info など）、次に `dEQP-VK.memory.*`・`synchronization2`・`dynamic_rendering`・`pipeline` の一部。数と時間は p007 で測り、夜間の耐久の枠（ユーザーに確かめる）に回す物を分ける。
- **代わりの試験**（CTS が使えない間）: Vulkan-Tools の `vulkaninfo`（Apache-2.0）で properties・features・limit を表の最小値と照らす小さい checker（`plan/ws226/tests/`、zedBSD の独自の物）。
- **回帰**: compositor（AAT の smoke）、libkeiland、Video（vkvideo-probe）、vkdemo、display-events、libegl、xserver、browser の build と QEMU の起動、5330・5320・Pi 4 の起動（T1）。build は amd64・rpi4・keiland-linux・keiland-freebsd の warning 0。

## 8. 段の分け（案）

| Phase | 内容 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| p002 | header の差し替え: 1.3.269 の C の header と `vk_video/` を改変なしで写す、`UPSTREAM` と取得の script、`API-PROVENANCE.md`・inventory、選択の tool の廃止、`vulkan_external.h` などの include の直し。library の版と export は不変 | file ごとの sha256 が台帳と一致、sysroot の header の確かめ、amd64・rpi4・keiland-linux・FreeBSD と kernel の build の warning 0、ELF の export の照合が前と同じ、T1 の boot と AAT の smoke | p001 の review、D1・D6 |
| p003 | 版と dispatch の土台: command の表に版の列、dispatch の gate（I10）、instance の 1.3（I8）、backend の wire の能力と Vulkan の版を分ける（I6、kernel の i915・bcm2711・venus の capset か property）、limit と feature の表の照らしの log（I7）、opcode の範囲を測る道具 | host 試験（dispatch の gate の表、版の min）、physical device の版が backend ごとに正しい、`vulkaninfo` 相当の checker | p002 |
| p004 | 1.1: 16 の command と 12 の core の名前、pNext の 1.1 の struct、`multiview`・ycbcr（I2）・`maxMemoryAllocationSize`（I12）、video の名乗りの条件（I1） | host 試験、venus で 1.1 を名乗り CTS の `api`・`info` の 1.1 の部分（p007 の後は CTS、前は checker） | p003 |
| p005 | 1.2: 13 の command、timeline・BDA・render pass 2・indirect count・host の query reset の符号化、1.2 の必須の feature | 同上（1.2） | p004 |
| p006 | 1.3: 31 の command と 6 の core の名前、dynamic rendering・dynamic state・copy2・private data・maintenance4 の符号化、sync2 の素通し（I11）、1.3 の必須の 17 の feature の素通し | venus で 1.3 を名乗る、CTS の subset | p005 |
| p007 | VK-GL-CTS の取得・license の監査・zedBSD の platform の patch・target の build（試験の image だけ） | 試験の image で `deqp-vk` が動き、`dEQP-VK.info.*` が流れる | p002（p004〜p006 と並行可） |
| p008 | 確かめ: venus で CTS の subset、app の回帰（AAT、Video、vkdemo）、i915・bcm2711 で版が正しく下がっていること（実機、T1 と Q1 経由） | 結果の記録、ws.md の完了の条件の判定 | p006、p007 |
| p009 | 規約の全文の見直し | 違反 0 | p008 |

native の i915・bcm2711 の 1.1〜1.3 の中身（executor と compiler）は別の WS（D3 の推奨。device ごとに 1 つ）。

## 9. 決めてほしい点（推奨つき）

- **D1 header の版**: (a) **v1.3.269（推奨）**: 今の provenance と同じ、virglrenderer 1.1.0 の venus-protocol が同じ file を持つ（wire の struct の範囲が host と合う）、Video の decode の拡張は非 beta で含まれる。欠点は I5（beta の encode の値）。(b) v1.3.296（1.3 の最後）: Video の encode と H.265 が正式な値、I5 が消える。host の venus（1.3.269）が知らない struct を送らない仕組みが要る。
- **D2 版の名乗り**: (a) **instance は 1.3、physical device はそれぞれが満たす版（推奨）**: 仕様どおり、venus は 1.3、native は 1.0。(b) 全部の device が 1.3 を満たすまで instance も 1.0: app から見て単純だが venus の 1.3 が使えない。
- **D3 native の device の中身**: (a) **この WS は libvulkan と版の名乗りまで、i915・bcm2711 の 1.1〜1.3 の executor と compiler は device ごとの別の WS（推奨）**: 大きさが違い、実機の試験が要り、WS は 1 つの到達目標（AGENTS.md）。(b) この WS に含める。
- **D4 i915 の Vulkan Video（I1）**: (a) **1.1 を名乗れるまで video の拡張を出さない（仕様どおり、推奨）**。Video の app（libmedia・vkvideo-probe）は i915 で使えなくなる期間ができる。(b) 今の私的な許し（ユーザーの判断 H2・HD6、2026-10-07）を i915 の 1.1 の WS まで続け、README に残す。どちらにするかはユーザーの判断（前の判断を変えるため）。
- **D5 OPTIMAL の `vkGetImageSubresourceLayout`（I3）**: (a) **copy の経路（decode の画像の `TRANSFER_SRC`）に置き換えて私的な約束を外す（推奨）**。(b) README に私的な約束として残す。
- **D6 vk.xml の置き場**: (a) **tool の入力として `build/tmp/` に取得するだけで tree に入れない（推奨）**: 生成物（dispatch・codec の .inc）は tree に入れて、生成の時だけ取得する。(b) `third_party/vulkan-headers/registry/vk.xml` として tree に入れる。
- **D7 CTS の流す範囲と時間**: (a) **`info`・`api` を T1 の通常の枠、他は夜間の枠（推奨）**。(b) 全部の mustpass を夜間に分割して流す。

## 10. 敵対的レビュー（自分で）

- **数の確かさ**: §2 の数は vk.xml の `<feature name="VK_VERSION_1_x">` の `require` の command と `api-commands.tsv` の名前の照らし（`build/tmp/ws226-p001/cmds-1.3.269.txt`）。alias は vk.xml の `alias` の属性で取った。tsv の名前と実際の export（`exports.map`）が食い違えば数がずれる。p003 で export の照合を足す。
- **native の opcode の範囲**: grep の目安で、i915 は範囲で振り分けるので数えていない。§4 の判断（1.0 の最小にも届かない）は limit と feature の報告の source に依り、opcode の範囲には依らない。
- **venus の 1.3**: host の Vulkan の版と virglrenderer 1.1.0 が 1.3 の command を全部運ぶことは、gpu-op.h の注記と context の xml の版からの推論で、QEMU で確かめていない（p006 の受け入れで確かめる）。host の Intel の driver の版は試験の host に依る。
- **CTS の license**: glslang の GPL 系の部分は「知られている」段階で、本文を読まずに一覧で確かめる（p007）。確かめるまで CTS の build を始めない。
- **sysroot と toolchain**: header の置き換えは `include/libc` の中だけで、`toolchain/llvm/sysroot.mk` は `find` で拾うので変更は要らない見込み。sysroot の header の確かめ（`-nostdinc` の compile）が Khronos の `vulkan.h` の platform の header（`vulkan_xlib.h` など、X11 の header を要る）で失敗する恐れがある。`#ifdef` の中なので compile は通る見込みだが、p002 の受け入れで確かめる。通らなければ写す file を減らすのではなく（`vulkan.h` が壊れる）、sysroot の確かめの対象から外す方法を Q1 に相談する（toolchain の file なので Q1 の許可）。
- **kernel**: kernel が 18,000 行の `vulkan_core.h` を include すると compile の時間が増える。名前の衝突は無い見込み（kernel の `drv_`・`i915_` の名前）。p002 で kernel の build を確かめる。
- **選ばなかった案**: header を選んで整形し続ける（WS224 の形）は、ユーザーの決定（そのまま使う）に反する。仕様から書き直す（WS225）は blocked のまま。libvulkan の代わりに Mesa を移植するのは WS の目標（libvulkan で実装しきる）の外。

## 11. ライセンスと転記

- 写すのは Khronos の Vulkan-Headers の C の header（Apache-2.0、改変なし）だけ。vk.xml（Apache-2.0 OR MIT）は tool の入力。Vulkan-Docs（features.adoc）は事実（必須の feature の名前）を読んだだけで、文は写していない。
- Mesa（ANV・v3dv、MIT）の source は読んでいない（hardware で 1.3 が可能という公開の事実だけ）。GPL の物は読んでいない。
- venus-protocol の番号の扱いは今の `gpu-op.h` の注記のまま（引用、写さない）。

## 12. 取得した物（`build/tmp/ws226-p001/`、Q1 が後で消す）

- `vulkan-headers-1.3.269.tar.gz`（sha256 `1637f36a…5367`）、`vulkan-headers-1.3.296.tar.gz`（`e204e0b3…0585`）、展開の `h269/`・`h296/`
- `features-1.3.269.adoc`（Vulkan-Docs v1.3.269 の `chapters/features.adoc`）
- `cts/LICENSE-vulkan-cts-1.3.10.0`・`cts/fetch_sources-vulkan-cts-1.3.10.0.py`（と 1.3.7.3 の同じ物）
- `cmds-1.3.269.txt`（§2 の照らしの出力）、`ops-*.txt`（opcode の grep の目安）

## Event

- 2026-10-13 / ws226-p001-design-v1: P4 が第 1 版を書いた（source は main `6aeb50143`）。コードは書いていない。design-reviewer の review-1 待ち。
