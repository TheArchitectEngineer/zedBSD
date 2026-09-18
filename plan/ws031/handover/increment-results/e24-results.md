
## p011 増分E-24 (2026-09-16): ソース監査 A 初見 + linux-c2-replay 計画

Linux 陽性(E-23)確定後、専門家プラン: 主作業=**linux-c2-replay**（C2 バイナリ+state を Linux i915 の
直接 ioctl で実行）、並行ソース監査は 3 範囲限定（A: __engines_record_defaults/intel_engine_emit_ctx_wa、
B: intel_gt_init/init_hw+forcewake、C: execlists_resume/submission_setup）。

### ソース監査 A の初見（zedBSD の WA 適用機構）
zedBSD は WA を2経路で適用:
1. `i915_draw_apply_engine_workarounds()` = **MMIO 直書き**（GEN7_MISCCPCTL, GEN8_ROW_CHICKEN2,
   GEN9_ROW_CHICKEN4, GEN10_SAMPLER_MODE, GEN9_CS_DEBUG_MODE1, GEN7_FF_THREAD_MODE, RING_PSMI_CTL,
   GEN7_FF_SLICE_CS_CHICKEN1, RING_CMD_CCTL）。context 非保存。
2. per-request の ring LRI（`context_registers[]`）: **compute selftest は L3ALLOC + FF_MODE2 のみ**。
   （draw selftest は加えて COMMON_SLICE_CHICKEN3, CS_CHICKEN1, HIZ_CHICKEN, COMMON_SLICE_CHICKEN4,
   COMMON_SLICE_CHICKEN1 も。だが draw も同様にハング → WA 数だけが差ではない。）

**重要な構造差**: zedBSD は Linux の `__engines_record_defaults()` 相当（restore-inhibit context で
ctx WA を LRI 適用 → 別 context へ切替えて **golden default_state を保存** → 以後の context が継承）を
持たない。ctx WA は per-request の ring で適用しており、golden image には焼かれていない。
専門家の §4A の区別: ① context 初期化時に適用し image 保存する WA（zedBSD は golden 保存機構なし）vs
② indirect-context restore WA（E-21 で実装・確認済み）。①の機構が欠落している可能性。

### linux-c2-replay 計画（専門家仕様、次フェーズの主作業）
Linux i915 の直接 ioctl（Level Zero でなく）で、zedBSD の C2 コード+state を Linux の context/PPGTT/
execlists 提出に載せる。Linux に任せる=GT/engine init, LRC 作成保存復元, PPGTT, execlists 提出, 完了管理。
持ち込む=C2 全命令バイト, program data, SBA, VFE, IDD, walker, batch 内 marker。
- DRM render node open → GEM context (I915_CONTEXT_PARAM_ENGINES に {RENDER,0} のみ, selector=0) →
  GEM object 確保 + softpin(EXEC_OBJECT_PINNED, 4GB 超は SUPPORTS_48B, GPU 書込は WRITE) →
  GEM_PWRITE でコード/IDD/batch/marker → EXECBUFFER2 → GEM_WAIT(timeout) → GEM_PREAD で marker 確認。
- 試験順: **L-MI → L-C0 → L-C2 →（成功時）L-C1**。C2 期待値=markerReady/markerDone 更新+正常完了+reset なし。
- ioctl 拒否(EINVAL 等)と受理 batch の GPU 停止を分けて記録。
判定: L-C2 成功→C2+state は Linux 管理下で完走＝zedBSD 基盤側監査を優先。L-C2 停止→C2 投入列/参照データ/
移植差分を調べる（init/context だけに限定しない）。

### 今回の状態
Linux 陽性環境は保存(/home/awe/linuxvm, boot-linux.sh)。zedBSD の C0/C2/C1/indirect-ctx 実装維持。
次フェーズ=linux-c2-replay ハーネス構築（大きめ）+ ソース監査 A（golden context 記録機構の差分）。
専門家: ベアメタル/SIP/PS 追加/別 chicken bit は保留。default ビルド warning 0。
