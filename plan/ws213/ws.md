<!-- awesome-plan project=zedbsd record=ws213 -->

# WS213: compositor の thread 化（入力・cursor と描画の分離）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成、ユーザーの承認）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: P4（2026-10-12 ユーザー「コンポジタのマルチスレッド化も入れてください。」、p001 の設計から）
Resume point: p001 の設計の第 3 版（[phase001/phase.md](phase001/phase.md)、review-2 と Q1 の判断（2 段の pull ほか）を反映）が再 review 待ち。p002（M0・C0）は Pi・T1 の確認待ち。p003 の着手は Q1 が WS211 の P3 と順を決めてから。
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

- 依存（p001 review-1 C1、review-2 Low）: 画面に映る cursor の 60fps は WS141 p016 (B) の B-u（位置だけの present が libvulkan の present の worker と GPU を通らない）と、その L1（display_mutex の待ち）・p016 の受け入れ B の前提（(b)・A3）が入ってから判定する。
- この WS が単独で保証するのは（review-2 H-e）、main の止まりが描画 thread の libvulkan の 1 transaction 以内であること（M0 の `KWL PERF main` の区間と `present_max`、目安 Pi で 1 refresh 以下）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（main loop の内訳と Pi の実測、描く物の一覧＋2 段の pull、不変条件、Vulkan の持ち主、cursor と WS141 の依存、試験） | planning（第 3 版、再 review 待ち） |
| [p002](phase002/phase.md) | M0（main loop の区間・cursor の present の内訳・event_us）と C0（cursor の present を 1 refresh に 1 回） | in-progress（main 41bd7a5a4 まで、Pi・T1 の確認待ち） |
| [p003](phase003/phase.md) | S1: display list（同じ thread で再生、等価の試験、walk の費用を測る） | planned（着手は Q1 が WS211 の P3 と順を決めてから） |
| [p004](phase004/phase.md) | S2: 描画 thread（2 段の pull、park、R→M の通り道） | planned |
| [p005](phase005/phase.md) | S3: 作業 thread（glyph・壁紙・大きな字） | planned |
| [p006](phase006/phase.md) | 規約の全文の見直し | planned |
