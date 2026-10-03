# Gen12 (Alder Lake-P) 自作カーネルドライバ: ピクセルシェーダ・ディスパッチ・ハング 相談レポート

## 0. 要約（TL;DR）

自作 OS の自作 i915 系ドライバ上で Gen12 3D パイプラインを直接叩いています。**ジオメトリはクリッパまで完走**しますが、**PixelShaderValid を立てて描画すると PS スレッドが EU 上でディスパッチされたままハング**し、リタイアしません。GPU fault は無し。RT write は RCC に一度も到達しません。命令バイトはメモリ上の正しい位置に確認済みです。

**伺いたいこと**: この症状（`SC_INSTDONE` で WMFE と PSS のみ not-done、`ROW_INSTDONE` で EU 群 not-done、RCC は idle、fault 無し）から、Gen12 で PS スレッドを最初にディスパッチする際に**最小限必要だが見落としがちな設定**は何でしょうか。特に「スレッドはディスパッチされるが実行/終了しない」を起こす典型原因を教えてください。

---

## 1. 環境

- **GPU**: Intel Alder Lake-P, Iris Xe (PCI `8086:46a8` rev 0c), 1 slice / 5 DSS enabled (`GEN11_GT_SLICE_ENABLE=0x1`, `GEN12_GT_GEOMETRY_DSS_ENABLE=0x1f`)
- **構成**: Linux ホスト上で QEMU/KVM、GPU を **VFIO PCI passthrough** でゲストへ。ゲストは自作 OS（Linux ではない）。
- **ドライバ**: Linux i915 を参照に**ゼロから書いた**ネイティブドライバ。execlists 提出、PPGTT、LRC コンテキストを自前実装。
- **提出経路**: RCS0 の kernel context。3D バッチは**PPGTT のバッチバッファ**を `MI_BATCH_BUFFER_START`（non-secure）でリングから起動。
- **検証手段**: 実機ブート毎に selftest がドライバ attach 中に走り、`debugcon` にログ出力。参照に Linux 7.1 の i915 ソースと Mesa（gen120.xml, BLORP, anv, gentool）を使用。

## 2. 確立済み（動作する）土台

以下は**実機で確認済み**です:
- BCS0 の XY_FAST_COLOR_BLT による単色塗り（PPGTT bind、pixel 読み戻し一致）
- RCS0 での MI_STORE 実行、user interrupt、completion
- STATE_BASE_ADDRESS（実 heap 番地、PPGTT）のパース完走
- **3D パイプライン全パケット（約35種）のパースと 3DPRIMITIVE 完走（PixelShaderValid=0 時）**
- VF が RECTLIST 3頂点から 1 primitive を組み立て、VS ステージ通過

つまり PS 無効なら描画パイプラインは完走します。**PixelShaderValid=1 の瞬間だけハング**します。

## 3. 症状（PixelShaderValid=1）

### 3.1 パイプライン統計カウンタ（MI_STORE_REGISTER_MEM で別リクエストにて読み出し）
```
IA_VERTICES_COUNT   = 3
IA_PRIMITIVES_COUNT = 1
VS_INVOCATION_COUNT = 3
CL_INVOCATION_COUNT = 1
CL_PRIMITIVES_COUNT = 1
PS_INVOCATION_COUNT = 0
PS_DEPTH_COUNT      = 0
```
→ ジオメトリはクリッパを通過（cl_prim=1）。PS 呼び出しはゼロ（リタイアカウンタ）。

### 3.2 CS 状態（ハング時、MMIO 読み出し）
```
RING_HEAD == RING_TAIL         (CS はバッチ末尾まで進んで停止)
ACTHD  = batch_va + 0x3c0      (3DPRIMITIVE 直後の PIPE_CONTROL 位置)
IPEHR  = 0x7a000004            (= GFX_OP_PIPE_CONTROL: drain 待ちの PIPE_CONTROL で停止)
ESR/EIR/EMR/IPEIR = 0
GEN12_RING_FAULT_REG (0xcec4) VALID bit = 0   ← GPU fault 無し
GEN12_FAULT_TLB_DATA0/1 も無害
```

### 3.3 SC_INSTDONE (0x7100) = `0xfbfffffd`
not-done (bit=0) は **WMFE Done(bit1)** と **PSS Done(bit26)** のみ。
**RCC Done(bit9)=1（render color cache は idle）**、SBE/SVL/SFBE/HIZ/IZFE/IZBE/AMFS すべて done。

### 3.4 ROW_INSTDONE (MCR 0xe164, 正しくステアリングして subslice 毎に読み出し)
```
idle:  ss0..ss4 = 0xffffffff   (全 EU done)         ss5 = 0 (disabled)
hang:  ss0..ss4 = 0x8610e87f   (多数の EU not-done) ss5 = 0
```
全 enabled subslice で**同一値**。not-done ビット: EU00-03/EU10-13 (SS0,SS1)、**IC Done(bit12)=not-done**。
→ **スレッドは EU にディスパッチされ、実行中のまま停止**（idle は全 done なので、これは live なスレッド状態）。

### 3.5 命令バイトの実在確認（提出直前に CPU から読み戻し）
```
kernel@0x100400400  [0]=0x80030061 [1]=0x70054aa0 [16]=0x00030132 [17]=0x00000004
```
Instruction Base(0x100400000) + KSP0(1024) = 0x100400400 に、カーネルが正しく配置されている。
`wbinvd` で全キャッシュ書き戻ししても症状不変。

### 3.6 電源/クロック
```
GEN11_GT_SLICE_ENABLE = 0x1
CTX_R_PWR_CLK_STATE (RPCS) = 0x80041000  (GEN8_RPCS_ENABLE|S_CNT_ENABLE|1 slice)
GEN9_PG_ENABLE=0x4, GEN6_RC_CONTROL=0x0, FORCEWAKE_ACK=0x1
```

## 4. ピクセルシェーダ・カーネル（手書き、gentool 検証済み）

Mesa の `gentool asm -p tgl` でアセンブルし、`disasm` で検証:
```
(W) mov (8|M0)     r112.0<1>:f   0x3f800000:f     ; red
(W) mov (8|M0)     r113.0<1>:f   0x0:f            ; green
(W) mov (8|M0)     r114.0<1>:f   0x0:f            ; blue
(W) mov (8|M0)     r115.0<1>:f   0x3f800000:f     ; alpha
    sendc.render (8|M0) null r112 null 0x00000000 0x08031400 {EOT,@1}
```
gentool 逆アセンブル: `simd8 rt_write last_rt (8) bti(0), wr:4+0, rd:0`。
バイト列:
```
80030061 70054aa0 00000000 3f800000
80030061 71054aa0 00000000 00000000
80030061 72054aa0 00000000 00000000
80030061 73054aa0 00000000 3f800000
00030132 00000004 58007024 00c40000   ; sendc.render, EOT(dword1 bit2), SFID=render(dword2 bits31:28=5)
```
descriptor 0x08031400 = SIMD8 single-source RT write, last RT, bti 0, mlen 4, rlen 0。
EOT ペイロードは g112-127 必須のため色を r112-115 に構築（gentool が指摘）。

**重要**: `send`(0x31) 版でも `sendc`(0x32) 版でも同一ハング。**mov を一切含まない `(W) send.ts null r112 ... {EOT}`（thread spawner の即 EOT）でも同一ハング**。→ カーネル内容に依らず同じ症状。

## 5. 3DSTATE_PS / PS_EXTRA / WM / SBE（現在値）

- **3DSTATE_PS** (12 dw): `KernelStartPointer0=1024`(instruction base 相対), `BindingTableEntryCount=0`,
  `MaximumNumberofThreadsPerPSD=63`(=`max_threads_per_psd-1`), **`_8PixelDispatchEnable=1`**,
  `DispatchGRFStartRegisterForConstantSetupData0=2`, KSP1/KSP2=0, scratch=0
- **3DSTATE_PS_EXTRA** (2 dw): `PixelShaderValid=1`（他ビット 0）
- **3DSTATE_WM** (2 dw): `StatisticsEnable=1`。`ForceThreadDispatchEnable=2`(force ON) も試したが効果なし。
  `BarycentricInterpolationMode=0`（BLORP の clear と同じ）
- **3DSTATE_PS_BLEND**: `HasWriteableRT=1`
- **3DSTATE_SBE** (6 dw): `VertexURBEntryReadOffset=1`, `VertexURBEntryReadLength=1`,
  `ForceVertexURBEntryReadOffset/Length=1`, `NumberofSFOutputAttributes=0`,
  `AttributeActiveComponentFormat[0..31]=XYZW`（BLORP の passthrough と同じ）
- **3DSTATE_SF**: `ViewportTransformEnable=0`（RECTLIST 要件）, `DerefBlockSize=PER_POLY`, `StatisticsEnable=1`
- **3DSTATE_RASTER**: `CullMode=NONE(1)`（0=BOTH で全 cull されるバグを修正済み）
- **3DSTATE_CLIP**: `PerspectiveDivideDisable=1`, `StatisticsEnable=1`
- **RENDER_SURFACE_STATE**: SURFTYPE_2D, B8G8R8A8_UNORM, 32x32, linear（Tile4 でも同一ハング）,
  pitch=128, base=RT の PPGTT va, MOCS index 3<<1
- **BINDING_TABLE**: surface heap offset 0, entry[0]=64(surface state offset)
- **3DSTATE_BINDING_TABLE_POINTERS_PS**: 0（surface base 相対）

## 6. STATE_BASE_ADDRESS / MOCS / URB / L3

- **SBA**: general/surface/dynamic/indirect/instruction すべて modify=1 で実 PPGTT 番地、size=0xfffff、
  bindless surface も設定。**MOCS フィールドは index を 1bit 左シフト**して格納（Mesa/Linux 準拠。
  素の index だと予約エントリを選ぶバグを修正済み）。instruction base の MOCS は write-back(index2)。
- **L3ALLOC (0xb134)** = `0xb0000040` (URB 32 ways, ALL 88 ways = TGL validated config)
- **URB**: `3DSTATE_URB_ALLOC_VS` entries=64, starting chunk=4（push const 32KB の後）。
  `3DSTATE_PUSH_CONSTANT_ALLOC_PS` = 全 32KB。HS/DS/GS は 0 entries。
- **提出**: PPGTT batch を non-secure `MI_BATCH_BUFFER_START`。エンジン WA は MMIO、
  コンテキスト WA（CS_CHICKEN1, FF_MODE2, COMMON_SLICE_CHICKEN3 等 ADL-P 一式）は LRI でリングから。

## 7. バッチの emit 順序

```
PIPE_CONTROL(CS_STALL|RT flush|Depth flush|DC flush|Flush)
PIPELINE_SELECT(3D, mask 0x13, media DOP gate)
STATE_BASE_ADDRESS(全 base valid)
PIPE_CONTROL(CS_STALL|state/const/texture/instruction cache invalidate)
[marker A]
3DSTATE_VERTEX_BUFFERS(VB0=pos, VB1=VUE header) / VERTEX_ELEMENTS(2) / VF_STATISTICS / VF /
  VF_SGVS / VF_SGVS_2 / VF_INSTANCING x2 / VF_TOPOLOGY(RECTLIST)
PUSH_CONSTANT_ALLOC_VS/HS/DS/GS(0) / PUSH_CONSTANT_ALLOC_PS(32KB) / URB_ALLOC_VS(64)/HS/DS/GS
CONSTANT_VS/HS/DS/GS/PS(全 empty)
[golden state] WM_HZ_OP / AA_LINE_PARAMETERS / WM_CHROMAKEY / POLY_STIPPLE_OFFSET / LINE_STIPPLE /
  SAMPLE_PATTERN(1x center) / DEPTH_BOUNDS(disabled) / BINDING_TABLE_POINTERS_VS/HS/DS/GS(clear)
CC_STATE_POINTERS / BLEND_STATE_POINTERS / VIEWPORT_STATE_POINTERS_CC / CPS_POINTERS(disabled) /
  BINDING_TABLE_POOL_ALLOC(disabled)
MULTISAMPLE(1 sample) / SAMPLE_MASK(0x1)
3DSTATE_VS/HS/TE/DS/STREAMOUT/GS/PRIMITIVE_REPLICATION  (すべて disabled/empty)
3DSTATE_CLIP / SF / RASTER / SBE / WM / PS / PS_EXTRA / PS_BLEND / WM_DEPTH_STENCIL
3DSTATE_DEPTH_BUFFER(NULL,D32_FLOAT) / STENCIL_BUFFER(NULL) / HIER_DEPTH_BUFFER / CLEAR_PARAMS
3DSTATE_BINDING_TABLE_POINTERS_PS(0)
3DSTATE_DRAWING_RECTANGLE(0..31, 0..31)
PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD|DEPTH_STALL)
3DPRIMITIVE(RECTLIST, 3 verts, 1 instance)
PIPE_CONTROL(CS_STALL|RT flush|DC flush|Flush)
[marker B]
MI_BATCH_BUFFER_END
```
RECTLIST 頂点（screen space, viewport transform 無効）: v0=(32,32), v1=(0,32), v2=(0,0)。W=1(STORE_1_FP)。

## 8. 排除済みの原因（すべて実機で確認）

| 仮説 | 結果 |
|---|---|
| GPU page fault | 無し（RING_FAULT_REG VALID=0） |
| Render power gating / RPCS 未設定 | RPCS 設定済（0x80041000）、症状不変 |
| MOCS フィールド符号化（index<<1） | 修正済（clear selftest で検証）、症状不変 |
| CULLMODE（0=BOTH で全 cull） | NONE=1 に修正済、症状不変 |
| null depth の format（0 は深度ステンシル書式） | D32_FLOAT(1) に修正済、症状不変 |
| linear RT が color pipe を stall | Tile4 でも同一ハング |
| ディスパッチ判定（coverage）で gate | `ForceThreadDispatchEnable=ON` でも ps=0、不変 |
| シェーダ内容 | bare `send.ts` EOT でも同一ハング |
| golden render state（intel_renderstate） | **Gen12 は Linux も未使用**（render_state_get_rodata が NULL）と確認 |
| pixel scoreboard stall（PIPE_CONTROL bit1） | 3DPRIMITIVE 前に入れても不変 |
| ADL-P workaround 一式（MMIO+LRI, Linux 7.1 準拠） | ハングは WA 追加前から同一 |
| 命令バイトの配置/コヒーレンシ | メモリ上に正しく存在確認 + wbinvd、不変 |

## 9. 我々の解釈と、専門家への質問

観測を総合すると:
- スレッドは**ディスパッチされている**（ROW_INSTDONE が idle=全done → hang=多数not-done に遷移）
- しかし**リタイアしない**（ps_invocations=0）
- **RT write は RCC に到達していない**（RCC done=idle、pixel 未書き込み）
- **IC Done(instruction cache) が not-done**
- 停止段は SF→SVL→**WMFE→PSS**→PS dispatch のうち WMFE/PSS のみ

### 質問
1. **この症状（WMFE+PSS のみ not-done、EU not-done、RCC idle、fault 無し、ps_invocations=0）で、Gen12 の PS スレッドが「ディスパッチされるが実行/終了しない」典型原因は何ですか？** 特に IC not-done は「命令フェッチ段で停止」を意味しますか、それとも「スレッド実行中の正常状態」ですか？

2. **PPGTT バッチから起動した 3D パイプラインで、EU の命令フェッチ（Instruction Base 相対の KSP）は CS の MI_STORE と同じ PPGTT 変換を使いますか？** それとも別経路（GGTT や専用 translation）がありますか？ Instruction Base に PPGTT 番地（0x100400000）を与えていますが、EU フェッチが別空間を見ている可能性はありますか？

3. **手書き RT-write カーネルで、PS スレッドを正しく終了させ pixel scoreboard に完了通知するために最小限必要な要素**は何ですか？ Gen12 は headerless RT write（ver>=11 で header_size=0）と理解していますが、R0/R1（dispatch mask）の扱いや SWSB で見落としがちな点はありますか？ `sendc` と `send` のどちらを使うべきですか？

4. **`3DSTATE_PS` / `PS_EXTRA` / `WM` / `SBE` で、PS を最初にディスパッチする際に「これが無いと WMFE/PSS が停止する」必須設定**はありますか？（例: barycentric mode、attribute enable、特定の PIPE_CONTROL 順序、非パイプライン状態のフラッシュ規約 Wa_1607854226 など）

5. 参考: 我々は Mesa の `gentool`（EU アセンブラ/逆アセンブラ）をビルド済みですが、**`brw_compile_fs` で定数色 FS を実際にコンパイルして参照カーネルを得るべき**でしょうか。それとも症状は命令フェッチ側で、カーネル自体は無関係でしょうか。

---

### 付録: 再現の最小要素
- Instruction Base = Surface/Dynamic Base と同一 4KB ページ（別ページでも同一ハング）
- kernel_context は PPGTT。同じ PPGTT への CS の MI_STORE は成功（marker が着地）
- 提出は `drv_i915_request` → RCS0 execlists、seqno/breadcrumb は正常動作
