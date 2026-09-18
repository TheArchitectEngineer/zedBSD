# Gen12 PS ハング 第3報 — 参照コンパイラ導入とカーネル無罪の確定

## 大きな進展: リファレンスコンパイラのビルドに成功

環境制約（agent-1 の sudo はパスワード要求、anv は glslang 依存）を回避し、
**`brw_compile_fs` を直接叩く standalone ハーネス（refps）をビルド・実行できました**。
鍵は mesa の build gating を直接無効化したことです:
```
src/compiler/nir/meson.build:  with_nir_headers_only = false and (...)   # 強制でフル NIR をビルド
```
これで NIR 生成ソース（nir_op_infos 等）がコンパイルされ、refps がリンク可能に。
ハーネス実装で必要だった初期化:
- `process_intel_debug_variable()`（未呼出だと `intel_simd=0` で全 SIMD が破棄され空プログラムになる）
- `compiler->shader_debug_log/shader_perf_log` を no-op に設定（NULL のまま呼ばれて pc=0 で SEGV）
- `intel_get_device_info_for_build(0x46a8, ...)`（オフライン用 devinfo）

## 参照カーネル（Mesa 生成、ADL-P、gentool 逆アセンブル）

```
; SIMD8 variant (offset 0), dispatch_grf_start=2
mov (8)  r127:d 0x3f800000        ; red
mov (8)  r124:d 0                 ; green
mov (8)  r125:d 0                 ; blue
mov (8)  r126:d 0x3f800000        ; alpha
sendc.render (8) null r127 r124 0x000000C0 0x02031400 {EOT,@1}
   ; simd8 rt_write last_rt bti(0), SPLIT SEND wr:1+3
   ;   payload1 = r127 (mlen=1), payload2 = r124-126 (ex_mlen=3, ex_desc=0xC0)
```
prog_data: `d8=1 d16=1 grf_used=128 grf_start0=2 grf_start2=2 num_varying=0 persample=0 uses_kill=0`

**私の手書きとの差**: 私は single send（`wr:4+0`, desc 0x08031400）、Mesa は **split send**
（`wr:1+3`, desc 0x02031400 + ex_desc 0xC0）。Gen12 の RT write は split send が正解でした。
（gentool は single send も「合法」と受理しますが、意味的に別物。）

## しかし — 参照カーネルでも同一ハング。PS は完全に無罪

**Mesa 生成カーネル（SIMD8）に差し替えても同一ハング。**
さらに **完全な dual-dispatch（SIMD8+SIMD16 両変種）+ Mesa 一致の 3DSTATE_PS
（8/16 pixel dispatch enable、KSP0=SIMD8、KSP2=SIMD16、GRFStart0=GRFStart2=2）でも同一ハング**。

観測は不変:
```
ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
SC_INSTDONE: WMFE, PSS のみ not-done、RCC done
ROW_INSTDONE(MCR steered): idle=0xffffffff → hang=0x8610e87f（全 subslice、EU not-done）
markerB=0（3DPRIMITIVE 後の PIPE_CONTROL で CS 停止）、GPU fault 無し
```

→ **PS カーネルも 3DSTATE_PS 設定も byte/field 単位で Mesa と一致するのにハング。
問題は PS の外、固定機能パイプラインのステートにある**ことが確定しました。
（専門家の「カーネルに絞るな」が正しかったです。）

## 現在の切り分け（確定事項）

| 検証済み・正しいと確認 | 状態 |
|---|---|
| SBA 全 base/size（生 DWORD） | ✓ |
| 3DSTATE_PS 全フィールド（KSP/dispatch/GRFStart/threads） | ✓ Mesa 一致 |
| PS カーネル bytes | ✓ Mesa 生成そのもの |
| CTX_CONTEXT_CONTROL / RPCS / L3 / URB | ✓ |
| ジオメトリ（VF→VS→CLIP, cl_prim=1） | ✓ |

**残る容疑**: ROW_INSTDONE が示すとおりスレッドはディスパッチされる。カーネルは正しい。
だが RCC idle（RT write 未到達）。→ RT write の宛先 **RENDER_SURFACE_STATE / binding table**、
または windower→PS dispatch を阻む固定機能ステートが最有力。

## 伺いたいこと

1. **PS カーネルも 3DSTATE_PS も Mesa 完全一致でこの止まり方（WMFE/PSS のみ not-done、
   RCC idle、EU not-done、ps=0、fault 無し）の場合、次に疑う固定機能ステートは具体的にどこ**でしょうか。
   RT の RENDER_SURFACE_STATE、binding table、3DSTATE_SBE/WM/RASTER、DRAWING_RECTANGLE、
   あるいは PIPE_CONTROL 順序のうち、この症状に最も効くのは？

2. **EU の per-thread の停止位置/理由を Gen12 で読むレジスタ**（EU_ATT/EUSC 等）をご教示ください。
   スレッドがカーネル先頭で止まっているのか、RT write(send)で止まっているのかを確定させたいです。

3. 現在 **Mesa の isl（libisl.a）もビルド済み**なので、`isl_surf_fill_state` で 32x32 B8G8R8A8 の
   RENDER_SURFACE_STATE を生成して私のと比較する予定です。RT サーフェス以外に、PS ディスパッチ時に
   windower が参照する state で見落としがちなものはありますか（例: null depth/HiZ の扱い、
   scoreboard、Wa 群）。

（環境: refps ハーネスは `~/zedBSD/plan/ws031/mesa-refs/mesa/build-gentool` に構築済み。
再利用可能。参照 FS の全 bytes・prog_data も取得済み。）
