
## p011 増分E-15 (2026-09-15): compute 対照の前提確定と専門家の設計修正（実装直前）

専門家（report8 回答）の指示で、投入前に program data とトポロジを確定し、設計を修正した。

### 確定した program data（refcs 出力）
```
prog_offset0=0 per_thread(regs=0) cross_thread(regs=0) total_scratch=0 total_shared=0
```
→ **P=0, C=0** 確定。CURBE=0 が正当:
- VFE.CURBEAllocationSize = ALIGN(P*T+C, 2) = 0
- IDD.ConstantURBEntryReadLength = 0、CrossThreadConstantDataReadLength = 0
- MEDIA_CURBE_LOAD は長さ0で明示発行（BLORP 準拠）
- KSP = InstructionBase + kernel_offset(1024) + prog_offset0(0)

### 確定した実機トポロジ（重要な修正）
実機ログ: `dss_en=0x0000001f` = **5 DSS**（popcount=5）。refcs の subslice_total=6 は PCI-ID default で
**実機と不一致**。→ VFE MaximumNumberofThreads = max_cs_threads(112) × **5** − 1 = **559**
（671 ではない）。VFE.DW3 = (559<<16)|(2<<8) = **0x022F0200**。実装では 0x913c を popcount して runtime 算出。

### 専門家による設計修正（compute-control-design.md に対して）
1. **最優先: SBA を PIPELINE_SELECT(GPGPU) の前に**。Wa_1607854226（ADL 対象）で SBA は 3D モードで適用。
   シーケンス: [3D確立] → pre-SBA flush → SBA → state/const/tex/inst invalidation →
   **PIPE_CONTROL(CS_STALL|RT_FLUSH|DEPTH_FLUSH|HDC_PIPELINE_FLUSH)** → PIPELINE_SELECT(GPGPU=2, 0x69041312)
   → PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD) → VFE → CURBE_LOAD → MIDL → markerReady → WALKER →
   MEDIA_STATE_FLUSH → [Wa_1607156449: post-sync なし stalling PC] → post-sync PC(markerDone) → markerCS → BB_END。
2. **HDC Pipeline Flush = PIPE_CONTROL DW0 bit9**（DC Flush=DW1 bit5 とは別）。生 DWORD で確認。
3. **refcs は clean/復旧済み RCS から実行**（PS ハングより前 or リセット後）。
4. IDD: **NumThreads=1（threads-1 でない）**、**BTE=0（A64 only、BLORP の surface count はコピーしない）**、
   Sampler=0、SLM=0、Barrier=0、**ThreadPreemptionDisable=1（DW2 bit20、Iris が Gen12 で設定）**。
5. GPGPU_WALKER 主要値は確定済みで正しい: SIMD8(0)、counter max=0、group dim=1、right mask=0x1、bottom=0xffffffff。
6. **観測 3 段**: markerReady(walker 前 MI)、compute marker(EU A64 store 0x100400c20)、
   markerDone(**post-sync PIPE_CONTROL** write、単なる MI でなく end-of-pipe 同期)。IDD は CPS 流用でなく専用領域。
7. **2 条件で切り分け**: **C0=walker なし**（初期化列+終端同期+marker が通るか）、**C1=walker あり**（refcs 1 回）。
   C0 成功 & C1 停止なら「walker 追加で不成立」まで絞れる。まだ fence/polling/PS 変更は入れない。

### 判定（専門家、C1、markerReady 更新時）
- EU marker + markerDone 更新 → 単純 compute 陽性対照成立（EU 実行・A64・終了・完了後 CPU 読み戻し）。
- EU 更新・Done 未更新 → store まで実行。EOT/終端同期を調べる。
- EU 未更新・Done 更新 → walker の仕事量/mask/KSP/payload/宛先を確認。
- 両方未更新 → 新規 compute 設定 or 共通基盤、未確定。
「完了したが marker=0 → PS と共通の下位問題」は強すぎるので確定させない。compute 成功も PS 実行を直接証明しない
（PS 固有へ重点を移せる、という表現に留める）。sample-mask predicate は PS 側で残る。

### 状態
全パラメータ・全パケット DW 値・修正シーケンス確定。実装（selftest.c に独立 compute 経路 + C0/C1 toggle +
MEDIA_CURBE_LOAD/MEDIA_STATE_FLUSH/post-sync PIPE_CONTROL emit）が次段。refcs は build-gentool 登録済み。
