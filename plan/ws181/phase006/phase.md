<!-- awesome-plan project=zedbsd record=ws181-p006 -->

# ws181-p006: 整列のメニューの大きな grid、App Home の時計と下寄せ、3 つの仮想 desktop（UAT 2026-10-07 の 3 回目）

Phase ID: `ws181-p006`
Parent: [WS181](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: T1-346 で ws181-guest.sh・p004-guest.sh PASS と Q1 が PNG を目視、T1-347 で p003-guest.sh PASS と App Home の 2 枚目（時計無し、icon が点の上に下寄せ、17 app なので 1 行））（旧: in-progress（2026-10-07 q844 P2: 実装・build・host の試験と host の PNG まで。QEMU は T1 へ））
Phase disposition: normal
Queue: q844（P2、Q1 の ACK 2026-10-07「(1)〜(3) で ACK、1 枚目は時計＋icon 2 行、2 枚目から 4 行、どの page も下寄せ」。(3) はユーザーの指示で 3 つの desktop と猫・鳥・ウサギに置き換え）

## 由来（ユーザー、2026-10-07、原文）

> アレンジメントの選択のポップアップは、幅を2倍、高さを5倍にします。重要な選択だという印象を持たせるためです。1行目に、水平分割、垂直分割。2行目に、左1列＋右垂直分割、右1列＋左垂直分割、3行目に上1行＋下水平分割、下1行＋上水平分割、4行目にタイル。ホーム画面は、アイコンを画面の下に寄せます。ホーム画面はスワイプで左右にページ送りできます。1枚目には時計を置きます。これは、重心を下にすることで、上から順にアプリを並べただけのメニュー画面という印象を壊し、時計のあるホーム画面、ここが何もしないときの居場所なんだというユーザの安心感を作るためです。仮想デスクトップの円は、右と下が切れて正円になっていないように見えます。また、選択された仮想デスクトップも正円にして、色で区別しましょう。

> 仮想デスクトップの円は、何番目、という覚え方が得意な人にはそれでいいのですが、それが苦手な人もいる気がしました。かといって、この位置に色のついた円を配置すると、Appleのウィンドウのボタンと似てしまい、役割が違うことで混乱するとも思いました。そこで、真ん中の仮想デスクトップを基本にして左に行くか、右に行くか、という操作にして、3つの仮想デスクトップにしようと思います。（クリック: 表示は「3 つの形を左・中・右に、今いる所を明るく」、起動・login の時は「真ん中」から）

> また、仮想デスクトップのアイコンは、猫、鳥、ウサギにします。真ん中が鳥です。シルエットのアイコンで、選択されると、アクセントカラーになります。

## 実装（2026-10-07）

1. 整列のメニュー（`arrange-shell.c`）: 536×260（ws181-p005 の 268×52 の幅 2 倍・高さ 5 倍）、2 列 × 4 行の cell（258×56、間 8）、行 1: 左右・上下、行 2: 左 1 列＋右の縦分割（left-main）・右 1 列＋左の縦分割（right-main）、行 3: 上 1 行＋下の横分割（top-main）・下 1 行＋上の横分割（bottom-main）、行 4: タイル（2 列の幅の 1 つ）。絵は 60×40（arrange.c の枠を 5 分の 1 に）、文字無し。key は左右で 1 つ、上下で 1 行。`arrange.c`・`arrange.h` に top-main・bottom-main（上限 4、1 つなら全面、主は上（下）半分、他は残りの半分に横並び）、番号をメニューの順に（log の名前は今までどおり、新しい 2 つは top-main・bottom-main）。
2. App Home（`home.c`）: icon の grid を画面の下（下から 72 px、page の点の上）に寄せる。1 枚目 = 時計＋icon 2 行（12 個）、2 枚目から 4 行（24 個）、page の数と選択の page はこれで数える（`home_page_of`・`home_page_first`）。時計（`home_draw_clock`）: 時:分 を 64 px（`glass.c` の新しい大きさ `SIZE_CLOCK`、atlas は空白・数字・: だけ、font は今の Mahora）、その下に長い日付を 24 px、1 枚目と一緒に横へ動く、検索の時は出さない。page 送りは今の横の drag・wheel・PageUp/PageDown・選択の移動。
3. 仮想 desktop（`kwl.h`・`main.c`・`shell.c`・`icons.c`・`icons.h`）: 4 → 3（`KWL_APPS_DESKTOPS` 3）、session は真ん中（`KWL_DESKTOP_START` 1、log では desktop=2）から。pill の印は猫（左）・鳥（真ん中）・ウサギ（右）のシルエット（`GLASS_ICON_DESKTOP_CAT`・`BIRD`・`RABBIT`、icons.c の自作の部品: 点・三角・箱・線）、今いる desktop は accent の色、他は淡い灰。最初の (3) の「正円の点」（右と下の欠けは solid の形の quad が box ちょうどのため）は置き換えで使わない。
4. 試験の追従: `plan/ws181/tests/ws181-guest.sh`（desktop=1 → 2、右へ運ぶのは 3）、`plan/ws142/tests/p003-guest.sh`（真ん中から右 → 3、左 → 2 → 1、端で止まる）、`plan/ws142/tests/p004-guest.sh`（2・3）、`tests/scenarios/desktop/windows/arrange.md`、`plan/ws181/tests/host-arrange.c`（top-main・bottom-main）。

## 確かめ（2026-10-07）

- build: zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`）と Linux の Keiland warning 0。
- host: `run-host-arrange.sh` 1213/0、`run-host-edge.sh` 29/0、`run-host-layout-state.sh` 43/0。`plan/ws128/tests/icon-dump.c` で全部の icon が PASS（3 つの動物は枠に触れない）。
- host の PNG: `sh plan/ws181/tests/p005-host.sh` → `build/review/ws181-p006.png`（Home の 1 枚目、メニュー、pill の 3 つの状態）、`build/review/ws181-p006-animals-raw.png`（96 px と 20 px の動物）。renderer の近似で compositor の撮影ではない。
- QEMU（T1-346、2026-10-07、Q1 の判定）: `ws181-guest.sh` と ws142 の `p004-guest.sh` は PASS（見た目も期待どおり）。ws142 の `p003-guest.sh` は
  `scroll-no-gesture (KWL GESTURE : 79, expected 78)` の 1 行が FAIL（2 回とも）。原因は試験の期待の古さ: 増えた 1 行は scroll の指を離した時の
  `KWL GESTURE kind=swipe2 phase=end`（ws142-p009、2026-10-06 の BUG-224 で入った「一度の swipe を一歩に」の印、T1 の log.txt の 127 行目）で、
  edge の gesture の begin は増えていない。3 つの desktop の変更とは無関係。試験を「begin の行が増えない」と「swipe2 の end が 1 つ増える」の 2 つの
  確かめに直した（2026-10-07 P2）。再試験は T1 へ。
- App Home の 2 枚目（4 行の下寄せ）は T1-346 の pen の image に app が 9 つしか無く 1 枚目に収まって撮れていない。再試験は app の多い AAT の image
  （`tests/` の AAT の build）で、App Home を右へ 1 page 送った画面を撮る。

## 残り

- 削除の依頼（Q1）: `plan/ws181/phase006/held-pill-round-dots.patch`（正円の点の案、ユーザーの置き換えで不要）、`plan/ws035/tests/zdesktop-p065.sh`・`zdesktop-p072.sh`（WS035 は完了、Master の一覧に無く、p065 は p005 で無くなったメニューからの desktop の切り替えと 4 つの desktop を前提にする。試験の整理の基準で直さず削除）。
- docs/ の desktop の数の記述（あれば）は Q1 へ。
