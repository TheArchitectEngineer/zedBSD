<!-- awesome-plan project=zedbsd record=q519 -->

# Queue

Status: finished
Active Queue: なし
Last finished Queue: q519

## q519

- Purpose: compositor の入力の device の層を `wayland/zedbsd/input-zedbsd.c` へ
- Timebox: 60 分。必須回帰がこれを超える Phase は手順の所要時間に従い最大 150 分。未知の問題の調査は 30 分で止め、結果と再開条件を記録する。
- Focus: WS104 の完了。WS105 は後続の context。
- Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
- Exact approved scope: [ws104-p005](ws104/q519/phase.md) 全範囲（開始前 snapshot SHA256 `4dc5acff3f9fda18b09f79310667fa444da7e07a2f26dec1d1afa5814cc2dac5`）。snapshot は local outbox に保存。
- Executor: Codex Q1、既存 executor finished、並行実行なしを確認。
- Applicable rules: AGENTS.md、guardrail.md、coding-style.md 全文、ws104/commands.md。
- Prerequisites: p004 cleared と実装の存在を確認。scope / criteria の後続取消なし。
- Started UTC: 2026-10-01T03:10:03.273973+00:00

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q519-i01 | [ws104-p005](ws104/q519/phase.md) | cleared | p004（context） | WS104 の既存依存順 |

Dependency graph: p004 (context) → q519-i01/ws104-p005。次の Phase は別 Queue。

## Upcoming Work Outlook

WS104 の未 cleared Phase（依存順）、その後 WS105。新規範囲の承認は含まない。GitHub へは未公開。

## Outcome

ws104-p005 **cleared**。列挙・open・ioctl・read・close を `zedbsd/input-zedbsd.c` へ移し、共通の input.c は capability による分類と event の解釈にした。tablet / touch の軸・name・id は 0/errno の hook を使用。共通 device operation / direct close は 0、UAPI input include は zwl-evdev.h の 1 行。amd64 disk-image build exit 0、自前 warning 0。新 module/header style-check 0、Linux host の C89 header syntax check 0。C1/C2/C9 13/13、glass p059、Notes pen（筆圧・tilt・消しゴム・undo・hover・PDF 保存と qpdf）、touch p013（2 指 event、pointer fallback、cancel、edge gesture、2 指 flick、1 指 drag/tap）、boot 全て PASS。p072 全 6・glass 全 6・Notes 5・touch 主要 8 PNG を目視し、login PNG は提示済み。証拠: `plan/history/ws104/q519/`。ioctl / read の errno、EOF、EAGAIN、EINTR、torn-event EIO と log の形を保った。物理 hotplug は未実施（列挙・再走査は移動して中身を保持、専用試験では仮想 node の追加・指/pen event を確認）。実機・Linux compositor は未実施。未達条件なし。実行者: main / Codex Q1。

実装 commit: `96d14088b27df63a5a7779efcb7bbc811dd37bf0`（WIP）。Finished UTC: 2026-10-01T03:44:22.009214+00:00。GitHub へは未公開。

Archived exact approved Phase: [scope](ws104/q519/scope.md), SHA256 `4dc5acff3f9fda18b09f79310667fa444da7e07a2f26dec1d1afa5814cc2dac5`. Terminal evidence: [Phase](ws104/q519/phase.md). Archive relocation preserves the recorded attempt outcome.
