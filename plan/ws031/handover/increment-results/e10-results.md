
## p011 増分E-10 (2026-09-15): WM.StatisticsEnable 監査 — ps=0 の再解釈と windower dispatch 疑い

専門家の助言 #1「3DSTATE_WM を BLORP と同じ空にした結果 StatisticsEnable を落としていないか」を監査。

### 発見: PS 統計はずっと無効だった
`3DSTATE_WM` DW1 = 0（行874「空、as BLORP emits」）だった。DW1 bit31 = Statistics Enable
（E-6 の落とし穴メモ点3 でも認識済みだったが、PS 追加時に空 WM にして誤って落とした）。
→ **E-7/E-8 の全 PS-hang 調査で読んでいた `ps_invocations=0` は計数無効による当然のゼロで、
証拠価値がなかった。** 他ステージ（VS DW7 bit10, CLIP/SF dw1 bit10）は立っていたので vs/cl は出ていた。

### 実機 (E-10): WM DW1 bit31 を立てて再測
```
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```
統計有効でも **ps=0**。この値はハング診断内の**直接 MMIO 読み**（`GEN12_REG_PS_INVOCATION_COUNT`,
行1479）で、wedge 中でも ia/vs/cl が正しいライブ値を返す＝カウンタブロックは生きている。
（前回まで使っていた post-hang の MI_STORE リクエスト方式は wedge したエンジンで完走せず
"statistics readback failed" になる。直接 MMIO 方式が正解。）

### 固定機能フィールドの genxml 厳密監査（全て正しい）
gen120/gen110/gen90.xml でビット位置を機械確認:
- 3DSTATE_PS DW6: bit0=8PixelDispatchEnable=1 ✓, bits31:23=MaxThreadsPerPSD=63 ✓(=64-1, Mesa と同じ),
  DW7 bits22:16=grf_start0=2 ✓, DW3 bits25:18=BindingTableEntryCount=1 ✓, DW4 scratch=0 ✓, DW1 KSP0=1024 ✓
- 3DSTATE_PS_EXTRA DW1: bit31=PixelShaderValid=1 のみ, bit30(mbz)=0 ✓, AttributeEnable=0（無属性PSで正）
- 3DSTATE_SBE DW1: VertexURBEntryReadLength=1（**非ゼロ、windower stall の典型 ReadLength=0 ではない**）,
  ReadOffset=1, ForceOffset/Length=1, NumSFOutputAttr=0 — position-only で妥当

### 解釈（report5 の fetch 寄り結論を反転させ得る）
- ps=0（dispatch カウンタ）→ 素直には **windower が PS スレッドを一つも dispatch していない** (仮説b)。
  これは report5 の有力仮説 (a)「EU 命令フェッチ失敗（スレッドは EU 上に存在）」と矛盾する
  （dispatch されていれば fetch で止まっても dispatch カウンタは加算されるはず）。
- ただし専門家の caveat: 統計の意味ある読み出しには pipeline flush が要り、drain 未完のこのケースでは
  ゼロ解釈に制限が残る（dispatch は起きているがカウンタが flush 前で不可視の可能性）。
- かつ固定機能フィールドは全て genxml 正しく、「dispatch を妨げる明白なステートバグ」は未発見。
- → **dispatch-vs-fetch の決着には専門家推奨の PS 内メモリ marker（dispatch を統計非依存で確認）が必要。**

差分: selftest.c 行874（WM StatisticsEnable=bit31）。run 機構: `/tmp/run-e10.sh`（build selftest config →
scp guest → vfio passthrough boot、debugcon 捕捉）。再起動不要（qemu 終了時 FLR で vfio リセット、連続 run 可）。
