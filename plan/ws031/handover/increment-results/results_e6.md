
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
