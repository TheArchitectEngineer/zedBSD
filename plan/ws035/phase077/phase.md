<!-- awesome-plan project=zedbsd record=ws035p077 -->

# ws035-p077: wl_subcompositor と wl_subsurface

Phase ID: `ws035-p077`
Parent: [WS035](../ws.md)（p028 から 2026-09-27 に分割）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（N=3 のサブエージェントを常に走らせ、1 つが WS035 を続ける。デスクトップと graphics が最優先）

## 背景

GTK・Qt・Chromium（Ozone）・Firefox は video・GL の部品・client 側の装飾・tooltip の影などを sub-surface に描く。zdesktop は
`wl_subcompositor` を広告しておらず、libwayland（zedBSD の client library）も core の `wl_subcompositor`・`wl_subsurface` を
持っていなかった（`API-PROVENANCE.md`「does not supply wl_subcompositor」）。toolkit は core の interface を libwayland-client から
使うので、両方が要る。

## 範囲

1. zdesktop: global `wl_subcompositor` v1（destroy、get_subsurface）と `wl_subsurface`（destroy、set_position、place_above、
   place_below、set_sync、set_desync）。protocol の error（bad_surface: 役割のある surface、自分・祖先を親に、bad_parent）。
2. 状態: 位置は親の commit で適用する（double-buffered）。sync（既定）の子の commit は cache に置き、親の状態が適用されるときに
   適用する（入れ子は再帰）。desync の子は自分の commit ですぐ（祖先が sync なら実質 sync）。
3. 合成: 窓（plain と glass の look）と popup の image の下（place_below で親の下）と上に子を描く（入れ子、位置は親の image の
   左上から、glass の look で body が伸び縮みする間は同じ比で）。子の buffer は frame が持ち、frame callback は frame の後。
4. 入力: pointer は窓の木（親と子）のうち pointer の下の一番上の surface に enter/leave・motion・button を送る（surface-local の
   座標）。keyboard は窓の main の surface のまま。popup の grab の間も同じ（chain の各 surface の木）。
5. libwayland: `wl_subcompositor`・`wl_subsurface` の interface の記述・wrapper・header（upstream の名前と版）。
6. 試験: 新しい probe（`userland/base/tests/subsurface-probe/`）と `plan/ws035/tests/zdesktop-p077.sh`。

## 受け入れ

1. Venus で p077 の試験が PASS: 親の上下の子、位置の変更が親の commit まで効かない（sync）、desync の子がすぐ動く、入れ子、
   place_above/below、子の上の pointer の enter と座標、子の destroy・親の destroy。画面を PNG で残す。
2. 回帰: p076 と WS035 の zdesktop の試験（menu-regress）、X11 の回帰、p075 の host 試験、boot test。
3. build warning 0、新しい file は style-check 0、既存の file は悪化させない。

## 判断が要る点

- place_above・place_below は親の commit を待たずにすぐ適用する（protocol では親の commit で適用）。toolkit が並べ替えるのは
  親の commit の直前が普通で、見た目の差は 1 frame。必要なら後で pending にする（可逆）。
- 入力の region（set_input_region）は保持せず、子の image 全体が入力を受ける（今の zdesktop は region を受け付けて捨てている）。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/desktop/wayland/subsurface.c`・`subsurface.h`（新規）: global `wl_subcompositor` v1（`protocol.c` の globals の 8）、
  `wl_subsurface`。子の list（下から上、親の下か上か）、位置は親の状態の適用で、sync の子の commit は cache（buffer・attach の有無・
  frame callback。damage は pending に残り一緒に行く）、親の適用で flush（入れ子は再帰）。set_desync は cache をすぐ適用。
  bad_surface・bad_parent の error（役割のある surface、自分・子孫を親に）。描画（`zwl_subsurface_draw`: glass の look は body と
  同じ比で伸ばす四角い shape、plain は quad）、frame が持つ buffer（`zwl_subsurface_collect`）、pointer の hit（`zwl_subsurface_at`）。
- `protocol.c`: 役割の無い surface の commit を `zwl_surface_queue` に分け、sub-surface の commit は subsurface.c へ、すべての
  適用の後に `zwl_subsurface_applied`。sub-surface に xdg の役割や cursor を付けるのを拒む。
- `seat.c`（pointer の作り直し）: keyboard の focus（`focus`、keyboard の enter/leave だけ）と pointer の surface（`pointer_surface`）
  を分けた。`zwl_seat_pointer_update` が pointer の下の surface（focus の窓か、その sub-surface。popup の grab の間は chain の surface か
  その sub-surface、chain の外は無し）へ enter/leave を送る。motion・button の前と focus の変化で呼ぶ。axis・frame・set_cursor・
  新しい wl_pointer の enter は pointer の surface へ。
- `popup.c`: p076 の grab の pointer の扱い（`pointer_focus`・`pointer_update`・`zwl_popup_motion`・`zwl_popup_pointer_target`）を
  seat へ移し、`zwl_popup_chain_at` を出す。popup の sub-surface も描く。
- `shell.c`（draw_body）・`compose.c`（plain の look と frame の hold）: 窓の sub-surface を image の下と上に。`display.c`: sub-surface の
  ある窓は fullscreen の scanout にしない（合成で描く）。`objects.c`: surface・wl_subsurface の破棄で木から外す。
- libwayland: `subsurface-protocol.c`（新規）と `wayland-client-protocol.h` に `wl_subcompositor`・`wl_subsurface`（upstream の名前・
  opcode・error）。`API-PROVENANCE.md` に追記。
- `userland/base/tests/subsurface-probe/`（新規）、`plan/ws035/tests/zdesktop-p077.sh`（新規）。lean image の config と vmunix.mk に probe。

### 確認（QEMU・Venus、lean image、runtime `build/ws035-run`）

- `plan/ws035/tests/zdesktop-p077.sh` PASS（`build/ws035-p077/`: shown.png・sync.png・applied.png・desync.png・above.png・
  destroyed.png。main の `build/ws035-shots/p077-20260927-*.png` にも写した）。a と c が窓の上、b は窓の左にはみ出た所だけ、s の後も
  変わらず（sync）、c の後に a が動いて magenta（c も一緒）、d で b が親の commit なしに青、o で b が窓の上、pointer は a（50,20）・
  c（20,20）・窓（300,200）で enter、x で a と c が消える。
- 回帰（seat の作り直しがあるので広く）: p076 PASS、menu-regress の p059・p062〜p065・p068〜p072・p014 PASS、menu-p002・p003 PASS、
  x11-p003・x11-p005 PASS、x11-p004 は lean image に glxtest が無く未実施、p075 の host 試験 PASS。boot test PASS
  （`build/ws035-p077-boot/login.png`、commit 99a1a65f の lean image）。
- i915 実機: 未実施。

### 規約

- style-check: 新しい file（subsurface.c・subsurface.h・subsurface-protocol.c・subsurface-probe/main.c）0。変えた既存の file は
  変更前と同数（seat.c 3、protocol.c 5、wayland-client-protocol.h 23、ほか 0）。build warning 0。

### 制限

- place_above・place_below はすぐ適用（上の判断）。入力の region は持たない（子の image 全体）。
- 窓の外に出た sub-surface も描くが、pointer の hit は窓の sub-surface なら木の全体、popup の grab の間は chain の popup の矩形の中だけ。
- Wiseview のタイルと App Home の縮小された窓には sub-surface を描かない。
- sub-surface の buffer の scale・transform・viewport は無い（p080 で viewporter）。
