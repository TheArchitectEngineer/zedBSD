
## p011 増分E-14 (2026-09-15): compute 陽性対照の完全仕様化（refcs + GPGPU_WALKER）

専門家指定の Gen12.0 compute 陽性対照を、実装直前まで仕様化。参照 = Mesa `blorp_exec_compute()`
の GFX_VERx10<125 経路（CFE_STATE/COMPUTE_WALKER は Gen12.5+ なので不使用）。

### 構築済みインフラ
- **refcs**（build-gentool 登録済み）: brw_compile_cs で最小 CS をコンパイル。
  144B, SIMD8(prog_mask=0x1), grf_used=128, local=1,1,1, barrier/sampler/scratch なし。
  disasm 確認: `send.hdc1 a64_untyped_write`（**predicate なし＝無条件**）→ `send.ts {EOT}`。
  store: tag 0xc0ffee02 → 0x100400c20（stateless MOCS=index3=UC で memory 直達）。
- ADL-P devinfo（refcs から取得）: max_cs_threads=112, subslice_total=6 → VFE MaxThreads=671。

### パケット仕様（全て genxml で確認、手 emit 値を確定）
scratchpad/compute-control-design.md に全 DW 値を記載。要点:
- 独立 batch: PIPE_CONTROL(flush) → PIPELINE_SELECT(GPGPU=2) → SBA(draw と同一) →
  PIPE_CONTROL(inval) → PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD, VFE 前必須) →
  MEDIA_VFE_STATE(DW3=0x029F0200, DW5=0x00020000) → MEDIA_INTERFACE_DESCRIPTOR_LOAD(TotalLen=32) →
  GPGPU_WALKER(SIMD8, 1x1x1, RightMask=0x1) → PIPE_CONTROL(flush) → markerCS → BB_END。
- INTERFACE_DESCRIPTOR_DATA(8DW, dynamic heap): KSP=1024, BTE=1, NumThreads=1, SLM/Barrier=0。
- MEDIA_VFE_STATE header=0x70000007, MIDL header=0x70020002, GPGPU_WALKER header=0x7105000D。

### 判定設計
- markerCS 着地 & compute marker=0xc0ffee02 → EU/A64/fetch/PPGTT/可視性すべて陽性 →
  PS ハングは PS 固有（windower dispatch/payload/sample-mask 経路）へ大きく分岐。
- markerCS 着地 & marker=0 → A64 可視性 or store 自体（PS と共通の下位問題）。

### 状態
実装（selftest.c に compute 経路を追加）は機械的に落とせる段階。二段階ハンドシェイク（mid-hang
可視性）と 3D→GPGPU 同一 batch 切替は simple 版（独立 batch, 正常完走）では不要なので後回し。
専門家 report8 の回答（pipeline 切替・fence）を simple 版成功後に反映。
