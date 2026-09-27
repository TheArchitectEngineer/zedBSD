<!-- awesome-plan project=zedbsd record=ws070-design -->

# WS070 設計: zdesktop の System Menu（`xdg_toplevel_menu_v1`）

Parent: [WS070](ws.md) / Phase: ws070-p001（[WS070](ws.md) の Phase 一覧）
Status: 設計（2026-09-27、ws070-p001）。仕様案は [spec.md](spec.md)（ユーザー提供）。仕様案と違える所・仕様案に無い所は、各節に
**決定**と理由を書いた。人間の判断が要る点は §11 に「未決」として置き、戻せる既定を選んで先へ進めた。

## 0. 前提と範囲

- 2026-09-27 のユーザーの決定（再確認しない）: protocol は仕様案の 3 interface（`xdg_menu_manager_v1`・`xdg_menu_v1`・
  `xdg_toplevel_menu_v1`）、item は数値の ID、型は normal・separator・checkbox・radio・submenu、属性は label・action・enabled・
  visible・checked・role・icon_name・shortcut、activation は `activated(item_id, seat, serial)`、更新は begin_update/commit の
  transaction、描くのは全部 zdesktop（浮いたタイトルバー／docked のシステムバー、popup、keyboard、外の click で閉じる）、
  active menu は focus の toplevel に従う。zdesktop の非標準の拡張なので client は **libkeiland の API** だけを使い、protocol の
  client header は非公開（`zed_gpu_buffer_v1` と同じ扱い）。
- 最初の使い手は terminal（Shell・Edit・View・Session・Help）。後で GTK4（GMenuModel）・Qt6（QMenuBar）の native menubar の
  backend が libkeiland の API を使う。
- 範囲外（v1）: icon の描画（zedBSD に icon theme が無い。role と icon_name は受けて保持し、log に出す）、touch mode（zdesktop は
  wl_touch を出していない）、HiDPI（出力の scale は 1）、accessibility の提示（screen reader が無い。model は compositor にあるので
  後で足せる）、popup の animation、mnemonic（下線の accelerator）、popup の scroll（出力に収まらない長さの menu）。Future Work 候補。

## 1. 全体の形

```
application / toolkit backend
   │  libkeiland: zdesktop_menu_*（model の鏡と局所の検査、transaction）
   ▼
libwayland-client: xdg_menu_manager_v1 / xdg_menu_v1 / xdg_toplevel_menu_v1（非公開 header）
   ▼  Wayland の socket
zdesktop
   protocol.c ─▶ menu.c（server 側の request、model、transaction、object の寿命）
   shell.c    ─▶ menu-shell.c（題名 bar／システムバーの top-level、popup、pointer・keyboard、shortcut、activation）
```

- model（階層・状態）は client が持ち、zdesktop は commit された写しを持つ。描画・配置・入力は zdesktop だけが決める（仕様案 §4、§26）。
- 1 つの `xdg_menu_v1` を複数の窓に付けてよい（GTK の application menu は窓の間で共有される）。activation はどの窓から選ばれたかが
  分かるように `xdg_toplevel_menu_v1` の event にする。

## 2. Protocol（version 1）

表記: 型は Wayland の wire の型（`uint`・`int`・`string`・`object`・`new_id`）。`?` は nullable。opcode は wire の順。

### 2.1 `xdg_menu_manager_v1`（global、version 1）

| opcode | request | 引数 | 意味 |
| --- | --- | --- | --- |
| 0 | `destroy` | — | binding を捨てる。作った menu と toplevel menu はそのまま有効 |
| 1 | `create_menu` | `new_id<xdg_menu_v1> id` | 空の menu model を作る |
| 2 | `get_toplevel_menu` | `new_id<xdg_toplevel_menu_v1> id`、`object<xdg_toplevel> toplevel` | toplevel に menu の表示先を作る |

error（enum `error`）: `already_exists = 0`（その toplevel に生きた `xdg_toplevel_menu_v1` が既にある）。

### 2.2 `xdg_menu_v1`（menu model、request だけ）

| opcode | request | 引数 |
| --- | --- | --- |
| 0 | `destroy` | — |
| 1 | `begin_update` | `uint serial` |
| 2 | `commit` | `uint serial` |
| 3 | `append_item` | `uint id`、`uint parent_id`、`uint type`、`string label`、`uint action` |
| 4 | `insert_item` | `uint id`、`uint parent_id`、`uint before_id`、`uint type`、`string label`、`uint action` |
| 5 | `remove_item` | `uint id` |
| 6 | `set_label` | `uint id`、`string label` |
| 7 | `set_action` | `uint id`、`uint action` |
| 8 | `set_enabled` | `uint id`、`uint enabled`（0・1） |
| 9 | `set_visible` | `uint id`、`uint visible`（0・1） |
| 10 | `set_checked` | `uint id`、`uint checked`（0・1） |
| 11 | `set_role` | `uint id`、`uint role` |
| 12 | `set_icon_name` | `uint id`、`string icon_name`（空文字列で消す） |
| 13 | `set_shortcut` | `uint id`、`uint modifiers`、`uint keysym`（keysym 0 で消す） |

- **item の ID**: 0 でない `uint`、menu の中で一意。`parent_id` 0 は根（top-level の列）。parent は根か `submenu` 型の item。
  `insert_item` の `before_id` は同じ parent の子（0 なら末尾、つまり `append_item` と同じ）。`remove_item` は子孫も消す。
  消した ID は同じ transaction の中でも再び使える。
- **型**（enum `item_type`）: `normal = 0`、`separator = 1`、`checkbox = 2`、`radio = 3`、`submenu = 4`。仕様案 §25 の
  `append_submenu` は `append_item(type = submenu)` で表す（**決定**: request を増やさず、型を 1 つの引数に集める。GMenuModel の
  section・submenu、QMenu の addMenu がどちらも「子を持つ item」に写る）。separator の label と action は使わない。
  submenu の action は使わない。
- **属性の既定**: `enabled = 1`、`visible = 1`、`checked = 0`、`role = none`、`icon_name = ""`、shortcut なし。
- **transaction**（仕様案 §11）: 変更の request（3〜13）は `begin_update(serial)` と `commit(serial)` の間だけで許す。
  zdesktop は begin で commit 済みの model の写しを作り、変更をその写しに当て、commit で差し替える。表示は commit の単位で
  変わり、途中の状態は見えない。serial は client が選び、commit は begin と同じ serial を名乗る（組を取り違えた client を見つける）。
  **決定**: transaction の外の変更は error にする（暗黙の transaction を作ると、中間の状態が描かれる機会が残る）。
  最初の組み立ても 1 つの transaction。
- **checkbox・radio**: `set_checked` は checkbox・radio だけ。状態は client が持つ（仕様案 §4）。zdesktop は選ばれても checked を
  変えず `activated` を送るだけで、client が `set_checked` と commit で答える。radio の組は描画には要らないので protocol に
  持たない（**決定**: 排他は client の責任。GMenuModel の stateful action、QActionGroup がそれぞれ持っている）。
- **role**（enum `role`、仕様案 §12、§14）: `none = 0`、`about = 1`、`preferences = 2`、`quit = 3`、`undo = 4`、`redo = 5`、
  `cut = 6`、`copy = 7`、`paste = 8`、`delete = 9`、`select_all = 10`、`new = 11`、`open = 12`、`save = 13`、`close = 14`、
  `find = 15`、`help = 16`、`fullscreen = 17`、`zoom_in = 18`、`zoom_out = 19`。v1 の zdesktop は並べ替えも icon もせず保持だけする。
- **shortcut**（仕様案 §13）: `modifiers` は enum `modifier` の bit の和（`shift = 1`、`ctrl = 2`、`alt = 4`、`super = 8`）、
  `keysym` は XKB の keysym（ASCII の印字文字は文字そのもの、`F1` = 0xffbe など）。**決定**: evdev の code でなく keysym にした。
  GTK の accelerator（`<Control><Shift>c`）と Qt の QKeySequence はどちらも keysym で表し、backend が変換せずに渡せる。
  zdesktop は keymap を持たないので、自分の US 配列の表で evdev → keysym を引く（§6.4）。
- **error**（enum `error`）: `invalid_id = 0`（0、重複、無い ID）、`invalid_parent = 1`（parent が根でも submenu でもない、
  `before_id` が parent の子でない、自分の子孫の下）、`invalid_type = 2`（型の範囲外、checkbox・radio 以外への `set_checked`）、
  `invalid_value = 3`（0・1 以外の真偽、role・modifier の範囲外）、`not_updating = 4`、`already_updating = 5`、
  `bad_serial = 6`、`too_large = 7`（item 1024 個、深さ 8、文字列 255 byte を超える）。
- **label**: UTF-8。v1 の zdesktop の atlas は ASCII だけで、それ以外の文字は描かれない（§11 の制限）。mnemonic の記号（`_`、`&`）は
  解釈しない。backend が取り除く。

### 2.3 `xdg_toplevel_menu_v1`（窓の menu の表示先）

| opcode | request / event | 引数 | 意味 |
| --- | --- | --- | --- |
| req 0 | `destroy` | — | 窓の menu が消える |
| req 1 | `set_menu` | `?object<xdg_menu_v1> menu` | 窓に menu を付ける（null で外す）。すぐ効き、その menu の commit 済みの状態が出る |
| ev 0 | `activated` | `uint item_id`、`uint action`、`?object<wl_seat> seat`、`uint serial` | 利用者が item を選んだ |
| ev 1 | `opened` | `uint item_id` | submenu（top-level を含む）の popup が開いた |
| ev 2 | `closed` | `uint item_id` | その popup が閉じた |

- `activated` は仕様案 §9 の `activated(item_id, seat, serial)` に `action` を足した（仕様案 §8 の `activate(42)`）。client は item
  でも action でも引ける。`seat` はその client の `wl_seat` の object（無ければ null）、`serial` はその入力に zdesktop が振った新しい
  serial（**決定**: 押された button・key の event は client へ送らないので、その代わりの serial を振る）。
- `opened`・`closed` は仕様案 §19 の optional な通知。GTK の `about-to-show`、Qt の `aboutToShow` に当たり、client はそれに答えて
  更新してよい（zdesktop は開いている popup を commit ごとに描き直す）。
- 寿命: toplevel が先に消えると表示先は不活性になり、`destroy` 以外の request は無視される。menu が先に消えると、それを付けていた
  表示先は何も出さない（再び `set_menu` できる）。

### 2.4 version の扱い

version 1 だけ。以後の追加（mnemonic、icon の画素でない指定、touch の hint 等）は新しい request・event を末尾に足して version を
上げる。libkeiland は bind する version を上限 1 で決める。

## 3. libwayland（client 側）

- `userland/desktop/libwayland/protocol.c` に 3 interface の表（`wl_message`・`wl_interface`）と typed の request の wrapper を足す。
  `event.c` に `xdg_toplevel_menu_v1_listener` の typed dispatch を足す（scanner の生成物を使う toolkit の形と同じ
  `*_add_listener`）。
- header は **非公開**: `userland/desktop/libwayland/xdg-toplevel-menu-v1-client-protocol.h`（install しない。libkeiland が
  `#include "userland/desktop/libwayland/…"` で使う。`zed-gpu-buffer-v1-client-protocol.h` と同じ）。
- export: `exports.map` の `xdg_*` が既に覆う（関数名は `xdg_menu_manager_v1_*` 等）。libkeiland.so は libwayland-client.so に
  動的に link する（platform/amd64/vmunix.mk の libkeiland の規則に `-l:libwayland-client.so` を足す）。

## 4. libkeiland の API（`include/libc/zdesktop.h`）

toolkit の model（GMenuModel の木と action 名、QMenuBar の QMenu・QAction）へ素直に写ることを優先した。

```c
struct zdesktop_menu_service;   /* 接続ごとの menu の窓口（xdg_menu_manager_v1 の binding） */
struct zdesktop_menu;           /* menu model（xdg_menu_v1）と、その ID の鏡 */
struct zdesktop_window_menu;    /* 窓の表示先（xdg_toplevel_menu_v1） */

struct zdesktop_menu_service *zdesktop_menu_service_open(struct wl_display *display);   /* 無ければ NULL・ENOTSUP */
void zdesktop_menu_service_close(struct zdesktop_menu_service *service);

struct zdesktop_menu *zdesktop_menu_create(struct zdesktop_menu_service *service);
void zdesktop_menu_destroy(struct zdesktop_menu *menu);
int zdesktop_menu_begin(struct zdesktop_menu *menu);
int zdesktop_menu_commit(struct zdesktop_menu *menu);
int zdesktop_menu_append(struct zdesktop_menu *menu, uint32_t id, uint32_t parent, unsigned type, const char *label, uint32_t action);
int zdesktop_menu_insert(struct zdesktop_menu *menu, uint32_t id, uint32_t parent, uint32_t before, unsigned type, const char *label, uint32_t action);
int zdesktop_menu_remove(struct zdesktop_menu *menu, uint32_t id);
int zdesktop_menu_set_label / set_action / set_enabled / set_visible / set_checked / set_role / set_icon_name / set_shortcut(...);

struct zdesktop_window_menu *zdesktop_window_menu_create(struct zdesktop_menu_service *service, struct xdg_toplevel *toplevel,
	const struct zdesktop_window_menu_listener *listener, void *data);
int zdesktop_window_menu_set(struct zdesktop_window_menu *window_menu, struct zdesktop_menu *menu);
void zdesktop_window_menu_destroy(struct zdesktop_window_menu *window_menu);

struct zdesktop_window_menu_listener {
	void (*activated)(void *data, struct zdesktop_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
	void (*opened)(void *data, struct zdesktop_window_menu *window_menu, uint32_t item);
	void (*closed)(void *data, struct zdesktop_window_menu *window_menu, uint32_t item);
};
```

- `ZDESKTOP_VERSION` は 2（System Menu を足した版）。
- 定数: `ZDESKTOP_MENU_ITEM_NORMAL`…`SUBMENU`、`ZDESKTOP_MENU_ROLE_*`、`ZDESKTOP_MENU_SHIFT`・`CTRL`・`ALT`・`SUPER`、
  `ZDESKTOP_MENU_ROOT`（0）。値は protocol の enum と同じ。
- **戻り値**: 0 か errno の値。**局所の検査**: libkeiland は ID・parent・型・transaction の鏡を持ち、protocol error になる呼び出し
  （重複 ID、無い parent、transaction の外の変更、checkbox 以外への checked…）を送らずに `EINVAL`・`EEXIST`・`ENOENT`・`EBUSY`・
  `E2BIG` で返す。protocol error は接続ごと client を殺すので、toolkit の backend の誤りを 1 つの呼び出しの失敗に留める。
- **event の queue**: service は global の発見だけを自分の queue（`wl_display_create_queue`、display の wrapper）で roundtrip し、
  manager・menu・表示先は application の既定の queue に置く。activated は application が自分の queue を dispatch したときに listener
  で届く（toolkit の event loop のまま）。
- **fallback**（仕様案 §22）: `zdesktop_menu_service_open` が NULL（`ENOTSUP`）なら、toolkit は client 側の menu を使う。
- **写し方**: GMenuModel は item（section は separator と子の並び、submenu は `submenu` 型）を ID の木に、`action` と `target` を
  action の番号に（backend が名前との表を持つ）、`items-changed` を transaction の中の insert・remove に写す。action の
  enabled・state の変化は `set_enabled`・`set_checked`。QMenuBar は QMenu を submenu、QAction を normal・checkbox（checkable）・
  radio（QActionGroup の exclusive）、`QAction::changed` を set_* に写す。どちらも更新の束を 1 transaction にまとめる。

## 5. zdesktop の model（menu.c）

- object の種類を 3 つ足す（`ZWL_MENU_MANAGER`・`ZWL_MENU`・`ZWL_TOPLEVEL_MENU`）。globals に
  `{ 7, "xdg_menu_manager_v1", 1 }`。`zwl_object` に `menu_model`（ZWL_MENU の model）、`toplevel_menu`（ZWL_TOPLEVEL → その表示先）、
  `shown_menu`（ZWL_TOPLEVEL_MENU → 付いている ZWL_MENU）を足し、表示先の toplevel は既存の `top` を使う。
- model は item の配列（ID、parent、型、action、enabled・visible・checked、role、modifiers・keysym、label と icon_name は malloc
  の文字列）。子の順は配列の順。transaction は写しの配列（begin で深い写し、commit で差し替え、`generation` を増やす）。item は
  1024 個まで、1 つの model で数百 KB を超えない。
- 検査は §2.2 の error の通り。protocol error は `wl_display.error` で object と code を正しく名乗る（`zwl_error` は object 1・code 1
  の固定なので、object・code を取る `zwl_error_code` を wire.c に足す）。
- 寿命の hook: `zwl_object_destroy` が ZWL_TOPLEVEL・ZWL_TOPLEVEL_MENU・ZWL_MENU・ZWL_SURFACE の退場を menu.c に知らせ、menu.c が
  相互の pointer を外し、menu-shell.c が開いている popup がそれを指していれば閉じる。client の切断でも同じ道を通る。
- log（試験が読む）: `ZWL MENU commit client=C menu=M serial=S items=N generation=G`、`ZWL MENU set client=C toplevel=T menu=M`、
  `ZWL MENU activate client=C surface=S item=I action=A via=pointer|key|shortcut`。

## 6. 描画と操作（menu-shell.c）

### 6.1 どこに出すか（仕様案 §15〜§17、§27）

- **浮いた窓**: 各窓のタイトルバーに、印と題名の右（題名の後ろ 18 px）から top-level の項目（visible な根の子）を text だけで並べる。
  focus の無い窓も自分の menu を出す（仕様案 §16「それぞれのウィンドウ」）。focus の無い窓の文字は薄い。
- **docked（最大化）の窓**: システムバーの docked の題名の右に同じ項目。docked の窓が前面にあるときだけ（今の docked の題名と同じ規則）。
  前面の窓が浮いているとき、システムバーは menu を出さない（画像 1 のとおり）。
- 題名は menu があるとき、自分の幅と使える幅の 40% の小さい方で切る。項目が入り切らないときは、入らない分を末尾の「...」の
  項目（overflow）に集め、その popup が残りの top-level を submenu の行として出す。
- 項目の見た目: 文字は SIZE_BAR（14 px）、左右 10 px の余白。pointer の下は白い pill、開いている項目は青い薄い pill。

### 6.2 popup（仕様案 §18）

- frosted glass の panel（題名の bar と同じ MODE_GLASS、白 0.82、縁 0.85、角 10 px、影）。行の高さ 30 px（仕様案 §21 の pointer mode
  28〜32）、separator 11 px、上下 6 px。幅は行の最大（左の溝 30 px + label + 40 px + shortcut + 右の矢印 16 px）、最小 200 px。
- 行: label（無効は薄く）、checked の checkbox は溝に ✓、checked の radio は溝に丸、submenu は右に ›、shortcut は右寄せの薄い文字
  （「Ctrl+Shift+C」「F11」）。pointer・keyboard の選択は青い角丸の帯に白い文字。separator は細い線。
  先頭・末尾・連続の separator は描かない。
- 位置: top-level の popup は項目の左端の 6 px 左、題名の bar（docked はシステムバー）の下 6 px。submenu は親の行の右（入らなければ左）。
  出力の端で押し戻す。✓ と › は glyph atlas に足し（U+2713、U+203A）、font に無ければ小さな四角と「>」で代える。
- popup は system bar と窓の上、cursor の下に描く（`zwl_glass_draw` の最後）。

### 6.3 pointer

- 題名の bar・システムバーの項目の hit は、最後に描いた frame で記録した矩形で調べる（見えている通りに当たる。App Home や
  デスクトップの slide で layer が動いている間は記録しない）。
- 項目の press: 窓を前面にして（浮いた窓）、その項目の popup を開く。同じ項目の press は閉じる。子の無い top-level の項目は press で
  activation。開いている間（menu mode）は zdesktop が pointer を全部取る: 別の top-level の項目の上に動けばその popup に切り替え、
  行の上で選択、submenu の行の上ではその子を開く。行の上の release（または click）で activation。無効の行と separator は何もしない。
  popup と項目の外の press は全部閉じ、その press は client に渡さない（**決定**: 閉じる click が下の窓を操作しない。GTK・macOS と同じ）。
- activation: `activated` を送り、開いている popup を全部閉じる（開いた順の逆に `closed`）。

### 6.4 keyboard

- **F10**: focus の窓に menu があれば最初の top-level の popup を keyboard で開く（最初の選べる行を選ぶ）。
- menu mode の間 key は全部 zdesktop の: ↑↓ で選べる行（有効、separator でない）を巡る、→ は submenu を開くか次の top-level、
  ← は submenu を閉じるか前の top-level、Enter・Space は activation か submenu、Esc は 1 段閉じる、F10 は全部閉じる、文字は
  その文字で始まる次の行へ。押した key の release も client に渡さない。
- **shortcut の実行**（仕様案 §13）: menu mode でないとき、key の press を zdesktop の system shortcut（Ctrl+Alt+←→ 等、shell.c）の
  後、client の前に、focus の窓の menu の shortcut と照らす。有効で見える item（祖先も有効で見える）の shortcut に合えば activation
  （via=shortcut）で、その press と release を client に渡さない。evdev → keysym は zdesktop の US 配列の表（文字は小文字、
  Shift で記号が変わる key は Shift を外して変わった後の keysym。Ctrl+Shift+= は Ctrl++ に合う）。
- modifier の対応: zdesktop の `wl_keyboard` の mask（shift 0x1、ctrl 0x4、alt 0x8、meta 0x40）→ protocol の shift・ctrl・alt・super。

### 6.5 focus と閉じる条件

- 開いている menu は前面の窓のもの。前面の窓が変わる、窓が docked・浮き を変える、App Home・Wiseview が開く、窓・表示先・model が
  消える、開いている submenu の item が消える・見えなくなる、とき閉じる（その段から下）。
- commit で開いている popup の行は描き直す（選択は item の ID で持つので、並びが変わっても保たれる。消えれば選択なし）。

### 6.6 log（試験が読む）

- `ZWL MENU bar client=C surface=S where=floating|docked item=I offset=X top=Y width=W height=H`（その窓の top-level の配置が
  変わった frame で。offset は浮いた題名の bar の左端から、docked は出力の左端から。overflow は item=0）
- `ZWL MENU open client=C surface=S item=I depth=D x=X y=Y width=W height=H`、`ZWL MENU row item=I depth=D y=Y height=H`
  （popup を開いたとき）
- `ZWL MENU close client=C surface=S item=I depth=D`、`ZWL MENU activate ...`（§5）

実装で足した規則（p003）: 題名の bar の項目は、その点で一番上の窓がその窓のときだけ当たる（`zwl_glass_window_at`。上の窓の
本体に隠れた題名の bar の項目は押せない）。submenu の開いた行の上の motion は開き直さない。

## 7. terminal の menu（p004）

| menu | 項目（shortcut） | 動作 |
| --- | --- | --- |
| Shell | New Window（Ctrl+Shift+N、role new）/ — / Close Window（Ctrl+Shift+Q、role close） | 同じ program を新しい process で起動 / terminal を終える |
| Edit | Copy（Ctrl+Shift+C、copy）/ Paste（Ctrl+Shift+V、paste）/ — / Select All（Ctrl+Shift+A、select_all） | 画面の文字の選択と terminal 内の clipboard（zdesktop に wl_data_device が無いので terminal の中だけ。正直に記録）。Copy は選択があるときだけ、Paste は clipboard があるときだけ有効（動的な enabled） |
| View | Zoom In（Ctrl++）/ Zoom Out（Ctrl+-）/ Normal Size（Ctrl+0）/ Text Size ▸ Small・Medium・Large・Huge（radio）/ — / Fullscreen（F11、checkbox） | font の大きさを変えて grid を作り直し shell に知らせる。radio は今の大きさ、上限・下限で Zoom が無効。Fullscreen は xdg_toplevel の fullscreen、checked は状態 |
| Session | Send Interrupt / Send End of File / — / Clear Screen / Reset Terminal | shell に ^C・^D を書く / 画面を消す / 画面の状態を初期化 |
| Help | About Terminal（role about） | 画面に 1 行の説明を出す（dialog の仕組みが無いので正直な簡単な動作） |

更新は 1 つの transaction にまとめる（選択・clipboard・大きさ・fullscreen が変わった round の終わりに `menu_refresh`）。
activated は `ZTERM MENU item=I action=A` を log に出す。

## 8. 試験

- **build**: zdesktop・libwayland・libkeiland・terminal が warning 0。新しい file は `plan/tools/style-check.py` 0、既存の
  file は悪化させない。
- **Venus（QEMU）**: lean な image（`plan/tools/titlebar/config-amd64-menu.mk`、`build-menu-image.sh`）で
  `plan/tools/titlebar/menu-p003.sh`（仮称）: zdesktop --glass と terminal。(1) 浮いたタイトルバーの menu の画面、(2) Edit を開いた popup の
  画面、(3) Select All → Copy → Paste の activation（log と画面）、(4) View ▸ Text Size の submenu と radio、(5) keyboard（F10、↓、Enter）、
  (6) shortcut（Ctrl+Shift+A、Ctrl+Shift+C）、(7) 外の click で閉じる、(8) docked のシステムバーの menu と popup、(9) 動的な更新
  （Copy の enabled、Fullscreen の checked、Zoom の限界）の commit が log と画面に出る、(10) 2 つ目の窓（New Window）で focus に
  従う。画面は Read で自分で見て判定する。
- **回帰**: `zdesktop-p059.sh`・`p064.sh`・`p070.sh`・`p068.sh` を lean image で（使う program が image にあるもの）。boot test は
  `plan/tools/boot-test.sh`。
- **i915 実機**: p005（任意、`flock /tmp/i915-hw.lock`）。

## 9. Phase への割り当て

| Phase | 内容 |
| --- | --- |
| p002 | libwayland の 3 interface（表、wrapper、listener、非公開 header）、zdesktop の menu.c（request、model、transaction、error、寿命）、`zwl_error_code` |
| p003 | menu-shell.c（題名 bar・システムバーの項目、popup、pointer、keyboard、shortcut、activation、log）、shell.c・seat.c・glass.c の hook |
| p004 | libkeiland の API（鏡と検査、service の発見）、terminal の menu と動作（選択・clipboard・zoom・fullscreen・session・about） |
| p005 | i915 実機、規約の全文との照合、回帰 |

## 10. 共有される file と衝突の危険（統合のとき）

- `userland/desktop/wayland/`（zwl.h、protocol.c、objects.c、wire.c、shell.c、seat.c、glass.c、Makefile）: WS069（X11 server）は
  zdesktop の外の program なので重なりは小さいが、zwl.h の object・server の field の追加は merge で並びがずれうる。
- `userland/desktop/libwayland/`（protocol.c、event.c）: WS069 の xserver が新しい interface を足すなら同じ file。
- `platform/amd64/vmunix.mk`（libkeiland の link、terminal の link）: WS068・WS069 も規則を足す file。
- `include/libc/zdesktop.h`、`userland/desktop/libkeiland/`: WS069 の xserver が libkeiland に何か足すなら重なる。

## 11. 決定（2026-09-27 ユーザーが既定を確定。以前は未決として既定で進めていた）

2026-09-27、ユーザーは以下の既定をそのまま確定した（main の session の伝達）。icon については「アイコンはあとで追加を考えましょう」。

1. **menu を開く key**: F10（GTK・Windows・KDE と gnome-terminal の既定）。terminal の中の program（mc の終了等）は F10 を
   受け取れなくなる（menu のある窓だけ）。
2. **zdesktop が shortcut を実行する**（仕様案 §13 の「持たせることができる」を「持たせる」にした）。client はその key を見ない。
3. **menu の外の click は client に渡さない**: 閉じるだけ。
4. **icon を描かない**（icon theme が無い）。icon は後で考える（ws.md の Future Work の候補）。
5. **label は ASCII**: atlas が ASCII だけ。日本語の label は glyph の cache（WS035 の libtruetype の拡張）が要る。

## 12. 右 click の context menu（WS071 のための余地、2026-09-27 coordinator の連絡）

WS071（ファイルマネージャ、main の tree の plan/ws071/spec.md §15）は、menubar に System Menu を使い、さらに zdesktop が描く右 click の
context menu（Open、Open With、Cut、Copy、Paste、Rename、Duplicate、Move、Tags、Share、Get Info、Delete）を求める。WS070 の範囲は
広げない（この節は設計の余地の記録だけで、実装しない）。

- **今の protocol のままで使える所**: context menu の中身は `xdg_menu_v1` の木そのもの（根の子が行、submenu で「Open With ▸」、
  enabled・visible・role・shortcut・separator も同じ）。client は context menu 用の `xdg_menu_v1` をもう 1 つ作り、transaction で
  中身を差し替えればよい（選ばれた file に合わせて Paste の enabled 等）。zdesktop の描画と操作（menu-shell.c の popup、submenu、
  keyboard、外の press で閉じる）も、top-level の popup を「根の子を行とする popup を任意の点に出す」ものとしてそのまま使える。
- **足りない所（追加が要る）**: menu を「surface の点に、この入力（seat と serial）に答えて出す」request と、その popup の選択・
  閉じたことを返す object が無い。`xdg_toplevel_menu_v1` は窓の menubar に結び付いた常設の表示先で、一度きりの popup ではない。
- **追加の案（version 2、後の WS）**: `xdg_menu_manager_v1.get_context_menu(new_id<xdg_context_menu_v1> id, object<xdg_menu_v1> menu,
  object<wl_surface> surface, int x, int y, object<wl_seat> seat, uint serial)`。zdesktop は serial が最近の press（右 button）の
  ものかを確かめ、surface の座標 (x, y) に根の子の popup を開く。`xdg_context_menu_v1` の event は `activated(item_id, action,
  serial)` と `done()`（選ばれても閉じられても最後に 1 回。client はその後 destroy する）、request は `destroy`（開いていれば閉じる）。
  menubar と同じ `xdg_menu_v1` を渡してもよい（その場合は根の子＝top-level が行になる）。libkeiland には
  `zdesktop_menu_popup(service, menu, surface, x, y, seat, serial, listener, data)` のような 1 つの呼び出しで包む。
- **2026-09-27 実装（ws071-p009）**: 上の version 2 の案のとおり（`bad_surface` の error は作らず、窓でない surface・古い serial は開かずに
  `done`）。記録は [ws071 phase009](../ws071/ws.md)。
- **zdesktop 側の変更の見込み**: menu-shell.c の state に「menubar からでない popup」（hit の無い anchor、parent = 根）を足し、
  閉じたとき `done` を送る。popup の配置・行・keyboard・外の press の扱いは共有できる。
