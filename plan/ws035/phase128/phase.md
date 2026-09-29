<!-- awesome-plan project=zedbsd record=ws035p128 -->

# ws035-p128: 窓の四隅（と辺）で resize する

Phase ID: `ws035-p128`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て、ユーザーの要望「ウィンドウの四隅がリサイズ領域になっていないようです。ウィンドウの四隅でリサイズ
可能にしてほしいです。」）

## 範囲

四隅（左上・右上・左下・右下）で、両方の辺を同時に動かす resize（`xdg_toplevel.resize_edge` の TOP_LEFT 等）が始まり、cursor も斜めの形になること。
compositor が描く縁（decoration・浮いたタイトルバー）の hit の判定と、client が自分の縁の press から `xdg_toplevel.resize` を頼む経路の両方を調べる。

## 調べて分かったこと（どこで四隅が抜けていたか）

1. **compositor（glass の shell、`userland/desktop/wayland/shell.c`）には、窓の縁の resize の判定がそもそも無かった。** `window_hit()` は
   title bar（`HIT_TITLE`）と body（`HIT_BODY`）だけで、その外は `HIT_NONE`（desktop の press）。四隅だけでなく**辺も** resize できなかった。
   zdesktop は全ての窓に server-side の装飾を答える（`decoration.c`）ので、client の側に縁は無い。
2. **client の経路**: `xdg_toplevel.resize` を送る client は試験の `popup-probe` だけ。Files・Terminal・Notes・PDF Viewer・libkeiland には
   move・resize の request が無い（`grep`）。client の側には直す所が無い（装飾は server-side）。
3. **protocol の受け側**（`toplevel.c` の `toplevel_resize`）は四隅（edges 5・6・9・10）を正しく受けて追う（p076 の試験が右下の角を使う）。
   ただし resize の後の「反対の辺を止める anchor」が、Files のように configure を受けた時点で ack して後から描く client で早く外れる不具合があった
   （下の 3）。左上・右上・左下の角では反対の角が 20 px ずれた。
4. cursor の形（`cursor.c`）: 斜めの矢印（`IMAGE_NWSE`・`IMAGE_NESW`）の絵は既にあるが、client が `wp_cursor_shape` で頼んだときだけ使われていた。

設計の文書（[compositing-design.md](../compositing-design.md) の「装飾の領域（題名の帯＝移動、枠と角＝resize、button）は zdesktop が処理」）の
とおり、compositor の側に縁と角を足した。辺も要る（設計どおり、今は辺も無い）ので、辺と角の両方を同じ判定で足した。

## 実装（2026-09-29）

- `shell.c`: 浮いた窓の外周（title bar の上端から body の下端まで、title bar と body の幅）の外側 `FRAME_BAND` = 8 px の帯を frame にした
  （`HIT_FRAME`、`frame_edges()`）。帯の上・下で左右の端から `FRAME_CORNER` = 24 px の中、または帯の左・右で上下の端から 24 px の中は、その角の
  2 辺（斜め）。docked・fullscreen・animation の窓、toplevel でない surface、画の無い窓には frame が無い。
  - press: `zwl_glass_button()` で body の次に frame を見て、`zwl_toplevel_resize_start()`（下）で resize を始める。system bar・左右の端の
    desktop の swipe・下端の Wiseview の gesture・Home・network・menu・title bar の controls はこれまでどおり先に press を取る。
  - hover: `zwl_glass_motion()` が、glass の shell が motion を取らなかったとき（client に motion が行くとき）に pointer の下の frame の edges を
    `zwl_cursor_frame()` に渡す。system bar・左右の swipe の帯・下端の Wiseview の帯の上では矢印を出さない（そこの press は gesture のもの）。
  - `ZWL GLASS launch` の log に `size=WxH` を足した（試験が窓の外周を知るため）。
- `toplevel.c`・`toplevel.h`: resize の開始を `zwl_toplevel_resize_start(server, surface, edges)`（公開）に切り出し、`xdg_toplevel.resize` の
  request と glass の frame の両方から使う。edge の bit を `ZWL_EDGE_TOP`・`BOTTOM`・`LEFT`・`RIGHT`（toplevel.h）に。
  - **anchor の早い解除の修正**: resize の終わりの後、client が最後の configure を ack した後の最初の commit で anchor を外していたが、Files は
    configure を受けた時点で ack し、前の大きさで描いた frame（Venus で 1 枚約 330 ms、3 枚）を後から commit する。その commit で anchor が外れ、
    最後の大きさの画が来たときに左・上の辺の resize では反対の辺が動いた。最後の大きさ以外の画で anchor を外すのは、resize の終わりから
    `RESIZE_STALE_MS` = 3 秒の後に（terminal のように自分の大きさ（cell の倍数）を描く client のため。それまでは anchor が反対の辺を止め続ける
    だけで害は無い）。最後の大きさの画が来たらすぐ外すのは今までどおり。
- `cursor.c`・`extras.h`・`zwl.h`: `server->frame_edges` と `zwl_cursor_frame(server, edges)`（形は角で `NW`・`NE`・`SW`・`SE`、辺で `N`・`S`・`W`・`E`）。
  `zwl_cursor_image()` は frame の edges を client の shape より先に見る。`compose.c` は frame の矢印を client の cursor surface より先に描く。
  変わるときだけ `ZWL CURSOR frame edges=N` を log。
- `seat.c`: glass の窓が pointer を取らない状態（glass でない・fullscreen・popup の grab・lock・drag and drop）では frame の cursor を消す。

## 検証（amd64、Venus の guest、worktree の graphical の login の image `build/p128.img`、2026-09-29）

- 新しい試験 `plan/ws035/tests/zdesktop-p128.sh`（`VENUS_SIZE=1920x1280`、デモの解像度）: **PASS**。kei の session で App Home から Files を開き
  （1120x720）、右下 → 左上 → 右上 → 左下の順に、角のすぐ外に pointer を置いて `ZWL CURSOR frame edges=10・5・9・6` を確かめて撮り、40 px 外へ drag:
  `ZWL RESIZE start edges=10・5・9・6`、settled の大きさはそれぞれ +40×+40、反対の角は動かない（1120x720 → 1160x760 → 1200x800 →
  1240x840 → 1280x880）。最後に右の辺（edges=8、幅だけ +40、高さはそのまま）。
  - 修正前の anchor では、同じ試験で左上・右上・左下の角の反対の角が 20 px 動いた（settled が中間の大きさで出た）。
- 回帰（1280x800、同じ image）: `zdesktop-p076.sh` PASS（`xdg_toplevel.resize` の右下の角と左の辺の clamp・anchor、move、popup、maximize 等）、
  `zdesktop-p059.sh` PASS（title bar の hover・move・dock・close）。
- boot test: `plan/tools/boot-test.sh build/p128.img` PASS。
- build: `plan/ws035/tests/build-login-image.sh build/amd64 graphical`、wayland の warning 0（clang・libcxx を含まない config、共有の toolchain の
  tree に触れていない: build の log に `llvm-source`・`libcxx`・`Permission denied` は無い）。
- 規約: `plan/tools/style-check.py` shell.c・toplevel.c・cursor.c・seat.c・compose.c は変更の前後とも 0。`git diff --check` 清浄。
- 画面（`build/ws035-shots/`）: `p128-20260929-hover-corners.png`（左上・右上・左下・右下・右の辺の cursor、pointer の周りを 2 倍）、
  `p128-20260929-opened.png`（開いた Files）、`p128-20260929-after-resizes.png`（4 つの角と右の辺の resize の後、1320x880）。
  全ての画は `build/p128-after/`（hover-*・after-*）。

未実施: 実機（i915、5330 の LCD）。touch・pen での角の resize（pen は pointer と同じ `zwl_seat_button_shell` を通るので同じ判定になる。
指の touch での試験はしていない）。Terminal・Notes・PDF Viewer・browser での drag（判定は compositor の側で client によらない。anchor の
3 秒は Files で確かめた）。

## 制限と残り

- 画面の下端 20 px（Wiseview の gesture）・左右の端 16 px（desktop の swipe）・system bar に掛かる frame は、そこでは resize にならない
  （gesture が先。矢印も出さない）。画面の下端まで伸びた窓の下の角は、その帯の上の側の帯（左右の帯の下の 24 px）から掴む。
- frame の帯は窓の外側 8 px（body の中には入れていない。client の scrollbar 等の press を奪わないため）。帯の幅を変えるならこの定数。

## Resume point

2026-09-29: cleared。次は ws.md の残りから人間の判断が要らないもの（p116 の一覧の 8: Venus で compositor の起動が約 6.5 秒、等）。
