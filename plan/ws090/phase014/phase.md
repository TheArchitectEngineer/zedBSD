<!-- awesome-plan project=zedbsd record=ws090-p014 -->

# ws090-p014: file chooser を親の窓の title bar にぶら下がる sheet にする（不透明の窓）

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws090-widgets`（branch `wt/ws090`）、main を merge した上。QEMU の Venus、実機は未実施）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て。ユーザー「File Pickerは、独立したタイトルバーを持つウィンドウではなく、親ウィンドウのタイトルバーに
ぶらさがって前面に表示されるスタイルにしたいです。親ウィンドウがない場合は独立にします。」、同日の追加「File Chooserは透過ウィンドウをやめましょう。」）
依存: p006（`kui_file_chooser`）

## 範囲と受け入れ

- compositor が `xdg_toplevel.set_parent` の親を覚える（親が消えたら忘れる）。
- sheet は Keiland の protocol の opt-in にする。他の toolkit の set_parent の dialog は今のまま。
- sheet には自分の title bar が無い。親の浮いた title bar の下端に上端を付け、横は中央、幅は親より狭くする。
  - 上から滑り出る。
  - 親と一緒に動く・前に出る・最小化する・Wiseview に入る。
  - 開いている間は親の body への入力を止め、親の title bar の drag は通す。
  - 最大化・fullscreen の親での置き場所も決める。
- libkeiui の chooser は、親があれば sheet を求め、無ければ独立の窓にする。
- 追加: chooser の窓（sheet・独立の両方）を不透明にし、glass を透かさない。文字のコントラストを確かめ、sheet の縁を整える。

## 設計

### protocol の選択: Titlebar Presentation（`keiland_titlebar_v1`）の mode 3 `SHEET`（version 3）

sheet は、窓の title bar をどう見せるかの選択である。自分の title bar を出さず、親の title bar の下に付く。
そのため、既存の Titlebar Presentation の mode として表す。追加は enum の値と version の 1 つ上げだけで、新しい global は要らない。
親子の関係は標準の `xdg_toplevel.set_parent` を使い、sheet は「親を持つ窓が SHEET の mode の titlebar を commit したもの」とする。
他の toolkit の set_parent の dialog は SHEET の mode を持たないので、今のまま独立の窓になる。

- `keiland.h`: `KEILAND_TITLEBAR_SHEET`（3）、`KEILAND_VERSION` 20。libkeiland の `keiland_titlebar_set_mode` は、compositor の
  version が 3 より古ければ `ENOTSUP` を返す（chooser はそのまま独立の窓になる）。
- libwayland の `titlebar-protocol.c` と zdesktop の `protocol.c` の広告を version 3 にした。zdesktop の `titlebar.c` は mode 3 を受け付ける。

### compositor（zdesktop）

- `sheet.c`（新）: `zwl_sheet_parent`（mapped の窓の親。親が生きていて、titlebar の shown の mode が SHEET のときだけ）、
  `zwl_sheet_of`（親の最も上の sheet）、`zwl_sheet_set_parent`、`zwl_sheet_surface_gone`（消えた親を子から外す）。
  `toplevel.c` の `set_parent` と surface の消滅から呼ぶ。`zwl.h` の object に `parent_window`・`sheet_ms`。
- `shell.c`:
  - 置き場所（`sheet_place`、毎 tick）:
    - x は親の body の中央。
    - 上端は、浮いた親では body.y − `ZWL_GLASS_GAP`（title bar の下端）。最大化・fullscreen の親では body.y（system bar の下、y=38）。
    - 出始めから 200 ms、ease-out で上から滑り出る。
    - 親の desktop と minimized を写す。
    - 親の body − 2×24 より広い sheet は、幅が 320 と min_width 以上なら configure で狭める。
  - 描画（`draw_sheet`）:
    - title bar と枠を描かず、body だけを描く。
    - 親の title bar の下端で scissor を切る。影が title bar に落ちず、滑る間は title bar の下から出る。
    - 上の角は丸めない（`draw_body` の丸めの box を上へ伸ばす）。
    - 境目に 1px の細線を引く。
  - 入力:
    - sheet は body の hit だけで、枠の resize は無い。
    - sheet を持つ親への押下は、title bar のもの以外は止めて、親と sheet を前に出す（`ZWL GLASS sheet holds`）。
    - 親の title bar の drag は通り、sheet がついて行く。
  - 重なり:
    - `window_raise` は、sheet なら親、親なら sheet を含めて、親 → sheet の順に上げる。
    - `window_lower` は親と sheet を一緒に下げる。
    - system bar の docked の title、Shift+矢印の desktop の移動、Wiseview の current は sheet を親として扱う。
    - Wiseview の tile に sheet は出ず、親を戻すと sheet も戻る。

### libkeiui

- `chooser.c`: 親があれば、chooser の窓に titlebar を作って SHEET の mode を commit する。
  拒まれたら（古い compositor）titlebar を捨てて独立の窓のままにする。窓を閉じる前に titlebar を消す。
- 不透明:
  - chooser は `keiland_glass` を作らない（`style.glass = 0`）。theme の不透明な地（ground の gradient、sidebar、白い content の panel と影）で描く。
  - zdesktop の `window_opacity` の既定は 1.0 なので、glass は透けない。利用者がこの設定を下げたときは、他の窓と同じに従う。
- コントラスト（open.png の実測）:
  - 本文 `text` は content で 15.2:1、sidebar で 14.0:1。
  - `text_secondary` は 6.4:1 と 5.8:1。
  - `text_faint` は sidebar で 2.1:1 だった。そこで sidebar の見出し（`kui_sidebar_section`、list.c）と chooser の空のフォルダの文を
    `text_secondary` に上げた。見出しは Files と共有なので、Files の不透明の見た目でも見出しが少し濃くなる。

## 確認（2026-09-30）

QEMU の Venus guest（`plan/ws035/tests/zdesktop-guest.sh`、`GUEST_RUNTIME=build/ws094-run`、image は `build/ws081/demo-win-venus.img` の複写）で確かめた。
merge の後に build し直して、もう一度流した。

- `plan/ws090/tests/sheet-guest.sh OUT install open move hold dock minimize saveas`: PASS。
  - open: Open の sheet が x=260 y=95（親の body 190,103 の中央、title bar の下）。
  - move: 親を drag すると sheet が x=120 y=152 について行く。
  - hold: 親の body の押下が止まる（`sheet holds`）。
  - dock: 最大化した親の下の y=38 に付く。
  - minimize: 親の最小化で両方が隠れる。Wiseview には親だけが出て、戻すと sheet も戻り、Cancel が効く。
  - saveas: Save As も sheet になる。
  - 各 step の Cancel で `TEXTEDIT CHOSEN` が増える。zdesktop の ERROR は 0。
- 画面（`build/ws090-shots/p014/`）: open.png・moved.png・docked.png・minimized.png・wiseview.png・restored.png・saveas.png（不透明の sheet）。
- host:
  - `plan/tools/keiui/host-chooser.sh` 85/85。
  - `plan/ws090/tests/host-draw.sh` 13/13（pixel の差 0）。
  - `host-input.sh` 63/63。
  - `host-widgets.sh` 94/94。
    - ws102-p019 の color glyph の後、この script は link で落ちていた（`keiland_color_glyph` の未定義）。
    - `picture/color-glyph.c` と png・zlib の compat を link に足して直した。
- 回帰:
  - WS079-p010（`plan/ws079/tests/zdesktop-p010.sh`）: PASS。
    - 新しい guest では先に全部の `.so` を入れる必要がある。入れないと `ld.so: undefined symbol` で落ちる。
  - C9（`plan/ws099/tests/criteria.sh`、criteria image を build し直した上）: 10/10 PASS（`build/ws090-p014-c9/results.txt`）。
  - boot test（`plan/tools/boot-test.sh`）: PASS（`build/ws090-p014-boot/login.png`）。
- style: `plan/tools/style-check.py` を変えた file すべてに流し、新しい違反は 0。
- build: `make ... wayland textedit libkeiui.so libkeiland.so libwayland-client.so` で warning 0（`-Werror`）。

## 制限・残り

- 親の無い chooser は、今の tree には無い。Text Editor・PDF Viewer・Image Viewer はどれも親を渡す。独立の窓の経路（SHEET を求めない）は
  コードの上だけで、画面では確かめていない（未実施）。
- sheet がある間、親への hover と axis（wheel）は止めていない。止めているのは button の押下だけである。touch の押下は確かめていない（未実施）。
- 実機は未実施。
