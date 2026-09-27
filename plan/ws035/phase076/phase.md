<!-- awesome-plan project=zedbsd record=ws035p076 -->

# ws035-p076: xdg-shell の残り（popup と positioner、toplevel の要求、ping）

Phase ID: `ws035-p076`
Parent: [WS035](../ws.md)（p028 から 2026-09-27 に分割）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（N=3 のサブエージェントを常に走らせ、1 つが WS035 を続ける。デスクトップと graphics が最優先。
main の session の伝達による要約）

## 出発点

前のエージェントが rate limit で p076 の途中で止まり、未 commit の作業が branch `salvage/ws035`（commit 1e16a8d5、親 758b573a）に
残っていた。これを `git cherry-pick -n` で未 commit の変更として取り込み（main の ed1017a1 の上で衝突なし）、読み直してから続けた。
取り込んだもの: `popup.c`・`popup.h`（xdg_positioner・xdg_popup、grab、popup_done、描画）、`protocol.c`・`seat.c`・`shell.c`・
`compose.c`・`display.c`・`objects.c`・`zwl.h` の接続、`userland/base/tests/popup-probe/`、`plan/ws035/tests/zdesktop-p076.sh`、
`platform/amd64/vmunix.mk` と `plan/ws070/tests/config-amd64-menu.mk` の probe の build。

取り込んだ時点の試験（`build/ws035-p076-salvage/`）は、step 5 の判定（`button other` を log 全体で数えていた試験の誤り）以外は
通ったが、読み直して次の不具合を見つけた:

1. submenu が grab を求めてから image を出すまでの間、keyboard の focus が window へ戻った（`zwl_popup_focus` が未表示の grab を
   見て諦めていた）。log では menu → window → submenu の順に focus が動いていた。
2. grab の間の pointer の enter/leave が keyboard の focus の変化と混ざり、submenu が pointer の外（surface-local の -260,10）で
   enter を受けた。
3. popup_done を送った popup や親を失った popup を描き続けた（client が destroy するまで）。親を失った popup は output の原点から
   描かれる恐れがあった。
4. client の切断で xdg_surface が popup より先に free され、popup の `role` が dangling になりえた（toplevel は先に消していたが popup は
   消していなかった）。
5. `xdg_toplevel.move` をボタンが離れた後の遅い要求でも受け、窓が次のクリックまで pointer に付いてくる恐れがあった。
6. ping、resize、min/max size は未実装。

## 範囲

1. xdg_positioner（v3 の全要求）と xdg_popup（grab、reposition、popup_done、destroy）。popup は親の window geometry からの位置で、
   窓の上（system bar の下）に作られた順に描く。grab の間は keyboard が一番上の表示された grab の popup、pointer はその client の
   chain（popup と toplevel）の下の surface、外の press は popup をすべて閉じて食べる。
2. xdg_toplevel の要求: set_parent（受け付け、階層は持たない）、show_window_menu（受け付け、zdesktop に window menu は無い）、
   move・resize（押したままのボタンの serial のときだけ）、set_min_size・set_max_size（resize が守る）、maximize・unmaximize・
   minimize（glass の look の dock・undock・最小化）。`xdg_surface.set_window_geometry` を commit で適用。
3. xdg_wm_base の ping: client へ届いた press ごとに ping（未回答が無ければ）。5 秒答えない client は応答なしで、その窓のタイトルに
   「(not responding)」。pong で戻る。
4. libwayland の xdg-shell を v3 に（試験の client が reposition を使うため。toolkit も v3 を使う）。

## 受け入れ

1. Venus の guest で `plan/ws035/tests/zdesktop-p076.sh` が PASS（menu・submenu・flip・dismiss・reposition・move・3 つの resize と
   limits・ping と応答なし・maximize/unmaximize/minimize）。画面を PNG で残す。
2. 回帰: WS035 の zdesktop の試験（p014・p059・p062〜p065・p068〜p072、`plan/ws070/tests/menu-regress.sh`）、X11 の回帰
   （`plan/tools/x11/`）、libwayland の host 試験（p075）、boot test。
3. build warning 0。新しい file は style-check 0、既存の file は悪化させない。

## 実装

- `userland/base/zdesktop/popup.c`（salvage から、直した）: 上の 1〜4 の不具合を直した。`popup_closed`（popup_done を送った popup は
  描かず hit もしない、popup_done は 1 度だけ）、`grab_shown`（表示された一番上の grab の popup。未表示の submenu の間は menu）、
  `pointer_grabbed`（grab の最初の popup が表示されてから grab が終わるまで pointer は popup.c のもの。seat の focus の変化は keyboard
  だけを動かす）、`pointer_update`（chain の下の surface へ leave/enter、chain の外では leave して誰にも送らない）、grab の終わりで
  pointer を focus へ戻す。positioner の set_parent_size・set_parent_configure の大きさの検査を別々に。
- `userland/base/zdesktop/toplevel.c`・`toplevel.h`（新規）: xdg_toplevel の window manager への要求（protocol.c の switch の default
  から）、interactive resize（pointer に従って resizing の状態付きの configure、離すと状態なしの configure。左・上の辺を引くときは
  反対の辺を固定する anchor を最後の大きさの image か、最後の configure の ack の後の commit まで保つ）、ping（送信、pong、5 秒の検査）。
- `seat.c`: 押しているボタンの bit と最後に client へ送った press の serial（move・resize の検査）、press のたびの ping、resize の
  motion と button を先に。grab が pointer を持つ間は `send_enter`・`send_leave` が pointer に送らない。
- `protocol.c`: xdg_wm_base v3 を広告、pong を toplevel.c へ、positioner と popup の作成、ack の serial を記録、resizing の状態の
  configure、xdg_surface と xdg_toplevel は親の版で作る（前は 1 固定）、toplevel の要求の default を toplevel.c へ。
- `shell.c`: タイトルに「(not responding)」（`shown_title`）、`zwl_glass_toplevel_request`（salvage から）。
- `objects.c`: surface の破棄で resize を終える、client の切断で popup を xdg_surface より先に消す。`display.c`: commit で
  resize の anchor。`main.c`: 毎 pass の ping の検査。
- libwayland: xdg_wm_base・positioner・surface・toplevel・popup を v3 に（positioner の set_reactive・set_parent_size・
  set_parent_configure、popup の reposition と repositioned、`_SINCE_VERSION`）。xdg の子 object を親の版で作る（前は 1 固定で、
  v3 で bind しても popup は v1 だった）。`API-PROVENANCE.md` に追記。
- `userland/base/tests/popup-probe/main.c`: 書き直し（規約、configure の大きさで描き直す、limits、resize の corner と左の辺、右の
  press で window menu、キー r で reposition、キー p で pong を止める・再開）。
- 試験の道具: `plan/ws035/tests/qmp-pointer.py` に右ボタン（`right-down`・`right-up`）。

## 判断が要る点

- show_window_menu は受け付けて何もしない（zdesktop に window の menu が無い）。window menu を System Menu（WS070）と一緒に作るかは
  後で決める（可逆: 今は何も表示しない）。
- set_parent は受け付けるが、dialog を親の上に保つ・親と一緒に最小化する等の階層は持たない（新しい窓は上に出るので当面は困らない）。

## 結果（2026-09-27）

（記入中）
