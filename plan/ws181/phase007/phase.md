<!-- awesome-plan project=zedbsd record=ws181-p007 -->

# ws181-p007: 整列のメニューの本当の glass と、広がって透けていく開き方（UAT 2026-10-07 の 4 回目）

Phase ID: `ws181-p007`
Parent: [WS181](../ws.md)
Status: in-progress（2026-10-07 q845 P2: 実装・build・host の PNG まで。QEMU は T1 へ）
Phase disposition: normal
Queue: q845（P2、Q1 の ACK 2026-10-07「ws181-p007 の範囲 1〜5 で ACK」）

## 由来（ユーザー、2026-10-07、原文）

> アレンジメントは完璧ですね。整列方法の選択ポップアップは、glassを適用してほしいのと、広がって大きくなるようなアニメーションとともに透明度が高くなる演出で表示してほしいです。

Q1 の補足: T1-346 の c10-arrange-menu.png ではメニューがほぼ不透明の白に見える → 窓の panel と同じ本当の glass（後ろの desktop が blur で透ける、frosted の設定にも従う）。開く時は pill の所の小さい大きさから広がり、同時に透明度が上がって最後の glass に（150〜200 ms、ease-out）。閉じる時は逆か短い fade。操作は animation の途中でも受ける。

## 原因（白く見えた理由）

- glass の白が 0.62（窓の panel は 0.34）で、shader の white lift（白い glass は luma 0.85 まで白くする、ws099-p005）と合わさって淡い壁紙ではほぼ白になる。
- メニューの glass は blur した**壁紙だけ**を見ていた（backdrop は popup の前に reset される）。後ろの窓（T1-346 の緑・黄の窓）が透けない。

## 実装（2026-10-07）

1. `arrange-shell.c`: glass を窓の panel と同じ白 0.34・rim 0.70・flat（`ARRANGE_MENU_WHITE`・`ARRANGE_MENU_RIM`）。window.frosted を切った時（`panels_opaque`、BUG-214）は白 1.0。
2. `shell.c`（`kwl_glass_draw`、8 行）: メニューが出ている間（開く・閉じるの animation も、`kwl_arrange_showing`）だけ、`kwl_arrange_draw` の前に既存の `draw_backdrop`（壁紙・desktop の icon・窓を 1/8 で blur、keyboard_blur と同じ経路）を呼び、後で `kwl_backdrop_reset`。frosted が off と Home の上では呼ばない。
3. 開く: 180 ms（`ARRANGE_MENU_OPEN_MS`）、ease-out cubic。pill の真ん中・bar の中から pill の幅の倍率（最小 0.15）で、全体（影・glass・光る cell・絵）を一様に拡大して本来の場所へ。白 0.92 → 0.34、全体の opacity 0.5 → 1。広がり終わった最初の frame で log `KWL ARRANGE menu settled ms=N`。
4. 閉じる: 120 ms（`ARRANGE_MENU_CLOSE_MS`）の fade（opacity → 0、上辺の中央を軸に倍率 → 0.96）、閉じた時の広がりの程度から。入力は閉じた時点で受けない。開いている間の当たり判定は animation の途中でも全体の矩形のまま（変えていない）。
5. 試験の追従: `plan/ws181/tests/ws181-guest.sh` の C10 で click の直後に `c10-menu-opening`、0.3 秒後に `menu-settled` の確かめと `c10-arrange-menu`。`tests/scenarios/desktop/windows/arrange.md` の手順 1・2 を今のメニュー（7 つの形、2 列、glass、開き方）に。
6. host の PNG: `plan/ws181/tests/p007-host.sh`（`p007-host.py`、p005-host.py と ws099-p034 の host の renderer の上）→ `build/review/ws181-p007.png`: 前（p006）と、45・90・135・180 ms。後ろに T1-346 と同じく緑・黄の窓を置き、blur で透けるのを見る。

## 確かめ（2026-10-07）

- build: zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`）と Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux`）warning 0。
- host: `run-host-arrange.sh` 1213/0（arrange.c は変えていない）。host の PNG は renderer の近似で compositor の撮影ではない。
- QEMU: 未（T1 へ: ws181-guest.sh の全部、`c10-menu-opening.png` と `c10-arrange-menu.png` を人が見る）。

## 残り

- QEMU の撮影の判定（T1、Q1）。実機（5330）の見た目はユーザーの UAT。
