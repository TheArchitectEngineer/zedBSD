
## p011 増分E-9 (2026-09-15): リファレンスコンパイラ導入と Mesa 完全照合（PS ハング調査、継続中）

専門家の助言（カーネルに絞るな／実際に GPU に適用される state を検証せよ／brw_compile_fs で
参照カーネルを作れ）を受け、Mesa の standalone ハーネスを構築して実測値と照合した。

### インフラ成果（再利用可能、`plan/ws031/mesa-refs/mesa/build-gentool`）
mesa の build gating を直接無効化（`src/compiler/nir/meson.build`: `with_nir_headers_only = false ...`）
してフル NIR をビルドし、ドライバ（glslang/LLVM 依存）無しで以下を standalone 化:
- **refps**: `brw_compile_fs` で定数色 FS を実 Gen12 ISA へコンパイル。必要初期化=
  `process_intel_debug_variable()`（intel_simd=0 だと全 SIMD 破棄）、
  `compiler->shader_debug_log/perf_log` を no-op（NULL 呼出で pc=0 SEGV）、
  `intel_get_device_info_for_build(0x46a8,...)`。
- **refsurf**: `isl_surf_fill_state` で RENDER_SURFACE_STATE 生成。
- **refurb**: `intel_get_urb_config` で URB 設定算出。
- venv `/tmp/mesa-venv`（Mako/PyYAML/packaging/setuptools）。

### 参照カーネル（Mesa 生成、ADL-P）
定数色 PS の RT write は **split send**（`sendc.render (8) null r127 r124 0xC0 0x02031400 {EOT,@1}`,
wr:1+3, ex_desc=0xC0, mlen=1+ex_mlen=3）。手書きの single send（wr:4）とは別物だった。
prog_data: `d8=1 d16=1 grf_used=128 dispatch_grf_start=2`。

### Mesa と照合・一致させた項目（すべて実機投入、症状不変）
| 項目 | 私の誤り → Mesa 正解 |
|---|---|
| PS カーネル | single send → split send（Mesa 生成そのもの、SIMD8+SIMD16） |
| 3DSTATE_PS | dispatch_grf_start=2, KSP0/KSP2, 8+16 dispatch |
| RENDER_SURFACE_STATE | `[01]` bit31 "Enable Unorm Path In Color Pipe" 欠落 → isl 正解値 |
| URB | entries 64→3576, deref PER_POLY→SIZE_32 |
| 3DSTATE_WM | force-dispatch → 空（BLORP と同じ） |

### 切り分けの決定的結果
- **RT write の形式（sendc / plain send / null_rt）を変えても同一ハング** →
  RT surface も sendc scoreboard 依存も原因ではない。PS スレッドがどの終端でも retire しない。
- 全カーネルが**同一ハング**（markerB=0, ps=0, WMFE/PSS not-done, RCC idle, fault 無し）→
  **EU がカーネルを実行していない**（dispatch されない or 命令フェッチ段で停止）。
- ROW_INSTDONE(MCR steered) idle=0xffffffff→hang=0x8610e87f（EU not-done）はスレッドの存在を示唆。

### 未解決の核心と、必要な知見
「PS スレッドが dispatch されているか」「どの命令で停止か」の確定には **Gen12 の EU per-thread IP /
停止理由レジスタ（SIP/EU_ATT/TD_CTL 等）**を読む必要があり、Gen12 内部知識（専門家）待ち。
自前で照合可能な範囲（kernel/PS/surface/URB/WM の Mesa 一致）は尽くした。

疑い（有力順）: (a) EU 命令フェッチが Instruction Base 相対の正しい PPGTT 位置を見ていない、
(b) windower が PS を dispatch しない固定機能ステート、(c) driver(anv) 固有の 3DSTATE 差分。
(c) は anv 実行（glslang 必要、agent-1 の sudo がパスワード要求でブロック）が要る。

変更ファイル: selftest.c, lrc.c, vk/linux/3dstate-gen12.inc, linux/i915-workarounds.inc(新規),
results-ws031.md, shaders/(新規). HAL/UAPI 変更なし。default/selftest ビルド warning 0。
