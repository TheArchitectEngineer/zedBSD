# WS031 実行 Queue

<!-- awesome-plan-current:start -->
Status: active
Workspace: ws031
Goal: WS031 完了（実機ネイティブで vkdemo 描画・scanout、ホスト非依存）
Authorization: current user 「WSの完了をゴールにして、実行をお願いします。」（2026-09-14）
<!-- awesome-plan-current:end -->

WS031 を p001→p012 の順に実行する。各 Phase = 1 モジュールを、確定インタフェース（前段 Phase doc / external-design §4）を満たすように実装し、host fixture と build で検証する。実装の重い Phase は下位モデルへ委譲する。

## 実行順と現況

| Order | Phase | Module | Status |
| --- | --- | --- | --- |
| 1 | ws031-p001 | 設計固め・header 契約枠 | cleared |
| 2 | ws031-p002 | top+cmd | in-progress |
| 3 | ws031-p003 | res | planning |
| 4 | ws031-p004 | spirv | planning |
| 5 | ws031-p005 | eu | planning |
| 6 | ws031-p006 | compile | planning |
| 7 | ws031-p007 | pipe | planning |
| 8 | ws031-p008 | cmdbuf | planning |
| 9 | ws031-p009 | sync | planning |
| 10 | ws031-p010 | wsi | planning |
| 11 | ws031-p011 | 統合（増分A→B→C）・実機描画 | planning |
| 12 | ws031-p012 | レビュー | planning |

## 運用

- 設計変更が必要と判明した Phase は **uncleared** にして停止し、関連 Phase / WS の設計 doc を修正（新 Phase は作らず、変更点を一言記す）してから再実行する。
- Phase clear ごとに、確定インタフェースと結果を該当 `phaseNNN/results.md` と ws.md に記録し、後段はそれを正本に参照する。
- 検証: host fixture（通常＋ASan/UBSan）、build 3 構成（i915 / i915+vk / GPU なし）warning 0、`git diff --check`。実機描画は p011、目視はユーザー。

## 境界・停止条件

- HAL/hal.h・UAPI 変更が必要になったら停止して提示。
- WS029 core hook（3D 有効化・入口結線）は該当 Phase 計画で差分提示し承認後に最小変更。
- Mesa 転記が MIT 以外に触れたら停止。
- host reboot・package 導入・cmdline 変更は不可。git add/commit/push・GitHub 同期はユーザー。
