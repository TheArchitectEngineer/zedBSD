<!-- awesome-plan project=zedbsd record=ws213 -->

# WS213: compositor の thread 化（入力・cursor と描画の分離）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した。旧: planning（2026-10-12 Q1 作成、ユーザーの承認））
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: P4（2026-10-12 ユーザー「コンポジタのマルチスレッド化も入れてください。」、p001 の設計から）
Resume point: p004（R・C・I の thread）は段 I・C・R を実装済み（agent/p4 f63ea685d、host 試験 4 本 PASS（TSan 0）・build warning 0、[phase004/phase.md](phase004/phase.md)）、T1（i915・Venus・Linux の TSan の guest）と Pi（6.4 の場面）の確かめ待ち。p003 は T1-547 と Pi place17 の結果で Q1 が判定。p002 は Pi・T1 の確認待ち。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「あ、コンポジターのメインループがシングルスレッドなんですね。でも描画完了は待たないならあまり関係ないのかなあ。いやでも、CPU描画が1つでもあればイベント処理が一瞬止まりますね。どうしても残るCPU描画はスレッドにするしかなさそうです。」「コンポジタの時計などの描画最適化と、スレッド化 … 必要な分はWSを立て、現在のWSで取り組む分はこのままお願いします。」描画の最適化は WS211、thread 化はこの WS。

## 目標

compositor（userland/desktop/wayland/）の main loop の CPU の仕事（描画の命令の記録、文字の絵の作成、ぼかしの準備など）の間も、入力の処理と cursor の更新が止まらない。Pi 4 で、重いコマの間も cursor と入力の反応が 60fps。

## 範囲（p001 で決める）

- 入力・cursor の thread と描画の thread の分け方（Wayland の protocol の処理をどちらに置くか、状態の写しの単位と lock）。pointer の経路（evdev・加速・位置）は入力の thread、client への通知は main（2026-10-12 Q1、review-4 H-B）。
- cursor の位置の更新を GPU の列から外す設計（WS141 p016 の段 B、第 4 版が review 待ち）との組み合わせ。
- 描画の thread の Vulkan の使い方（queue・command buffer・fence の持ち方、libvulkan の thread の安全）。
- Linux・FreeBSD の backend（libkeiland-backend の Linux/FreeBSD、WS131 等）への影響。
- 試験: QEMU（T1）、5330、Pi 4（入力→shown の計測、重いコマの間の cursor）。

## 完了の条件（仮）

Pi 4 で App Home の開閉・drag・app の起動の間も、cursor が 60fps で動き入力が遅れない（実機の log とユーザーの確認）。i915・Venus で回帰が無い（T1）。

- 依存（p001 review-1 C1、review-2 Low、WS141 review-B4 M-f）: 画面に映る cursor の 60fps は WS141 p016 (B) の B-u（位置だけの present が libvulkan の present の worker と GPU を通らない）と p016 の受け入れ B の前提（(b)・A3）が入ってから判定する。B-u の L1（display_mutex の待ち）は前提ではなく、p016 の計測の後の次の版で扱う。
- 受け入れの分担（WS141 review-B4「WS213 との関係」、Q1）: WS141 p016 (B) は「main loop が止まらない時の入力→ioctl」、この WS は端から端（evdev の入力 → 画面に映る cursor）の 60fps。C と R が同じ VkQueue を同時に使うことの libvulkan の安全は p016 B-u が明記する前提。
- この WS が単独で保証するのは（review-2 H-e、review-3 M-3、review-4 H-B）: pointer が動いている間、evdev の時刻から cursor の受け箱に書かれるまでの遅れ（box_lag）の p99 ≤ 1 refresh（16.7ms、App Home の開閉・drag の間も、必達）と最長 ≤ 16.7ms（原因が kernel の長い transaction なら WS208・kernel への依存、review-5 N-4）、main の 1 pass の最長 ≤ 33.3ms（app の起動は p99 を記録）、walk の外の main の Vulkan の最長 ≤ 16.7ms（Pi、p004）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（main loop の内訳と Pi の実測、描く物の一覧＋2 段の pull、不変条件、Vulkan の持ち主、cursor の thread・入力の thread と WS141 の依存、試験） | planning（第 6 版、review-5 で S2 条件付き可、Q1 の確認とユーザーの review 待ち） |
| [p002](phase002/phase.md) | M0（main loop の区間・cursor の present の内訳・event_us）と C0（cursor の present を 1 refresh に 1 回） | in-progress（main 41bd7a5a4 まで、Pi・T1 の確認待ち） |
| [p003](phase003/phase.md) | S1: display list（同じ thread で再生、等価の試験、walk の費用を測る） | in-progress（実装済み、T1・Pi の確かめ待ち） |
| [p004](phase004/phase.md) | S2: 描画 thread・cursor の thread・入力の thread（2 段の pull、park、R→M の通り道） | in-progress（段 I・C・R 実装済み。place23 の box_lag の超過は kernel 側（1 core の順番か user の写しの global な lock）と切り分け、kernel の設計 K-D・K-1・K-2 を Q1 の判断待ち。T1・Pi の確かめ待ち） |
| [p005](phase005/phase.md) | S3: 作業 thread（glyph・壁紙・大きな字） | in-progress（実装済み agent/p4 09e429a9a、host 試験 PASS、T1・Pi の確かめ待ち） |
| [p006](phase006/phase.md) | 規約の全文の見直し | planned |
| [p007](phase007/phase.md) | kernel の入力の遅れ: K-D（起床・横取りの後の待ちと metadata lock の待ち・持ちの診断、`kern.perf=1`）と K-1（sched の起床の持ち分）・K-2（metadata lock の入りの削減と受け渡し）の設計 | in-progress（K-D 実装済み agent/p9 45ae288d7、K-1・K-2 は設計だけで design-reviewer 待ち、Pi は BUG-294 の焼き直しの後） |
