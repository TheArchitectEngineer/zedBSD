# WS031 結果: i915 ネイティブ Vulkan 実行器（build-complete、2026-09-15）

libvulkan が drv_gpu UAPI へ送る Vulkan コマンドを、ホスト（QEMU/Venus/Mesa ANV）非依存で i915 カーネルドライバ内 native 実行器が GEN へ変換する経路を実装した。**全 12 phase を build-passing 基準で完了**（p011 の実機描画のみビッグバンテスト待ち）。Mesa（MIT）と Intel PRM の値・encoding を出典付き `.inc` へ転記し、論理は zedBSD 規約で新規実装。HAL 不変、UAPI 不変（既存 drv_gpu op 使用）。

## Phase 結果
| phase | module | 結果 |
| --- | --- | --- |
| p001 | 設計・契約枠 | cleared。外部設計＋12 契約ヘッダ |
| p002 | top+cmd | cleared。cmd decoder/object table/routing、drv_gpu 統合（capset/blob/map/command） |
| p003 | res | cleared。memory/buffer/image/sampler/descriptor、Gen12 surface state 転記 |
| p004 | spirv | cleared。SPIR-V→baseline IR、vkdemo vert/frag 検証 |
| p005 | eu | cleared。Gen12 EU エンコーダ（mesa-23.1.0 表形式転記）、field 照合 |
| p006 | compile | cleared。SPIR-V IR→GEN baseline codegen、vkdemo 通過 |
| p007 | pipe | cleared。3DSTATE_VS/PS/PIPELINE_SELECT emission |
| p008 | cmdbuf | cleared。record→batch、3DPRIMITIVE draw |
| p009 | sync | cleared。fence を engine seqno へ接続 |
| p010 | wsi | cleared。swapchain/present/display（modeset はビッグバン） |
| p011 | 統合 | build-passing 達成。実機描画（増分A/B/C）はビッグバンテスト |
| p012 | review | cleared。静的解析 0、回帰なし |

## 検証（agent-1、build-passing 基準）
- host fixture 8 種（cmd/spirv/res/sync/eu/compile/pipe/cmdbuf）通常＋ASan/UBSan PASS。
- i915 kernel build PASS（vmunix check、warning 0、FPU 不使用 `-mgeneral-regs-only` クリア）。
- 静的解析 gcc/clang 0 件。WS029 host fixture 全 PASS（回帰なし）。vk は I915 config で gate。git diff --check clean。
- コンパイラチェーン end-to-end: vkdemo の SPIR-V を parse→IR→GEN まで host 検証。

## 実装（すべて local、driver は Zlib、`.inc` は Mesa MIT 出典付き）
`src/drivers/gpu/i915/vk/`: vk-internal.h、vk.c、cmd.c、res.c、spirv.c、eu.c、compile.c、pipe.c、cmdbuf.c、sync.c、wsi.c、display.c、各 header、`linux/{surface-state,eu-encoding,3dstate}-gen12.inc`。i915.c/internal.h に drv_gpu 統合（capset/blob/map/command routing、session/attach）。vmunix.mk 配線。

## 制限・ビッグバンテスト（実機で確定）
実機ネイティブでの増分A（三角形）→B（texture+depth）→C（vkdemo）描画。EU の SWSB 依存・3-source/send operand・message descriptor、compile の GRF 規約、pipe の fixed-function fields、cmdbuf の RCS0 submission 実配線、wsi の display register programming、res_dispatch 等の Venus 精密 decode。値・encoding は Mesa 転記で配置は正、semantics は実機反復で確定する（テープアウト後ビッグバン方式）。

## Big-bang hardware bring-up: completion-interrupt fix (verified on real IGD)

Test: Dell Latitude 5330, Alder Lake-P Iris Xe (8086:46a8 rev 0c), via QEMU
VFIO PCI passthrough on 10.0.10.25 (boot-time vfio-pci binding). Selftest
built in with CONFIG_DRIVER_PCI_I915_SELFTEST=y.

### Symptom
Driver attached and the BCS0 selftest submission executed (marker
0xdeadbeef stored, breadcrumb seqno stored: seqno_hw=1), but the completion
interrupt was never taken (total=0), so the request never retired and attach
stopped at the selftest.

### Diagnosis (in order)
1. seqno_hw=1 proved the engine ran the whole ring including MI_USER_INTERRUPT
   (not a ring/parse problem).
2. GT register read-back at the stall: GEN11_GFX_MSTR_IRQ=0x80000000|GT_DW(0),
   GEN11_GT_INTR_DW(0) bit set, master enable set -> the interrupt propagated
   all the way to the top-level master register and sat *pending*. The GT
   interrupt configuration was fully correct.
3. Both MSI and INTx failed identically -> the loss was upstream of PCI
   delivery, i.e. the CPU never took the pending interrupt.
4. kern_irq_disable() read-back: interrupts were DISABLED at selftest time.
   Device bring-up runs with processor interrupts masked, so the pending
   completion interrupt could never be delivered while the wait spun.

### Root cause
Not VFIO, not MSI, not GT config. Attach/bring-up runs with CPU interrupts
disabled; the selftest waited for an interrupt-driven retirement that could
never fire in that context. This is real hardware behaviour, independent of
passthrough; the earlier bare-metal "pass" never compiled the selftest in and
the non-selftest attach path published the node without waiting on any
interrupt, so the defect was latent.

### Fix (src/drivers/gpu/i915/selftest.c only)
Around the completion wait: save the caller state with kern_irq_disable(),
kern_irq_enable() for the duration of the wait, restore afterwards. Success is
now keyed on engine->completed_seqno == request->seqno (interrupt-driven
retirement) plus the marker store. No HAL/UAPI change.

Reverted dead ends: engine.c RING_IMR write (ineffective; ~GT_RENDER_USER_INTERRUPT
would have wrongly masked context-switch/error sources) and all temporary
diagnostics in i915.c/irq.c.

### Result (real hardware, passthrough)
    i915: selftest passed (bcs0 store and user interrupt)
    i915: registered native GPU node (storage, native streams, jobs)

GPU execution + completion interrupt proven on a cold Alder Lake-P IGD.

## p011 増分A0 (2026-09-15): reply framing + res memory decode

- cmd_dispatch に reply framing 規約を実装: reply-request flag 時に echoed opcode
  を書き、各 _dispatch が [VkResult][payload] を append（void コマンドは echo のみ）。
- res_dispatch に vkAllocateMemory(21)/vkFreeMemory(22) の wire decode を実装。
  memory_alloc + object table 登録、reply=[opcode][result][present][identifier]。
- 新 host fixture i915-vk-resdispatch-test.c: libvulkan と同一エンコードの stream を
  cmd_dispatch に流し、object table と reply framing を検証。plain + ASan/UBSan PASS。
- 既存 host fixtures 全 PASS（cmd の 137 framing 期待値を 12B に更新、res-test は
  cmd.c 依存を追加してリンク解決）。i915+vk kernel build warning 0。
- 変更: vk/cmd.c, vk/res.c, tests/i915-vk-{cmd,res,resdispatch}-test.c。HAL/UAPI 変更なし。
- 次: 残 res コマンド（vkCreateBuffer/BindBufferMemory/vkCreateImage/BindImageMemory/
  sampler/descriptor）→ 増分B、その後 pipe/cmdbuf/sync/wsi decode と reply transport(178/180)。

## p011 増分A 続き (2026-09-15): res buffer/image decode

- res_dispatch を拡張: vkCreateBuffer(50)/vkDestroyBuffer(51)/vkBindBufferMemory(28)、
  vkCreateImage(54)/vkDestroyImage(55)/vkBindImageMemory(29) の wire decode を実装。
  VkBufferCreateInfo / VkImageCreateInfo / VkExtent3D / *_external(pNext) を libvulkan
  codec.c と同一順で decode。destroy/bind は kind＋関数ラッパで共有。
- create の reply = [opcode][result][present][identifier]（24B）、bind = [opcode][result]
  （8B）、destroy/free = [opcode]（4B）。libvulkan の reply_capacity と一致。
- fixture i915-vk-resdispatch-test.c を memory/buffer/image の create/bind/destroy 全経路に
  拡張（単一 attach に統合し LeakSanitizer clean）。全 host fixtures PASS（plain+ASan/UBSan）、
  i915+vk kernel build warning 0。
- 次: sync(fence 35-39)/pipe(shader・pipeline)/cmdbuf(pool・record・draw・renderpass)、
  queue submit(18)、reply transport(178/180)。三角形へ。

## p011 増分A 続き (2026-09-15): sync fence decode

- sync_dispatch に vkCreateFence(35)/vkDestroyFence(36)/vkResetFences(37)/
  vkGetFenceStatus(38) の wire decode を実装（sync.c に cmd.h 追加）。
- vkWaitForFences(39) は libvulkan 側で vkGetFenceStatus のポーリングとして実装される
  ため executor には来ない（sync.c 実測）。GetFenceStatus の reply は status VkResult
  （signaled→VK_SUCCESS, else→VK_NOT_READY=1）。
- fixture: i915-vk-sync-test.c に fence decode 経路（create/status/destroy）を単一 attach
  内で追加。全 host fixtures PASS（plain+ASan/UBSan）、kernel build warning 0。
- 残: cmdbuf(pool/buffer/record/draw/renderpass)、pipe(shader/pipeline)、queue submit(18)、
  reply transport(178/180)。

## p011 増分B (2026-09-15): cmdbuf command-buffer lifecycle + 記録モデルの発見

- cmdbuf_dispatch に vkCreateCommandPool(85)/vkDestroyCommandPool(86)/
  vkResetCommandPool(87)/vkAllocateCommandBuffers(88)/vkFreeCommandBuffers(89)/
  vkBeginCommandBuffer(90) を実装。command buffer は 64KB の GEM batch buffer を
  session PPGTT に bind して確保（memory_alloc と同じ経路）。cmdbuf.c に cmd.h/
  ../internal.h/kmem/pmem を追加。
- **重要な発見**: vkCmd*（93-136）と vkEndCommandBuffer(91) は top-level stream には
  来ない。libvulkan は command->recording にローカルに積み、submit 時に
  **vkExecuteCommandStreamsMESA(180)** で別 resource として実行させる（commands.c 実測）。
  よって描画記録は 178/180 の command-stream transport が前提。これは p011 の増分順を
  規定する（下記）。
- AllocateCommandBuffers の reply は [op][result][count][ids...]（実測 reply_capacity
  count*8+16 と一致）。
- fixture i915-vk-cmdbuf-test.c を heavyweight 化（full i915 stack + cmd.c + pipe.c）。
  従来の記録テスト（ローカル batch）＋新 lifecycle テスト（wire 経由）を単一 attach で。
  全 host fixtures PASS（plain+ASan/UBSan）、kernel build warning 0。
- 変更: vk/cmdbuf.c、tests/i915-vk-cmdbuf-test.c。HAL/UAPI 変更なし。

### 増分順（記録モデル発見を反映）
- 次(B2): reply/command-stream transport（vkSetReplyCommandStreamMESA 178 /
  vkExecuteCommandStreamsMESA 180 を executor で解釈、i915.c command 経路の
  NULL reply を実 reply resource へ結線）。これで実 libvulkan の往復と vkCmd* 記録が動く。
- その後: pipe(shader/pipeline decode + compiler 結線)、queue submit(18) を RCS0 request
  経路へ結線（実行部は selftest 済み）、wsi flip → 三角形。

## p011 増分B2 (2026-09-15): reply/command-stream transport（178/179）+ 設計判断

### 設計判断（ユーザー提起「なぜ MESA 拡張？不要では？」への回答）
drv_gpu UAPI の command/submit op（include/drivers/gpu.h L131/L134）は **reply out 引数を
持たない fire-and-forget**（L111-112）。よって ioctl からインライン reply を返せず、reply は
**別リソース**に書く必要がある → 178/179（reply-stream）は Venus の飾りではなく本 UAPI では
**必須**。一方 180（ExecuteCommandStreamsMESA＝記録の間接再生）は不要にできる：EndCommandBuffer
を top-level にして記録ストリームをそこで送り、executor がコマンドバッファ専用 batch を一度組む
方が native で綺麗（submit は組み済み batch を参照するだけ・別 API のまま）。→ 178/179 は実装、
180 は後段の libvulkan リファクタで回避。

### 実装（executor 側 = cmd.c、resolution = i915.c）
- cmd.c: vkSetReplyCommandStreamMESA(178)＝reply cursor を 0 に、vkSeekReplyCommandStreamMESA(179)
  ＝cursor を seek（完了トレーラ位置へ）、vkEnumerateInstanceVersion(137)＝トレーラ完了プローブ
  `[opcode][result=0][present=1][0][version≥1.1.0]`（20B, version を最後に publish）。cmd.c は i915
  内部型に依存せず軽量を維持。
- i915.c: `i915_vk_command_reply()` が先頭 178 セレクタを解析し resource_id(=object->slot) を
  session->objects から解決 → blob の kernel alias を reply バッファとして drv_i915_vk_command へ渡す
  （同期 command 経路・非同期 submit 経路の両方）。
- vk.c: reply 書き込み後に kern_io_write_barrier() で publish（libvulkan の acquire ポーリングに対応）。
- fixture: resdispatch に end-to-end 追加（実 blob を reply resource にし、178 解決＋AllocateMemory
  reply が offset 0、完了トレーラが末尾 -20 に landing することを検証）。cmd-test の 137 を 20B
  トレーラ框に更新。全 host fixtures PASS（plain+ASan/UBSan）、kernel build warning 0。
- 変更: vk/cmd.c、vk/vk.c、i915.c、tests/{cmd,resdispatch}-test.c。**HAL 変更なし。UAPI 変更なし**
  （既存 178/179/137 の wire を解釈しただけ）。

### 次
libvulkan リファクタ（EndCommandBuffer を top-level 化して 180 を回避、記録ストリームを executor が
batch へ decode）→ pipe(shader/pipeline decode + compiler 結線)→ queue submit(18) を RCS0 request
経路へ→ wsi flip。三角形へ。

## p011 増分B3 (2026-09-15): cmdbuf 記録 decode（libvulkan リファクタ不要と判明）

### 発見：EndCommandBuffer は既に top-level で記録を送っている
libvulkan は vkCmd* を command->recording にバッファし、**vkEndCommandBuffer で
recording 全体を vulkan_command_execute で送る**（commands.c 実測）。executor は
それを [178][vkCmd*...][91 End][179][137] として受け、各 vkCmd* を top-level コマンド
（先頭に commandBuffer wire_id）として decode すればよい。180 は大 recording 溢れ時のみ。
→ **libvulkan リファクタ不要**。ユーザー直感どおりの形が既存実装。

### 実装（cmdbuf.c）
- vkEndCommandBuffer(91)＝recording stream 唯一の reply 持ち：batch 終端＋[91][result]。
- vkCmdBindPipeline(93)/vkCmdDraw(106)/vkCmdBindVertexBuffers(105)/vkCmdEndRenderPass(135)
  ＝reply なし記録。各 handler が commandBuffer wire_id を lookup し、モジュール記録関数
  （cmd_bind_pipeline/cmd_draw 等）で batch へ GEN 追加。
- begin_command(90) を VkCommandBufferBeginInfo 全消費に修正（[178][90][179][137] の整列を保証、
  primary のみ）。
- fixture: cmdbuf-test に wire 記録経路を追加（pipeline 生成→Begin/BindPipeline/BindVertexBuffers/
  Draw/End を wire で流し、batch に 3DSTATE_VS/PS・3DPRIMITIVE・MI_BATCH_BUFFER_END が landing）。
  全 host fixtures PASS（plain+ASan/UBSan）、kernel build warning 0。
- 変更: vk/cmdbuf.c、tests/i915-vk-cmdbuf-test.c。HAL/UAPI 変更なし。

### 残（三角形へ）
BeginRenderPass(133) decode＋render target 結線（renderpass/framebuffer=pipe 80-84）、
pipe（shader module 59・pipeline 65＋SPIR-V→GEN コンパイラ結線）、queue submit(18)→RCS0
（実行部 selftest 済み）→ wsi flip。

## p011 増分C1 (2026-09-15): pipe shader module decode

- pipe_dispatch に vkCreateShaderModule(59)/vkDestroyShaderModule(60) を実装。
  VkShaderModuleCreateInfo [sType][pNext][flags][codeSize][word count][words...] を
  decode し、SPIR-V を shader module オブジェクト（code+words）として保持。pipeline 生成時に
  stage 付きでコンパイルする素材。reply=create 24B / destroy 4B。
- コンパイラ本体（spirv_parse→compile→shader_binary）は実装・host 済み。ここは wire→保持の結線。
- fixture: pipe-test に cmd.c + module stub を追加し、CreateShaderModule/Destroy の wire 経路
  （SPIR-V 3 words の往復と object table 保持）を検証。全 host fixtures PASS（plain+ASan/UBSan）、
  kernel build warning 0。pipe.c は i915 内部型非依存（kmem のみ）で軽量維持。
- 変更: vk/pipe.c、tests/i915-vk-pipe-test.c。HAL/UAPI 変更なし。

### 残（三角形へ・最大の decode）
vkCreateGraphicsPipelines(65)＝VkGraphicsPipelineCreateInfo（stages→shader module 参照して
compile、vertex input/input assembly/viewport/raster/multisample/depth-stencil/color-blend/
layout/renderpass/subpass）の巨大 decode。essential を使い残りは byte 整列のため各 sub-struct を
decode。加えて render pass(82)/framebuffer(80)、queue submit(18)→RCS0、wsi flip。

## p011 増分C2 (2026-09-15): vkCreateGraphicsPipelines decode + compiler 結線（最大の decode）

- pipe_dispatch に vkCreateGraphicsPipelines(65)/vkDestroyPipeline(67) を実装。
  VkGraphicsPipelineCreateInfo の全 sub-struct（stages/vertex-input/input-assembly/
  tessellation/viewport/rasterization/multisample/depth-stencil/color-blend/dynamic/
  layout/renderpass/subpass/base）を libvulkan codec.c と厳密一致で decode。
  **発見: decode は uniform**（各 optional state は presence u64 → あれば実体）なので
  encoder の rasterize/color/depth 条件を再現不要。各 state は正確な byte layout で消費
  （array 要素幅・interleaved scalar・string の 4byte padding を厳密に）。
- 抽出: 各 stage の module handle + stage bit、input assembly の topology。
- **コンパイラ結線**: 各 stage の shader module SPIR-V を spirv_parse→i915_vk_compile で
  GEN へ、GEN code を session PPGTT に bind した GEM buffer に配置し、その GPU va を
  vs_kernel/fs_kernel に。i915_vk_pipeline_create でパイプライン構築。pipeline が code GEM を
  所有し destroy で解放（pipe struct に session/vs_code/fs_code 追加）。pipe.c は heavyweight化
  （../internal.h + GEM）。
- fixture: pipe-test を heavyweight 化し、**実 vkdemo シェーダ（cuboid.vert/frag.spv）**で
  ShaderModule 2つ → GraphicsPipeline を wire で作成し、reply・object table・vs_kernel/
  fs_kernel/code GEM・topology を検証。全 host fixtures PASS（plain+ASan/UBSan）、
  kernel build warning 0。cmdbuf-test に compiler include を追加。
- 変更: vk/pipe.c、tests/i915-vk-{pipe,cmdbuf}-test.c。HAL/UAPI 変更なし。

### 残（三角形へ）
render pass(82)/framebuffer(80) decode（render target 結線）、queue submit(18)→RCS0
（実行部 selftest 済み）、wsi flip。パイプライン・記録・リソース・同期・transport は完了。

## p011 増分D (2026-09-15): queue submit → RCS0（実行結線・すんなり動作）

- i915_vk_queue_submit を WS029 request 経路に結線（i915_submit_stream と同型）:
  RCS0 engine、各 command buffer につき drv_i915_request_alloc(engine, session->gpu, ...)、
  request->context = session->gpu->contexts[RCS0]、request->batch = cmdbuf batch GEM、
  request->batch_va = batch->va、queue → kick。**seqno は kick(emit) 時に採番**されるため
  最後の request->seqno を kick 後に読み fence を arm（当初 alloc 直後に読み 0→即 signaled の
  バグを修正）。
- vkQueueSubmit(18) decode を cmdbuf に実装（VkSubmitInfo: waits/cmdBuffers/signals + fence を
  厳密 decode）。cmd.c の routing に opcode 18 → COMMAND_BUFFER を追加。cmdbuf.c に sync.h/
  device-io.h を追加。
- fixture: cmdbuf-test に sync.c を取り込み、記録済み cb0 を wire で QueueSubmit → reply success、
  fence が RCS0 breadcrumb に arm され、completed_seqno を進めると retire することを検証。
  全 host fixtures PASS（plain+ASan/UBSan）、kernel build warning 0。
- 変更: vk/cmdbuf.c、vk/cmd.c、tests/i915-vk-cmdbuf-test.c。HAL/UAPI 変更なし。

### 実行経路が完成
res/sync/pipe(compiler)/cmdbuf(record+submit)/transport が揃い、**実 Vulkan コマンドから
GEN batch を組み RCS0 で実行、fence で完了検出**まで executor 上で成立。
残（可視の三角形）: render pass(82)/framebuffer(80) + vkCmdBeginRenderPass(133) decode で
render target を結線、wsi flip で表示。

## p011 分かれ道の評価 (2026-09-15): render target emit がハードウェア反復の境界

host 検証可能な executor は完成（res/sync/pipe+compiler/cmdbuf record+submit/transport）。
残るは render pass(82)/framebuffer(80)/vkCmdBeginRenderPass(133) と、その核である
**render target の GEN emit**（cmd_begin_render_pass が color attachment の
RENDER_SURFACE_STATE・binding table・3DSTATE_PS_BLEND/WM・clear を batch へ出す処理。現状 no-op）。

**評価**: 
- decode（render pass/framebuffer/BeginRenderPass のオブジェクト化）は tractable だが large。
- しかし**有効な render target を bind せずに draw を実行すると GPU が faultする**ため、
  emit なしでは「無害な実行」もできない。→ render target emit が gating。
- render target emit は Gen12 の正確な state 生成で、**実機のフィードバックなしに blind で
  書くと当たらない可能性が高い**（ユーザーの言う「かなり苦労して修正」side）。

**方針（ユーザー指示「だめなら uncleared」に従う）**: render target emit ＋ scanout(wsi flip)
を **p011 増分E = 実機反復フェーズ**として切り出す。まず現 executor をビッグバンで実機に載せ、
selftest 通過後に「単色 clear のみの最小 render pass」から emit を実機フィードバックで詰める。
ここまでの decode/submit は全て host 済みなので、実機では render target state だけを反復対象にできる。

## p011 増分E-1 (2026-09-15): 単色clear 実機成功（ファウンデーション疎通）

BCS0 の XY_FAST_COLOR_BLT で GGTT... PPGTT-mapped buffer を単色塗りし、CPU 読み戻しで
全ピクセル一致を実機検証。selftest 同型の割込み待ちで完了検出。

    i915: clear selftest color=0xffff0000 px[0]=0xffff0000 px[mid]=0xffff0000 px[last]=0xffff0000 seqno=2/2
    i915: clear selftest passed (solid color fill on bcs0)

### 実機フィードバックで判明した Gen12 の3点（ユーザー提案のバイト識別パターン診断で特定）
1. Gen12 ADL-P BCS0 は**レガシー XY_COLOR_BLT を実行しても書かない**（seqno は完了）。
   → **XY_FAST_COLOR_BLT（opcode 0x44, gen120.xml, 11 dwords, 4-dword fill color）**を使う。
2. **BCS0 kernel context は engine->kernel_vm(PPGTT)** を使う。GGTT offset は解決しない。
   → buffer を drv_i915_gem_bind_vm(&engine->kernel_vm,obj) し、object->va(PPGTT) で address。
   （selftest の MI_STORE は MI_USE_GGTT 明示なので GGTT で動いていた）
3. **Destination Pitch フィールドは 0-based（実バイト − 1）**。W*4 を入れると 1 行 +1 byte ずれ、
   行が進むごとにバイトシフト（byte-distinct fill で確定）。W*4-1 で全行整列。

### 意義
GPU が実機で単色サーフェスを生成できることを検証。これは render path のファウンデーション。
2D 塗り（BCS）は確立。次段: 3D(RCS0) render target への clear/描画、その後 scanout(表示)。
変更: selftest.c（drv_i915_clear_selftest 追加）、i915.c（selftest 段で呼出）。HAL/UAPI 変更なし。

## p011 増分E-2 (2026-09-15): RCS0 実行 実機確認

    i915: rcs selftest marker=0xcafef00d seqno=1/1
    i915: rcs selftest passed (render engine executes)

RCS0 kernel context(PPGTT) で MI_STORE marker を書き、PIPE_CONTROL breadcrumb ＋ 割込みで
完了検出。**queue_submit が使う RCS0 request 経路が実機で健全**。BCS0(clear)/RCS0(exec) 両輪確立。
変更: selftest.c(drv_i915_rcs_selftest)、i915.c(呼出)。

### 次: RCS0 render target clear（3D state 反復）
必要な Gen12 3D state: STATE_BASE_ADDRESS(実 heap 番地)、RENDER_SURFACE_STATE(color RT)、
3DSTATE_BINDING_TABLE_POINTERS_PS + binding table、3DSTATE_{VS,PS,SF,CLIP,WM,VIEWPORT,...}、
定数色を出す PS、3DPRIMITIVE(全画面 rect)。実機フィードバックで段階的に。
その後 scanout(表示)、三角形、shader OP 拡張、texture。

## p011 増分E-3 (2026-09-15): 3D パイプライン土台（PIPELINE_SELECT + STATE_BASE_ADDRESS）実機確認

RCS0 で PIPELINE_SELECT(3D) + STATE_BASE_ADDRESS(22 dw) + MI_STORE marker を実行:

    i915: rt sba selftest marker=0x5ba5eba5 seqno=2/2
    i915: rt sba selftest passed (STATE_BASE_ADDRESS parses)

### 判明
- STATE_BASE_ADDRESS の 22-dword 長は正しい（parser が marker に到達＝完走）。
- PIPELINE_SELECT(3D) は request 経路で動く。
- **落とし穴**: 各 base を「null address + modify enable + max size」にすると GPU がアドレス0に
  ヒープを張ろうとして fault→ハング（seqno 未完）。→ 未使用 base は modify=0、使う base は実
  アドレス(PPGTT va)を modify=1 で指す。

### 次段（3D render target draw の残り、実機反復）
実 heap（surface state / dynamic state / instruction）を STATE_BASE_ADDRESS に結線 →
RENDER_SURFACE_STATE(color RT) + binding table + 3DSTATE_BINDING_TABLE_POINTERS_PS →
定数色 PS カーネル(compile or 手書き GEN) + 3DSTATE_{VS,PS,PS_EXTRA,SBE,WM,PS_BLEND,SF,CLIP,
RASTER,MULTISAMPLE,VIEWPORT} + DRAWING_RECTANGLE → 頂点(RECTLIST) → 3DPRIMITIVE →
RT 読み戻しで単色確認。gen110/gen120.xml から順次転記。

## p011 増分E-4 (2026-09-15): render target draw 用シェーダ確立（最難関ブロッカー解消）

ツールチェーン（glslc）が無い環境で、i915 SPIR-V パーサが受理する**最小 SPIR-V を手組み**し、
i915 コンパイラで実 GEN カーネルを生成：
- passthrough VS（input loc0 vec4 → gl_Position builtin）：48B GEN, grf 17
- 定数色 PS（out loc0 = vec4(1,0,0,1)）：48B GEN, grf 18

パーサの要点: Output 変数で Location 無し = builtin(gl_Position)。OpCompositeConstruct/OpStore/
OpLoad/OpConstant で最小構成。生成器と .spv は plan/ws031/shaders/ に保存。

これで render target draw のシェーダ（VS/PS）が揃った。draw の残ピース: 実 heap を
STATE_BASE_ADDRESS に結線、RENDER_SURFACE_STATE(RT)+binding table、3DSTATE 群(VS/PS/PS_EXTRA/
SBE/WM/PS_BLEND/SF/CLIP/RASTER/MULTISAMPLE/VIEWPORT/DRAWING_RECTANGLE/BINDING_TABLE_POINTERS_PS)、
頂点(RECTLIST)、3DPRIMITIVE → RT 読み戻し。シェーダは runtime で spirv_parse+compile して命令ヒープへ。

## p011 増分E-5 (2026-09-15): STATE_BASE_ADDRESS ハングの根本原因 — 3D state はリングではなく PPGTT batch から

### 症状
実 heap を STATE_BASE_ADDRESS に結線すると、base の値次第で以降のメモリ書き込み（PPGTT の
MI_STORE、GGTT の MI_STORE、breadcrumb の PIPE_CONTROL post-sync）が全て消え、context complete
も来ない。CS は head==tail/MODE_IDLE まで走り切り、RING_FAULT/EIR/ESR は 0、ホスト側 DMAR fault も無し。

### 切り分け（全て実機、1 ブート 1 失敗）
| 実験 | 結果 |
|---|---|
| base=0x100400000 / 0x100600000（rt/surface obj） | 通る |
| base=0x100800000 / 0x100a00000（dynamic/instruction obj） | 落ちる |
| 同じ VA に plain MI_STORE | 通る（PPGTT マッピングは正常、CPU 側 PTE も正常） |
| object と slot の入れ替え、bind 順逆転（VA と paddr の分離） | **VA 値だけが効く**（paddr・slot・回数は無関係） |
| base を obj 無しで掃引: 4G+7M ✓ / 4G+16M ✓ / 8G ✓ / **4G+8M ✗ / 4G+10M ✗** | 閾値ではなく特定範囲 |
| clflush / wbinvd | 効果なし（キャッシュ・コヒーレンシではない） |
| GGTT 全エントリ読み戻し | 不一致 0（GTT ウィンドウのマッピングは正常） |
| CSB レジスタミラー | 失敗 request は promote のみで complete 無し |

### 根本原因
selftest が SBA を `request->extra[]`（=**リング直置き**）で流していた。リング／secure batch から
実行される 3D state 命令のステートフェッチは **GGTT アドレス空間**で行われる（Linux i915 の
golden renderstate が GGTT に pin した batch を SECURE dispatch し、SBA base を GGTT オフセットに
reloc しているのがその実証）。MI_STORE は自前の GGTT/PPGTT ビットを持つため PPGTT に書けていた
のが「CS の書き込みは通るのに state fetch は死ぬ」の正体。PPGTT VA を GGTT として解釈した結果、
4GB 超の未定義アドレスで 3D パイプライン（state ユニット）が固まり、後続の pipelined 書き込みと
breadcrumb が滞留した（CS パーサ自体は tail まで進むので "idle" に見える）。
なぜ 0x800000〜0xa00000 だけ固まり 0x700000/0x1000000 は無事かは未解明（4GB 超の GGTT
アドレスの扱いはハード定義外）。**リングに 3D state を置かない**ことが正解であり、実 executor は
最初から PPGTT batch（cmdbuf の GEM batch）なので設計変更は不要。

### 修正
- `drv_i915_draw_selftest`: 3D 命令を kernel_vm PPGTT の batch（MI_BATCH_BUFFER_START non-secure）
  から実行。surface/dynamic/instruction の全 base 同時設定まで通過:

      i915: sba none       markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=3 completed=3 seqno=3
      i915: sba surf<-surf markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=4 completed=4 seqno=4
      i915: sba dyn<-dyn   markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=5 completed=5 seqno=5
      i915: sba all        markerA=0xa5a50001 markerB=0xd7a3f00d hwsp=6 completed=6 seqno=6
      i915: draw step1 passed (real-heap SBA parses)

- PIPELINE_SELECT: Gen12 は mask bits (15:8) を立てないと選択が無視される。selftest と executor
  （pipe.c）の両方が mask 無し（no-op）だったので `GEN12_PIPELINE_SELECT_DWORD()`（mask 0x13 +
  media sampler DOP gate、Mesa と同形）に統一。
- 副次確認: 非特権 batch 内の `MI_STORE | MI_USE_GGTT` は着地しない（GGTT 書き込みはリング専用）。

### 教訓
- 「CS は走り切ったのに書き込みが消える」= パイプライン側のストール。CS レジスタ（IPEHR/ACTHD/
  MODE_IDLE）と CSB（promote のみ）を読めば区別できる。
- 3D state のデバッグはリングでやらない。selftest も executor と同じ batch 経路を使う。

## p011 増分E-6 (2026-09-15): Gen12 3D パイプライン一式が実機でパース・完走（draw step 2a）

246 dword・約35パケットのフル 3D パイプラインを PPGTT batch から実行し、完走を確認:

    i915: draw batch 246 dwords markerA=0xa5a50001 markerB=0xd7a3f00d completed=3 seqno=3
    i915: draw stat ia_vertices = 3
    i915: draw stat ia_primitives = 1
    i915: draw stat vs_invocations = 3
    i915: draw step2 passed (3D pipeline state parses and assembles the rectangle)

### 参照の確立（推測を排除）
- `dump_gen.py`（scratchpad）で gen120.xml を `<import>` 連鎖（gen110→gen90→…）ごと解決し、
  **全パケット長・フィールドビット位置・enum 値を機械的に抽出**。手打ちの取り違えを排除した。
  gen120.xml は差分のみを持ち、STATE_BASE_ADDRESS 等の大半は gen110.xml から継承される。
- 最小 3D パイプラインの既知良好構成として Mesa の **BLORP**（`blorp_genX_exec_brw.h`）と
  **anv simple shader**（`genX_simple_shader.c`）を参照。両者とも **VS を無効化**し、VF が VUE を
  URB に直接書いてクリッパ/SF が読む構成。VS の URB 出力レイアウトという失敗要因が消えるので、
  これを採用した。

### 実装（すべて genxml 由来の定数）
- `vk/linux/3dstate-gen12.inc`: 3D パイプライン全パケットの opcode/長さ、isl フォーマット番号、
  VFCOMP/topology/surface type、統計レジスタ番号、L3ALLOC を転記。
- `selftest.c`: RENDER_SURFACE_STATE(32x32 B8G8R8A8_UNORM linear) + binding table、BLEND_STATE /
  COLOR_CALC_STATE / CC_VIEWPORT、RECTLIST 頂点(3頂点、スクリーン空間)、VERTEX_BUFFER/ELEMENT
  (VUE header は VB1 から、position は VB0 から、W=STORE_1_FP)、URB/push-constant 割り当て、
  全ステージ disable、CLIP/SF/RASTER/SBE/WM/PS/PS_EXTRA/PS_BLEND、null depth、DRAWING_RECTANGLE、
  3DPRIMITIVE。

### 判明した落とし穴（いずれも実機で発見）
1. **L3 未分割では URB が 0**。fresh context は L3 を分割していないので、VF は VUE を書く先が無い。
   TGL の検証済み値 URB=32ways / ALL=88ways を **L3ALLOC(0xB134)** に書く。
   レジスタ書き込みは特権命令なので **リング（request->extra[]）から** 発行する（batch は非特権）。
2. **3DSTATE_VF_INSTANCING は頂点要素ごとの状態でコンテキストに残る**。未初期化だと不定。要素0/1を明示クリア。
3. **統計カウンタは各ステージの Statistics Enable を立てないと動かない**
   （3DSTATE_CLIP dw1 bit10 / 3DSTATE_SF dw1 bit10 / 3DSTATE_WM dw1 bit31 / 3DSTATE_VS dw7 bit10）。
   3DSTATE_VF_STATISTICS だけでは IA 系しか動かない。
4. **URB は push constant 領域の後ろから**。Gen12 の max_constant_urb_size_kb=32、
   URB 割り当ては 8KB チャンク単位なので VS の開始チャンクは 4。
5. **`kern_logf` はフィールド幅指定（`%-16s`）非対応**。指定すると引数がずれてポインタ値が出る。
6. **判定基準の誤り（自分のバグ）**: `ps_invocations=0` / `cl_invocations=0` は正常。
   PixelShaderValid=0 なら PS 呼び出しは定義上ゼロ、クリッパは RECTLIST の必須条件として
   無効化されるためバイパスされて 0。スクリーン空間座標でクリッパを有効にすると NDC 体積で
   全部棄却されるので、有効化は誤り。

### 次段（step 2b）: ピクセルシェーダ
自前コンパイラの `i915_vk_eu_send()` は**プレースホルダ**で、mlen/rlen/EOT/descriptor をすべて
`(void)` で捨てている（`compile.c` の `compile_terminate` のコメント通り「descriptors are completed
on hardware」）。したがって実 RT write は未実装で、まず**手書き GEN カーネル**で正解の
エンコーディングを確定させ、その後コンパイラに教え込む。

確定済みの Gen12 RT write ディスクリプタ:
- SFID = `GEN_SFID_RENDER_CACHE` = 5
- msg_type = `GEN_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE` = 12 (desc bits 17:14)
- binding table index: desc bits 7:0 / msg_control: bits 13:8 / last_render_target: bit 12
- rt slot group = (group/16) << 11
- Gen12 (ver>=11) は ex_desc に Render Target Index を置く: `ex_desc = target << 12`
- SIMD8 single source subspan01 の msg_control = 4
- send 命令フィールド(xe.json): SEND_EOT=bit34, SEND_SFID=bits95:92,
  SEND_DESC_IS_REG=bit48, SEND_EX_DESC_IS_REG=bit49, SEND_EX_BSO=bit39

RT 読み戻しで単色が出れば step 2 完了 → scanout、三角形へ。

## p011 増分E-7 (2026-09-15): Mesa の EU アセンブラ導入と、ピクセルシェーダ投入（draw step 2b、未完）

### 成果1: gentool（Mesa の Gen12 EU アセンブラ／逆アセンブラ）をビルド

`plan/ws031/mesa-refs/mesa` を最小構成で configure してビルド:

    python3 -m venv /tmp/mesa-venv && /tmp/mesa-venv/bin/pip install Mako PyYAML packaging setuptools
    PATH=/tmp/mesa-venv/bin:$PATH meson setup build-gentool -Dtools=intel \
      -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
      -Degl=disabled -Dgbm=disabled -Dopengl=false -Dllvm=disabled -Dbuildtype=release
    ninja -C build-gentool src/intel/compiler/gen/gentool

    gentool asm -p tgl -o out.bin shader.asm
    gentool disasm -p tgl out.bin

これは**再利用可能な大きな資産**: 自前コンパイラ（vk/eu.c, vk/compile.c）の出力を逆アセンブルして
検証できるようになる。ユーザ環境は汚していない（venv と mesa-refs 配下のみ）。

実際に初回から効果があり、`ERROR: send with EOT must use g112-g127` を**ビルド時に**指摘した。
実機なら原因不明のハングになっていた類のミス。

### 成果2: Gen12 render target write の符号化を確定

    (W) mov (8|M0)  r112:f 0x3f800000:f   /* red   */
    (W) mov (8|M0)  r113:f 0x0:f          /* green */
    (W) mov (8|M0)  r114:f 0x0:f          /* blue  */
    (W) mov (8|M0)  r115:f 0x3f800000:f   /* alpha */
        send.render (8|M0) null r112 null 0x0 0x08031400 {EOT,@1}
    // wr:4+0, rd:0; simd8 rt_write last_rt (8) bti(0)

- SFID = GEN_SFID_RENDER_CACHE = 5
- desc: bti bits7:0 / msg_control bits13:8 (SIMD8 single source subspan01 = 4) /
  last_rt bit12 / msg_type bits17:14 = 12 / **mlen bits28:25 / rlen bits24:20**
  （mlen/rlen は 24:20・19:16 ではない。gentool の `wr:`/`rd:` 表示で確定）
- Gen12 は ex_desc に Render Target Index（bits 12+）と null_rt（bit20）
- EOT する send のペイロードは **g112-g127 でなければならない**
- ソースは `plan/ws031/shaders/const_color_ps.asm`

なお自前コンパイラの `i915_vk_eu_send()` は mlen/rlen/EOT/descriptor をすべて `(void)` で捨てる
プレースホルダのままで、実 RT write は出せない。まず手書きで正解を確立し、後でコンパイラに教える方針。

### 実機で発見・修正した実バグ（3件）
1. **MOCS フィールドは index を1ビット左シフトして入れる**。生の index を書くと1つ右の
   エントリが選ばれ、Gen12 の index 0/1 は**予約**なので未定義動作になる。
   根拠: Linux i915 の MOCS テーブル（index2=WB, index3=uncached）と Mesa isl の TGL 分岐
   （`internal = 2 << 1`, `uncached = 3 << 1`）が一致。`GEN12_MOCS(index)` を追加し、
   STATE_BASE_ADDRESS / RENDER_SURFACE_STATE / VERTEX_BUFFER_STATE / XY_FAST_COLOR_BLT を修正。
   clear selftest は新エンコーディングでも通過（修正が安全であることの確認）。
2. **`CULLMODE_BOTH = 0`, `CULLMODE_NONE = 1`**。3DSTATE_RASTER を全ゼロで出すと
   **全プリミティブが culling される**。BLORP も anv も CULLMODE_NONE を明示している。
3. **null depth buffer も型を持つ**。`SURFTYPE_NULL` には `D32_FLOAT(1)` を組み合わせる
   （Mesa isl と同じ）。フォーマット 0 は深度ステンシル書式で、存在しないステンシルを待たせる。

### 現在の到達点と残課題
ジオメトリは**クリッパまで完全に通っている**（実機カウンタ: ia_vert=3, ia_prim=1, vs=3,
**cl_inv=1, cl_prim=1**）。しかし **PS スレッドが一度も起動しない**（ps=0, ps_depth=0）まま、
3DPRIMITIVE 直後の PIPE_CONTROL（CS_STALL + RT flush）でパイプラインがドレインせず停止する。
ACTHD = batch->va + 0x3c0 = dword 240（バッチ全246 dword の末尾側）で、IPEHR = 0x7a000004 = PIPE_CONTROL。

**シェーダコードは無関係であることを証明済み**: mov を一切含まない 1 命令
（`send.render null_rt {EOT}`、ループ不可能）でも同一箇所で同一のハング。つまり停止点は
CL→SF→WM→PSD の固定機能ステートであって、EU プログラムではない。

未検証の Mesa との差分（次に当たる候補）:
- **3DSTATE_SBE `Number of SF Output Attributes` = 0**。Mesa の経路（BLORP の clear、anv の
  simple shader）は常に 1 以上。属性ゼロの PS が許されない可能性。
- **3DSTATE_WM `Barycentric Interpolation Mode` = 0**。
- **3DSTATE_DEPTH_BOUNDS を出していない**（anv の simple shader は出す）。
- 3DSTATE_VF_SGVS の InstanceIDEnable（BLORP/anv は立てる）。

## p011 増分E-8 (2026-09-15): PS ディスパッチ・ハングの精密切り分け（draw step 2b、継続中）

Linux 7.1 ソース（`~/linux-pc98/external/kernel/linux-7.1/drivers/gpu/drm/i915`）を一次情報として
ADL-P の全 workaround を転記・適用し、多数の実バグを潰したが、**PS スレッドが EU 上でハングする**
最終ブロッカーは未解決。ただし原因は EU スレッド実行/終了レベルまで精密に特定できた。

### 適用した修正（すべて Linux 7.1 / Mesa 由来、実機で検証）
- **MMIO エンジン/GT workaround**（`i915-workarounds.inc` 新規 + selftest で適用）:
  Wa_14015795083(GEN7_MISCCPCTL DOP gate off)、Wa_1606700617(GEN9_CS_DEBUG_MODE1 FF_DOP_CLOCK_GATE)、
  Wa_14010919138(GEN7_FF_THREAD_MODE tess DOP gate)、Wa_1607297627(RING_PSMI_CTL power down disable)、
  ROW_CHICKEN2/4・SAMPLER_MODE（multicast MMIO）、RING_CMD_CCTL（CS 自身の MOCS）。
- **コンテキスト workaround**（LRI でリング経由=特権）:
  COMMON_SLICE_CHICKEN3(CPS aware color pipe disable)、CS_CHICKEN1(preempt/replay/3DPRIM pause)、
  FF_MODE2(GS/HS timer 224, TDS 4)、HIZ_CHICKEN、COMMON_SLICE_CHICKEN4、COMMON_SLICE_CHICKEN1。
- **RPCS（render power/clock state）**: `lrc.c` で `CTX_R_PWR_CLK_STATE` を 0 から
  `intel_sseu_make_rpcs` 相当（slice fuse GEN11_GT_SLICE_ENABLE から slice 数を数えて
  GEN8_RPCS_ENABLE|S_CNT_ENABLE|slices<<12）に変更。fuse 実測 slice_en=0x1, dss_en=0x1f, rpcs=0x80041000。
- **golden state 群**（batch に emit）: WM_HZ_OP(override クリア)、SAMPLE_PATTERN(1x center)、
  DEPTH_BOUNDS、AA_LINE_PARAMETERS、WM_CHROMAKEY、POLY_STIPPLE_OFFSET、LINE_STIPPLE、
  BINDING_TABLE_POINTERS_VS/HS/DS/GS(クリア)、CPS_POINTERS(disabled CPS_STATE)、
  BINDING_TABLE_POOL_ALLOC(disabled)、全ステージ 3DSTATE_CONSTANT_*(empty)、
  PUSH_CONSTANT_ALLOC_PS(全32KB)、null stencil に型付与、VERTEX_BUFFER L3 bypass disable、
  3DPRIMITIVE 前に PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD|DEPTH_STALL)。

### 精密切り分け（実機カウンタ + SC_INSTDONE + ROW_INSTDONE + fault reg）
ジオメトリは**クリッパまで完走**: `ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1`。
- **GPU fault なし**: GEN12_RING_FAULT_REG(0xcec4) の VALID bit クリア。TLB fault data も無害。
- **SC_INSTDONE=0xfbfffffd**: WMFE(bit1 windower front end)と PSS(bit26 pixel scoreboard)のみ not-done。
  **RCC(bit9 render color cache)は done** = カラー書き込みは一度も RCC に到達していない。
- **ROW_INSTDONE=0x8610e87f**（idle は 0xffffffff）: **複数 EU が not-done**。
  → **PS スレッドは EU にディスパッチされ、実行中のままリタイアしない**。
  ps_invocations=0 はリタイアカウンタなので、ハング中スレッドでは 0 のまま。

### 結論（今回の到達点）
**PS スレッドはディスパッチされるが、RT write 完了前に EU 上でハングする**。
RCC が idle = RT write が RCC に届いていない = スレッドは RT write/EOT の手前で停止。
PSS はピクセル完了通知を待ち続け、WMFE も詰まり、3DPRIMITIVE 後の PIPE_CONTROL でパイプが
ドレインせず停止。

### 排除できた原因（すべて実機で確認）
GPU fault / power gating(RPCS) / MOCS(index<<1 修正済) / cull mode / depth format /
linear vs Tile4 RT(両方ハング) / force thread dispatch(効果なし) / golden render state(Gen12 は
Linux も未使用: render_state_get_rodata が NULL) / pixel scoreboard stall(効果なし) /
シェーダ内容(bare send.ts EOT も同一ハング) / 全 ADL-P workaround(ハングは WA 追加前から同一)。

### 手書き PS カーネル（gentool 検証済みだが SWSB 未スケジュール）
    (W) mov (8|M0) r112:f 0x3f800000  (r113=0,r114=0,r115=0x3f800000)
        sendc.render (8|M0) null r112 null 0x0 0x08031400 {EOT,@1}
gentool は「simd8 rt_write last_rt bti(0), wr:4」と解釈（符号化は正しい）。
だが gentool は**アセンブラであって SWSB スケジューラではない**。Gen12 は明示 SWSB 必須で、
mov→send の依存が @1 のみ（r112/113/114 の mov は 2-4 命令前）だと未解決依存が残る可能性。

### 次段の推奨（最有力順）
1. **参照 PS カーネルの入手**: Mesa の brw コンパイラ（libintel_compiler_brw + NIR）をビルドし、
   定数色 FS を実際に Gen12 ISA へコンパイルして、正しい SWSB/メッセージ/ヘッダを持つ
   カーネルバイナリを得る。手書きカーネルと逆アセンブル比較（gentool disasm）して差分を特定。
   これが EU ハングを決定的に解決する道。
2. EU スレッド状態の直接読み出し（per-subslice の TDL/EU IP を MCR 経由で読む）。
3. RT write に proper R0 ヘッダを付ける版を試す（Gen12 は headerless が正だが、
   scoreboard 通知の観点で要検証）。

### インフラ成果（再利用可能）
- **gentool（Mesa Gen12 EU アセンブラ/逆アセンブラ）ビルド済み**:
  `~/zedBSD/plan/ws031/mesa-refs/mesa/build-gentool/src/intel/compiler/gen/gentool`。
  venv: `/tmp/mesa-venv`（Mako/PyYAML/packaging/setuptools）。configure は
  `meson setup build-gentool -Dtools=intel -Dgallium-drivers= -Dvulkan-drivers= ...`。
- **dump-gen-packets.py**（`plan/ws031/tests/`）: gen120.xml を import 連鎖ごと解決して
  全パケット長・ビット位置・enum を機械抽出。今セッションの全 3D state はこれで転記。

## p011 増分E-10 (2026-09-15): WM.StatisticsEnable 監査 — ps=0 の再解釈と windower dispatch 疑い

専門家の助言 #1「3DSTATE_WM を BLORP と同じ空にした結果 StatisticsEnable を落としていないか」を監査。

### 発見: PS 統計はずっと無効だった
`3DSTATE_WM` DW1 = 0（行874「空、as BLORP emits」）だった。DW1 bit31 = Statistics Enable
（E-6 の落とし穴メモ点3 でも認識済みだったが、PS 追加時に空 WM にして誤って落とした）。
→ **E-7/E-8 の全 PS-hang 調査で読んでいた `ps_invocations=0` は計数無効による当然のゼロで、
証拠価値がなかった。** 他ステージ（VS DW7 bit10, CLIP/SF dw1 bit10）は立っていたので vs/cl は出ていた。

### 実機 (E-10): WM DW1 bit31 を立てて再測
```
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```
統計有効でも **ps=0**。この値はハング診断内の**直接 MMIO 読み**（`GEN12_REG_PS_INVOCATION_COUNT`,
行1479）で、wedge 中でも ia/vs/cl が正しいライブ値を返す＝カウンタブロックは生きている。
（前回まで使っていた post-hang の MI_STORE リクエスト方式は wedge したエンジンで完走せず
"statistics readback failed" になる。直接 MMIO 方式が正解。）

### 固定機能フィールドの genxml 厳密監査（全て正しい）
gen120/gen110/gen90.xml でビット位置を機械確認:
- 3DSTATE_PS DW6: bit0=8PixelDispatchEnable=1 ✓, bits31:23=MaxThreadsPerPSD=63 ✓(=64-1, Mesa と同じ),
  DW7 bits22:16=grf_start0=2 ✓, DW3 bits25:18=BindingTableEntryCount=1 ✓, DW4 scratch=0 ✓, DW1 KSP0=1024 ✓
- 3DSTATE_PS_EXTRA DW1: bit31=PixelShaderValid=1 のみ, bit30(mbz)=0 ✓, AttributeEnable=0（無属性PSで正）
- 3DSTATE_SBE DW1: VertexURBEntryReadLength=1（**非ゼロ、windower stall の典型 ReadLength=0 ではない**）,
  ReadOffset=1, ForceOffset/Length=1, NumSFOutputAttr=0 — position-only で妥当

### 解釈（report5 の fetch 寄り結論を反転させ得る）
- ps=0（dispatch カウンタ）→ 素直には **windower が PS スレッドを一つも dispatch していない** (仮説b)。
  これは report5 の有力仮説 (a)「EU 命令フェッチ失敗（スレッドは EU 上に存在）」と矛盾する
  （dispatch されていれば fetch で止まっても dispatch カウンタは加算されるはず）。
- ただし専門家の caveat: 統計の意味ある読み出しには pipeline flush が要り、drain 未完のこのケースでは
  ゼロ解釈に制限が残る（dispatch は起きているがカウンタが flush 前で不可視の可能性）。
- かつ固定機能フィールドは全て genxml 正しく、「dispatch を妨げる明白なステートバグ」は未発見。
- → **dispatch-vs-fetch の決着には専門家推奨の PS 内メモリ marker（dispatch を統計非依存で確認）が必要。**

差分: selftest.c 行874（WM StatisticsEnable=bit31）。run 機構: `/tmp/run-e10.sh`（build selftest config →
scp guest → vfio passthrough boot、debugcon 捕捉）。再起動不要（qemu 終了時 FLR で vfio リセット、連続 run 可）。

## p011 増分E-11/E-12 (2026-09-15): 専門家の A/B/C 対照試験 — dispatch は起きていない疑いが強化

専門家の計画（B: WM のみ変更 → C: PS 内 marker → refblorp）を実行。

### E-11 = Test B（同一シェーダ、WM に ForceThreadDispatch=ForceON + EDSC_PSEXEC を追加）
WM DW1 = `(1<<31)|(2<<19)|(1<<21)`（StatEnable|ForceON|PSEXEC）。gen80.xml でビット確認
（ForceThreadDispatch bits20:19=ForceON=2、EarlyDepthStencil bits22:21=PSEXEC=1）。
加えて pre-draw PIPE_CONTROL 直後・3DPRIMITIVE 直前に **CS marker（MI_STORE_DWORD_IMM）** を追加。

```
draw batch 353 dwords markerA=0xa5a50001 markerMid=0xc5c50003 markerB=0x00000000 completed=2 seqno=3
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```
- **markerMid 着地** → CS は pre-draw stall（CS_STALL|STALL_AT_SCOREBOARD|DEPTH_STALL）を通過し
  3DPRIMITIVE を発行済み。**ハングは pre-draw stall ではなく post-draw drain PIPE_CONTROL**（IPEHR=0x7a000204）。
- **ForceThreadDispatch=ForceON でも ps=0**。coverage 非依存で dispatch を強制しても windower は
  PS を dispatch しない → 「windower が dispatch できない」の強い証拠。WM state 変更では解消せず。

### E-12 = Test C（PS 先頭に A64 data-cache marker を追加した FS）
refps_marker.c（refps.c + `nir_store_global(0xc0ffee01 → 0x100400c10)`）を brw_compile_fs でコンパイル
（size=496, d8=1 d16=1, grf_used=128, scratch=0, grf_start0=2）。gentool 逆アセンブルで marker が
**`send.hdc1 a64_untyped_write`（Data Port 1、RT 経路とは別）**で RT write の**前**に emit されることを確認。
PS_EXTRA に HasUAV(bit2) を追加、WM は B のまま、SIMD8 dispatch（grf_start0=2）。

```
draw batch 353 dwords markerA=.. markerMid=0xc5c50003 markerPS=0x00000000 markerB=0x00000000 ..
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```
- **markerPS=0**（A64 marker 未着地）+ ps=0 の**2つの独立 negative** → PS は store まで実行していない、を強く示唆。
- RT surface も未書き込み（draw pixels 全て 0x0）。

### 解釈（専門家の fixed interpretation に従う）
- markerPS=0 は「未 dispatch/初期停止/marker のメモリ経路・可視性の問題」いずれとも両立し、
  **「dispatch されなかった」の確定ではない**。A64 store が drain 未完で L3 に留まり memory 未達＝
  CPU から見えないだけ、の可能性が残る。
- 確定には **compute-shader 陽性対照**（同一 RCS/PPGTT/命令配置で同じ A64 store を正常完了させ、
  drain/reset 前に CPU 可視を検証）が必要。これが取れれば markerPS=0 を「PS 未実行」の証拠に昇格できる。

### 確定した negative（重要）
- pre-draw stall は原因でない（markerMid 着地）。
- coverage/dispatch-gating は原因でない（ForceON 無効）。
- PS/PS_EXTRA/SBE の全フィールドは genxml 正しい（E-10）。
- → 残る mechanism 不明。次は compute 陽性対照 or refblorp（全 packet+参照データ+caller 初期化の照合）。

インフラ追加: refps_marker（build-gentool 登録済み）、run-e1x.sh（TAG 差し替えで連続実行、再起動不要）。

## p011 増分E-13 (2026-09-15): 専門家指定の2静的監査 — marker predicate と stateless MOCS

専門家（第7報回答）の指示: compute 対照を作る前に (1) marker の sample-mask predicate、(2) stateless
data-port MOCS が本当に UC か、を静的監査せよ。両方とも専門家の予測と一致した。

### 監査1: Test C の marker は sample mask で predicate される（無条件トレースでない）
gentool verbose 逆アセンブル（refps_marker SIMD8）:
```
(W) mov (1) f1.0:uw   r1.14:uw            ; f1 <- r1.14 = PS dispatch/sample mask
    mov (8) r6        0xc0ffee01           ; data（channel mask 下）
(W) mov (8) r2.0      0x00400c10           ; addr low（無条件）
    mov (8) r4.0<2>   r2.0<0>              ; addr broadcast（channel mask 下）
(f1.0) send.hdc1 (8) null r4 r6 ... a64_untyped_write   ; store は f1.0=sample mask で predicate
    sendc.render (8) ... {EOT}
```
→ **markerPS=0 は「スレッド未実行」でも「スレッド実行済みだが sample mask=0 で store 無効」でも起こる。**
Mesa 25.1 の HDC lowering が FS 副作用に `brw_emit_predicate_on_sample_mask()` を適用するため（正しい挙動）。
帰結: E-12 の「ForceON 無効＝dispatch-gating でない」は「同一 FS への ForceON/PSEXEC 変更だけでは
症状不変」に後退。ForceON とスレッドの実 channel mask が非ゼロは別事項。ps=0 と markerPS=0 は
独立した2つの不在証明としては扱わない。

### 監査2: stateless data-port MOCS = index 3 = UC（memory 直達）
- SBA emit: DW3 = `mocs << 16`、mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX=3) = 3<<1 = 6
  → **DW3 = 0x00060000**（stateless MOCS field=6 → table index 6>>1=3）。専門家の予測値と一致。
- driver は MOCS table を programming（engine.c init_mocs/l3cc、GEN9_LNCFCMOCS 書き込み）。
  `gen12_mocs_table[3]` = `MOCS_ENTRY(3, LE_1_UC|LE_TC_1_LLC, L3_1_UC)` = **UC**（LLC uncached, L3 uncached）。
- → A64 marker write は **UC で memory 直達**。「L3 dirty line に残って CPU 不可視」説は**否定**。
  markerPS=0 は可視性問題ではない。

### 帰結と次段
markerPS=0 の残る説明は (a)スレッド未実行、(b)スレッド実行済みだが sample mask=0。**sample-mask
predicate が交絡**。決着には無条件の EU 実行トレース = 専門家指定の **Gen12.0 GPGPU_WALKER compute
陽性対照**（refcs: 1 workgroup/1 invocation、sampler/SLM/barrier/scratch なし、compiler 生成 A64 store で
tag 書き込み、正常終了）が必要。参照は Mesa `blorp_exec_compute()` の GFX_VERx10<125 経路
（CFE_STATE/COMPUTE_WALKER は Gen12.5+ なので**使わない**）。成功すれば EU/A64/命令フェッチ/PPGTT が
一般に動くことを陽性で確定し、PS ハングを PS 固有問題へ分岐できる。並行して refblorp（通常色書き経路の
全 packet+参照データ+caller 初期化を完走させる参照一式）。

## p011 増分E-14 (2026-09-15): compute 陽性対照の完全仕様化（refcs + GPGPU_WALKER）

専門家指定の Gen12.0 compute 陽性対照を、実装直前まで仕様化。参照 = Mesa `blorp_exec_compute()`
の GFX_VERx10<125 経路（CFE_STATE/COMPUTE_WALKER は Gen12.5+ なので不使用）。

### 構築済みインフラ
- **refcs**（build-gentool 登録済み）: brw_compile_cs で最小 CS をコンパイル。
  144B, SIMD8(prog_mask=0x1), grf_used=128, local=1,1,1, barrier/sampler/scratch なし。
  disasm 確認: `send.hdc1 a64_untyped_write`（**predicate なし＝無条件**）→ `send.ts {EOT}`。
  store: tag 0xc0ffee02 → 0x100400c20（stateless MOCS=index3=UC で memory 直達）。
- ADL-P devinfo（refcs から取得）: max_cs_threads=112, subslice_total=6 → VFE MaxThreads=671。

### パケット仕様（全て genxml で確認、手 emit 値を確定）
scratchpad/compute-control-design.md に全 DW 値を記載。要点:
- 独立 batch: PIPE_CONTROL(flush) → PIPELINE_SELECT(GPGPU=2) → SBA(draw と同一) →
  PIPE_CONTROL(inval) → PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD, VFE 前必須) →
  MEDIA_VFE_STATE(DW3=0x029F0200, DW5=0x00020000) → MEDIA_INTERFACE_DESCRIPTOR_LOAD(TotalLen=32) →
  GPGPU_WALKER(SIMD8, 1x1x1, RightMask=0x1) → PIPE_CONTROL(flush) → markerCS → BB_END。
- INTERFACE_DESCRIPTOR_DATA(8DW, dynamic heap): KSP=1024, BTE=1, NumThreads=1, SLM/Barrier=0。
- MEDIA_VFE_STATE header=0x70000007, MIDL header=0x70020002, GPGPU_WALKER header=0x7105000D。

### 判定設計
- markerCS 着地 & compute marker=0xc0ffee02 → EU/A64/fetch/PPGTT/可視性すべて陽性 →
  PS ハングは PS 固有（windower dispatch/payload/sample-mask 経路）へ大きく分岐。
- markerCS 着地 & marker=0 → A64 可視性 or store 自体（PS と共通の下位問題）。

### 状態
実装（selftest.c に compute 経路を追加）は機械的に落とせる段階。二段階ハンドシェイク（mid-hang
可視性）と 3D→GPGPU 同一 batch 切替は simple 版（独立 batch, 正常完走）では不要なので後回し。
専門家 report8 の回答（pipeline 切替・fence）を simple 版成功後に反映。

## p011 増分E-15 (2026-09-15): compute 対照の前提確定と専門家の設計修正（実装直前）

専門家（report8 回答）の指示で、投入前に program data とトポロジを確定し、設計を修正した。

### 確定した program data（refcs 出力）
```
prog_offset0=0 per_thread(regs=0) cross_thread(regs=0) total_scratch=0 total_shared=0
```
→ **P=0, C=0** 確定。CURBE=0 が正当:
- VFE.CURBEAllocationSize = ALIGN(P*T+C, 2) = 0
- IDD.ConstantURBEntryReadLength = 0、CrossThreadConstantDataReadLength = 0
- MEDIA_CURBE_LOAD は長さ0で明示発行（BLORP 準拠）
- KSP = InstructionBase + kernel_offset(1024) + prog_offset0(0)

### 確定した実機トポロジ（重要な修正）
実機ログ: `dss_en=0x0000001f` = **5 DSS**（popcount=5）。refcs の subslice_total=6 は PCI-ID default で
**実機と不一致**。→ VFE MaximumNumberofThreads = max_cs_threads(112) × **5** − 1 = **559**
（671 ではない）。VFE.DW3 = (559<<16)|(2<<8) = **0x022F0200**。実装では 0x913c を popcount して runtime 算出。

### 専門家による設計修正（compute-control-design.md に対して）
1. **最優先: SBA を PIPELINE_SELECT(GPGPU) の前に**。Wa_1607854226（ADL 対象）で SBA は 3D モードで適用。
   シーケンス: [3D確立] → pre-SBA flush → SBA → state/const/tex/inst invalidation →
   **PIPE_CONTROL(CS_STALL|RT_FLUSH|DEPTH_FLUSH|HDC_PIPELINE_FLUSH)** → PIPELINE_SELECT(GPGPU=2, 0x69041312)
   → PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD) → VFE → CURBE_LOAD → MIDL → markerReady → WALKER →
   MEDIA_STATE_FLUSH → [Wa_1607156449: post-sync なし stalling PC] → post-sync PC(markerDone) → markerCS → BB_END。
2. **HDC Pipeline Flush = PIPE_CONTROL DW0 bit9**（DC Flush=DW1 bit5 とは別）。生 DWORD で確認。
3. **refcs は clean/復旧済み RCS から実行**（PS ハングより前 or リセット後）。
4. IDD: **NumThreads=1（threads-1 でない）**、**BTE=0（A64 only、BLORP の surface count はコピーしない）**、
   Sampler=0、SLM=0、Barrier=0、**ThreadPreemptionDisable=1（DW2 bit20、Iris が Gen12 で設定）**。
5. GPGPU_WALKER 主要値は確定済みで正しい: SIMD8(0)、counter max=0、group dim=1、right mask=0x1、bottom=0xffffffff。
6. **観測 3 段**: markerReady(walker 前 MI)、compute marker(EU A64 store 0x100400c20)、
   markerDone(**post-sync PIPE_CONTROL** write、単なる MI でなく end-of-pipe 同期)。IDD は CPS 流用でなく専用領域。
7. **2 条件で切り分け**: **C0=walker なし**（初期化列+終端同期+marker が通るか）、**C1=walker あり**（refcs 1 回）。
   C0 成功 & C1 停止なら「walker 追加で不成立」まで絞れる。まだ fence/polling/PS 変更は入れない。

### 判定（専門家、C1、markerReady 更新時）
- EU marker + markerDone 更新 → 単純 compute 陽性対照成立（EU 実行・A64・終了・完了後 CPU 読み戻し）。
- EU 更新・Done 未更新 → store まで実行。EOT/終端同期を調べる。
- EU 未更新・Done 更新 → walker の仕事量/mask/KSP/payload/宛先を確認。
- 両方未更新 → 新規 compute 設定 or 共通基盤、未確定。
「完了したが marker=0 → PS と共通の下位問題」は強すぎるので確定させない。compute 成功も PS 実行を直接証明しない
（PS 固有へ重点を移せる、という表現に留める）。sample-mask predicate は PS 側で残る。

### 状態
全パラメータ・全パケット DW 値・修正シーケンス確定。実装（selftest.c に独立 compute 経路 + C0/C1 toggle +
MEDIA_CURBE_LOAD/MEDIA_STATE_FLUSH/post-sync PIPE_CONTROL emit）が次段。refcs は build-gentool 登録済み。

## p011 増分E-16 (2026-09-15): compute 陽性対照 C0/C1 — 問題は PS 固有でなく EU 共通と判明

専門家の C0/C1 設計（SBA 先行、CURBE 省略、MIDL 前 MEDIA_STATE_FLUSH、post-sync PPGTT PC、
Wa_1607156449）を実装し実機投入。**分岐点の結果**が出た。

### C0（walker なし）= 完全成功
```
compute C0 ready=0xc0ffee10 eu=0xdead0000 done=0xc0ffee20 done_hi=0x00000000 cs=0xc0ffee30 completed=3 seqno=3
```
markerReady 更新 / EU 初期値 / **markerDone(post-sync PPGTT write) 更新** / markerCS 更新 / 完走。
→ GPGPU 初期化列（PIPELINE_SELECT(GPGPU)、SBA(3D モードで)、VFE(MaxThreads=559)、MEDIA_STATE_FLUSH、
MIDL）+ **非特権 batch の PPGTT post-sync PIPE_CONTROL** + end-of-pipe 完了、すべて正常。専門家の
「非特権 batch で GGTT post-sync は NOOP → PPGTT で書く」も実証（markerDone が PPGTT 番地に着地）。

### C1（walker あり）= ハング（PS と同一署名）
```
compute C1 ready=0xc0ffee10 eu=0xdead0000 done=未更新 cs=未更新 completed=3 seqno=4
HANG: ipehr=0x70040000(MEDIA_STATE_FLUSH) acthd=0x100600150 instdone=0xffdeffff sc=0xffffffff row=0x8610e87f
```
- markerReady 更新（CS は walker まで到達、walker を発行）。
- **EU marker 未更新**。compute の A64 store は**無条件（predicate なし、gentool 確認済み）**なのに未着地 →
  **compute スレッドが store 命令を実行していない**（PS の sample-mask 交絡がないので確定的）。
- CS は walker 後の MEDIA_STATE_FLUSH でスレッド完了を待って停止（IPEHR=0x70040000）。
- **row_instdone=0x8610e87f = PS ハングと完全に同一**。EU not-done も同じ。

### 結論（調査の大転換）
- batch/VFE/walker/IDD は全 DW を dump して確認済み（VFE MaxThreads=0x22f=559、walker SIMD8/dim=1/
  right=0x1、MIDL offset=0x380、post-sync 0x7a000004/0x104000/done_va/tag）。設定は仕様どおり。
- **PS と最小 compute が同一署名でハングし、無条件 compute store すら実行されない** →
  問題は **PS 固有（windower dispatch）ではなく、PS/compute 共通の「EU が dispatch された
  スレッドを実行/完了できない」障害**。命令フェッチ/スレッド起動段の共通故障（旧仮説 a）が最有力に復帰。
- ただし専門家の C1 判定（EU 未更新+Done 未更新）通り、「新規 compute dispatch 設定の不備」も論理的には
  残る。ただし walker/VFE/IDD の全 DW が仕様一致で、かつ PS と同一署名という点が「共通 EU 障害」を強く支持。

### 次に切り分けるべき点
- EU 命令フェッチ（Instruction Base 相対、KSP=instruction+1024）が両者で失敗している可能性。
  以前 PS で「命令領域を EOT で carpet しても hang」だったが、その再検証を compute の無条件 store で行える。
- EU の電源/クロック/enable（RPCS、EU fuse）— rpcs(ctx/reg)=0x80041000 は設定済みだが要再確認。
- スレッド完了通知（EOT）経路の共通故障。

C0 の完全成功で「submission/PPGTT/state/post-sync/完了」は健全と確定。障害は EU スレッド実行そのものに局在。
default ビルド warning 0。i915.c は bring-up 中 draw をスキップ（`if(0)`）、compute を rt の後に配置。

## p011 増分E-17 (2026-09-15): C2(store なし)も同一ハング — EU 命令フェッチが prime suspect

専門家指定の ①C0 に GPU 読み戻し追加、②C2(store なし・正規 EOT) 実行、③電源 ACK/MCR 記録を実装・投入。
順序を C0→C2→C1 にして C2 を clean engine で先に走らせた（C1 が wedge する前に）。

### GPU 読み戻し（C0, MI_COPY_MEM_MEM PPGTT→PPGTT）= 完全一致
- IDD readback = `00000400 0 00100000 0 0 0 00000001 0` = 期待値と**完全一致**
  （KSP=0x400, ThreadPreemptionDisable, NumThreads=1）。
- kernel readback（36 DW）= store カーネルのバイト列と**完全一致**。
→ **GPU(CS) は IDD もカーネルも正しい PPGTT VA から読める**。heap 書き込み・上書き・アドレス変換・
  公開はすべて正常。専門家の分岐「一致 → CS はその VA を読める（ただし EU I-cache 経路は別）」。

### 電源 ACK / MCR（baseline/C0/C2 すべて同一・健全）
`eu_dis(0x9134)=0x0`（EU 無効なし）、`slice_ack(0x804c)=3`、`ss01_eu_ack(0x805c)=3`、
`ss23_eu_ack(0x8060)=3`（EU 給電済み）、`mcr(0xfdc)=0x80000000`（multicast 復元済み、単一 instance 固着なし）。

### C2（store なし・正規 EOT）= C1 と完全同一署名でハング【核心】
refcs_empty（brw_compile_cs, 32B）= `(W)mov r127 r0; (W)send.ts {EOT}`（R0 由来 payload の正規 compute
終了。bare send.ts ではない、gentool 確認済み）。
```
compute C2 ready=0xc0ffee10 eu=0xdead0000 done=0xdead0000 cs=0xdead0000 completed=3 seqno=4
HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH) row_instdone=0x8610e87f instdone=0xffdeffff sc=0xffffffff
```
- **メモリ store が一切ないのに、C1(store)・PS と完全同一署名でハング**。
- markerReady 更新（walker 発行）だが done/cs 未更新（post-walker flush でスレッド完了待ち停止）。
→ 専門家判定「A64 store なしでも停止 → EU 実行環境の共通故障」。ハングは store・windower・PS 固定機能とも無関係。

### 収束した結論
- windower dispatch / sample-mask / PS 固定機能 / A64 store / RT write: **すべて原因でない**（compute にこれらは無い）。
- submission / PPGTT / state / SBA / VFE / MIDL / walker 発行 / post-sync: **すべて健全**（C0 成功 + GPU 読み戻し一致）。
- EU 電源・fuse・MCR: **健全**。
- **残る: dispatch された EU スレッドが命令を実行/完了しない**。C1 の無条件 store（早い命令）が未実行だった
  ことから「EU が命令を実行していない」が有力 → **EU 命令フェッチ（Instruction Base 相対 KSP=0x400、
  実効 VA=0x1_00400400、4GB 超）が成立していない**が prime suspect。CS のメモリ読みは通るが EU I-cache 経路は別。

### 次（専門家 §4）
命令領域だけを低位 VA(0x00800000) vs 高位 VA(0x1_00800000) にマップして C2 を走らせ、
L だけ成功なら高位 VA を含むアドレス経路（自作 SBA/PTE/変換）の不具合を確定。
両方失敗なら命令フェッチ以外へ。要 VA 配置制御（GEM allocator or bind-at-VA）。
default ビルド warning 0。i915.c は draw スキップ中、compute を rt の後に配置。

## p011 増分E-18 (2026-09-16): 低位 VA でも同一ハング — bit32/高位 VA 説を棄却

専門家 §4 の低位/高位 VA テストを実装。命令ページを `drv_i915_ppgtt_insert` で明示 VA にマップし、
SBA Instruction Base をそこへ向ける。surface/dynamic base は現行の高位 VA のまま（C0 で健全確認済み）、
**命令フェッチのアドレスだけ**を動かす。C2(store なし)カーネルを使用。

### 結果
```
compute C0     inst_base=0x100400000  ready=✓ eu=初期 done=✓ cs=✓ completed=3 seqno=3   （完走）
compute C3-low inst_base=0x00800000   ready=✓ eu=dead done=dead cs=dead completed=3 seqno=4
  HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH) row_instdone=0x8610e87f （PS/C1/C2 と完全同一）
```
電源 ACK/MCR は C0/C3-low とも baseline と同一（健全）。fault=0。

### 結論
- **4GB 超 Instruction Base（bit32）は原因ではない**。命令ベースを低位 0x800000 に変えても同一ハング。
  smoking gun と見た「全 VA が 4GB 超」は、少なくとも命令フェッチ経路の直接原因ではなかった。
- C0(walker なし)だけ完走、C1/C2/C3-low/PS(walker/primitive で EU スレッド dispatch)はすべて同一署名でハング。
  → **EU スレッドが dispatch された瞬間、命令ベース VA・kernel 内容・pipeline(3D/compute) に依らず
  無条件に同一ハング**。windower/PS 固定機能/store も無関係。

### caveat（要確認）
- C3-low の低位マッピングが実際に有効だったかは、C3-low での GPU 読み戻しを入れていないため未検証
  （fault=0 なので未マップ scratch フェッチの可能性は残るが、C0 の高位読み戻し一致と同一署名から、
  「VA 無関係」が最尤）。次イテレーションで C3-low の低位 VA からの MI_COPY 読み戻しを追加して確定可能。

### 残る仮説（有力順）
1. **EU の send メッセージ（hdc1 store / ts EOT / render RT-write）が完了しない** — 共通の shared-function
   dispatch / message gateway 経路の故障。C1 の無条件 store 未着地、C2 の EOT 未完了、PS の RT-write 未完了が
   すべて「send が完了しない」で一貫説明できる。thread は fixed-function から見て永遠に未 retire。
2. EU スレッド起動環境（kernel_context の LRC/state に、EU スレッド実行に必要な共通設定の欠落）。
3. 命令フェッチが VA 非依存で共通失敗（I-cache/L3 経路、ただし CS 読みは成功）。

### 次
- 専門家の外部対照（同一 GPU/VFIO の Linux ゲストで EU workload 成功可否）で「物理/パススルー」vs
  「自作 init/context/memory」を切り分けるのが有力。
- または C3-low 低位読み戻しで VA 説を完全排除 → send 完了経路 / LRC EU 設定へ。
default ビルド warning 0。

## p011 増分E-19 (2026-09-16): context-restore WA 監査 — indirect context batch が完全に欠落【強リード】

専門家 §4 の指示で、context-restore の WA 実装有無を監査。特に `gen12_emit_indirect_ctx_rcs()` /
Wa_18022495364（GEN12_CS_DEBUG_MODE2 に INSTRUCTION_STATE_CACHE_INVALIDATE を、context restore 用の
間接バッチで発行）の有無。

### 監査結果: 完全に欠落
- `RING_INDIRECT_CTX(0x1c4)` / `RING_INDIRECT_CTX_OFFSET(0x1c8)` / `RING_BB_PER_CTX_PTR(0x1c0)` は
  i915-regs.inc に**定義のみ存在**。
- しかし LRC（lrc.c）に **indirect context batch / per-ctx WA batch の設定が一切ない**
  （INDIRECT_CTX, BB_PER_CTX, wa_bb, CTX_INDIRECT 等すべて grep ヒット 0）。
- `CS_DEBUG_MODE2`, `Wa_18022495364`, `INSTRUCTION_STATE_CACHE_INVALIDATE`, `gen12_emit_indirect_ctx_rcs`
  はコード全体に**存在しない**。
- → **kernel_context の context restore 時に、Wa_18022495364 を含む indirect-ctx WA が一切適用されていない**。

### なぜ強リードか
Gen12 の indirect context batch は**全 context restore 時**（context のリング命令が走る前）に実行され、
Wa_18022495364 はそこで命令ステートキャッシュを無効化する。これが欠けていると:
- dispatch された EU スレッドが stale な命令ステートをフェッチ → 実行しない → 全スレッドが同一ハング。
- C0(EU スレッドなし)は成功（EU フェッチ不要）。MI/CS/post-sync も成功（EU 命令キャッシュ非依存）。
- 命令ベース VA(高位/低位)無関係（キャッシュが stale なのは VA に依らない）。
これは E-16〜E-18 の観測（「walker/primitive で EU スレッドが dispatch された瞬間、VA・kernel 内容・
pipeline に依らず無条件ハング」）を完全に説明する。

### 専門家の扱い方（重要）
「欠けていた場合も、非特権描画バッチへ LRI を一つ追加して終わりにせず、**context restore の初期化経路と
適用タイミングを参照実装に対応させる変更**として扱う」。まず Linux 外部対照で「自作 init vs 物理/パススルー」
を確定してから、この context-restore init path を実装するのが専門家の順序。

### 併せて記録すべき点（専門家 §4）
- WA は「値」でなく「対象 context・reset 後の再適用」を監査する。GEN9_CS_DEBUG_MODE1.FF_DOP_CLOCK_GATE_DISABLE,
  GEN8_ROW_CHICKEN2.GEN12_DISABLE_EARLY_READ, GEN9_ROW_CHICKEN4.GEN12_DISABLE_TDL_PUSH 等が実行時に成立しているか。
- GEN12_FF_MODE2 は CPU 読み戻しで判定しない（Wa_1608008084: 正しく読み戻せない）。発行した LRI 値と適用先 context で確認。

### 今回の作業範囲（専門家）
C3-low の全バイト読み戻し（低位 VA 0x800400 から）→ Linux ゲストで OpenCL 陽性対照（eu_positive_control.py,
enable_guc=0）→ context-restore WA の実装有無・適用時点の記録（本項で欠落を確認）。

## p011 増分E-20 (2026-09-16): C3-low 低位読み戻し = 有効。VA 説を確定的に棄却
C3-low-rb(walker なし, 低位 inst_base=0x800000): IDD 一致、低位 VA 0x800400 から C2 empty カーネルを
CS が MI_COPY で正しく読める(8 words 完全一致、以降ゼロ)、seqno 完走。C3-low(walker あり)は同一署名で
ハング。→ 低位マッピングは valid。専門家判定「全バイト一致し停止 → bit32 単独原因説を外す」。
**命令ベース VA(高位/低位)は EU スレッドハングと無関係と確定**。残る最有力は E-19 の context-restore
WA 欠落(Wa_18022495364 / indirect context batch 皆無)。

## p011 増分E-21 (2026-09-16): indirect-context 経路を実装・動作。Wa_18022495364 単独では未解消

専門家の詳細仕様で、欠落していた indirect-context restore 経路を実装し、Wa_18022495364 の A/B 比較を実施。

### 実装（専門家仕様どおり）
- GGTT 上に 64byte(16DW) indirect batch（enter marker MI_STORE_DWORD_IMM|GGTT → NOOP → Wa LRI →
  leave marker → padding、BB_END なし）。`drv_i915_gem_bind_ggtt` で wa_ggtt 取得。
- kernel_context の LRC: state[0x13]=0(BB_PER_CTX)、**state[0x15]=wa_ggtt|1(RING_INDIRECT_CTX)**、
  **state[0x17]=0x340(RING_INDIRECT_CTX_OFFSET=0xD<<6)**。offsets table が 0x1c0/0x1c4/0x1c8 スロットを
  持つので値を書くだけ。wbinvd で image を memory へ。
- Wa LRI: `0x11000001, 0x000020d8(GEN12_CS_DEBUG_MODE2), 0x00400040(masked INSTRUCTION_STATE_CACHE_INVALIDATE)`。
- 提出ごと force-restore（既存 descriptor bit2）、inhibit は先行 selftest で解除済み。

### 結果
```
wa_ggtt=0x00132000  regs[0x12]=0x21c0 [0x14]=0x21c4 [0x16]=0x21c8  ctx_ctrl=0xffff0008 desc_low=0x00101119
B(WA) C0     enter=0xe07e0000 leave=0x1ea7e000 ready=✓ done=✓ completed=3 seqno=3   （完走）
B(WA) C3-low enter=0xe07e0000 leave=0x1ea7e000 ready=✓ eu=dead done=dead completed=3 seqno=4
  HANG: ipehr=0x70040000 row=0x8610e87f （PS/C1/C2 と同一）
```

### 判定（専門家の表に厳密に沿う）
- **enter/leave marker が両方着地** → **indirect-context restore 経路が実際に実行された**（欠落機構の
  実装成功。RING_INDIRECT_CTX 配線・offset 0x340・GGTT batch すべて正しく動作）。Wa LRI は enter/leave の
  間で実行済み（faultなら leave 未着地のはず）。
- **WA ありでも C3-low は同一署名でハング** → 専門家判定「前後 marker が両方出るが WA ありでも C2 停止 →
  **この WA 単独では解消しない → Linux 外部対照へ進む**」。
- （pass A=no-WA は B の C3-low ハングでエンジン wedge のため未実行。ただし A は indirect 経路のみで
  WA なし＝従来の broken 相当なので、比較は「両者ハング」で確定。）

### 得られたこと
- **欠落していた indirect-context restore 経路を実装し、実機で動作確認**（enter/leave marker）。
  他の restore-time WA を今後追加可能な基盤ができた。
- ただし **Wa_18022495364（命令ステートキャッシュ無効化）単独では EU スレッドハングは解消しない**。
  E-19 の「この WA 欠落が単独原因」という仮説は否定側。

### 次（専門家指定）
Linux 外部対照（eu_positive_control.py, 同一 8086:46a8 を VFIO パススルー、enable_guc=0）で
「自作 init/context/memory」vs「物理/パススルー」を確定。Linux で EU が動けば自作側差分に絞れる。
専門家いわく「別の bit を次々追加せず Linux 対照へ」。default ビルド warning 0。

## p011 増分E-22 (2026-09-16): Linux 外部対照 — IGD パススルーで i915 が bind せず（準備段階の壁）

専門家提供の eu_positive_control.py で Linux 外部対照を試行。テストホスト(Debian 13)上に Ubuntu 24.04
ゲストを構築し、同一 8086:46a8 を VFIO パススルー。

### 成功した部分
- Ubuntu 24.04 cloud image + cloud-init で自動化: ゲスト起動、intel-opencl-icd/clinfo/python3-pyopencl/
  numpy を apt 導入、eu_positive_control.py を自動実行。インフラは動作。
- ゲストは GPU を検出: `pci 0000:00:02.0: [8086:46a8] type 00 class 0x030000 PCIe Root Complex Integrated Endpoint`。

### ブロッカー: i915 が passthrough IGD に bind しない
- テスト結果: `FAIL: FileNotFoundError: '/sys/devices/pci0000:00/0000:00:02.0/driver'`
  → デバイスに driver シンボリックリンクなし = **i915 が bind していない**。
- ゲスト dmesg に **i915 メッセージ皆無**（drm_connector と modprobe@drm のみ）。手動 modprobe i915 でも bind せず。
- QEMU 起動時: `vfio_container_dma_map(..., 0x380000000000, 0x108000, ...) = -22(EINVAL)` +
  `PCI peer-to-peer transactions on BARs are not supported`。
- `x-igd-opregion=on` を付けても変化なし。

### 解釈（専門家判定表に沿う）
- 専門家「GPU 未列挙 / i915 未バインド / build 失敗 → 外部対照の準備段階。物理故障や共通 send 障害の
  証拠にしない」に該当。→ **EU の陰性結果ではない**。まだ Linux で EU が動くか未確定。
- 原因: **Intel IGD パススルーは Linux i915 に OpRegion/VBT/stolen memory(DSM)/GTT の適切な公開が必要**で、
  標準の `vfio-pci` + `x-igd-opregion=on` だけでは不足。IGD パススルーは legacy mode / x-igd-gms /
  host 側 IOMMU・BIOS 設定など finicky な条件を要する既知の難所。zedBSD ドライバはこれら BIOS/OpRegion
  依存を回避して直接 HW を叩くため動く（＝両者の初期化前提が根本的に異なる）。

### 次の選択肢
1. IGD パススルー設定を詰める（x-igd-gms でメモリ量、x-igd-legacy-mode、host kernel params、
   IGD を primary display にする legacy assignment 等）— 深掘りが必要な領域。
2. 専門家に「Linux i915 を ADL-P IGD passthrough で bind させる最小構成」を照会。
3. E-21 の成果（indirect-context 経路の実装・動作）を土台に、他の restore-time 差分を Linux ソース
   照合で探す方向（Linux 実機を経由せずコード比較）。

### 環境
テストホストの GPU は vfio-pci に維持（zedBSD テスト継続可）。Ubuntu ゲスト一式は /home/awe/linuxvm に保存。
default ビルド warning 0。この回の主成果は E-21（indirect-context 経路実装+動作、Wa_18022495364 単独では
EU ハング未解消）。

## p011 増分E-23 (2026-09-16): ★Linux 外部対照が陽性 — 問題は zedBSD の init/context に確定

専門家提供の eu_positive_control.py を Linux ゲストで実行し、**決定的な陽性対照が取れた**。

### 結果（同一 GPU・同一 VFIO・enable_guc=0=execlists）
```
DIAG param enable_guc=0
[drm] Initialized i915 1.6.0 for 0000:00:02.0   （wedge なし）
GPU candidate: Intel(R) Iris(R) Xe Graphics, vendor_id=0x8086
Device: Intel(R) Iris(R) Xe Graphics; OpenCL 3.0 NEO (intel-opencl-icd 23.43)
PASS run=1: marker=0xc0ffee02, canary intact
PASS run=2: marker=0xc0ffee03, canary intact
PASS run=3: marker=0xc0ffee04, canary intact
probe_exit=0
GPU reset/hang/wedged: なし（クリーン）
```
kernel 6.8.0-139-generic。**同一物理 GPU(8086:46a8)・同一 VFIO パススルー・enable_guc=0（execlists、
zedBSD と同じ提出モデル）で、Linux i915+OpenCL が EU カーネルを実行しメモリに書き込み正常終了・reset なし。**

### 到達に必要だった設定（IGD passthrough + Linux 固有）
1. **`-cpu host,host-phys-bits-limit=39`**: host IOMMU MGAW=39bit（CPU phys=46bit）。ゲスト物理幅を 39 に
   制限して IGD の高位 IOVA(0x380000000000)マップ失敗(vfio_container_dma_map=-22)を解消。QEMU IGD 文書の既知対策。
2. **`linux-modules-extra-$(uname -r)`**: Ubuntu 最小 cloud image は i915 を含まない（modules-extra に分離）。
   導入後 i915 が bind（/dev/dri/card0, renderD128 作成）。
3. **`enable_guc=0`（modprobe.d）**: ADL-P i915 は既定で GuC 必須だが firmware(adlp_guc_70.bin)欠如で
   **GPU wedged**。enable_guc=0 で execlists にすると GuC 不要で init 成功。zedBSD の execlists と同条件。
4. QEMU: q35, x-igd-opregion=on, rombar=0（legacy mode は不使用）。

### 結論（調査の確定的分岐）
専門家判定表「Intel GPU で tag 一致・正常終了・reset なし → **自作側の init/context/memory・命令公開経路の
差分を優先**」に該当。→ **EU はこの GPU/VFIO で Linux(execlists) では動く。問題は zedBSD ドライバの
init/context/memory 設定に確定**。物理故障・VFIO パススルー・GPU 不能・GuC はすべて否定。
zedBSD も execlists（GuC 不使用）なので、**Linux execlists の init/context と zedBSD の差分**が原因。

### 次段
- Linux execlists 経路（enable_guc=0）の GT/context/LRC 初期化 vs zedBSD の初期化を照合し、
  EU スレッド実行に必要な設定の欠落を特定。
- 専門家の deferred 案「現在の C2 バイナリを Linux 側の GEM/context で実行」も有力（EU 実行環境を
  Linux で再現して zedBSD state と比較）。
- indirect-context 経路(E-21)は実装済みだが Wa 単独では不足 → 他の GT/context init 差分へ。

環境: Ubuntu ゲスト一式 /home/awe/linuxvm 保存（再現可能）。GPU は vfio-pci 維持。zedBSD テスト継続可。

## p011 増分E-24 (2026-09-16): ソース監査 A 初見 + linux-c2-replay 計画

Linux 陽性(E-23)確定後、専門家プラン: 主作業=**linux-c2-replay**（C2 バイナリ+state を Linux i915 の
直接 ioctl で実行）、並行ソース監査は 3 範囲限定（A: __engines_record_defaults/intel_engine_emit_ctx_wa、
B: intel_gt_init/init_hw+forcewake、C: execlists_resume/submission_setup）。

### ソース監査 A の初見（zedBSD の WA 適用機構）
zedBSD は WA を2経路で適用:
1. `i915_draw_apply_engine_workarounds()` = **MMIO 直書き**（GEN7_MISCCPCTL, GEN8_ROW_CHICKEN2,
   GEN9_ROW_CHICKEN4, GEN10_SAMPLER_MODE, GEN9_CS_DEBUG_MODE1, GEN7_FF_THREAD_MODE, RING_PSMI_CTL,
   GEN7_FF_SLICE_CS_CHICKEN1, RING_CMD_CCTL）。context 非保存。
2. per-request の ring LRI（`context_registers[]`）: **compute selftest は L3ALLOC + FF_MODE2 のみ**。
   （draw selftest は加えて COMMON_SLICE_CHICKEN3, CS_CHICKEN1, HIZ_CHICKEN, COMMON_SLICE_CHICKEN4,
   COMMON_SLICE_CHICKEN1 も。だが draw も同様にハング → WA 数だけが差ではない。）

**重要な構造差**: zedBSD は Linux の `__engines_record_defaults()` 相当（restore-inhibit context で
ctx WA を LRI 適用 → 別 context へ切替えて **golden default_state を保存** → 以後の context が継承）を
持たない。ctx WA は per-request の ring で適用しており、golden image には焼かれていない。
専門家の §4A の区別: ① context 初期化時に適用し image 保存する WA（zedBSD は golden 保存機構なし）vs
② indirect-context restore WA（E-21 で実装・確認済み）。①の機構が欠落している可能性。

### linux-c2-replay 計画（専門家仕様、次フェーズの主作業）
Linux i915 の直接 ioctl（Level Zero でなく）で、zedBSD の C2 コード+state を Linux の context/PPGTT/
execlists 提出に載せる。Linux に任せる=GT/engine init, LRC 作成保存復元, PPGTT, execlists 提出, 完了管理。
持ち込む=C2 全命令バイト, program data, SBA, VFE, IDD, walker, batch 内 marker。
- DRM render node open → GEM context (I915_CONTEXT_PARAM_ENGINES に {RENDER,0} のみ, selector=0) →
  GEM object 確保 + softpin(EXEC_OBJECT_PINNED, 4GB 超は SUPPORTS_48B, GPU 書込は WRITE) →
  GEM_PWRITE でコード/IDD/batch/marker → EXECBUFFER2 → GEM_WAIT(timeout) → GEM_PREAD で marker 確認。
- 試験順: **L-MI → L-C0 → L-C2 →（成功時）L-C1**。C2 期待値=markerReady/markerDone 更新+正常完了+reset なし。
- ioctl 拒否(EINVAL 等)と受理 batch の GPU 停止を分けて記録。
判定: L-C2 成功→C2+state は Linux 管理下で完走＝zedBSD 基盤側監査を優先。L-C2 停止→C2 投入列/参照データ/
移植差分を調べる（init/context だけに限定しない）。

### 今回の状態
Linux 陽性環境は保存(/home/awe/linuxvm, boot-linux.sh)。zedBSD の C0/C2/C1/indirect-ctx 実装維持。
次フェーズ=linux-c2-replay ハーネス構築（大きめ）+ ソース監査 A（golden context 記録機構の差分）。
専門家: ベアメタル/SIP/PS 追加/別 chicken bit は保留。default ビルド warning 0。

## p011 増分E-25 (2026-09-16): ★★linux-c2-replay 全 PASS — C2/C1 の state は正しい、問題は zedBSD 基盤に確定

専門家の主作業 linux-c2-replay を実装・実行。zedBSD の compute バッチ+kernel+IDD+state を、Linux i915 の
直接 ioctl（GEM_MMAP_OFFSET+mmap, softpin EXEC_OBJECT_PINNED, EXECBUFFER2 に I915_CONTEXT_PARAM_ENGINES
{RENDER,0}, GEM_WAIT）で、**zedBSD と同一 GPU VA（shared@0x100400000, batch@0x100600000）に softpin して
同一バイトで実行**。

### 結果（Linux i915, enable_guc=0=execlists, 同一 VFIO）
```
L-MI: waited=1 ready=0xc0ffee10 done=0xc0ffee20                          （MI/softpin/PPGTT 動作）
L-C0: waited=1 ready=✓ done=✓ cs=✓                                       （compute 初期化列 完走）
L-C2: waited=1 ready=✓ done=✓ cs=✓  eu=dead(store-less で正常)            （walker 完走・ハングなし）
L-C1: waited=1 ready=✓ done=✓ cs=✓  eu=0xc0ffee02                        （★EU が A64 store を実行・書込）
GPU hang/reset/wedged: なし
```

### 結論（決定的）
- **zedBSD でハングする同一の C2（store-less）/C1（A64 store）バッチ+kernel+IDD+VFE+walker+state が、
  Linux i915 では完走**。L-C1 では **EU スレッドが実際に実行しメモリに 0xc0ffee02 を書き込んだ**。
- 実装のポイント: 現代 i915 は GEM_PWRITE 非対応 → GEM_MMAP_OFFSET(WB)+mmap でアクセス。それ以外は
  zedBSD の emit をそのまま移植（PIPELINE_SELECT/SBA/VFE/MEDIA_STATE_FLUSH/MIDL/GPGPU_WALKER/post-sync）。
  VA は zedBSD と同一に softpin したので**バッチバイトは同一**（移植による DWORD 変更なし。max_threads=559 も同一）。
- → 専門家判定「L-C1 も成功 → EU 書込までの強い対照成立 → Linux と zedBSD の基盤差分を主対象に」。
  **C2/C1 のコード・投入 state・walker/VFE/IDD 設定はすべて正しい（Linux で EU が動く）。問題は zedBSD の
  GT/engine/context/LRC 初期化に決定的に確定**。windower/sample-mask/store/VA/batch content はすべて除外。

### これで確定した切り分け
| 層 | 判定 |
|---|---|
| C2/C1 のバッチ・kernel・IDD・VFE・walker・state | **正しい**（Linux で完走・EU 書込） |
| 物理 GPU・VFIO パススルー・GPU 能力 | 健全（Linux で EU 動作、E-23/E-25） |
| zedBSD の submission/PPGTT/SBA/MI/post-sync | 健全（C0 成功） |
| **zedBSD の GT/engine/context/LRC 初期化** | **← 原因はここ** |

### 次段（専門家の 3 監査範囲、zedBSD 側改修は replay 確定後＝今）
A. __engines_record_defaults / intel_engine_emit_ctx_wa（golden context に ctx WA を焼く機構）— 最優先。
   zedBSD は golden default_state 記録機構を持たない（E-24）。
B. intel_gt_init / intel_gt_init_hw（forcewake, GT WA, PPGTT, MOCS）。
C. intel_execlists_submission_setup / execlists_resume（MOCS 再初期化等）。
Linux が execlists 経路で EU workload 実行前に必ず通す処理のうち、zedBSD に欠落しているものを特定。

インフラ: Linux dev VM（/home/awe/linuxvm, boot-dev.sh, SSH:2222, libdrm-dev, c2replay）— 今後 zedBSD state を
Linux で比較検証できる強力な対照環境。GPU は vfio-pci 維持。default ビルド warning 0。

## p011 増分E-26 (2026-09-16): zedBSD 基盤監査 第1ラウンド — 複数を除外、PAT index3 修正（THE fix でない）

E-25 で「問題は zedBSD の GT/engine/context 初期化」に確定後、専門家の 3 監査範囲を調査。

### 除外できた候補
1. **forcewake**: `drv_i915_write32` は auto-forcewake しないが、`drv_i915_engines_start` が
   **I915_FORCEWAKE_ALL をデバイス寿命の間 常時保持**（device->forcewake_held=1、"taken for the life of
   the device"）。→ 0xe000 系 WA 書込は着地。forcewake は原因でない。
2. **ring prologue（gen12_emit_flush_rcs 相当）**: request.c の i915_request_emit_prologue は
   `PREPARSER_DISABLE + PIPE_CONTROL(I915_RCS_INVALIDATE_FLAGS) + PREPARSER_ENABLE`。invalidate フラグは
   COMMAND_CACHE/TLB/**INSTRUCTION_CACHE**/TEXTURE/VF/CONST/STATE_CACHE を含み包括的。C0 も通るので健全。
   （Linux は flush+invalidate の2段だが、zedBSD は prologue invalidate + extra flush + breadcrumb flush で
   実質同等。）
3. **ctx WA レジスタ内容**: apply_engine_workarounds が ROW_CHICKEN2/4, SAMPLER_MODE, CS_DEBUG_MODE1,
   FF_THREAD_MODE, PSMI_CTL, FF_SLICE_CS_CHICKEN1, CMD_CCTL を MMIO 直書き。per-request ring で
   L3ALLOC, FF_MODE2（compute）。**draw は全 ctx WA(COMMON_SLICE_CHICKEN3, CS_CHICKEN1, HIZ_CHICKEN,
   COMMON_SLICE_CHICKEN4 等)を適用してもハング**したので、ctx WA 内容は差ではない。

### PAT の発見と修正（正しいが THE fix でない）
zedBSD は **Gen12 PAT テーブル(0x4800..0x481c)を programming していなかった**（Linux tgl_setup_private_ppat 相当が欠如）。
- 実機デフォルト: `0x4800=3(WB)`, `0x480c=3(WB)`。→ **index 0(object/instruction ページ)は元から WB で正しい**。
  index 3(scratch/page-table、UC であるべき)が WB で誤り。
- Linux 値 {WB,WC,WT,UC,WB,WB,WB,WB} を engines_start(forcewake 保持中)で programming。0x480c を UC に修正。
- **それでも C3-low(=C2 walker) はハング**（同一署名 row=0x8610e87f）。→ PAT/caching は EU ハングの原因でない
  （object ページ index 0 は元から WB だったため）。index 3 の修正は Linux 一致の正しい修正なので保持。

### 現状
除外: windower/sample-mask/store/VA/batch content(E-25 で確定) + forcewake/ring prologue/ctx WA 内容/PAT。
C2/C1 の state は Linux で完走（E-25）。問題は zedBSD 基盤のどこか一点だが、コード監査では未特定。

### 次段の提案
**動作する Linux(c2replay 成功環境)と zedBSD の GPU レジスタ状態を直接 diff** するのが決定的。
igt-gpu-tools の intel_reg か、/sys の GPU MMIO BAR mmap で、EU/thread-dispatch/GT/context 関連レジスタを
両側でダンプして差分を取る。これで「Linux が設定し zedBSD が設定していないレジスタ」を実験的に特定できる。

## p011 増分E-27 (2026-09-16): レジスタ diff — GLOBAL は全一致、差は CONTEXT/LRC に局在

専門家 §1-2 の指示で、動作 Linux(c2replay 成功)と zedBSD の実効レジスタ値を diff。

### 手法
- Linux: ゲストに intel-gpu-tools 導入、`intel_reg read`（forcewake+MCR steering を適切に処理）で採取。
  （mmap /sys resource0 は forcewake なしで全 0 になり不可。intel_reg が正解。）
- zedBSD: compute selftest の baseline に regdump probe を追加、同一アドレスを MCR multicast で読む。

### 比較結果（zedBSD vs Linux, C2 投入前）
| Reg | zedBSD | Linux | 一致 |
|---|---|---|---|
| CMD_BUF_CCTL 0x2084 | 0x100 | 0x100 | ✓ |
| CMD_CCTL 0x20c4 | 0x306 | 0x306 | ✓ |
| GLOBAL_MOCS2/3 0x4008/0x400c | 0x37/0x05 | 0x37/0x05 | ✓ |
| LNCFCMOCS1 0xb024 | 0x00100030 | 0x00100030 | ✓ |
| MI_MODE 0x209c | 0x200 | 0x200 | ✓ |
| CS_CHICKEN1 0x2580 | 0x01 | 0x01 | ✓ |
| GFX_MODE 0x229c | 0x08 | 0x08 | ✓ |
| MISCCPCTL 0x9424 | 0xfffffffe | 0xfffffffe | ✓ |
| DFR_CHICKEN 0x9550 | 0x3ff | 0x3ff | ✓ |
| L3ALLOC 0xb134 | 0xd0000020 | 0xd0000020 | ✓ |
| L3SQCREG1 0xb100 | 0xb3400000 | 0xb3400000 | ✓ |
| L3SQCREG4 0xb118 | 0x40 | 0x40 | ✓ |
| ROW_CHICKEN2 0xe4f4 | 0xffff4100 | 0xffff4100 | ✓ |
| ROW_CHICKEN4 0xe48c | 0xffff0200 | 0xffff0200 | ✓ |
| **SAMPLER_MODE 0xe18c** | **0xb021** | **0x3020** | **✗** |
| CONTEXT_CONTROL 0x2244 | 0x08 | 0x00 | (context 依存の global read、無効) |

### SAMPLER_MODE の差を検証 — 原因でない
zedBSD は bit0(INDIRECT_STATE_BASE_ADDR_OVERRIDE)+bit15(ENABLE_SMALLPL)を追加、Linux は default(0x3020)。
→ zedBSD の SAMPLER_MODE 書込を default(0x3020, Linux 一致)に変更して C2 テスト → **regdump で 0x3020 を確認
したが C3-low は依然ハング**（同一署名）。→ SAMPLER_MODE の差は EU ハングの原因でない。revert（元の WA 保持、
Linux は per-context で適用している可能性があるため）。

### 帰結
**比較した GLOBAL GT/engine レジスタは全て一致**（MOCS/CMD_*/L3/ROW_CHICKEN/DFR/MISCCPCTL 等）。
SAMPLER_MODE の差も原因でない。→ **差は GLOBAL 状態ではなく CONTEXT/LRC 状態に局在**。
唯一 CONTEXT_CONTROL(0x2244) が zedBSD=0x08 vs Linux=0x00 だが、これは現在ロード context の値を global read
したもので context 依存。context 実効状態(CONTEXT_CONTROL, RPCS, ctx WA)は SRM か LRC 比較で採取が必要。

### 次段（専門家 §4 の LRC/context 比較）
- C0 後に switch-out した context の LRC を採取（context A→B→A 手順）、Linux と構造(LRI layout)＋値(mask 付き)を比較。
- または context 実効レジスタ(CONTEXT_CONTROL, RPCS, MI_MODE, CS_CHICKEN1)を対象 context の ring から SRM。
- 特に RPCS(EU/subslice enable)が候補。zedBSD=0x80041000、Linux 値との比較が必要。
インフラ: Linux dev VM(intel_reg 導入済み), c2replay, regdump probe。GPU vfio-pci。default ビルド warning 0。

## p011 増分E-29 (2026-09-16): Stage 1 = Gen12 EMIT_INVALIDATE prologue(AUX 込み)実装。C2 は依然停止
専門家の context ライフサイクル実装 Stage 1: request prologue を Linux gen12_emit_flush_rcs 準拠の
EMIT_INVALIDATE(前段 flush DW1=0x103070a1 + PREPARSER_DISABLE + invalidate PC 0x20344c1c + CCS AUX inv
LRI(0x11020001,0x4208,1) + MI_SEMAPHORE_WAIT register poll(0x0e01c003) + PREPARSER_ENABLE, 22 DW)に置換。
専門家提供の正確な hex 使用。ring 予約を 28U に拡張。従来は invalidate-only(前段 flush と AUX 欠如)だった。
結果: **C0 完走(新 prologue の AUX wait も完了=prologue は健全)、C3-low(=C2 walker)は依然同一署名ハング**。
→ prologue の AUX 追加単独では C2 未解消(正しい修正なので保持)。専門家の言う通り単一ピースでなく
golden-context lifecycle 一式が必要。次=Stage 2/3(bootstrap A で ctx WA を 22-DW EMIT_BARRIER 前後で適用→
別 context B で保存→C へ継承[全体 memset せず 0xe000 コピー+ppHWSP クリア, restore-inhibit 解除]→C0/C2/C1)。
prologue 修正保持(/tmp/request.c.bak-prologue)。default ビルド warning 0。

## p011 増分E-30 (2026-09-16): golden-context 実装・検証完了 → walker ハングは LRC 非依存と確定(仮説反証)
専門家の golden-context lifecycle(A bootstrap→B save→C inherit/restore)を selftest 内に自己完結実装
(i915_golden_run helper + ctxA/B/C 標準 context を kernel_vm 共有で生成)。
**保存機構は本物であることを実測検証**:
- A 初期 ctrl=0x00090009(inhibit) → B へ切替後 A.image 再読込で **ctrl=0xffff0008**(HW が restore-inhibit を
  初回ロード後に自動クリア=Linux 挙動)、LRI layout 健全(s1=0x11081019 s2=0x00002244[=0x2244 offset] s3=ctrl)。
- C は A の saved image[0x1000..0xe000] を継承コピー、ppHWSP クリア、ring/PDP/ctrl=0x00090008(restore) に再所有。
- **gC0(marker, walker 無)= golden C 上で完走**(ready=0xc0ffee10 done=0xc0ffee20)→ C は正しく restore 実行。
**しかし walker は依然ハング**:
- gC2 = **marker+walker@shared->va = Linux で完走実証済み L-C1 と同一ケース** → golden C 上で **HANG**
  (ready=0xc0ffee10[CS 側 store は発火] だが eu=0xdead0000[EU 未実行] done=0xdead0000[post-sync 未発火]、
   同一署名 ipehr=0x70040000 acthd=batch+0x150 instdone=0xffdeffff row=0x8610e87f)。
- gC3-low(empty+walker@low) も同様 HANG。
### 帰結(重要)
**本物の HW-saved+inherited+restored context でも walker は同一ハング** → 停止原因は golden-context state の
欠如では **ない**。LRC per-context image は差別化要因でない(golden 仮説を反証)。
これに E-23/E-25(同一 batch/kernel/IDD/VFE/walker/state が同一 HW/VFIO 上の Linux i915 で完走、L-C1 の EU が
0xc0ffee02 を書込)を併せると、**差は「未比較の global/GT 状態 or setup 手順」に局在**。
EU 電源は生存(baseline: eu_dis 0x9134=0, slice_ack 0x804c=0x3, eu_ack 0x805c/0x8060=0x3, dss=0x1f, 559 threads)。
停止は「dispatch 済み EU thread が実行/drain されず CS が post-walker sync で無限待機」。
次: golden 経路は保持(正当な移植)、専門家へ反証結果を報告し未比較状態(L3 SLM/scratch/thread-dispatch/
render power well/barrier-threadgroup)の方向付けを依頼。build warning 0。selftest.c.bak-golden 保持。

## p011 増分E-31 (2026-09-16): 方針転換 = Linux-parity 全経路移植。参照固定 + 実装台帳作成
専門家の大転換: 候補試験を止め、動作 Linux(6.8.0-139/enable_guc=0)の実行経路を PCI→device init→memory→
context→submit/complete/recovery まで一式移植し、**実装台帳(porting ledger)**で管理。golden/PAT/AUX/WA は
selftest でなく通常 driver 経路へ統合。
### 直近着手#1 完了分
- 参照環境を凍結・記録: `plan/ws031/linux-parity/linux-reference/manifest.txt`(kernel 6.8.0-139.139=6.8.12 base,
  gcc-13, enable_guc=0, adlp_dmc.bin v2.20+PXP, host-phys-bits=39, QEMU/VFIO 構成, fixtures hash)。
- 参照 source 保存: 同 `linux-reference/i915-src`(= v6.8.12 drivers/gpu/drm/i915, 386 c files, 13M)+uapi-drm+
  fixtures(linux-c2-replay.c sha256 d8cc6a8e.., qemu-boot-dev.sh)。Ubuntu delta 照合は台帳 L0.2。
- 実装台帳作成: `plan/ws031/linux-parity/ledger.md`。Linux i915_driver_probe→…→__engines_record_defaults の
  実 call tree を source から抽出し zedBSD 現状(i915_start: dma→bar→uncore→gt-reset→ggtt→busmaster→irq→
  engines)へ対応付け、P0..P11 + 適合層で状態(UNIMPLEMENTED/PORTED/VERIFIED/NOT_TAKEN)管理。
### source 接地で判明した構造的欠落 2 点(golden の核)
- **P6.4 intel_clock_gating_init**: i915_gem_init 内 intel_gt_init の**前**に呼ばれ、source 明記で「display
  だけでなく default-state 記録前に必要な context 設定を含む」。zedBSD 未実装。
- **P8.3 intel_renderstate_emit**: __engines_record_defaults が bootstrap context に emit_ctx_wa の**後に
  platform の null render-state batch**を流し 3D/compute pipeline 既定を image に焼く。E-30 の selftest golden は
  これを欠く(ctx WA 2 本のみ)。→ golden が walker を直さなかった一因の可能性(ただし単独賭けはせず経路移植で対処)。
### 直近着手 残り
台帳 L0.3/L0.4(適合層定義 + 最上位 attach 順を i915_driver_probe 構造へ置換)→ P0..P11 子関数移植 →
PAT/AUX/WA/golden を通常経路へ統合。受入試験= driver-ready 後に通常入口から MI→init→eu-empty→eu-store→PS。
第22報で台帳提出。build 影響なし(ドキュメント/参照のみ)。

## p011 増分E-32 (2026-09-16): M0 完了 — 参照正本確定・台帳訂正(専門家の読み違い指摘を反映)
専門家の詳細訂正を受け M0(参照固定+台帳整備)完了。**新しい GPU 実行結果でなく参照資料と移植計画の整備が成果**。
### 正本 source 確定 (L0.2/L0.3)
- guest(GPU 無しで起動 boot-nogpu.sh)で `apt-get source linux=6.8.0-139.139` 取得。source:Package=linux
  source:Version=6.8.0-139.139, version_signature=6.8.12 base, i915 srcversion F4AF3762..。dsc/orig/diff hash 記録。
- **port-scope 全ファイルで ADL-P 経路は upstream v6.8.12 と同一**を diff で確認(i915_driver/i915_gem/intel_gt/
  intel_gt_pm/intel_renderstate/intel_clock_gating/intel_wopcm/intel_pcode = SAME)。差分 2 件(intel_workarounds:
  Wa_14019877138 は xelpg_ctx_workarounds_init=Meteor Lake, intel_mocs: IP range 12,70-74=Xe-LPG)は**非 ADL-P**。
  → 作業基準 v6.8.12 は正当。ubu-i915-src を正本として repo 保存。
- config 保存: DEBUG_GEM=未設定(→P7.11 verify_workarounds は return 0=NOT_TAKEN 確定), PXP=y, GVT=y, WERROR=未設定。
- param 実値: enable_guc=0(/etc/modprobe.d/i915-euctl.conf), enable_dc=-1。
### 台帳の読み違い訂正 (source で全て確認)
- **C1 撤回**: intel_renderstate_emit は Gen12 で batch 発行なし(render_state_get_rodata が ver6-9 のみ→NULL→
  so->vma==NULL→return 0)。呼び出し+pin/unpin+分岐を移植、Gen9 流用/新 batch 創作しない。
- **C2 撤回**: intel_clock_gating_init は ADL-P で nop(hook に ALDERLAKE 分岐なし→nop_clock_gating_funcs)。
  位置/選択を移植、HW 設定を足さないのが一致。→ 「golden 欠落核 2 点」先行実装案は取り下げ。
- 行訂正確認: P0.5 vgpu_detect(BAR map+magic 確認, 非 vGPU 経路再現; vGPU 限定でない), P2.9 pcode_init
  (!IS_DGFX→return 0), P6.2 wopcm(guc_size 0 で早期 return), P7.5b set_mocs_index(SW index のみ, mocs_init と別),
  P7.7 uC(init_hw=__uc_check_hw 残存)。追加漏れ: setup_private_pat(gem_init loop, init_ggtt 前=既存 PAT の正位置),
  kernel_vm(gt_init pm_init 後), sanitize_gpu(mmio_probe 末尾, gt_init_mmio と別)。
### 台帳 rev2 構造
「入口が呼ばれる/内部分岐/実際の効果」を分離し各行に根拠(SOURCE/CONFIG/RUNTIME)。作業単位 M0-M5 定義。
成果物: plan/ws031/linux-parity/{ledger.md rev2, linux-reference/{manifest.txt rev2, ubu-i915-src(正本),
i915-src(比較用 v6.8.12), config, ubuntu-source-hashes, fixtures}}。次=M1(適合層)→M2(P0-P2)。git 操作なし。

## p011 増分E-33 (2026-09-16): M1 適合層 — DMA + MMIO/forcewake レイヤ実装、GPU 無し契約試験 全 pass
専門家の固定順(M1-A 型/返却/所有権/計測 → M1-B 自動試験 → M2)に従い、適合層を portable-core + backend-vtable +
mock の形で実装。src/drivers/gpu/i915/parity/{osdep,tests}。zedBSD kernel build には未 wire(M2)、mock で GPU 無し試験。
### アドレス 3 分離 (専門家最重要点)
判明: 現 gem.c は kern_pmem_alloc の run.paddr(CPU/guest-phys)を drv_i915_ggtt_insert/ppgtt_insert に**直渡し**。
現 QEMU/VFIO は guest vIOMMU 無し→dma==guest-phys(偶然一致)で動くだけ。address_types.h で
osdep_cpu_phys_t / osdep_dma_addr_t / osdep_gpu_vaddr_t を別 struct 型化し、GPU PTE には dma_addr_t のみ到達可能に。
### 実装レイヤ (各 VERIFIED=mock 契約試験のみ, HW VERIFIED ではない)
- **DMA** (dma.c): set_info(0/-errno)/map_sg(count/0=dma_map_sg 契約)/map_sgtable(0/-errno)/map_page(sentinel)を
  API ごと別契約で保持。所有権=mapping に resource_id+pin。pin 中 unmap は -EBUSY(寿命安全)。sg は orig_nents(unmap 用)と
  nents(coalesce 後)を分離。**DMA-1..6 + set_info + trace overflow = 36 checks 全 pass**。
- **MMIO/forcewake** (mmio.c): 通常 access は domain 保持を要求 / raw access は init 用 / posting read / masked RMW /
  forcewake refcount+ACK(0->1 で wake+ACK 待ち, 1->0 で sleep, nested get, ACK timeout で ref 巻戻し, over-put 検出) /
  MCR steering 排他 lock。**FW-1..3 + MMIO-1..3 + MCR + balance = 35 checks 全 pass**。
  → forcewake-ALL 常時保持の是正基盤。是正自体は P7.8 gt_resume と同一単位で実施(解放だけ先行しない)。
### 計測 (trace.c)
固定長 ring に entry/exit/acquire/release/map/unmap/sync/worker/unimpl/fail を記録。overflow を dropped で計上
(gap≠未実行)。静的呼び出し確認と実機通過を別記録。未実装 backend op は UNIMPL 記録+失敗(HW 非対応と偽らない)。
### IOMMU backend 情報
nogpu guest は GPU 非搭載(00:02.0=virtio)のため GPU の iommu_group は GPU-passthrough guest で M2 時に採取予定。
QEMU 構成上 guest vIOMMU 無し→i915 DMA は direct(dma==guest-phys)が確定事実。
残り M1: PCI 契約層 + sync/workqueue 契約層(+試験)。次 M2: P0→P1→P2 wiring。build 影響なし(standalone test)。GPU vfio-pci 維持。

## p011 増分E-34 (2026-09-16): M1 適合層 4 層すべて実装 + GPU 無し契約試験 118 checks 全 pass
専門家の固定順 M1-A(型/返却/所有権/計測)+M1-B(自動試験)を完了。src/drivers/gpu/i915/parity/{osdep,tests}(1426 LOC)。
portable-core + backend-vtable + mock。kernel build 未 wire(M2)。全て -Wall -Wextra -Werror clean。
- **DMA** (dma.c): アドレス 3 分離(cpu_phys/dma_addr/gpu_vaddr の別 struct 型, GPU PTE には dma_addr のみ到達)、
  API 別返却契約(map_sg=count/0, map_sgtable=0/-errno, map_page=sentinel, set_info=0/-errno)、
  所有権(pin 中 unmap=-EBUSY)、sg の orig_nents/nents 分離。**36 checks**。
- **MMIO/forcewake** (mmio.c): 通常/raw access 分離、forcewake refcount+ACK(0->1 wake, 1->0 sleep, nested, timeout 巻戻し,
  over-put 検出)、posting read、masked RMW、MCR steering 排他。**35 checks**。
- **PCI** (pci.c): config 幅別 access、capability walk(不在 cap は操作しない)、enable_device(D0+IO/MEM, STATUS 半分を
  汚さない 16bit RMW)、bus master、MSI(IRQ handler と分離、不在は -ENODEV 非 fatal・無書込)。**27 checks**。
- **sync/workqueue** (sync.c): completion(wait 前 complete=即時, mid-wait IRQ race, timeout)、queue_work は遅延実行
  (inline 実行しない)、FIFO、cancel(実行前/実行中)、二重 queue idempotent。**20 checks**。
- **trace** (trace.c): 固定長 ring、overflow を dropped 計上、静的呼出確認と実機通過を別記録。
計 **118 checks 全 pass**。結果=plan/ws031/linux-parity/M1-test-results.txt。
注: これは mock 契約試験(mock-VERIFIED)であり HW-VERIFIED ではない(専門家指示どおり区別)。
次 M2: 実 backend(drv_pci_*/uncore/drv_dma_*/waitq wrap)+ probe.c で P0→P1→P2 wiring、単一 attach 選択、
parity_stop_after 診断停止、fault-injection(GPU 無し)→ 実機 P0-P2 到達+安全 teardown。GPU vfio-pci 維持。

## p011 増分E-35 (2026-09-16): M2 着手① — 接続契約の補強(PCI/MSI, uncore, workqueue)。契約試験 153 checks 全 pass
専門家の M2 指示①(「テストした契約」を Linux 契約と固定せず、親 API の返却値・副作用・寿命に対応)を反映。
- **sync 補強**: completion を bool→**count**(N complete で N wait 満足, complete_all/reinit 別契約)、work の
  **PENDING/RUNNING 分離**(callback 前に pending 解除→**self-requeue で再実行**)、cancel_work_sync(実行中でないことを
  返却時保証, 真の並行 sync は kernel backend 試験へ)、二重 queue idempotent。**31 checks**。
- **PCI 補強(§1.1)**: enable_device を **resource-aware**(MEM のみ device は MEM decode のみ, IO BAR 有れば IO も)+
  **参照数**(enable×2/disable×1 は enabled 維持, 最後の disable で decode 解除)。MSI を **setup**(vector alloc→
  vector から message 生成[固定コピー禁止, data=vector]→enable, alloc 失敗=非 enable 無 leak, message 失敗=取得 vector 解放)。
  bit 操作は下位 helper に残置。IRQ handler 設置(P4)と分離。**43 checks**。
- **MMIO 補強(§1.2)**: 3 層化(auto=通常 access が forcewake を自動 get/put, held=呼出側保持, raw=init 用)。
  Intel masked write(mask を上位 16bit に持つ**単一 write**, RMW と別)を追加。**43 checks(旧 35+auto+mask)**。
計 **153 checks 全 pass**(DMA 36 + MMIO 43 + PCI 43 + sync 31)。-Wall -Wextra -Werror clean。mock-VERIFIED。
UNIMPL/sentinel の「後続確実停止」(§1.4)は probe.c の BLOCKED stage で実施予定(M2 work3)。
次: ② runtime PM 早期基盤(early init/参照管理, enable/suspend-resume は前倒ししない)→ ③ kernel backend 接続
(実 zedBSD lock/waitq/workqueue で GPU 無し試験)→ ④ 単一 parity attach で P0→P2 実行+安全 teardown。GPU vfio-pci 維持。

## p011 増分E-36 (2026-09-16): M2 ② runtime PM 早期基盤。契約試験 173 checks 全 pass
device runtime PM 層(osdep/runtime_pm)を実装。**init_early(struct 初期化)と enable(driver-load 末尾の autosuspend)を
区別**、probe 中(!enabled)は put で suspend しない(suspend inhibit)。**get_sync と resume_and_get の失敗時 usage
count 差**を保持(get_sync=失敗でも inc 維持=caller が put, resume_and_get=失敗で巻戻し)。forcewake/display power とは
別層(混同しない)。**rpm 20 checks**。計 173 checks(DMA36/MMIO43/PCI43/sync31/rpm20)。-Werror clean。mock-VERIFIED。
次: ③ kernel backend 接続(実 zedBSD lock/waitq/workqueue で GPU 無し backend 試験)+ 実 PCI/MMIO/DMA backend →
④ probe.c で単一 parity attach P0→P2 + parity_stop_after 診断 + fault-injection → 実機 P0-P2 到達+安全 teardown。

## p011 増分E-37 (2026-09-16): M2 ③ Work1 着手 — parity osdep 6 層を kernel build へ統合、in-kernel コンパイル成功
専門家 M2 作業1「kernelへ組み込む(GPU 操作なし, legacy 不変)」の第一歩。platform/amd64/vmunix.mk の
AMD64_I915_SOURCES に parity/osdep/{trace,dma,mmio,pci,sync,runtime_pm}.c を追加。**kernel の厳格フラグ
(-ffreestanding -mcmodel=kernel -mgeneral-regs-only -Wall -Wextra -Werror)で 6 ファイル全て 0 error/0 warning
コンパイル、vmunix へ link 成功**(rc=0)。stdint.h は kernel build で利用可(既存 internal.h も使用)。
呼び出し元なし=legacy attach 不変・GPU 操作なし・device 公開なし(専門家指示どおり)。/tmp/vmunix.mk.bak-parity 保持。
開示: vmunix.mk はビルド列挙のみ変更(HAL/UAPI ではない)。次: ③ 実 backend(spinlock/waitq/ zedBSD には
workqueue API 無し→thread+waitq で deferred work, drv_dma_*/drv_pci config/kern_mmio wrap)+ in-kernel backend 試験、
→ ④ probe.c P0→P2 + parity_stop_after + fault-injection → 実機到達+teardown。

## p011 増分E-38 (2026-09-16): M2 ③④ — parity attach P0 を実機実行成功(実 backend 経由)
実 PCI backend(parity/backend_pci.c, drv_pci_device_config_* wrap)+ probe.c(P0..P2 diagnostic walk)+
parity.h を実装。Makefile に CONFIG_DRIVER_PCI_I915_PARITY 追加、i915_start 冒頭で gated 選択(parity ON 時は
legacy を実行せず parity_attach→ENODEV で非公開)。parity OFF/ON 両ビルド 0 error/warning。
**実機(8086:46a8, VFIO)で parity attach P0 実行成功**:
- pci_enable_device が実 config access: PM cap を 0xd0 に発見し **D0 遷移**、resource-aware で decode enable
  (want=0x3=IO|MEM, 最終 command=0x0007)、trace 全記録。
- 次 P0 dep(i915_driver_create)未実装→**honest BLOCKED**(偽 fallback せず)。
- teardown で PCI_COMMAND を saved(0x7)へ restore(安全終了)。device 非公開。
診断結果: reached=P0 outcome=BLOCKED where=i915_driver_create err=0。単一 parity 選択(legacy 不実行)を実機実証。
開示: Makefile(config -D 追加)/vmunix.mk(source 追加)/i915.c(gated 選択, legacy 既定不変)変更。/tmp に .bak 保持。
次: P0 残 dep(driver_create/early_probe/vgpu_detect/gt_probe)→ P1(uncore MMIO backend+sanitize)→ P2(DMA/GGTT/
bus master/MSI)を順次実装し BLOCKED frontier を前進、P2 到達+teardown を目標。GPU vfio-pci 維持。

## p011 増分E-39 (2026-09-16): parity attach P0 完了 + P1 実 forcewake MMIO を実機実行
実 MMIO backend(parity/backend_mmio.c: kern_mmio wrap + Gen9+ forcewake req/ack[render 0xa278/0xd84,
GT 0xa188/0x130044, KERNEL bit0 masked])実装。probe.c を P0 完了(rpm_init_early/driver_create/early_probe/
vgpu_detect=physical/gt_probe_all)+ P1(BAR claim+map 8MB → osdep_mmio init → forcewake GT get → fuse 読み → put)
へ拡張。**実機(8086:46a8)で実行成功**:
- P0 pci_enable_device ok(command=0x0007)。
- **P1 uncore BAR mapped 8388608 bytes**。
- **P1 device_info: 実 forcewaked MMIO 読み slice_fuse=0x1(1 slice) dss_fuse=0x1f(5 DSS)** — selftest の dss_count=5 と
  一致=読み値正当。forcewake GT domain get(ACK 実機で返る)→ read → put の handshake が実機動作。
- BLOCKED at intel_gt_init_mmio(次 P1 未実装 dep)。teardown で BAR unmap→PCI restore(安全終了)。device 非公開。
診断: reached=P1 outcome=BLOCKED where=intel_gt_init_mmio。BLOCKED frontier が P0→P1 へ前進。
次: P1 残(gt_init_mmio/sanitize_gpu)→ P2(DMA mask/GGTT/memory region/bus master/MSI)を実装し P2 到達+teardown 目標。
GPU vfio-pci 維持。build 0 error/warning。

## p011 増分E-40 (2026-09-16): parity attach が P0→P1→P2 を実機で走破(BLOCKED frontier=P2 内)
実 DMA backend(backend_dma.c: set_info は drv_dma_device_address_bits で device 実 DMA 幅を検証, host-phys-bits を
使わない)実装。probe.c を P1 完了(gt_init_mmio=MCR multicast, sanitize_gpu=**実 GT full reset 実行完了**)+
P2(set_dma_info)へ拡張。**実機実行**:
- P0 pci_enable_device ok。P1 BAR 8MB map + fuse 読み(5 DSS)+ **sanitize_gpu GT reset done(polls=39, HW が GDRST clear)**。
- **P2 set_dma_info: dma_bits=32 max_seg=0x1000000 coherent=1**(drv_dma_device の実値。host phys-bits 39 でない)。
- BLOCKED at i915_ggtt_probe_hw(P2 最初の未実装 dep)。teardown で BAR unmap→PCI restore。device 非公開。
診断: reached=P2 outcome=BLOCKED。**専門家目標「P2 到達+安全 teardown」達成**(GGTT 以降は未実装で honest BLOCKED)。
注: dma_bits=32 は zedBSD DMA 層が報告する device DMA 幅(GPU VA 48bit や MGAW 39 とは別概念、要 P2 GGTT 実装時に確認)。
次: P2 残(ggtt_probe/init, memory region, bus master, MSI, opregion)+ Work1 の in-kernel 並行試験。M2 中間報告を作成。
build 0 error/warning。GPU vfio-pci 維持。

## p011 増分E-41 (2026-09-16): P2 frontier 前進 — ggtt_probe_hw を実機実行(GGTT 4GB/1M entries)
probe.c に P2.2 i915_ggtt_probe_hw 実装(SNB_GMCH_CTRL[0x50] config 読み→GGMS→table size, BAR upper half に
GTT window map, entries=table/8)。DMA アドレスを使わない config+MMIO のみ。**実機実行**:
- P2 ggtt_probe: ggms=3 entries=1048576 (4096 MiB GPU VA window) — Gen12 GGTT 実値。GTT window map 成功。
- BLOCKED at **i915_ggtt_init_hw**(scratch page + PTE fill = scratch の DMA アドレスを PTE に書く段。drv_dma 32bit
  質問が直接効く箇所なので、専門家回答待ちとして DMA アドレス捏造せず BLOCKED)。
- teardown に gtt window unmap 追加(逆順: gtt→regs→release bar→pci restore)。
診断: reached=P2 outcome=BLOCKED where=i915_ggtt_init_hw。frontier= P2 の GGTT scratch/PTE 直前。
→ 次 P2 step(ggtt_init_hw の DMA scratch)は Q1(drv_dma 32bit vs Linux 39bit, PTE への DMA アドレス符号化)の
専門家回答が生きる箇所。非 gate な作業(Work1 in-kernel 並行試験)は sync backend[completion→waitq, workqueue→thread]
追加が要る。build 0 error/warning。GPU vfio-pci 維持。

## p011 増分E-42 (2026-09-16): 32bit DMA の出所を特定 + drv_dma は identity+bounce。専門家指示で 39bit 要求へ
専門家回答を受け、DMA/GGTT scratch を実装開始。まず 32bit の出所を調査:
- **32 の出所 = pci-pcat.c:88 のバス DMA 制約ハードコード**(.address_bits=32, .max_segment_size=16MB[=報告の max_seg
  0x1000000], .coherent=1)。型/HW 制限でなく保守的既定(drv_dma の演算は uint64_t 一貫)。専門家の「汎用初期 mask」に該当。
- **drv_dma_map は identity**(mapping->segment.address = allocation->paddr、この env は vIOMMU 無しで paddr 直)。
  **ただし mask 超過ページには bounce buffer 経路**あり。
- **i915 は既に 39bit で確保**(I915_DMA_MAX_ADDRESS=(1<<39)-1 で kern_pmem_alloc_limited)。
→ **帰結**: 32bit バス制約のままだと i915 の 39bit 確保ページ(>4GB)が bounce される。parity は bounce sync 未実装なので
  **39bit 要求が必要**(専門家 Q-B 指示と一致)。「32 で偶然収まる」ではなく、bounce を避けるため 39bit 要求が正当。
- 訂正反映: 「4GB RAM だから 32bit 内」は誤り(Q35 は RAM を 4GB 上下に分割配置しうる)→実 memory map と実 DMA 返値で判定。
専門家決定: Q-A=scratch は internal object→pages→DMA mapping 経路(alloc_coherent 決め打ちせず), Q-B=39bit 要求維持
(streaming/coherent=0x7fffffffff, max_seg=UINT_MAX), Q-C=IRQ 基盤に資源/handler 分離入口追加(P4 移動せず),
Q-D=full reset は __intel_gt_reset(ALL) 関数列(GEN11_GRDOM_FULL, 2 write/poll+50us+retry)接続, Q-E 順=DMA/scratch+
契約試験→実 kernel 並行試験→MSI 分離→P2 実機。次: 39bit DMA device 作成 + memory map 出力 + scratch(internal object)。

## p011 増分E-43 (2026-09-16): P2 set_dma_info を 39bit 要求へ修正、実機実証(Q-B 対応)
専門家 Q-B に従い、parity P2.1 を Linux 準拠の 39bit 要求へ修正。共有 PCAT バス(32bit 保守既定)を使わず、
**i915 自身の 39bit DMA device を drv_dma_device_create で宣言**(dma_set_mask 相当, address_bits=39,
max_segment=UINT_MAX, coherent=1)。要求値を backend 現在値から逆算しない。**実機実行**:
- dma_request: streaming/coherent_mask=0x7fffffffff max_seg=0xffffffff(Linux 要求値)。
- dma_backend: bus_bits=32(共有バス既定) i915_bits=39(i915 device) i915_max_seg=0xffffffff coherent=1。
→ 39bit 要求が accept され 32 へ縮退せず。teardown で dma device destroy。
これにより i915 の 39bit 確保ページ(>4GB)が bounce されず identity map される道が開いた(drv_dma は identity+
mask 超過時 bounce だが 39bit device なら ≤39bit は bounce なし)。BLOCKED は依然 ggtt_init_hw(scratch)。
次: scratch を internal object 経路(pages→SG→DMA map→CPU pin→内容初期化→PTE encode, GGTT/PPGTT encoder 分離,
範囲検査 overflow 回避)で実装 + 契約試験(39bit mask/>4GB DMA/mapping 失敗/GGTT vs PPGTT/scratch 途中失敗)。
build 0 error/warning。GPU vfio-pci 維持。

## p011 増分E-44 (2026-09-16): Work1 完了 — scratch を internal-object/drv_dma_vector 経路で実装、実機で PTE encode
専門家 Q-A/Q-E に従い scratch を実装。**drv_dma_map は既存 allocation 内のみ map 可**(任意 kern_pmem 不可)と判明し、
zedBSD の internal-object/SG 相当 = **drv_dma_vector**(pages 確保+CPU addr+DMA segment 公開)を使用(alloc_coherent 決め打ちせず)。
- GGTT/PPGTT encoder を別モジュール parity/pte.c に実装: GGTT PTE=dma|GEN8_PAGE_PRESENT(LM bit1 clear), PPGTT=dma|
  PRESENT|RW(bit1=RW)。overflow-safe 範囲検査(length-1<=mask-addr)+ page align 検査。範囲外/misalign は encode 拒否
  (AND で丸めない/32bit 切詰めない)。**pte 契約試験 18 checks**(GGTT≠PPGTT, >4GB 保持, 範囲/整列拒否)。
- probe.c: scratch を **ggtt_probe 内**で drv_dma_vector_create→CPU addr 取得→memset 0→segment 取得→range-check→
  GGTT PTE encode。table は fill しない(clear_range/scratch_range は別 callback)。teardown で vector free。
- **実機**: P2 scratch cpu=0xffff80003fe5c000 dma=0x3fe5c000 len=0x1000 ggtt_pte=0x3fe5c001(=dma|PRESENT, LM clear)。
  drv_dma_vector の DMA segment から取得した実 DMA アドレスで PTE を符号化。dma=0x3fe5c000 はこの run(1GB RAM)では
  low だが encode は full 64bit 保持(mock で >4GB 検証済)。
- mock 契約試験 計 **191 checks 全 pass**(DMA36/MMIO43/PCI43/sync31/rpm20/pte18)。build 0 error/warning。
Work1(DMA config+scratch+契約試験)完了。次=Work2 実 kernel 並行試験(sync backend: completion↔waitq, workqueue↔thread)。
GPU vfio-pci 維持。

## p011 増分E-45 (2026-09-16): Work2 着手 — in-kernel sync ktest 構築、K1 検証。attach 文脈は blocking-wait に不適と判明
専門家 Work2(実 kernel 並行試験)着手。parity/ktest.c に **実 waitq+spinlock completion**(counting 契約準拠:
kcompletion_init[spin_init LOCK_RANK_DEVICE + waitq_init], kcomplete[done++/wake_all], kcomplete_all,
kwait[spin_lock→done 消費 or waitq_sleep(seq,deadline) ループ])+ kthread worker を実装。K1-K5 試験(complete-before-wait,
counting, blocking-wait-woken-by-thread, timeout, complete_all)。parity attach 冒頭で呼出。
### 実機結果 + 判明した文脈制約
- **K1(complete-before-wait, sleep 無)= PASS**(実 waitq completion の counting+wake 機構は動作)。
- **K4(timeout, waitq_sleep+deadline)= hang**。原因: **i915 attach は boot device-probe 文脈**で、
  NVMe driver が明記する通り「idle/早期 boot 文脈では timer 駆動 waitq wakeup が stall しうる」(nvme は
  この文脈で sleep せず poll する)。attach 中の blocking-wait/timeout は不適。
### 対処
parity attach からの inline ktest 呼出は**無効化**(P0-P2 経路は intact、build 0 error/warning)。ktest.c は image に
含むが未呼出。Work2 は「regular threads/timer が steady state の文脈」= GPU-free boot hook or 完全起動後の deferred
kthread で実行する必要。deadline 単位は sched_ticks で正しい(kern_deadline_after=now+delta, scheduler_ticks 比較)。
次: ktest を適切文脈(boot 完了後 hook / 独立 kthread)で走らせ K2-K5 + IRQ-context complete + cross-CPU + sync-cancel を
検証。GPU vfio-pci 維持。

## p011 増分E-46 (2026-09-16): Work2 実行文脈整備(A)完了 + 実 kernel 並行試験 pass。deferred runner 実装
専門家 Q1① を実装。**early attach は register のみ**(i915_start parity は drv_i915_parity_runner_register→return 0,
非公開・非実行)、**boot readiness hook**(main.c boot_start の VFS init 前=scheduler/timer up・device 発見済,
最小 image で VFS 失敗しても到達可)が **managed runner thread** を起動(hook は待たない)。runner が Work2→P0-P2 を
一度だけ実行。parity/runner.{h,c}, include/drivers/i915-parity.h 追加。
### 実機結果(runner thread=ready 文脈)
- **Work2 in-kernel 並行試験 全 pass**: K1(complete-before-wait) / **K4(timeout=attach では hang していた)** /
  K2(counting) / **K3(blocking wait を別 thread が起床)** / K5(complete_all) = **9 checks, 0 failures**。
  → 実 waitq+spinlock completion + kthread の blocking/timeout/cross-thread wakeup が ready 文脈で正しく動作。
  attach 文脈の timer 問題は runner thread(通常 schedule)で解消。
- 続けて **P0→P2 が runner から実行**: pci_enable / fuses 5DSS / sanitize reset(polls=40) / 39bit DMA /
  ggtt_probe 4GB / scratch(dma=0x3fc77000 pte=0x3fc77001) / BLOCKED at ggtt_init_hw。runner thread end ktest_rc=0。
### 帰結
専門家 Q1① の「register→readiness hook→managed thread で test+probe を一度実行」構造を実機実証。early で P0-P2 を
実行して後段で再実行、はしない。build 0 error/warning(parity off/on 両方)。開示: main.c(hook), i915.c(register+defer),
include/drivers/i915-parity.h 追加。次(B 残): IRQ-context complete / cross-CPU queue / 実行中 sync-cancel・再queue。
その後 C(full __intel_gt_reset 手順, MSI 分離, ggtt_init_hw)→ D(P2 を 4GB/4vCPU 参照条件で実機)。GPU vfio-pci 維持。

## p011 増分E-47 (2026-09-16): 同期 backend 共有化 + runner result/寿命 + Work2 B ほぼ完了(cross-CPU 実測)
専門家の直近着手順を実施:
### runner の結果・寿命管理を閉じる
runner-result 構造体(selftest_status/scope, probe_status[STOPPED_AT_P2/BLOCKED/FAILED/NOT_RUN], last_op,
blocked_or_failed_op, cleanup, published)を実装。thread object(detached 自動回収)と handle(result/device 参照)を
分離、result は fill 後に lock 下で公開、done=probe 確定(thread 消滅と非同一視)。共通 launch 判定(register/start
両方が呼ぶ, ready&&!launched を lock 下で一度)。SYNC_ONLY(GPU 未登録=sync のみ)/PROBE(登録=sync+P0-P2)モード。
overall rc を ktest 戻り値だけから生成しない(runner-result で分離)。
### 同期 backend を共有化(今実施)
parity/backend_sync.{h,c} に実 waitq+spinlock completion + kthread worker workqueue を移動(ktest 専用実装を廃し、
通常 driver[将来 M4]と共有)。ktest.c は test case のみ。
### Work2 B(実機, runner ready 文脈)
- K0(native waitq/timer deadline, completion 非依存) ✓ / K1-K5(completion) ✓ / WQ-requeue(実行中 self-requeue で
  2 回実行) ✓ / **cancel_work_sync**(三者 runner/worker/canceller, callback 復帰まで sync-cancel 復帰せず order_ok=1) ✓ /
  **cross-CPU**(payload 書込→queue→別 CPU worker が読む: **gens=64 cross_cpu=64 mismatch=0 queue_fail=0**=全 64 世代が
  別 CPU で実行され公開 payload 一致) ✓。**計 19 checks 0 failures**。
- runner-result: selftest=PASS scope=K0-K5 probe=BLOCKED last_op=i915_ggtt_init_hw cleanup=1 published=0。
### 残 B / 次
残 B = **IRQ-context complete**(実 IRQ 経路からの complete。triggerable IRQ source 要, 次実装)。
次 C = full __intel_gt_reset 手順 / MSI 分離 / ggtt_init_hw → P2 後段。D = 4GB/4vCPU 実機。
build 0 error/warning。開示: vmunix.mk(backend_sync 追加)。GPU vfio-pci 維持。

## p011 増分E-48 (2026-09-16): ggtt_init_hw 実装 → P2 frontier が MSI 直前まで前進(実機)
専門家の主目標「BLOCKED at ggtt_init_hw を越え P2 末尾へ」に対応。probe.c に ggtt_init_hw + P2 後段を実装:
- **ggtt_init_hw**: address_space_init(GGTT)[VM 範囲 4GB 記録] + CPU aperture=**GMADR(BAR2)を drv_pci_device_bar で読取**
  (gmadr=0x380000000000[VFIO 高位], mappable_end=0x10000000=256MB。io_mapping_init_wc は lazy per-page なので range 記録、
  全 aperture の eager map せず。zedBSD は WC map flag 無し→WC/MTRR は n/a と記録)+ **fence 32 本を forcewake 下で
  register 初期化**(FENCE_REG_GEN6_LO/HI=0x100000+i*8)。scratch は現位置保持、二重確保なし。
- gt_tiles_init(single tile NOTE)/ memory_regions_hw_probe(smem NOTE)/ ggtt_enable_hw(Gen6+ no-op 分岐再現、無書込)/
  **pci_set_master**(実 config 書込, command=0x0007)。
- **BLOCKED が ggtt_init_hw → pci_enable_msi へ前進**(P2 最後の substantive step。C3 の IRQ 資源/handler 分離が必要)。
実機: runner-result selftest=PASS(K0-K5) probe=BLOCKED last_op=pci_enable_msi cleanup=1 published=0。
→ P2 は MSI を残すのみ(以後 GVT/OpRegion/PCODE/DRAM は概ね NOTE/early-return)。次: C3 MSI 分離で P2 末尾到達 +
C1 full reset 手順 + B IRQ-context test + D(4GB/4vCPU)。build 0 error/warning。GPU vfio-pci 維持。

## p011 増分E-49 (2026-09-16): parity 起動を参照条件へ統一 + env/BAR2 診断 + aperture 範囲検証
ユーザ決定により parity launcher を **manifest 参照条件へ統一**(run-parity-ref.sh: -machine q35,memory-backend=mem /
-cpu host,host-phys-bits-limit=39 / -m 4096 / -smp 4 / memfd size=4G / vfio-pci x-igd-opregion=on,rombar=0)。
probe.c に env 診断(CPUID 0x80000008 で phys_bits, raw BAR2/BAR3)+ **aperture 範囲検証**(gmadr<=cpu_limit,
size-1<=limit-gmadr, overflow-safe。範囲外は FAILED で map せず)。
### 実機(参照条件)
- **CPUs ready: 4, memory 4089MB**(4vCPU/4GiB 確認)。highest_usable=6GB(RAM が 4GB 上に分割配置=専門家指摘の通り)。
- **phys_bits=39 cpu_limit=0x7fffffffff bar2_raw=0x0c bar3_raw=0x70**。
- **GMADR=0x7000000000**(bit38, 39bit 内。旧 -cpu host の 0x380000000000[bit45] から変化)mappable_end=256MB。
  aperture 範囲検証 PASS(0x7000000000+0x10000000 <= 0x7fffffffff)。VFIO DMA -22 エラー無し。
- B 試験(K0-K5)pass 継続、P2 は pci_enable_msi まで(前回同様)。runner-result cleanup=1 published=0。
→ 参照条件で aperture が CPU-mappable に。旧 1GiB/2vCPU 結果は開発記録として別保持、D 受入は本条件を標準化。
### HAL 変更(ユーザ判断待ち)
C3 の MSI 分離(hal.h hal_irq_register_msi + src/hal/amd64/irq.c)と実 WC mapping(hal.h HAL_SPACE_WC +
src/hal/amd64/space.c PAT)は HAL 変更を伴い、ユーザに hal.h diff 提示中(承認待ち)。それまで WC は range 記録+検証、
MSI は BLOCKED 維持。build 0 error/warning。GPU vfio-pci 維持。

## p011 増分E-50 (2026-09-16): C3 MSI分離 + 実WC/PAT + GPU-freeテスト実装(承認済HAL追加、build検証まで)
ユーザ承認(HAL追加)+専門家指示に従い、①〜④を実装し parity ON/OFF とも build 0 error/warning(amd64 vmunix check: PASS)。

### ① include/hal/hal.h(追加のみ、既存不変)
- MSI分離4宣言: `hal_irq_alloc_msi`(handler無しで vector/routing/message 確保, PCI MSI Enable は触らない, 未接続到着は
  mask+EOI, mapped_addr=メッセージ宛先アドレス) / `hal_irq_attach_msi`(handler接続) /
  `hal_irq_detach_msi_sync`(handler実行のみdrain, queued work/timerは待たない, sleepable文脈・自handler禁止) /
  `hal_irq_free_msi`(handler detach済+**source停止済**が前提)。
- `#define HAL_SPACE_WC (64)`(無音fallback無し。互換fallbackは呼び側が明示選択)。

### ② src/hal/amd64/irq.c(+他アーキ stub)
- 4関数を既存機構へ写像: alloc=register_msi のリソース確保のみ(mode=NONE, handler=NULL) /
  attach=irq_set_handler(mode NONE→REALTIME) / detach_sync=removing+irq_set_handler(NULL)でdrain・allocated保持 /
  free=allocated/msi/mode解放(handler!=NULL or in_handler/in_flight!=0 は HAL_ERR_STATE)。
- 未接続 vector 到着は既存 irq_handler() の no-consumer 経路(hardware_mask+EOI)がそのまま吸収=二重EOI無し。
- 既存 hal_irq_register_msi/unregister_msi は**シグネチャ不変で温存**(wrapper化は最終クリーンアップで実施)。
- arm64/sparcv9/m68k(bsp-x68k)/i386 は4関数を **HAL_ERR_UNSUPPORTED stub**(既存MSIは不変)。

### ③ PAT/WC(src/hal/amd64/{defs.h,asm.c,space.c})
- defs.h: `AMD64_PTE_PAT_4K=0x080`(PTレベルのPATビット), `AMD64_PAT_INDEX_WC=4`, `AMD64_MSR_IA32_PAT=0x277`。
- asm.c amd64_cpu_init(): **IA32_PAT を再プログラム**し index4=WC、index0-3/5-7 は reset既定(WB/WT/UC-/UC)維持。
  value=0x0007040100070406。手順=wbinvd→wrmsr→wbinvd→flush_tlb。BSP は kernel PT 構築前・AP は bring-up 中に実行、
  index4 を選ぶ live PTE は存在しないため aliasing 無し。**per-mapping MSR write ではなく CPU PAT init**(専門家指示通り)。
- space.c: hal_space_map_device が HAL_SPACE_WC を受理(mask追加)。WC と NOCACHE/WRITETHRU/DEVICE の同時指定は
  HAL_ERR_INVALID(競合cache policy)。device_window_map は WC を attributes に保持(UCへ潰さない)。
  device_window_populate は WC 時 `AMD64_PTE_PAT_4K`(PCD/PWT clear=index4=WC)、非WC時は従来 NOCACHE。
  **HAL_SPACE_WC は直接 PTE bit6 ではなく、leaf sizeに応じた PAT index を選ぶ**(専門家指示通り、当面4KiB leafのみ)。

### ④ GPU-freeテスト(ktest.c、deferred runner内で実機実行)
- MSI M0-M4: alloc→free / attach NULL拒否 / attach→(free拒否 HAL_ERR_STATE)→detach mismatch拒否→detach_sync→free /
  二重alloc は distinct vector / 解放済 vector への free・detach 拒否。probe handler は device未発火で安全。
- WC: WC|NOCACHE と WC|DEVICE を map前validationで HAL_ERR_INVALID、出力ポインタ不変を確認(mapは行わない)。
  ※ WC 正経路(実PTE=WC)の検証は GPU-free で安全な mappable 非RAM 領域が無いため、実HWの P2 GMADR aperture で行う。

→ build検証完了。次: probe.c の P2 を新API/WC apertureへ接続 + C1 full __intel_gt_reset + C4/P2残(OpRegion/DRAM/bandwidth) →
実機(参照条件 4GiB/4vCPU/host-phys-bits-limit=39)で ktest(MSI/WC)PASS と PAT が boot を壊さぬ事を確認し STOPPED_AT_P2 到達。
drm 非blacklist・GPU vfio-pci 維持。

## p011 増分E-51 (2026-09-16): E-50 実装を実機(参照条件)で検証 — ktest 41/0, PAT boot安全
参照条件(4GiB/4vCPU/host-phys-bits-limit=39/memfd 4G/x-igd-opregion=on)で E-50 の HAL C3+WC/PAT+testを実機実行。
- **boot健全: CPUs ready 4, memory 4089MB, panic/fatal/triple無し** → **IA32_PAT 再プログラム(全CPU)は boot を壊さない**(最重要de-risk)。
- **ktest: 41 checks, 0 failures**(MSI M0-M4 + WC contract 全PASS)。cross-CPU 64/64 mismatch=0 継続。
  - 初回は source 文字列不正で alloc_msi=INVALID→M0-M3 FAIL。canonical 形式 `"PCI ffff:ff:1f.7"`(16字, PCI dddd:bb:ss.f)へ
    修正し全PASS。msi-source.c は形式のみ検証(source 非保存)ゆえ合成アドレスで安全。
- **probe P2 進行(不変, 期待通り)**: BAR mapped(8MiB) / slice_fuse=0x1 dss_fuse=0x1f(5 DSS) / gt_reset(polls=38) /
  dma streaming/coherent=0x7fffffffff i915_bits=39 / ggtt_probe ggms=3 entries=1048576(4096MiB) / scratch dma=0x100179000
  ggtt_pte=0x100179001 / ggtt_init_hw gmadr=0x7000000000 mappable_end=0x10000000, 32 fences / pci_set_master ok(cmd=0x0007)。
  runner probe=BLOCKED last_op=pci_enable_msi(probe.c 未接続のため)。VFIO/DMA -22 無し。
- 既存の "A64 TIMECOUNTER AP FAIL cpu=1..3" は本作業前から存在する boot診断(回帰ではない)。
→ ①〜④ を実機確定。次: probe.c P2 を新API/WC apertureへ接続(MSI: HAL alloc→PCI message+enable→P2末で handler未接続のまま
  PCI MSI停止+HAL free / WC: ggtt_init_hw で GMADR mappable を HAL_SPACE_WC で ioremap_wc+unmap=WC正経路の実HW検証) +
  C1 full __intel_gt_reset + C4/P2残(OpRegion/DRAM/bandwidth) → STOPPED_AT_P2。

## p011 増分E-52 (2026-09-16): probe.c を C3 MSI分離 + 実WC apertureへ接続、実機で MSI blocker 解消
E-50/51 の HAL 基盤を parity probe P2 へ接続し、実機(参照条件)で検証。build 0 error/warning。
### 接続
- **backend_pci.c**: osdep vtable の `alloc_msi_vector`/`free_msi_vector`(従来 NULL=BLOCKED)を実装。
  alloc=`hal_irq_alloc_msi`(handler無し, canonical source は device address から `format_msi_source` で生成) →
  msg_event を PCI MSI DATA として返す。free=`hal_irq_free_msi`(呼び側 teardown が PCI MSI 無効化済=source停止後)。
  parity_pci_priv に msi_irq/msi_source[17] を追加。
- **probe.c P2.8**: `osdep_pci_setup_msi`(vector確保→PCI message書込→MSI enable)成功後、P2末診断として
  `osdep_pci_teardown_msi`(MSI無効=source停止→vector解放)。handler は P4 まで未接続。frontier を
  pci_enable_msi → **intel_opregion_setup** へ前進。
- **probe.c ggtt_init_hw**: NOTE("WC pending review")を実 WC map へ置換。GMADR 先頭ページを
  `hal_space_map_device(HAL_SPACE_WC)` で ioremap_wc 相当マップ→即 unmap(参照は lazy per-page、ここは WC属性の
  end-to-end 実証)。
### 実機(参照条件)結果
- **ktest 41/0 継続**(MSI M0-M4 + WC contract)。
- **WC aperture probe page mapped va=0xffffffffe1000000** ← HAL_SPACE_WC+PAT index4 の**WC正経路が実HWで成立**
  (GPU-free では到達不能だった正経路)。
- **pci_enable_msi ok(vector programmed, msi_enabled=1, handler unattached)→ torn down(source stopped, vector freed)**
  ← C3 MSI分離が実HWで alloc→PCI message→enable→disable→free まで一貫動作。
- frontier: reached=P2 outcome=BLOCKED where=**intel_opregion_setup**(MSI blocker 消滅)。selftest=PASS, VFIO -22 無し。
→ 残: C4/P2末(intel_opregion_setup[ASLS/x-igd-opregion=on], DRAM info, display bandwidth[tgl_get_bw_info]) + C1 full
  __intel_gt_reset → STOPPED_AT_P2。drm 非blacklist・GPU vfio-pci 維持。

## p011 増分E-53 (2026-09-16): intel_opregion_setup 分岐を実装、実機で ASLS=0(opregion 不在)確認 → frontier=intel_dram_detect
- probe.c P2.9: ASLS(PCI cfg 0xFC)→OpRegion base を読み、8KiB を map して "IntelGraphicsMem" 署名+version 検証する
  実分岐を実装(x-igd-opregion=on 前提)。ASLS==0 は Linux の -ENOTSUPP(不在)分岐として扱う。
- **実機結果: ASLS=0x00000000** → OpRegion 不在。x-igd-opregion=on でも OVMF が IGD 用 ASLS を programming せず
  (VBIOS/IGD-OVMF 非対応環境では ASLS=0 は正常)。Linux 同様 opregion 無しで続行=faithful。
  intel_opregion_absent を記録し frontier を **intel_dram_detect** へ前進。selftest=PASS, ktest 41/0 継続。
- 判断ポイント(専門家へ): hw_probe 残りの intel_dram_detect / intel_bw_init_hw(tgl_get_bw_info)は
  **PCODE mailbox 経路**を要し、かつ **display 帯域計算**であって GT/EU-hang の臨界経路上に無い。
  (a) PCODE mailbox を port して P2 を形式的に閉じる か、(b) hw_probe の GT 関連部を P2 完了扱いとし
  i915_gem_init / GT init(EU-hang 臨界経路)へ主軸を移す か、を要相談。

## p011 増分E-54 (2026-09-16): P1 reset完全移植 + WC全範囲 + 共通PCODE + DRAM/bandwidth → STOPPED_AT_P2 到達(実機)
専門家指示(a: Linux通常初期化を実装として完成)に従い、hw_probe 末尾までを移植。参照条件で **STOPPED_AT_P2** 到達。build 0 error/warning。

### ① P1 reset 完全移植(probe.c)
単発GDRST write を `__intel_gt_reset(ALL_ENGINES)` 完全経路へ置換: reset callback選択 → FORCEWAKE_ALL(RENDER+GT)取得 →
retry-while-ETIMEDOUT(最大3) → uncore lock相当 → 空engine集合のprepare/cancel(P1でengine未生成=iterationは空、前倒し生成せず) →
gen11 full-domain reset = **gen6_hw_domain_reset(GEN11_GRDOM_FULL=0x1): ADL-P(IP<12.70)は正常時 write+wait×2** → udelay(50)相当の
settle(GDRST dummy read×64) → forcewake解放。wait は fast atomic poll(sched_ticks deadline, 非sleep)。
**実機: gt_reset attempt=0 passes=2 settle=64 rc=0**(初回成功)。

### ② WC aperture 全範囲(probe.c ggtt_init_hw)
先頭1ページ→**mappable_end 全範囲(base=gmadr.start=0x7000000000, size=0x10000000=256MiB)を HAL_SPACE_WC で map**、
P2寿命保持、teardown で同範囲 unmap。BAR範囲のCPU view(aperture相当RAMは確保しない)。
**実機: mapped requested=mapped=0x10000000 va=0xffffffffe1000000 attr=RW|WC / teardown unmap rc=0**。

### ③ 共通PCODE(新規 parity/pcode.{c,h})
intel_pcode.c の `__snb_pcode_rw`/`snb_pcode_read` を移植。共通 sb_lock(spinlock)で全PCODE取引を直列化、MAILBOX busy→-EAGAIN、
DATA/DATA1書込→READY|mbox書込→READY down を fast poll(500µs+20ms を tick deadline に畳込、非sleepゆえspinlock安全)→
応答read→gen7 status→errno。MAILBOX=0x138124/DATA=0x138128/DATA1=0x13812c/READY=1<<31。PCODEはforcewake domain外=raw access。

### ④ DRAM/bandwidth(新規 parity/dram_bw.{c,h})
- `intel_dram_detect`→gen12_get_dram_info→icl_pcode_read_mem_global_info: 要求 0x0d、val decode(type/channels/qgv/psf)。
  wm_lv0 は true→(gen12で)false。**実機: raw=0x3420 type=DDR4 channels=2 qgv_points=4 psf_gv_points=3**。
- `intel_bw_init_hw`→**tgl_get_bw_info(&adlp_sa_info)**(deburst16/deprogbwlimit38/displayrtids256/derating20)→icl_get_qgv_points:
  QGV要求(pt<<16)|0x10d を4点、PSF要求 0x20d。**実機QGV: DCLK=2134/2934/3201/2668 等、PSF CLK=32/48/48**。
  bandwidth計算(deratedbw/peakbw/psf_bw/num_planes、丸め・配列上限・ct=0ガード)を6群×点で再現。sagv=ENABLED(qgv!=1)。
  PSF取得失敗時は num_psf_points=0 で計算除外(参照fallback)を実装。void経路=PCODE失敗は許容(BLOCK化しない)、戻り値で
  未実装と区別。

### ⑤ P2受入(実機・参照条件 4GiB/4vCPU/host-phys-bits-limit=39、GPUあり通常probe一回)
P0→P1(full reset)→P2(DMA/GGTT/WC全範囲/tile/memory-region/MSI/OpRegion[ASLS=0]/DRAM/bandwidth)→end_of_P2。
**reached=P2 outcome=STOPPED where=end_of_P2 / probe=STOPPED_AT_P2 cleanup=1 published=0 / selftest=PASS(ktest 41/0) /
boot CPUs ready 4 / VFIO -22 無し**。teardown 逆順(WC aperture→scratch→DMA→GTT→BAR→PCI)成立。

→ 残(専門家次報要求): GPU-free 試験追加(reset制御フロー/WC全範囲失敗回収/PCODE取引・decode・calc)、
実IRQ文脈completion試験(one-shot)。その後 P3→P4→P5(実機呼出順維持)。旧MSI API撤去は別作業。

## p011 増分E-55 (2026-09-16): 新規経路の GPU-free 試験追加(reset/PCODE/DRAM/bandwidth/WC)→ 56 checks 0 fail、P2受入維持
専門家指示の試験を ktest(既存のGPU-free runner)へ追加。fake osdep_mmio backend(既存 vtable、新規汎用基盤は作らない)で
reset/PCODE を決定的に検証。reset は制御フロー検証のため reset.c へ抽出(parity_gt_reset_all(mmio,timeout_ms))。
dram decode は純粋関数 parity_dram_decode に分離。

### 追加試験(15件、計 41→56、全pass)
- **reset 制御フロー(fake GDRST)**: 成功(即ack→rc0)/ timeout(never-clear→3 retry全て rc62)/ retry回復(1回失敗→2回目 rc0)。
- **PCODE 取引(fake mailbox)**: 成功(応答語返却)/ busy(開始時READY→-EAGAIN)/ status error(mailbox status→-ETIMEDOUT)。
- **DRAM decode(純粋)**: 0x3420→DDR4/2ch/4qgv/3psf / 不明type→-EINVAL。
- **DRAM detect + bandwidth(fake PCODE 経路)**: global→QGV×4→PSF を script、dram_detect rc0 + bw_init rc0 + SAGV=ENABLED /
  PSF読取失敗scriptで num_psf_points=0 fallback しても bw_init rc0(許容)。
- **WC 全範囲(実HAL)**: window超過サイズ→拒否(出力不変)/ 直後の正当map成功(回収=window状態リーク無し)→unmap。

### robustness 修正(実機 IF=0 対策)
reset/pcode の fast poll は sched_ticks deadline。**bring-up は IRQ無効(IF=0)で走る**([[i915-attach-irqs-disabled]])ため、
never-clear + tick停止だと deadline を跨げず無限ループ。**spin hang-guard(2e7 iter)**を追加(実MMIO read≈1µs=正当reset µsを遥かに
超える範囲、実機happy pathは value-match で即抜けるため未到達)。この修正で timeout 試験が正しく rc62 を返し、実機の reset 安全性も向上。

### 実機(参照条件)再確認
ktest **56 checks 0 failures**、reset 試験ログ(rc0/rc62×3/rc62→rc0)確認。**P2受入維持: reached=P2 STOPPED end_of_P2、
probe=STOPPED_AT_P2 cleanup=1 published=0、dram rc0 bw rc0 sagv=2、boot CPUs 4、VFIO -22 無し**。

### 未了
実IRQ文脈 completion 試験(one-shot): driver 試験から使える one-shot timer→IRQ→completion 機構が未露出(kern timer は
process itimer 中心)。IRQ 試験harness 新設は「新規汎用基盤を作らない」方針と相反。専門家に方針確認(最小hook新設 or 保留)。
→ P2受入は達成。次段は方針確認後 P3(display noirq→IRQ設置→nogem→GEM init の実機呼出順)へ。旧MSI API撤去は別作業。

## p011 増分E-56 (2026-09-16): T1(待機・errno)+ T2(帯域状態)を移植差修正、実機 ktest 61/0・P2受入維持
専門家の 5 作業単位のうち T1/T2 を実施。build 0 error/warning、STOPPED_AT_P2 維持。

### T1 待機・errno(新規 wait.c、reset.c/pcode.c 改)
- **共通時間層 wait.c**: `kern_rtc_read_counter`(単調・IRQ無効でも進行・CPU跨ぎ非後退)で **実時間 atomic poll / udelay / sleep可能poll** を実装。sched_ticks + spin-guard 代用を撤去。時間源不在時のみ bounded fallback で「時間基盤異常」を診断ログ(通常HW timeout と区別)。
- **reset.c**: `parity_gt_reset_all(uncore_lock, mmio, fast_us=2000)`。**spin_lock_irqsave で uncore lock 区間を成立**(gen8_reset_engines相当)。retry は **-ETIMEDOUT のときだけ**(ret==-ETIMEDOUT)。ack待ちは wait.c の atomic 2000µs、settle は **parity_udelay(50)**(GDRST read×64 の代用を撤去)。ログ **passes=実際のwrite/poll回数**(固定2でない)。errno は `<errno.h>` 記号(-ETIMEDOUT 等、手書き62撤去)。
- **pcode.c**: **device所有の sb_lock(mutex)** で直列化(file-static spinlock + 遅延初期化フラグを撤去)。fast=500µs atomic → 未完なら **sleep可能 20ms**(mutex保持ゆえ sleep 可)。status→負errno記号。
- **実機 reset 試験**: 成功 `passes=2 rc=0` / timeout `passes=1 rc=-42`×3 / retry `passes=1 rc=-42`→`passes=2 rc=0`。real P1 は `passes=2 rc=0`。

### T2 帯域状態(dram_bw.c/.h 改)
- 計算結果を **device所有 `struct parity_bw_state`(max[6]{deratedbw[8]/peakbw[8]/psf_bw[3]/num_qgv_points/num_psf_gv_points/num_planes} + sagv_status + valid)** へ保存(local+logのみを撤去)。sb_lock/mmio/di/bw を引数化、probe が device寿命で所有。
- **num_planes は max[i+1] へ**(参照どおり。max[0]へ独自代入せず=ゼロ初期化のまま)。
- **ct<=0 は入力検査**として明示(算出不能テーブルを有効化せず deratedbw/peakbw=0、参照差としてログ)。PSF失敗時は保存 num_psf_gv_points=0 に反映。
- 試験を **return後の保存状態検証**へ強化(max[0].num_qgv_points=4 / deratedbw[0]・peakbw[0]≠0 / max[0].num_planes=0 かつ max[1].num_planes≠0 / PSF失敗で max[0].num_psf_gv_points=0)。

### 実機(参照条件)
**ktest 61 checks 0 failures**、dram_detect rc=0 / bw_init rc=0 / sagv=2、WC 256MB map+unmap rc=0、**reached=P2 STOPPED end_of_P2 / probe=STOPPED_AT_P2 cleanup=1 published=0**、boot CPUs 4、VFIO -22 無し。
→ 残: T3(PAT CD/PGE/MTRR完全手順)、T4(実IRQ one-shot試験)、T5(P3 noirq、device状態をP2から継承)。

## p011 増分E-57 (2026-09-16): T3(PAT CD/PGE/MTRR完全手順)を CPU初期化へ補完、実機 boot 健全
amd64_cpu_init の wbinvd→wrmsr→wbinvd→flush_tlb を、Intel SDM のメモリ型MSR更新手順へ置換(新規 amd64_pat_configure): IRQ保存→PGE無効→no-fill cache(CR0.CD=1,NW=0)+WBINVD+CR3再読でTLB flush→MTRR無効化(DEF_TYPE.E=0, 内容保持)→IA32_PAT設定(index4=WC)→MTRR復元→WBINVD+TLB flush→CR0/CR4復元→IRQ復元。CPUID.01H:EDX.MTRR で DEF_TYPE アクセスを gate。設定後 IA32_PAT を read-back し不一致は HAL_FATAL(記録)。defs.h に IA32_MTRR_DEF_TYPE=0x2ff。
「未使用slotだから省略」を撤回し現CPU初期化で補完。WC slot配置/PTE変換は不変、MTRR内容は推測せず保存・無効化・復元のみ。
### 実機(参照条件)
BSP+3AP の全4CPUで完全手順を実行し **boot 健全(CPUs ready 4, memory 4089MB, panic/fatal/triple無し, PAT read-back一致=HAL_FATAL不発)**。WC aperture 256MB map 継続、ktest 61/0、STOPPED_AT_P2 維持。
→ 残: T4(実IRQ one-shot completion試験)、T5(P3 noirq)。

## p011 増分E-58 (2026-09-16): T4 実IRQ文脈 completion 試験(one-shot hook)実装、実機 ktest 63/0
専門家決定(a: 最小one-shot hook、汎用timer API新設なし)を実装。
- **kern/clock.c**: 診断ビルド限定(#if CONFIG_DRIVER_PCI_I915_PARITY)の one-shot hook。kern_diag_oneshot_arm(fn, arg, avoid_cpu)/disarm。kernel_timer_handler 末尾で diag_oneshot_fire(cpu): armed を acquire-load→avoid_cpu と一致なら skip→atomic_compare_exchange(1→0)で一回だけ claim→fn(cpu,arg)。
- **IRQ-safety**: 共有 backend(parity_kcompletion)は plain spin_lock + waitq_sleep = **thread文脈専用**(spin_lock は IRQ 無効化せず、waitq_sleep は plain unlock 要)。同一CPUで waiter が lock 保持中に timer IRQ が complete() を呼ぶと deadlock 窓。→ hook を **waiter の CPU(avoid_cpu)以外でのみ発火**させ、cross-CPU 完了で窓を解消(参照条件 4CPU、単一CPUは skip)。backend は改変せず。
- **ktest**: 静的 completion + oneshot_timer_cb(cpu/tag記録+parity_kcomplete)。runner が自CPUを avoid にして arm→kwait→復帰・cross-CPU・tag 検証→disarm。オブジェクトは file-scope=診断kernel寿命保持(IRQ内 free/unregister 無し)。
### 実機(参照条件)
**ktest 63 checks 0 failures**(+2: 「timer IRQから共有completion通知」「一回・cross-CPU・tag一致」)。boot CPUs 4、STOPPED_AT_P2 維持、cleanup=1 published=0。deadlock 無し。
→ 残: T5(P3 noirq、device状態をP2から継承、DMC firmware provider 等)。

## p011 増分E-59 (2026-09-16): T5 P3 noirq 着手 — continue-mode で device状態をP2から継承、frontier=drm_vblank_init
- **parity.h**: PARITY_STAGE_P3 追加。**runner**: stop_after=P3。
- **continue-mode**: stop_after==P2 は従来の診断stop+cleanup。P3継続時は P2 資源(WC aperture/MSI/MMIO/DRAM・bw状態)を**P2末尾で破棄せず**、最終 teardown で逆順解放。MSI は P2 診断時のみ即解放、継続時は **P4のhandler接続用に保持**(msi_kept)、teardown で解放。
- **P3 = intel_display_driver_probe_noirq**: 参照の noirq 子処理列(drm_vblank_init/intel_bios_init/intel_vga_register/intel_power_domains_init(+_hw)/intel_pmdemand_init_early/intel_dmc_init/modeset・flip workqueue/intel_mode_config_init/intel_cdclk_init/intel_color_init/intel_dbuf_init/**intel_bw_init(display SW状態=P2のintel_bw_init_hwとは別)**/intel_pmdemand_init/intel_init_quirks/intel_fbc_init)を移植対象として記録。i915_inject_probe_failure=no-op、HAS_DISPLAY=true(OpRegion不在と独立)を再現。
- **最初の正確な未実装依存 = drm_vblank_init**(drm_device + INTEL_NUM_PIPES = display/drm 層。compute中心 parity は未 bring-up)。P3 子処理の移植には display 基盤の立ち上げが前提。
### 実機(参照条件)
P3 enter(device state carried from P2, msi_kept=1) → reached=P3 outcome=BLOCKED where=drm_vblank_init / teardown で MSI resource released + WC aperture unmap rc=0。ktest 63/0、boot CPUs 4、cleanup=1 published=0、VFIO -22 無し。
→ T1-T5 完了(実機確定)。P3 子処理(display/drm基盤)の移植深度は要スコープ確認。

## p011 増分E-60 (2026-09-16): ① legacy PS描画 一回(現ビルド, 独立起動)= timeout(PS段stall再現)。原因探し再開せず記録して終了
専門家指示① を実施。**parity probe は同時実行せず**(CONFIG_DRIVER_PCI_I915_PARITY=n)、現ソースのビルド(現HAL/CPU init/PAT完全手順込)で **CONFIG_DRIVER_PCI_I915_SELFTEST=y**、既存 `drv_i915_draw_selftest`(RCS0 VS+PS オフスクリーン描画)を一回。EU compute 試験は skip(ハング前に到達させないため)、store/clear/rcs/rt は非EU/render prep として保持。i915.c の一時変更のみ(描画後リバート)、ktest.c の T4 one-shot は `#if CONFIG_DRIVER_PCI_I915_PARITY` guard 追加(PARITY=n build 修正=保持)。

**test_name=legacy_ps_once_current_build / parity_path_executed=false**。
- build id: vmunix sha256 先頭 623df460b0edb359 / selftest.c 7f02d65f… / i915.c(draw-once改) / 起動 4GiB・4vCPU・host-phys-bits-limit=39・vfio-pci x-igd-opregion=on(Linux成功環境/引渡し不変)。POLL_BOUND=200000000(有限)。
- prep PASS: bcs0 store / clear(px 全て 0xffff0000)/ rcs(marker 0xcafef00d)/ rt sba(0x5ba5eba5)。
- **draw = timeout(提出後 期限まで未完)**: markerA=0xa5a50001, markerMid=0xc5c50003 **到達**、**markerPS=0x00000000, markerB=0x00000000**(PS段が完了せず=walker/PS stall)、completed=2 / seqno=3(request未完)。RT pixels [0]/[mid]/[last]=0x00000000(未書込)expected=0xffff0000。stats readback=0xffffffff(ps_invocations等 未実行)。
- 停止状態(reset前採取): fuses slice_en=0x1 dss_en=0x1f eu_dis=0x0 rpcs(ctx/reg)=0x80041000、idle ss0-4 row_instdone=0xffffffff / ss5=0x00000000(EU per-thread instdone)。attach stopped at selftest:5(EIO)。
- 記録保存: 10.0.10.25:~/bigbang/legacy_ps_once_current_build.log。boot CPUs 4、panic無し。VM は timeout で終了(同起動で parity 開発試験を継続せず)。
→ **判定=timeout**。**現ビルド(T1-T5/PAT完全手順等 全部込)でも既知の PS段 stall は不変**(HAL/CPU init 変更は本hangを直しも壊しもしない)。原因探索/リバート/レジスタ探索へ戻らず、次(②completion IRQ安全化 → ③wait.c → ④DRM device+vblank)へ。i915.c は元へ復帰、parity build(PARITY=y)を継続。

## p011 増分E-61 (2026-09-16): ② 共有completion を同一CPU IRQ安全化(spin_lock_irqsave)、実機 ktest 65/0
専門家指示② を実施。avoid_cpu 回避を completion 安全性の代わりにしない。**共有 backend を直す**(新completion作らない)。
- **backend_sync.c**: parity_kcomplete/kcomplete_all/kreinit/kwait を **spin_lock_irqsave/spin_unlock_irqrestore** 化。lost-wakeup race は既存の「c->lock を token登録〜state=SLEEPING 間 保持 + waitq sequence(observed)」機構が処理済(spin_lock_irqsave で囲むだけの naive 化ではなく、sched_sleep_locked が内部で hal_irq_disable + context-switch で IRQ状態を扱う契約に接続)。deadlock窓(同一CPUで waiter が c->lock 保持中に IRQ が complete)を、**保持中は IRQ無効**により解消。
- **one-shot hook**: avoid_cpu に加え **require_cpu**(0xffffffff=不問)追加。同一CPU試験用に waiter の CPU でのみ発火可能に。
- **ktest**: cross-CPU(avoid=self)継続 + **同一CPU試験(require=self)追加**=waiter 自身の CPU の timer IRQ から共有 completion を通知し完了・同一CPU・tag 検証。
### 実機(参照条件)
**ktest 65 checks 0 failures**(+2: cross-CPU / same-CPU)。既存 K1-K5/counting/timeout/cancel・requeue/cross-CPU 全pass(irqsave化で回帰無し)。P3 到達(reached=P3 BLOCKED drm_vblank_init)維持、boot CPUs 4、cleanup=1 published=0。**同一CPU IRQ→completion が安全**に。avoid_cpu/require_cpu は診断条件へ(安全性要件ではない)。
→ 残: ③ wait.c(slow待機を参照sleepへ/時間源異常を隠さない/us_to_ticks overflow・切上げ)、④ DRM device+vblank。

## p011 増分E-62 (2026-09-17): ③ wait.c 修正 + 実機知見=HAL timecounter が KVM で不安定 → 自前 TSC 較正で解決
専門家指示③(slow待機を参照sleepへ / 時間源異常を隠さない / us_to_ticks overflow・切上げ)を実施。実装過程で **重要な実機知見**を発見・解決。

### 実機知見: kern_rtc_read_counter(validated timecounter)は KVM 4vCPU で不安定
- 診断: probe 開始時 `time-base probe: ok=0 counter=0 freq=0`。一方 ktest の reset 試験は動作(rc=-42)。= **validated TSC timecounter が late-calibration かつ multi-vCPU stress 後に guard で自己無効化**。freq=0/ok=0 を返す時と返さない時がある。
- 解決: **自前 TSC 較正**。runner thread 開始時(IRQ有効・sched_ticks 進行・IRQ-off wait より前)に、生 rdtsc を **scheduler tick 10 ticks(100ms)にわたって測定** → freq を算出しキャッシュ(`parity_wait_init`)。以後の wait は **生 rdtsc + キャッシュ freq**(rdtsc は常時読取可・IRQ非依存・guard無効化の影響なし)。実機 **freq=2501787070 Hz(≈2.5GHz)安定**。

### wait.c 修正(③本体 + 上記)
- **時間源 = 生 rdtsc(lfence;rdtsc;lfence)+ 自前較正 freq**。sched_ticks/spin-guard 代用は無し。reset は spin_lock_irqsave(IRQ off=CPU pin)ゆえ rdtsc delta に cross-CPU rebase 不要。
- **slow 側 = sleep可能待機**(sched_yield 代用を撤去。kern_deadline_after + waitq_sleep で 1 tick 間隔 sleep、mutex保持中 sleep 可・spinlock 不可。reset は slow_ms=0 で sleep 経路に入らない)。
- **us_to_ticks**: 乗算 overflow(飽和)+ **切上げ**(要求より短い待ちにしない)。
- **時間源不在を隠さない**: freq 未較正は **-EIO(time-base anomaly)** で返す(通常成功/‐ETIMEDOUT に混ぜない)。probe 開始前に `parity_wait_time_base_ok()` 検査、不在なら FAILED where=time_base_anomaly。pcode.c は wait の戻り値を**そのまま伝播**(下位の -EIO を一律 -ETIMEDOUT へ変換しない)。
- **dram_bw**: ct<=0(算出不能)を local `incomputable` で追跡し **bw->valid=0** に反映(ゼロ継続+valid=1 の不整合を解消。正常入力の式は不変)。

### 実機(参照条件)
`TSC self-calibrated freq=2501787070 Hz` → **ktest 65/0**、reset attempt=0 passes=2 rc=0、dram rc=0 / bw rc=0 sagv=2、**reached=P3 BLOCKED drm_vblank_init**、boot CPUs 4、time-base anomaly 無し、cleanup=1 published=0。
→ ①②③ 完了。残 ④ DRM device 管理 + drm_vblank_init 移植(P3=案A)。

## p011 増分E-63 (2026-09-17): ④ DRM device管理 + drm_vblank_init 移植(案A Work A+B)、実機で frontier=intel_bios_init へ前進
専門家指示④(案A: display 対象経路を順に移植)の Work A+B を実施。build 0 error/warning。

### 新規 parity/drm_device.{c,h}
- **Work A(drm_dev_init 相当)**: `struct parity_drm_device`= parent 参照 / driver_features / open_count / managed-resource lock / event lock / minor_registered(=0, /dev/dri 非公開) / num_crtcs / vblank[4] / vblank worker / inited。`parity_drm_dev_init` は管理lock初期化・parent/features設定(ゼロ構造体を渡す方式ではなく実初期化)。**P2 の device状態(DRAM/bw/MSI/WC)は再初期化せず保持**。
- **Work B(drm_vblank_init + drm_vblank_worker_init)**: `parity_drm_vblank_init(ddev, num_crtcs)` = vblank_disable_immediate 設定 → **device vblank worker を既存 parity_kworkqueue で生成**(worker を先に、device管理成立後) → num_crtcs 分 per-CRTC を init(spin lock / waitq / pipe index / count / seq)→ 記録。`parity_drm_vblank_fini`= worker 回収 + per-CRTC 解放(managed cleanup 相当)。
- **pipe数 = INTEL_NUM_PIPES = hweight8(pipe_mask)**。ADL-P(xe_lpd)pipe_mask=A|B|C|D(0xf)→ **4 CRTCs**(vCPU/DSS数の流用でなく device runtime info)。

### probe.c P3 接続
drm_vblank_init UNIMPL を **実処理へ置換**: parity_drm_dev_init → parity_drm_vblank_init(4) → frontier を **intel_bios_init** へ前進。teardown で DRM device+vblank を **P3→P2 逆順**で解放。

### 実機(参照条件)
`P3 display_driver_probe_noirq enter (device state carried from P2, msi_kept=1)` → `drm_dev_init acquire` → **`drm_vblank_init: 4 CRTCs + vblank worker initialised`(a0=0x4)** → `intel_bios_init unimplemented` → `reached=P3 outcome=BLOCKED where=intel_bios_init` / teardown `DRM device + vblank released`。ktest 65/0、boot CPUs 4、TSC self-calibrated 継続、cleanup=1 published=0。
→ ①②③ + ④ Work A/B 完了。残 Work C(intel_bios_init → VGA → power domain → … DMC firmware provider)。次報主目標=BIOS/power domain 側へ前進。

## p011 増分E-64 (2026-09-17): T単位=HAL時間backendを正式修正(TSC自前較正の迂回を撤去)、wait.cをHAL backendへ接続。実機 timecounter 公開・ktest 65/0
専門家指示T(「HAL検査を迂回する自前TSC較正を正式backendに固定しない=HAL/kernel側を改善」)を実施。**根本原因を特定し HAL を修正**、wait.c から rdtsc/sched-tick 較正を撤去。

### 根本原因(3.1 診断)
HAL の `kern_rtc_read_counter`(amd64 validated timecounter)が本環境で常に不可用。診断ログで確定:
- `A64 TIMECOUNTER CANDIDATE policy=0 ratio=0/0 crystal=0 invariant=0` = **CPUID 0x15(TSC比率/crystal)不在、かつ invariant-TSC bit=0**(QEMU -cpu host は invtsc を既定でマスク)。→ `amd64_tsc_cpuid_frequency_evaluate` が invariant 必須ゆえ **UNAVAILABLE** を返し candidate 不成立 → 公開されず。
- 過去の「動いた」実行は wait.c の(現在撤去した)silent fallback 経由だった。
- 追加確認: KVM CPUID 0x40000000 sig=KVMKVMKVM 一致だが **max leaf=0x40000001**(0x40000010 KVM_TSC_KHZ leaf 非公開)→ CPUID からの freq 取得も不可。
- guard は別に「cross-CPU raw TSC が last_sample を下回ると**永久無効化**」も持つ(runtime regression 用)。

### HAL 修正(共通backend、迂回でなく本体修正)
1. **KVM 検出**(`amd64_kvm_present`: CPUID 0x40000000 signature)→ KVM は仮想TSCの安定を保証。
2. **KVM 時は NEEDS_PIT** を選択(invariant 不在でも既存 PIT 較正経路 lapic.c を起動)→ **PIT が TSC 周波数を実測**(実機 **2,497,019,031 Hz ≈ 2.497GHz**、自前較正値と一致=相互検証)。
3. **metadata 互換判定を PIT source では invariant-TSC bit 非必須に緩和**(timecounter-policy.c)。CPUID の invariant advertisement でなく、**各AP の実 TSC bracket probe** が経験的に検証する契約(uncertainty 有界)。CPUID15 source は従来どおり invariant 必須。
4. **runtime guard を permanent-disable→monotonic clamp** に変更(cross-CPU の小skew は last値へ clamp して単調維持、Linux TSC clocksource の last-value guard 準拠)。初回 regression は `A64 TIMECOUNTER REGRESS` で一度記録(今回未発火=bracket 許容内)。

### parity 側(3.3: 較正・CPU固有命令を外す)
wait.c は **kern_rtc_read_counter(HAL共通backend)** のみ使用。**rdtsc inline / sched-tick 較正 / parity_wait_init を撤去**。us_to_ticks は overflow飽和+切上げ。時間源読取失敗は **-EIO + sticky time-base fault**(`parity_wait_time_base_faulted`)で通常成功/‐ETIMEDOUT と区別、probe は開始前に `parity_wait_time_base_ok()` で検査し不在なら FAILED time_base_anomaly。udelay も fault を latch(log して先へ進まない)。slow側は kern_deadline_after/waitq_sleep 戻り値を処理(異常時無限loop回避)。

### 実機(参照条件)
`A64 TIMECOUNTER READY cpus=4 source=pit hz=0:2497019031` = **HAL timecounter 公開**。ktest **65/0**、reset(成功 passes=2 rc=0 / timeout passes=1 rc=-42 / retry)、dram rc=0 bw rc=0 sagv=2、**reached=P3 BLOCKED intel_bios_init**(drm_vblank_init 4 CRTCs)、time-base anomaly 無し、boot CPUs 4、cleanup=1 published=0。**自前TSC較正の迂回を撤去し、正式backend(source=pit)で受入**。
→ T の核(HAL backend 正式化 + wait.c 接続)完了。残: T細部(slow高分解能sleep-range、wait 4試験)、D(DRM managed cleanup/timer/seqlock/4試験)、P(P3続行 BIOS/VGA/power domain)。

## p011 増分E-65 (2026-09-17): D単位=DRM device管理 + drm_vblank_init を完成(drmm契約/per-pipe timer・seqlock/途中失敗回収/試験)、実機 ktest 73/0
専門家指示D(「DRM device管理と drm_vblank_init を一まとまりで完成」)を実施。build 0 error/warning、HAL timecounter source=pit 継続。

### drm_device.{c,h} 強化
- **drmm managed cleanup**: `struct parity_drmm_action drmm[16]` + `parity_drmm_add_action_or_reset`。**契約=登録先が満杯ならその action をその場で実行し失敗を返す**(参照 drm_add_action_or_reset)。`parity_drm_dev_fini` は登録逆順で全 action を実行。
- **per-CRTC 状態**: lock / waitq / pipe# / count / **seqlock 世代** / **vblank-disable timer(disable_timer_inited + deadline, モデル)** / inited。
- **drm_vblank_worker_init**: device vblank worker を既存 parity_kworkqueue で生成(device管理成立後)。cleanup は worker 終了 → CRTC 回収の順。
- **途中失敗回収**: vblank_init は worker+CRTC を init 後に cleanup action を drmm 登録。drmm 満杯で登録失敗すると **contract により cleanup が即実行**(worker破棄+CRTC回収)し vblank_init は失敗を返す=leak なく unwind。num_crtcs 範囲外(0 / >4)は部分状態なしで拒否。

### GPU-free 試験(ktest +8、計 73/0)
正常init(4 CRTCs+worker+per-CRTC state)/ dev_fini が managed cleanup 実行 / CRTC数 0・過大の拒否 / add_action_or_reset が満杯listで action即実行 / **vblank_init が cleanup 登録不可時に unwind**(vblank_inited=0, worker破棄)。

### 実機(参照条件)
`A64 TIMECOUNTER READY cpus=4 source=pit hz=2496778134` / **ktest 73 checks 0 failures** / P3 `drm_vblank_init: 4 CRTCs + vblank worker initialised` / reached=P3 BLOCKED intel_bios_init / boot CPUs 4 / cleanup=1 published=0 / time-base anomaly 無し。
→ T核 + D 完了。残: T細部(slow高分解能sleep-range、wait 4試験)、P(P3続行 intel_bios_init → VGA → power domain → DMC …)。

## p011 増分E-66 (2026-09-17): 専門家是正 T(時間基盤race/戻り値/error)+D(per-pipe vblank worker)+P(intel_bios_init 実処理) を実装、selftest 83/0・P3到達。実機attachはGPU無応答(command=0xffff)で保留
第34報後の専門家是正(T/D/P並行)を実装。build 0 error/warning、HAL timecounter source=pit 継続。**GPU-free selftest = ktest 83 checks 0 failures**、selftest 内 probe が **reached=P3**(per-pipe vblank + intel_bios_init 実行)。実機attach(実VFIOデバイス)は P0 で **command=0xffff**(config 全1=デバイス無応答)→ P1 pci_bar err=3(EINVAL)。これは全変更点(すべてP3)より前で発生し、`pci` は static ローカル(スタック非依存)ゆえ真のデバイス読値。原因はパススルーGPUの wedged 状態(FLR/再起動が必要, 当該sessionはsudo-free で実施不可, 停留qemuプロセス無し)。

### T(時間基盤の残不整合) — timecounter-policy.c / wait.c / reset.c
- **T① last_sample を CAS 単調最大へ**(最優先): `amd64_timecounter_read_guarded` の clamp を 64-bit `__atomic_compare_exchange_n` retry ループへ置換。raw>last のとき CAS で共有最大へ前進(失敗時 last 再読込→再評価)、raw<last は published 最大へ clamp(初回のみ REGRESS 診断)。**外側 state->lock(IRQ無効下 exchange spinlock)で直列化済を明示**し、CAS で「単発store≠compare-and-write全体atomic」の race(2CPUが last=100読→120と110書き→後退)を排除。
- **waitq_sleep() 戻り値を実処理**: slow stage の `(void)waitq_sleep` を戻り値取得へ。`0/ETIMEDOUT/EAGAIN`=間隔満了/起床→レジスタ条件+全体期限を再確認(ループ継続, per-interval 満了を HW timeout に変換しない)。それ以外(EINVAL/EBUSY 等 API異常)=WAIT_API fault latch → -EIO 伝播。waitq_sleep 実装確認: flags=0 は 0 か ETIMEDOUT のみ返す。
- **parity_udelay を error返却化**(void→int): 時間源失敗で -EIO。**reset.c 呼出元**は udelay!=0 で uncore lock 解放 + reset 中断 + retry へ入らない(forcewake は共通exit で解放)。
- **parity_time_base_fault を atomic/共有・初回原因保存**: `volatile int` + `parity_time_base_set_fault(cause)` が CAS で初回のみ latch(NO_COUNTER/READ_FAIL/WAIT_API)、以後上書きしない。reader は atomic load。
- **待機毎に (counter,frequency) を一組保持**: `read_counter_consistent(base_freq,&now)` が freq 変化(単位変更)or 読取失敗を READ_FAIL として検出し差分計算を続けない。

### D(DRM vblank 完成) — drm_device.{c,h}
- **per-pipe worker**: device単一 `vblank_wq` を撤去し、`parity_drm_vblank_crtc` 各々に `struct parity_kworkqueue worker`+`worker_created` を埋込。正本 drm_vblank_worker_init は CRTC毎生成。
- **正本安全順**: 各pipe: lock/waitq/pipe#/count → **disable timer → seqlock → cleanup action 登録 → worker 生成**。cleanup を worker より先に登録し、worker生成失敗も dev_fini が回収(cleanup は worker_created=0 を許容)。cleanup は worker破棄→timer同期削除→seqlock/inited クリアの順。
- **途中失敗回収**: drmm満杯(cleanup登録不可)→contract即実行+失敗返し、worker生成失敗→当該pipe cleanup登録済ゆえ dev_fini が全pipe逆順回収。ログ `vblank_slots=4`。
- 実機 log: `drm_vblank_init: vblank_slots=4 (per-pipe worker+timer+seqlock)`。

### P(intel_bios_init 実処理) — bios.{c,h} + probe.c
- 正本 intel_bios_init 構造移植: display_devices/bdb_blocks list init → HAS_DISPLAY(ADL-P=true) → init_vbt_defaults → **OpRegion VBT(P2の同起動状態, ASLS==0ゆえ opregion_vbt_present=0=真の不在, 固定NULLでない)** → **not IS_DGFX ゆえ SPI flash path 実行しない** → **PCI ROM を実読取**(config 0x30 base 読取, base==0 は真の不在で即return, 非0なら size probe+`hal_space_map_device`+"$VBT" scan+`intel_bios_is_valid_vbt`+copy) → 有効なら process_vbt(get_bdb_header+version+BDB block walk) / 不在なら **init_vbt_missing_defaults**(PORT_A..F反復, ADL-P phy TC(F..I)を skip → A/B/C の 3 child device 生成, device_type: A=INTERNAL+DP, B/C=TMDS+DP, version=155)。VBT創作/QEMU ROM変更 無し。**未実装でなく実読取結果としての不在**。
- 実機 log: `intel_bios_init: source=0 vbt_found=0 version=155 bdb_blocks=0 child_devices=3 missing_defaults=1`(真の不在→既定3子device)。frontier は `intel_vga_register` へ前進。
- **probe.c**: drm_dev/vbt_state を static化(per-pipe worker で ~1.9KB 増の attach スタックフレーム回避, 既存 static trace/pci と同型, attach は一度に一つ)。

### 試験(GPU-free, ktest 73→83, +10)
- drm 更新(per-pipe): 正常4CRTC+per-pipe worker / dev_fini 全worker破棄 / CRTC数 0・過大拒否 / add_action満杯即実行 / **cleanup登録不可 unwind** / **per-pipe worker生成失敗 unwind**(fault注入 `parity_drm_vblank_test_fail_worker_at`, pipe2失敗→num_crtcs=2, 先行worker回収)。
- bios(+8): is_valid_vbt 受理/NULL・短小・不正署名拒否 / process_vbt が BDB header+block walk(version=200, blocks=2)/ missing_defaults 3子device・version155 / PORT_A=internal非TMDS / **top-level intel_bios_init が fake-pci(ROM BAR0)+opregion不在で既定fallback**(child=3, source=NONE)。

### 実機(参照条件 4GiB/4vCPU/host-phys-bits-limit=39/vfio-pci x-igd-opregion=on,rombar=0)
`A64 TIMECOUNTER READY cpus=4 source=pit hz=~2.497GHz` / **ktest 83/0** / selftest-probe `vblank_slots=4` + `intel_bios_init … child_devices=3 missing_defaults=1` reached=P3 / boot CPUs 4 / panic/fatal/triple 無し。**実機attach: P0 command=0xffff → reached=P1 FAILED pci_bar err=3**(2起動再現)。stale qemu 無し。→ **コード完成・selftest実証済。実機 P0→P3 attach 再確認は GPU wedged(host PCI reset必要, sudo-free で不可)により保留**。static-fix image(sha 86cae223)を chaos に staged。台帳 E-66。

## p011 増分E-67 (2026-09-17): E-66 のブロッカー(実機attach command=0xffff)を根因特定・解消。実機 attach が **reached=P3 BLOCKED intel_vga_register** に到達
E-66 で実機 attach が P0 `command=0xffff` → pci_bar EINVAL となった件を診断・解消。**GPU=vfio-pci 維持、host uptime 2日(E-65 と同一環境, 再起動せず)。**

### 根因(コードでなく実行順 × デバイス idle-suspend)
- host 側デバイスは健全: `setpci COMMAND=0x0003`、`reset_method=flr pm`、FLR/D0 可。→ wedged ではない。
- 診断ビルド(runner の selftest を一時 skip=attach 先行)で **attach が P0 command=**`0x0007`**→P1→P2→P3 BLOCKED intel_vga_register に到達**。→ **parity コードは正しい**。
- 差分は「selftest の所要時間」。パススルー IGD は **idle 時に D3 へ自動 suspend**(config が 0xffff を返す)。E-65 は selftest 73 checks で attach が suspend 窓内に走ったが、E-66 は 83 checks + per-pipe worker の kthread 生成/破棄で所要時間が延び、attach 実行が suspend 窓を超えた。selftest は全 fake backend で実デバイスに触れないので、原因は所要時間のみ。disable_idle_d3=Y / power/control=on / FLR→D0 では不変(guest 実行中の suspend を防げず)。

### 解消(runner.c: attach 先行へ並べ替え)
- **device-sensitive な実機 attach を selftest より先に実行**(attach は起動直後=デバイス D0 のうちに走る)。selftest は GPU-free ゆえ後段で可。両方とも実行。実機ハングや GPU 状態は不変(順序のみ、parity 初期化は不変)。

### 実機再確認(参照条件, host FLR 後 一回起動, image sha 4efb69c7)
`A64 TIMECOUNTER READY cpus=4 source=pit hz≈2.497GHz` / boot `HAL cpu 4, memory 4089MB` / `CPUs ready 4` / panic・fatal・triple 0。
- **実機 attach: P0 `command=0x0007` → P1 uncore BAR mapped(8388608B=8MiB) → P2 `pci_set_master command=0x0007` + `opregion ASLS=0`(真の不在) + `dram_detect rc=0 / bw_init rc=0 / sagv=2`(実 DDR4/帯域) → P3 `drm_vblank_init vblank_slots=4(per-pipe worker+timer+seqlock)` + `intel_bios_init source=0 vbt_found=0 version=155 child_devices=3 missing_defaults`(真の不在→既定3子device) → `reached=P3 outcome=BLOCKED where=intel_vga_register`**。teardown 逆順(DRM+vblank→MSI→WC unmap rc=0)成立。
- selftest: `ktest 83 checks 0 failures`、`source=pit`。runner-result `selftest=PASS probe=BLOCKED last_op=intel_vga_register blocked_at=intel_vga_register cleanup=1 published=0`。
→ **E-66 の T/D/P 実装 + 実行順修正で、実機 attach が P3(新 frontier intel_vga_register)へ到達**。次: `intel_vga_register`→power domains(+_hw)→pmdemand→DMC / KVM 0x40000001 feature 判定+pvclock backend / wait.c 4 fault 試験 + CAS race 試験。台帳 E-60〜E-67。

## p011 増分E-68 (2026-09-17): P3主実装 = intel_vga_register + intel_power_domains_init(xelpd map) + intel_pmdemand_init_early。log分離修正 + KVM clock-feature採取。実機 reached=P3 BLOCKED intel_power_domains_init_hw, ktest 93/0
第35報後の専門家指示(VGA登録→表示系電源管理)を実施。build 0 error/warning、per-pipe vblank/時間error伝播/BIOS既定は保持。GPU=vfio-pci、drm非blacklist、attach先行維持。

### log分離(即修正)
- parity_result に `last_completed` 追加。runner は `last_completed_op = pr.last_completed`(実行済), `blocked_or_failed_op = pr.where`(未実装)を分離。実機 `last_op=intel_pmdemand_init_early blocked_at=intel_power_domains_init_hw`(以前は両方同一)。

### intel_vga_register(vga.{c,h})
- vga_client_register(pdev, intel_gmch_vga_set_decode) を移植。**GPU の PCI class が VGA(0x030000)なら登録、非VGA(secondary)なら -ENODEV 許容, 他error返す**(return0/plane無効化/偽-ENODEV にしない)。decode callback = intel_gmch_vga_set_decode → intel_gmch_vga_set_state が **host bridge 00:00.0(i915->gmch.pdev, drv_pci_find_device で取得)の GMCH_CTRL(ver>=6=0x50)を 16bit masked RMW**(GPU function でない)。enable時 LEGACY|NORMAL IO/MEM, disable時 NORMAL のみ。unregister で callback 解除。実機 `client=registered gmch_bridge=found ret=0`。
- 台帳分離: intel_vga_disable/redisable と /dev/vga_arbiter ユーザIF は未実装(現P3経路に不要)。

### intel_power_domains_init(power_domains.{c,h})
- 設定正規化(sanitize_disable_power_well)、**allowed_dc_mask=get_allowed_dc_mask(ADL-P ver13≥12 → DC9|DC3CO|UPTO_DC6=0xe)**、target_dc_state=sanitize(UPTO_DC6)=0x4、mutex、async_put_work。
- **intel_display_power_map_init**: ADL-P=display ver13 → **xelpd descriptor 群**(always_on / PW_1[DMC,always_on,has_fuses] / DC_off / PW_2[has_vga,has_fuses] / PW_A..D[per-pipe,irq_pipe_mask] / DDI_IO_A..E,TC1..4[icl_ddi_ops] / AUX_A..E,TC1..4[icl_aux_ops])を正本順・属性(always_on/has_vga/has_fuses/irq_pipe_mask/ops-kind)で構成。計 **26 wells**。各wellの power-domain 所属から **domain→wells map** を構成。**refcount/hw_enabled(-1=未sync) は descriptor と別管理**(refcount≠HW ON)。
- 台帳分離(次increment=init_hw 5.2): power-well の enable/disable/sync_hw **MMIO本体**(hsw/icl_ddi/icl_aux/dc_off ops)、hsw.idx/SKL_DISP_PW id の実値、集約well(PW_2/DC_off)の網羅的 per-domain mask(現状は主要 pipe/DDI/AUX/transcoder domain を忠実移植, 深いnest macro展開の残余)。

### intel_pmdemand_init_early
- mutex + waitqueue 初期化(display ver に関係なくこの位置で, 正本順)。

### KVM clock-feature 採取(track C, timecounter.c)
- CPUID **0x40000001 EAX** を採取・記録。実機 `eax=0x1007efb clocksource=1 clocksource2=1 stable_bit=1`。**CLOCKSOURCE2(bit3)利用可を確認** → pvclock backend が正しい将来path(次increment)。「KVM signature ゆえ raw TSC 信頼」の文言を除去し、**signature≠stability, PIT較正TSC を検証済backend として採用(source=pit 維持, pvclock利用済と扱わない)** と reframe。

### 試験(GPU-free, ktest 83→93, +10)
- vga: decode enable=LEGACY+NORMAL / disable=NORMAL / bridge無で set_state=-ENODEV。
- power: domains_init 26 wells + DC mask, PIPE_A→always_on+PW_A, DC_OFF→always_on+DC_off, PW_2 has_vga非always_on, refcount/hw_enabled 分離, cleanup。
- pmdemand: init_early。

### 実機(参照条件, FLR後一回起動, image d64c01c4)
`KVM clock-features eax=0x1007efb (cs=1 cs2=1 stable=1) → PIT-calibrated TSC` / `TIMECOUNTER READY source=pit hz≈2.497GHz` / CPUs ready 4 / panic 0。
- **attach: P0 command=0x0007 → P1/P2 → P3 intel_vga_register(registered, gmch found) → intel_power_domains_init(26 wells, dc=0xe/0x4) → intel_pmdemand_init_early → reached=P3 BLOCKED intel_power_domains_init_hw**。teardown 逆順(power domains map→VGA client→DRM+vblank→MSI→WC unmap rc=0)。
- selftest **ktest 93/0**。runner-result `last_op=intel_pmdemand_init_early blocked_at=intel_power_domains_init_hw cleanup=1 published=0`。
→ 専門家の最低到達(vga越え + power_domains_init/pmdemand_init_early 実処理完了)達成。次: init_hw(icl_display_core_init 5.2) / PCI probe PM契約 + ROM resource取得 / 時間層 fault・CAS試験 + pvclock backend(CLOCKSOURCE2確認済)/ 有効VBT parse深化。台帳 E-60〜E-68。

## p011 増分E-69 (2026-09-17): 電源表を正本へ是正 — DC値=register-bit(0x4000000a/0x2), xelpd well数 26→30(AUX_TBT1-4追加), 全属性(id/hsw.idx/is_tc_tbt/fixed_enable_delay/enable_timeout)を正本値へ。実機 wells=30, ktest 97/0
第36報後の専門家指示「電源表と定義を完成 → 依存を閉じる → init_hw一式」の順の(1)データ是正を実施。build 0 error/warning。GPU=vfio-pci、attach先行維持。

### DC 定義を register-bit 表現へ(独自抽象 0xe/0x4 を廃止)
- `DC_STATE_EN_UPTO_DC5=0x1 / UPTO_DC6=0x2 / DC9=0x8 / DC3CO=0x40000000`(正本 i915)。
- ADL-P(ver13, disable_pw=1, enable_dc=-1): **allowed_dc_mask=0x4000000a**(DC9|DC3CO|UPTO_DC6), **target_dc_state=0x2**(sanitize(UPTO_DC6))。get_allowed_dc_mask / sanitize_target_dc_state を正本ロジックへ。

### xelpd well 表を 30 wells へ(26 は誤: AUX_TBT1-4 欠落)
- 正本 xelpd_power_wells 全 30 instance を順序・属性で構成: always_on / PW_1 / DC_off / PW_2 / PW_A-D / DDI_IO_A,B,C,D,E,TC1-4(9) / **AUX_A,B,C,D,E(5) + AUX_USBC1-4(4) + AUX_TBT1-4(4)**。前回は AUX を A-E + TC1-4=9 とし **AUX_TBT を欠落**(→13 が正)。
- **属性を正本値で保存**(配列位置から推測しない): id(DISP_PW_ID_NONE=0 / SKL_DISP_PW_1=8 / PW_2=9 / DC_OFF=11)、**hsw.idx**(PW_2=1,PW_A=5,DDI_A=0,DDI_D=7,AUX_A=0,AUX_TC1=3,AUX_TBT1=9…i915_reg.h 実値)、has_vga(PW_2)、has_fuses(PW_1/PW_2/PW_A-D)、irq_pipe_mask(PW_A-D)、**is_tc_tbt(AUX_TBT1-4)**、**fixed_enable_delay(AUX_A-E/USBC1-4)**、**enable_timeout=500(AUX_USBC1-4, WA_14017248603)**、ops-kind(hsw/icl_ddi/icl_aux/dc_off)。
- **NULL list=所属なし vs 長さ0 list=全domain を区別**: always_on=domains_all(全), PW_1=always_on だが所属 NULL(none)。domain bitmap 幅=POWER_DOMAIN_NUM。
- 集約 domain mask(PW_2/DC_off)を正本 macro 展開で構成(PW_2=PW_B+PW_C+PW_D+DC_OFF_PORT+INIT, DC_off=DC_OFF_PORT+PW_C+PW_D+DSI+AUDIO+AUX_A/B+DC_OFF+INIT)。domain→wells map 構成。parity_power_well_by_id 追加。

### 試験(GPU-free, ktest 93→97, +4)— 期待値を正本から更新
- 26/0xe/0x4 を回帰正解に残さず、**30 / 0x4000000a / 0x2** へ。追加: AUX_TBT4 is_tc_tbt+hsw.idx=12 / AUX_USBC1 fixed_enable_delay+enable_timeout=500+idx=3 / by_id(PW_1)=1,(PW_2)=3 & PW_2 idx=1,PW_A idx=5 / always_on=all-domains, PW_1=NULL(none), DDI_LANES_TC4 が well へ mapping。

### 実機(参照条件, FLR後一回起動, image 9df2873f)
`intel_power_domains_init: wells=30 allowed_dc=0x4000000a target_dc=0x2 disable_pw=1 (xelpd map)` / **ktest 97/0** / reached=P3 BLOCKED intel_power_domains_init_hw / last_op=intel_pmdemand_init_early / CPUs 4 panic 0 cleanup=1 published=0。
→ (1)電源表/定義の正本是正 完了・HW検証。**次(同増分の続き, staged): (2)PCI probe PM契約 + 時間 fault 4試験 + pvclock backend(cs2確認済)+ 高分解能 sleep-range + ROM resource(guest PCI層に PCI_ROM_RESOURCE 抽象が無く層拡張要) / (3)well ops(get順/put逆順/refcount/ops-by-kind/post-enable VGA・IRQ・fixed-delay IS_DG2条件) / (4)intel_power_domains_init_hw(false)=icl_display_core_init(DC無効化/PCH/combo PHY/PW1/CDCLK stepping table/DBUF/MBUS/BW_BUDDY)+INIT参照保持+sync_hw / (5)cleanup 状態機械(driver_remove対応)。** 台帳 E-60〜E-69。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-70 (2026-09-17): 4単位のうち A(PCI probe runtime PM契約) + B(時間fault注入口+4試験) + C(power-well操作本体) を実装・実機検証。ktest 119/0, reached=P3 BLOCKED intel_power_domains_init_hw
第37報後の専門家指示(順序固定 A→B→C→D, 独立単位を順に閉じる)のうち **A/B/C を完成**。build 0 error/warning。GPU=vfio-pci、attach先行維持、電源表(30 wells/DC 0x4000000a/0x2)保持。

### A: PCI probe を囲む runtime PM 契約(既存 osdep_rpm を runner/probe へ接続)
- **local_pci_probe 相当**: attach 冒頭(P0 より前, osdep_pci_init 直後)で **osdep_rpm(pci-probe-pm backend)を get_sync → device を D0 へ resume**、teardown 最後(全 MMIO/PCI 解放後)に put。新 PM 実装は作らず既存 osdep_rpm を接続。
- **3参照を別管理**: device 生存参照(runner の g_runner.device)/ PCI probe runtime PM 参照(新規, get_sync/put)/ i915 runtime_pm(P0.3 の init_early)。get_sync は resume 失敗でも usage を残す(teardown で解放)契約を保持。継続(publish)経路は参照を次所有者へ渡す旨をコメント化(診断は常に teardown ゆえ put)。
- **新 backend**(backend_mmio.c): resume = **osdep_pci_set_power_state(D0) の実処理**(PM cap 有→PMCSR RMW / 無→正当な no-op。空stubでない)。suspend=D3hot。
- trace: `pci_probe_runtime_pm_get_sync`(usage/resume_rc)+ config vendor 読取。実機 `PM probe get_sync: cfg_vendor=0x8086 usage=1 active=1 resume_rc=0`、teardown `PCI probe runtime PM released (usage=0)`。
- 試験(fake PM backend, ktest +4): 正常 get_sync(usage++/active) / put(usage--)/ **get_sync は resume 失敗で usage 残す** / **resume_and_get は resume 失敗で usage 巻き戻す**(get_sync と区別)。resume 失敗経路は active=0 を与えて backend を実際に呼ぶ。

### B: 時間 fault 注入口 + 既決4試験(実 wait/udelay/reset 本体を使用)
- **CAS 更新 helper を抽出**(timecounter-policy.c: `amd64_timecounter_monotonic_max(slot,raw)` = guarded reader が実使用)+ **boot self-test**(bsp_prepare で一度): 120→110 で共有最大が後退せず(PASS)、110→120 で前進(PASS)。実機 `A64 TIMECOUNTER CAS-SELFTEST 120-then-110=PASS 110-then-120=PASS`。
- **wait.c に時間源注入境界**: `parity_time_test_ops{read, slow_sleep_override, slow_sleep_rc}` + `parity_wait_test_set()`。本番は NULL→kern_rtc、試験は scripted context。全時間読出/slow-sleep を境界経由に。**実 probe の時間源は壊さない**(試験後 set(0))。
- 試験(ktest +4, fake MMIO + scripted time): (2a)counter 読出失敗→ -EIO(正常/HW timeout に化けない) (2b)freq 変化→ -EIO (3)**reset の udelay で counter 失敗→ parity_gt_reset_all が -EIO で中断・lock/forcewake 解放・retry せず** (4)slow-stage の wait API 異常(EINVAL)→ 無視せず -EIO 伝播。
- pvclock backend(cs2 確認済)/ 高分解能 sleep-range は正本移植範囲に残置(次)。PIT 較正 TSC 保持。

### C: power-well/domain 操作本体(fake MMIO 試験)
- **低水準 ops と refcount ops を分離**: enable/disable/is_enabled/sync_hw と get/put を別関数。get() は 0→1 で enable()、明示 enable() は refcount 不変(display-core 用の別入口)。
- **ops 本体(MMIO)**: driver 制御レジスタ族別(hsw=0x45404 / icl_aux=0x45444 / icl_ddi=0x45454)、REQ=0x2<<(idx*2)/STATE=0x1<<(idx*2)。enable=REQ set→**ACK(STATE)待ち=parity_wait_reg**→post_enable。disable=REQ clear→STATE clear 待ち。is_enabled=STATE 読取。sync_hw=is_enabled 呼出後に記録(bare readback でない, hw_enabled=-1 を真偽扱いしない)。
- **省略しない3点**: ①post_enable が has_vga で **intel_vga_reset_io_mem 呼出**(vga.c 追加, legacy-IO 本体は模型・呼出接続済)、pipe mask で IRQ post-enable。②IRQ post-enable は intel_irqs_enabled() gate(P4前=0 で guarded no-op, P4 handler 前倒し無し)。③fixed_enable_delay の固定待機は IS_DG2 条件—ADL-P 非DG2 ゆえ ACK 待ち(enable_timeout=500 は WA 反映)。
- domain get(所属 well 昇順)/put(逆順)、途中失敗で unwind。
- 試験(ktest +14, fake MMIO に PW 制御レジスタ+ACK 模型・no_ack 追加): single get→put / nested get 二重・put で一度 disable / 明示 enable は refcount 不変 / sync_hw が状態記録 / **2 domain が PW_A 共有→refcount 2, 個別 put** / post_enable が PW_2(has_vga)で reset_io_mem 呼出・PW_A で IRQ gate off / **ACK 欠落= -ETIMEDOUT と time-base 異常= -EIO を区別**。

### 実機(参照条件, FLR後一回起動, image b36f8a5d)
`CAS-SELFTEST PASS` / `KVM clock-features cs2=1` / `TIMECOUNTER READY source=pit` / `PM probe get_sync cfg_vendor=0x8086 usage=1 active=1` / **ktest 119/0** / reached=P3 BLOCKED intel_power_domains_init_hw / teardown `PCI probe runtime PM released (usage=0)` / CPUs 4 panic 0 cleanup=1 published=0。
→ A/B/C 完了。**残(D, 次): intel_power_domains_init_hw(false)=icl_display_core_init(DC無効化→PCH→combo PHY→PW1→CDCLK stepping→DBUF[ADL-P early-return]→MBUS[ADL-P early-return]→BW_BUDDY)+POWER_DOMAIN_INIT参照保持+sync_hw+cleanup状態機械 / ROM resource(PCI層拡張) / pvclock+sleep-range / VGA legacy-IO 本体。** 台帳 E-60〜E-70。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-71 (2026-09-17): 単位1=C(power-well ops)を正本の個別opsへ是正(D接続前)。sync_hw BIOS引継ぎ/is_enabled REQ+STATE/ACK警告継続/disable他要求元/VGA実I/O。ktest 124/0
第38報後の専門家指示「D前にまず C を正本の個別opsへ是正」を実施。A(PCI probe PM)・B(時間fault)は保持。build 0 error/warning、reached=P3 BLOCKED intel_power_domains_init_hw 維持。

### C の是正(power_domains.c / vga.c)
- **sync_hw**: bare readback を廃し正本 `intel_power_well_sync_hw` へ = **ops->sync_hw()(HSW: BIOS→driver 要求引継ぎ)実行 → is_enabled() → hw_enabled 保存**。HSW handoff: BIOS が REQ 保持なら **driver REQ を先に立て(未設定時)、その後 BIOS REQ 解除**(逆順禁止、count==0で省略せず、SW refcount 不変)。BIOS reg = driver reg − 4(CTL1=CTL2−4)。
- **is_enabled(ADL-P)**: driver reg の **REQ と STATE 両方**立つ確認(STATE だけでない)。DC_off は自 state、always_on は 1。
- **disable**: 自分が最後の要求元のときだけ STATE clear 待ち。**BIOS/KVMR が REQ 保持なら STATE 残置を許容**(無条件 STATE=0 待ちにしない)。hw_enabled=is_enabled(driver 所有反映)。
- **ACK timeout 扱い**: enable/disable は void 相当。**実HW ACK timeout(-ETIMEDOUT)は警告して継続**(AUX は timeout 予期、ack_timeouts 記録、呼出元を失敗させない=domain get unwind しない)。**時間源異常(-EIO)のみ停止**。B の時間 fault 伝播は保持。
- **timeout 単位**: enable_timeout=500 は `parity_wait_reg(…, slow_ms=enable_timeout)` へ(ms/slow、fast_us へ渡さない)を確認。
- **VGA reset_io_mem 実I/O本体**(vga.c): arbiter client 登録時のみ **kern_io_out8(0x3C2, kern_io_in8(0x3CC))**(VGA_MIS_R→VGA_MIS_W)。GPU MMIO 書込/plane 無効化で代用せず。未登録(GPU-free 試験は client 無)は no-op ゆえ stray IO 無し。

### 試験(fake MMIO を BIOS/driver REQ 分離+STATE=任意要求元 へ改修、ktest 119→124/0)
誤期待値を正本から修正+追加: is_enabled 00/01(STATE=BIOS のみ)/11 区別 / **sync_hw が BIOS 要求引継ぎ(driver REQ 立て→BIOS 解除、refcount 不変)** / handoff 不要時は is_enabled 記録 / **disable が BIOS 保持 well を許容(STATE 残・driver 所有 off)** / **ACK 欠落=警告継続(enable 0, ack_timeouts=1)** / 時間源異常=-EIO / get・put refcount / 明示 enable refcount 不変 / 2 domain PW_A 共有 / post_enable VGA(PW_2)・IRQ gate off。

### 実機(参照条件, FLR後一回起動, image a5c2397b)
`CAS-SELFTEST PASS` / **ktest 124/0** / `PM probe get_sync cfg_vendor=0x8086 usage=1 active=1` / reached=P3 BLOCKED intel_power_domains_init_hw / `PCI probe runtime PM released usage=0` / CPUs 4 panic 0 cleanup=1 published=0。
→ C 是正完了(実 GPU 未接続=fake MMIO 試験)。**残(D, 次): (2)D依存 combo PHY(intel_combo_phy_init+PHY列挙)/CDCLK(early probe hook+ADL-P stepping table+init_hw 読出/sanitize/必要時変更/cdclk.hw保存)/PCODE(skl_pcode_request 前+PCODE write 後を共通 PCODE へ, sb_lock→送信→reply mask→再要求→timeout再試行)/DBUF(ADL-P early-return だが有効化全省略でない)/MBUS(ADL-P early-return)/BW_BUDDY(P2 DRAM)。(3)icl_display_core_init(false)[DC無効化→PCH+reset handshake→combo PHY→PW1明示enable→CDCLK→DBUF→MBUS→BW_BUDDY→末尾]→POWER_DOMAIN_INIT参照保持(disable_power_well=1では disable_wakeref分岐なし)→intel_power_domains_sync_hw(全wellに是正済個別ops)→initializing=false。cleanup=driver_remove対応(well有効+runtime PM参照, 全OFF/refcountゼロ代入しない, 最後のPM put が使う PCI device/backend の寿命確認)。未実装子処理は先に実装+fake backendで閉じてから実GPU接続。到達=init_hw(false)完了+INIT参照保持で intel_dmc_init入口。** 並行: pvclock(cs2確認済)/高分解能sleep-range(clocksource≠clockevent)/ROM resource/有効VBT parse。台帳 E-60〜E-71。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-72 (2026-09-17): D1=共通PCODE の skl_pcode_request / snb_pcode_write を実装(CDCLK変更の通信経路)。GPU-free 4試験、ktest 128/0
第39報後の専門家指示「D0(fuse/DC_off/VGA補完)と並行、CDCLK前にまず共通PCODEを実装」に従い **D1 を実装**。build 0 error/warning、reached=P3 BLOCKED intel_power_domains_init_hw 維持。C是正(E-71)・A/B は保持。

### 共通PCODE 拡張(pcode.c/h、新lock/別mailbox 作らず既存拡張)
- **snb_pcode_write_timeout / snb_pcode_write**: 既存 parity_snb_pcode_rw(is_read=0) を sb_lock 下で。write = timeout(500us,0)。
- **skl_pcode_try_request**: `__snb_pcode_rw(mbox,&request,NULL,500,0,true)`(atomic=fast のみ)→ 読戻し値で `(reqval & reply_mask)==reply` 判定、*status に txn 結果。
- **skl_pcode_request**: **最初の要求を明示送信**(prime)→ reply mask 条件確認しながら **timeout_base_ms 再要求 poll** → 不成立なら **50ms bounded busy 追加 poll**(preemption無効相当だが **IRQ無効長時間spin にしない**=preemption残し bounded busy、高分解能sleep-range 未実装ゆえ)→ 全出口で lock 解放 → `status ? status : ret`(PCODE status 優先、無ければ待機結果)。
- **エラー区別**: **時間源異常(-EIO)は即停止・lock解放**(通常 PCODE timeout に化けさせない)。PCODE status errno はそのまま伝播。**C の「ACK警告継続」を PCODE 全体へ広げない**(呼出元の正本準拠)。

### 試験(GPU-free scripted PCODE、ktest 124→128/0)
fake mmio に pcode_sticky_status(恒常ステータス)/ pcode_no_ready(READY 維持)追加。4ケース: **最初の要求で承認(再要求せず, txn=1)** / **数回後承認(毎回正 request, txn=3)** / **恒常 PCODE エラー→errno 伝播(-EIO/-ETIMEDOUT に化けない)** / **時間基盤異常→-EIO 停止・lock 解放**(READY 維持で poll が fault へ入る)。

### 実機(参照条件, FLR後一回起動, image 35d88cb4)
`CAS-SELFTEST PASS` / **ktest 128/0** / reached=P3 BLOCKED intel_power_domains_init_hw / CPUs 4 panic 0 cleanup=1 published=0。
→ D1 完了(GPU-free 検証、実 GPU 未接続=専門家指示どおり D 全体接続後に一回起動)。**残(D 継続): D0(fuse待ち実poll[PW1=PG0前+PG1後]/DC_off gen9_set_dc_state・gen9_dc_off ops[enable=disable_dc_states, is_enabled=DC_STATE_EN 読, DMC payload 確認]/VGA vga_get・vga_put 所有権)/ D2(combo PHY intel_combo_phy_init+PHY列挙, CDCLK early-probe hook+ADL-P stepping table+init_hw 読/sanitize/PCODE変更準備要求→PLL設定→変更後通知→cdclk.hw保存, DBUF gen9_dbuf_enable, MBUS ADL-P early-return, BW_BUDDY P2 DRAM)/ D3(icl_display_core_init(false) 親順+POWER_DOMAIN_INIT参照保持+全well個別sync_hw+cleanup=driver_remove対応)。DC_off の disable_dc_states は CDCLK/DBUF/combo PHY(D2)へ接続。fake統合後 実GPU一回で init_hw(false)完了+INIT参照保持で intel_dmc_init入口。** 台帳 E-60〜E-72。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-73 (2026-09-17): D1是正=PCODE 再試行の待機・preemption 契約を正本へ。適合層に kern_preempt_disable/enable(scheduler)+ kern_usleep_range を追加。ktest 128/0, boot健全
第40報後の専門家指示「PCODE の追加50ms を preemption 有効の busy poll のままにしない」を実施。combo PHY/CDCLK は次(残)。build 0 error/warning、CPUs 4 panic 0、reached=P3 BLOCKED intel_power_domains_init_hw。

### 適合層(kernel)追加
- **kern_preempt_disable/enable**(src/kern/sched.c, include/kern/sched.h): sched_cpu に **preempt_count**(nesting)追加。disable=count++、enable=count--; 0 かつ need_resched で sched_yield。**scheduler tick の preempt 判定を preempt_count!=0 で defer**(need_resched を残し、enable で解消)。全出口で入口状態へ復元。IRQ禁止でなく preemption のみ(IRQ禁止と別)。boot 健全確認(scheduler 変更が回帰なし)。
- **kern_usleep_range(min_us,max_us)**(src/kern/clock.c, include/kern/clock.h): monotonic counter による min_us の bounded 高分解能待機。**真の yielding hrtimer sleep-range は HAL 保留項目**(µs 規模 usleep_range に供する、10ms tick 待ち/無間隔 busy loop でない)。

### PCODE 再試行を三層へ(pcode.c)
- **一回の mailbox 取引**: 現 500µs/slow=0 保持。
- **通常再要求区間**(_wait_for 相当): parity_pcode_poll(sleep_between=1)= 未成立時 **kern_usleep_range(10,20)** で sleep 可能待機。
- **追加再要求区間**(preempt_disable→50ms→preempt_enable 相当): **kern_preempt_disable() → parity_pcode_poll(sleep_between=0, 50ms, sleep なし) → kern_preempt_enable()**。**IRQ 長時間無効化にしない**(preemption のみ)。全出口で preempt 復元・sb_lock 解放。時間源異常(-EIO)は即停止、PCODE status errno 伝播(C の「ACK 警告継続」を PCODE 全体へ広げない)。

### 試験(ktest 128/0 維持)
既存 D1 4試験は通常/追加区間を経由: **最初承認(再要求なし)/数回後承認(毎回正 request)/恒常 PCODE エラー→errno 伝播(通常区間 timeout→追加区間 preempt-off 50ms→errno 返却)/時間基盤異常→-EIO 停止・preempt と mutex 復元**。追加区間 + preempt disable/enable balance は PCODE-error 試験が実行(50ms preempt-off, boot 健全=scheduler defer 動作)。

### 実機(参照条件, FLR後一回起動, image 812b7333)
CPUs 4 / panic 0 / **ktest 128/0** / reached=P3 BLOCKED intel_power_domains_init_hw / cleanup=1 published=0。
→ D1 完了(待機・preemption 契約含む)。**残(D 主成果): combo PHY(intel_combo_phy_init→icl_combo_phys_init: PHY列挙→verify_state→不一致で procmon/PHY_MISC/lane・group/master/COMP_INIT 初期化, fake 3ケース[全適切/COMP立つがprocmon不一致/初期化要])/ CDCLK 三段(hook・table選択[intel_init_cdclk_hooks, ADL-P stepping]/現値取得・sanitize[bxt_sanitize_cdclk, 適切なら変更せず]/変更[計算→PCODE変更準備→PLL/divider→変更後通知→cdclk.hw保存, 準備失敗で後続PLL進めず], fake 4ケース)/ D0(fuse実poll/DC_off gen9_set_dc_state/VGA vga_get-put)/ D3(icl_display_core_init(false)+INIT参照保持+全well sync_hw+cleanup driver_remove対応)。fake統合後 実GPU一回で init_hw(false)完了+INIT参照保持で intel_dmc_init入口。D未完中は GPU渡さず GPU-free 試験のみ(定期attach確認起動を省く)。** 台帳 E-60〜E-73。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-74 (2026-09-17): D2主作業=combo PHY 関数本体移植(intel_combo_phy_init/verify_state/procmon, ADL-P)。GPU-free 検証(probe=NOT_RUN, ktest 134/0)
第41報後の専門家指示「主作業=combo PHY 本体移植」を実施。preemption制御(E-73)/PCODE三層/BIOS引継ぎは保持。build 0 error/warning。**実機は GPU渡さず構成で検証**(D未完中は実device P0→P3を実行しない、の指示に従う)。

### combo PHY 移植(combo_phy.{c,h})— 正本 intel_combo_phy.c + intel_combo_phy_regs.h 基準
- **parity_intel_combo_phy_init → for_each_combo_phy(ADL-P: PHY_A,B)**: 各PHY verify_state→適切なら skip、不一致だけ init_one。return=初期化した本数。
- **verify_state**(icl_combo_phy_verify_state): combo_phy_enabled(PHY_MISC power-down clear && COMP_DW0 COMP_INIT)→ DISPLAY_VER≥12 の TX_DW8_LN0(ODCC SEL|DIV2)/PCS_DW1_LN0(DCC RUN_DCC_ONCE)→ procmon(DW1 mask/DW9/DW10)→ master(PHY_A)なら COMP_DW8 IREFGEN → CL_DW5 CL_POWER_DOWN_ENABLE。**COMP_INIT だけで初期化済判定しない**(全設定照合)。
- **init_one**(icl_combo_phys_init body): PHY_MISC(DE_IO_COMP_PWR_DOWN clear)→ TX_DW8_GRP(ODCC)→ PCS_DW1_GRP(DCC)→ set_procmon(DW1 rmw/DW9/DW10 write)→ master IREFGEN(DW8 rmw)→ COMP_INIT(DW0 rmw)→ CL_POWER_DOWN_ENABLE(CL_DW5 rmw)。**lane側読(LN0)→group側書(GRP)を別offsetで保持**。
- **procmon**: COMP_DW3 の process(bits28-26)/voltage(25-24) 情報から icl_procmon_values[5](0.85V/0.95V/1.05V × dot0/1, dw1/dw9/dw10 正本値)を選択(未知は 0.85V dot0 へ fallback=正本 MISSING_CASE)。ADL-P固定/GPU周波数推測 しない。
- ADL-P: has_phy_misc=全PHY true、phy_is_master=PHY_A のみ(JSL/EHL/RKL/DG1/ADL-S 分岐は非該当)。REG offset: COMBOPHY_A=0x162000/B=0x6C000, COMP=+0x100+4dw, CL=+4dw, PCS_GRP=+0x600/LN0=+0x800, TX_GRP=+0x680/LN0=+0x880, PHY_MISC 0x64C00/04。

### 試験(GPU-free, fake MMIO に generic offset store + GRP→LN(0) broadcast 追加, ktest 128→134/0)
- verify: COMP_INIT無=未enabled で false / **COMP_INIT だけ(procmon不一致)で false**(初期化済扱いしない)。
- **init_one が COMP_INIT/IREFGEN(master A)/CL_POWER_DOWN/procmon(DW9=0x62AB67BB)を正しいreg/mask/順序で書く**。init_one 後に verify_state=1(GRP→LN broadcast 反映)。
- top-level: 全適切なら再init 0本、未設定なら 2本(A,B)。期待値は正本から作成。lane/group を同変数に潰さず broadcast 効果を fake で表現。

### 実機(GPU渡さず構成 run-parity-nogpu.sh: vfio-pci device 除去, image 2e4254f5)
`CAS-SELFTEST PASS` / **ktest 134/0** / **probe=NOT_RUN**(GPU非パススルー→ runner SYNC_ONLY, 実device P0→P3 実行せず)/ CPUs 4 / cleanup=0 published=0。
→ combo PHY 完了(GPU-free 検証)。実GPU への接続は D 本来位置(icl_display_core_init)で後接続。**残(D 続): CDCLK三段(hook/table+現値取得sanitize+PCODE前後変更)/ D0(fuse実poll・DC_off gen9_set_dc_state・VGA vga_get-put)/ D3(icl_display_core_init(false)+INIT参照保持+全well sync_hw+cleanup driver_remove)。並行: sleep-range 実backend(hrtimer/clockevent+waitq)、preemption 実scheduler試験(実IRQ→切替要求→延期→enable→切替)、PCODE追加区間2試験。fake統合後 実GPU一回で init_hw(false)完了+INIT参照保持で intel_dmc_init入口。** 台帳 E-60〜E-74。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-75 (2026-09-17): 主作業=CDCLK本体移植(hook選択→現値取得bxt_get_cdclk→sanitize→PCODE前後変更bxt_set_cdclk)。combo PHY返却契約是正。GPU-free 140/0 (image f40715595)
第42報後の専門家指示「主作業=CDCLK(hook/table→read+sanitize→変更)を一続きで」を実施。combo PHY(E-74)は保持し返却契約のみ是正。build 0 error/warning。**実機はGPU渡さず構成(run-parity-nogpu.sh, chaos, ADL-P 8086:46a8 rev0c を vfio-pci に残置)で検証**。

### 順序1: combo PHY 親接続の返却契約是正(初期化本数をエラーと取り違えない)
- `parity_intel_combo_phy_init(m, trace, unsigned *initialised_out)` を **戻り値0固定(正本void契約=常に成功)** に変更。初期化本数は out-param(診断)へ分離。D3親は return==0 を成功判定でき、`>0` をエラー誤読しない。
- ktest 2件を更新: 全適切→ `rc==0 && n==0`、未設定→ `rc==0 && n==2`(**2本初期化でも rc==0 で親続行**を明示検証)。

### 主作業: CDCLK(cdclk.{c,h} 新規)— 正本 intel_cdclk.c 基準、ADL-P(display ver 13, HAS_CDCLK_CRAWL=1 / HAS_CDCLK_SQUASH=0)
- **device状態**: `parity_cdclk_dev{ hw(config: ref/vco/cdclk/bypass/voltage_level), table, funcs, display_ver, has_cdclk_crawl/squash, m, sb_lock, 診断7項 }`。config は kHz、vco 特殊値=0(PLL off)/~0u(unknown=full再設定)を型で保持。
- **hook選択** `parity_intel_init_cdclk_hooks`: `parity_adlp_display_step(revid)` で adlp_revids[] を引き **rev 0x0c → STEP_D0**(生revを stepping に流用しない)。STEP_D0∉[A0,B0) → **adlp_cdclk_table + tgl_cdclk_funcs**、crawl=1/squash=0。RPL-U(別SKU)は非該当。
- **現値取得** `bxt_get_cdclk`: icl_readout_refclk(SKL_DSSM refclk 19.2/24/38.4)→ bxt_de_pll_readout(BXT_DE_PLL_ENABLE の PLL_ENABLE|LOCK 判定、ratio→vco。未LOCKは vco=0)→ ver≥12 で bypass=ref/2 → vco==0 は cdclk=bypass、それ以外は CDCLK_CTL CD2X div sel で cdclk=vco/div。squash=0 なので squash_ctl 読まず。voltage=tgl_calc_voltage_level。
- **sanitize** `bxt_sanitize_cdclk`: intel_update_cdclk 後、vco==0 か cdclk==bypass なら再設定へ。DPLL可なら CDCLK_CTL を読み pipe field(CD2X_PIPE_NONE)を無視、calc_cdclk/calc_pll_vco/skl_cdclk_decimal|cd2x_div_sel で expected を再構成し一致すれば無変更。不一致は **cdclk=0 / vco=~0u**(~0 を実周波数計算に使わない)。
- **変更** `bxt_set_cdclk/_bxt_set_cdclk`: 共通PCODE `skl_pcode_request(CDCLK_CONTROL, PREPARE, READY, READY, 3)` で PCU 準備 → **失敗なら PLL/CDCLK_CTL を書かず return(CD-3)** → crawl(hw.vco>0&&new>0&&!unknown で adlp_cdclk_pll_crawl、それ以外 icl_cdclk_pll_update=disable/enable、LOCK 待ちは parity_wait_reg)→ CDCLK_CTL(cd2x_div_sel|PIPE_NONE|decimal, INVALID_PIPE で vblank 待ちなし)→ `snb_pcode_write(CDCLK_CONTROL, voltage_level)` 通知 → **失敗なら HW済を巻戻さず状態も requested で上書きしない(CD-4)** → intel_update_cdclk → voltage_level=requested。squash=0 で midpoint(crawl+squash 両要)は常に不成立=単段。
- **init_hw** `intel_cdclk_init_hw→bxt_cdclk_init_hw`: sanitize → cdclk!=0&&vco!=0 なら無変更(diag_no_change=1) → 否なら calc(min)→set_cdclk。intel_cdclk_init(後段P3)ではない。P2 DRAM/BW/電源状態は破壊しない。

### 試験(GPU-free, fake に BXT_DE_PLL_ENABLE の LOCK/FREQ_REQ_ACK モデル追加, ktest 134→140/0)
- hook: rev0x0c→STEP_D0、adlp_table+tgl、crawl1/squash0。
- readout: DSSM=38.4/PLL ratio34(locked)/CD2X÷2 → **ref38400 vco1305600 bypass19200 cdclk652800 voltage3** を復号。
- **CD-1 変更不要**: 合法 pre-OS 状態 → init_hw が無変更(cdclk/vco 保持、HW書込なし、diag_no_change=1)。
- **CD-2 sanitize後再設定要**: PLL off → sanitize が **cdclk=0/vco=~0** を強制(~0 を実周波数化しない)。
- **CD-3 prepare-fail**: PCU が PREPARE 拒否 → prepare_status≠0 かつ **PLL/CDCLK_CTL 未書込**(hw_sequence_reached=0)。

### 実機(GPU渡さず run-parity-nogpu.sh, chaos, image f40715595)
`CAS-SELFTEST 120/110 双方向 PASS` / **ktest 140/0**(134→140 = 新規CDCLK 6件、失敗0)/ **probe=NOT_RUN**(SYNC_ONLY, 実device P0→P3 実行せず)/ selftest=PASS / cleanup=0 published=0。CD-3 は実機で注入PCODE status→err -42 が伝播(時間基盤正常)。
→ CDCLK read/sanitize/prepare-fail 検証済。**残: CD-2変更成功全経路(crawl/PLL/CDCLK_CTL/notify)+CD-4 notify-fail の fake 接続、D0(fuse実poll・DC_off gen9_set_dc_state・VGA vga_get-put)、D3(icl_display_core_init(false)+INIT参照+全well sync_hw+cleanup)。並行: sleep-range 実backend(hrtimer/clockevent+waitq)、preemption 実scheduler試験、PCODE追加区間2試験。** 台帳 E-60〜E-75。GPU=vfio-pci維持, drm非blacklist, attach先行維持, 描画/hang探索へ戻らず。

## p011 増分E-76 (2026-09-17): CDCLK変更経路を fake で最後まで実行(CD-2a 完全再設定 / CD-2b crawl / CD-4 通知失敗の部分更新)。per-txn PCODE fake + 書込みtrace。GPU-free 145/0 (image 14745a2a)
第43報後の専門家指示「CD-2変更成功とCD-4通知失敗を fake で最後まで通し、PLL/CDCLK_CTL の変更列を検証」を実施。CDCLK本体(E-75)は保持し、未実行経路を通す段階。build 0 error/warning。**実機はGPU渡さず構成(run-parity-nogpu.sh, chaos)で検証**。

### 順序1: fake を「変更途中まで成功」構成にする
- **per-txn scripted PCODE**(`struct fake_pcode_txn{exp_mbox, exp_data, resp_data, resp_data1, status, ready_delay}`): 全取引一律statusを廃し、**PREPARE成功→HW変更→最後の通知だけ失敗** を同一実行で起こせる。MAILBOX書込みで期待コマンド/入力データを照合(不一致・想定外取引・未消費で ptxn_bad=1 → 試験失敗)。ready_delay で READY 解除を遅らせ待機を実際に使わせる(既定0=即時)。既存 script/sticky/no_ready は ptxn==NULL 時のfallbackとして保持。
- **PLL 要求/応答分離**(E-75の LOCK/FREQ_REQ_ACK モデル保持): driver書込み値をそのまま読出さず、fakeが LOCK(PLL_ENABLE従属)/FREQ_REQ_ACK(FREQ_REQ従属)を付与。
- **順序付き書込みtrace**(`wt_off/wt_val/wt_n` + `fake_wt_find(off,val,mask)`): 本番関数の操作列を記録。テスト側が最終状態を直接書いて成功を作らない。

### 順序2: CD-2 を sanitize確認から変更成功へ(完全再設定とcrawlは別試験)
- **CD-2a(init_hw() から完全再設定を最後まで)**: PLL-off 入力 → sanitize が cdclk=0/vco=~0 → 変更経路。38.4MHz fixture 期待値(公開adlp_cdclk_table最小値選択、fake期待値であり本番固定値ではない): ref38400/bypass19200/**cdclk179200/vco537600**/ratio14/除数3/**voltage0**。合格: PREPARE成功→**PLL無効化(PLL_ENABLE clear書込)→有効化(ratio14→PLL_ENABLE)→CDCLK_CTL=0x780164(decimal(179200)|div1.5|PIPE_NONE)→通知→状態更新**、pll_en<ctl の順序、hw.cdclk=179200/vco=537600/voltage=0。書込みtraceと返却フィールドで検査(hw_sequence_reached だけに依存しない)。
- **CD-2a variant**: PLL LOCK中だが CDCLK_CTL decimal不整合 → 現値非ゼロでも unknown を経て完全再設定へ(cdclk179200/vco537600)。
- **CD-2b(setter から crawl)**: 既知有効値(cdclk307200/vco614400)→要求(cdclk556800/vco1113600/voltage2)を bxt_set_cdclk へ。**PLL無効化せず** ratio29 + FREQ_REQ → LOCK|ACK → FREQ_REQ解除。CDCLK_CTL=0x380458。合格: pll_dis 不在 / FREQ_REQ 存在 / hw.cdclk556800・vco1113600・voltage2。**完全再設定とcrawlは別trace**(最終周波数一致でまとめない)。
- **CD-2b guard**: 現VCO==要求VCO → 不要なcrawl要求を出さない(0x46070 書込みゼロ)。
- CD-2-sanitize(E-75, cdclk=0/vco=~0)は CD-2-sanitize として残置(変更成功の代替にしない)。

### 順序3: CD-4 通知失敗は「全不変」でなく部分更新を検証
- CD-2a fixture 複製、PREPARE/PLL応答成功、**最後のPCODE通知だけ失敗**(注入 raw status 0x11 → -EACCES)。
- 合格(memcmp!=0を使わず個別比較): PREPARE成功 / PLL・CDCLK_CTL 実書込あり / notify=指定errno(-EACCES) / HW巻戻しなし / **最後の intel_update_cdclk 未実行** → **hw.vco=537600(PLL helper が更新済)だが hw.cdclk=0(update_cdclk 未実行ゆえ179200にならない)/ voltage_level≠要求値2(上書きされない)**。「要求設定を丸ごとコピーしない」と「一切不変」の区別を厳密化(helper の途中更新 hw.vco は正本と一致)。生status(0x11)と変換後errno(-EACCES=zedBSDでは-25)を区別、時間基盤異常(-EIO)と別扱い。

### 順序4: combo PHY の void 契約説明を是正(採用は継続)
- out引数分離(E-75)は保持。ただし「void=常に成功」ではなく「戻り値で成否を返さない契約」と明記。D3接続時は return 0 に頼らず、**正本処理が戻った後に既存fault状態(parity_wait_time_base_faulted 等)を親で確認**し、MMIO/時間源/未実装異常なら次のHW操作へ正常続行しない、という局所方針を D3 実装で用いる(本増分は方針確定、実接続は D3)。

### 実機(GPU渡さず run-parity-nogpu.sh, chaos, image 14745a2a)
`CAS-SELFTEST PASS` / **ktest 145/0**(140→145 = CD-2a/variant/CD-2b/guard/CD-4 の5件、失敗0)/ **probe=NOT_RUN** / selftest=PASS。ログ: CD-4 `PCODE freq set failed (err -25=-EACCES, freq 179200)`(通知拒否が伝播、部分更新契約成立)。
→ CDCLK 変更成功・crawl・通知失敗を本番関数で検証済。**残(次=D0/D3統合)**: D3親 `intel_power_domains_init_hw(false)→icl_display_core_init(false)`(DC_state無効化→PCH reset handshake→combo PHY+**fault確認**→PW1 enable→CDCLK(本体済)→gen9_dbuf_enable[gen9_dbuf_slices_update+enabled_slices_mask]→tgl_bw_buddy_init→xelpd WA[CHICKEN_DCPR_2/DISPLAY_ERR_FATAL_MASK])→POWER_DOMAIN_INIT参照保持→全well sync_hw→initializing=false / cleanup=driver_remove(INIT参照put, well全OFFにしない)。gen12_dbuf_slices_config と icl_mbus_init は ADL-P で no-op。**D0 の3実装**: fuse実poll(PW1 PG0前/PG1後)、DC_off gen9_set_dc_state/gen9_dc_off ops(DMC payload確認)、VGA arbiter get→I/O→put所有権。**並行3**: sleep-range実backend(絶対期限+hrtimer/clockevent+waitq, 20-25ms+同一CPU別thread)、preemption実scheduler試験(実IRQ→切替要求→延期→最外側enableで処理)、PCODE追加区間2試験(通常未承認+追加承認/追加でcounter失敗→時間基盤異常, scripted time)。台帳 E-60〜E-76。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-77 (2026-09-17): D3親統合 intel_power_domains_init_hw(false) を本番子関数・共有device状態で fake 上最後まで実行(D-NORMAL/PRESERVE/FAULT/REMOVE 四ケース)。fuse実poll。GPU-free 153/0 (image 2d971aa5)
第44報後の専門家指示「D0残とD3親初期化を接続、本番と同じ子関数・同じdevice状態で init_hw(false) を fake 上最後まで」を実施。CDCLK本体(E-75/76)は再作成せず接続。build 0 error/warning。**実機はGPU渡さず構成(chaos)で検証**。

### CD-4 fixture 整合(第44報の指摘)
CD-4 を crawl setter fixture(要求voltage2 vs 変更前0)へ修正。voltageが真に判別材料になり、`hw.voltage_level==0(≠要求2)/hw.cdclk==307200(update_cdclk未実行で556800にならない)/hw.vco==1113600(crawl helper更新)` を個別検査。生status 0x11 → -EACCES。

### 順序1: D3親と既存部品の接続(display_core.{c,h} 新規、共有device状態)
- `parity_display_core{ pd, cd, pwc, m, sb_lock, dram_type/channels, initializing, dbuf_enabled_slices, init_wakeref_held, pm_wakeref, 診断(fault_stop/fault_where/reached_init_ref/reached_sync_hw) }`。**同一MMIO・同一sb_lock・同一power_domains/well・同一cdclk.hw・P2 DRAM情報を子で複製せず共有**。
- `parity_intel_power_domains_init_hw(dc, resume)` → `icl_display_core_init(false)`: DC_state無効化(gen9_set_dc_state DISABLE)→ PCH reset handshake(HSW_NDE_RSTWRN_OPT rmw RESET_PCH_HANDSHAKE_ENABLE)→ **combo PHY(本体, void契約=戻り後 parity_wait_time_base_faulted で fault確認し異常時停止)**→ PW1 明示enable(lock取得, 既存ops)→ **CDCLK(本体)**→ gen9_dbuf_enable → tgl_bw_buddy_init → xelpd WA(CHICKEN_DCPR_2/DISPLAY_ERR_FATAL_MASK=~0)。gen12_dbuf_slices_config/icl_mbus_init は ADL-P で no-op(正本確認)。→ **POWER_DOMAIN_INIT参照取得保持(disable_power_well=1で追加参照なし)→ 全well parity_power_well_sync_hw → initializing=false**。単一巨大mutexで包まず、pd->lock は PW1/sync/dbuf の各所で取得、CDCLK は sb_lock(別)で入れ子回避。

### 順序2(fuse): PW1 実 fuse poll(D0の一部)
power_well_enable の「modelled wait」を実装へ: has_fuses → pg=ICL_PW_CTL_IDX_TO_PG(idx)=idx+PG1。PW1(pg=PG1)は **enable前に PG0 fuse(SKL_FUSE_STATUS 0x42000 の dist bit)待ち + Wa_16013190616(GEN8_CHICKEN_DCPR_1 DISABLE_FLR_SRC)**、REQ→ACK後に **自PG(PG1)fuse待ち**。PG0=5us/他1us、timeout warn+継続、-EIO は停止。fakeは fuse状態を well REQ/STATE と別入力(REQ書込でfuse自動成立にしない、遅延fuse可)。

### 順序3(DBUF/BW): 正本の待機・保存状態
- **DBUF**(gen9 一組): `enabled_dbuf_slices_mask`(各slice STATE読)/`gen9_dbuf_slice_set`(**RMW REQUEST→posting read→udelay(10)→STATE読→不一致WARN**、汎用pollでない)/`gen9_dbuf_slices_update`(slice_mask全slice, pd->lock)/`gen9_dbuf_enable`(既存有効slice読+S1追加、全ONにしない)。ADL-P=4 slice(S1..S4)。fakeは STATE が REQUEST に追従。
- **BW_BUDDY**: tgl_buddy_page_masks 表を (num_channels, type=parity dram enum) で引き PAGE_MASK(0/1) 書込、未一致は BW_BUDDY_DISABLE。P2 DRAM情報入力(CDCLK固定fixture値を流用しない)。ver13 で TLB timer WA(ver12)なし。

### 順序4(INIT/sync/remove): driver-remove を通常domain putで代用しない
`parity_intel_power_domains_driver_remove`: 主init_wakeref を取り出し、**runtime-PM側のみ解放(pm_wakeref=0)、well の domain refcount は put しない**(wells は有効のまま=再ロード考慮)。disable_power_well=1 ゆえ disable_wakeref put もなし。全well count=0 の一律cleanupにしない。faultで未取得なら解放しない。

### 試験(D3統合四ケース、子をstubにせずMMIO/PCODE/時間のみfake、ktest 145→153/0)
| ケース | 合格 |
|---|---|
| **D-NORMAL** | 親完了(INIT参照+全well同期到達)/ CDCLK 179200 共有device で実行/ DBUF S1有効+BW page mask 0x1C/ combo PHY_A COMP_INIT + PW1 enable |
| **D-PRESERVE** | PHY/CDCLK既適切 → PCODE無(ptxn未消費)/ cdclk diag_no_change=1/ DBUF既存S1保持 |
| **D-FAULT** | 子途中(PW1 fuse/ACK)に時間異常注入 → 最初の停止位置保持/ INIT参照取得せず/ 全well同期到達せず/ remove で未取得参照を解放しない |
| **D-REMOVE** | D-NORMAL完了から診断終了 → rpm wakeref のみ解放/ 全well refcount 合計 不変(>0, domain put なし=wells有効維持) |

fault latch はテスト毎 `parity_wait_test_reset_fault()`(新, test専用)でクリアし各ケース clean 開始。実IRQ/scheduler網羅試験は始めない。

### 実機(GPU渡さず run-parity-nogpu.sh, chaos, image 2d971aa5)
`CAS-SELFTEST PASS` / **ktest 153/0**(145→153 = CD-4修正 + D-NORMAL 4分割 + D-PRESERVE/FAULT/REMOVE + fault-remove)/ **probe=NOT_RUN** / selftest=PASS。
→ D3正常経路が本番子関数・共有状態で最後まで通る。**残(次)**: D0 の DC_off(gen9_set_dc_state enable/is_enabled register/disable の DMC payload分岐, DC_off用簡略CDCLK/PHY作らず本体共有)+ VGA arbiter(vga資源get→I/O read→I/O write→put, client登録と操作所有権を分離, I/O accessorのみfake)、fuse遅延成立試験。並行3: sleep-range実backend(絶対期限+hrtimer/clockevent+waitq, 早期起床で期限を後ろ延ばさない)、preemption実scheduler試験(実IRQ→切替要求→延期→最外側enable)、PCODE追加区間2試験(承認成功/時間源異常で preempt・mutex 復元)。台帳 E-60〜E-77。GPU=vfio-pci維持, drm非blacklist, attach先行維持。probe.c frontier は GPU-free 中 UNIMPL のまま(実GPU一回は全fake統合後)。

## p011 増分E-78 (2026-09-17): D0残(fuse単位是正・DC_off・VGA所有権)を本番経路へ接続 + 実sleep backend実装。GPU-free 156/0 (image 2a38e46c)
第45報後の専門家指示「DC_off・VGAを本番へ接続し、実sleep/preemptionを完了。fuse待機の単位を正本へ」を実施。親関数は作り直さず残依存を接続。build 0 error/warning。**実機はGPU渡さず構成(chaos)で検証**。

### 順序1: fuse待機の単位是正(コメントの5us/1usをtimeoutにしない)
正本 gen9_wait_for_power_well_fuses → intel_de_wait_for_set(...,1) → intel_wait_for_register_fw(...,timeout_ms=1) → **fast_timeout_us=2 + slow_timeout_ms=1 を全PG共通**(5us/1us はコメントのみ、PG0を5msに延ばさない)。pw_wait_fuse を parity_wait_reg(...,2u,1u,0) へ修正。PW1順序は正本対応(WA→PG0 fuse前→REQ→ACK→PG1 fuse後→post-enable)保持。

### 順序2: DC_off・VGA を本番経路へ(模型・省略を介さず D3 から)
- **DC_off**(2入口を同一便利関数にまとめない): D3先頭=gen9_set_dc_state(DISABLE)。POWER_DOMAIN_INIT取得→DC_off well enable→`parity_dc_off_enable`=gen9_disable_dc_states: target≠DC3CO→DC無効化→**CDCLKを一時構造体へ読み保存値と比較(intel_cdclk_init_hw呼び直しでない)**→**DBUF実maskを保存maskと照合(gen9_dbuf_enable呼び直しでない)**→combo PHY本体で復元(簡略版作らず)。disable側はintel_dmc_has_payload分岐(未初期化なら何もしない)、enabled はDC_STATE_EN読。pwc に共有device(cd/dbuf_slices/target/allowed_dc)を接続(二重lock回避=各helperのlock前提を合わせ、親に巨大lock追加せず)。
- **VGA**(登録済フラグと操作所有権を分離): intel_vga_reset_io_mem = **LEGACY_IO資源get→VGA_MIS_R(0x3CC)読→VGA_MIS_W(0x3C2)へ同値書→put**。client登録に依存せず実行。取得失敗ならI/Oせず未取得資源をputしない。I/O accessor だけ注入可(parity_vga_io_test_set)、本番は arbiter+port I/O。

### 順序3: 実sleep backend(kern_usleep_range、並行から主作業へ繰上げ)
tick=KERN_CLOCK_HZ=100(10ms)。**絶対期限を単調counterで一度だけ計算**(earliest=base+min_us)。1 scheduler tick以上残る間は private waitq に tick期限で登録し **waitq_sleep→sched_sleep_locked でCPUを明け渡す**(同一CPUの別threadが走る)。**早期起床は同じ絶対期限へ再待機(延長しない)**。sub-tick残(PCODE usleep_range(10,20)等)は busy-wait で **µs要求を10ms tickへ丸めない**。時間源異常で停止。waitq_sleep が戻り時に token除去=timer状態がframe外に残らない。**fuse/well/PLL の slow(slow_ms=1)は wait.c 側で既に waitq yield 済、PCODE通常区間が主消費者。追加区間(preempt-off)は sleep_between=0 で sleep呼ばず(安全)。**
- 試験: 20-25ms を co-runner(別thread)と同時実行 → **elapsed≥19ms / sched_ticks≥1 crossed(sub-tick busy-spinでない=yield) / co-runner進行**。boot健全(yielding PCODE経路でhang無し)= 実行結果。max_us以内return必須の試験にしない。

### 実機(GPU渡さず run-parity-nogpu.sh, chaos, image 2a38e46c)
`CAS-SELFTEST PASS` / **ktest 156/0**(153→156 = D-NORMAL の DC_off本体+VGA順序 2件、D-PRESERVE 2-slice化、usleep yield 1件)/ **probe=NOT_RUN** / selftest=PASS。D3四ケース(D-NORMAL/PRESERVE/FAULT/REMOVE)は DC_off/VGA/fuse是正込みで再通過。
→ **D0完了**(fuse単位・DC_off・VGA本番接続)+ **実sleep backend実装・検証**。**残(次)**: preemption実scheduler試験(同一CPU A/B二重禁止、実IRQ→切替要求→延期→最外側enable、A はB通知をblocking waitしない、非blocking診断+有限期限で確認)、PCODE追加区間2試験(通常未承認+追加承認→preempt/mutex復元 / 追加でcounter失敗→時間源異常, scripted time)、D3四ケース最終再実行、probe.c UNIMPL を実init_hw呼出へ置換(P0→P3前半→intel_power_domains_init_hw(false)→fault無確認→INIT保持→intel_dmc_init入口)。到達後 実GPU一回(4GiB/4vCPU/39bit/parity単独/attach先行)。台帳 E-60〜E-78。GPU=vfio-pci維持, drm非blacklist, attach先行維持。実sleepと実preemptionの受入は別記録(sleepは本増分で実行結果、preemptionは次)。

## p011 増分E-79 (2026-09-17): 実preemption scheduler試験 + PCODE追加区間2試験を完了。sleep利用者接続確認。GPU-free 159/0 (image 3098b4ff)
第46報後の専門家指示「実sleep完成・preemption/PCODE二試験完了」のうち、**preemption実試験とPCODE追加区間2試験を実行結果まで完了**。sub-tick sleep(µs)の期限timer化(dynticks)は後述の制約により次段。build 0 error/warning。**実機GPU渡さず(chaos)**。

### 実preemption scheduler試験(同一CPU A/B、実IRQ→切替延期→最外側enable)
- sched_set_cpu で thread A/B を同一CPU(1)へ pin。B は waitq で sleep。A: kern_preempt_disable×2(count=2)→ **kern_diag_oneshot_arm(require_cpu=1)** で次の実 periodic timer IRQ から callback を発火 → callback が waitq_wake_one(B)(B runnable + need_resched)。A は非blockingにfired待ち(禁止中にsleepしない)。
- 観測: **fired後 B未実行(切替延期)/ 内側enable(2→1)でも未実行/ 最外側enable(1→0)で sched_yield → B実行**。実IRQ・切替要求・延期・最外側処理・実切替の順を trace。boot成功だけを合格にしない(実IRQから切替要求が起きた実行のみ合格)。sched_test_preempt_count() 追加(test専用)。
- A は禁止区間で B通知を blocking wait しない(有限期限の非blocking spin)。count を直接ゼロ書きで回復しない。

### PCODE追加区間 2試験(scripted time、通常/追加を別証拠)
- fake 応答/時間源を **preempt状態で分岐**(sched_test_preempt_count>0=追加区間)させ決定的化。
- **(a)通常未承認→追加承認**: reply を preempt-off区間でのみ READY。通常区間 timeout → 追加区間(kern_preempt_disable)で承認 → **ret=0、preempt復元(count0)、mutex解放(trylock成功)**。追加区間到達を txn数で確認。
- **(b)追加区間 counter読出失敗→時間源異常**: 追加区間の counter read を失敗(kern_rtc_read_counter 戻り0)→ parity_pcode_poll が **-EIO(通常timeout -ETIMEDOUT=-42 でない)**、**preempt復元・mutex解放**。scripted time、実時間非消費。
- udelay/atomic poll と追加50ms区間(preempt禁止・sleepなし・IRQ長時間禁止しない)は不変。

### sleep利用者の接続確認
実sleep backend(E-78)の主消費者=PCODE通常区間 usleep_range(10,20)。fuse/well/PLL の slow(slow_ms=1)は wait.c 側で既に waitq yield。追加50ms区間は sleep_between=0(sleep呼ばず)。

### 実機(GPU渡さず, chaos, image 3098b4ff)
`CAS-SELFTEST PASS` / **ktest 159/0**(156→159 = 実preemption 1 + PCODE追加区間 2)/ **probe=NOT_RUN** / selftest=PASS。D3四ケース・sleep yield試験も再通過。

### 残(次、実GPU受入前)と制約の明示
- **sub-tick sleep の期限timer化(dynticks)= 唯一の未完点**。現状: ms級は waitq で yield、µs級(PCODE usleep_range(10,20))は busy-wait。sub-tick 精度(2-3ms を正しく待つ)には**高分解能 one-shot clockevent が必須**。調査結果: 当機は **HPET 無し、per-CPU LAPIC timer が periodic(KERN_CLOCK_HZ=100/10ms)で scheduler tick と共用**。よって sub-tick 化には LAPIC を one-shot 化し、kernel_ticks を IRQ計数から**単調時刻由来へ**変え、次イベント=min(次tick境界, 最早sleep期限)で再武装する **dynticks 変換**が必要(専門家が HAL変更を承認済)。boot-critical path ゆえ次増分で段階的 boot健全確認しつつ実施(lapic.c one-shot / clock.c kernel_timer_handler 単調tick / sleep期限をcounter単位化)。破綻時は 159/0 の hybrid へ revert。
- fuse遅延(100µs)試験(slow側 fast=2µs→slow=1ms 経路確認)、probe.c UNIMPL を実 intel_power_domains_init_hw(false) 呼出へ置換、到達後 実GPU一回(参照条件)。
台帳 E-60〜E-79。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-80 (2026-09-17): timer HAL境界の分析(第一提出物)+ レビュー不要作業A(純粋次イベント計算)B(fuse遅延試験)を実施。GPU-free 169/0 (image da3a74c9)
第47報後の専門家指示「HAL境界変更はレビュー前に適用しない。第一提出物=timer境界の現行宣言・呼出関係・変更案」に対応。実preemption/PCODE(E-79)は保持。build 0 error/warning。**実機GPU渡さず(chaos)**。

### 第一提出物: timer境界の確認(コード適用なし)
- **HAL→kernel の唯一のtimer契約 = `kernel_timer_handler(cpu, ack)`**(include/hal/hal.h, 「scheduling tick発生」)。amd64は irq.c の IRQ_TIMER パスから呼ぶ。kern/clock.c 受口で CPU0 が kernel_ticks++(IRQ=1 tick)。
- **`amd64_lapic_timer_start/stop`**(lapic.h)は **HAL内部専用**(amd64 bsp clock.c のみ)。**kernel→HAL の timer設定API は存在しない**(kernelは発火時刻を指示不可)。`amd64_lapic_timer_arm` は現状不在。
- **判定(現行→変更案→影響 対応表)**: 段階1(one-shotで論理100Hz tick再現: LAPIC one-shot化+amd64 irq.c が timecounter から次10ms境界算出し再武装、10ms境界のみ kernel_timer_handler 1回)=**amd64 HAL内に収まり kernel_timer_handler契約/tick単位 不変=内部変更**(ただし方針に従い段階1 diff もレビューへ)。段階2(sub-tick sleep期限を同イベント管理へ)=**kernelがHALに「時刻Tで起こせ」と言う手段が無く、HALがsub-tickを tick と区別通知する手段も無い → 新境界IF必須=レビュー必須**。案A(kernel→HAL arm)/案B(HAL→kernel earliest-deadline query)を未適用diffで提出予定。KERN_CLOCK_HZ上げず/idle tick停止・全面tickless は非対象。

### レビュー不要作業(本報で実施・検証)
- **A. 純粋な次イベント/tick計算**(timer_calc.{c,h} 新設, HW非依存純関数): next_event=min(次tick,最早sleep) / sleep-only発火=0 tick・tick不動 / 取消で選び直し / tick満了=1 tick / 複数set_sleepで最早保持 / 遅延発火=跨いだtick全配信し期限をnowの先へ(過去期限待たない) / sub-tick発火はtick計上せず。単位・意味は既存tick維持。fake時刻で7検査。
- **B. fuse遅延試験**: PG0成立・PG1が数read遅れて成立 → fuse待機(fast2µs→slow1ms)が遅延PG1を観測し継続。PG1永久不成立 → HW timeout警告+enable継続(-EIO でない, 時間源異常と区別)。

### 実機(GPU渡さず, chaos, image da3a74c9)
`CAS-SELFTEST PASS` / **ktest 169/0**(159→169 = timer-calc 7 + fuse遅延 2)/ **probe=NOT_RUN** / selftest=PASS。

### 残(次)
段階1 未適用diff(lapic.c one-shot / amd64 irq.c 再武装, hal.h無変更)+ 境界不変確認、案A/B 未適用IF diff(期限表現・CPU/IRQ文脈・設定/取消/過去期限/同期・返却契約・影響範囲)をレビューへ。probe接続diff。timer受入後に段階2(sub-tick sleep)→ sleep試験是正(要求値直接比較・待機理由trace・20-25ms/2-3ms/10-20µs)→ 実GPU一回。破綻時 復旧版=3098b4ff(E-79 hybrid)。台帳E-60〜E-80。GPU=vfio-pci維持, drm非blacklist, attach先行維持。

## p011 増分E-81 (2026-09-17): 通常sleepを既存10ms tick+waitqで完成(HAL非変更)+ probe.c接続 → **実ADL-P GPUで電源HW初期化完了、intel_dmc_init入口到達**。image c35a1a4f
第48報後の専門家指示「HAL非変更・通常sleepを既存tick/waitqで完成 → probe接続 → 実GPU一回」を実施。**目標到達: 実機で intel_power_domains_init_hw(false) 完了 → INIT参照保持 → intel_dmc_init 入口で正確停止**。build 0 error/warning。

### 通常sleep完成(HAL非変更、既存10ms tick+waitq)
- kern_usleep_range の「sub-tick busy分岐」を撤去し、**未満了なら常に既存waitqで次tickを待つ**(one-shot/dynticks/HAL変更なし)。絶対期限を一度計算・早期起床で延長せず・時間源異常伝播・tick到来だけでreturnせず単調counterで最小待機確認。**10ms粒度で要求範囲より遅れうる旨を仕様/診断に明記**(未実装扱いにしない)。udelay/atomic/PCODE追加50ms区間(preempt禁止・sleepなし)は不変。利用者=PCODE通常区間 usleep_range(10,20)・parity_wait_reg slow。要求値と粒度を分けて記録(引数書換えなし、CDCLK 3ms prepare保持)。
- timer_calc(E-80)は保存のみ・本番非接続。実preemption/PCODE/fuse遅延/D3四ケース は回帰保持(GPU-free 169/0 維持)。

### probe.c 接続(UNIMPL → 実 intel_power_domains_init_hw(false))
- probe.c frontier で 実device状態(mmio/power_domains/cdclk[rev→adlp_display_step→STEP_D0でhook]/pwc[vga,irqs=0]/dram_info)を構築し `parity_intel_power_domains_init_hw(&dcore, false)` 呼出。fault_stop なら FAILED(停止子名)、成功で last_completed=intel_power_domains_init_hw → **intel_dmc_init を UNIMPL で BLOCKED(INIT参照保持)**。teardown に driver_remove(init rpm wakeref解放・well維持)を map cleanup 前へ追加。本番入口に試験override無し(fake/injection/ reset_fault は ktest専用)。

### 実機 ADL-P(8086:46a8 rev0c, run-parity-ref.sh 参照条件 4GiB/4vCPU/39bit/x-igd-opregion/rombar=0, image c35a1a4f)
`CAS-SELFTEST PASS` / attach P0 command=0x0007 → P2 pci_set_master/msi → P3: drm_vblank(4)/bios(VBT無→既定,ver155,child3)/vga_register(client登録,gmch found)/power_domains(30 wells,DC 0x4000000a/0x2) → **combo PHY A/B (0 initialised=BIOS既に適切で再init不要)** / **cdclk sanitizing(pre-os再設定要)** → **intel_power_domains_init_hw(false) done: cdclk=179200 vco=537600 dbuf=0x7 init_ref=1 sync_hw=1**(実PCUへPCODE prepare/notify・実DE PLL・実DBUF slice・実BW_BUDDY・全30 well sync が成立)→ **intel_dmc_init 入口で BLOCKED(err=0, INIT参照保持)**。teardown 逆順成立: driver_remove(rpm wakeref解放/well維持)→power map→VGA→DRM→MSI→WC unmap→PCI probe PM(usage=0)。CPUs4 panic0。
→ **到達目標達成**: last_completed_op=intel_power_domains_init_hw / blocked=intel_dmc_init / init_wakeref保持。P3全体完了・描画ではない。**次: intel_dmc_init(DMC firmware load)が新frontier**。sleep_backend=periodic_tick / nominal_tick=10ms / HAL_timer_interface_changed=0。台帳E-60〜E-81。GPU=vfio-pci維持, drm非blacklist, attach先行維持, 描画/hang探索へ戻らず。

## p011 増分E-82 (2026-09-17): DMC第一段=参照firmware(i915/adlp_dmc.bin)を固定同梱し read-only provider を接続。GPU-free 172/0 (image f96cd9eb)
第49報(実機 電源HW初期化完了)後の専門家指示「次単位=intel_dmc_init: firmware取得→解析→非同期ロード→後始末。第一着手=参照blob確保+provider接続」に対し、**第一段(firmware供給)を完了**。build 0 error/warning。

### firmware識別情報(manifest追加)
- **要求名**: `i915/adlp_dmc.bin`
- **取得元**: 参照環境 chaos(solaris10-man) `/lib/firmware/i915/adlp_dmc.bin`(最新版DLせず固定ファイル使用)
- **非圧縮bytesサイズ**: 79088
- **SHA-256**: 3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb
- **byte-sum checksum**: 0x002b075a(GPU-free試験の完全性照合用)
- version(2.20)はファイル名から決めず、後段の CSS header 解析結果で確定予定。

### read-only firmware provider(HAL非追加, VFS非使用)
- 起動imageへ同梱: `firmware_adlp_dmc.c`(79088 byte C配列, 参照blobのbyte-for-byte複製)。
- `osdep/firmware.{c,h}`: `osdep_request_firmware(fw, name)`=名前一致で data/size 返し 0、不在は -ENOENT かつ data=NULL。`osdep_release_firmware`=ハンドル破棄のみ(**静的blobをfreeしない**)。request_firmware()/release_firmware() 契約に対応。
- 試験(GPU-free): 名前一致で 79088 byte + checksum 一致 / release でハンドル0(blob非free)/ 不在名で -errno・data無し。

### 実機(GPU渡さず, chaos, image f96cd9eb)
`CAS-SELFTEST PASS` / **ktest 172/0**(169→172 = dmc-fw 3件)/ probe=NOT_RUN / selftest=PASS。電源HW初期化(E-81)は回帰保持。

### 残(DMC本体、次)
- **parser**(正本 intel_dmc.c/.h/intel_dmc_regs.h): CSS/package/dmc header(v1/v3)解析、stepping選択(rev0c→D0)、size/MMIO範囲検査、payload保存。main決め打ちせず選択各ID。bytes/DWORD単位・offset基準・blob寿命とpayload寿命の分離。解析結果=要求名/stepping/選択ID/payloadサイズ/付随MMIO件数を出力(期待値は実blobを正本解析から)。
- **非同期worker**: intel_dmc_init() は state/work/参照準備しqueueしてreturn(同期版に短縮しない)。worker が 取得→解析→program load→参照処理→firmware解放。DMC state/worker引数/参照display状態は device所有領域(一時localでない)。既存共有workqueue/completion 再利用。
- **program load**: 既存MMIO適合層。前処理→DMCイベント無効化→preempt禁止payload書込→付随MMIO→末尾。DMC_PROGRAM(addr,i)=4byte刻み(DWORD index、file offset/CPU/GPU VA混同しない)。IRQ長時間禁止/巨大spinlock/しない。診断: 取得済/解析保存済/書込開始/書込完了/worker終了(has_payload=trueだけを完了証拠にしない)。
- **電源参照分離**: PCI probe / 電源HW初期化INIT参照 / DMCロード参照 を別管理(1 bool/counterにまとめない)。DMCは自参照取得→成功時解放/失敗時保持。driver_removeの「rpm-onlyだけ解放」をDMCの通常domain参照解放へ流用しない。
- **fini**: work完了同期(flush_work相当、cancel_work_syncで代用しない)→DMC参照処理→payload/DMC state解放→既存 power-domain driver_remove→VGA/DRM/MSI/map/PCI PM。worker使用中メモリを free して cleanup=1 にしない。
- **GPU-free 4試験**: DMC-NORMAL(実blob取得解析ロード完了、DMC参照のみ解放・親INIT残)/ DMC-NO-FW(実不在→fallback+参照保持+fini)/ DMC-BAD-FW(header破損→不正書込へ進まず途中確保回収)/ DMC-FINI(queue済/実行中→完了同期前にMMIO/device/payload破棄しない)。確保失敗はDMC-FINI variant。既存172回帰保持。
- 揃ったら 実GPU一回(通常位置ロード、独自同期障壁足さず、後続未実装なら次停止点、診断終了でDMC work完了待ち採取)。log: firmware(path/size/sha256/parsed_version/選択ID/payloadサイズ)、実行(init入口/work投入/worker開始/program書込完了/worker終了/適合層fault/INIT参照・DMC参照 get-release)、probe(last_completed_op/次unimpl/dmc_program_write_completed/cleanup/PM/published=0)。
台帳E-60〜E-82。GPU=vfio-pci維持, drm非blacklist, attach先行維持, 10ms tick/HAL非変更 確定維持。

## p011 増分E-83 (2026-09-17): DMC F1=parser 移植。固定blob(adlp_dmc.bin)を解析し version 2.20・MAIN+4 pipe DMC・payload を device所有領域へ保存。GPU-free 173/0 (image 8c10eb5e)
第50報後の専門家指示「F1 parser: 固定blobを解析しdevice所有payloadへ変換」を実施。**実blobの解析結果を確定**(version等を推測代入せず正本parser本体で取得)。build 0 error/warning。

### F1 parser(dmc.{c,h}新規, 正本 intel_dmc.c 準拠)
- packed on-disk構造: css_header(128B)/fw_info(12B)/package_header(16B)/dmc_header_base(20B)/v1(128B)/v3(256B)。
- parse_css→parse_package→dmc_set_fw_offset(steppingマッチ, 各id最初の一致保持=最後採用でない)→各id parse_header。**長さ単位をヘッダ毎に保持**(CSS/package/v3 header_len=DWORD, v1=byte, fw_size=DWORD, entry offset=DWORD)。**offsetは CSS+package 末尾からの加算**(ファイル先頭でない)。残サイズ検査後にポインタ生成、切詰めは拒否。
- stepping: rev0c→STEP_D0→step_name "D0"→{'D','0'}を device情報から供給。
- payload: **device所有 arena(静的, blob と別)へコピー**→provider解放後も有効。3状態分離(entry選択/payload保存/program書込)を別フィールド(present/payload/後段)。max_fw_size=DISPLAY_VER13(0x20000)。
- **実blob解析結果(fixture, 正本parserで取得)**: version=**2.20**(CSS), package_ver=2, entries=6, css_len=128。選択5 ID(全 header_ver3): MAIN off_dw6301 start0x80000 mmio7 fwsz6274 **payload25096B** / PIPEA off12639 start0x90000 mmio9 payload10188 / PIPEB off15250 start0x98000 mmio9 payload12264 / PIPEC off18380 start0x52000 mmio5 payload2264 / PIPED off19010 start0x59000 mmio5 payload2264。総payload 52076B < arena 128KB。
- 試験(GPU-free): 実providerのblobを本番parserへ渡し version==(2<<16|20)/entries6/pkg_ver2/5 ID present/MAIN header_ver3/MAIN payload25096B・payload!=0、**provider解放後もpayload有効**。parity_dmc_parse_reset で arena 解放。

### 実機(GPU渡さず, chaos, image 8c10eb5e)
`CAS-SELFTEST PASS` / **ktest 173/0**(172→173 = DMC parse)/ probe=NOT_RUN / selftest=PASS。firmware provider(E-82)・電源HW初期化(E-81)は回帰保持。

### 残(F2/F3/F4, 次)
- **F2 program load**: 前処理→DMCイベントhandler処理→preempt禁止 各id payload書込(DMC_PROGRAM(addr,i)=start_mmioaddr+i*4)→復元→付随MMIO(**dmc_mmiodata()経由: pipe DMCイベント既定無効化でblob値から変換, 全件そのまま書かない**)→ADL-P clock-gating WA(開始A-D/末尾C-Dのみ, 対称戻しに短縮しない)→保存状態。既存MMIO/preempt適合層, 新timer/forcewake常時保持しない。診断: 取得/解析保存/書込開始/書込完了/worker終了(has_payloadだけを完了証拠にしない)。fakeで payload各id アドレス/DWORD/順序/件数逐次照合(前後処理・付随MMIOは別区分)。
- **F3 worker/参照/fini**: intel_dmc_init=DMC参照取得+state/work準備し**queueしてreturn**(worker直呼びせず, 別CPU worker が init return前開始は許容)。worker=取得→parse→load→参照処理→provider解放。**電源参照3者分離**(PCI probe/INIT/DMCロード、DMCは成功時解放失敗時保持, driver_removeのrpm-only流用せず, **DMC obj確保前にDMC参照取得ありうる→参照所有をobj内だけに置かない**)。不在fallback=既定-ENOENT且つpath未指定でlegacy adlp_dmc_ver2_16(解析失敗をfallback扱いにしない, alias偽装しない)。fini=**flush_work相当(cancel_work_syncで代用しない, false=idleを失敗にしない)**→DMC参照処理→payload/state解放→power-domain driver_remove→下位資源。worker使用中メモリfreeしない。
- **F4**: intel_dmc_init UNIMPL置換(電源HW初期化の後, 通常probeにDMC完了待ち障壁足さず, 後続未実装を次停止点, 診断終了でDMC work同期し採取)。GPU-free 4試験(DMC-NORMAL/NO-FW/BAD-FW/FINI)後 実GPU一回。
台帳E-60〜E-83。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-84 (2026-09-18): DMC F2=program load 本体を移植。固定blob解析結果で 13019 payload + 35 aux + 80 evt-disable を逐次照合。GPU-free 174/0 (image b2ca92b8)
第51報(F1 parser)後の専門家指示「F2 program load を書込み照合と共に実装」を実施。build 0 error/warning。**実際にどこへ・何回・何を書いたかを逐次検証**。

### F2 program load(parity_intel_dmc_load_program、正本 intel_dmc_load_program 準拠)
- 順序: MAIN payload存在確認 → **pre clock-gating WA(pipe A-D: CLKGATE_DIS_PSL_EXT|=PIPEDMC_GATING_DIS bit12)** → **event handler無効化(ver≥12, 各present DMC 8 handler の CTL=TYPE_EDGE_0_1|EVENT_ID_FALSE=0x30100 / HTP=0)** → preempt禁止 → 全ID payload書込(**DMC_PROGRAM=start_mmioaddr+i*4**, write_fw) → preempt復元 → 全ID 付随MMIO(**dmc_mmiodata()変換: pipe DMC の EVT_CTL は 0x30100, MAIN と非EVT_CTL は raw**) → dc_state=0 → **DC_STATE_DEBUG(0x45520) rmw(CORES|MEMORY_UP) + posting read** → **post WA(pipe C-D のみ clear, 対称戻しでない)**。
- event base: MAIN 0x8f000, pipe DMC 0x5f000+0x400*(id-1)。_DMC_REG(id,reg)=reg-0x8f000+base(id)。EVT_CTL=base+0x34+4h, HTP=base+4+4h。**payload領域とevent制御領域を別基点で生成**(pipe payload 0x90000近傍でも event は 0x5fxxx)。
- entry offset を load で再加算しない(parser使用済, load は保存payload読み start+4i へ書く, 4byte刻み)。ID走査順保持(物理アドレス昇順に並べ替えない)。payloadループ内に sleep/通常log/forcewake常時保持 追加せず。診断: payload_writes/aux_writes/evt_disable_writes/load_seq_completed(末尾処理まで完了で1) + psum/asum(順序付き逐次checksum)。has_payloadだけを完了証拠にしない。
- **試験(GPU-free, 実blob→本番parser→本番load, fake MMIO)**: payload_writes=**13019**(MAIN6274+PIPEA2547+PIPEB3066+PIPEC566+PIPED566), aux_writes=**35**(7+9+9+5+5), evt_disable_writes=**80**(5 DMC×8×2), dc_state=0, load_seq_completed=1。**psum を test側で独立に再計算(同順 addr+val*7)し一致**=各payload writeの addr/値/順序/件数を検証。fake wt_total=**13141**(WA4+evt80+payload13019+aux35+dbg1+WA2)=全書込みが fake へ到達(過不足0)。

### 実機(GPU渡さず, chaos, image b2ca92b8)
`CAS-SELFTEST PASS` / **ktest 174/0**(173→174 = DMC F2 load)/ probe=NOT_RUN / selftest=PASS。F1 parser・firmware provider・電源HW初期化 は回帰保持。

### 残(F3/F4, 次)
- **F3 worker/電源参照/fini**: intel_dmc_init=DMC固有電源参照取得+state/work準備し queue return(worker直呼びせず, 別CPU worker が init return前開始許容, queue前に引数/参照先初期化)。worker=既存provider取得→本番parser→本番load→DMC固有電源参照処理→providerハンドル解放。**電源参照3者分離**(PCI probe/親INIT/DMCロード, 正常後解放はDMC自身の参照のみ, 不在/解析失敗で保持, DMC obj確保前に参照取得ありうる→所有をobj内だけに置かない, MMIO faultで中断時 payload存在でもDMC参照残りうる→has_payloadだけで解放済と判定しない)。不在fallback=既定-ENOENT且path未指定で legacy adlp_dmc_ver2_16(解析失敗をfallback扱いにせず, alias偽装しない, 正常系は adlp_dmc.bin)。arena方式維持だが**個別free後にarena free しない=一括解放**。fini=**flush_work相当(cancel_work_syncで代用しない, false=idleを失敗にしない, callback実行終了確定後にpayload解放)**→DMC参照処理→arena一括+state解放→power-domain driver_remove→下位資源。worker使用中メモリfreeしない。既存共有workqueue再利用(第二基盤作らない)。
- **F4**: probe.c intel_dmc_init UNIMPL を実入口へ置換(電源HW初期化の後, 通常probeにDMC完了待ち障壁足さず queue して次へ, 後続未実装を停止点, 診断終了でDMC work同期し採取)。GPU-free 4試験(DMC-NORMAL[worker経由load, DMC参照のみ解放親INIT残]/NO-FW[実不在+fallback要求+参照保持fini回収]/BAD-FW[試験用copyでMAINヘッダ破損→program write進まず arena使用分回収]/DMC-FINI[queue済/実行中→完了前にarena/device/MMIO破棄せず, 試験hookでpreempt禁止区間前に停止し別threadからfini, MMIO fault variant])後 実GPU一回(参照条件)。log: firmware(path/size/sha/parsed_version/選択ID)/queued/worker_started/finished/payload・aux書込数/program_load_sequence_completed/最初のfault/親INIT・DMC参照 get-release/work同期/arena解放/last_completed_op/blocked/cleanup/published=0。
台帳E-60〜E-84。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-85 (2026-09-18): DMC F3=非同期worker/電源参照/fini + flush_work追加。F2試験を生blob独立照合へ補強。実共有workerで DMC-NORMAL/NO-FW 成立。GPU-free 178/0 (image ab10df39)
第52報後の専門家指示「F3 worker/電源参照/fini完成 + F2試験をチェックサムから直接比較へ補強」を実施。build 0 error/warning。**実共有workqueue経由でDMCロードが端から端まで動作**。

### F2試験の補強(試験側のみ、ロード本体は不変)
- 期待payloadを**生blob(fw.data)から独立に読み**照合(移植先parserの保存copyに依存しない=copyバグを検出)。offset=readcount(CSS128+package400=528)+dmc_offset*4+v3 header256。psum は order-sensitive多項式hash(単純Σでない)で addr/値/順序/件数を捕捉、fake wt_total=13141で過不足0。
- **値変異検出器**: MAIN payload 1 DWORD を反転→psum が固定基準と乖離することを確認(照合器がno-opでない実証)。

### F3(dmc.{c,h}: parity_dmc_dev + init/worker/fini)
- **flush_work追加**(backend_sync): parity_kflush_work=対象workを**取消さず実行完了を待つ**(cancel_work_syncと別、PENDINGは実行を待ちRUNNINGはcallback終了まで、戻り false=idle は失敗でない)。既存共有workqueue再利用(第二基盤作らず)。
- **init**: DMC固有 INIT参照を取得(parity_display_power_get INIT=親と別のdomain get)→ state/work/参照 準備 → **queue前に全公開**(別CPU workerがreturn前開始でも成立)→ parity_kqueue_work。worker直呼びせず。
- **worker(dmc_load_work_fn)**: osdep_request_firmware(default)→ -ENOENT且つpath未指定で **legacy adlp_dmc_ver2_16 fallback**(解析失敗をfallback扱いにせず)→ 本番parser → MAIN payload成立時 本番load → **load_seq_completed で成功時 DMC参照解放(domain put)、未完(fault)は保持** → provider解放。診断分離(work_submitted/worker_started/firmware_acquired/fallback_requested/main_payload_present/load_seq_completed_flag/first_fault, main_payload_present と load_seq_completed 兼用せず)。
- **電源参照3者分離**: PCI probe / 親INIT(driver_remove で rpm-only解放) / **DMCロード(domain get, 成功時 domain put, 不在/faultで保持)**。driver_remove の rpm-only 解放を DMC の通常domain参照解放へ流用しない。DMC obj は device所有(dd), 参照所有を obj内だけに置かない。
- **fini**: **flush_work(cancelでない)→ 残存DMC参照処理(dmc_put_ref)→ arena一括解放(個別free後arena free しない)→ parse reset**。worker使用中メモリfreeしない。

### 試験(GPU-free, 実共有worker経由)
- **DMC-NORMAL**: init→queue→**実kworker が worker実行**→provider→parser→load(13019書込)→**DMC参照解放**。全well refcount合計 前後不変(DMC get+put=net0, 親INIT参照保持=leak無)。work_submitted/worker_started/firmware_acquired/main_payload_present/load_seq_completed=1, dmc_wakeref_held=0。
- **DMC-NO-FW**: 不在path→default+**fallback要求**→payload無→**DMC参照保持**(rpmブロック)。fini が保持参照を解放(dmc_wakeref_held 1→0)。

### 実機(GPU渡さず, chaos, image ab10df39)
`CAS-SELFTEST PASS` / **ktest 178/0**(174→178 = F2補強2 + DMC-NORMAL1 + DMC-NO-FW2)/ probe=NOT_RUN / selftest=PASS。F1/F2/firmware provider/電源HW初期化 回帰保持。

### 残(次)
- **DMC-BAD-FW**(provider override osdep_firmware_test_set で試験用copyのMAINヘッダ破損→MAIN payload不成立→program write進まず arena使用分回収)、**DMC-FINI**(worker を preempt禁止区間前の試験hookで停止→別threadからfini→完了前にarena/device/MMIO破棄せず, 解除後同期回収, MMIO fault variant含む, override解除もworker完了後)。
- **F4**: probe.c intel_dmc_init UNIMPL を実 parity_intel_dmc_init へ置換(電源HW初期化の後 queueして次へ, 通常probeにDMC完了待ち障壁足さず, 後続(modeset/flip wq)未実装を停止点, 診断終了で DMC work同期し採取, DMC fini は power-domain remove より先)。実GPU一回(参照条件)。log: firmware_id/parsed_version/work_submitted/worker_started/work_sync_completed/load_seq_completed/first_fault/payload・aux・evt数/親INIT・DMC参照 get-release/arena_released/last_completed_op/blocked/cleanup/published=0。
台帳E-60〜E-85。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-86 (2026-09-18): DMC F4=probe接続 + 残り試験(BAD-FW/FINI running/FINI fault) 完了。**実機で DMC firmware ロード成功**(load_seq_completed=1, DMC参照解放=0, intel_mode_config_init で停止)。GPU-free 184/0
専門家承認「案1(b)」(4試験完成 → F4 probe接続=modeset/flip wq まで実装し intel_mode_config_init で停止 → 実機1回)を実施。build 0 error/warning。**12982 は実機正として受理(専門家判断(A))**。

### 追加試験(GPU-free, 実共有worker経由)
- 試験用hook(本番は0): `parity_dmc_test_pause`(parse後・preempt禁止区間前でworkerが yield park)、`parity_dmc_test_fault_at`(N write後にpayloadループ中断=適応層fault模擬: preempt復元/load_seq_completed=0/DMC参照保持)。provider override `osdep_firmware_test_set(ops)`(試験用copyを返す, 解除はworker完了後)。
- **DMC-BAD-FW**: 試験用copyのMAIN v3 header(528+6301*4)の header_ver を 7 に破損→firmware_acquired=1/main_payload_present=0/payload_writes=0/load_seq_completed=0/**DMC参照保持**→fini で参照回収+使用分arena回収(PIPEA payload=0)。
- **DMC-FINI(running)**: pause hook で worker park → 別thread(spawn_detached)から fini → **fini は flush で block、arena/payload は worker実行中に解放されない**(MAIN payload!=0, g_fini_done=0) → 解除後 worker がロード完走(13019)→ fini が同期後に1回だけ回収(参照0, payload=0)。
- **DMC-FINI(fault)**: fault_at=1000 → payload_writes=1000 で中断, first_fault=1, **preempt_count=0(復元)**, main_payload_present=1 だが **DMC参照保持**(has_payloadだけで解放済と判定しない実証)→fini が所有権で回収, 全well refcount合計 前後一致(net 0)。
- 罠: FINI(fault) の fake_mmio_open が fuse_status を0へ戻し 1ms fuse timeout×井戸数×init回数 で120s超過→ `f.fuse_status=0xFFFFFFFF` を再設定して解消。

### F4(probe.c)
- intel_dmc_init UNIMPL を実入口へ置換: dmc_wq 作成 → stepping を **PCI revision から導出**(sc/ss='A'+(step-A0)/4, '0'+(step-A0)%4) → parity_intel_dmc_init(queue して即次へ, 完了待ち障壁なし) → **modeset/flip workqueue 作成** → intel_mode_config_init(未実装)で BLOCKED。
- teardown: **DMC fini(flush→参照処理→arena一括解放)→ dmc/modeset/flip wq 破棄 → power-domain driver_remove** の順(DMC が先)。
- 診断: revid/step/DMC ver/各ID present・fw_size を teardown で採取(書式文字列の生改行混入でビルドが1回割れ→修正済)。

### 実機(GPU渡し, chaos, image d284fe1a)
`intel_dmc_init: DMC load queued (path=i915/adlp_dmc.bin, work_submitted=1)` / `modeset/flip workqueues: modeset=1 flip=1` / **`intel_dmc_fini (worker synced; load_seq_completed=1, payload_writes=12982, dmc_ref_held=0)`** / `attach end: reached=P3 outcome=BLOCKED where=intel_mode_config_init err=0`。DMC参照は worker が成功時に自身で解放、teardown クリーン、fault 0。
- GPU-free(固定 'D0')13019 vs 実機 12982(-37 DWORD): 実機は PCI revision(0x0c) 由来 stepping でエントリ選択が異なるため(失敗でない)。**専門家判断(A)=実機正として受理、裏取り不要**。

### GPU-free(chaos, GPU渡さず)
`CAS-SELFTEST PASS` / **ktest 184/0**(178→184 = BAD-FW2 + FINI(running)2 + FINI(fault)2)/ selftest=PASS。回帰なし。

### 残(次): P3後段(intel_mode_config_init〜fbc_init)→P4 IRQ→P5 display nogem→P6 gem/gt init→実機EU試験(工程表は第E-86報の別紙)。
台帳E-60〜E-86。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持, 描画/EU再試験・hang原因探索は保留継続。

## p011 増分E-87 (2026-09-18): P3後段(intel_mode_config_init〜intel_fbc_init)を完成、**実機で intel_display_driver_probe_noirq() 完走**。GPU-free 197/0。併せて **PCI ID 読出しの不具合を発見(E-86の12982解釈を覆す)**
専門家承認(判断①readout+sanitize優先 / ②execlists先行 / 「P3後段をまず完成」)を受け実施。build 0 error/warning。新規 display_state.{c,h}。

### 正本順序(intel_display_driver_probe_noirq 末尾)を全実装
`intel_mode_config_init` → `intel_cdclk_init` → `intel_color_init` → `intel_dbuf_init` → `intel_bw_init` → `intel_pmdemand_init` → `intel_init_quirks` → `intel_fbc_init` → return 0。error label は `intel_dmc_fini`→`intel_power_domains_driver_remove`＝既存 teardown 順と一致。
- **intel_atomic_global_obj_init**(共通機構を1回移植): memset→state->obj→kref_init(1)→obj->state/funcs→**list_add_tail**。cdclk/dbuf/bw/pmdemand の4つが登録し、**挿入順(cdclk,dbuf,bw,pmdemand)が観測可能**。
- **mode_config**: ADL-P(ver13)=max 16384x16384 / cursor 256x256 / min 0x0 / depth 24 / prefer_shadow 1 / **async_page_flip=1**(HAS_ASYNC_FLIPS=ver>=5)。version ラダー(>=7/>=4/==3/else)と cursor ラダー(i845/i865, i830/i85x/i915g/i915gm, else)を**分岐として保持**(legacy_platform enum で選択、ADL-P=NONE)。
- **intel_color_init**: `DISPLAY_VER != 10` で即 0(ADL-P)。ver10 の linear degamma LUT blob は DRM property blob 不在のため **-ENOSYS**(未実装を静かな成功にしない)。
- **intel_bw_init**: global obj 登録 + **ADL-P では icl_force_disable_sagv が実際に走る**(no-op でない)。`intel_has_sagv`(HAS_SAGV=ver>=9&&!IS_LP かつ status!=NOT_CONTROLLED) && ver 11..13。
  - **icl_max_bw_index と tgl_max_bw_index は互いの変種でない**: 走査方向(前進/後退)・比較(`num_planes >=` / `<=`)・既定値(UINT_MAX / 0)がすべて逆。両方を逐語移植し、**両者が別 group を選ぶ fixture で差を検出**。
  - `icl_qgv_points_mask` = QGV_PT(GENMASK(nq-1,0)) | PSF_PT(GENMASK(npsf-1,0))、`prepare` = ~(qgv|psf<<8) & mask、`restrict` = **skl_pcode_request(mbox 0xe, reply_mask 0xf, reply 0x0, 1ms)**、`is_sagv_enabled` = !is_power_of_2(~mask & qgv_mask & 0xff)。
  - **2つの bw 状態を混同しない**: `display.bw.max[]`/`sagv.status`(P2のHW表=parity_bw_state) と `struct intel_bw_state`(ここで作る atomic global state=qgv_points_mask 保持)。
- **intel_pmdemand_init**: global obj のみ。Wa_14016740474 は **ver14 A0..C0 限定=ADL-Pでは不発**。
- **intel_init_quirks**: intel_quirks[] 25件を逐語移植(hook は intel_set_quirk(id)+info log に還元)。DMI 側は **dmi_check_system 不在を記録**(dmi_available=0、黙って省略しない)。
- **intel_fbc_init**: need_fbc_vtd_wa(VT-d かつ skl/bxt のみ)→sanitize(tri-state: param>=0 / !HAS_FBC→0 / BDW||ver>=9→1)→**fbc_mask の各 bit ごとに intel_fbc_create**(INIT_WORK + mutex_init + funcs 選択)。**ADL-P(xe_lpd) fbc_mask=BIT(INTEL_FBC_A) の1個のみ**(MTLの XE_LPDP は A|B。誤って2個にしない)。
- **データ是正**: parity_sagv_status を正本値へ(UNKNOWN=0, **DISABLED=1**(欠落していた), ENABLED=2, NOT_CONTROLLED=**3**)。旧 NOT_CONTROLLED=1 は誤り。

### 試験(GPU-free) 184→**197/0**
DS-MODE(ADL-P値)/DS-MODE ladders(全arm)/**DS-INDEX(ver13=tgl→BIT1 と ver11=icl→BIT0 で異なる+psf同値はOR)**/DS-OBJ(順序・back-link・kref=1・funcs別・ver14 WA不発)/**DS-SAGV(PCODE 0xe data 0x5、status→DISABLED)**/DS-SAGV-SKIP(NOT_CONTROLLED で PCODE 0件)/DS-SAGV-FAIL(PCODE失敗でも bw_init は0、sagv.status 不更新)/DS-ENOMEM(3番目の alloc 失敗→-ENOMEM、obj_list不変)/DS-COLOR(ver10=-ENOSYS)/DS-QUIRKS(ADL-P非該当・PCI一致・subdev不一致・DMI)/DS-FBC(vtd WA/tri-state/個数/funcsラダー)/DS-FINI。

### 実機(GPU渡し, chaos)
**`P3 probe_noirq COMPLETE: global objs=4 (order: cdclk,dbuf,bw,pmdemand) mode_config max=16384x16384 cursor=256x256 async_flip=1`**
`intel_bw_init: obj=3 sagv_forced=1 qgv=0x4 psf=0x6 mask=0x10b pcode=0 sagv_status=1` ← **実機HWのQGV表でSAGV強制無効がPCODE成功**。
`intel_fbc_init: fbc_mask=0x1 enable_fbc=1 created=1 funcs=6(IVB)`。teardown = display-state fini → DMC fini → power domains → …逆順成立。**`reached=P3 outcome=BLOCKED where=intel_irq_install err=0`**(新frontier=P4)。

### ★発見: PCI ID 読出しが不正(E-86の判断(A)を覆す)
同一実行内で:
```
PCIID cached: ven=0xffff dev=0xffff rev=0x00 subsys=ffff:ffff
PCIID live  : ven=0x8086 dev=0x46a8 rev=0x0c subsys=1028:0b02
```
`drv_pci_device_vendor/product/revision/subvendor/subproduct`(キャッシュ済PCIヘッダ)が**全て0xffff/0**を返す。live config read(`osdep_pci_read8/16`、P0で `cfg_vendor=0x8086` と実証済の経路)は正しい ADL-P 実体を返す。
- **DMC**: revid=0x0→step **A0** を選択していた(正しくは rev 0x0c→**D0**)。**payload MAIN=6237(A0) vs 6274(D0)、差37=12982 vs 13019 に一致**。→ **E-86の「12982=実機正」は誤り。12982 は誤った stepping の payload。**
- **CDCLK(E-75, 既存)**: 同じ accessor を使用(probe.c:706)。revid=0x0→STEP_A0 → Wa_22011320316 の **a-step table** を選んでいた(rev 0x0c の正解は adlp_cdclk_table)。
- **quirks(本増分)**: 0xffff で照合していた。live では subsys=1028:0b02(Dell)。いずれも intel_quirks[] に ADL-P 項目が無いため**結果は不変**(quirk_mask=0)。
- 修正案: 3箇所の `drv_pci_device_*` を live config read へ(P0で実証済の経路)。**CDCLK の table 選択が変わるため、専門家確認待ち**(本増分では診断のみ、挙動未変更)。

### 残(次): 判断待ち=PCI ID修正の適用可否。その後 P4 intel_irq_install(唯一HAL圧の出うる箇所, MSI-split維持)→P5 display_nogem(readout+sanitize優先)→P6 gem/gt(execlists先行)→P7前半→実機EU試験(要・明示解除)。
台帳E-60〜E-87。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持, 描画/EU再試験・hang原因探索は保留継続。

### E-87b (2026-09-18, 同増分の追補): 専門家判断(A)により PCI ID を live config read へ修正、実機で stepping 是正を確認
`drv_pci_device_*`(キャッシュ済PCIヘッダ, 実機で全0xffff/0)を使っていた3箇所を **live config read**(`osdep_pci_read8/16`、P0の `cfg_vendor=0x8086` で実証済の経路)へ置換: CDCLK stepping(probe.c:706相当)/DMC stepping(同749相当)/quirk PCI ID(同853相当)。cached-vs-live 診断ログは記録として残置。build 0 error/warning。

**実機(GPU渡し, chaos)で是正を確認:**
```
intel_dmc_init: revid=0xc step=D0            (修正前 revid=0x0 step=A0)
PCIID cached: ven=0xffff dev=0xffff rev=0x00 subsys=ffff:ffff | live: ven=0x8086 dev=0x46a8 rev=0x0c subsys=1028:0b02
intel_init_quirks: dev=0x46a8 subsys=1028:0b02 quirk_mask=0x0   (修正前 dev=0xffff subsys=ffff:ffff)
intel_dmc_fini (load_seq_completed=1, payload_writes=13019, dmc_ref_held=0)   (修正前 12982)
DMC ver=2.20 payload=6274/2547/3066/566/566                                   (修正前 MAIN=6237)
probe_noirq COMPLETE / reached=P3 BLOCKED where=intel_irq_install err=0
```
→ **payload_writes が 12982→13019 となり GPU-free フィクスチャ(step 'D0' ハードコード)と完全一致**。MAIN 6237(A0)→6274(D0)、差37の説明が閉じた。quirk は live ID でも `intel_quirks[]` に ADL-P 項目が無く quirk_mask=0(結果不変、入力のみ是正)。
GPU-free 回帰: `CAS-SELFTEST PASS` / **ktest 197/0** / selftest=PASS(probe=NOT_RUN)。
注: 実機(ref構成)は attach 完走後 ktest 実行中に 120s timeout で打切り(従来どおり ktest は GPU-free 側で採取)。

**★副産物の発見(CDCLK 忠実性ギャップ, 本デバイスには無影響)**: `parity_intel_init_cdclk_hooks` の3分岐が**すべて同じ `adlp_cdclk_table` + TGL funcs を代入**しており、正本の **`adlp_a_step_cdclk_table` が未移植**(コメントだけ "not this device")。正本の a-step 表は内容が別物(307200/556800/652800 系で、**179200 のエントリを持たない**)。
- 本デバイス(rev 0x0c = D0)の正解は `adlp_cdclk_table` なので **実機の挙動は正しい**(cdclk=179200 vco=537600 は修正前後で不変)。修正前は「A0と誤判定したが、分岐が同一実装だったため偶然正しい表を引いていた」状態で、修正後は**正しい理由で正しい表**を引く。
- 残ギャップ: A0/A1 シリコンでは誤った表を引く(Wa_22011320316 未実装)。本機では再現不能。**台帳に分離して記録、次増分以降の判断事項**。

台帳E-60〜E-87。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-88 (2026-09-18): P4=intel_irq_install 完成。**実機で gen11 IRQ を install→master enable→uninstall まで成立**。HAL 変更なし。GPU-free 207/0
専門家承認「P4の作業」を実施。新規 irq.{c,h} + pch.{c,h}、power_domains に `parity_display_power_is_enabled` 追加。build 0 error/warning。

### HAL は変更していない(事前確認どおり)
HAL には既に**分離済み MSI API** が存在し、正本の request_irq/free_irq がそのまま対応: `hal_irq_alloc_msi`(P2で実施済、ハンドラ未装着。未装着中の到着は mask+ack され NULL dispatch されない)/ `hal_irq_attach_msi`(=request_irq)/ `hal_irq_detach_msi_sync`(=free_irq、全CPUで実行完了保証)/ `hal_irq_free_msi`(source停止後)。P2 が `msi_kept=1` で vector を保持していたので接続のみ。**IF=0 制約も非該当**(parity attach は管理 kthread から走る)。
**適応差分2点(HAL変更ではない)**: ①ハンドラ署名は `void(int, hal_irq_ack_t, void*)` で **EOI はハンドラ責務**(全経路で送出)。Linux の IRQ_HANDLED/IRQ_NONE は MSI では spurious 検出用のみなので、戻り値でなく device state に記録。②`IRQF_SHARED` は MSI で無意味、HAL に相当なし。

### P4 本体(irq.c)
- `intel_irq_install`: irqs_enabled/irq_enabled を **postinstall より前**に立てる(正本の順序)→`intel_irq_reset`→`hal_irq_attach_msi`(失敗時 irq_enabled=0 で返す)→`intel_irq_postinstall`。`pwc.irqs_enabled` も同時に立てる(power-well post_enable の `intel_irqs_enabled()` gate)。
- `gen11_irq_reset`: master disable(書込後 read で level 採取)→`gen11_gt_irq_reset`→`gen11_display_irq_reset`→GU_MISC→PCU。`gen3_irq_reset` は IMR=~0→posting→IER=0→**IIR を 2回**クリア(正本の paranoid)。
- `gen11_gt_irq_reset/postinstall`: ADL-P のエンジンは **RCS0|BCS0|VECS0|VCS0|VCS2**、CCS/GSC0/HECI-GSC 無しなので Xe-HP 系レジスタは書かない。**execlists(判断②)** ゆえ `irqs = RENDER_USER|CS_MASTER_ERROR|CONTEXT_SWITCH|WAIT_SEMAPHORE = 0x909`、dmask=0x09090909/smask=0x09090000。GuC 側の分岐は明示的に残置(判断②の反対側)。
- `gen11_display_irq_reset` / `gen8_de_irq_postinstall`: DISPLAY_INT_CTL=0 → 電源の入った transcoder の TRANS_PSR_IMR/IIR → 電源の入った pipe の DE_PIPE → DE_PORT → DE_MISC → GEN11_DE_HPD → (>=PCH_ICP)SDE。postinstall は icp_irq_postinstall(SDE)→per-pipe IER→DE_PORT/MISC→TC/TBT hotplug→DISPLAY_INT_CTL enable。
- **ADL-P の解決値**: pipe_fault=**RKL_**(0x00100F80, ver>=13。GEN11_ の 0x00700F80 ではない)、port_aux=0x00003F07、underrun=0x80600000、flip_done=0x8 → pipe_masked=0x10100F80 / pipe_enables=0x90700F89 / misc_masked=0x00080000(EDP_PSR のみ、ver>=11 ゆえ GSE 無し)/ de_hpd_enables=0x003F003F。
- `intel_irq_uninstall`: **先に全 source を reset**(ハンドラを外しても device は送信を止めない)→flags クリア→`hal_irq_detach_msi_sync`。vector 解放は P2 資源として probe.c 側。
- ハンドラ(`gen11_irq_handler`): irqs_enabled 確認→master disable→master_ctl==0 なら再 enable して IRQ_NONE 相当→source 計数→GU_MISC IIR ack→master enable→EOI。**GT/display の bottom half は P5/P6 の範囲なので現状は計数のみ**(サービスしない)ことをコメントで明記。

### 前提部品
- **pch.{c,h}**(soc/intel_pch.c 移植): id テーブル全件(IBX〜ADP)、`intel_is_virt_pch`、`intel_virt_detect_pch`(ADL→ADP)、ISA ブリッジ走査(非Intelはskip、全件走査して最初の一致)。**正本が QEMU q35 を明示対応**(`INTEL_PCH_QEMU_DEVICE_ID_TYPE 0x2900 /* qemu q35 has 2918 */` + Red Hat/QEMU subsystem)。PCH_ADP(8) >= PCH_ICP(6) が P4 の gate。ブリッジの identity も **live config read**(E-87b と同じ理由)。
- **`parity_display_power_is_enabled`**: 正本は well を**逆順**に走査し、always_on は skip、**キャッシュ済 hw_enabled** で判定(HW 再読しない)。hw_enabled==-1(未同期)は「有効」と解釈しない。

### 試験(GPU-free) 197→**207/0**
IRQ-MASKS(ADL-P と近傍 ver の gen8_de_* 解決値)/PCH-QEMU(0x2918→virt→ADP、>=ICP)/PCH real・subsys不一致・ブリッジ皆無・NOP・idテーブル・virt述語/IRQ-RESET(master 最初、各ブロック mask、**IIR 2回**)/IRQ-POST(dmask/smask、**master enable が最後**、ADL-P DE マスク一式、icp SDE、TC/TBT)/IRQ-GUC(GuC 側は CS 3本を落とし irqs=0x1)/IRQ-POWEROFF/IRQ-NODISPLAY。
- **罠(試験側の誤り、コードは正本どおり)**: IRQ-POWEROFF で「transcoder A も skip される」と期待したが、**TRANSCODER_A を持つ well は存在せず** always-on well のみが覆う。正本は always_on を skip するのでドメインは「有効」判定が正しい。期待値を実挙動へ是正。

### 実機(GPU渡し, chaos)
```
P4 intel_detect_pch: type=8 id=0x7a80 source=2 bridges=1 bridge_dev=0x2918 subsys=1af4:1100
P4 intel_irq_install: rc=0 msi_irq=16 attached=1 gt_irqs=0x909 dmask=0x9090909 smask=0x9090000 reset_writes=62 post_writes=28
P4 de masks: pipe_masked=0x10100f80 pipe_enables=0x90700f89 port_masked=0x3f07 misc_masked=0x80000 de_irq_mask[A]=0xefeff07f master_enabled=1
P4 irq observed: count=0 handled=0 none=0 gt=0 display=0 last_master_ctl=0x0 gu_misc_iir=0x0
teardown: intel_irq_uninstall (sources reset, handler detached; irq_count=0 handled=0 none=0)
attach end: reached=P3 outcome=BLOCKED where=intel_display_driver_probe_nogem err=0
```
- **PCH は正本が想定する QEMU 経路そのもの**(q35 ISA bridge 0x2918 / Red Hat・QEMU subsystem → virt → ADP)で検出。推測ではなく正本の分岐を通っている。
- 全マスクが GPU-free の予測値と**完全一致**。master enable 到達、clean uninstall。
- **割込みは 20ms の観測窓で 0 件**。engine 未起動・display 未活性・vblank 無しなので発生源が無く、想定どおり(失敗ではない)。実際の割込み受信の実証は P5/P6 で発生源ができてから。
- 注: 実機 ref 構成は attach 完走後 ktest 途中で 120s timeout(従来どおり ktest は GPU-free 側で採取)。timeout を 240s にしても着地点は実行ごとにばらつき、**ハングではなく kill のタイミング差**であることを3回目の完走で確認。
- 注: `reached=P3` は runner に P3 を要求しているためのラベルで、`where=` が示す停止点(P5 入口)が実際の frontier。

### 残(次): P5 intel_display_driver_probe_nogem(判断①=readout+sanitize 優先、setup_outputs は検出・ログまで、BIOS fb 引き継ぎ無し)→P6 gem/gt(execlists 先行)→P7前半→実機EU試験(**描画再試験に当たるため専門家の明示解除が必要**)。
台帳E-60〜E-88。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-89 (2026-09-18): P5-0 実機サーベイ(=sanitize 範囲が確定)＋P4 ハンドラの display IIR ack 補強＋P5-a(probe_nogem 前段)。GPU-free 223/0、実機で frontier=intel_setup_outputs
専門家承認「P5計画で進めてください」を実施。新規 display_nogem.{c,h}、vga.c に intel_vga_disable 追加。build 0 error/warning。**HAL 非変更**。

### P5-0 実機サーベイ(診断のみ、電源ゲート付き読出し) — リスク2が解消
```
pipe A..D: power(pipe=1 trans=1) TRANSCONF=0x00000000 TRANS_DDI_FUNC_CTL=0x00030000 PLANE_CTL(1)=0x00000008
DDI A/B/TC1..TC4: DDI_BUF_CTL=0x00000080
PLL DPLL0/DPLL1/TBT/TC1..TC4: ENABLE=0x00000000
wells: total=30 on=8 on_unused=0
```
→ **TRANSCONF_ENABLE(b31)・TRANS_DDI_FUNC_ENABLE(b31)・PLANE_CTL_ENABLE(b31)・DDI_BUF_CTL_ENABLE(b31)・PLL_ENABLE(b31) がすべて clear**。デバイスは完全に静止状態。
- `intel_sanitize_crtc` は全 pipe で `!hw.active` により早期 return → **`intel_crtc_disable_noatomic` / `hsw_crtc_disable`(フル modeset disable, +2〜3 DMC)は不要**。**判断①の範囲内で P5 が完結**する見通しが立った(計画のリスク2が実測で解消)。
- `sanitize_dpll_state` は全 PLL off ゆえ `!pll->on` で return、`power_domains_sanitize_state` は on_unused=0 ゆえ disable 対象なし → **P5-d で実 HW を OFF にする操作は発生しない見込み**。
- 8 wells on = always_on + PW_1 + PW_2 + PW_A..D + DC_off(すべて INIT 参照が保持)で refcount>0、整合。

### P4 補強: display IIR ack 経路(既出 P4 の実ギャップ修正、HAL非変更)
P4 は `GEN8_PIPE_VBLANK` 等を **IER で enable 済み**だが、ハンドラが DE_PIPE/DE_PORT/DE_MISC/DE_HPD/SDE の IIR を読まず ack しないため、いずれかの source が上がると **master 線が立ちっぱなし＝割込みストーム**になる状態だった。`gen11_display_irq_handler` → `gen8_de_irq_handler` を移植:
- DISPLAY_INT_CTL を 0 に gate → 各 source の IIR を read → **非ゼロなら同値を write-back(ack)** → 種別を計数(vblank/flip_done/underrun/fault) → DISPLAY_INT_CTL を再 enable。
- master bit が立っているのに IIR が 0 の場合は正本同様「lied」として計数(ack しない)。
- bottom half(vblank/flip/underrun 報告/HPD/AUX)は P5+ の範囲なので **ack して計数するだけ**であることをコード内に明記。
- 試験 +5(IRQ-ACK): 全 source 同時 assert の decode、ack が「読んだ IIR 値の write-back」であること、DISPLAY_INT_CTL の gate off→on 順、lied 経路、未 assert source を触らないこと。
- 罠(試験側): セットアップの書込みが書込みトレースに残り誤検出。`f.wt_n=0` をセットアップ後へ移動。

### P5-a: probe_nogem 前段(intel_wm_init 〜 intel_vga_disable)
正本順: `intel_wm_init`(→skl_wm_init: intel_sagv_init + skl_setup_wm_latency) → `intel_panel_sanitize_ssc`(ADL-P で実質なし) → `intel_pps_setup` → `intel_gmbus_setup` → `intel_crtc_init`×4 → `intel_plane_possible_crtcs_init`/`intel_shared_dpll_init`/`intel_fdi_pll_freq_update`(ADL-P return)/`intel_update_czclk`(return) → `intel_display_driver_init_hw`(intel_update_cdclk + logical=actual=hw + **adlp_display_wa_apply**) → `intel_dpll_update_ref_clks` → `intel_hdcp_component_init` → `intel_update_max_cdclk` → `intel_hti_init` → `intel_vga_disable`。
- **wm latency**: PCODE `GEN9_PCODE_READ_MEM_LATENCY`(0x6) を data0=0/1 で **2回**読み 8 段を復号 → `adjust_wm_latency`(level n>=1 が 0 なら n.. を 0 化 / level0==0 なら全段に read_latency(ver>=12→3)加算 / 16GB DIMM WA は level0 に +1)。**HAS_HW_SAGV_WM(ver>=13 && !DGFX) → num_levels=6**(8 ではない)。
- **SAGV**: icl+ は bw_init で確定済みなので status は P2 の bw 状態を引き継ぎ、block_time は PCODE 0x23 から読む。
- **device 表から確定した値(推測せず)**: `num_scalers[pipe]=2`(ver>=11)、`num_sprites[pipe]=4`(ver>=13) → **1 pipe = primary+4 sprite+cursor = 6 plane**、**xe_lpd に `has_hti` は無い**(RKL/ADL-S のみ)→ `intel_hti_init` は no-op で HDPORT_STATE を読まない、`intel_ddi_crt_present` は ver>=9 で false。
- **shared DPLL**: adlp_plls = DPLL0/DPLL1(combo) + TBT + TC PLL1..4(dkl) の **7本**、enable reg 0x46010/14/20/30/34/38/3c。
- **gmbus**: mmio_base=PCH_DISPLAY_BASE(0xc0000)、ICP pin 表 9件(dpa/dpb/dpc/tc1..tc6)、GMBUS0/GMBUS4 reset。**i2c adapter は i2c core 不在のため作らず「未実装」を記録**(静かな成功にしない)。同様に hdcp component / acpi fwnode も記録のみ。
- **adlp_display_wa_apply**: Wa_22011091694(GEN9_CLKGATE_DIS_5 |= DPCE_GATING_DIS) / Bspec49189(GEN8_CHICKEN_DCPR_1 &= ~DDI_CLOCK_REG_ACCESS)。rmw は正本 `intel_uncore_rmw` と同じく**値が変わらなければ書かない**。
- **intel_vga_disable** は正本と同じく vga.c 側へ実装(legacy IO アクセサがそこに閉じているため)。VGA_DISP_DISABLE 済みなら即 return、さもなくば legacy IO で SR01 |= SCREEN_OFF → 300us → CPU_VGACNTRL に VGA_DISP_DISABLE。VGA 定数は実カーネル `video/vga.h` で確認(SEQ_I=0x3C4/SEQ_D=0x3C5/SR01_SCREEN_OFF=0x20)。

### 試験(GPU-free) 212→**223/0** (P5-a 11件、内訳 WM4/DPLL2/CRTC1/MAXCDCLK2/WA2/VGA2 相当)
P5A-WM(2取引の復号 + adjust 3規則)/P5A-DPLL(7本の id・funcs・reg、ver>=14 は表を捏造しない)/P5A-CRTC(6 plane・2 scaler・plane_ids_mask=0x9f・cpu_transcoder=-1)/P5A-MAXCDCLK(ref 38.4→652800、24→648000)/P5A-WA(2つの rmw、非ADL-P は無書込み)/P5A-VGA(既 disable なら IO に触れない / 未 disable なら get→out→in→out→put の5手順の後にレジスタ書込み)。

### 実機(GPU渡し, chaos)
```
P5a wm: levels=6 latency=3/54/83/102/147/147/144/144 valid=1 sagv_status=1 block_time=35us
P5a objects: crtcs=4 planes/crtc=6 scalers=2 dplls=7(mgr=1) gmbus_pins=9 pps_base=0x61200 gmbus_base=0xc0000
P5a hw: max_cdclk=652800 nssc_ref=38400 adlp_wa=1 hti_read=0 vga_already_off=1 vga_disabled=0 writes=2
attach end: reached=P3 outcome=BLOCKED where=intel_setup_outputs err=0
```
- **wm latency は実機 PCODE の実値**。level 0 が 3 で level 6,7 が 144 のまま＝raw level0 が 0 だったため read_latency(3) が **level 0..5 にだけ**加算された＝**num_levels=6 のゲートが実機で効いている**実証。
- `vga_already_off=1`: VGA_DISP_DISABLE は既に立っており正本同様 early return(rombar=0 で GOP が IGD を駆動していないことと整合)。
- `writes=2` は gmbus reset の 2 件のみ。ADL-P WA の 2 つの rmw は **対象ビットが既に目的の状態**で書込み不要だった(正本 `intel_uncore_rmw` も差分なしなら書かない)。DCPR_1 の読み値が all-ones でないことは「clear 側が無書込みで済んだ」ことから逆に裏付けられる。

### 残(次): P5-b(intel_setup_outputs = intel_ddi_init の判定部＋encoder レコードまで)→P5-c(readout)→P5-d(sanitize、P5-0 の結果より実 HW OFF は発生しない見込み)→P6(gem/gt, execlists 先行)→P7前半→実機EU試験(**要・明示解除**)。
台帳E-60〜E-89。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-90 (2026-09-18): **P5 完成** — P5-b(setup_outputs)/P5-c(readout)/P5-d(sanitize)。実機で `intel_display_driver_probe_nogem()` 完走、frontier=**i915_gem_init(P6)**。GPU-free 243/0
E-89 の続き。build 0 error/warning。**HAL 非変更**。判断①(readout+sanitize 優先)の範囲で P5 が完結。

### P5-b: intel_setup_outputs(判定部＋encoder レコード)
- `HAS_DDI` → `intel_ddi_crt_present`(ver>=9 で false) → `intel_bios_for_each_encoder(intel_ddi_init)`。
- **`dvo_port_to_port` は ver>=13 で xelpd マップ**を使う: TC ポートは HDMIF..HDMII / DPF..DPI を取り、**HDMIC/HDMID ではない**(pre-xelpd と取り違えると TC が誤マップされる)。
- `intel_ddi_init` の早期 return を全て再現・記録: PORT_NONE / strap(ver<9) / **assert_port_valid** / port_in_use / DSI / HTI / not DVI-HDMI-DP。
- `intel_port_to_phy`(ver>=13: TC1→PHY_F) / `intel_phy_is_tc`(PHY_F..I) / `intel_ddi_is_tc`(ver>=12: port>=TC1) / `intel_display_power_ddi_lanes_domain`(d13 表)。
- clock ops は ver>=11 かつ非 ADLS/RKL/DG1/JSL/EHL ゆえ **icl combo(非TC) / icl TC** の2系統。`ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy)=1<<_PICK(phy,10,11,24,4,5)`、**`TC_CLK_OFF(tc)` は連続でない**(TC_PORT_4 以降が bit21 から再開)。TC 側は `DDI_CLK_SEL != NONE` かつ `TC_CLK_OFF` clear の**両方**で enabled。
- **★正本どおりの帰結**: missing-defaults VBT は PORT_A/B/C の3子デバイスを生成するが、**ADL-P の port_mask に PORT_C は無い**ため3件目は `assert_port_valid` で棄却され `WARN Platform does not support port C`。→ **encoder は 2本**。
- スコープ外(記録): DRM encoder 登録 / DP・HDMI サブ初期化 / AUX / HPD / connector 生成。

### P5-c: intel_modeset_readout_hw_state
- `hsw_get_pipe_config`: PIPE 電源 gate → `hsw_enabled_transcoders`(xe_lpd は panel transcoder が **DSI_0/DSI_1 のみ**、TRANSCODER_EDP は runtime mask に無い + 自 pipe の transcoder) → TRANSCODER 電源 gate → **TRANSCONF.ENABLE** で active 判定 → timings(TRANS_HTOTAL/VTOTAL、各フィールド +1) + PIPESRC。
- **TRANSCONF は PIPE オフセット(0x70008+pipe*0x1000)**で、transcoder オフセットではない。DSI transcoder のレジスタは 0x1000 等間隔でない(0x6b000/0x6b800)ので専用テーブル。
- `readout_plane_state`(PLANE_CTL.ENABLE)、encoder は `intel_ddi_get_hw_state`(DDI_BUF_CTL.ENABLE → TRANS_DDI_FUNC_CTL の port-select 走査、MST 検出)、`intel_dpll_readout_hw_state`(PLL_ENABLE)。
- **書込みゼロ**(電源照会と読出しのみ)。
- **記録した未実装**: color config / DSC / VRR / bigjoiner / scaler 詳細 / output_format / linetime / pixel_multiplier / framestart_delay。いずれも sanitize の判断に入らない。connector ループは空(判断①ゆえ connector 不在)＝connector 起点の sanitize は発火しない、を明記。

### P5-d: sanitize
`intel_early_display_was`(ver10..12 のみ→ADL-P 不発) / `intel_pch_sanitize`(IBX のみ→不発) / per-crtc(underrun 報告初期化・vblank reset・active なら **PIPEDMC_CONTROL enable + vblank on**) / `intel_fbc_sanitize`(DPFC_CTL_EN clear) / `intel_sanitize_plane_mapping`(**ver>=4 で return**→不発) / `intel_ddi_sanitize_encoder_pll_mapping` / `intel_sanitize_all_crtcs` / `intel_dpll_sanitize_state`(on かつ active_mask==0 → PLL_ENABLE clear、+`adlp_cmtg_clock_gating_wa` は **ADL-P A0..B0 かつ DPLL0 のみ**→D0 では不発) / `intel_wm_get_hw_state` / `intel_power_domains_sanitize_state`(**逆順**、always_on でなく refcount==0 かつ **fresh is_enabled()** → disable)。
- **`intel_crtc_disable_noatomic` は未実装**。到達条件は「active かつ encoder 無し」のみで、P5-0 の実測によりこのデバイスでは発生しない。発生した場合は**黙って無視せず WARN + フラグ**を立てる。
- 注: `power_domains_sanitize_state` は **fresh read**、`__intel_display_power_is_enabled` は **cached** を使う(正本の使い分けをそのまま保持)。

### 試験(GPU-free) 229→**243/0**
P5B-PORTMAP(xelpd 対 legacy マップ、port→phy、is_tc、CRT不在)/P5B-OUTPUTS(**3子→2 encoder、PORT_C は assert_port_valid で棄却**、早期 return 全経路、eDP は DP のみ・PORT_B は DP+HDMI)/P5B-DDICLK(combo は DDI_CLK_OFF、TC は CLK_SEL と TC_CLK_OFF の両方)/P5C-READOUT(静止・**pipe B active fixture**(timings/PIPESRC/plane/encoder link/DPLL)・**電源OFF は「inactive」でなく power-gated と記録**)/P5D-SANITIZE(静止で全 no-op)/P5D-DPLL(未使用のみ off)/P5D-CMTG(A0 かつ DPLL0 のみ)/P5D-FBC(active のみ deactivate、EN ビットだけ落とす)/P5D-ENCCLK(**disabled encoder のみ** gate)/P5D-ACTIVE(active pipe は DMC+vblank を入れ、未実装の disable_noatomic を偽装せずフラグ)/P5D-WELL(未参照 well を逆順 disable)。

### 実機(GPU渡し, chaos)
```
P5b setup_outputs: vbt_children=3 ddi_init=3 encoders=2 skipped=1 crt_present=0
P5b encoder[0]: port=A phy=0 tc=0 clk=1 pd=17 dvo=0x00 dev_type=0x1004 dp=1 hdmi=0
P5b encoder[1]: port=B phy=1 tc=0 clk=1 pd=18 dvo=0x01 dev_type=0x14   dp=1 hdmi=1
P5b ddi skip[0]: port=2 reason=3        (= assert_port_valid, "Platform does not support port C")
P5c [CRTC:A..D] hw state readout: disabled ×4
P5c [ENCODER port A/B] hw state readout: disabled pipe_mask=0x0 mst=0
P5c DPLL0/1/TBT/TC1..4 hw state readout: pipe_mask 0x0, on 0
P5c readout: crtcs=4 active_pipes=0x0 planes_visible=0 encoders_linked=0 dplls_on=0
i915: parity [ENCODER port A] is disabled with an ungated DDI clock, gate it
P5d sanitize: vblank_resets=4 dmc_pipes=0 vblank_on=0 fbc_deact=0 enc_clk_gated=1
              dplls_disabled=0 wells_disabled=0 cmtg_wa=0 early_was=0 wm_read=1
              (crtc_disable_noatomic needed: 0)
attach end: reached=P3 outcome=BLOCKED where=i915_gem_init err=0
```
- **P5 完走。frontier は P6(`i915_gem_init`)**。
- P5-c の readout が **P5-0 サーベイを独立に裏付け**(全 CRTC/encoder/PLL が disabled)。
- **sanitize は実機で実際に1件是正した**: PORT_A は encoder が disabled なのに DDI クロックが ungated で残っており、正本どおり gate した(`enc_clk_gated=1`)。他は全て no-op で、P5-0 からの予測どおり。
- `crtc_disable_noatomic needed: 0` = フル modeset disable は不要、判断①の範囲で P5 が閉じたことの実機確認。

### 残(次): **P6 i915_gem_init → intel_gt_init**(判断②=execlists 先行)。WA表/MOCS/SSEU/RC6/context image が hang 候補の本命。→P7前半→実機EU試験(**描画再試験に当たるため要・明示解除**)。
台帳E-60〜E-90。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-91 (2026-09-18): **P1 `intel_gt_init_mmio` → P3 ハングの根本原因 2 件を特定・修正**(カーネル側)。P6-0/a/b は参照位置(P1)のまま実機完走、frontier=**P6-c `__engines_record_defaults`**

### 事象
P6-0a `parity_intel_gt_init_mmio` を参照どおり P1 に置くと、P3 `intel_power_domains_init_hw` 内(2 回目 combo_phy_init 直後)で毎回ログ 175 行目で停止。forcewake 有無・fault clear 有無・MMIO 書込み破棄でも不変。「P6 先頭へ移す」回避は却下され、原因究明を指示された。

### 調査経路(全て実機ログで裏付け)
1. slow 段 sleep 計測: 停止点は **PW_1 の STATE 待ち**(`parity_wait_reg` fast=0→slow 段)で、`waitq_sleep`(1 tick 期限)に入ったまま二度と起きない(`SLEEP ... tick=599 dl=600 cpu=0 if=1` の後に `WOKE` 無し)。本来 2ms で `ACK timeout (continuing)` になる経路なので HW ではなくカーネルの起床機構。
2. 別 CPU のウォッチドッグ(自 CPU の LAPIC tick 数を時間源)で **CPU 0(BSP)だけ tick が消失**(0.7 s あたり CPU0 +4 / 他 CPU +72)し、CPU 0 だけが進めるグローバル tick(`kernel_ticks`/`scheduler_ticks`)が停滞 → tick 期限の sleep が全て破綻、を実測。
3. `WOKE ... if=0`: **`sched_sleep_locked()`(= `waitq_sleep` の実体)が `hal_irq_disable()` の戻り値を捨てて復元しない**(`sched_sleep()` は復元)。`dispatch.S` が RFLAGS を pushfq/popfq で保存復元するため、一度でも wait-queue で眠ったスレッドは以後 **IF=0 で走り続ける**。BSP 上で IF=0 のまま MMIO ポーリング等をすると周期 LAPIC 割込みは 1 個しか保留されず残りは消える。
4. IF 復元を修正すると別の停止: QEMU モニタで全 vCPU が `asm_hlt` (IF=0) = **パニック**。スクリーンダンプで `amd64 fault v=14 rip=0 err=0x10 cr2=0`(**NULL への命令フェッチ = 壊れた戻り先への ret**)。fault ハンドラ診断で **tid 2 = `wlan_retirement_worker`(CPU 1)**、i915 とは無関係の周期スレッド。
5. `-fstack-usage` で全カーネルのフレームを測定: **`parity_intel_power_domains_init_hw` 131,176 B / `parity_dc_off_enable` 131,144 B / `parity_sync_ktest` 137,448 B**(他は全て ≤ 4,568 B)。カーネルスレッドのスタックは **16 KiB**(`AMD64_SYS_STACK_SIZE`)。原因は `struct osdep_trace`(4096 レコード = 128 KiB)をローカル変数に置いたこと。P3 init_hw 突入で runner のスタックが約 115 KiB あふれ、`osdep_trace_init()` がその下のヒープ(他スレッドの `struct thread`/スタック)を**ゼロ埋め**していた。

### 根本原因(2 件、いずれもカーネル側)
| # | 欠陥 | 影響 | 修正 |
|---|---|---|---|
| RC-1 | `struct osdep_trace`(128 KiB)がスタック上(display_core.c ×2、ktest.c ×11) | P3 のたびに runner スタック下のヒープ ~115 KiB をゼロ埋め。被害対象はヒープ配置で変わる(P1 の `gt_init_mmio` は配置をずらしただけ) → 起床構造の破壊(元のハング)、他スレッドの戻り先 0 化(パニック) | display_core.c: 共有 static `dc_trace`。ktest.c: trace/fake-MMIO 等の大きなローカルを static。**`-Wframe-larger-than=8192` を AMD64_CFLAGS に追加**(再発をビルドエラーに) |
| RC-2 | `sched_sleep_locked` / `_interruptible` / `_notify` が IRQ 状態を復元しない | wait-queue で眠った全スレッドが以後 IF=0。BSP ではグローバル tick 枯渇 → tick 期限 sleep の停滞・遅延 | `sched_sleep()` と同じ規約で `enabled = hal_irq_disable()` … 復帰後 `if (enabled) hal_irq_enable()`(早期 return 経路含む) |

| RC-3 | (修正の検証中に発覚) `Makefile` の `.d` 取り込みが `$(BUILD)` 配下 **7 階層まで**で、`parity/osdep/*.d`(8 階層)が対象外 → `trace.h` の容量変更後も `osdep/trace.o` 等が**再ビルドされず**旧レイアウト(128 KiB)のまま link され、32 KiB リングの直後(`g_fw_test`、DMC テスト用ノブ)を上書き | GPU-free で DMC 3 件が「ノブが worker に見えない」形で失敗(実機 run も同じ汚染下で"合格"していた) | `Makefile` に `$(BUILD)/*/*/*/*/*/*/*/*.d` を追加 + `build/amd64/kern64` をクリーン再ビルド |

- P1 の `gt_init_mmio` 自体には欠陥なし(参照位置に**戻した**)。
- `OSDEP_TRACE_CAPACITY` 4096→**1024**(32 KiB; post-mortem dump は 256 件)。ktest の 11 リングは 2 スロットの共有 static プール、display_core は共有 static `dc_trace`。.bss は 4.67 MB(変更前と同規模。11×128 KiB を static にした中間版は .bss 6.2 MB で **UEFI が `Allocate kernel: EFI_NOT_FOUND`** となり起動不能=カーネル像サイズに上限がある点を記録)。
- P60-FAULT の期待値を正本に合わせて修正(`intel_gt_clear_error_registers` は RING_FAULT_VALID のみ rmw で落とす→書込み値は 0 ではなく 0x3000)。`check_and_clear_faults` は参照でも `intel_engines_init_mmio` 末尾で呼ばれる = P1 配置が正。
- 診断コード(ウォッチドッグ/sleep 計測/fault ハンドラ診断/CPU 別 tick 計数)は全て**除去**。カーネル差分は sched.c の IRQ 復元と vmunix.mk のフレーム guard のみ。
- HAL インタフェース不変(10ms tick / kernel_timer_handler / KERN_CLOCK_HZ / waitq 期限単位)。

### 試験(クリーン再ビルド後, vmunix 8c8bf007, .bss 4.67 MB, stale obj 0)
- GPU-free: **273/0**(P60-FAULT 期待値修正込み。RC-3 修正前は DMC 3 件が上記の汚染で失敗していた)
- 実機(GPU 渡し, chaos, **P1 配置のまま**):
```
P1 gt_init_mmio: clock=19200000Hz sseu(slice=0x1 dss=0x1f eu/ss=16 total=80) l3bank=0x7 engine_mask=0x10503 engines=5 fault=0x0
P3 intel_power_domains_init_hw(false) done: cdclk=179200 vco=537600 dbuf=0x7 init_ref=1 sync_hw=1
P3 probe_noirq COMPLETE ... P5d sanitize ... (crtc_disable_noatomic needed: 0)
P6b hw: pat=1 gt_wa(w=1 skip=4 ok=4 mismatch=0 noverify=1) mocs(global=64 l3cc=32) whitelist_writes=60 engines=5
attach end: reached=P3 outcome=BLOCKED where=intel_engines_init err=0   (= 設計上の P6-c 停止点)
ktest (completion+workqueue): 273 checks, 0 failures                     (post-attach, 693 行, panic 0)
```
- ログ: `~/bigbang/run-parity-hw-e91.log` / `run-parity-nogpu-e91.log`(chaos)。

### 残(次): **P6-c `__engines_record_defaults`**(最初の GPU コマンド実行 = null request、承認済み)→ P7 前半 → 実機 EU 試験(**要・明示解除**)。
台帳E-60〜E-91。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-92 (2026-09-18): **P6-c 完成 — `__engines_record_defaults` が実機で成功。parity 経路で初めて GPU が命令を実行**（5 エンジン全て、エラー 0）。frontier=`__engines_verify_workarounds`

### 実機結果（GPU 渡し, chaos, vmunix d35db04c）
```
P6c resume: rc=0 reset_engines=0 stop_cs_timeouts=0 engines_resumed=5 l3cc(rcs, mocs_init_engine)=32
P6c record_defaults: rc=0 polls=9 timed_out=0 wedged=0 | gt irq during: user=9 ctx_switch=10 engine=12 error=0
P6c rcs0  defaults: state=3 rq_seqno=2 krq_seqno=1 hwsp_seqno=1 | submits=2 promotes=2 completes=2 errors=0 late=0 mmio=0 serial=3 wakeref_serial=3
P6c rcs0  default_state: 65536 bytes | reg[1]=11081019 CTX_CTRL=ffff0008 RING_HEAD=188 RING_TAIL=188 RING_START=fff3b000 RING_CTL=1 RPCS=80041000
P6c bcs0  default_state: 16384 bytes | ... RING_HEAD=f8  RING_TAIL=f8
P6c vcs0/vcs2/vecs0 default_state: 16384 bytes | ... RING_HEAD=78 RING_TAIL=78
teardown: engines stopped and reset (rc=0) before release
attach end: reached=P3 outcome=BLOCKED where=__engines_verify_workarounds err=0
ktest (post-attach): 338 checks, 0 failures
```
- **HW が保存した RING_HEAD/TAIL が c3a の語単位予測と一致**（RCS 98 dw=0x188、BCS 62 dw=0xf8、VCS/VECS 30 dw=0x78）＝投入した命令列が設計どおりに実行・完了した直接証拠。
- LRI ヘッダ 0x11081019 を HW が同形で書き戻し、render の RPCS=0x80041000 を保持。
- c4a（投入なしの resume 段）単独でも実機確認済（vmunix fa5fffa4）：HWS_PGA 5 本とも一致、MODE_GEN7=0x8、MI_MODE=0x200、HEAD=TAIL=0、ESR=0。

### P6-c の構成（すべて新規、正本 6.8.12 から再導出）
| 単位 | ファイル | 内容 | GPU-free |
|---|---|---|---|
| c0 | gt_mem.{c,h} | GT object / GGTT 上端窓 / kernel ppgtt(scratch 塔+PML4) / gt scratch | 284 |
| c1 | gt_engine.{c,h} | HWSP / ELSQ reg / CSB ポインタ / enable_execlists / reset_csb | 292 |
| c2 | gt_lrc.{c,h}, gt_lrc_offsets.inc | LRC image・ring・descriptor・INDIRECT_CTX/PER_CTX_BB | 307 |
| c3 | gt_request.{c,h}, gt_submit.{c,h} | ring 命令生成 / ELSQ 投入 / CSB 処理 / seqno | 322 |
| c4a | gt_resume.{c,h}, gt_fw_ranges.inc | intel_engines_init + intel_gt_resume（正本順） | 331 |
| c4b | gt_defaults.{c,h} | __engines_record_defaults（park 切替・default state・wedge） | 338 |

### 途中で見つけて直したもの
1. **媒体 forcewake ドメインの欠落**（c1 前提）: VCS0/VCS2/VECS0 のレジスタが always-on 扱いで無検査だった。→ 3 ドメイン追加、P6 は 5 ドメイン全取得（=FORCEWAKE_ALL）。
2. **forcewake 域表を正本から機械生成**（c4a）: `__gen12_fw_ranges` 43 項目を `gen_fw_ranges.py` で抽出（手作業表は 0x9xxx の RENDER/always-on 分割、0x8000 MSG_IDLE、0xa2a0 等も欠いていた）。0x40000–0x1bffff（表示・PCODE）は always-on のまま＝P2〜P5 に影響なし。
3. **`reset_csb_pointers` の `ring_set_paused(0)` 抜け**（c4a）: 正本は冒頭で HWSP の PREEMPT 語を 0 に戻す。reset.prepare が 1 にするため、これが無いと**全 breadcrumb 末尾の preempt busywait が永久待ち**になっていた。
4. **P6-b の順序是正**（c4a）: `intel_gt_resume` の正本順（gt_sanitize[全エンジン HW リセット含む] → **rc6_sanitize** → init_hw → rps → エンジン毎 WA/whitelist/execlists_resume → rc6）へ。rc6_sanitize（PG_ENABLE/RC_CONTROL/RC_STATE=0）が抜けていた。
5. **XCS breadcrumb は 18 dw**（c3a、RCS は 22 dw）。NOOP 埋めはしない。
6. **カーネル像の物理 8 MiB 上限**（c4b）: 像は 2 MiB に置かれ、終端が 8 MiB を超えると UEFI が `Allocate kernel: EFI_NOT_FOUND`。境界は総計 6,243,520 B（成功）〜6,325,440 B（失敗）の間。→ 未使用だった osdep DMA マッピング表 256→32（-230 KB、6,091,968 B に）。**恒久策（カーネル配置）はプラットフォーム側の判断事項として記録**。

### 正本で確定した事実（P6-c）
- gen12 に renderstate は無い → record request は「ctx WA の LRI + breadcrumb」のみ。init breadcrumb も発行されない（selftest/eb/GSC のみ）。
- LRC オフセット表は 6.8.12 と 7.1 でバイト一致（`gen_lrc_offsets.py` で機械生成）。
- gen11+ の GGTT 窓は UC 写像で `gen8_ggtt_invalidate` は何も書かない（big-bang は GFX_FLSH_CNTL を書いていた＝差分）。
- ADL-P は全 5 エンジンで AUX table invalidate が必要（MI_SEMAPHORE_WAIT でポーリング）。`gen12_emit_flush_rcs` は EMIT_INVALIDATE でもフラッシュ側ブロックが走る。
- Wa_22011802037 は ADL-P 該当（stop_cs で PREFETCH_DISABLE、reset 前に MI_FORCE_WAKE 排出）。
- `gt_sanitize(gt, true)` は `reset_engines(gt) || force` の評価順により**必ず全エンジン HW リセット**を行う。
- record_defaults は 1 エンジン 2 投入（record context → retire → park で kernel context 切替 → retire）。2 回目の完了で初めて record context の image が書き戻される。
- 新規 timeline は専用 4 KiB HWSP 頁・seqno は +2 刻み（has_initial_breadcrumb）、kernel timeline はエンジン HWSP の 0x100・+1 刻み。

### 記録した適応
- GGTT: drm_mm → 上端固定窓 256 頁＋静的ビットマップ。object 固定プール 64。
- 投入: tasklet＋優先度キュー → 1 エンジン 1 件 in-flight、CSB は待ち経路で同期処理（正本の intel_engine_flush_submission と同じ）。park 切替は CSB 完了後に投入。
- intel_ring_begin の wrap は実装せず拒否（4 KiB ring に ~0.4 KiB の request は wrap し得ない）。
- wait_for_idle は ≥200 ms の実時間ポーリング。

### 残(次)
**`__engines_verify_workarounds`**（判断②で常時診断として承認済）：kernel context 上で各エンジンの WA レジスタを **SRM でメモリへストアする request** を投入し、GPU から見た値を照合する（null request ではない GPU 実行）→ P7 前半 → 実機 EU 試験（要・明示解除）。
台帳E-60〜E-92。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。

## p011 増分E-93 (2026-09-18): **カーネル物理配置を可変化（ローダ＋HAL）**。像の物理 8 MiB 上限（E-92 項 6）を恒久解消。既定は従来どおり 2 MiB、塞がっていれば 1 GiB 未満の最下位 2 MiB 整列空きへ再配置。実機 GPU 渡しで再配置版・従来版とも E-92 と同一結果

### 結果（chaos, vmunix 528a49d9, BOOTX64.EFI 25088 B）
| 構成 | ローダ | HAL | 結果 |
|---|---|---|---|
| 既定（`kernel_phys` 無し）GPU-free | `KERN LOAD 0x200000` | `link=200000-7d0000 load=200000-7d0000 (as linked)` | ktest 338/0 |
| 既定 GPU 実機 | 同上 | 同上 | record_defaults rc=0 polls=9 error=0 → BLOCKED=verify_workarounds、ktest 338/0（E-92 と同一） |
| 再配置強制 `kernel_phys=0x2000000` GPU-free | `KERN LOAD 0x2000000` | `load=2000000-25d0000 (relocated by the loader)` + `shadow 200000-800000 reserved` | ktest 338/0 |
| 再配置強制 GPU 実機 | 同上 | 同上 | record_defaults rc=0 polls=9 error=0 → BLOCKED=verify_workarounds、ktest 338/0 |
| BIOS ローダ（SeaBIOS, TCG） | 常に 2 MiB | `(as linked)` → PAGING PASS → IRQ READY | ktest は TCG の実時間タイムアウトで FAIL 多数（配置と無関係、KVM では未計測） |
- ホストテスト: 新規 `plan/ws031/tests/kernel-placement-host.c`（ELF 計画/再基底化/HAL 規則）PASS（ASan/UBSan も）、ws025 `memory-handoff-host` PASS（再配置受理・不整列/超過/窓外の拒否を追加）。ws003 `x86-parameter-handoff-test` は変更前から `ZEDBSD_*`→`KERN_*` 改名でビルド不能（本件と無関係、未修正）。

### 設計（`bootloader/include/amd64-kernel-image.h` に契約を集約）
- 仮想アドレスは固定（-mcmodel=kernel の上位 2 GiB 制約）。**物理だけ可変**: `physical = virtual − LINK_VIRT_BASE − LINK_PHYS_START + kernel_phys_start`。handoff 形式は不変（`kernel_phys_start/end` が「実際の配置」を意味するようになっただけ）。旧ローダ（常に 2 MiB）とも相互運用。
- 定数: LINK_PHYS_START=2 MiB、PHYS_ALIGN=2 MiB（ローダの大ページ写像のため）、PHYS_LIMIT=1 GiB（bootstrap 窓）、MAX_BYTES=16 MiB（space.c の W^X 葉表 8 枚＝vmunix.ld の ASSERT と同値）。
- **ローダ** (`elf64.c`: plan は link_* と physical_* を分離、`zbl_elf64_place()` で再基底化 / `bootx64.c`: `place_kernel()`): (1) リンク位置へ AllocateAddress、(2) 失敗なら GetMemoryMap を取り EfiConventionalMemory の中で 2 MiB 整列・≥2 MiB・<1 GiB・像が収まる最下位候補へ（候補拒否は 8 回まで再試行）、(3) 全滅なら 1 GiB 未満の記述子を `A64 KERN MAP t=.. start +pages` で列挙して `Place kernel: EFI_NOT_FOUND`。bootstrap ページ表はリンク範囲のスロットだけを配置先へ向ける（非再配置時は恒等のまま）。`zedbsd.cfg` の `kernel_phys=auto|link|0x…` で方針を固定可能（実機で切り分け用）。
- **HAL** (`image.c` 新設): `prekern_amd64_image_init()` をコンソール初期化直後に置き、リンク範囲と配置を検証（整列/上限/サイズ一致/USABLE・BOOT_RECLAIM 内に完全包含）して `A64 KERNEL link=… load=…` を出力、失敗時は理由と 1 GiB 未満の範囲表を出して FATAL。`amd64_image_to_phys()`・W^X 窓（スロットはリンク座標、PTE 物理は +delta）・ダイレクトマップの text/rodata 境界・legacy alias・不変条件・page.c の予約 4 箇所・boot.c/handoff-validation.c の固定値検査を全て image 幾何経由に置換。`zbl6_kernel_placement_valid()` を純粋関数として共有。
- **shadow**: 再配置時、リンク物理範囲は bootstrap 窓から見えなくなる（窓の VA は像自身を示す）。早期ページ表ページはその窓経由で書かれるため、shadow を早期ビットマップ・範囲アロケータ・boot 回収から恒久予約（≤16 MiB、今 6 MiB）。`ram_allocate` に「早期表ページが像/shadow に載ったら FATAL」の防護を追加。

### 途中で見つけて直したもの
1. **shadow の幅**（初版で再配置版が CR3 切替後にトリプルフォルト）: ローダは 2 MiB スロット単位で向け替えるため、像末尾〜スロット末尾（0x7d0000–0x800000）も配置先直後を指す。像サイズ分だけの予約では、その余りから取った早期表ページが窓越しに配置先直後（0x25d0000〜）へ書かれ、ダイレクトマップ表が壊れた（`-d int`: PF CR2=DIRECT+0x100000 in `amd64_ram_lookup`）。→ shadow を 2 MiB 境界に丸めて予約。
2. boot.c の `total_memory < 4 MiB` 検査（像 6 MiB で無意味）を image_init の「配置が報告 RAM 内」検査に置換。

### 残(次)
E-92 の残と同じ: `__engines_verify_workarounds`（SRM request、着手前に一言）→ P7。OSDEP_DMA_MAX_MAPPINGS 32 は上限解消により 256 へ戻せる（次の像成長時に）。
台帳E-60〜E-93。GPU=vfio-pci維持, 10ms tick/HAL インタフェース非変更維持（HAL 内部の配置機構のみ変更）, drm非blacklist, attach先行維持。git commit/push なし。

## p011 増分E-94 (2026-09-18): **P6-c5 `__engines_verify_workarounds` 実機成功** — GPU が SRM でエンジン WA レジスタをメモリへ書き、全件一致（rcs0 5/5、他 4 エンジン 1/1、MCR 域 3 件は正本どおり CS 経路から除外）。frontier=`intel_migrate_init`

### 実機結果（GPU 渡し, chaos, vmunix 7084d425, ログ `~/bigbang/run-parity-hw-e94.log`）
```
P6c record_defaults: rc=0 polls=9 timed_out=0 wedged=0
P6c verify_workarounds: rc=0 where=- polls=23 timed_out=0 | gt irq during: user=9 ctx_switch=11 engine=13 error=0
P6c rcs0 verify_wa: state=PARKED list=8 emitted=5 mcr_skipped=3 verified=5 mismatched=0 not_verifiable=0 err=0 rq_seqno=2 krq_seqno=3 hwsp_seqno=3 | el submits=4 promotes=4 completes=4 errors=0 serial=5 wakeref_serial=5 ring_emit=608
P6c bcs0/vcs0/vcs2/vecs0 verify_wa: state=PARKED list=1 emitted=1 verified=1 mismatched=0 | submits=4 completes=4 errors=0 ring_emit=400
attach end: reached=P3 outcome=BLOCKED where=intel_migrate_init err=0
ktest (post-attach): 347 checks, 0 failures   (GPU-free も 347/0)
```
- rcs0 の 8 件: RING_CMD_CCTL(fake)、Wa_1606700617(CS_DEBUG_MODE1)、Wa_14010919138(FF_THREAD_MODE)ほか 5 件を SRM で読み戻し全一致。GEN8_ROW_CHICKEN2/GEN10_SAMPLER_MODE/GEN9_ROW_CHICKEN4 の 3 件は 0xde80–0xe8ff の MCR 域＝正本 `mcr_range()` どおり CS 経路では検証しない。
- XCS 4 本は RING_CMD_CCTL のみ（正本 xcs_engine_wa_init は ADL-P に該当なし）。
- 各エンジン SRM request(seqno 2)→ park 切替(seqno 3) の 2 投入、CSB promote/complete 各 4（record_defaults の 2 組を含む累計）、エラー 0。kernel ring 消費 rcs0 608 B / XCS 400 B（4 KiB、wrap なし）。

### 実装（`gt_verify_wa.{c,h}`、正本 gt/intel_gt.c + gt/intel_workarounds.c 6.8.12）
- `parity_gen12_mcr_range()` = `mcr_ranges_gen12[]`(0x8150–815f, 0x9520–955f, 0xb100–b3ff, 0xde80–e8ff, 0x24a00–24a7f)。
- `parity_wa_list_srm()` = `wa_list_srm()`: `MI_STORE_REGISTER_MEM_GEN8|MI_SRM_LRM_GLOBAL_GTT`(0x12400002) / reg / scratch+4·**リスト index**(欠番込み) / 0、MCR 域は発行しない。
- `parity_wa_list_check()` = `wa_verify()`: `((cur ^ set) & read) != 0` で -ENXIO、read mask 0(NO_VERIFY) は常に合格、正本と同文言でログ。
- `parity_engine_verify_wa_submit()` = `engine_wa_list_verify()` 前半: `count==0` なら何も投入しない / scratch 1 頁を GGTT 上端窓へ pin / **engine->kernel_context** に request(request_alloc の invalidate flush)→SRM→breadcrumb→ELSQ 投入。
- `parity_engine_verify_wa_park()` = `intel_engine_pm_put()` → `switch_to_kernel_context()`(wakeref_serial==serial なら投入なし)。
- `parity_engines_verify_workarounds()`: エンジン毎に submit→`i915_request_wait(HZ/5)`→verify→park を正本順で直列、最後に `intel_gt_wait_for_idle`。何か失敗すれば -EIO(正本の `err = -EIO` と同じ集約)。probe.c では FAILED → teardown(reset)。
- ETIME はこの errno 集合に無いため `ETIME=ETIMEDOUT` を gt_verify_wa.h で定義（正本の -ETIME 表記を保つ）。
- 記録した適応: 待ちは CSB/HWSP ポーリング(≥200 ms)、park 切替は SRM request の CSB 完了後に投入(1 in-flight)、タイムアウトしたエンジンには park を投入しない、結果頁は固定プールの object。
- GPU-free ktest +9（P6C5-INIT/SRM/WAIT/VERIFY/LOST/PARK/IDLE/TIME/EMPTY）: 発行語(SRM・reg・scratch+4i・0)の位置、MCR 除外、read mask 0 除外、-ENXIO、park 投入、-ETIME→-EIO、空リスト無投入。
- 正本で確定: verify は CONFIG_DRM_I915_DEBUG_GEM 下のみコンパイル（ここでは常時診断、承認②）。失敗時は intel_gt_init の err_gt(__intel_gt_disable)。SRM の宛先は vma+4*i の i がリスト index（count でない）。

### 残(次)
intel_gt_init の残り: `intel_uc_init_late`(GuC 無効→無)、**`intel_migrate_init`**(migrate 用 context: pinned_context → intel_engine_create_pinned_context, ring 256 KiB? 要正本再導出)、その後 i915_gem_init の続き(P7)。
台帳E-60〜E-94。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。git commit/push なし。

## p011 増分E-95 (2026-09-18): **P6-c6 `intel_migrate_init` 実機成功 → intel_gt_init の GPU 側処理を完走**。frontier=`intel_engines_driver_register`（uabi 名付け・エンジン一覧＝簿記のみ）→ i915_driver_probe の次段（intel_pxp_init / intel_display_driver_probe = P7）

### 実機結果（GPU 渡し, chaos, vmunix dd421997, ログ `~/bigbang/run-parity-hw-e95.log`）
```
P6c verify_workarounds: rc=0 polls=23 timed_out=0 error=0
P6c migrate_init: rc=0 engine=bcs0 tables=11 windows=16777216 pte_window=0x1000000 exposed_pts=8 ring=524288 state=16384 | ggtt ring=0xfff34000 state=0xfff30000 hwsp=0xfff13108 | lrc PDP0=00000001:00291000 RING_CTL=0007f001 top_pd=0x100291000
teardown: engines stopped and reset (rc=0) → objects released (pte_writes=444 live=0) → … → PM released (usage=0)
attach end: reached=P3 outcome=BLOCKED where=intel_engines_driver_register err=0
ktest: 354 checks, 0 failures（GPU-free / 実機とも）
```
- migrate ppgtt: PML4[0]→PDP→PD→PT×9（[0,16 MiB) の 8 枚＋PTE 窓 [16 MiB, +32 KiB) の 1 枚）。PTE 窓の先頭 8 エントリは窓自身の PT 8 枚を PAT 3（UC）で写像（正本 `insert_pte`）。
- 512 KiB ring と 16 KiB state を GGTT 上端窓に pin、LRC の PDP0 = migrate top_pd の DMA(0x1_0029_1000)、RING_CTL=(512K−4K)|VALID。timeline はエンジン HWSP の 0x108（HWS_MIGRATE）。**投入は無し**（正本も init では投入しない）。
- 全 GT object が teardown で解放（live=0）。

### 実装
- `gt_migrate.{c,h}`: `first_copy_engine`(class COPY の先頭=bcs0) → `parity_gt_ppgtt_create` → sz=2·CHUNK_SZ(8 MiB)、d.offset=base+sz、sz += (sz>>12)·8 → `alloc_range(0, sz)` → `foreach_pt([0,d.offset))` で `insert_page(dma(pt), d.offset, PAT_NONE)` → `intel_engine_create_pinned_context(bcs0, vm, SZ_512K, HWS_MIGRATE)` = lrc_alloc + init_state + update_regs。fini は unpin/put + vm 解放。戻り値は正本どおり init 失敗でも致命にしない。
- `gt_mem.{c,h}` 拡張: `parity_gt_ppgtt_alloc_range`(= `__gen8_ppgtt_alloc`: `gen8_pd_range`/`gen8_pt_count` の index 計算、新表は下位 scratch encode で充填、親エントリ = `gen8_pde_encode`)、`parity_gt_ppgtt_foreach_pt`(= `__gen8_ppgtt_foreach`、葉 PT ごとに昇順)、`parity_gt_ppgtt_insert_page`(= `gen8_ppgtt_insert_entry`)。表は `tables[16]`（親・index・level・DMA を保持）、destroy で逆順解放。
- **記録した適応**: DMA ベクタ上限 `DRV_DMA_VECTOR_MAX_SIZE`=64 KiB のため、64 KiB 超の object は `drv_dma_alloc_coherent`（i915 DMA device の max_segment_size は UINT_MAX）1 本で確保（`contiguous`）。page DMA = base + offset。汎用層は変更なし。stash の事前確保/ww lock は固定プールからの逐次確保に置換。
- GPU-free ktest +7（P6C6-INIT/ENGINE/VM/PTE/CTX/FINI/LEAK）: 表の木構造(PDP/PD/PT×9・scratch 充填)、PTE 窓の 8 エントリ、pinned ctx（512 KiB ring contiguous、PDP0=top_pd、RING_CTL、HWSP 0x108、seqno 0）、解放漏れ無し。338→347→354/0。

### 正本で確定した事実
- `intel_gt_init` は `__engines_verify_workarounds` 後 `intel_uc_init_late`(GuC 無しで無処理)→`intel_migrate_init`(戻り値未検査)→`out_fw`。以降 `i915_gem_init` は `intel_engines_driver_register` のみ（uabi class/instance 名付け、rb-tree）。
- migrate vm の窓は HAS_64K_PAGES 無し(ADL-P)の古典配置 [0,8M) src / [8M,16M) dst / [16M,16M+32K) PTE。CHUNK_SZ=8 MiB（"~1ms at 8GiB/s preemption delay"）。
- pinned context: CONTEXT_BARRIER_BIT、timeline = engine status page の offset（has_initial_breadcrumb=false）、`intel_context_pin` で lrc_init_state（CONTEXT_INIT）。

### 残(次)
P7 = i915_driver_probe の続き: `intel_pxp_init`(ADL-P: PXP 対応 GT の有無で -ENODEV 相当か要正本確認) → `intel_display_driver_probe`(GEM 後の表示: intel_display_driver_probe → modeset 初期化・出力・fbdev 等) → `i915_driver_register`。実機 EU 試験は要・明示解除。
台帳E-60〜E-95。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。git commit/push なし。

## p011 増分E-96 (2026-09-18): **P7 完成 — `i915_driver_probe` を最後まで実行（intel_pxp_init → intel_display_driver_probe → i915_driver_register）。実機で `outcome=STOPPED where="i915_driver_probe complete"`＝Linux 通常初期化の全経路を parity で踏破**。ktest 365/0

### 実機結果（GPU 渡し, chaos, vmunix 397e6386, ログ `~/bigbang/run-parity-hw-e96.log`）
```
P7 pxp_init: rc=0 full=1 engine=vcs0 kcr_base=0x32000 ce ring=4096 state=16384 ggtt=0xfffb4000 hwsp=0xfff19180 stream_cmd=1 component_added=1
P7 display_driver_probe: rc=0 active_crtcs=0 initial_commit=0 overlay=0 fbdev=0 ipc_enabled=1
P7 hpd_init: encoders=2 pins=4,5 | de: enabled=0 hotplug=0 IMR=0xffffffff TC_CTL=0 TBT_CTL=0 | pch: enabled=0x30000 hotplug=0x30000 SHPD_FILTER=0xf8 SDEIMR=0x3f043f07 SHOTPLUG_DDI=0x88 SHOTPLUG_TC=0 | poll: works=1 core_gets=1
P7 driver_register: opregion=0 kms_poll=1 | power_domains_enable: wells_on 8 -> 2 dc_state=0x2 verify_mismatches=0 | runtime_pm_enable: probe usage=0 active=1
P7 well always_on / PW_1: refcount=0 hw_enabled=1      (残り 2 本＝正本どおり: PW_1 は always_on・所属 domain 無し)
P7 DC_STATE_EN=0x00000002 DC_off disable_calls=1 dc_state_writes=1 rewrites=0 dmc_has_payload=1
teardown: driver_unregister (rpm usage=1, INIT reference re-taken: wells_on=8 DC_STATE_EN=0x00000000)
attach end: reached=P3 outcome=STOPPED where=i915_driver_probe complete err=0
ktest: 365 checks, 0 failures（GPU-free / 実機とも）
```
- **intel_power_domains_enable が INIT 参照を手放し、8 本点いていた well が 2 本（always_on, PW_1）に減り、最後に DC_off well が落ちて DC6 が武装**（DC_STATE_EN=0x2、DMC payload あり）。teardown の `intel_power_domains_disable` で INIT を取り直すと 8 本に戻り DC_STATE_EN=0。verify_state 不一致 0。
- hotplug: エンコーダは port A/B（pin 4/5）→ 正本 `gen11_hpd_irq_setup`: DE 側は TC ピン無しで IMR 不変・TC/TBT CTL クリア、PCH(ADP>TGP) 側は SHPD_FILTER_CNT=0x0F8、SDEIMR の bit16/17 をアンマスク、SHOTPLUG_CTL_DDI=0x88（A/B の HPD enable）、TC は 0。
- pxp: ADL-P は has_pxp・VDBOX あり・media GT 無し → full feature。vcs0 に 4 KiB ring の pinned context（HWS_PXP=0x180）と streaming page。KCR/irq の HW 初期化は mei-pxp component の bind で走る（ここには無い）。
- IPC: DISP_ARB_CTL2.IPC 有効化。initial_commit: active crtc 0 → 空コミット（正本と同値）。

### 実装
- `pxp.{c,h}`（P7-0）: `find_gt_for_required_protected_content`→`pxp_init_full`（session mgmt / `create_vcs_context` / `intel_pxp_tee_component_init` の alloc_streaming_command）。fini は destroy_vcs_context。
- `driver_probe.{c,h}`（P7-a/b）: `intel_ddi_hpd_pin`（default / tgl / xelpd）、`intel_hpd_init_pins`（hpd_gen11 / hpd_icp）、`intel_hpd_init`→`gen11_hpd_irq_setup`＋`icp_hpd_irq_setup`（`intel_uncore_rmw` 意味論＝変化時のみ書込、`ibx_display_interrupt_update` は intel_irqs_enabled 時のみ）、`intel_hpd_poll_disable`（poll_init_work を inline、DISPLAY_CORE get/put）、`skl_watermark_ipc_init`、`intel_initial_commit`（active crtc 0 の形）、`intel_power_domains_enable`（INIT put＋verify_state）、`intel_runtime_pm_enable`（probe 参照 put）、`i915_driver_register` の N/A 一覧、remove 側 `intel_runtime_pm_disable`/`intel_power_domains_disable`/display unregister。
- `power_domains.{c,h}`: **DC_off well の disable を正本化**＝`gen9_dc_off_power_well_disable`（DMC payload 必須 → target により `tgl_enable_dc3co`/`skl_enable_dc6`（assert は警告）/`gen9_enable_dc5`）＋`gen9_set_dc_state`（allowed mask、`gen9_dc_mask`、DMC 無視検出、`gen9_write_dc_state` の再書込ループ）。`is_enabled(DC_off)` を HW 読取り（`gen9_dc_off_power_well_enabled`）に。pw_ctx に display_ver / dmc_has_payload / dc_state を追加。
- runner: 完走時の表示を `STOPPED_AT_P2` → `COMPLETE` に。
- ktest +11（P7-HPD-PIN/SETUP/NOIRQ/NODISP、P7-IPC、P7-DC6-NODMC/DC6/DC6-OFF、P7-PXP-NONE/PXP/PXP-FINI）。P5D-WELL は DC_off の HW 読取り化に合わせ DMC payload 付きで数え直し（正本どおり DC_off も落ちる）。354→365/0。
- **記録した適応**: DRM object model 無し（initial_commit は active crtc 0 の形のみ、connector の polled 設定と drm_kms_helper_poll は簿記のみ）、userspace 向け登録（drm_dev/debugfs/sysfs/pmu/perf/hwmon/audio component/fbdev/acpi video/dsm/switcheroo）は N/A 記録、poll_init_work は inline、runtime PM は参照を落とすが autosuspend は武装しない（intel_runtime_suspend 未移植）、verify_state は DEBUG_RUNTIME_PM 相当の well 半分のみ。

### 正本で確定した事実
- `intel_display_driver_probe`: initial_commit → overlay(gen2-4) → fbdev_init → hpd_init → hpd_poll_disable → skl_watermark_ipc_init。`intel_hpd_irq_setup` は `display_irqs_enabled && funcs.hotplug`。
- `intel_power_domains_verify_state` は CONFIG_DRM_I915_DEBUG_RUNTIME_PM 下のみ実体。
- `intel_runtime_pm_enable` は autosuspend 10 s＋`pm_runtime_put_autosuspend`（probe 参照）。remove は `pm_runtime_get_sync` で戻す。
- ADL-P: has_pxp=1（GEN12_FEATURES）、mei-pxp 経路。KCR init は component bind 時。

### 残(次)
**Linux 通常初期化（i915_driver_probe）の parity 移植はここで完走**。残るのは (a) DRM object model が要る部分（active crtc がある場合の initial_commit、connector 検出・hotplug 処理本体、fbdev）、(b) runtime suspend/resume、(c) 実機 EU 試験（要・明示解除）。次の方針は判断事項。
台帳E-60〜E-96。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。git commit/push なし。

## p011 増分E-97 (2026-09-18): **実機 EU 試験（明示解除・1 回）— HANG。parity で Linux 通常初期化を完走させた GT 上でも、compute 陽性対照（C1）は big-bang 時代と同一署名で停止**

### 実機結果（GPU 渡し, chaos, vmunix 94a75d4d, ログ `~/bigbang/run-parity-hw-e97-eu.log`）
```
EU-TEST HANG: rc=-ETIMEDOUT engine=rcs0 dss=5 max_threads=559 batch_dwords=322 submitted=1 completed=0 timed_out=1 wedged=1 polls=40000(2 s)
  ready=c0ffee10 eu=dead0000 done=dead0000 cs=dead0000 idd_rb_ok=1 kernel_rb_ok=1 | gt irq: user=0 ctx_switch=1 error=0
EU-TEST hang: ipehr=70040000 acthd=1:004014c0 instdone=ffdeffff fault=0 row_instdone(raw)=8610e87f eu_dis=0 slice_ack=3 ss01_eu_ack=3 ss23_eu_ack=3
rcs0 dump(eu-test): HEAD=80 TAIL=d8 START=fffc9000 ACTHD=1:004014c0 IPEHR=70040000 ESR=0 EIR=0 | EXECLIST_STATUS=20:3098 submits=5 promotes=5 completes=4 errors=0
EU-TEST ctx: CTX_CTRL=00090008 RING_CTL=1 PDP0=1:00213000 | idd_rb=00000400 0 00100000 0 0 0 1 0 | kernel_rb = C1 の 36 dword と完全一致
teardown 正常（engines reset rc=0, objects live=0）。attach end: outcome=STOPPED where=i915_driver_probe complete。
※EU ビルドでは ktest が完走していない（新規 P6C4B-INHERIT 試験を `parity_engines_defaults_release` の後に挿入したため、解放済み default_state を参照して停止＝試験側の誤り。「lines=367」をチェック数と読み違えた）。EU 試験自体は probe 側で先に完了しているので結果には影響しない。挿入位置を直した後の ktest は GPU-free/実機とも 367/0（vmunix 6882fb9a、EU 試験は PARITY_EU_TEST=0 で不実行、probe=COMPLETE）。
```
- **署名は E-16〜E-30 と同一**：READY marker 着地（CS は walker 直前まで実行）→ GPGPU_WALKER 投入 → EU スレッドが完了せず、後続 MEDIA_STATE_FLUSH（IPEHR=0x70040000）で CS 停止。row_instdone=0x8610e87f（EU not-done）、GPU fault 無し、EU 電源 ack 3・eu_dis 0。
- **今回の条件（前回までと違う点）**：(1) P0〜P7 を Linux 6.8.12 正本どおりに初期化した GT（WA 表は SRM で実機検証済み、MOCS/PAT/RC6/RPS/execlists/golden context/indirect-ctx BB/record_defaults）。(2) context は `__engines_record_defaults` が保存した **engine->default_state を継承**（CTX_CTRL=0x00090008=restore inhibit 無し、`lrc_init_state` 正本どおり）。(3) request は execbuf 形（request_alloc の invalidate → `gen8_emit_init_breadcrumb` → `gen8_emit_bb_start`(ARB on, PPGTT batch, ARB off) → fini breadcrumb）。(4) forcewake 全ドメイン保持、driver_register 後（wells 8→2・DC6 武装後）。(5) batch/kernel/IDD/VA は Linux で完走実証済み L-C1 と同一バイト（CS 経由の IDD/kernel 読み戻しも一致＝PPGTT 写像健全）。
- **結論**：Linux 通常初期化の parity 移植（execlists）では EU ハングは解消しない。移植済み・検証済みの要素は単独原因から除外。

### 実装（試験用）
- `eu_test.{c,h}`：big-bang の C1 バッチ（322 dw、readback 込み）を transcribe、`parity_gt_ppgtt_alloc_range/insert_page` で shared@0x100400000・batch@0x100401000 を kernel vm に写像（PAT 0）、新規 context（engine->default_state 継承）＋専用 timeline、execbuf 形 request、2 s ポーリング、HANG 時は engine dump＋EU 電源レジスタ＋reset_prepare/`gt_reset_all`（wedge）。`PARITY_EU_TEST`（既定 0）で明示ビルド時のみ実行。
- `gt_engine.h`/`gt_lrc.c`：**engine->default_state を導入し `lrc_init_state` が継承**（正本 shmem_read＋CONTEXT_VALID、inhibit 解除）。record_defaults 後に配線、teardown で解除。migrate/pxp の pinned context も以後これを継承（正本どおり）。
- ktest +2（P6C4B-INHERIT、EU-BATCH 語順）: 365→367/0（INHERIT の挿入位置を直した後）。
- **試験後に見つけた自分の移植誤り（E-95）**：`alloc_range` の実表 PDE を `PPAT_UNCACHED` で符号化していた。正本 `set_pd_entry` は `gen8_pde_encode(..., I915_CACHE_LLC)`＝`PPAT_CACHED_PDE(0)`（UNCACHED は scratch 塔のみ）。→ `parity_gen8_pde_encode_cached` で修正、P6C6-VM 期待値更新。**修正後の EU 再試験は未実施**（要・指示）。今回の EU 試験は uncached PDE の PPGTT で走った（CS 経由の読み戻しは一致しており写像自体は有効）。

### 残る差分候補（Linux 陽性対照 E-23/E-25＝同一 GPU・execlists との差）
1. **MCR（multicast）レジスタの実効値**：ROW_CHICKEN2/4・SAMPLER_MODE・L3SQC 等の EU 向け WA は CS の SRM では検証不能（正本も除外）。steering 付き MMIO 読み戻しで確認可（安価）。
2. **PDE キャッシュ属性**（上記、修正済み・未再試験）。
3. parity が「適応／N/A」と記録した箇所：GGTT/WC 窓は big-bang 資産（P2）、割込み駆動 retire は無く CSB ポーリング、`intel_pxp_init_hw`（Linux は mei_pxp bind で KCR init/irq が走る）、runtime PM、hwconfig（GuC 前提）。
4. 初期化以外：Linux は同じ GPU を Linux ブート後（i915 通常ロード）に使用。zedBSD は OVMF＋FLR 後。fuse/クロック/電源状態の差はレジスタ全量 diff（E-27 は GLOBAL 一致・MCR 一部）でしか詰められない。

### 残(次)
判断事項：(a) PDE 修正で EU 再試験 1 回、(b) MCR 実効値監査（steered read）、(c) 停止時の EU/TDL/GAM 状態の追加採取（専門家指定レジスタ）、(d) GuC submission 移植（Linux 既定だが陽性対照は execlists）。
台帳E-60〜E-97。GPU=vfio-pci維持, 10ms tick/HAL非変更維持, drm非blacklist, attach先行維持。git commit/push なし。
