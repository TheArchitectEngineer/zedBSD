<!-- awesome-plan project=zedbsd record=ws074-p241 -->

# ws074-p241: definiteなstretch cross sizeをflex ratioの主軸計算へ渡す

Status: uncleared（host部分cleared、production link/target確認はpending）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: [ws074-q20261018-02](../queue.md)

## 範囲・受け入れ

p240の残りから、非置換の横書きflex itemについて、単一行・nowrap・stretchでdefiniteになるcross sizeをaspect-ratioのflex baseと自動最小サイズへ渡す。明示的なpx/%のcross sizeも同じ計算に通す。row/column、frame/margin、cross min/max、auto marginやwrapなどの非適用条件を最小例とChromium、固定WPTで確認する。public ABI・shell・WPT本体・他WS・toolchainは変更しない。

依存: p234 ratioとp240自動最小サイズの統合済みコード（53f46c61b）。p240 wholeのtarget確認を前提にしない。変更対象: libbrowser/layout/flex.c、WS074の試験と記録。Guardrail、C全文、browser-component全文を適用。

最大90分。独立geometry、aspect-ratio固定257 IDの同条件前後（baseline149PASS）、host build warning0、変更関数の全文review/format/diff check。区切りでAcid3差0・Acid2非増加とproduction buildを確認する。QEMU/実機は別のtarget確認として残し、host部分のclearanceと区別する。

## 開始（2026-10-18）

[自走の指示](../autonomy.md)で後続有限Queueを承認。009/045/046は、cross sizeが配置段で初めて決まり、それより前のbase/minimumが0になる。p240の成果と未達は保持し、本Phaseのscoped outputで残りを補う。Master/共有Queue/GitHubのprojectionはpending。

Design note（2026-10-18）: cross sizeの決定を共通helperにし、baseとminimumだけでなくrowのcross relayout/最終配置でもその制限を保つ。従来の配置はmin/maxとauto marginを無視して高さを上書きしていた。p241のcross-size整合の範囲内で直し、frame・margin・minmax・非適用の独立11例を既存29例へ追加した。

Checkpoint: 40/40 geometry（Chromium39一致/既存仕様差1）。共有toolchain/sysrootが他WSのRISC-V追加で更新中のため、通常のproduction makeはsysroot再生成を要求する。WS074から共有toolchain/sysrootを変更せず、make dry-runの同じtarget compiler/flagsを使ったflex.cの専用object compileはwarning0。production libraryの再buildは共有作業後の関門としてpending。

## ws074-q20261018-02-i01 結果（2026-10-18）

Event ws074-p241-host-partial-clear-20261018: [証拠](evidence/20261018/README.md)。固定補助WPT149→152/257 PASS（009/045/046）、後退0。独立40/40、Chromium39一致/既存仕様差1、host build warning0、amd64 object compile warning0、Acid3差0/Acid2差873。探索中のshared-library上書きによるrunner errorは固定binaryで全件再実行して解消。host部分cleared、wholeはproduction link/target未実施によりuncleared。補助試験を全Interop2025の達成率に加算しない。次の有限Queueはp189/p190の最新censusとrunner coverageに進む。p240の未達をclearとせず、残るbasis/replacedも保持する。

Commit receipt: `9b94ebe38` WIP。git logのread-backで実装・試験・自走承認・結果の11file保存を確認。

Event ws074-p251-linear-type-design-20261018: [p251](../phase251/phase.md)がdirect linear calcのpercentage存在を保持する。0係数でもCSS_UNIT_PERCENTとなるためdependent computed/used pathsを全1491/static1561と独立値で検証。nonlinear min/max/clampの既存近似は未変更。p250はこのoutput後にcomputed fitを検証し、p246/q11等history/whole残りは維持。
