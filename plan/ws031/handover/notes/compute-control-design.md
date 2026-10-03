# Gen12.0 compute 陽性対照（refcs / GPGPU_WALKER）実装仕様 — 完全版

目的: 同一 RCS0/LRC/PPGTT で無条件 A64 store を行う compute を正常完走させ、marker が CPU 可視に
なることを確認。成功すれば「EU 実行・命令フェッチ・A64/PPGTT・可視性」が一般に動くことを陽性で確定し、
PS ハングを PS 固有問題へ分岐できる。参照: Mesa blorp_exec_compute() の GFX_VERx10<125 経路。

## カーネル（refcs、確認済み）
- 144 bytes, SIMD8 (prog_mask=0x1), grf_used=128, local=1,1,1, barrier/sampler/scratch なし
- disasm: `send.hdc1 (8) ... a64_untyped_write`（**predicate なし＝無条件**）→ `send.ts {EOT}`
- store: tag 0xc0ffee02 → VA 0x100400c20（stateless MOCS=index3=UC で memory 直達）
- ADL-P devinfo: max_cs_threads=112, subslice_total=6 → VFE MaxThreads=112*6-1=**671**

## バッチ列（独立 batch、3D→GPGPU 切替の順序問題を回避）
```
PIPE_CONTROL(CS_STALL|RT_FLUSH|DEPTH_FLUSH|DC_FLUSH|FLUSH_ENABLE)   ; 既存 draw と同じ
PIPELINE_SELECT(GPGPU=2)  = GEN12_PIPELINE_SELECT_DWORD(2)          ; mask 0x13+DOP gate 込み
STATE_BASE_ADDRESS (22dw)  = draw と同一（surface/dynamic/instruction base、stateless MOCS=6）
PIPE_CONTROL(STATE_INV|CONST_INV|TEXTURE_INV|INSTRUCTION_INV|CS_STALL)
PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD)                          ; VFE 前の stalling PC（必須）
MEDIA_VFE_STATE
MEDIA_INTERFACE_DESCRIPTOR_LOAD
GPGPU_WALKER
PIPE_CONTROL(CS_STALL|DC_FLUSH|FLUSH_ENABLE)                        ; store を memory へ
MI_STORE_DWORD_IMM  markerCS = 0xC5C5C5C5 (完走確認)
MI_BATCH_BUFFER_END
```
IDD は dynamic heap に別途 pack（下記）。

## パケット値（genxml 確認済み、手 emit）

### PIPELINE_SELECT: GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_GPGPU) ; GPGPU 選択値=2

### MEDIA_VFE_STATE (9 DW, header opcode: CmdType3|Pipeline2|MediaOp0|SubOp0)
- DW0 = (3<<29)|(2<<27)|(0<<24)|(0<<16)|7 = 0x70000007
- DW1 = 0    ; Per-Thread Scratch=0, Scratch Base=0
- DW2 = 0
- DW3 = (671<<16)|(2<<8)   ; MaxThreads[31:16]=671, NumURBEntries[15:8]=2  → 0x029F0200
- DW4 = 0    ; Max Dual-Subslices（blorp は未設定→0）
- DW5 = (2<<16)|0          ; URBEntryAllocSize[31:16]=2, CURBEAllocSize[15:0]=0 → 0x00020000
- DW6,7,8 = 0              ; scoreboard

### MEDIA_INTERFACE_DESCRIPTOR_LOAD (4 DW, SubOp=2)
- DW0 = (3<<29)|(2<<27)|(0<<24)|(2<<16)|2 = 0x70020002
- DW1 = 0
- DW2 = 32                 ; Interface Descriptor Total Length = 8DW*4 = 32 bytes
- DW3 = <idd_offset in dynamic heap>   ; e.g. I915_DRAW_CPS_STATE_OFFSET 領域を流用 or 新設

### INTERFACE_DESCRIPTOR_DATA (8 DW, dynamic heap, 64B aligned)
- DW0 = 1024               ; Kernel Start Pointer[47:6] low = kernel offset (I915_DRAW_PS_KERNEL_OFFSET)
- DW1 = 0                  ; KSP high
- DW2 = 0
- DW3 = 0                  ; Sampler Count=0, Sampler Ptr=0
- DW4 = 1                  ; BindingTableEntryCount[4:0]=1, BTPointer[15:5]=0
- DW5 = 0                  ; Const URB Read Offset/Length=0
- DW6 = 1                  ; NumThreadsInGroup[9:0]=1, SLM=0, Barrier=0
- DW7 = 0                  ; Cross-Thread Const Read Length=0

### GPGPU_WALKER (15 DW, MediaOp=1, SubOp=5)
- DW0 = (3<<29)|(2<<27)|(1<<24)|(5<<16)|13 = 0x7105000D
- DW1 = 0                  ; Interface Descriptor Offset=0
- DW2 = 0                  ; Indirect Data Length=0
- DW3 = 0                  ; Indirect Data Start Address=0
- DW4 = 0                  ; ThreadWidthMax=0,HeightMax=0,DepthMax=0,SIMDSize[31:30]=0(SIMD8)
- DW5 = 0                  ; StartX
- DW6 = 0
- DW7 = 1                  ; X Dimension=1
- DW8 = 0                  ; StartY
- DW9 = 0
- DW10 = 1                 ; Y Dimension=1
- DW11 = 0                 ; StartZ
- DW12 = 1                 ; Z Dimension=1
- DW13 = 0x00000001        ; Right Execution Mask（SIMD8, 1 invocation → 0x1）
- DW14 = 0xffffffff        ; Bottom Execution Mask

## 読み出し
marker VA 0x100400c20 を clflush して読む。0xc0ffee02 なら compute の A64 store が CPU 可視で成功。
markerCS(別スロット) で batch 完走も確認。

## 判定
- markerCS 着地 & compute marker=0xc0ffee02 → EU/A64/fetch/PPGTT/可視性すべて陽性。
  → PS ハングは PS 固有（windower dispatch / PS payload / sample-mask 経路）。大きく分岐。
- markerCS 着地 & compute marker=0 → A64 可視性 or store 自体の問題（PS と共通の下位問題）。
- markerCS 未着地（compute batch もハング）→ その構成の不備（新規なので要切り分け）。

## 未確定（専門家 report8 待ち、ただし simple 版には不要）
- 二段階ハンドシェイク（mid-hang 可視性）は simple 版成功後に追加。
- 3D→GPGPU 同一 batch 切替の順序要件（独立 batch にするので simple 版は回避）。
