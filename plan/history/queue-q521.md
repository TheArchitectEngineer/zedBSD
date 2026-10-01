<!-- awesome-plan project=zedbsd record=q521 -->

# Queue

Status: finished
Active Queue: なし
Last finished Queue: q521

## q521

- Purpose: install の path を `userland/desktop/paths.h` の macro に
- Timebox: 60 分。必須回帰がこれを超える Phase は手順の所要時間に従い最大 150 分。未知の問題の調査は 30 分で止め、結果と再開条件を記録する。
- Focus: WS104 の完了。WS105 は後続の context。
- Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
- Exact approved scope: [ws104-p007](../ws104/phase007/phase.md) 全範囲（開始前 snapshot SHA256 `84841406333e4e2239898fc78d1f14fa77814fb904590783244577f7e8c01eb1`）。snapshot は local outbox に保存。
- Executor: Codex Q1、既存 executor finished、並行実行なしを確認。
- Applicable rules: AGENTS.md、guardrail.md、coding-style.md 全文、ws104/commands.md。
- Prerequisites: p003・p006 cleared と実装の存在を確認。scope / criteria の後続取消なし。
- Started UTC: 2026-10-01T04:14:21.528421+00:00

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q521-i01 | [ws104-p007](../ws104/phase007/phase.md) | cleared | p003・p006（context） | WS104 の既存依存順 |

Dependency graph: p003・p006 (context) → q521-i01/ws104-p007。次の Phase は別 Queue。

## Upcoming Work Outlook

WS104 の未 cleared Phase（依存順）、その後 WS105。新規範囲の承認は含まない。GitHub へは未公開。

## Outcome

ws104-p007 **cleared**。cleared（q521）。paths.h の BINDIR / LIBEXECDIR / DATADIR / SYSCONFDIR で desktop の install path を指定し、zedBSD の値を保持。26 file の 61 行と include、host script 7 本の -I. を準備済み patch で更新（p006 後の main.c の include hunk のみ手順どおり調整）。旧 install literal の残り 0。amd64 build exit 0、自前 warning 0。bin/* と dynamic/*.so の該当 path 文字列は前後一致。host 7 本 exit 0、desktop は前後 PASS、Textedit 34/34、Files default・thumbnail PASS。paths.h style-check 0、C1/C2/C9 13/13、boot PASS。p072 全 6 PNG と login PNG を目視し login は提示済み。証拠: plan/history/ws104/q521/（boundary-checks.json、strings-before.txt、strings-after.txt、host-summary.txt、criteria-results.txt、login.png）。system shell / 公開 emoji header / Open With の bare directory / OS path / sessiond は合意どおり保持。実装 cec34d3e（WIP）、実行者 main / Codex Q1、未達条件なし。実機・Linux 未実施、GitHub 未公開。

実装 commit: `cec34d3e1871e30290e471562b0d6c9c25da3f20`（WIP）。検証時 checkout HEAD: `cec34d3e1871e30290e471562b0d6c9c25da3f20`。Finished UTC: 2026-10-01T04:42:00.543038+00:00。GitHub へは未公開。
