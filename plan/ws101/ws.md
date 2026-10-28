<!-- awesome-plan project=zedbsd record=ws101 -->

# WS101: GPU の compute（i915 の Vulkan の compute、GLES 3.1 の compute、Noct の自動並列化が 5330 の GPU で動く）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。目標の「Noct の自動並列化が 5330 の GPU で動く」は L1 で達成（p015 S13 cleared、2026-09-30、5330 の passthrough）。最適化は 2026-09-30 ユーザー「GPUでは疎通できたので、最適化の優先度を下げます」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-09-30 ユーザー「デモのときに来客から、Compute Shaderが使えるのか聞かれて、もし使えないと答えたらがっかりさせてしまいますね。これはOSCデモ（10月17日）には使えるようにしたいです。ただし、優先事項の中では最優先ではない、くらいです。NoctにはOpenGL ESを使ってCompute Shaderを生成してGPUアクセラレーションを行う自動並列化機能が入っており、これが動くようにするのをゴールにしたらどうでしょう。Compute Shaderに対応しておけば、ローカルLLMも動かせるようになって、夢が広がります。」

達成基準: G1 i915 のネイティブの実行器の Vulkan の compute、G2 libglesv2 の OpenGL ES 3.1 の compute、G3 Noct の自動並列化（`accel_opengles.c`）が GPU の backend を選び CPU と一致、G4 fg010 の台本の場面（S13）。設計は [design.md](design.md)、作業の手引きは [guide.md](guide.md)。

## ユーザーの決定

- 2026-09-30 朝（design.md §7）: D1「OpenGL ES 3.1」を名乗ってよい、D2 構成で選ぶ形で Noct の accel の build の変更を許可（`ZEDBSD_NOCT_ACCEL := y`）、D3 N = 4,000,000・3 倍以上、D5 デモの見本は整数だけ。
- 2026-09-30 朝 の方針「広く浅く」: 全 WS が同じ段にそろってから次の段へ（L1〜L3）。
- 2026-09-30「GPUでは疎通できたので、最適化の優先度を下げます」。
- 2026-09-30（p017 の後、次の手の問いに）「今のまま」→ S13 は今の見本のまま「動くこと」を見せ、最適化はここで止める。p012・p018 はユーザーが再開を言う時まで保留。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p013 は WS の外の一括の見直しへ。

## 結果

- G1: i915 の compiler（GLCompute・built-in・SLM・barrier・atomic・OpArrayLength）と実行器（`vkCreateComputePipelines`・`vkCmdDispatch`・`vkCmdDispatchIndirect`、`vk/vk-compute.c`）。5330 の passthrough で kernel の試験の場面 vkcs 21/21 PASS、回帰 vkx・vke1・vke2・vkc PASS（p002〜p007）。
- G2: GLSL ES 3.10 の compute（p008、host）、libglesv2・libegl の ES 3.1 の compute の API（p009、Venus で glescompute PASS）、i915 の passthrough で glescompute PASS（p010）。
- G3・G4: Noct の toolchain の差分を main が当てた（189c3bf1）。5330 の passthrough で `mix.nct`（N = 4,000,000）が CPU と全要素一致、GPU の dispatch 8 回（p011）。デモの image の kei の Terminal で `sh /usr/share/gpudemo/s13.sh` が通った（p015）。
- L2: 時間の分解の道具（`KEI_GLES_COMPUTE_TRACE=2`・`time-split.sh`、p016）。libGLESv2 の device の copy の使い回しで QEMU の GPU の call 835 → 212 ms（CPU 23 ms、p017）。
- 証拠は 5330 の i915 passthrough（QEMU の guest に渡した実機の GPU）と QEMU の Venus。素の 5330 での確認は未実施。

## 制限・移管

- L2（GPU ≤ CPU / 3、p017）は未達のまま、ユーザーの「今のまま」で止めた。L3（p012 素の 5330 の G1 の残りと回復の手順、p018 10 倍）は着手せず。
- 規約の全文と回帰（p013）は WS の外の一括の見直しへ。
- ローカルの LLM は F-061。subgroup の操作・image load/store・atomic counter は範囲の外。
- 試験（`tests/`）は [plan/tools.md](../tools.md)・`plan/tools/hw5330/`・`plan/tools/hwsv7/`・WS068 の手引きから使われる。plan/tools/ へ移す案（Q1 の判断）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws101-p001 | 設計（design.md） | cleared（2026-09-30） |
| ws101-p002 | compiler の核（GLCompute・built-in・SSBO の atomic ほか） | cleared（2026-09-30、host） |
| ws101-p003 | 実行器の object と記録（vkCreateComputePipelines・vkCmdDispatch） | cleared（2026-09-30、host） |
| ws101-p004 | 実行器の batch（`vk/vk-compute.c`） | cleared（2026-09-30、host） |
| ws101-p005 | 実機の bring-up と試験の場面 vkcs | cleared（2026-09-30、5330 の passthrough で vkcs 9/9） |
| ws101-p006 | SLM・barrier・fence・SLM の atomic | cleared（2026-09-30、vkcs 15/15） |
| ws101-p007 | vkCmdDispatchIndirect と G1 の受け入れ | cleared（2026-09-30、vkcs 21/21） |
| ws101-p008 | GLSL ES 3.10 の compute | cleared（2026-09-30、host） |
| ws101-p009 | libglesv2・libegl の ES 3.1 の compute の API | cleared（2026-09-30、Venus） |
| ws101-p010 | GLES の compute の i915 での G2 | cleared（2026-09-30、5330 の passthrough） |
| ws101-p011 | L1: Noct の toolchain の差分と G3 の見本 | cleared（2026-09-30、5330 の passthrough） |
| ws101-p012 | L3: 素の 5330 の G1 の残り・G3 の時間・回復の手順 | planned のまま WS を閉じた（ユーザー「今のまま」で保留） |
| ws101-p013 | 規約の全文との照合と回帰 | in-progress のまま WS を閉じた（規約の全文は WS の外の一括の見直しへ） |
| ws101-p014 | L1: G3 が動く | p011 の中で満たした（Phase の directory なし） |
| ws101-p015 | L1: デモの場面 S13 | cleared（2026-09-30、5330 の passthrough） |
| ws101-p016 | L2: 時間の分解 | cleared（2026-09-30、QEMU の Venus だけ） |
| ws101-p017 | L2: 3 倍 | uncleared のまま WS を閉じた（QEMU 835 → 212 ms、ユーザー「今のまま」） |
| ws101-p018 | L3: 10 倍 | 着手せず（Phase の directory なし） |

## 記録の所在

Phase の計画・結果（`phaseNNN/`）と Noct の差分の置き場（`p011-toolchain/`、main に当てた 189c3bf1）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws101/` にある。Queue の履歴は [plan/history](../history/index.md)。
