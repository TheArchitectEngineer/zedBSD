# Gen12 PS ハング 第10報 — compute 陽性対照が分岐点。問題は PS 固有でなく EU 共通

ご指示どおり C0/C1 を実装・実機投入しました。**調査を大きく分岐させる結果**が出ました。

## C0（walker なし）= 完全成功
```
compute C0 ready=0xc0ffee10  eu=0xdead0000(初期)  done=0xc0ffee20  cs=0xc0ffee30  completed=3 seqno=3
```
- markerReady 更新、EU marker 初期値、**markerDone（post-sync PPGTT write）更新**、markerCS 更新、完走。
- → GPGPU 初期化列（3D モードで SBA → PIPELINE_SELECT(GPGPU) → VFE(MaxThreads=559) → MEDIA_STATE_FLUSH →
  MIDL）、**非特権 batch の PPGTT post-sync PIPE_CONTROL**、end-of-pipe 完了、すべて正常。
  ご指摘の「非特権 batch で GGTT post-sync は NOOP → PPGTT で書く」も、markerDone が PPGTT 番地に
  着地したことで実証できました。

## C1（walker あり）= ハング、しかも PS と完全同一署名
```
compute C1 ready=0xc0ffee10  eu=0xdead0000(未更新)  done=未更新  cs=未更新  completed=3 seqno=4
HANG: ipehr=0x70040000(post-walker MEDIA_STATE_FLUSH)  acthd=0x100600150  instdone=0xffdeffff
      sc=0xffffffff  row_instdone=0x8610e87f
```
- markerReady 更新 → CS は walker を発行。
- **EU marker 未更新**。今回の compute store は**無条件（predicate なし、gentool で確認済み）**なのに未着地
  → **compute スレッドが store 命令を実行していない**（PS の sample-mask 交絡がないので確定的）。
- CS は walker 後の MEDIA_STATE_FLUSH でスレッド完了を待って停止。
- **row_instdone=0x8610e87f は、これまでの PS ハングと完全に同一**。EU not-done も同じ。

batch は全 DW を dump して確認済みです（抜粋）:
```
VFE:   70000007 0 0 022f0200(MaxThreads=559,URB=2) 0 00020000(URBAlloc=2) 0 0 0
MIDL:  70020002 0 00000020(len32) 00000380(IDD@dyn896)
WALKER:7105000d 0 0 0 0 0 0 1(Xdim) 0 0 1(Ydim) 0 1(Zdim) 1(rightmask) ffffffff(bottom)
PC-B:  7a000004 00104000 00400c28(done_va lo) 00000001(hi) c0ffee20(tag) 0
```

## 解釈（ご提示の C1 判定に沿って）
- ご判定表では「EU 未更新・Done 未更新 = 新規 compute の dispatch 設定と共通基盤の両方が残る、停止位置で分ける」。
- 停止位置は **post-walker MEDIA_STATE_FLUSH**（＝walker は消費され、スレッド完了待ちで停止）。
  walker/VFE/IDD の全 DW が仕様一致で、かつ **PS と同一の EU not-done 署名**である点から、
  私の暫定判断は「**PS/compute 共通の EU スレッド実行/完了の故障**」です。無条件 compute store すら
  実行されないので、少なくとも「スレッドが命令を実行して store まで到達」はしていません。
- これにより、これまで PS 側で疑っていた windower dispatch・sample-mask・固定機能ステートは、
  **少なくとも唯一の原因ではない**と分かりました（compute には windower も PS 固定機能もないため）。

## 伺いたいこと
1. **この署名（walker 発行済み・post-walker flush で停止・EU not-done・無条件 store 未実行）から、
   共通の EU スレッド実行故障として次に確認すべき点**をご教示ください。候補として:
   - EU 命令フェッチ（Instruction Base 相対 KSP=instruction_base+1024）が両者で成立していない可能性。
     以前 PS で「命令領域を EOT で carpet しても hang」でしたが、compute の**無条件 store** で
     「スレッドが1命令でも実行するか」をより確実に再検証できます。EU が正しい PPGTT/L3 経路から
     命令をフェッチしているかを確定する方法（EU IP、SIP、あるいは既知良好な KSP 配置）はありますか。
   - EU の電源/クロック/enable。現在 rpcs(ctx)=rpcs(reg)=0x80041000（slice/subslice/EU 設定済みのつもり）
     ですが、dispatch されたスレッドが走らない場合に疑う RPCS/EU fuse/クロックゲートの具体点は。
   - スレッド完了通知（EOT）経路。compute は `send.ts {EOT}`、PS は `sendc.render {EOT}`。両者で
     EOT が Thread Spawner に届かない共通要因（例: TS の設定、あるいは EOT の宛先）はありますか。
2. C1 の「compute dispatch 設定の不備」を完全に排除するため、**最小限の追加確認**（例: IDD の KSP を
   直読で照合、walker の thread counter/mask を別値で試す）で有効なものがあればご指示ください。

（C0 の完全成功で submission/PPGTT/state/post-sync/完了経路は健全と確定。障害は EU スレッド実行に局在。
 refcs/gentool 等インフラ構築済み。実機 run は再起動不要で連続実行可。default ビルド warning 0。）
