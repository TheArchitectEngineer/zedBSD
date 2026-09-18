# Gen12 PS ハング 第7報 — 対照試験 A/B/C 完遂。dispatch されていない疑いが強化、ただし未確定

ご指示の A/B/C を実行しました。結論から言うと、**「windower が PS を dispatch していない」疑いが
二つの独立観測で強まりましたが、ご指摘の caveat（marker の可視性）により確定には至っていません。**

## Test B — WM だけ変更（同一シェーダバイナリ）

WM DW1 = StatEnable | **ForceThreadDispatchEnable=ForceON** | **EDSC_PSEXEC**
（gen80.xml でビット確認: ForceThreadDispatch bits20:19=2、EarlyDepthStencil bits22:21=1）。
加えて **pre-draw PIPE_CONTROL の直後・3DPRIMITIVE の直前に CS marker**（MI_STORE_DWORD_IMM）を追加。

```
markerA=着地  markerMid=着地  markerB=未着地   ps=0
IPEHR=0x7a000204（post-draw drain PIPE_CONTROL）
```

- **markerMid 着地** → CS は pre-draw の CS_STALL|STALL_AT_SCOREBOARD|DEPTH_STALL を通過し、
  3DPRIMITIVE を発行済み。**ハングは pre-draw stall ではなく post-draw drain** で確定。
- **ForceThreadDispatch=ForceON でも ps=0**。coverage 非依存で dispatch を強制しても windower は
  PS を一つも dispatch しない → 「windower が dispatch できない」の強い材料。WM state 変更では解消せず。

## Test C — PS 先頭に A64 data-cache marker を追加

refps_marker（refps.c + `nir_store_global(0xc0ffee01 → 0x100400c10)`）を brw_compile_fs でコンパイル。
**gentool 逆アセンブルで確認**:

```
send.hdc1 (8) null r4 r6 ... a64_untyped_write:x simd8 flat   ← marker（Data Port 1、RT とは別経路）
sendc.render (8) ... {EOT} bti(0)                             ← 通常の RT write（後）
```

marker は RT 経路ではなく HDC1/A64、RT write の前、アドレス・値も正確。PS_EXTRA に HasUAV(bit2) を追加、
WM は B のまま、SIMD8 dispatch（grf_start0=2, scratch=0、コンパイル結果に対応）。

```
markerMid=着地  markerPS=0x00000000（A64 marker 未着地）  ps=0  RT pixels=全0
```

## 解釈（ご提示の fixed interpretation に従う）

- **ps=0（ForceON でも）と markerPS=0 の二つの独立 negative** → PS は store まで実行していない、を強く示唆。
- ただし markerPS=0 は「未 dispatch / 初期停止 / marker のメモリ経路・可視性の問題」いずれとも両立し、
  **確定ではありません**。特に、A64 write が drain 未完で L3 に留まり memory 未達＝CPU から
  見えないだけ、の可能性が残ります（同一 store の CPU 可視性をまだ独立検証していないため）。

## 伺いたいこと

1. **markerPS を確定させる最短手段**をご教示ください。ご提案の compute-shader 陽性対照（同一 RCS/PPGTT/
   命令配置で同じ A64 store を正常完了させ CPU 可視を検証）を実装する予定ですが、compute パイプライン
   （CFE_STATE / COMPUTE_WALKER）は新規の大きめ実装です。**より安価に、drain を待たずに A64 write を
   CPU 可視にする方法**（例: marker ページ／A64 メッセージの MOCS を uncached/write-through にして
   memory に直接届かせる、あるいは L3 側を読む手段）はありますか。あれば C を再走させるだけで確定できます。

2. **ForceThreadDispatch=ForceON でも一つも dispatch されない**場合、PS/PS_EXTRA/SBE/DEPTH の各フィールドは
   genxml で正しいことを確認済みですが、Gen12 で windower が PS スレッドを spawn する際に残る前提条件
   （PS スレッド payload 用の URB/thread 割当、scratch base が scratch=0 でも必須か、
   BINDING_TABLE_POOL、あるいは PS 有効時のみ必要になる depth/HiZ 以外の 3DSTATE）で、
   優先的に疑うべきものはどれでしょうか。

3. 次の調査基盤として **refblorp**（BLORP 通常色書き経路の全 packet + 参照データ + caller 初期化を採取して
   照合）を、compute 対照と並行して着手する価値はありますか。優先順位のご意見を伺えれば。

## 現状の確定 negative（整理）
- pre-draw stall は原因でない（markerMid 着地）。
- coverage/dispatch-gating は原因でない（ForceON 無効）。
- RT write の形式は無関係（第5報）。PS/PS_EXTRA/SBE/DEPTH フィールドは genxml 正しい（第6報/今回）。
- kernel/surface/URB は Mesa 一致（第4報）。
- → 残る mechanism 不明。dispatch 前後（windower の spawn 前提 or A64 可視性）に収束。

（インフラ: refps/refps_marker/refsurf/refurb/gentool 構築済み。実機 run は再起動不要で連続実行可。）
