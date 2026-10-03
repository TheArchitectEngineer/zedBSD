
## p011 増分E-13 (2026-09-15): 専門家指定の2静的監査 — marker predicate と stateless MOCS

専門家（第7報回答）の指示: compute 対照を作る前に (1) marker の sample-mask predicate、(2) stateless
data-port MOCS が本当に UC か、を静的監査せよ。両方とも専門家の予測と一致した。

### 監査1: Test C の marker は sample mask で predicate される（無条件トレースでない）
gentool verbose 逆アセンブル（refps_marker SIMD8）:
```
(W) mov (1) f1.0:uw   r1.14:uw            ; f1 <- r1.14 = PS dispatch/sample mask
    mov (8) r6        0xc0ffee01           ; data（channel mask 下）
(W) mov (8) r2.0      0x00400c10           ; addr low（無条件）
    mov (8) r4.0<2>   r2.0<0>              ; addr broadcast（channel mask 下）
(f1.0) send.hdc1 (8) null r4 r6 ... a64_untyped_write   ; store は f1.0=sample mask で predicate
    sendc.render (8) ... {EOT}
```
→ **markerPS=0 は「スレッド未実行」でも「スレッド実行済みだが sample mask=0 で store 無効」でも起こる。**
Mesa 25.1 の HDC lowering が FS 副作用に `brw_emit_predicate_on_sample_mask()` を適用するため（正しい挙動）。
帰結: E-12 の「ForceON 無効＝dispatch-gating でない」は「同一 FS への ForceON/PSEXEC 変更だけでは
症状不変」に後退。ForceON とスレッドの実 channel mask が非ゼロは別事項。ps=0 と markerPS=0 は
独立した2つの不在証明としては扱わない。

### 監査2: stateless data-port MOCS = index 3 = UC（memory 直達）
- SBA emit: DW3 = `mocs << 16`、mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX=3) = 3<<1 = 6
  → **DW3 = 0x00060000**（stateless MOCS field=6 → table index 6>>1=3）。専門家の予測値と一致。
- driver は MOCS table を programming（engine.c init_mocs/l3cc、GEN9_LNCFCMOCS 書き込み）。
  `gen12_mocs_table[3]` = `MOCS_ENTRY(3, LE_1_UC|LE_TC_1_LLC, L3_1_UC)` = **UC**（LLC uncached, L3 uncached）。
- → A64 marker write は **UC で memory 直達**。「L3 dirty line に残って CPU 不可視」説は**否定**。
  markerPS=0 は可視性問題ではない。

### 帰結と次段
markerPS=0 の残る説明は (a)スレッド未実行、(b)スレッド実行済みだが sample mask=0。**sample-mask
predicate が交絡**。決着には無条件の EU 実行トレース = 専門家指定の **Gen12.0 GPGPU_WALKER compute
陽性対照**（refcs: 1 workgroup/1 invocation、sampler/SLM/barrier/scratch なし、compiler 生成 A64 store で
tag 書き込み、正常終了）が必要。参照は Mesa `blorp_exec_compute()` の GFX_VERx10<125 経路
（CFE_STATE/COMPUTE_WALKER は Gen12.5+ なので**使わない**）。成功すれば EU/A64/命令フェッチ/PPGTT が
一般に動くことを陽性で確定し、PS ハングを PS 固有問題へ分岐できる。並行して refblorp（通常色書き経路の
全 packet+参照データ+caller 初期化を完走させる参照一式）。
