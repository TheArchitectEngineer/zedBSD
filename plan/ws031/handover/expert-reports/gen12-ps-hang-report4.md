# Gen12 PS ハング 第4報 — Mesa と多数照合しても不変

リファレンスコンパイラ（refps）に加え、**isl と Mesa 共通関数のハーネスを追加**し、
実際に Mesa が生成する値と自分の実装をビット単位で照合しました。結果、**照合できたものは
すべて Mesa と一致させましたが、症状は完全に不変**です。

## 追加で照合・修正したもの（すべて Mesa 生成値に一致させた）

### 1. PS カーネル（refps = brw_compile_fs）
- SIMD8 + SIMD16 の両変種、`sendc.render` の **split send（wr:1+3, ex_desc=0xC0）**
- → 私の single send を Mesa の split send に置換。**それでも不変**。

### 2. 3DSTATE_PS（refps の prog_data）
- `dispatch_grf_start=2`（SIMD8/16 とも）、KSP0=SIMD8、KSP2=SIMD16、8/16 pixel dispatch enable
- → dual-dispatch を Mesa 通りに設定。**不変**。

### 3. RENDER_SURFACE_STATE（isl_surf_fill_state, 32x32 B8G8R8A8 linear）
- isl 正解: `[01]=0x86000008`（**bit31 "Enable Unorm Path In Color Pipe"** + MOCS6 + QPitch8）、`[05]=0x00000100`
- 私は `[01]=0x06000000`, `[05]=0` だった → isl 正解値に置換。**不変**。

### 4. URB 設定（intel_get_urb_config）
- Mesa: VS **entries=3576**、start=chunk4、**deref_block_size=SIZE_32**
- 私は entries=64、deref=PER_POLY だった → Mesa に一致（3576 / SIZE_32、SF DerefBlockSize も SIZE_32）。**不変**。

### 5. 3DSTATE_WM
- BLORP は空（全ゼロ）→ 私の force-thread-dispatch を除去し空に。**不変**。

## すでに一致確認済み（第1〜3報）
SBA 全 DWORD、CTX_CONTEXT_CONTROL、RPCS、L3ALLOC、CULLMODE、depth format、MOCS 符号化、
HDC pipeline flush、BindingTableEntryCount=1、pixel scoreboard stall、ADL-P workaround 一式。

## 症状（全 Mesa 一致後も不変）
```
ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
SC_INSTDONE: WMFE, PSS のみ not-done、RCC done
ROW_INSTDONE(MCR steered): idle=0xffffffff → hang=0x8610e87f
markerA 着地、markerB 未着地（3DPRIMITIVE 後の PIPE_CONTROL で CS 停止）、GPU fault 無し
```

## 現状の解釈

kernel/PS/surface/URB/WM を Mesa 生成値に一致させても、PixelShaderValid=1 で描画すると
windower(WMFE) と pixel scoreboard(PSS) が not-done のまま、pipe がドレインせず停止する。
RCC は idle のまま（カラー書き込み未到達）。

残る差分は、**driver(anv/blorp/iris) が emit する他の 3DSTATE パケット**（SBE / RASTER / CLIP /
MULTISAMPLE / SF / DRAWING_RECTANGLE / VF_SGVS / DEPTH 群 / STREAMOUT / PRIMITIVE_REPLICATION）
のいずれか、または **PS スレッド dispatch 機構そのもの**にあると考えられます。これらは standalone 関数
では得られず、anv を実行しないと正確な emit 値が取れません（anv は glslang 依存でビルドがブロック）。

## 伺いたいこと

1. **kernel/PS/surface/URB を Mesa 一致にしてもこの止まり方（WMFE/PSS のみ not-done、RCC idle、
   ps=0、fault 無し）の場合、残る 3DSTATE 群のうち Gen12 で「これが無い/違うと pixel pipe が
   dispatch/drain できず止まる」典型はどれ**でしょうか。特に SBE の VertexURBEntryReadOffset/Length、
   DEPTH/HiZ の null 設定、SF の viewport transform、VF_SGVS の InstanceID、DRAWING_RECTANGLE の
   いずれかに落とし穴はありますか。

2. **PS スレッドが dispatch されているか**を確定する Gen12 の方法をご教示ください。
   - ROW_INSTDONE の idle→hang 遷移（EU not-done）は「スレッドが EU 上にいる」と読めますか。
   - per-thread の EU IP / 停止理由を読むレジスタ（SIP, EU_ATT, TD_CTL 等）はありますか。

3. anv を実行せずに **BLORP clear の完全な 3DSTATE 列（実際の emit 値）** を得る方法はありますか
   （aub トレース、genX の standalone 呼び出し、既存のダンプなど）。あれば全パケットを逐一照合できます。

（インフラ: refps/refsurf/refurb ハーネスは `build-gentool` に構築済み。brw_compile_fs / isl /
intel_get_urb_config を standalone で叩けます。glslang があれば anv もビルド可能な見込み。）
