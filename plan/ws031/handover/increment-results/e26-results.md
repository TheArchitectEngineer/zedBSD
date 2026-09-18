
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
