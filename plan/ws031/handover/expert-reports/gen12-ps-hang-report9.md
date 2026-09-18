# Gen12 PS ハング 第9報 — compute 陽性対照 C0/C1、実装直前の確認

ご指示の投入前チェックを完了し、設計を修正しました。実装（C0/C1）に入る前に、Gen12.0 固有で
BLORP/genxml からは確定しきれない数点を伺います。

## 反映済み（ご指摘どおり修正）
- **program data 確定**: refcs 出力で P=push.per_thread.regs=0、C=push.cross_thread.regs=0、
  total_scratch=0、total_shared=0、prog_offset[0]=0 → CURBE=0 が正当。KSP=InstructionBase+1024+0。
- **実機トポロジ確定**: `dss_en=0x0000001f` = **5 DSS**（refcs の subslice_total=6 は PCI-ID 既定で実機と不一致）。
  → VFE MaximumNumberofThreads = 112×5−1 = **559**（実装では 0x913c を popcount して runtime 算出）。
- **SBA を PIPELINE_SELECT(GPGPU) の前**に移動（Wa_1607854226）。3D 確立 → pre-SBA flush → SBA →
  state/const/tex/inst invalidation → PIPE_CONTROL(CS_STALL|RT_FLUSH|DEPTH_FLUSH|**HDC_PIPELINE_FLUSH=DW0 bit9**)
  → PIPELINE_SELECT(GPGPU=2, mask0x13, DOP=1) → PIPE_CONTROL(CS_STALL|STALL_AT_SCOREBOARD) → VFE …。
- IDD: NumThreads=1、**BTE=0（A64 only）**、Sampler=0、SLM=0、Barrier=0、ThreadPreemptionDisable=1(DW2 bit20)。
- 観測 3 段: markerReady(walker 前 MI) / EU marker(A64 store 0x100400c20) / markerDone(**post-sync PIPE_CONTROL**)。
- **C0（walker なし）/ C1（walker あり）** で切り分け。fence/polling/PS 変更はまだ入れない。

## 伺いたいこと（Gen12.0 固有、空振り回避のため）

1. **【最重要】compute state fetch と PPGTT batch**。以前 3D で、**リングから実行すると 3D state fetch が
   GGTT 空間で行われ**、PPGTT batch から実行して解決した経緯があります（現在の draw も PPGTT batch）。
   GPGPU も同一 RCS0 kernel_context の PPGTT batch（非特権 MI_BATCH_BUFFER_START）から実行予定ですが、
   **MEDIA_VFE_STATE / MEDIA_INTERFACE_DESCRIPTOR_LOAD / GPGPU_WALKER の state・IDD フェッチ、および
   Interface Descriptor（Dynamic Base 相対）や kernel（Instruction Base 相対）の解決は、3D と同じ
   PPGTT 変換で行われますか**。compute 側に GGTT 前提や別経路の要件（例: IDD/CURBE を GGTT に置く等）は
   ありますか。

2. **MEDIA_CURBE_LOAD を長さ0で発行**する場合、CURBEDataStartAddress=0・CURBETotalDataLength=0 で
   問題ないでしょうか。それとも P=C=0 のときは CURBE_LOAD 自体を省く方が安全ですか。

3. **markerDone の post-sync PIPE_CONTROL**（Write Immediate）を **PPGTT 番地**へ書く場合、Gen12 で
   追加要件はありますか。MI_STORE_DWORD_IMM の marker は PPGTT に着地しています（GGTT ビットなし）が、
   PIPE_CONTROL の post-sync write も同じ PPGTT 番地・同じ扱いで良いでしょうか（GGTT 強制の要否）。
   また Wa_1607156449（post-sync なし stalling PC を前置）は compute batch の終端でこの1回で十分ですか。

4. **compute batch の終端で PIPELINE_SELECT(3D) に戻す必要**はありますか。compute 対照は draw より前に
   clean な kernel_context で走らせ、その後 draw が自前で PIPELINE_SELECT(3D) を発行します。compute を
   GPGPU モードのまま終えても、後続 draw の 3D 選択で復帰する認識で良いでしょうか。context を跨ぐ
   pipeline mode の残存に注意点はありますか。

5. **C0（walker なし）の妥当性確認**。VFE→CURBE_LOAD→MIDL→（walker 省略）→終端同期→marker という列で、
   walker を省いても VFE/MIDL 単体が正常完了する前提で良いですか（＝初期化列の健全性テストとして成立するか）。
   MIDL だけを発行して walker を出さない場合に Gen12 特有の副作用（media パイプの待ち等）はありますか。

（インフラ: refcs/refps/refps_marker/refsurf/refurb/gentool 構築済み。実機 run は再起動不要で連続実行可。
 実装後は VFE 9DW / IDD 8DW / GPGPU_WALKER 15DW の生 DWORD と 3 marker、停止時レジスタ（reset 前採取）を添えます。）
