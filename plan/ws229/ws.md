<!-- awesome-plan project=zedbsd record=ws229 -->

# WS229: i915 を Gen9・Gen9.5（Core の 6〜10 世代の iGPU）に対応させる

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 p001 cleared、p002〜p004 を並行で実装）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q977（p002、P2）・q978（p003、P1）・q979（p004、P3）
Target: ベータ2 に入れば入れる（2026-10-14 ユーザー「Gen9対応は進めて、今日の夕方のコードフリーズに間に合えば入れます。間に合わなくてもドロップはオーケーです。」）。間に合わなければベータ3
Resume point: p001。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「i915ドライバを、Core i5 の第8世代のiGPUに対応させるの、どれくらい難しいと思う？Gen9かGen9.5のEUって、命令が2つペアになってた気がする。EUコンパイラは流用できるんだろうか。」
「了解。これはベータ2の必須ではないけど、裏で走らせてくれる？まずGOPのバッファのままで表示できるようにして、最後にディスプレイコントローラもドライバを実装する。」

## 目標

- Gen9（Skylake・Kaby Lake）と Gen9.5（Coffee Lake・Whiskey Lake・Comet Lake など）の iGPU で、i915 の GT（render・compute・blit）と EU コンパイラの後段を動かし、Vulkan（libvulkan）が使えること。
- 段階: (1) 表示は GOP の framebuffer のまま（display controller に触らない）で GT と EU を動かす。(2) 最後に Skylake 系の display controller（DPLL・DDI の buffer translation・power well・watermark・CDCLK・PCH の hotplug・DMC）を実装する。
- 今の Gen11・Gen12 の経路を壊さない（Tiger Lake・Alder Lake の 5320・5330）。

## Q1 の見積もり（2026-10-14、±50 %）

GOP のままで GT と EU: 6〜8 LW。display controller を足して全体: 10〜15 LW。p001 の調べで締める。

## 実機（2026-10-14 ユーザー）

「手元には第6、7，8、11，12、13世代、のCore i5があります。」「全部Laptopです。」→ 6 世代（Skylake、Gen9）・7 世代（Kaby Lake、Gen9.5）・8 世代（Kaby Lake R か Whiskey Lake、Gen9.5）で実機の確かめができる。11〜13 世代の laptop（Tiger Lake・Alder Lake-P・Raptor Lake-P）は今の PCI ID にあり、Gen12 の経路の回帰に使う。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（a〜d の答え、Phase の分け方 §5、見積もり 14.5 LW） | cleared（2026-10-14） | — |
| p002 | GT の起動（Gen9）、最初の commit で共有の契約（`gt->graphics_ver`） | planned（q977、P2） | p001 |
| [p003](phase003/phase.md) | EU の符号化（Gen9）、`drv_i915_shader_compile_gen()`、SENDS、fence の stall | in-progress — merge 済み（28d6deaae）、host の受け入れ PASS（Gen12 の 88 の kernel の byte が同一、Gen9 の 96 の往復と期待の byte）。実機は p005 | p001 |
| p004 | 3D と compute の state（Gen9） | planned（q979、P3。P3 の ws143 の直しの後） | p001、p002 の契約 |
| [p005](phase005/phase.md) | Vulkan の通しと実機（6・7・8 世代を USB で）、5330 の回帰 | planned（q980、P1。準備から） | p002〜p004 |
| p006 | display controller（Gen9） | planned（今日の範囲外） | p005 |
| p007 | 規約の全文の見直し | planned | p005 |
