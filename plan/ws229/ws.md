<!-- awesome-plan project=zedbsd record=ws229 -->

# WS229: i915 を Gen9・Gen9.5（Core の 6〜10 世代の iGPU）に対応させる

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。Gen9 の GT・EU・3D と compute は既定で有効でベータ2 に入った。p006 の display controller は WS237（2026-10-15 ユーザー「Gen9は残りの安全策の作業をマージしたら終了です」）と WS260 で扱った）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Gen9（Skylake・Kaby Lake）と Gen9.5（Coffee Lake・Whiskey Lake・Comet Lake など）の iGPU で、i915 の GT（render・compute・blit）と EU コンパイラの後段を動かし、Vulkan（libvulkan）を使えるようにする。段階は (1) GOP の framebuffer のままで GT と EU、(2) 最後に Skylake 系の display controller。Gen11・Gen12 の経路（5320・5330）を壊さない。

## ユーザーの決定

- 2026-10-14（原文）「i915ドライバを、Core i5 の第8世代のiGPUに対応させるの、どれくらい難しいと思う？Gen9かGen9.5のEUって、命令が2つペアになってた気がする。EUコンパイラは流用できるんだろうか。」「了解。これはベータ2の必須ではないけど、裏で走らせてくれる？まずGOPのバッファのままで表示できるようにして、最後にディスプレイコントローラもドライバを実装する。」
- 2026-10-14「Gen9対応は進めて、今日の夕方のコードフリーズに間に合えば入れます。間に合わなくてもドロップはオーケーです。」
- 2026-10-14（実機）「手元には第6、7，8、11，12、13世代、のCore i5があります。」「全部Laptopです。」
- 2026-10-15（WS237）「Gen9は残りの安全策の作業をマージしたら終了です」

## 結果

- p002: GT の起動（request.c・boot の key `i915.gen9`・suspend の拒否・N0-lite）、main 93787f2be。host の受け入れで Gen12 の起動の書き込み 543 行が基準と同一。
- p003: EU の符号化（Gen9、SENDS、fence の stall）、main 28d6deaae。Gen12 の 88 の kernel の byte が同一、Gen9 の 96 の往復。
- p004: 3D と compute の state（Gen9）、main 42431971b。Gen12 の byte が同一、gen90.xml の厳密な decode。
- p005: Gen9.5 の実機（Let's Note SV7、UHD 620、QEMU の passthrough）で ktest・eu・vkcs・vkx・vke1・vke2・vkc・draw・r1 が全部 PASS（T1-608・T1-612）、5330（Gen12）の回帰。
- p007: 規約の全文の見直し（host の試験 5 本 PASS・byte 不変）。
- 既定で有効。Gen9 の display controller は WS237・WS260 の物（この WS の p006 では実装していない）。

## 制限・移管

- 6 世代（Skylake）・7 世代（Kaby Lake）の実機は未確認（記録は Gen9.5 の SV7 だけ）。
- display controller（p006）は WS237・WS260 へ。
- 回帰に残す試験（`eu-gen9-host.sh`、`host/run.sh`、`eu-test-gen9-host.sh`、`run-bootfb-host-test.sh`）は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws229-p001 | 調べと設計（Phase の分け方、見積もり 14.5 LW） | cleared |
| ws229-p002 | GT の起動（Gen9） | in-progress のまま閉じた（main 93787f2be、実機は p005 で PASS） |
| ws229-p003 | EU の符号化（Gen9） | in-progress のまま閉じた（main 28d6deaae、実機は p005 で PASS） |
| ws229-p004 | 3D と compute の state（Gen9） | in-progress のまま閉じた（main 42431971b、実機は p005 で PASS） |
| ws229-p005 | Vulkan の通しと実機、5330 の回帰 | cleared（T1-608・T1-612） |
| ws229-p006 | display controller（Gen9） | この WS では未着手。WS237・WS260 で扱った |
| ws229-p007 | 規約の全文の見直し | cleared |

## 記録の所在

Phase の計画・結果・試験は削除する（回帰に残す試験は plan/tools/ へ移す）。git の commit `fb1f6afee` の `plan/ws229/` にある。Queue の履歴は [plan/history](../history/index.md)。
