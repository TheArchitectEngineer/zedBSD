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
