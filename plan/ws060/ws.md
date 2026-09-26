<!-- awesome-plan project=zedbsd record=ws060 -->

# WS060: UFS の journal の commit を batch にして名前の操作を速くする（BUG-040）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG004
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: q440・q441（p002・p003）、規約は ws063-p002（2026-09-27）
Resume point: —（完了）
<!-- awesome-plan-current:end -->

## 目標

[BUG-040](../bugs/BUG-040.md): journal の volume で名前の操作が 1 回約 100 ms（操作ごとに device 全体の flush）。group commit にして、journal の volume の 200 の作成を journal 無しの数倍以内にし、続けた操作の間も guest が応答する。

受け入れ: BUG-040 の受け入れ案。crash の試験（途中で切っても volume が UFS OK で prefix が保たれる）、`check-volume.py`、sh・make の差分試験、boot。規約。

## 結果（2026-09-27、completed）

| 項目 | 結果 |
| --- | --- |
| 方式（v3） | metadata の書き込みを buffer cache に pin して走っている transaction に集め、約 1 秒ごと（flusher の hook）・`fsync`/`sync`・unmount・枠が満ちたときに commit: 2 つの slot の交互に中身と descriptor を書き、flush、commit の記録、flush、unpin。mount で最新の未適用の commit を replay。journal の file と locator は WS063 で `.ufs-journal`・`ZJ3L`・`ZJ3R` に |
| BUG-040 | 200 の空 file の作成: v2 の journal 20.6 秒 → v3 0.364 秒（journal 無し 0.361 秒） |
| crash | ws060-p003 で 4 時点と root、ws063-p002 で書き直しの後に再び 4 時点・root で UFS OK |
| 回帰 | ws060-p003: boot、make の差分試験 91/91、SMP 6/6、COW、itimer、swaphog、`dir-grow.sh`、sh の差分試験 |
| 規約 | ws063-p002 で WS060 の変更（ufs.c の v3、buf.c の pin と flusher の hook、mount(8) の option）を全文で見直した |

host 試験（`plan/ws001/tests/` の journal の 2 本）は既存の理由で build できず未実施（ws060-p003）。QEMU だけ、実機は未実施。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws060-p001 | 操作ごとの flush の実測と group commit の設計 | canceled（q431-i01 で uncleared、撤回。設計は p002、実装と BUG-040 の受け入れは p003 が果たしたので置き換え） |
| ws060-p002 | batch の redo journal（v3）の設計 | cleared（q440-i01） |
| ws060-p003 | v3 の実装、BUG-040 の受け入れ、crash の試験 | cleared（q441-i01） |

Phase の記録は git の履歴にある（WS の完了で削除）。

## 制限・移管

WS063 の ws.md の「制限・移管」（v2 の tail の volume は v2 のまま、解放した block の集合の上限、枠が満ちたときの操作の途中の commit）。
