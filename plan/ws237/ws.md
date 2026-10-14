<!-- awesome-plan project=zedbsd record=ws237 -->

# WS237: i915 の Gen9・Gen9.5（第 6〜10 世代 Core）の display controller の制御

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 起票・着手、P2）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS229](../ws229/ws.md)（Gen9 の GT と Vulkan、display は GOP のまま）、[BUG-310](../bugs/BUG-310.md)（bootfb: GPU で描いて GOP の framebuffer へ CPU で写す、resolved）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「SV7でKeilandが起動でき、利用できました。せっかくなので、ディスプレイコントローラの制御も実装してください。」

## 目標

- Gen9・Gen9.5 で i915 が display controller を自分で制御し、bootfb の CPU の写し（tearing・copy の費用）を無くす。
- 段: (1) firmware が点けた pipe・plane・panel の状態の readout と、同じ mode のままの primary plane の flip（vblank の割り込みで page flip、copy 無し）、(2) panel の backlight（明るさ）、(3) mode set（PLL・DDI・pipe・transcoder）と外部の DP・HDMI、(4) suspend・resume の時の display。
- ベータ2 の既定: 段 (1) が実機で通れば、bootfb の代わりに使う。通らなければ bootfb のまま（drop できる形）。
- source の手本: Linux の i915 は MIT（file ごとに確かめる）。zedBSD の Gen12 の display（resident display・takeover）の作りに合わせる。

## Phase（見積もりは [p001 の design.md](phase001/design.md) §5、±50 %）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [ws237-p001](phase001/phase.md) | 設計: SKL・KBL・CFL の display の register と順（Gen12 の display の code との差）、readout、flip と vblank、backlight、試験の道（SV7 の passthrough と素の起動） | cleared の候補（2026-10-14 P2、Q1 の判定待ち） |
| ws237-p002 | readout と primary plane の flip（段 1）、1.5 LW | in-progress（P2） |
| ws237-p003 | backlight、1.0 LW | planned（p002 の実機の結果の後） |
| ws237-p004 | mode set と外部の display、6.0 LW | planned |
| ws237-p005 | suspend・resume の display、1.5 LW（番号は 2026-10-14 Q1） | planned |
| ws237-p006 | 規約の全文、0.5 LW | planned |
