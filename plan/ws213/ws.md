<!-- awesome-plan project=zedbsd record=ws213 -->

# WS213: compositor の thread 化（入力・cursor と描画の分離）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。2026-10-12 ユーザー「非常に快適です」（place23）、2026-10-13 ユーザー「いや、これ以上詰めなくていいです。」で最適化を終えた）
<!-- retained-after-completion --> phase001 は userland/desktop/wayland/ の source の comment（cursor-box.h・cursor-thread.h・draw-list.c・glyph-worker.c・input-thread.h・render-thread.h）が設計の出典として参照している。消すか残すかは Q1 の判断待ち。
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

compositor（userland/desktop/wayland/）の main loop の CPU の仕事（描画の命令の記録、文字の絵の作成、ぼかしの準備など）の間も、入力の処理と cursor の更新が止まらない。Pi 4 で、重いコマの間も cursor と入力の反応が 60 fps。

## ユーザーの決定

- 2026-10-12（由来）:「あ、コンポジターのメインループがシングルスレッドなんですね。でも描画完了は待たないならあまり関係ないのかなあ。いやでも、CPU描画が1つでもあればイベント処理が一瞬止まりますね。どうしても残るCPU描画はスレッドにするしかなさそうです。」「コンポジタの時計などの描画最適化と、スレッド化 … 必要な分はWSを立て、現在のWSで取り組む分はこのままお願いします。」（描画の最適化は WS211、thread 化はこの WS）
- 2026-10-12「コンポジタのマルチスレッド化も入れてください。」
- 2026-10-12（p001 の設計）描く物の一覧＋pull の形を選んだ。p005 は「進め方はおすすめでいいです」。
- 2026-10-12 place23（p004 の I・C・R と WS211 p008）で「非常に快適です」。
- 2026-10-13「SMP に進む」（p007 の Pi の K-D の読みを途中で止めた）。
- 2026-10-13「いや、これ以上詰めなくていいです。」（最適化の終了）

## 結果

- M0 の計測と C0（cursor の present を 1 refresh に 1 回）は main（41bd7a5a4）。
- S1 の display list の再生（p003、db29b07f5）、S2 の描画 thread・cursor の thread・入力の thread（p004、f63ea685d ほか）、S3 の作業 thread（p005、09e429a9a）、kernel の K-D の診断の計数（p007、45ae288d7）は main の履歴にある（2026-10-28 に `git merge-base --is-ancestor` で確かめた）。
- host 試験は各 Phase で PASS（TSan 0、build warning 0）。T1（i915・Venus・Linux の TSan の guest）と Pi の Phase ごとの判定は未実施のまま閉じた。Pi の評価はユーザーの目（上の原文）。

## 制限・移管

- 完了の条件の box_lag（evdev から cursor の受け箱までの遅れ）の p99 ≤ 16.7 ms は未確認。place23 で超過が見え、kernel 側（1 core の順番か user の写しの global な lock）と切り分けた所で止めた。
- kernel の K-1（sched の起床の持ち分）・K-2（metadata lock の入りの削減）は設計だけで終了。Pi の idle の context switch の多さは Future Work の F-097。
- p006 の規約の全文は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」、2026-10-27 の精査で一括の見直しに読み替え）。
- 回帰に残す試験（kd・lock・pin・list-replay の host 試験）は plan/tools/ へ移す（Q1 の OK の後、[tools.md](../tools.md) の登録も直す）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws213-p001 | 調査と設計（main loop の内訳、描く物の一覧と 2 段の pull、不変条件、Vulkan の持ち主、試験） | planning（第 6 版、review-5 で S2 条件付き可）のまま閉じた。実装は p002〜p005 |
| ws213-p002 | M0（区間・cursor の present の内訳）と C0（cursor の present の間引き） | in-progress のまま閉じた（main 41bd7a5a4、Pi・T1 の判定は未実施） |
| ws213-p003 | S1: display list を同じ thread で再生 | in-progress のまま閉じた（実装は main、host 試験 PASS） |
| ws213-p004 | S2: 描画 thread・cursor の thread・入力の thread | in-progress のまま閉じた（段 I・C・R は main、ユーザーの「非常に快適です」） |
| ws213-p005 | S3: 作業 thread（glyph・壁紙・大きな字） | in-progress のまま閉じた（実装は main、host 試験 PASS） |
| ws213-p007 | kernel の入力の遅れ: K-D の診断、K-1・K-2 の設計 | in-progress のまま閉じた（K-D は main、K-1・K-2 は設計だけ） |
| ws213-p006 | 規約の全文の見直し | WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・結果・試験は削除する（上の retained の phase001 は Q1 の判断の後）。git の commit `fb1f6afee` の `plan/ws213/` にある。Queue の履歴は [plan/history](../history/index.md)。
