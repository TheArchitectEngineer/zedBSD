<!-- awesome-plan project=zedbsd record=ws181-p001 -->
# ws181-p001: 設計 — 窓の状態の機械、App Home の独立のモード、画面の端の gesture、整列のメニューと整列モード

Parent: [WS181](../ws.md)
Status: in-progress（2026-10-07 q842-i01 P2: 設計の第 1 版と review の結果まで。指摘の反映は未、利用の上限でラップアップ）
Disposition: normal
Queue: q842 / q842-i01

## 範囲

コードは書かない。[design.md](design.md) に、ユーザーの UAT（2026-10-07、ws.md の原文）の 6 つの目標の挙動を、今の compositor の実装
（layout.c・shell.c・home.c・touch.c）に対する変更として設計する。実装は p002（状態）・p003（App Home と gesture）・p004（整列）。

## 受け入れ

- 6 つの目標のそれぞれに、規則・状態・出来事の表・log・試験（host と QEMU）がある。
- 今の不具合（docked の窓が後ろに残る、click で floating になる）の原因と、それを無くす不変条件がある。
- 人の判断の点は既定の案を添えて Q1 に送った（design.md §7）。
- design-reviewer の review を受け、指摘を反映した（design.md §9）。

## 記録

- 2026-10-07 P2: 第 1 版を書いた。読んだ物: layout.c・layout.h、shell.c（dock・undock・layout_*・bar_press・pull・Wiseview の端・描画の層）、home.c（layer・button・motion）、touch.c の冒頭、objects.c の消滅、ws142-p007、tests/scenarios/desktop/windows/layout-mode-switch.md。
- 2026-10-07 P2: design-reviewer の review を受けた（blocking 4・should-fix 12・minor 9、要約は design.md §9）。Q1 の指示（利用の上限）でラップアップ。

## 再開の情報（2026-10-07 P2 のラップアップ）

- どこまで: design.md の §0〜§8 は第 1 版として書き終わっている。design-reviewer の review は**済み**で、その指摘は §9 に要約して残した。**本文への反映はまだ**。
- 次の 1 手: §9 の B1〜B4 を本文に反映する（§1.4 の `dock_owner` は tick で観測する形にし、`layout_leave` で全部を消す。§3 の上端の帯は `kwl_glass_button` の先頭近くに置く）。続けて S1〜S12・M1〜M9 を反映し、版を第 2 版にする。その後 design-reviewer にもう一度通すかは、反映の量で決める（blocking を直したので、通すのが既定）。
- Q1 への判断の待ち: §7 の D1〜D7。review を受けて、D3（touchpad も 下 → Home・上 → Wiseview に揃えるか）と D5（pill のどこを tap してもメニューを開くか、今の desktop の絵だけか）は、既定を置かずユーザーに聞く方がよい。D2 の移動は、連れて行く操作では docked のまま連れて行く形に直す（S7）。新しく聞く点: 上端の帯を mouse にも効かせるか（S8）、整列モードの印と、閉じた時に詰め直すか（S6）、Home の上に bar を残すか（S9）。
- ws142 の試験と記録（host-layout.c・p010-guest.sh・ws142-p007・p008 の決定）を変えてよいかの Q1 の許可（S10）。
