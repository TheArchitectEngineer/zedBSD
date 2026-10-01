<!-- awesome-plan project=zedbsd record=q517 -->

# Queue

Status: finished
Active Queue: なし
Last finished Queue: q517

## q517

- Purpose: libkeiland の OS の 3 file を `libkeiland/zedbsd/` へ
- Timebox: 60 分。必須回帰がこれを超える Phase は手順の所要時間に従い最大 150 分。未知の問題の調査は 30 分で止め、結果と再開条件を記録する。
- Focus: WS104 の完了。WS105 は後続の context。
- Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
- Exact approved scope: [ws104-p003](ws104/q517/phase.md) 全範囲（開始前 snapshot SHA256 `a0352aa3d9c90282ad2ce0c99791b14114269f0003252c2175230d772bf660d0`）。snapshot は local outbox に保存。
- Executor: Codex Q1、既存 executor finished、並行実行なしを確認。
- Applicable rules: AGENTS.md、guardrail.md、coding-style.md 全文、ws104/commands.md。
- Prerequisites: p002 cleared と実装の存在を確認。scope / criteria の後続取消なし。
- Started UTC: 2026-10-01T02:25:22.606785+00:00

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q517-i01 | [ws104-p003](ws104/q517/phase.md) | cleared | p002（context） | WS104 の既存依存順 |

Dependency graph: p002 (context) → q517-i01/ws104-p003。次の Phase は別 Queue。

## Upcoming Work Outlook

WS104 の未 cleared Phase（依存順）、その後 WS105。新規範囲の承認は含まない。GitHub へは未公開。

## Outcome

ws104-p003 **cleared**。libkeiland の network・network-link・audio を `zedbsd/*-zedbsd.c` に byte 同一で移動（移動前 hash と照合）。Makefile と host script 2 本の path を更新。共通 C の OS 専用 include は 0。公開 export は前後一致。amd64 build exit 0、自前 warning 0、host audio 14/14、Settings host build、Settings guest 回帰 8 本、boot 全て PASS。移動した 3 file の style-check 0。証拠: `plan/history/ws104/q517/`（hash、export 一覧、回帰 summary、login PNG）。旧 object は共有 build に残し、消していない。実機・Linux は未実施。

実装 commit: `78da13872bbef7d62572fadab616ab78043d13f6`（WIP）。Finished UTC: 2026-10-01T02:34:59.058808+00:00。GitHub へは未公開。

Archived exact approved Phase: [scope](ws104/q517/scope.md), SHA256 `a0352aa3d9c90282ad2ce0c99791b14114269f0003252c2175230d772bf660d0`. Terminal evidence: [Phase](ws104/q517/phase.md). Archive relocation preserves the recorded attempt outcome.
