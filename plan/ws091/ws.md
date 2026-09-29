<!-- awesome-plan project=zedbsd record=ws091 -->

# WS091: 画像 viewer

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計、[design.md](design.md)）・p002（実装、`/bin/imageview`）cleared。次は p003（規約と回帰、main の指示の後）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「画像ビューアの追加。」

- PNG・JPEG・GIF（libpng-compat・libjpeg-compat・libgif-compat）の表示、拡大・縮小・fit・pan（touch の pinch と慣性、WS090 の部品）、
  同じ folder の前後の画像、Files からの起動（WS093）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws091-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| [ws091-p002](phase002/phase.md) | 実装: PNG・JPEG・GIF の表示、fit・拡大縮小・pan、touch の pinch と慣性、同じ folder の前後の画像、`/bin/imageview FILE`、App Home への登録（他の担当の file は main の許可の最小の差分） | cleared（2026-09-29。touch の guest と実機は未実施） | p001 |
| ws091-p003 | 全文の規約と回帰 | planning | p002 |
