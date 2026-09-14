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
