<!-- awesome-plan project=zedbsd record=ws153 -->

# WS153: Settings の Apps の頁と third-party の app の repository（package の仕組み）

<!-- awesome-plan-current:start -->
Status: incomplete・保留（ユーザーが検討中）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: WS153 U2〜U15 は「ユーザーが検討中（質問しない、決まるまで WS153 の p002 以降は止める）」（decisions-log、Master の未決））。旧: incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した。旧: planning）。残り・制限: U2〜U15 の決定の後に p002 以降
Primary Milestone: MG007
Related Milestones: MG006、MG002
Parent: [Master](../master.md)
Queue: なし（担当と時期は未定）
Resume point: p001（package の仕組みの検討）から。
<!-- awesome-plan-current:end -->

## 単一目標

Settings に Apps の頁を足し、third-party の app の repository から app を探す・入れる・更新する・消すことができるようにする。

## ユーザーの指示（2026-10-04 夜、原文）

「SettingsにAppsタブを追加するWSを作ってください。パッケージシステムの検討が最初のPhaseです。パッケージシステムはuserland/packagesではなくて、サードパーティーアプリのリポジトリのことです。」

## 注意（範囲の区別）

ここでの package の仕組みは、**third-party の app の repository**（利用者が後から入れる app の配布）。tree の `userland/packages/`（zedBSD の build で外部の source を cross build して image に入れる仕組み、WS032）とは別物。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws153-p001](phase001/phase.md) | package の仕組み（third-party の app の repository）の検討 | in-progress（2026-10-18 Q1: phase.md に合わせた。旧: planning） | — |
| ws153-p002 以降 | p001 の結論で決める（repository の client・install の仕組み・Settings の Apps の頁・試験） | planning | p001 |
