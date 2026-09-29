<!-- awesome-plan project=zedbsd record=ws094-design -->

# WS094 の設計: desktop の file の icon

2026-09-29 ws094-p001。目標は [ws.md](ws.md)（ユーザー「デスクトップにファイルアイコンの表示。」）。

## 1. 今の状態（調査の結果）

| 部分 | 所在 | 事実 |
| --- | --- | --- |
| compositor（zdesktop、WS035） | `userland/desktop/wayland/` | protocol を手で実装する独自の compositor（`protocol.c` の globals: wl_compositor・xdg_wm_base・wl_seat・wl_shm・wl_data_device_manager・xdg_menu・keiland_titlebar・keiland_glass ほか）。**layer-shell（背景の層の client）は無い**。壁紙は compositor が描く（`compose.c`、`--wallpaper=`）。App Home（`home.c`）は compositor が icon を描き `/bin/sh -c` で app を起こす。仮想 desktop が 4 つ（`shell.c`、システムバーの絵、Wiseview から窓を移す）。App Home は左上の角からの drag で開く（hot corner）、Wiseview は下端からの swipe。backdrop（`backdrop.c`）は窓の下の景色（壁紙と下の窓）をぼかして glass に使う |
| session | `sessiond/session.sh` | `~/Desktop` ほかの folder を作り、`exec /bin/wayland --session …`。compositor の後に何かを起こす仕組みは無い（script は compositor に exec する） |
| Files（WS071、WS093） | `userland/desktop/files/` | 窓ごとに別 process（新しい窓は `fm_apps_spawn` で `/bin/files`）。model: listing（`dir.c`）、2 秒ごとの mtime の監視（`ui.c`）、MIME（`mime.c`）、thumbnail（`thumb.c`）、icon の絵（`icons.c`）、icon の格子（`ui-grid.c`）、選択（`select.c`）、開く（`fm_open_entry`・`fm_apps_*`、WS093 の既定と Always Open With）、名前の変更・複製・Trash・Undo（`actions.c`・`trash.c`・`undo.c`）、context menu（`keiland_menu_popup`、`ui-context.c`）、DnD（`dnd.c`・`ui-drag.c`、text/uri-list、move/copy）、touch（`touch.c`: tap は click、long press は context menu） |
| WS090 | `plan/ws090/design.md` | 新しい共有 library `libkeiui`（設計のみ。p002 以降は未着手）。Files は p009・p010 で移す計画 |

## 2. 誰が描くか（J1）

| 案 | 内容 | 利点 | 欠点 |
| --- | --- | --- | --- |
| A: compositor が描く | App Home と同じく zdesktop が `~/Desktop` を読み、icon を描き、開く | protocol が要らない。起動の順序が単純 | Files の model（MIME・thumbnail・開く・Always Open With・名前の変更・Trash・Undo・DnD・context menu・touch）を compositor に作り直すか移す。file の操作（大きな copy・壊れた画像の thumbnail）が compositor の loop を止める危険。compositor は全ての client の要で、落ちると session が終わる |
| **B: Files が背景の層の client として描く（既定）** | `files --desktop` が、compositor の新しい role「desktop surface」（壁紙の上・全ての窓の下の全画面の透明な surface）に描く | Files の model と操作をそのまま使う（WS093 の開く・Always Open With も）。file の操作が compositor を止めない。落ちても compositor が起こし直すだけ | compositor に protocol（role・重ね順・入力・Wiseview と App Home の扱い）と、client の起動を足す。libkeiland に client の API を足す |

**既定 B**。理由: 目標の操作（開く・選択・名前・Trash・右 click・DnD・touch）はすべて Files にあり、compositor に同じものを作ると二重になる。
compositor の変更は role と重ね順・入力・起動に限る。

## 3. compositor の desktop surface（protocol `keiland_desktop_v1`）

- **global** `keiland_desktop_manager_v1`（version 1）: request `get_desktop_surface(id: new keiland_desktop_surface_v1, surface: wl_surface, token: string)`。
  - role は compositor が起こした client にだけ許す: compositor は起こす時に使い捨ての token を環境変数 `KEILAND_DESKTOP_TOKEN` で渡し、
    違う token・2 つ目の desktop surface は protocol の error（`role`・`defunct`）。Files は起動の直後に token を読んで環境から消す
    （`unsetenv`）。消さないと、desktop から起こした app（`fm_apps_launch`）が token を継ぎ、desktop が落ちた隙に role を取れる。
  - event `configure(serial, x, y, width, height)`: 置き場（出力の中の、システムバーを除いた作業領域）。`ack_configure(serial)`。
  - request `destroy`。
- **重ね順**: 壁紙の直後、全ての toplevel・subsurface・popup の下。**4 つの仮想 desktop のどれでも同じ 1 枚**を示す（`~/Desktop` は 1 つ）。
  全画面の窓の下では描かない（見えない）。
- **合成**: premultiplied alpha で壁紙の上に重ねる（透明の所は壁紙が見える）。backdrop（窓の glass）の「窓の下の景色」に含める。
  App Home を開く時の「desktop の層」（壁紙と窓が右下へ縮む）に含める。Wiseview の間は描かない（窓の tile だけ。p002 で決めた）。
  backdrop は他の窓の上にある窓の glass の分だけ作られるので、一番下の窓の glass はこれまでどおりぼかした壁紙だけを映す（desktop の icon は
  映らない。p002 の制限）。
- **入力**: pointer・touch は、どの窓の上でもない所で desktop surface に届く。ただし compositor の gesture（左上の hot corner の drag、下端からの
  Wiseview の swipe、端からの仮想 desktop の swipe）が先に取る。keyboard focus は desktop surface を click した時に移り、窓を click すると窓へ。
  focus の切り替え（Alt+Tab 等）・Wiseview の tile・window の一覧・システムバーの窓の印には出さない。最後の窓が閉じても focus は自動では
  desktop に移さない（click した時だけ）。desktop に focus がある時の System Menu は、窓の無い時と同じ compositor の既定（desktop surface は
  window menu を持たない。Files の操作は context menu と key で行う）。
- App Home・Wiseview・lock 画面・greeter を開いている間は、それらが入力を取り、desktop surface には届かない（lock と greeter の間は描かない）。
- **popup と DnD**: desktop surface を親にした popup（`keiland_menu_popup` の context menu、名前の変更の field は Files の中で描く）を許す。
  DnD（`wl_data_device`）の enter・motion・drop の対象に desktop surface を含める（窓の無い所へ落とすと desktop へ）。desktop surface からの
  `start_drag` も窓と同じに扱う。
- **起動**: `--session` の compositor は `/etc/keiland/desktop` の最初の語が `on` の時に `/bin/files --desktop` を起こす（p002 から p003 までの既定は
  起こさない。main の判断 2026-09-30。p003 で Files の `--desktop` ができたら既定を「起こす」に戻し、`off` で止める形にする、J6）。
  落ちたら 2 秒後に起こし直す（1 分に 3 回まで。越えたら log して止める）。`--session` でない試験の compositor は `--desktop-client=PATH` で
  起こすか、試験が自分で `files --desktop` を起こす（token は `--desktop-token=` で固定できる）。
- **log**: `ZWL DESKTOP role client=… width=… height=…`、`ZWL DESKTOP start pid=…`・`restart`・`gone`（試験は log と画面で判定する）。
- **libkeiland の client**: `keiland_desktop_create(display, surface, token, listener)`・`keiland_desktop_ack(…)`・`keiland_desktop_destroy`
  （glass・titlebar と同じ形）。

## 4. Files の desktop mode（`files --desktop`）

- **model**: `~/Desktop`（`$HOME/Desktop`、無ければ作る）を開いた 1 つの tab。folder の監視は今の 2 秒の mtime（`ui.c`）。
  隠し file は出さない。
- **見た目**: 窓・sidebar・titlebar・glass の panel は無い。作業領域の全体が canvas。icon は Files の icon の絵（`fm_grid_entry_icon`）と
  画像の thumbnail、大きさ 64 px、格子 96 × 104 px。名前は slate（#1e293b）の文字に白い柔らかな縁取り（Kei の壁紙は明るいので、白い文字に影では
  読みにくい。最大 2 行、長ければ中を省く）。選択は icon の後ろの淡い白の角丸の地と、名前の青（#2f7cf6）の pill に白い文字。rubber band の選択は
  半透明の青の矩形。
- **配置（J2・J3）**: 格子の cell に置く。**右上から下へ、列が埋まれば左の列へ**（左上は App Home の hot corner のため避ける）。
  利用者が動かした icon の cell は `~/.config/keiland/desktop-layout`（1 行 `NAME<TAB>COLUMN<TAB>ROW`、列は右から数える）に保存し、
  新しい file は空いた最初の cell に置く。無い名前の行は次の保存で消える。Files 内での名前の変更は行の名前も変える。画面の大きさが変わって
  cell が外れたら、空いた cell へ（保存はしない）。「Clean Up」は名前順に詰め直す（保存を消す）。
- **操作**:
  - click で選択、Ctrl・Shift で追加、空いた所からの drag で rubber band、空いた所の click で選択を外す。
  - double click・Enter で開く: file は `fm_open_entry(…, 0)`（WS093 の既定、利用者の Always Open With）。folder は新しい Files の窓
    （`fm_apps_spawn` で `/bin/files FOLDER`）。
  - 右 click（touch の long press）: 項目の上なら Files の context menu（Open・Open With・Always Open With・Cut・Copy・Paste・Rename・Duplicate・
    Tags・Get Info・Move to Trash）に「Show in Files」を足したもの。空いた所なら「New Folder・Paste・Clean Up・Show Desktop in Files・Change Wallpaper…」
    （Change Wallpaper は Settings、WS089 があれば `/bin/settings --page=wallpaper`、無ければ出さない、J7）。
  - 名前の変更は icon の名前の所に text field（Files の `ui-field.c`）。F2 でも。Delete で Trash、Ctrl+C・X・V・A・Z。
  - drag: 選んだ icon を desktop の中で動かす（cell へ吸着、保存）。folder の icon の上に落とせばその folder へ移す。窓の外へ出れば
    Files の DnD（text/uri-list、move・copy）で他の Files の窓や app へ。Files の窓から desktop へ落とせば `~/Desktop` へ移す（別の device なら copy、
    Files の既定と同じ）。落とした所の cell に置く。
  - touch: tap は click、double tap は開く、long press は context menu、long press の後の drag は icon の移動（Files の touch.c と同じ規則）。
- 「Show in Files」は `/bin/files ~/Desktop`（項目の選択は渡さない。Files の新しい窓は folder を開くだけ）。
- **log**: `ZFILES DESKTOP ready items=… cells=…`、`ZFILES DESKTOP place name=… column=… row=…`、既存の `ZFILES OPEN`・`LAUNCH`・`ACTION` 等。

## 5. WS090（libkeiui）との関係（J5）

WS094 は libkeiui を待たない。desktop mode は Files の中の 1 つの見せ方なので、Files の今の描画（`canvas.c`・`text.c`・`icons.c`・`ui-grid.c`）
と入力の model を使う。WS090 の p009・p010（Files を libkeiui へ移す）が来た時に、desktop mode も Files の一部として移る。同じ file を同時に
変えないよう、WS094 の Files の Phase（p004〜p006）と WS090 の p009・p010 は重ねない（main が順を決める）。新しく足す描画（名前の影、選択の地）は
libkeiui の theme に後で移せるよう、`ui-desktop.c` の中の小さな関数に閉じる。

## 6. 範囲外

- 複数の出力（今の compositor は 1 つ）。出力ごとの desktop は後で。
- desktop の設定の画面（icon の大きさ、並べ方、表示の on/off の Settings の頁）。on/off は `/etc/keiland/desktop` の file だけ（J6）。
- desktop に置く特別な icon（Trash・Computer・device）。`~/Desktop` の file と folder だけ。
- symbolic link・`.desktop` の launcher。
- 実機の確認は各 Phase の範囲外（QEMU の Venus で確かめ、実機は未実施と書く）。

## 7. Phase

| Phase | 内容 | 依存 | 触る所 |
| --- | --- | --- | --- |
| p002 | compositor: `keiland_desktop_v1`（role・token・configure）、重ね順（壁紙の上・窓の下・全ての仮想 desktop）、合成（alpha・backdrop・App Home・Wiseview）、入力の経路（compositor の gesture が先）、focus、`--session` での起動と起こし直し、log。試験用の最小の client（probe） | p001 | `wayland/`（WS035 の source） |
| p003 | libkeiland の client の API（`keiland_desktop_*`）と Files の `--desktop` の骨組み: surface を取り、作業領域の全体に `~/Desktop` の icon を右上から並べて描く（見るだけ）、監視 | p002 | `libkeiland/`・`include/libc/keiland.h`・`files/` |
| p004 | 選択（click・Ctrl・Shift・rubber band）、double click・Enter で開く（WS093）、folder は新しい窓、keyboard、配置の保存と Clean Up | p003 | `files/` |
| p005 | 右 click・long press の menu（項目と空いた所）、名前の変更、Trash・Copy・Paste・New Folder・Show in Files | p004 | `files/` |
| p006 | drag: desktop の中の移動、folder への移動、Files の窓との DnD、touch（double tap・long press・drag） | p005 | `files/` |
| p007 | 全文の規約と回帰（compositor の回帰の該当、Files の回帰、boot test） | p006 | — |

確かめ方（各 Phase）: QEMU の Venus の guest で、main の zdesktop の image の複写にこの worktree の wayland・files・libkeiland を入れ、QMP の pointer・key
で操作し、log（SSH）と画面で判定する（console・serial は読まない）。p002 は probe の client で重ね順・入力・popup・DnD の対象・起こし直しを確かめる。
touch は pen の image（注入）。各 Phase の最後に boot test。compositor を変える p002 は WS035 の回帰（`plan/tools/` の zdesktop の試験のうち窓・App Home・
Wiseview・backdrop の該当）も流す。

受け入れ（WS）: session の compositor で desktop に `~/Desktop` の icon が右上から並び、double click で WS093 の対応の app が開き、右 click の menu・
名前の変更・Trash・移動と保存・Files の窓との DnD・touch の double tap と long press が働く（QEMU の Venus の画面と log）。

## 8. 判断が要る点（既定を選んだ）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| J1 | 誰が描くか | Files が背景の層の client（案 B） | Files の model と操作を二重にしない。file の操作で compositor を止めない |
| J2 | 並べ始める角 | 右上から下へ | 左上は App Home の hot corner |
| J3 | 配置の保存 | `~/.config/keiland/desktop-layout`（名前と cell） | file の属性（xattr）を使わず、他の folder に影響しない |
| J4 | 仮想 desktop | 4 つとも同じ icon | `~/Desktop` は 1 つ |
| J5 | WS090 | 待たない。Files の Phase と WS090 p009・p010 を重ねない | デモに間に合わせる |
| J6 | 表示の on/off | p003 までは既定 off（`on` で起こす）、p003 から既定 on（`off` で起こさない） | Settings の頁は範囲外。Files の `--desktop` ができるまでは起こさない（main 2026-09-30） |
| J7 | Change Wallpaper | Settings（WS089）の頁があれば出す | 無い app を menu に出さない |

## 9. 他の WS との調整（main へ）

- **compositor の source（`userland/desktop/wayland/`）は WS035 の担当**で、WS035 は今も Phase が続いている（別の worktree もある）。p002 は
  WS094 の Phase として compositor に足すので、main の許可と、WS035 の作業と同じ file（`protocol.c`・`compose.c`・`input.c`・`shell.c`・`home.c`・
  `backdrop.c`・`zwl.h`・`main.c`）を同時に変えない順が要る。
- **libkeiland** は WS092（file chooser）・WS090 も触る。p003 の追加（新しい file `desktop.c` と header の宣言）は小さいが、main の許可と順が要る。
- **Files** は WS090 の p009・p010 と重ねない（§5）。
- `sessiond/session.sh` は変えない（compositor が起こすため）。
