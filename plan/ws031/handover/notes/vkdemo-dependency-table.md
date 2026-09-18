# vkdemo／libvulkan が i915 executor に要求するもの（実 command 依存表）

WS031 E-107（2026-09-18）。読取りだけの調査（source と埋込み SPIR-V の decode）。path は `agent-1:~/zedBSD/` 基点。R = `userland/base/vkdemo/renderer.c`、D = `userland/base/vkdemo/display.c`。
**実 stream の記録（実行しての採取）ではない**。libvulkan の送信 code と vkdemo の呼出しから導いた表で、実行時の確認は VK-1 以降で行う。通常 Vulkan の公開はまだ成功扱いにしない。

## 0. 結論: opcode の不足より手前にある 4 つの関門

1. **libvulkan は今の i915 node を open 時に拒否する**。capset（id 4）は 156 byte で byte 0 = 1 だけ（`vk/vk.c:165-180`）。libvulkan は +4 に VK XML version 1.3.269（0x0040310D）、+152 に timeline 数（非 0）、全長 168 byte ちょうど、+160 に vendor 識別 0x5a424453、+164 に flags（OPAQUE 1／STRICT_QUEUE 2／QUIESCE 4、認める値は 1・3・7）を要求し、さらに strict_queue＋native_quiescence＋GPU_CAP_JOB＋GPU_CAP_JOB_CAPACITY が揃わない node を落とす（`context.c:115-186`、`instance.c:654-656`、`device.c:278-280`）→ 実質 flags = 7 が必要。
   **flags は「byte を合わせれば通る」ものではなく、kernel が守る契約の宣言**: STRICT_QUEUE = fence は成功時だけ完了、QUIESCE = native の仕事を他 session を失敗させずに退役できる、JOB／JOB_CAPACITY = `vkQueueSubmit` ごとに GPU_JOB_RESERVE（opcode 155 の timeline）→ COMMIT／CANCEL／CANCEL_FAULT、RESERVE が EAGAIN のとき GPU_JOB_CAPACITY で back-pressure、GPU_COMMAND_WAIT は**その submit の GPU 仕事が終わったときだけ** status 0（非 0 = DEVICE_LOST）。parity の request／fence／reset の上でこの契約を実装してから宣言する（magic byte を先に立てない）。
2. **未実装 builtin opcode が空成功だった**（payload が次の opcode として読まれる）→ **E-107 で撤去済み**（`vk/cmd.c`、§4）。
3. **SPIR-V parser は vkdemo の `-O0` vertex shader を下ろせない**（§2）。しかも未対応 opcode を黙って読み飛ばして成功を返す（header comment の「未対応は EINVAL」と実装が不一致）→ 誤った EU code が生成され得る。VK-2 で「下ろせない命令は拒否」へ直す対象。
4. **133 `vkCmdBeginRenderPass` に handler が無い**（135 end はある）。ほか recording 系 126／115／116／103／132 も無い（§3）。

## 1. vkdemo の呼出し順

`vkdemo_initialize`（R:147-236）= create_context → format 確認 3 件 → create_storage → publish_upload → create_descriptors → create_render_pass → create_targets → create_pipeline → create_commands → upload_texture。毎 frame は `vkdemo_render`（R:243-360）。compute／dispatch は無い。

| 段 | 呼出し（file:line） | 事実 |
|---|---|---|
| Instance | vkCreateInstance R:567、vkEnumeratePhysicalDevices R:575/601、vkGetPhysicalDeviceMemoryProperties R:618 | API 1.0。instance 拡張は KHR_surface／KHR_display（offscreen でないとき） |
| Display | vkGetPhysicalDeviceDisplayPropertiesKHR D:48/66、vkGetDisplayModePropertiesKHR D:314/332、vkCreateDisplayModeKHR D:374（fallback）、plane 系 D:406-478、vkCreateDisplayPlaneSurfaceKHR D:102 | 320×240、identity、opaque |
| Device | queue family R:698/718、surface support R:731・D:140、vkCreateDevice R:653、vkGetDeviceQueue R:660、properties R:676 | graphics+present queue 1 本、device 拡張 KHR_swapchain、feature 要求なし |
| Swapchain | surface caps D:149、formats D:552/570、vkCreateSwapchainKHR D:228、vkGetSwapchainImagesKHR D:234/252 | FIFO、RGBA8_UNORM 優先（BGRA8 可）、minImageCount+1、usage COLOR_ATTACHMENT（readback 時 +TRANSFER_SRC） |
| Format | vkGetPhysicalDeviceFormatProperties R:169/176/183 | colour、RGBA8 sampled、D32_SFLOAT |
| Storage | vkCreateImage R:863、requirements R:870、vkAllocateMemory R:820、bind R:876、view R:910、vkCreateBuffer R:938、requirements R:945、bind R:951、vkMapMemory R:958 | depth 320×240 D32、texture 64×64 RGBA8（TRANSFER_DST＋SAMPLED）、offscreen colour 320×240、upload buffer 20480 B（vertex @0、texel @4096）、readback 307200 B。全 image 2D／1 mip／optimal |
| Descriptors | sampler R:1082、set layout R:1100、pool R:1117、allocate R:1129、update R:1149、pipeline layout R:1163 | COMBINED_IMAGE_SAMPLER 1 個（set 0 binding 0、fragment）、nearest／clamp-to-edge、push constant 4 byte（vertex、offset 0） |
| Render pass | vkCreateRenderPass R:1253、vkCreateFramebuffer R:1322、vkCreateSemaphore R:1332/1576 | attachment 2（colour CLEAR/STORE、D32 CLEAR/DONT_CARE）、subpass 1 |
| Pipeline | vkCreateShaderModule R:1359、vkCreateGraphicsPipelines R:1520 | vertex stride 20（loc 0 = R32G32B32 @0、loc 1 = R32G32 @12）、TRIANGLE_LIST、cull なし、depth LESS＋write、blend なし、static viewport／scissor |
| Commands | pool R:1545、allocate R:1557、fence R:1566 | primary 1 本 |
| Upload（1 回） | begin R:1599、vkCmdPipelineBarrier R:1715/1688、vkCmdCopyBufferToImage R:1750、end R:1619、submit R:1645、wait R:1652 | |
| Frame | acquire R:271、reset fence R:289、reset pool R:296、barrier、vkCmdBeginRenderPass R:1808、bind pipeline R:1811、vertex buffers R:1813、descriptor sets R:1816、push constants R:1820、`vkCmdDraw(36,1,0,0)` R:1823、end R:1824、（readback: vkCmdCopyImageToBuffer R:1843）、submit＋wait、vkQueuePresentKHR R:333、vkQueueWaitIdle R:346 | |
| Teardown | vkDeviceWaitIdle R:440、destroy R:2038-2144、D:281/286、R:453/457 | |

vkFlush／InvalidateMappedMemoryRanges は HOST_COHERENT でない memory type のときだけ呼ばれ、libvulkan はその type を出さないので実行されない。

## 2. Shader と compiler の不足範囲

SPIR-V は `userland/base/vkdemo/shaders.h` の C 配列（`shaders/cuboid.{vert,frag}.spv` と byte 一致を確認）。glslc `-O0`、SPIR-V 1.0（`shaders/provenance.json`）。

- **Vertex**（769 word、id bound 126）: OpExtInst×4（Sin×2、Cos×2）、OpConstant×13（float 9、int 4）、OpVariable×13（PushConstant 1、Input 2、Output 2、**Function 8**）、OpLoad×29、OpStore×10、OpAccessChain×17、OpCompositeConstruct×3、**OpFNegate×1**、OpFAdd×6、OpFSub×2、OpFMul×13、OpMemberDecorate×5（Offset、BuiltIn Position／PointSize／ClipDistance／CullDistance）。
- **Fragment**（161 word）: OpExecutionMode OriginUpperLeft、OpTypeImage（2D sampled float）、OpTypeSampledImage、OpVariable×3、OpLoad×2、OpStore×1、**OpImageSampleImplicitLod×1**。
- 両方とも分岐なし（OpBranch／SelectionMerge／LoopMerge／Phi なし）。

kernel の parser（`vk/spirv.c`）が扱うもの: 宣言 15／71（Location・Binding・DescriptorSet・BuiltIn）／22／23／32／43／59、本体 65／61／62／129／131／133／142／148／80／81／87／12（Sin・Cos・InverseSqrt）。

**vkdemo に必要で未対応**:
| 不足 | 内容 |
|---|---|
| Function storage の変数 | local 8 個・store 10 個・load の大半が無効果（`ptr_kind` が PTR_NONE のままで store→load の受渡しが無い）。`-O0` の vertex shader 全体が成立しない |
| float OpConstant の operand | bit は保持（:295）するが IR へ出ない。`compile.c` に即値経路が無い |
| OpFNegate（127） | define だけ |
| component OpAccessChain | `ptr_offset` は計算するが LOAD_INPUT の immediate は常に 0（:443-444）、IR に swizzle／extract が無い |
| 3〜4 要素の OpCompositeConstruct | 2 要素しか渡らない（:472）、`compile.c:210` は COMPOSE を src[0] の MOV へ |
| FSUB | `compile.c:187-189` で **ADD として lower されている** |
| OpMemberDecorate BuiltIn | 未解析。「Location の無い Output」の代替規則（:311）でだけ動く |
| 未対応 opcode の扱い | 黙って skip（comment は「EINVAL」と記載）。**空成功の compiler 版** |

fragment shader の opcode は全部認識される。`compile.c` の header は register 規約を「実機 bring-up で詰める baseline」と書いており、E-103 で確定した値（PS DW3／DW7／PS_EXTRA、GRF start など prog_data 由来）とは未照合。
→ VK-2 の範囲: (a) 下ろせない命令を拒否、(b) `-O0` の local 変数（store→load forwarding か、shader を `-O` で再生成するかの選択。後者は shader 側の変更なので要判断）、(c) 即値・negate・swizzle・3〜4 要素 compose・FSUB、(d) E-103／E-105 で確定した parity の 3D state との突合せ。

## 3. 呼出しごとの分類

非 recording の command は全部 flag = 1（reply 要求）。generic create の reply = `[opcode][VkResult][present u64 = 1][wire id u64 = client が選んだ id]`（これ以外の形は DEVICE_LOST 扱い、`objects.c:458-497`）。generic destroy = `[opcode]` のみ。
分類 a = libvulkan 内で完結、b = command stream、c = `/dev/gpuN` ioctl。

| API | 類 | opcode／ioctl | kernel handler | reply |
|---|---|---|---|---|
| vkCreateInstance | b | 0、2×2、6、3、8、7×2（node ごと）。先に GPU_GET_INFO／GPU_GET_CAPSET | **無** | create、count＋id、property 構造体。apiVersion ≥ 1.1、HOST_VISIBLE+COHERENT と DEVICE_LOCAL の memory type を要求 |
| vkEnumeratePhysicalDevices、Get*Properties、vkGetDeviceQueue | a（cache） | — | — | — |
| vkGetPhysicalDeviceFormatProperties | b | 4 | **無** | present＋u32×3 |
| display／plane 照会 6 種、vkCreateDisplayModeKHR、surface support／caps／formats | c | GPU_DEVICE_QUERY、GPU_DISPLAY_QUERY／MODE／CONSTRAINTS | i915 の capability に GPU_CAP_DISPLAY 無し（`i915.c:42-43`） | ioctl 構造体 |
| vkCreateDisplayPlaneSurfaceKHR、vkDestroySurfaceKHR | a | — | — | — |
| vkCreateDevice | b | 11、155（vkGetDeviceQueue2、timeline index を運ぶ） | **無** | create、queue id |
| vkCreateSwapchainKHR | b+c | GPU_DISPLAY_CLAIM。共有経路（GPU_CAP_SHARE 要）: 54、31、21、29、BLOB_CREATE_PLACED、RESOURCE_EXPORT／IMPORT。copy 経路: 54、31、21、29＋readback 50、30、21、28、blob create／map、RESOURCE_CREATE | 54／21／29／50／28 有、**31／30 無** | 各呼出しに同じ |
| vkGetSwapchainImagesKHR、vkAcquireNextImageKHR | a（acquire semaphore は software signal） | GPU_DISPLAY_WAIT／EVENTS の可能性 | — | — |
| vkCreateImage／Destroy | b | 54／55 | 有 | create／opcode |
| vkCreateBuffer／Destroy | b | 50／51 | 有 | 同上 |
| vkGet{Image,Buffer}MemoryRequirements | b | 31／30 | **無** | present＋VkMemoryRequirements |
| vkAllocateMemory | b+c | 21。host-visible なら GPU_BLOB_CREATE（blob_id = memory wire id）→ GPU_RESOURCE_MAP | 21 有 | present＋id。map は非 0 の page 境界 offset と要求どおりの byte 数 |
| vkMapMemory／Unmap／Flush／Invalidate | a | — | — | — |
| vkBind{Image,Buffer}Memory | b | 29／28 | 有 | result |
| vkFreeMemory | b+c | 22、GPU_RESOURCE_DESTROY | 有 | opcode |
| vkCreateImageView／Destroy | b | 57／58 | **無** | create |
| vkCreateSampler／Destroy | b | 70／71 | **無** | create |
| descriptor set layout／pool／allocate／update と destroy | b | 72、74、77、79、73、75 | **無** | 77 = count＋id、79 = opcode |
| vkCreatePipelineLayout／Destroy | b | 68／69 | **無** | create |
| vkCreateRenderPass／Framebuffer と destroy | b | 82／83、80／81 | **無** | create |
| vkCreateShaderModule／Destroy | b | 59／60 | 有 | create |
| vkCreateGraphicsPipelines／vkDestroyPipeline | b | 65／67 | 有 | result、count、id |
| command pool 作成／破棄／reset、vkAllocateCommandBuffers | b | 85、86、87、88 | 有 | 88 = count＋id |
| vkBeginCommandBuffer | b（即送信） | 90 | 有 | result |
| vkCmd*（local に batch、flag = 0、header `[op][0][cmdbuf id u64]`、1 MiB 超で flush） | b | 126、115、133、93、105、103、132、106、135、116 | 93／105／106／135 有、**126／115／116／133／103／132 無** | — |
| vkEndCommandBuffer | b | recording＋91（flag 1）を 1 stream で | 91 有 | 91＋result |
| vkQueueSubmit | b+c | GPU_JOB_CAPACITY、GPU_JOB_RESERVE → 18 → GPU_JOB_COMMIT／CANCEL | 18 有 | result |
| fence 作成／破棄／reset | b | 35、36、37 | 有 | create／result |
| vkWaitForFences | b+c | 38 の polling＋GPU_COMMAND_WAIT（job sequence）。39 は送られない | 38 有 | SUCCESS／NOT_READY |
| vkCreateSemaphore／Destroy | b | 40／41 | **無** | create |
| vkQueuePresentKHR | b+c | copy 経路: 85、88、35（初回）→ 90 → [126、116、126、91] → 18、38 → GPU_RESOURCE_WRITE、GPU_DISPLAY_PRESENT。共有経路: 126、119、114／113＋GPU_DISPLAY_PRESENT_SYNC | 部分 | result |
| vkQueueWaitIdle／vkDeviceWaitIdle | b | 35、18（count 0）、38＋GPU_COMMAND_WAIT、36。19／20 は送られない | 有 | — |
| vkDestroySwapchainKHR | b+c | destroy 群＋GPU_DISPLAY_RELEASE、GPU_RESOURCE_DESTROY | 部分 | — |
| vkDestroyDevice／vkDestroyInstance | b | 12／1 | **無** | opcode |

## 4. Stream の framing と、拒否の実装

- header = `u32 opcode, u32 flags`（little endian）。scalar は u32／u64、pointer は u64 の 0/1 存在 marker、配列は u64 count＋要素、byte 列は 4 の倍数へ 0 詰め、文字列は u64 長（NUL 込み）＋詰め、handle は client が選ぶ u64 wire id（`wire.c:284-669`）。
- **command ごとの長さ欄は無い** → 理解できない command は終端を決められず、skip できない。decoder は stream を中止するしかない。
- 1 transaction（`context.c:708-835`）= ① `[178][0][u64 1][u32 reply_resource][u64 offset 0][u64 reply_capacity]`（36 B）② command 本体 ③ `[179][0][u64 reply_capacity − 20]`、`[137][1][u64 1]`。reply は reply blob の offset 0 から順に `[opcode][VkResult（ある API だけ）][出力]`、末尾 20 byte の trailer `[137][0][u64 1][version ≥ 1.1.0]` は version word を最後に書く。
- 投入: GPU_CAP_NOTIFICATION があれば GPU_COMMAND_SUBMIT＋GPU_COMMAND_CONTEXT_FENCE → GPU_COMMAND_WAIT、無ければ GPU_COMMAND＋polling（上限 10 s）。
- **opcode 180** は command が GPU_COMMAND_MAX − 128 = 65408 byte を超えたときだけ: `[180][0][u32 1][u64 1][u32 stream_resource][u64 offset 0][u64 bytes][u64 0][u32 0][u64 0][u32 0]`。kernel に handler 無し。vkdemo の最大 command は vertex shader module の約 3.1 KB なので 180 は出ない見込み（**size からの導出で、実測ではない**）。実装時は stream resource の所有者・範囲（offset＋bytes が resource 内）・実行中の寿命（解放／再 map されない）を検査し、入れ子の 180 は拒否。
- **E-107 の修正**: `i915_vk_cmd_builtin` の既定枝は reader を poison して `ENOTSUP`（以後の decode／実行なし、reply 長は公開されない）。routed module（`cmdbuf.c:640`、`res.c:894`、`sync.c:347`、`wsi.c:46`）は従来から EINVAL。host 試験 `i915-vk-cmd-test.c` に「0／1／17／148／180 の後ろに正しい 137 probe を置いても probe が走らない」を追加、9 fixture PASS。

## 5. 未確認（次に確かめること）

- 既存 31 handler が libvulkan の書く byte 配置をそのまま消費するか（host fixture は kernel 側の想定配置で書かれている。libvulkan の encoder 出力を fixture にする突合せが必要）。
- GPU core の JOB ioctl が i915 と端から端まで動くか。
- 長さ・count・offset・handle 所有者の検査の網羅（handler ごと）。
