
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
