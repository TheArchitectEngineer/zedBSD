<!-- awesome-plan project=zedbsd record=ws229 -->

# WS229: i915 を Gen9・Gen9.5（Core の 6〜10 世代の iGPU）に対応させる

<!-- awesome-plan-current:start -->
Status: planned（2026-10-14 Q1 作成）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q976（p001、P1）
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

「手元には第6、7，8、11，12、13世代、のCore i5があります。」→ Gen9（6・7 世代）と Gen9.5（8 世代）の実機の確かめができる。11〜13 世代は今の Gen12 の経路の回帰（desktop の model なら PCI ID の追加が要る）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計: Gen9・9.5 と Gen12 の違いの一覧（EU の命令の符号化・send の descriptor・execlists の ELSP・forcewake・MOCS・context の layout・workaround・3D と compute の state・GOP のままの起動）、Phase の分け方と見積もり | planned（q976、P1） | — |
