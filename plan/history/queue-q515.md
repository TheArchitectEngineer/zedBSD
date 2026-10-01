<!-- awesome-plan project=zedbsd record=q515 -->

# q515 history（finished 2026-10-01）

<!-- awesome-plan-current:start -->
Status: finished（2026-10-01）
Active Queue: なし
Last finished Queue: [q515](queue-q515.md)（ws104-p001 cleared）
<!-- awesome-plan-current:end -->

## q515（finished）

- Purpose: desktop の公開ヘッダーを libc から分離し、WS104 と WS105 の開始条件を整える。
- Timebox: 実行開始から 60 分。完了を保証する時間ではない。超過時は結果・残り・再開条件を記録する。
- Focus: WS104 → WS105（2026-10-01 current user「WS104とWS105が、次のあなたの目標です」）。
- Approval: current user, 2026-10-01「では、Queueを実行してください。」。直前に提案した q515（ws104-p001 のみ）を承認・実行指示。
- Existing decisions: current user, 2026-10-01「許可します」。`p001-sysroot.patch` の適用と WS105 D25 の Linux guest 起動確認を許可。この技術判断の許可と Queue の実行承認は別。
- Exact approved scope: [ws104-p001](ws104/q515/phase.md) の全範囲。desktop 公開ヘッダーを `include/libc/` から `userland/desktop/keiland/` へ移動し、準備済みの `p001-sysroot.patch` と `p001-paths.patch` を適用する。Phase の必須確認、結果の記録、関連記録の更新と `git commit -m WIP` を含む。
- Scope boundary: ヘッダーの内容・公開 API・振る舞いの変更、HAL の変更、新しい Linux backend、承認済み差分以外の toolchain の変更は含まない。push・GitHub 公開は行わない。
- Executor: main（Q1）。
- Applicable rules: [AGENTS.md](../../AGENTS.md)、[Guardrail](../guardrail.md)、[C の全文規約](../coding-style.md)、[検証コマンド](../tools/keiland-linux/zedbsd-commands.md)。
- Proposal basis: commit `ac5453cca370bda7d9e2d75edb956eb3e9cb30ef`。
- Phase snapshot SHA256: `139a83ef53562983ab117fc813ae523e9196a03b935e446bdda1d8d9e13b1846`。
- sysroot patch SHA256: `a3789928f89b882696048c31022ad3a4b88dbb4e4cca99f9998b0d74bda13c96`。
- paths patch SHA256: `50f999011e7fb05af3a0b08d080438d92adee50a651b08accadd1d546c90f979`。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q515-i01 | [ws104-p001](ws104/q515/phase.md) | cleared | Phase の依存なし。sysroot 差分は許可済み | WS104 p002・p004 と WS105 p002 の前提を作る |

Dependency graph: `q515-i01/ws104-p001 -> ws104-p002, ws104-p004, ws105-p002 (context; p002 は ws104-p003 も必要)`。外部ノードはこの Queue の実行対象ではない。

## 達成基準と不確実性

- amd64 の disk-image build が成功し、プロジェクト自身の source の warning が 0。
- sysroot を実際に作り直した証拠があり、`usr/include` のファイル名・内容の hash が前後で同一。
- Phase 指定範囲に移動前のヘッダー path の参照が残らない。
- `boot-test.sh` が PASS。login prompt の PNG をユーザーに示す。
- host 試験 4 本（textedit core、Files build、Settings build、audio）が PASS。
- sysroot の更新による外部 package の再 build が所要時間の主な不確実性。準備済み patch が現行 tree に当たるかは、実行開始時に再確認する。未知の依存・範囲外の修正が必要なら uncleared とし、証拠と再開条件を残す。

## Upcoming Work Outlook

| Candidate | Readiness | Why next / dependency |
| --- | --- | --- |
| [ws105-p001](../ws105/phase001/phase.md) | 選定可能。Phase の依存なし、D25 許可済み | Debian 13 の試験 guest と SSH・QMP の操作道具。q515 の代替候補、または後続 Queue の候補 |
| [ws104-p002](ws104/q516/phase.md) | 選定可能。ws104-p001 cleared | Settings の audiod socket の直接参照を libkeiland の API へ |
| [ws104-p004](ws104/q518/phase.md) | 選定可能。ws104-p001 cleared | compositor の GPU buffer の境界を OS module 内に移す |

Outlook は実行許可ではない。Queue は 1 Phase。GitHub には未公開。

Execution started (UTC): `2026-10-01T01:54:33.003327+00:00`。開始時に clean tree、既存 executor 不在、patch 2 本の `git apply --check` 成功を確認。

## Outcome（2026-10-01）

q515-i01 / ws104-p001: **cleared**。amd64 build（自前 warning 0）、sysroot の 241 file 同一、旧 path 0、host 4 本、boot test PASS。実装 `12d7efeea05917a0d12c50a93824a6b6dc990c59`。60 分の timebox 内（約 6.0 分）。証拠・未実施・制限は [Phase の結果](ws104/q515/phase.md#結果)。WS104 は incomplete、p002〜p008 が残る。新しい Queue は未選定。GitHub へは未公開。

Archived exact approved Phase: [scope](ws104/q515/scope.md), SHA256 `139a83ef53562983ab117fc813ae523e9196a03b935e446bdda1d8d9e13b1846`. Terminal evidence: [Phase](ws104/q515/phase.md). Archive relocation preserves the recorded attempt outcome.
