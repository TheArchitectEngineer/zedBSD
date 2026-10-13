<!-- awesome-plan project=zedbsd record=q1003 -->

# q1003 / WS074 iframe 子の親ページ誤描画

Status: finished（2026-10-13 22:49〜22:54 JST、最大90分）
Approval: このチャットで [p180 の exact scope](../ws074/phase180/phase.md) を示した質問へユーザーが「q1003 の範囲を承認する」と回答。承認はこの q1003-i01 に限る。

q1003-i01 / [ws074-p180](../ws074/phase180/phase.md): **cleared**。iframe の子が `layout/box.c` から親ページ box として生成されていた。子の DOM と iframe box を残し、子 box の生成を止めた。`display:contents` でも子を親へ移さない。最小 host fixture は修正前 FAIL、修正後 PASS（0×0、80×40、display:contents、DOM 子要素）。実 frame load **52/52 pass**、host build warning/error0。固定 WPT の Acid3 score100/100・inventory100/100・例外0、RGB差 **42921/480000**（q1002の43486から565減、91.0581%一致）、Acid2差 **1865/120000**（不変）。iframe 内の `FAIL` 2 paint item 消失。[report](../ws074/phase180/evidence/20261013-q1003/report.json)、Acid3/Acid2 並列画像、修正前後 layout/paint。`git diff --check` pass。`clang-format` は環境に無く、C全文・component全文を手動確認。QEMU/別OSと main 最新 HEAD 全体の再buildは未実施。

本文 y373.50 / reference y350 の差は変わらない。iframe 子が押し下げるという仮説は棄却。form/inline line の高さが次候補。p100の全画面pixel差0、Acid2現行exact、WS074 acceptanceは未達。独立 branch の成果は main 作業 tree に投影してコミットはメインセッションとの調整待ち。GitHub Issue/Project publication と push は保留。`master.md` は未変更。次 Queue は未承認。
