
## p011 増分E-19 (2026-09-16): context-restore WA 監査 — indirect context batch が完全に欠落【強リード】

専門家 §4 の指示で、context-restore の WA 実装有無を監査。特に `gen12_emit_indirect_ctx_rcs()` /
Wa_18022495364（GEN12_CS_DEBUG_MODE2 に INSTRUCTION_STATE_CACHE_INVALIDATE を、context restore 用の
間接バッチで発行）の有無。

### 監査結果: 完全に欠落
- `RING_INDIRECT_CTX(0x1c4)` / `RING_INDIRECT_CTX_OFFSET(0x1c8)` / `RING_BB_PER_CTX_PTR(0x1c0)` は
  i915-regs.inc に**定義のみ存在**。
- しかし LRC（lrc.c）に **indirect context batch / per-ctx WA batch の設定が一切ない**
  （INDIRECT_CTX, BB_PER_CTX, wa_bb, CTX_INDIRECT 等すべて grep ヒット 0）。
- `CS_DEBUG_MODE2`, `Wa_18022495364`, `INSTRUCTION_STATE_CACHE_INVALIDATE`, `gen12_emit_indirect_ctx_rcs`
  はコード全体に**存在しない**。
- → **kernel_context の context restore 時に、Wa_18022495364 を含む indirect-ctx WA が一切適用されていない**。

### なぜ強リードか
Gen12 の indirect context batch は**全 context restore 時**（context のリング命令が走る前）に実行され、
Wa_18022495364 はそこで命令ステートキャッシュを無効化する。これが欠けていると:
- dispatch された EU スレッドが stale な命令ステートをフェッチ → 実行しない → 全スレッドが同一ハング。
- C0(EU スレッドなし)は成功（EU フェッチ不要）。MI/CS/post-sync も成功（EU 命令キャッシュ非依存）。
- 命令ベース VA(高位/低位)無関係（キャッシュが stale なのは VA に依らない）。
これは E-16〜E-18 の観測（「walker/primitive で EU スレッドが dispatch された瞬間、VA・kernel 内容・
pipeline に依らず無条件ハング」）を完全に説明する。

### 専門家の扱い方（重要）
「欠けていた場合も、非特権描画バッチへ LRI を一つ追加して終わりにせず、**context restore の初期化経路と
適用タイミングを参照実装に対応させる変更**として扱う」。まず Linux 外部対照で「自作 init vs 物理/パススルー」
を確定してから、この context-restore init path を実装するのが専門家の順序。

### 併せて記録すべき点（専門家 §4）
- WA は「値」でなく「対象 context・reset 後の再適用」を監査する。GEN9_CS_DEBUG_MODE1.FF_DOP_CLOCK_GATE_DISABLE,
  GEN8_ROW_CHICKEN2.GEN12_DISABLE_EARLY_READ, GEN9_ROW_CHICKEN4.GEN12_DISABLE_TDL_PUSH 等が実行時に成立しているか。
- GEN12_FF_MODE2 は CPU 読み戻しで判定しない（Wa_1608008084: 正しく読み戻せない）。発行した LRI 値と適用先 context で確認。

### 今回の作業範囲（専門家）
C3-low の全バイト読み戻し（低位 VA 0x800400 から）→ Linux ゲストで OpenCL 陽性対照（eu_positive_control.py,
enable_guc=0）→ context-restore WA の実装有無・適用時点の記録（本項で欠落を確認）。
