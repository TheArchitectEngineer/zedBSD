<!-- awesome-plan project=zedbsd record=ws099-p030 -->
# ws099-p030: タイトルバーの検索欄・menu の項目のドラッグで窓を動かす

Status: in-progress（q670 / q670-i01、P2、2026-10-04。実装・build 済み、QEMU は T1 に依頼する）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: q670（[Queue](../../queue.md)）

## 範囲（2026-10-04 ユーザー、Q1 と決めた仕様）

ユーザー:「検索バーの横幅が大きくて、ウィンドウを移動するときにタイトルバーをつかめる領域が小さいです。…テキスト領域もウィンドウ移動のドラッグ領域にできませんか？同様にメニュー項目もドラッグ領域にしたいです。通常の操作はマウスボタンのリリース時に、マウス押下位置とリリース位置の距離が、クリックならゼロ、タッチなら極めて小さいときに、クリックとして処理、距離があればウィンドウドラッグ。」「検索バーでドラッグする操作は、検索バーでクリックしてキャレットを設定すればできることに。」

- 検索欄（フォーカスが無い時）と menu の項目: 押した時は位置を覚えるだけ。閾値（mouse・pen 2 px、touch 8 px）に達したら窓の move（押した点が pointer の下に留まる）。docked の窓（system bar の中）は題名と同じ pull。動かずに離したらクリック: 検索欄はフォーカスと押した位置のキャレット、menu は開く（離した時に開く）。
- フォーカスの有る欄: 押した所にキャレット、ドラッグで選択（今まで pointer でのキャレット・選択は無かったので新設）。
- 閉じる・最小化等のボタン、他の control（戻る・パンくず・segment・tab 等）は今のまま。menu が開いている間（menu mode）の操作も今のまま。
- source は compositor（`userland/desktop/wayland/`）だけ。

## 実装

- `shell.c`: `zwl_glass_press_moved`（閾値の判定、`server->shell_source` が touch なら 8 px）、`zwl_glass_press_move`（floating は `server->drag` を押した点基準で、docked は `server->pull`）。`glass_motion_take` で `zwl_menu_motion` の次に `zwl_titlebar_motion`。log `ZWL GLASS press move surface=S x= y=`・`ZWL GLASS press pull surface=S`。
- `menu-shell.c`: menu が閉じている時の top-level 項目（titlebar の「…」を含む）の press を `shell_menu.waiting` に保留。`zwl_menu_motion` で閾値を越えたら move（log `ZWL MENU press moves client= surface= item= docked=`）。release で押した点の項目が同じなら `shell_open`。左 button が離れていれば保留を捨てる、menu が開いたら保留を捨てる、窓が消えたら `zwl_menu_forget` で捨てる。
- `titlebar-shell.c`: 欄の hit に文字の始まりの x（`text_x`）。フォーカスの無い検索欄の press は `waiting`、閾値で move（log `ZWL TITLEBAR press moves`）、離したら `shell_act` でフォーカスし、押した位置にキャレット（log `ZWL TITLEBAR caret … cursor=N`）。フォーカスの有る欄の press は `selecting`（キャレットを押した所、motion で cursor、release で log `ZWL TITLEBAR select … anchor=A cursor=C`）。位置から文字の境界は `shell_field_at`（前の文字列の幅で一番近い境界、表示の幅を越えたら打ち切り）。

## 確かめ

- build: `make ZEDBSD_CONFIG=plan/ws099/tests/config-amd64-criteria.mk BUILD=build/ws099-p030/img build/ws099-p030/img/bin/wayland` exit 0・warning 0。`make keiland-linux KEILAND_LINUX_BUILD=build/p2-keiland-linux` exit 0・warning 0。FreeBSD は未実施。
- `plan/tools/style-check.py`（titlebar-shell.c・menu-shell.c・shell.c）: 新しい違反 0（titlebar-shell.c の 1 件は変更前からの行）。
- QEMU: 未実施。T1 に依頼（下）。

## QEMU の試験（T1 に依頼、2026-10-04）

- `plan/ws099/tests/p030-drag.sh`（pen の image: `plan/ws079/tests/build-pen-image.sh`、guest は `plan/ws079/tests/pen-guest.sh start`）。マウスと touch の両方で: A. terminal の menu の Edit — 押しただけでは開かず離して開く、1 px の揺れはクリック、マウスの drag で窓が (60,80) ちょうど動き menu は開かない、touch の tap（3 px の揺れも）で開く、指の drag で (80,60) 動く、docked の system bar の Edit を下へ drag で pull（undock via=pull）。B. titlebar-probe の検索欄 — クリックでフォーカスとキャレット、フォーカス中の drag で選択（anchor=0 cursor=6、窓は動かない）、Z で置換、クリックでキャレット、フォーカスの無い欄の drag で (50,40) 動きフォーカスしない、touch の tap でフォーカス・指の drag で選択、指の drag で (70,50) 動く、閉じる button は押した時（release の前に GLASS close）。zdesktop の ERROR 0。
- 回帰（挙動の変わる所の周り）: `plan/tools/titlebar/titlebar-p010.sh`（検索欄のクリック・入力）、`plan/tools/titlebar/menu-p003.sh`（menu のクリック）、`plan/tools/titlebar/menu-occlude.sh`、`plan/ws079/tests/zdesktop-p013-touch.sh`（title bar の touch）。
