# Gen12 PS ハング 第8報 — 2静的監査は両方ご予測通り。compute 陽性対照(refcs)を構築中

## 静的監査（compute 対照の前に、ご指示の2点）

### 監査1: Test C の marker は sample mask で predicate されていた（無条件トレースでない）
refps_marker の SIMD8 を verbose 逆アセンブル:
```
(W) mov (1) f1.0:uw  r1.14:uw            ; f1 <- r1.14 = PS dispatch/sample mask
    mov (8) r6       0xc0ffee01           ; data（channel mask 下）
(W) mov (8) r2.0     0x00400c10           ; addr（無条件）
    mov (8) r4.0<2>  r2.0<0>              ; addr broadcast（channel mask 下）
(f1.0) send.hdc1 (8) null r4 r6 a64_untyped_write   ; store は f1.0=sample mask で predicate
    sendc.render (8) ... {EOT}
```
→ ご指摘通り、**markerPS=0 は「未 dispatch」でも「実行したが sample mask=0」でも起こる**。
第7報の「ForceON 無効＝dispatch-gating でない」は「同一 FS への ForceON/PSEXEC だけでは症状不変」に
後退させます。**ps=0 と markerPS=0 も独立した2つの不在証明としては扱いません。**

### 監査2: stateless data-port MOCS = index 3 = UC（memory 直達）だった
- SBA DW3 = `mocs<<16`、mocs=GEN12_MOCS(index 3)=3<<1=6 → **DW3=0x00060000**（ご提示値と一致）。
- driver は MOCS table を実際に programming し、`gen12_mocs_table[3] = MOCS_ENTRY(3, LE_1_UC|LE_TC_1_LLC,
  L3_1_UC)` = **UC**（LLC/L3 とも uncached、ご提示の control=0x5/l3cc=0x0010 と一致）。
- → A64 marker write は **UC で memory 直達**。「L3 dirty で CPU 不可視」説は否定。markerPS=0 は可視性問題ではない。

**帰結**: markerPS=0 の残る説明は (a)スレッド未実行、(b)実行したが sample mask=0。sample-mask predicate が
交絡。決着には無条件の EU 実行トレース = compute 陽性対照が必要（ご指摘通り）。

## refcs（compute 対照カーネル）構築済み
brw_compile_cs で最小 CS をコンパイル（既存 refps 基盤を流用）:
```
refcs: size=144 grf_used=128 prog_mask=0x1(SIMD8) local=1,1,1 uses_barrier=0 uses_sampler=0
disasm: send.hdc1 (8) ... a64_untyped_write（predicate なし＝無条件） ; send.ts {EOT}
```
1 workgroup / 1 invocation、sampler/SLM/barrier/scratch なし、compiler 生成の**無条件** A64 store で
tag(0xc0ffee02) を 0x100400c20 へ書く。ご指定の refcs スコープ通りです。

## 次段: Gen12.0 GPGPU_WALKER dispatch を selftest に実装
Mesa `blorp_exec_compute()` の GFX_VERx10<125 経路（CFE_STATE/COMPUTE_WALKER は Gen12.5+ なので不使用）を
参照に、同一 RCS0/LRC/PPGTT batch で MEDIA_VFE_STATE / MEDIA_INTERFACE_DESCRIPTOR_LOAD /
INTERFACE_DESCRIPTOR_DATA / GPGPU_WALKER を emit し、まず「store して正常終了」を確認します。

### 伺いたい Gen12.0 固有の要点（ハード試行の空振りを避けるため）
1. 同一 context 内で **3D → GPGPU の PIPELINE_SELECT 切替**に、Gen12 特有の順序/PIPE_CONTROL 要件
   （CS_STALL、pipeline flush、DOP gate の扱い）はありますか。3D 側は mask 0x13+media DOP gate で
   選択できています。compute 対照は独立 batch にする予定です。
2. **MEDIA_VFE_STATE** の最小構成（1 thread、URB entries、scratch=0、CURBE/per-thread size=0）と、
   **INTERFACE_DESCRIPTOR_DATA** の KSP は 3DSTATE_PS と同様に Instruction Base 相対 offset で良いか、
   Number of Threads in Group・binding table=0・sampler=0・SLM=0 で良いか。
3. 陽性後の二段階ハンドシェイク（EU が tag 書き→可視性/完了同期→CPU の release を待つ）で、
   「visible」とみなす前に CS 内で必要な **data-port fence と応答待ち**の具体形をご教示ください。
   逆アセンブルの `flat` は CPU coherent とは限らない（Mesa は GFX8_BTI_STATELESS_NON_COHERENT 使用）との
   ご指摘を踏まえ、まず単純 store+正常終了→CPU 可視を確認し、その後ハンドシェイクを足す段取りです。

（インフラ: refps/refps_marker/refcs/refsurf/refurb/gentool 構築済み。実機 run は再起動不要で連続実行可。）
