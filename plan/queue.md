<!-- awesome-plan project=zedbsd record=queue -->

# Queue: なし（サブエージェントの運用）

<!-- awesome-plan-current:start -->
Status: なし（2026-09-27 11:52 から、ユーザーの指示でサブエージェントが worktree の branch で Phase を実行し、メインは計画と merge）
Active Queue: なし
Last finished Queue: [q495](history/queue-q495.md)（ws068-p024 cleared。GLES 3.0 の API（1））
Executor: 作業用のサブエージェント N=4（2026-09-28、使用 55%・リセットまで 1 時間 28 分で N=3 → 4。見込み約 20%/時 → リセット時 約 85%。T-30 に使用量を確かめ、T-15 に N=0 → 計画の整理）: WS075 i915（p006 の修正の確認と commit）、WS074 ブラウザ（p058）、Keiland（p110 F-050 → terminal の選択）、WS073（BUG-086 → BUG-088 → BUG-087）
<!-- awesome-plan-current:end -->

Upcoming Work Outlook: WS071 p007〜（File Manager）、WS035 p076〜p080 → p055・p057・p058（compositor）、WS073 p003〜（バグ）。salvage の片付けの後、枠が空けば WS068 p025〜（GLES 3.0 の API）→ p013（desktop GL）を作業用のサブエージェントへ。ACPI（WS049）と Arm64（WS044・WS048）はデスクトップが片付くかリミットが余るとき、WS001 はユーザーの指示のときだけ。
