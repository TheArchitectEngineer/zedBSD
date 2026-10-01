<!-- awesome-plan project=zedbsd record=q518 -->

# Queue

Status: finished
Active Queue: なし
Last finished Queue: q518

## q518

- Purpose: compositor の GPU の buffer の境界を引き上げる
- Timebox: 60 分。必須回帰がこれを超える Phase は手順の所要時間に従い最大 150 分。未知の問題の調査は 30 分で止め、結果と再開条件を記録する。
- Focus: WS104 の完了。WS105 は後続の context。
- Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
- Exact approved scope: [ws104-p004](../ws104/phase004/phase.md) 全範囲（開始前 snapshot SHA256 `b5ed7709e4fd06c3f1e79587ae466224fa9b9a6a82559f6f14efea1b1abb0cd3`）。snapshot は local outbox に保存。
- Executor: Codex Q1、既存 executor finished、並行実行なしを確認。
- Applicable rules: AGENTS.md、guardrail.md、coding-style.md 全文、ws104/commands.md。
- Prerequisites: p001 cleared と実装の存在を確認。scope / criteria の後続取消なし。
- Started UTC: 2026-10-01T02:34:59.246799+00:00

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q518-i01 | [ws104-p004](../ws104/phase004/phase.md) | cleared | p001（context） | WS104 の既存依存順 |

Dependency graph: p001 (context) → q518-i01/ws104-p004。次の Phase は別 Queue。

## Upcoming Work Outlook

WS104 の未 cleared Phase（依存順）、その後 WS105。新規範囲の承認は含まない。GitHub へは未公開。

## Outcome

ws104-p004 **cleared**。GPU の wire layout・zedBSD GPU protocol/global・import を OS module に閉じ、共通 import は Vulkan image / memory の adopt だけにした。共通 layout、旧 factory、vulkan_external include、直接 vkGetFenceFdKHR は 0。device proc pointer で fence を呼ぶ。amd64 disk-image build exit 0、自前 warning 0。GPU v1 check（52 source）、dedicated host 18 cases × ordinary/sanitize、decode host 17 cases × ordinary/sanitize、forge guest、600 fence（全て generation 1）と 600 frames、C1/C2/C9 13/13、boot 全て PASS。p072 の全 6 PNG を目視し Wiseview・drag・復元・desktop 移動を確認。login PNG も目視・提示済み。証拠: `plan/history/ws104/q518/`。clang-format 19.1.7 と新 module / common import の style-check を実施（0）。全文規約の最終確認は p008。実機・Linux は未実施。旧 protocol/log と通常 path を保持し、import 失敗時の所有権を module で明示した。未達条件なし。

検証時 checkout HEAD: `69f0f2c030becfa426fb98e2be9ad1c324488e4a`（WIP）。Finished UTC: 2026-10-01T03:10:03.009057+00:00。GitHub へは未公開。

### 証拠のラベルの訂正（2026-10-01）

q518 の実装 commit は `5d413c08051c50ef856dd81989f15d1989e81438`（WIP）。終了時の HEAD `69f0f2c030becfa426fb98e2be9ad1c324488e4a` は人間の screenshot 追加を含む検証時の checkout であり、WS104 の実装 commit として記したラベルを訂正した。q518 の cleared、試験結果、承認範囲は変わらない。
