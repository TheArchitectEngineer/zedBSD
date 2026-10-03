# Gen12 PS ハング 第6報 — WM.StatisticsEnable 監査の結果、ps=0 を再取得。dispatch 疑いへ

ご助言 #1（「3DSTATE_WM を BLORP と同じ空にした結果、統計を無効にしていないか」）を実行しました。
**まさにご指摘の通り、統計が無効でした。** その上で有効化して再測した結果を報告します。

## 1. 監査結果: PS 統計はずっと無効だった（これまでの ps=0 は無意味だった）

- `3DSTATE_WM` DW1 = 0 でした（「空、BLORP と同じ」にした際に落とした）。DW1 bit31 = Statistics Enable。
- 他ステージ（VS DW7 bit10、CLIP/SF DW1 bit10）は立っていたので vs/cl は計数されていました。
- → **第2〜5報で「PS 未実行の証拠」として扱っていた `ps_invocations=0` は、計数無効による当然のゼロで、
  証拠価値がありませんでした。** ご指摘に深く感謝します。

## 2. WM.StatisticsEnable=1 で再測（実機）

```
hang stats ia_vert=3 ia_prim=1 vs=3 cl_inv=1 cl_prim=1 ps=0
```

- この値は**ハング中の直接 MMIO 読み**（PS_INVOCATION_COUNT レジスタを CPU から直読）です。
  wedge 中でも ia/vs/cl は正しいライブ値を返すので、カウンタブロックは生きています。
- 統計を有効化しても **ps=0** のままでした。

## 3. 固定機能フィールドを genxml で厳密監査（すべて正しい）

「windower が dispatch できない典型フィールドバグ」を疑い、gen120/gen110/gen90.xml でビット位置を
機械照合しました。結果、以下はすべて構造的に正しい:

- **3DSTATE_PS**: DW6 bit0 = 8-Pixel-Dispatch-Enable = 1、bits31:23 = MaxThreadsPerPSD = 63（= 64−1、
  Mesa の `max_threads_per_psd − 1` と一致、非ゼロ）、DW7 bits22:16 = grf_start0 = 2、
  DW3 bits25:18 = BindingTableEntryCount = 1、DW4 Per-Thread-Scratch = 0、DW1 KSP0 = 1024。
- **3DSTATE_PS_EXTRA**: DW1 bit31 = PixelShaderValid = 1 のみ、bit30(mbz) = 0、AttributeEnable = 0（無属性PSで正）。
- **3DSTATE_SBE**: VertexURBEntryReadLength = **1**（非ゼロ。windower stall の典型 ReadLength=0 ではない）、
  ReadOffset = 1、ForceOffset/Length = 1、NumSFOutputAttributes = 0（position-only で妥当）。

## 4. 現在の解釈（第5報の「fetch 寄り」結論を反転させ得る）

- ps=0（dispatch カウンタ）を素直に読むと、**windower が PS スレッドを一つも dispatch していない**（仮説b）。
  これは第5報の有力仮説 (a)「EU 命令フェッチ段で停止（スレッドは EU 上に存在）」と**矛盾**します
  （dispatch されていれば、その後フェッチで止まっても dispatch カウンタは加算されるはずなので）。
- 一方で、ご指摘の caveat が効きます: 統計の意味ある読み出しには pipeline flush が要り、drain 未完の
  本ケースでは「dispatch は起きているがカウンタが flush 前で不可視」の可能性が残ります。
- さらに、固定機能フィールドは genxml で全て正しく、**dispatch を妨げる明白なステートバグは未発見**です。

## 5. 伺いたいこと

1. **PS_INVOCATION_COUNT は dispatch 時点で加算されますか、それとも thread retire / flush 境界でのみ
   可視になりますか。** ia/vs/cl は wedge 中のライブ MMIO 読みで正しい最終値を返しています（＝これらは
   flush 非依存でライブ加算）。PS カウンタだけが特別に flush 依存ということはありますか。もし dispatch 時点で
   ライブ加算されるなら、ps=0 は「dispatch されていない」の強い証拠になります。

2. **本命の PS 内メモリ marker テストの設計**をご確認ください。予定:
   - PS の**先頭命令**で、副作用ストア（A64 stateless、または RT とは別ページ）で marker を書く。
   - 副作用シェーダに合わせ **WM.ForceThreadDispatch=ON / EarlyDepthStencil=PSEXEC**（Iris が
     has_side_effects FS に対して行う設定）へ変更。
   - drain 未完でも CPU から marker を観測できるよう、store/MOCS/mapping/CPU 側読みを別途検証（PIPE_CONTROL
     に依存しない）。
   この設計で「dispatch されたか」を統計非依存で確定できるという理解で合っていますか。marker の
   ストアパスとして、RT write と同じ dataport を避けるべき（RT は RCC idle だった）という理解で
   正しいでしょうか。

3. 固定機能フィールドが全て正しいのに windower が dispatch しない場合、Gen12 で残る典型原因
   （PS スレッド用の URB/スクラッチ/PSD リソース、Push Constant の必須性、あるいは 3DPRIMITIVE 前の
   PIPE_CONTROL（現在 CS_STALL | STALL_AT_SCOREBOARD | DEPTH_STALL を入れています）の要否/順序）で、
   優先的に疑うべきものはありますか。

（インフラ: refps/refsurf/refurb + gentool 構築済み。次段として BLORP clear の全 3DSTATE を standalone で
emit させて全 packet をバイト照合する refblorp ハーネスも構築可能です。実機 run は再起動不要で連続実行できます。）
