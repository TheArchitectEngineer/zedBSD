
## p011 増分E-16 (2026-09-15): compute 陽性対照 C0/C1 — 問題は PS 固有でなく EU 共通と判明

専門家の C0/C1 設計（SBA 先行、CURBE 省略、MIDL 前 MEDIA_STATE_FLUSH、post-sync PPGTT PC、
Wa_1607156449）を実装し実機投入。**分岐点の結果**が出た。

### C0（walker なし）= 完全成功
```
compute C0 ready=0xc0ffee10 eu=0xdead0000 done=0xc0ffee20 done_hi=0x00000000 cs=0xc0ffee30 completed=3 seqno=3
```
markerReady 更新 / EU 初期値 / **markerDone(post-sync PPGTT write) 更新** / markerCS 更新 / 完走。
→ GPGPU 初期化列（PIPELINE_SELECT(GPGPU)、SBA(3D モードで)、VFE(MaxThreads=559)、MEDIA_STATE_FLUSH、
MIDL）+ **非特権 batch の PPGTT post-sync PIPE_CONTROL** + end-of-pipe 完了、すべて正常。専門家の
「非特権 batch で GGTT post-sync は NOOP → PPGTT で書く」も実証（markerDone が PPGTT 番地に着地）。

### C1（walker あり）= ハング（PS と同一署名）
```
compute C1 ready=0xc0ffee10 eu=0xdead0000 done=未更新 cs=未更新 completed=3 seqno=4
HANG: ipehr=0x70040000(MEDIA_STATE_FLUSH) acthd=0x100600150 instdone=0xffdeffff sc=0xffffffff row=0x8610e87f
```
- markerReady 更新（CS は walker まで到達、walker を発行）。
- **EU marker 未更新**。compute の A64 store は**無条件（predicate なし、gentool 確認済み）**なのに未着地 →
  **compute スレッドが store 命令を実行していない**（PS の sample-mask 交絡がないので確定的）。
- CS は walker 後の MEDIA_STATE_FLUSH でスレッド完了を待って停止（IPEHR=0x70040000）。
- **row_instdone=0x8610e87f = PS ハングと完全に同一**。EU not-done も同じ。

### 結論（調査の大転換）
- batch/VFE/walker/IDD は全 DW を dump して確認済み（VFE MaxThreads=0x22f=559、walker SIMD8/dim=1/
  right=0x1、MIDL offset=0x380、post-sync 0x7a000004/0x104000/done_va/tag）。設定は仕様どおり。
- **PS と最小 compute が同一署名でハングし、無条件 compute store すら実行されない** →
  問題は **PS 固有（windower dispatch）ではなく、PS/compute 共通の「EU が dispatch された
  スレッドを実行/完了できない」障害**。命令フェッチ/スレッド起動段の共通故障（旧仮説 a）が最有力に復帰。
- ただし専門家の C1 判定（EU 未更新+Done 未更新）通り、「新規 compute dispatch 設定の不備」も論理的には
  残る。ただし walker/VFE/IDD の全 DW が仕様一致で、かつ PS と同一署名という点が「共通 EU 障害」を強く支持。

### 次に切り分けるべき点
- EU 命令フェッチ（Instruction Base 相対、KSP=instruction+1024）が両者で失敗している可能性。
  以前 PS で「命令領域を EOT で carpet しても hang」だったが、その再検証を compute の無条件 store で行える。
- EU の電源/クロック/enable（RPCS、EU fuse）— rpcs(ctx/reg)=0x80041000 は設定済みだが要再確認。
- スレッド完了通知（EOT）経路の共通故障。

C0 の完全成功で「submission/PPGTT/state/post-sync/完了」は健全と確定。障害は EU スレッド実行そのものに局在。
default ビルド warning 0。i915.c は bring-up 中 draw をスキップ（`if(0)`）、compute を rt の後に配置。
