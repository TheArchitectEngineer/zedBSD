<!-- awesome-plan project=zedbsd record=q1000 -->

# q1000 / WS074 Acid3 lane

Status: finished（2026-10-13、q1000-i01 uncleared）
Owner: この Codex セッション。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの P1/P2/P3 Queue と source 所有を分離する。
Approval: 2026-10-13 このチャットのユーザー「p178のAcid3描画試行」→「Acid3のすべてのテストでピクセル単位の100%一致になることをゴールに、作業してください。」→「このセッションでQueueも管理する」。最終目標は [p100](phase100/phase.md)。本 Queue の有限範囲は [p178](phase178/phase.md) の最初の1領域の試行。
Timebox: 2026-10-13 22:21〜25:21 JST の最大3時間。実際の終了は 22:26 JST。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1000-i01 | [ws074-p178](phase178/phase.md) | 固定 WPT と現行 source の基準を作る。削除禁止に合う runner を WS074 内で用意し、Acid3 score・RGB差・Acid2・例外を測る。差を最大3領域まで分類し、原因が局所的な1領域を修正・再計測する。複数 module の設計変更・参照/比較条件の変更は対象外 | uncleared（参照画像の箱が欠け、差が画面全体に及ぶ。局所修正を実施せず） | p099・p172 cleared。WPT 取得と worktree を準備 |

Dependency graph: p099 → p178、p172 → p178、p178 の実測結果 → 次の p100 有限 Queue。p100 の whole clearance は q1000 の局所成果だけでは成立しない。

Upcoming Work Outlook: 最初の候補は[p179](phase179/phase.md)の参照背景画像。verified 出力の後に p178 の局所差を再選定する。全画面で差画素0、得点100/100、fail/Uncaught/crash/timeout 0、および必要な回帰・全文規約を満たすまで p100 を cleared にしない。次の Queue はこの Queue の終了時に自動開始しない。

Coordination: q1000 は既存の q924〜q939 と衝突しない番号として 2026-10-13 に選んだ。共有 `plan/queue.md` への投影と main 統合はこの session が行う。GitHub Issue/Project publication は設定に従い保留。

Outcome: 固定 WPT の取得と現行 host build は成功。Acid3 score 100/100、RGB差 302056/480000、Acid2差 1865/120000。[p178の証拠・停止理由](phase178/phase.md)を参照。背景画像の取得/paint と Acid2 の差を次の選定候補とする。p178/p100/WS074 は未達。次 Queue は未選定。
