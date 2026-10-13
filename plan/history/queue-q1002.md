<!-- awesome-plan project=zedbsd record=q1002 -->

# q1002 / WS074 Acid3 得点の right/auto 幅

Status: finished（2026-10-13 22:39〜22:45 JST、最大90分）
Approval: このチャットで [p178 の exact scope](../ws074/phase178/next-queue-proposal.md)を示した質問へユーザーが「自動で承認します」と回答。今回の q1002-i01 として捕捉した。

q1002-i01 / [ws074-p178](../ws074/phase178/phase.md): **部分 scope cleared**。幅0の containing block で auto 幅・right0 の絶対配置子を縮める際、min-content 下限が欠けて幅0になる一般的な欠陥を `layout/position.c` で修正。最小 fixture の host-position は25/25 pass、host build warning/error0。固定 WPT の Acid3 score100/100・inventory100/100・例外0、RGB差 **43486/480000**（q1001の56647から13161減）、Acid2差 **1865/120000**（不変）。参照得点の x=625→231.80、右端x=625。得点 ROI x200..625/y210..350 の差は9572→800。[report](../ws074/phase178/evidence/20261013-q1002/report.json)、Acid3/Acid2 並列画像、test/reference layout/paint。`git diff --check` pass、C全文・component 規約の新規違反なし。別OS/QEMU・main現行HEAD全体の再buildは未実施。

p178 の1領域 pixel 完全一致は未達で **uncleared**。B1適性評価はユーザーの単独指示で現行scope対象外。q1000-i01 の uncleared は保存。p100/WS074 は未達。残り Acid3 差43486、Acid2差1865。次の差領域は新しい exact scope/Queue の選定が必要。独立 branch の成果は main 作業 tree に投影してコミットはメインセッションとの調整待ち。GitHub Issue/Project publication と push は保留。`master.md` は未変更。
