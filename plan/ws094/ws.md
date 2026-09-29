<!-- awesome-plan project=zedbsd record=ws094 -->

# WS094: desktop の file の icon

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: main の依頼（worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`）
Resume point: p001（設計、[design.md](design.md)）cleared。次は p002（compositor の desktop surface。WS035 の source なので main の許可と順が要る）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「デスクトップにファイルアイコンの表示。」

- `~/Desktop` の file と folder を desktop（壁紙の上）に icon と名前で並べる。double click で開く（WS093 の対応）、選択・移動（配置の保存）・
  右 click の menu・drag and drop（Files との間）、file の増減の追従。compositor（WS035）と Files（WS071）のどちらが描くかは p001 で決める。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws094-p001](phase001/phase.md) | 設計（[design.md](design.md)、J1〜J7 は既定: Files が背景の層の client として描く） | cleared（2026-09-29） | — |
| ws094-p002 | compositor: `keiland_desktop_v1`（role・token・configure）、重ね順・合成・入力・focus・popup・DnD の対象、`--session` での起動と起こし直し、probe | planned | p001、main の許可（WS035 の source） |
| ws094-p003 | libkeiland の client の API と Files の `--desktop` の骨組み（`~/Desktop` の icon を右上から描く、監視） | planned | p002、main の許可（libkeiland） |
| ws094-p004 | 選択・開く（WS093）・keyboard・配置の保存と Clean Up | planned | p003 |
| ws094-p005 | context menu（項目・空いた所）・名前の変更・Trash・Copy・Paste・New Folder・Show in Files | planned | p004 |
| ws094-p006 | drag（desktop の中・folder へ・Files の窓との DnD）と touch | planned | p005 |
| ws094-p007 | 全文の規約と回帰 | planned | p006 |
