<!-- awesome-plan project=zedbsd record=ws213 -->

# WS213: compositor の thread 化（入力・cursor と描画の分離）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成、ユーザーの承認）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: P4（2026-10-12 ユーザー「コンポジタのマルチスレッド化も入れてください。」、p001 の設計から）
Resume point: p001 の調査と設計（今の main loop の仕事の内訳、thread の分け方、状態の写しと lock、Vulkan の queue と command buffer の持ち方、cursor の plane（WS141 p016 の段 B）との関係、Pi の計測）。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「あ、コンポジターのメインループがシングルスレッドなんですね。でも描画完了は待たないならあまり関係ないのかなあ。いやでも、CPU描画が1つでもあればイベント処理が一瞬止まりますね。どうしても残るCPU描画はスレッドにするしかなさそうです。」「コンポジタの時計などの描画最適化と、スレッド化 … 必要な分はWSを立て、現在のWSで取り組む分はこのままお願いします。」描画の最適化は WS211、thread 化はこの WS。

## 目標

compositor（userland/desktop/wayland/）の main loop の CPU の仕事（描画の命令の記録、文字の絵の作成、ぼかしの準備など）の間も、入力の処理と cursor の更新が止まらない。Pi 4 で、重いコマの間も cursor と入力の反応が 60fps。

## 範囲（p001 で決める）

- 入力・cursor の thread と描画の thread の分け方（Wayland の protocol の処理をどちらに置くか、状態の写しの単位と lock）。
- cursor の位置の更新を GPU の列から外す設計（WS141 p016 の段 B、第 4 版が review 待ち）との組み合わせ。
- 描画の thread の Vulkan の使い方（queue・command buffer・fence の持ち方、libvulkan の thread の安全）。
- Linux・FreeBSD の backend（libkeiland-backend の Linux/FreeBSD、WS131 等）への影響。
- 試験: QEMU（T1）、5330、Pi 4（入力→shown の計測、重いコマの間の cursor）。

## 完了の条件（仮）

Pi 4 で App Home の開閉・drag・app の起動の間も、cursor が 60fps で動き入力が遅れない（実機の log とユーザーの確認）。i915・Venus で回帰が無い（T1）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調査と設計 | planned |
| p002 | 実装 | planned |
| p003 | 規約の全文の見直し | planned |
