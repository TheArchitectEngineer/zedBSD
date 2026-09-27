<!-- awesome-plan project=zedbsd record=ws035p084 -->

# ws035-p084: 窓の間の drag and drop（wl_data_device）と zdesktop-files の窓の外・パンくずへの drop

Phase ID: `ws035-p084`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 main の割り当て。サブエージェントが worktree の branch で実行）

## 範囲

2026-09-27 main:「a new WS035 phase: drag and drop between windows — wl_data_device start_drag / enter / motion / drop / dnd
actions in zdesktop (p079 cancels drags today), libwayland pieces, and zdesktop-files using it for drops outside its window and
onto the titlebar breadcrumb (the F-item from ws071-p010). One pass first.」

1. zdesktop: `wl_data_device.start_drag`（ボタンを押している間だけ。source が無い drag は同じ client の中だけ）、pointer の下の
   面（窓の本体、または窓の titlebar のパンくずの段）への `data_offer`・offer の type・`source_actions`・`enter`・`motion`・`leave`、
   `accept`・`set_actions` から action を選ぶ（Ctrl で copy、次に target の好み、次に copy・move・ask の順）、source に `target`・
   `action`、release で `drop`（accept と action があるとき）・source に `dnd_drop_performed`、`finish` で `dnd_finished`、
   それ以外は target に leave・source に `cancelled`。Esc で取り消し。icon の面を pointer に、無ければ zdesktop の小さな紙の印。
2. titlebar: `zed_titlebar_v1` version 2 の event `drop_target(id, detail)`（パンくずの段の上にある間、enter・motion の前に。
   id 0 で「どの段でもない」）。zdesktop は窓の本体の上の座標（負の y）で enter を送り、その段を青く光らせる。
3. libwayland・libzdesktop: `drop_target` の記述と dispatch、`zdesktop_titlebar_listener.drop_target`（`ZDESKTOP_VERSION` 7）、
   version 2 の bind。data device の client 側は p079 で揃っていた。
4. zdesktop-files: 窓の中の drag が窓の外へ出たら zdesktop の drag and drop に渡す（`text/uri-list`、copy・move）。drop を受ける
   側: folder の項目・sidebar の folder・他の tab・titlebar のパンくずの段・表示中の folder（他の窓からのとき）が target。
   drop で名前を読み（自分の drag なら選択から）、move（別の device なら copy）か copy の task、`finish`。

## 設計の判断（戻せる既定）

- パンくずの段への drop は `zed_titlebar_v1` の event で段を教え、drag の target はその窓の surface にする（data device の
  semantics はそのまま、座標は本体の上の負の y）。titlebar の control ごとの別の data の口は作らない。
- 窓の中の drag（ws071-p010）は今までどおり窓の中で処理し、pointer が窓の外へ出た時点で Wayland の drag にする（zdesktop は
  押している間も pointer を focus の窓へ送るので、外の座標で分かる）。戻ってきた drop は自分の drag（`self=1`）として選択を使う。
- move の意味: target が file を動かす（source は消さない）。Finder・Nautilus と同じ。
- 受ける側は file の名前（`text/uri-list`）だけ。tag・ゴミ箱・Favorites への他の窓からの drop は無し（下の残り）。

## 実装（2026-09-27）

- zdesktop: `data.c`（上の 1、`zwl_data_drag_motion`・`_release`・`_cancel`、object の破棄で drag の後始末）、`seat.c`（drag の間の
  motion・button・wheel・Esc）、`objects.c`（offer・device・surface・titlebar の破棄も data.c へ）、`compose.c`・`compose.h`（icon と
  印、icon の buffer の保持）、`shell.c`（`zwl_glass_body_at`、`zwl_glass_draw_drag_badge`、drag の間は `still` でない）、`damage.c`、
  `titlebar.c`（`zwl_titlebar_send_drop_target`、manager の version 2）、`titlebar-shell.c`（`zwl_titlebar_drop_at`、段の光）、`zwl.h`。
- libwayland: `titlebar-protocol.c`（event と dispatch、titlebar の proxy を manager の version で作る。前は 1 固定で、version 2 の
  event で client が切れた）、`zed-titlebar-v1-client-protocol.h`。libzdesktop `titlebar.c`、`include/libc/zdesktop.h`、
  `titlebar-probe`（listener に NULL）。
- zdesktop-files: `dnd.c`（新規）、`window.c`・`window.h`（manager の bind、`fm_window_push` を公開）、`titlebar.c`（drop_target を
  窓の入力の列へ）、`ui-drag.c`（`fm_drop_event`・`fm_drop_accepts`・`fm_drop_perform`、窓の外へ出たら `FM_REQUEST_DRAG_OUT`、
  表示中の folder への drop は content の縁を光らせる）、`ui.c`、`files.h`、`main.c`（drag out・drop・答え）、`Makefile`。

## 検証（amd64 だけ、Venus の guest、lean image `plan/tools/files/build-files-image.sh`、2026-09-27）

- `plan/ws035/tests/zdesktop-p084.sh` PASS: 2 つの zdesktop-files（A: Desktop、B: Documents）を titlebar で左右へ。A の Logo.png を
  B の content へ（drag start・enter client=2・accept text/uri-list・drop・`DROP operation=move ... destination=/tmp/fhome/Documents`・
  finish・A の `DRAG out done dropped=1`、file が移る）。B の Logo.png を A の titlebar の「…」（Home）へ（`drop_target client=1
  id=4 detail=0`、home へ move）。A の Screenshot.png を A 自身のパンくずへ（`self=1`、home へ move）。B から壁紙の上で離す
  （`drag cancel`、`dropped=0`、何も動かない）。ERROR 無し。
- 回帰 PASS: files-p010（窓の中の DnD）・files-p014（CONTROLS）・files-p018（bounds）、zdesktop-p079（clipboard）・p076（popup・
  toplevel）、titlebar-p008（protocol と model）・titlebar-p010、menu-p003（`GUEST_RUNTIME=build/ws071-run`）。
- 規約: 新しい `dnd.c` の style-check 0、変えた file は前と同じ 0。build warning 0（変えた component）。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p084-20260927-venus-two.png`、`-over.png`（B の上: zdesktop の印と B の content の
  青い縁）、`-moved.png`、`-crumb.png`（A のパンくずの「…」が青く光る）、`-self.png`。
- 実機（i915）: 未実施。boot test: 2026-09-27 のユーザーの指示で無し。

## 残り

- drag の icon: zdesktop-files は icon の面を渡さない（zdesktop の印だけ）。項目の絵の icon は wl_shm の面が要る。
- 他の窓からの drop を tag・ゴミ箱・Favorites で受けること、他の app（terminal へ path の text、`text/plain`）との drop。
- drop の座標の popup（ask の action）、drag の中の自動の scroll と spring-loaded（F-039）。
- plain の look の target は窓の本体だけ（titlebar のパンくずは glass の look だけ）。
- 自分の drag が窓の外へ出た後、窓へ戻っても窓の中の見た目（半透明の項目）は出ない（target の光だけ）。
