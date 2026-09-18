
## p011 増分E-11/E-12 (2026-09-15): 専門家の A/B/C 対照試験 — dispatch は起きていない疑いが強化

専門家の計画（B: WM のみ変更 → C: PS 内 marker → refblorp）を実行。

### E-11 = Test B（同一シェーダ、WM に ForceThreadDispatch=ForceON + EDSC_PSEXEC を追加）
WM DW1 = `(1<<31)|(2<<19)|(1<<21)`（StatEnable|ForceON|PSEXEC）。gen80.xml でビット確認
（ForceThreadDispatch bits20:19=ForceON=2、EarlyDepthStencil bits22:21=PSEXEC=1）。
加えて pre-draw PIPE_CONTROL 直後・3DPRIMITIVE 直前に **CS marker（MI_STORE_DWORD_IMM）** を追加。

```
draw batch 353 dwords markerA=0xa5a50001 markerMid=0xc5c50003 markerB=0x00000000 completed=2 seqno=3
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```
- **markerMid 着地** → CS は pre-draw stall（CS_STALL|STALL_AT_SCOREBOARD|DEPTH_STALL）を通過し
  3DPRIMITIVE を発行済み。**ハングは pre-draw stall ではなく post-draw drain PIPE_CONTROL**（IPEHR=0x7a000204）。
- **ForceThreadDispatch=ForceON でも ps=0**。coverage 非依存で dispatch を強制しても windower は
  PS を dispatch しない → 「windower が dispatch できない」の強い証拠。WM state 変更では解消せず。

### E-12 = Test C（PS 先頭に A64 data-cache marker を追加した FS）
refps_marker.c（refps.c + `nir_store_global(0xc0ffee01 → 0x100400c10)`）を brw_compile_fs でコンパイル
（size=496, d8=1 d16=1, grf_used=128, scratch=0, grf_start0=2）。gentool 逆アセンブルで marker が
**`send.hdc1 a64_untyped_write`（Data Port 1、RT 経路とは別）**で RT write の**前**に emit されることを確認。
PS_EXTRA に HasUAV(bit2) を追加、WM は B のまま、SIMD8 dispatch（grf_start0=2）。

```
draw batch 353 dwords markerA=.. markerMid=0xc5c50003 markerPS=0x00000000 markerB=0x00000000 ..
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```
- **markerPS=0**（A64 marker 未着地）+ ps=0 の**2つの独立 negative** → PS は store まで実行していない、を強く示唆。
- RT surface も未書き込み（draw pixels 全て 0x0）。

### 解釈（専門家の fixed interpretation に従う）
- markerPS=0 は「未 dispatch/初期停止/marker のメモリ経路・可視性の問題」いずれとも両立し、
  **「dispatch されなかった」の確定ではない**。A64 store が drain 未完で L3 に留まり memory 未達＝
  CPU から見えないだけ、の可能性が残る。
- 確定には **compute-shader 陽性対照**（同一 RCS/PPGTT/命令配置で同じ A64 store を正常完了させ、
  drain/reset 前に CPU 可視を検証）が必要。これが取れれば markerPS=0 を「PS 未実行」の証拠に昇格できる。

### 確定した negative（重要）
- pre-draw stall は原因でない（markerMid 着地）。
- coverage/dispatch-gating は原因でない（ForceON 無効）。
- PS/PS_EXTRA/SBE の全フィールドは genxml 正しい（E-10）。
- → 残る mechanism 不明。次は compute 陽性対照 or refblorp（全 packet+参照データ+caller 初期化の照合）。

インフラ追加: refps_marker（build-gentool 登録済み）、run-e1x.sh（TAG 差し替えで連続実行、再起動不要）。
