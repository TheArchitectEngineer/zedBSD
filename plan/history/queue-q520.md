<!-- awesome-plan project=zedbsd record=q520 -->

# Queue

Status: finished
Active Queue: なし
Last finished Queue: q520

## q520

- Purpose: compositor の session と OS の hook を zedBSD の module に
- Timebox: 60 分。必須回帰がこれを超える Phase は手順の所要時間に従い最大 150 分。未知の問題の調査は 30 分で止め、結果と再開条件を記録する。
- Focus: WS104 の完了。WS105 は後続の context。
- Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
- Exact approved scope: [ws104-p006](ws104/q520/phase.md) 全範囲（開始前 snapshot SHA256 `651222400cd1fab3f99bd20053cf2adcfdb676976bea067a08e79fdc498db1a5`）。snapshot は local outbox に保存。
- Executor: Codex Q1、既存 executor finished、並行実行なしを確認。
- Applicable rules: AGENTS.md、guardrail.md、coding-style.md 全文、ws104/commands.md。
- Prerequisites: p005 cleared と実装の存在を確認。scope / criteria の後続取消なし。
- Started UTC: 2026-10-01T03:44:22.362063+00:00

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q520-i01 | [ws104-p006](ws104/q520/phase.md) | cleared | p005（context） | WS104 の既存依存順 |

Dependency graph: p005 (context) → q520-i01/ws104-p006。次の Phase は別 Queue。

## Upcoming Work Outlook

WS104 の未 cleared Phase（依存順）、その後 WS105。新規範囲の承認は含まない。GitHub へは未公開。

## Outcome

ws104-p006 **cleared**。cleared（q520）。handoff.c を zedbsd/handoff-zedbsd.c へ移動（root からの include path 以外 byte 同一）、zwl-os.h の 7 hook と zedBSD の空実装を追加。main の open/close・poll と compose の display acquire/release が境界を使う。成功した acquire は swapchain / targets の失敗と通常 close で release、未 acquire の failure / prepare-only は release しない。amd64 disk-image exit 0、自前 warning 0。新 OS module/header の style-check 0。C1/C2/C9 13/13 と boot PASS、p072 の全 6 PNG と login PNG を目視し login は提示済み。READY/GO、RELEASED/LOGOUT、wire と既存 log の形式を保持。証拠: `plan/history/ws104/q520/`（boundary-checks.json、criteria-results.txt、login.png）。物理実機・Linux は未実施。WS 全体の全文規約の最終確認は p008。実装 3bfe50b9（WIP）、実行者 main / Codex Q1、未達条件なし。

実装 commit: `3bfe50b946aac0d3bcd8a3ff60b0b1bf5cd23906`（WIP）。検証時 checkout HEAD: `3bfe50b946aac0d3bcd8a3ff60b0b1bf5cd23906`。Finished UTC: 2026-10-01T04:14:16.734002+00:00。GitHub へは未公開。

Archived exact approved Phase: [scope](ws104/q520/scope.md), SHA256 `651222400cd1fab3f99bd20053cf2adcfdb676976bea067a08e79fdc498db1a5`. Terminal evidence: [Phase](ws104/q520/phase.md). Archive relocation preserves the recorded attempt outcome.
