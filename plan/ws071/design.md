<!-- awesome-plan project=zedbsd record=ws071-design -->

# WS071 設計: zedBSD File Manager（`zdesktop-files`）

Parent: [WS071](ws.md) / Phase: [ws071-p001](phase001/phase.md)
Status: 設計（2026-09-27、ws071-p001）。仕様案は [spec.md](spec.md)（ユーザー提供、§番号は spec.md の節）。仕様案と違える所・仕様案に
無い所は各節に**決定**と理由を書いた。人間の判断が要る点は §15「判断が要る点（既定で進めた）」に置き、戻せる既定を選んで先へ進める。

## 0. 前提（再確認しない決定）

- client は標準の Wayland と Vulkan を使い、zdesktop の非標準の拡張（System Menu 等）は **libzdesktop の API だけ**で使う（ユーザーの規則）。
- menubar（File・Edit・View・Go・Window・Help）は WS070 の System Menu。右 click の context menu は system が描く: WS070 の protocol に
  version 2 の `xdg_menu_manager_v1.get_context_menu` と `xdg_context_menu_v1` を足し（[ws070 design.md §12](../ws070/design.md) の案）、
  libzdesktop の `zdesktop_menu_popup` で包む。**WS070 の protocol の拡張を WS071 の Phase（p009）として実装する**。
- font と壁紙は commit しない（`build/ws035-fonts`、`build/ws035-wallpaper`。日本語の fallback font も同じ扱い、§4.3）。
- 最初の版の範囲は [ws.md](ws.md) の「最初の版」。クラウド・SMB/NFS/WebDAV・カラム/ギャラリー表示・全体の indexer・動画/PDF の
  サムネイルは Future Work の候補（ws.md に列挙）。

## 1. program と置き場所

- **決定**: `userland/base/zdesktop-files`、`/bin/zdesktop-files`。窓の題名 `Files`、app_id `zdesktop-files`。zdesktop の App Home に
  「Files」を 1 行足す（`home.c` の `home_add_app`、p011）。理由: `zdesktop-terminal` と同じ命名（zdesktop の base の client）。
- 1 process = 1 窓（中にタブ）。New Window（Ctrl+N）は自分を新しい process で起動する（zdesktop-terminal の New Window と同じ）。
  理由: 窓ごとに Vulkan の swapchain と event の流れを分けずに済み、1 つの窓の不具合が他の窓を巻き込まない。process の間で共有するもの
  （clipboard、tag、recent、sidebar）は file に置く（§6）。

## 2. 描画の構成

```
zdesktop-files
  window.c   Wayland: registry、xdg_toplevel、wl_seat の pointer と keyboard、key の repeat → fm_event の列
  present.c  Vulkan: swapchain と、CPU が描いた canvas（host-visible の linear image）を 1 枚の四角で貼る pipeline
  canvas.c   CPU の 2D 描画: premultiplied BGRA、clip、矩形・角丸（AA）・影・gradient・多角形（AA）・画像の拡縮
  text.c     libtruetype の face（本文 + fallback）、glyph の cache、UTF-8、計測・省略・描画
  icons.c    folder・書類・種類別・sidebar・toolbar の icon を canvas の図形で描く（icon theme も画像資産も使わない）
  ui*.c      画面の配置・描画・hit test・入力の解釈（toolbar、sidebar、home、grid、list、preview、Quick Look、情報、dialog）
  model      dir.c・nav.c・select.c・task.c・trash.c・undo.c・tags.c・mime.c・apps.c・search.c・thumb.c・places.c・clip.c
  menu.c     System Menu（menubar）と context menu（libzdesktop）
```

- **決定: 描画は CPU（canvas）、表示は Vulkan。** 各 frame は canvas（窓と同じ大きさの BGRA）を CPU で描き、Vulkan の linear image に
  写して（host-visible の map に直接描く）、fragment shader が nearest で 1 枚の四角として swapchain に貼る。理由:
  1. **host で画面を試験できる**: UI の描画と入力の解釈が Wayland と Vulkan から切り離され、host の試験（`plan/ws071/tests/`）が同じ
     code で窓の絵を PNG に描いて比べ、目で見られる。guest を起動する前に大半の不具合を見つけられる。
  2. file manager の絵（角丸・影・gradient・文字・縮小画像）は GPU の頂点の組より CPU の図形の方が素直に書け、Venus と i915 の
     shader compiler の差（i915 の native compiler は `gl_VertexIndex` 等を取らない）に触れない。shader は texture を 1 枚貼るだけ。
  3. 描き直しは入力と task の進みのときだけ（animation は持たない）で、1 frame の CPU 時間は問題にならない見込み（性能の数値は
     受け入れに使わない）。問題になれば damage の矩形だけ描き直す（Future Work 候補 F-f）。
  代案（GPU の 2D renderer: SDF の角丸と glyph atlas）は後で present.c と canvas の境界を保ったまま差し替えられる。
- zdesktop-terminal の Vulkan の骨格（device、swapchain、FIFO、fence で 1 frame ずつ、PREINITIALIZED→GENERAL）をそのまま使う。
  canvas の image は窓の大きさで作り、resize で作り直す（`vkDeviceWaitIdle` の後）。shader は `shaders/canvas.vert`・`canvas.frag`、
  `shaders/regenerate.py` が `shaders.h` を作る（terminal と同じ。build は shader compiler を要らない）。
- **共有の UI library は作らない（今は）**: zdesktop-terminal と重なるのは Vulkan の初期化と Wayland の骨格だけで、描き方が違う
  （terminal は cell の atlas）。2 つ目の CPU 描画の client が出たら canvas・text・icons を library（例 `libzdesktop-ui`）へ出す
  （Future Work 候補 F-g）。
- **窓の透明**: zdesktop は GPU buffer（Vulkan）の窓を不透明として合成し、角丸と影は zdesktop が付ける（`shell.c` の body の描画）。
  client の中で壁紙のすりガラスは見えないので、本体は明るく静かな不透明の面（淡い青灰の gradient）と、白に近い角丸の panel で
  「すりガラス風」を表す。**窓ごとの本当のすりガラス**（alpha の Vulkan surface の後ろに zdesktop が blur を敷く）は zdesktop の機能で、
  Future Work 候補 F-h（WS035 の blur の後）。

### 2.1 event と frame の流れ

- window.c は Wayland の event を `struct fm_event`（pointer の enter/leave/motion/button/axis、key の press/release と evdev code、
  modifier、時刻、button の serial）に変えて列に置く。key の repeat は window.c（zdesktop は repeat しない）。
- main loop: `poll`（Wayland の fd と timer）→ event を UI に渡す → task と search を時間の枠（1 回 10 ms 程度）で進める → 変化が
  あれば canvas を描き present。task や search が動いている間は poll の timeout を 0〜16 ms にする。
- UI の entry point は `fm_ui_event(app, event)`・`fm_ui_tick(app, now)`・`fm_ui_draw(app, canvas)` の 3 つだけで、host の試験は
  window.c・present.c・menu.c の代わりに script の event を与えて canvas を PNG に書く。

## 3. 画面の構成（spec §2〜§12、§28、§32、§38）

zdesktop が描く浮いたタイトルバーに題名「Files」と menubar（File Edit View Go Window Help）と — □ ×（spec §36）。client の本体:

```
┌──────────────────────────────────────────────────────────────────────┐
│ ( ‹ )( › )( ⌂ )  Home › Projects › zedBSD     [ 🔍 Search       ] ▦ ≡ ◨ ◔ │  toolbar（浮いた pill、本体の上端から 10 px）
│ ┌─────────────┐ ┌──────────────────────────────────────────┐┌──────┐ │  tab bar（タブが 2 つ以上のときだけ）
│ │ Favorites   │ │                                          ││ prev │ │
│ │ ⌂ Home      │ │  content: home dashboard / icon / list    ││ iew  │ │
│ │ ▭ Desktop   │ │                                          ││ pane │ │
│ │ …           │ │                                          ││      │ │
│ │ Locations   │ │                                          ││      │ │
│ │ Tags        │ │           ( 3 items selected — 42.8 MB )  ││      │ │  status の pill（要るときだけ）
│ └─────────────┘ └──────────────────────────────────────────┘└──────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

- **色**（静か、色は選択とタグだけ、spec §38）: 本体の地 `#EEF2F7`→`#E8EDF5`（縦の gradient）、sidebar は地の上の淡い panel
  （`#F7F9FC`、alpha 風に地と混ぜた色）、content は白 `#FFFFFF`（角 16 px、細い縁 `#E2E7EF`、淡い影）、文字 `#1E2632`、薄い文字
  `#6B7585`、accent `#2F7CF6`、選択の地は accent の 16%（文字は濃いまま）、focus の外の選択は灰 `#E4E8EE`。
- **toolbar**（spec §3）: 高さ 44 px の白い pill（角 22 px、影）。‹ › ⌂ は 32 px の丸いボタン（押せないときは薄い）。パンくず
  （spec §6）は各段が押せる。幅が足りなければ先頭から「…」に畳む。Ctrl+L でパンくずが編集できる path の欄に変わる。検索欄
  （spec §7）は角丸の欄に虫眼鏡と placeholder「Search」。右に ▦（icon）≡（list）の切替、◨（preview pane）、task があるときだけ
  進みの輪（spec §33）。窓の操作（— □ ×）は zdesktop の題名 bar にあるので toolbar に置かない（spec §3 の例との違い、理由は
  zdesktop が描くため）。
- **sidebar**（spec §8）: 幅 208 px。見出し Favorites（Home、Desktop、Documents、Downloads、Pictures、Music、Movies + 利用者が
  足した folder）、Locations（Recents、Trash、Computer（`/`）、mount された volume）、Tags（色の丸と名前）。選択は accent の淡い
  pill と accent の icon・文字。クラウド（spec §8 のクラウド節）は出さない（Future Work）。
- **content**: 場所が Home なら dashboard（§3.1）、folder・Recents・Trash・タグ・検索の結果は icon か list（§3.2）。
- **preview pane**（spec §17）: 幅 260 px、View > Show Preview か ◨ で出し入れ。選択が 1 つならサムネイルか大きな icon、名前、
  種類、大きさ、更新日時、タグ、簡易内容（text は先頭の 12 行、画像は縮小）。選択が複数なら数と合計。
- **status の pill**（spec §32）: content の下端の中央に、選択が 2 つ以上か task のあるときだけ薄い pill（「3 items selected — 42.8 MB」、
  「Copying 120 of 450 files」）。常設の status bar は持たない。
- **tab bar**（spec §30）: タブが 2 つ以上のときだけ toolbar の下に pill のタブを並べる（題名は場所の名前、× で閉じる）。

### 3.1 Home の dashboard（spec §5、§19、§28）

- **hero card**: content の上の幅いっぱい、高さ 180 px、角 18 px。絵は `/usr/share/zdesktop/wallpaper.ppm`（デスクトップの壁紙。
  commit しない資産で image に入る）を card に合わせて切り出し縮小したもの、無ければ canvas の gradient と多角形で描く山と湖。
  文言は時刻の挨拶と利用者名（「Good afternoon, awe」）と要約（「4 files changed today · 118 GB free」）。mockup の宣伝文句は使わない
  （§15-8）。
- **folder cards**: Documents、Pictures、Music、Movies、Projects（`~/Projects` があれば）、Downloads。各 card に青い folder の icon、
  名前、項目数（「12 items」）。押すとその folder へ。
- **Recent files**（見出しの右に「Show all →」で Recents へ）: recent の database（§6.2）の新しい順の 8 件: icon かサムネイル、名前、
  場所（「Documents / Projects」）、時刻（「Today 16:20」「Yesterday 20:18」「Sep 21」）。
- **Recent folders**: file manager が開いた folder の新しい順の 6 件を小さい pill で。
- ピン留め・おすすめ（spec §28）は Favorites（sidebar）で代える。dashboard の独立した pin は Future Work。

### 3.2 icon 表示と list 表示（spec §10〜§13）

- **icon**: 格子の cell 108×118 px。icon 64 px（folder は青い folder、file は種類の色の帯と拡張子の書類、画像はサムネイル）、
  名前（2 行まで、中央、はみ出しは末尾を「…」）、副題（folder は「12 items」、file は「2.4 MB」）。
- **list**: 行 28 px。列 Name（icon 18 px 付き）、Kind、Size、Date Modified を既定で出し、View > Columns で Date Created
  （UFS の birth time が無いので ctime を「Changed」として出す）、Tags、Owner を出し入れする。見出しの click で並べ替え（もう一度で逆順）。
- 並べ替え: Name（自然順、folder が先）、Kind、Size、Date。隠し file（`.` で始まる）は View > Show Hidden Files（Ctrl+H）。
- 選択（spec §13）: click で 1 つ、Ctrl+click で足し引き、Shift+click で範囲、空き地からの drag で矩形選択（Ctrl で足す）。
  選択は accent の淡い地。keyboard: 矢印で移動、Shift+矢印で広げる、Home/End、文字の入力で名前の先頭が合う項目へ（type-ahead）。
- スクロール: wheel と keyboard（PageUp/Down）。細い scroll bar を右端にスクロール中だけ出す。

## 4. 部品

### 4.1 canvas（CPU の 2D）

- pixel は premultiplied の BGRA8（Vulkan の `B8G8R8A8_UNORM` と同じ並び）。clip の矩形の stack。
- 図形: 矩形、角ごとの半径を持つ角丸（符号付き距離で 1 px の AA）、角丸の縁、影（角丸の距離を半径 r で柔らげた近似の gauss）、
  縦・横の線形 gradient、円、多角形（非ゼロ規則、縦 4 本の副走査線と横の厳密な被覆の scanline 法）、太さのある折れ線
  （多角形に変える）、画像の拡縮（bilinear、縮小は box の平均）と角丸の clip。
- 大きい絵（hero card）は別の canvas に 1 度描いて cache し、大きさが変わったときだけ描き直す。

### 4.2 text

- font: `/usr/share/fonts/zdesktop.ttf`（Inter、zdesktop と同じ）、fallback `/usr/share/fonts/zdesktop-fallback.ttf`（任意。
  日本語の file 名を読むため、§15-9）。`--font=`、`--fallback-font=` で変えられる。
- glyph は (face, 大きさ, glyph 番号) の hash の cache に coverage の bitmap で持つ。大きさは 11〜28 px の数段。太字は font が 1 本
  なので、coverage を横に 1 px 広げた擬似の太字（見出しだけ）。
- UTF-8 の復号（不正な byte は U+FFFD）、幅の計測、幅に収める末尾の「…」、2 行の折り返し（icon の名前）。
- 入力は US 配列の evdev → 文字（zdesktop-terminal の keys.c と同じ考え）。IME は無い（日本語の入力は Future Work、§15-9）。

### 4.3 icons

- 全部 canvas の図形で描く（資産を持たない）: folder（2 色の gradient とタブ）、書類（角の折れ、種類の色の帯と拡張子の文字）、
  Home・Desktop・Documents・Downloads・Pictures・Music・Movies・Recents・Trash・Computer・Volume・Tag・Back・Forward・Search・
  Grid・List・Preview・進みの輪。種類の色: 画像 緑、音 桃、動画 紫、text 灰、source 青緑、archive 茶、PDF 赤、実行 file 濃い灰。

## 5. model

### 5.1 場所と履歴（spec §4〜§6）

- 場所（`struct fm_location`）: 種類（HOME dashboard、DIRECTORY path、RECENTS、TRASH、TAG 名、SEARCH query と scope）と値。
- タブごとの履歴は場所の配列と今の位置（ブラウザと同じ: 新しい場所へ行くと先の分を捨てる）。Back・Forward は履歴を動き、
  階層の親は Go > Enclosing Folder（Ctrl+Up）。各場所は最後の選択と scroll の位置を覚える。
- 開いている folder は、画面に戻るたびと 2 秒ごとに `stat` の mtime で変化を見て読み直す（kqueue 等の通知は使わない。
  §15-10）。

### 5.2 一覧（dir.c）

- `readdir` と `lstat`（symlink は先の `stat` も見て folder かどうか）。項目: 名前、種類（folder、file、symlink、他）、大きさ、
  mode、uid・gid、mtime・ctime、MIME（拡張子で。中身の判定は preview・open のときだけ）、タグ（xattr、§6.1、読むのは表示中の
  folder の項目だけ）、folder の項目数（icon 表示の副題。遅延で数える）。

### 5.3 task（spec §33）

- **決定: task は main loop の中で協調的に進める**（thread を使わない）。1 回の枠で copy なら 256 KiB ずつ、削除なら項目ずつ、
  枠の時間（10 ms）を超えたら戻る。理由: model が単一 thread のままで、試験が決定的になり、UI の応答は枠の長さで守れる。
- 種類: copy、move（同じ device は `rename`、違えば copy + 削除）、trash（§5.4）、restore、delete（完全）、empty trash、duplicate、
  link（symlink を作る、DnD の Ctrl+Shift）。
- 手順: 走査（数と bytes の合計）→ 実行（directory の再帰は iterator の stack、file は open/read/write、mode と mtime と xattr を写す、
  symlink は symlink のまま）→ 終わり（undo の記録、表示の更新、log）。
- 衝突: 行き先に同じ名前があれば「name 2.ext」「name 3.ext」…と両方残す（Finder の Keep Both）。上書き・Skip の dialog は
  Future Work（§15-11）。自分の中への copy・move は拒む。
- 取り消し: 進みの popover の × で止め、途中の file は消す（copy）か、そこまでで止める（move・trash は項目ごとに完結）。
- 失敗: 最初の失敗の理由（「Permission denied: /x/y」）を task に残し、残りの項目は続ける。終わりに status の pill に出す。

### 5.4 ゴミ箱（spec §23、§24）

- **決定: freedesktop.org Trash specification 1.0 の home trash**（`$XDG_DATA_HOME/Trash`、既定 `~/.local/share/Trash`）。
  `files/NAME` と `info/NAME.trashinfo`（`[Trash Info]`、`Path=`（URL の % の escape）、`DeletionDate=YYYY-MM-DDThh:mm:ss`）。
  名前の衝突は `NAME.2`、`NAME.3`…。理由: 仕様が公開され、GTK・Qt の file manager と互換（後で GTK4・Qt6 のアプリが同じゴミ箱を
  使える）。他の volume の file も home trash へ（copy + 削除の task）。`$topdir/.Trash-$uid` は Future Work。
- Trash の場所は list 表示で Name、Original Location、Date Deleted、Size。Put Back（元の場所へ。衝突は Keep Both、親が無ければ作る）、
  Delete Immediately、Empty Trash（確認の dialog）。
- 完全削除（Shift+Delete、Delete Immediately、Empty Trash）は確認の dialog（窓の中の card、Cancel と Delete）を出す（spec §23）。

### 5.5 Undo（spec §31）

- 窓（process）ごとの undo と redo の stack（各 64 件）。記録: rename（旧→新）、move（元の path の組）、trash（元の path と trash の
  名前）、restore、tags（path、旧、新）、new folder（path）、copy・duplicate（作った path。undo はゴミ箱へ）。
- undo の前に今の状態が記録と合うか（file が記録の場所にあるか）を確かめ、合わなければ「Can't undo: … was moved or deleted」を
  status の pill に出して何もしない。完全削除は undo できない（確認の dialog でそう書く）。

### 5.6 clipboard（spec §15 の Cut・Copy・Paste）

- zdesktop に `wl_data_device` が無いので、**file の clipboard** `$XDG_RUNTIME_DIR/zdesktop-files.clipboard`（1 行目 `copy` か
  `cut`、以後 path を 1 行ずつ）。同じ利用者の zdesktop-files の窓（process）の間で共有する。他のアプリとの clipboard は
  zdesktop の data device の後（Future Work 候補 F-i）。
- Cut は印を付けるだけで、Paste のときに move する（Finder・Explorer と同じ。切り取った項目は薄く描く）。

## 6. 保存先（spec §9、§19、§20）

### 6.1 タグ

- **決定: file のタグは extended attribute `user.zdesktop.tags`**（値はタグの名前を改行で区切った UTF-8）。UFS と tmpfs は xattr を
  持つ（`src/drivers/fs/ufs.c` の `ufs_getxattr` 等、libc の `getxattr`）。理由: タグが rename・move で file と一緒に動き、database
  との食い違いが起きない（macOS の Finder のタグと同じ考え）。copy は xattr を写す（§5.3）。xattr の無い file system では
  タグを付けられない（エラーを出す）。
- タグの定義（名前と色、任意の icon の名前）: `$XDG_CONFIG_HOME/zdesktop/tags`（`name<TAB>#rrggbb[<TAB>icon]` の行）。無ければ既定の
  5 つ（Work 青、Personal 紫、Ideas 桃、Reference 橙、Archive 灰。mockup の 仕事・プライベート・アイデア・参考・アーカイブ）。
- sidebar のタグの絞り込みのための索引: `$XDG_DATA_HOME/zdesktop/tag-index`（`tag<TAB>path`）。zdesktop-files がタグを変えるたびに
  更新し、表示するときに各 path の xattr を確かめて食い違いを落とす。他の道具が付けたタグは、検索（`tag:name`）で見つかる。
- API はアプリの中（`tags.c`）。2 つ目の使い手が出たら libzdesktop へ出す（libzdesktop の「最初の使い手と一緒に足す」方針）。

### 6.2 最近の項目（アプリ横断の recent database、spec §19）

- **決定: libzdesktop に recent の API を足す**（アプリ横断なので。libzdesktop は OS への道でもある）:

```c
struct zdesktop_recent_item {
	char path[ZDESKTOP_RECENT_PATH_MAX];		/* 4096 */
	char application[ZDESKTOP_RECENT_NAME_MAX];	/* 64: 使ったアプリの app_id */
	int64_t time;					/* 使った時刻（Unix の秒） */
};
int zdesktop_recent_add(const char *path, const char *application);
int zdesktop_recent_list(struct zdesktop_recent_item *items, size_t capacity, size_t *count);	/* 新しい順 */
int zdesktop_recent_remove(const char *path);
```

- 保存: `$XDG_DATA_HOME/zdesktop/recent`（既定 `~/.local/share/zdesktop/recent`）、`time<TAB>application<TAB>path` の行、古い順。
  追加は同じ path を消して末尾へ、256 件まで。書き込みは lock file の `flock` の中で一時 file に書いて `rename`（読み手は常に完全な
  file を見る）。`ZDESKTOP_VERSION` を 3 に上げる（context menu と recent）。
- zdesktop-files は file を開いたとき `zdesktop_recent_add(path, "zdesktop-files")`。**変更（p005・p006）**: 最近開いた folder は
  アプリ横断の list に入れず、file manager の中の list `$XDG_DATA_HOME/zdesktop-files/recent-folders`（新しい順に 12）に置く
  （folder の移動のたびにアプリ横断の list が folder で埋まるのを避ける）。

### 6.3 sidebar と設定

- Favorites の並び: `$XDG_CONFIG_HOME/zdesktop-files/sidebar`（path の行）。無ければ既定の 7 つ。drag で並べ替え・folder を足す、
  context menu の Remove from Sidebar（spec §9）。
- 表示の設定（icon/list、列、並べ替え、preview、隠し file）: `$XDG_CONFIG_HOME/zdesktop-files/settings`（`key=value`）。
- 既定の folder（Desktop、Documents、Downloads、Pictures、Music、Movies）は `$HOME` の下の英語の名前。無い folder は sidebar に
  薄く出し、押すと作るかを聞かずに作らない（「Documents does not exist」を出す）。

## 7. 開く・別のアプリで開く（spec §14）

- **MIME の判定**（`mime.c`）: 拡張子の表（約 80: text、source、画像、音、動画、archive、PDF、font、3D model 等）→ 表に無ければ
  中身の先頭 4 KiB の magic（ELF、`#!`、PNG、JPEG、GIF、PDF、zip、gzip、xz、PPM）→ NUL が無く UTF-8 として正しければ `text/plain`、
  他は `application/octet-stream`。folder は `inode/directory`。`file` の magic database は使わない（base の `file` の出力を
  解析するより小さく決定的）。
- **関連付け**（`apps.c`）: 内蔵の既定 + `/etc/zdesktop/open-with` + `$XDG_CONFIG_HOME/zdesktop/open-with`（後が優先）。行は
  `pattern<TAB>name<TAB>command`（pattern は `text/*` のような MIME の glob、command の `%f` は shell の quote をした path）。
  最初に合うものが既定、合うもの全部が Open With の submenu。内蔵の既定:

| MIME | 既定 | 他 |
| --- | --- | --- |
| `inode/directory` | この窓で開く | New Tab、New Window |
| `image/*` | Quick Look（file manager の中の大きな表示） | — |
| `text/*`、source、script | Terminal で `less` | Terminal で `vi`・`remacs`（実行 file があるとき） |
| 実行 file（mode の x、ELF、`#!`） | Terminal で実行 | Terminal で `less` |
| `model/obj` 等 | `mview --windowed`（あるとき） | — |
| その他 | Terminal で `less` | — |

- 起動は `fork` と `setsid` と `execl("/bin/sh", "sh", "-c", command)`（zdesktop の App Home と同じ）、Terminal は
  `/bin/zdesktop-terminal --command=...`。開いた file は recent に足す（§6.2）。
- **変更（p012、2026-09-27）**: 一覧の優先は利用者 → system → 内蔵（同じ名前は先のもの）。PATTERNS は `,` で区切った MIME の glob
  （`fnmatch`）。command の先頭の `@terminal ` は残りを新しい zdesktop-terminal の `--command=` で（shell の quote が 2 重にならない）、
  `@quicklook` は Quick Look。`%f` が無い command は末尾に path を足す。一覧は開くたびに読む（編集がすぐ効く）。起動は fork を 2 段に
  して孫が走らせる（file manager が待つ子を残さない、stdin・stdout・stderr は `/dev/null`）。内蔵の表は Quick Look（`image/*`）、
  Terminal (less)（text と source）、Remacs・Terminal (ed)（`/bin`・`/usr/bin`・`/usr/local/bin` に実行 file があるとき）、
  最後に全部に Terminal (less)。`mview` は変換済みの model の directory しか読まない（`--model=DIR`）ので内蔵の表に入れない
  （model の file は Terminal (less) で開く）。`/bin/vi` は base に無いので出さない。

## 8. 検索（spec §7）

- 検索欄に打つとすぐ（最後の入力から 150 ms 後）始め、結果は list 表示（Name、Location、Kind、Date Modified）に増えていく。
  scope は欄の下の 3 つの chip: This Folder（今の folder から再帰）、Home、Computer（`/`、`/proc`・`/dev` 等の仮想の mount は除く）。
- 語: 空白で区切った語の全部に合う項目。`tag:NAME`（タグ）、`.ext`・`*.ext`・`ext:png`（拡張子）、`kind:image|text|audio|video|folder`
  （種類）、他は名前の部分一致（ASCII の大小を区別しない）。metadata は種類・タグまで。**file の中身の検索**と全体の indexer は
  Future Work（spec §34）。
- 走査は task と同じ協調（1 回の枠に項目 N 個）、結果は 5000 件まで。Esc か欄を空にすると元の場所へ戻る。

## 9. プレビュー・Quick Look・情報（spec §17、§18、§21）

- **サムネイル**（`thumb.c`）: 画像（PNG、PPM/PGM）を 256 px の長辺に縮小して memory の LRU（64 枚）に持つ。作るのは表示中の
  項目だけ、1 回の枠に 1 枚。PNG の decode は libpng-compat（§11）。JPEG・GIF・動画・PDF は Future Work。disk の thumbnail cache
  （freedesktop の thumbnail 仕様）も Future Work。
- **Quick Look**（spec §18）: Space で開閉。**決定: 窓の中の overlay**（content の上を暗くし、中央に大きな白い card: 画像は収まる
  最大、text は等幅でなく本文の font で先頭 60 行、他は大きな icon と情報）。←→ で選択の中を移る。Esc でも閉じる。理由: zdesktop に
  xdg_popup・subsurface が無く、「別窓でなく中央に overlay」（spec）を client の中で満たせる。compositor が描く system の preview は
  新しい protocol が要る（Future Work 候補 F-j、§15-7）。
- **Get Info**（spec §21、Ctrl+I）: Quick Look と同じ overlay の card に表: Name、Full path、Kind、MIME type、Size（bytes 付き）、
  Changed（ctime）、Modified、Accessed、Owner（`getpwuid`・`getgrgid` の名前と数）、Permissions（`rwxr-xr-x` と 8 進）、Tags、
  Checksum（SHA-256。「Compute」を押すと task で計算）、Extended attributes（名前と大きさ）。作成日時は UFS が birth time を
  返さない（`struct stat` に無い）ので「—」と書く。

## 10. System Menu と context menu（spec §15、§36、§37）

### 10.1 menubar（libzdesktop の `zdesktop_menu_*`）

label は ASCII（zdesktop の atlas の制限、WS070 §11-5）。shortcut を登録すると zdesktop がその key を取るので、
**修飾 key の付いた組だけを登録**し、F2・Delete・Space・Backspace・Enter・矢印は client が自分で扱う（検索欄や名前の編集の中で
Delete 等が効かなくなるのを避ける）。Ctrl+C・X・V・A・Z は menu の activation として届き、text の欄に focus があればその欄の
編集（切り取り・copy・貼り付け・全選択・取り消し）に回す（macOS の first responder と同じ）。

| menu | 項目（shortcut） |
| --- | --- |
| File | New Window（Ctrl+N）、New Tab（Ctrl+T）、New Folder（Ctrl+Shift+N）、Open（Ctrl+O）、Open With ▸、— 、Get Info（Ctrl+I）、Move to Trash、— 、Close Tab（Ctrl+W）、Close Window（Ctrl+Shift+W） |
| Edit | Undo（Ctrl+Z）、Redo（Ctrl+Shift+Z）、— 、Cut（Ctrl+X）、Copy（Ctrl+C）、Paste（Ctrl+V）、Duplicate（Ctrl+D）、Select All（Ctrl+A）、— 、Rename、Tags ▸（タグごとの checkbox） |
| View | Icons（Ctrl+1、radio）、List（Ctrl+2、radio）、Columns・Gallery（無効、将来）、— 、Sort By ▸（Name・Kind・Size・Date Modified の radio）、List Columns ▸（checkbox）、— 、Show Sidebar（Ctrl+Alt+S）、Show Preview（Ctrl+Alt+P）、Show Hidden Files（Ctrl+H） |
| Go | Back（Alt+Left）、Forward（Alt+Right）、Enclosing Folder（Ctrl+Up）、— 、Home（Ctrl+Shift+H）、Desktop（Ctrl+Shift+D）、Documents（Ctrl+Shift+O）、Downloads（Ctrl+Shift+L）、Recents（Ctrl+Shift+R）、Computer（Ctrl+Shift+C）、Network（無効、将来）、Trash、— 、Go to Location…（Ctrl+L）、Find（Ctrl+F） |
| Window | Minimize（Ctrl+M）、Zoom、— 、Previous Tab（Ctrl+Shift+Tab）、Next Tab（Ctrl+Tab） |
| Help | File Manager Help、Keyboard Shortcuts、— 、About Files |

選択・clipboard・undo・タブの数に合わせて enabled・checked を 1 transaction で更新する（WS070 の terminal と同じ）。
Help の 3 つは Quick Look と同じ overlay の card に text を出す。

### 10.2 context menu（WS070 protocol の version 2、p009）

- protocol（ws070 design.md §12 の案を確定）:
  - `xdg_menu_manager_v1` version 2 に request `get_context_menu`（opcode 3）: `new_id<xdg_context_menu_v1> id`、
    `object<xdg_menu_v1> menu`、`object<wl_surface> surface`、`int x`、`int y`、`object<wl_seat> seat`、`uint serial`。
  - `xdg_context_menu_v1`: request `destroy`（0、開いていれば閉じる）。event `activated(uint item_id, uint action, uint serial)`（0）、
    `done()`（1、選ばれても閉じられても最後に 1 回）。
  - zdesktop は serial がその client に送った最近の button の press のものか（最後の 1 つ）を確かめ、違えば開かずに `done` を送る
    （protocol error にしない: 古い serial の race で client を殺さない）。popup の行は `menu` の根の子。位置は surface の (x, y)、
    出力の端で押し戻す。keyboard・submenu・外の press で閉じる、は menubar の popup と同じ（menu-shell.c の共有）。
  - error（`xdg_menu_manager_v1`）: `bad_surface = 1`（toplevel の窓でない surface）。
- libwayland: `menu-protocol.c` に interface と wrapper、非公開 header に宣言。libzdesktop:

```c
struct zdesktop_context_menu;
struct zdesktop_context_menu_listener {
	void (*activated)(void *data, struct zdesktop_context_menu *popup, uint32_t item, uint32_t action, uint32_t serial);
	void (*done)(void *data, struct zdesktop_context_menu *popup);
};
struct zdesktop_context_menu *zdesktop_menu_popup(struct zdesktop_menu_service *service, struct zdesktop_menu *menu,
	struct wl_surface *surface, int32_t x, int32_t y, struct wl_seat *seat, uint32_t serial,
	const struct zdesktop_context_menu_listener *listener, void *data);	/* NULL・ENOTSUP は version 1 の zdesktop */
void zdesktop_context_menu_destroy(struct zdesktop_context_menu *popup);
```

- zdesktop-files の context menu（spec §15、label は英語）: 項目の上: Open、Open in New Tab・Open in New Window（folder）、
  Open With ▸、— 、Cut、Copy、Paste（folder の中へ）、— 、Rename、Duplicate、Move To ▸（Favorites）、— 、Tags ▸、Share（無効、将来）、
  — 、Get Info、Move to Trash。空き地: New Folder、Paste、— 、View ▸、Sort By ▸、Show Hidden Files、— 、Get Info（今の folder）。
  Trash の中: Put Back、Delete Immediately、— 、Empty Trash。sidebar: Open in New Tab、Open in New Window、Remove from Sidebar、Get Info。
  context menu 用に menubar とは別の `zdesktop_menu` を 1 つ持ち、右 click のたびに中身を transaction で作り直して popup する。
- fallback: version 1 の zdesktop（`ENOTSUP`）では context menu を出さず、log に 1 行書く（同じ操作は menubar と keyboard で出来る）。

## 11. サムネイルの decoder（PNG）

- 既存の userland に PNG の decoder は無い（WS035 の p040 libz-compat・p041 libpng-compat は planning のまま。zdesktop の壁紙は PPM）。
- **決定: WS035 の決定（D2〜D4: base のプログラムは `userland/base/libz-compat`・`libpng-compat` を使う、header は
  `include/libc/compat/`、関数名は本家と同じ、libpng は simplified API だけ）に従い、その decode の半分を WS071 p010 で作る**:
  libz-compat の `inflate`（`inflateInit`・`inflate`・`inflateEnd`、zlib の header と adler32、動的・固定 Huffman、stored）と
  libpng-compat の `png_image_begin_read_from_file`・`png_image_begin_read_from_memory`・`png_image_finish_read`・`png_image_free`
  （8 bit の gray・gray+alpha・RGB・RGBA・palette（tRNS）、16 bit は上位 byte、interlace（Adam7）は読まずに失敗）。encode
  （deflate、`png_image_write_to_file`）は WS035 の p040・p041 に残す。外部の package は使わない（自前の実装、ライセンスは Zlib）。
  WS035 の記録との照合は main session に頼む（merge の注記）。

## 12. ドラッグ&ドロップ（spec §16）

- zdesktop に `wl_data_device` が無い（`protocol.c` の globals、WS070 design.md §7 の terminal の clipboard の注記）ので、**窓の中の
  DnD** だけを作る: 項目を folder（grid・list）、sidebar の folder・タグ・Trash、パンくずの段、tab に落とす。修飾: 無し = move（同じ
  device）か copy（違う device）、Ctrl = copy、Ctrl+Shift = link（Finder の Option・Cmd+Option の代わり）。drag の間は項目の小さい
  絵と「+」の印が pointer に付く。sidebar の Favorites へ folder を落とすと足す、Favorites の中の drag で並べ替え（spec §9）。
- 窓の間・アプリ・デスクトップへの DnD は zdesktop の data device が要る（Future Work 候補 F-i）。

## 13. 装置と mount（spec §25）

- Locations に Computer（`/`）と、`setmntent(MOUNTED)` の mount のうち仮想でないもの（`ufs`、`fat`、`msdos`、`iso9660` 等。`proc`・
  `devfs`・`tmpfs` は出さない）を「名前（mount point の最後の段）」で出す。`statvfs` で空き容量を情報に出す。unmount・eject は
  Future Work（mount の権限と仕組みが要る）。

## 14. 試験

- **host**（`plan/ws071/tests/`、guest を起動しない）:
  - `host-build.sh`: model と UI の file を host の cc で `build/ws071-host/` に build（window.c・present.c・menu.c は除き、試験の
    `host-main.c` が event を与える）。libtruetype も host で build（その source は host の libc で compile できる）。
  - `host-model`: dir・nav・select・task（copy・move・trash・restore・delete・衝突・自分の中への copy の拒否・xattr の copy）・undo・
    tags・mime・apps（関連付けの読み込み）・search・recent（libzdesktop の recent.c を host で）を一時 directory で確かめる。
  - `host-render`: 決まった内容の一時の `$HOME` で、場面（dashboard、icon、list、選択、rename、検索、preview、Quick Look、Info、
    dialog、進み）を PNG に描き（`build/ws071-host/*.png`）、目で見る。数値の比較は画素の粗い検査（背景・選択の色の画素がある等）まで。
- **guest（Venus、QEMU）**: lean な image（`plan/ws071/tests/config-amd64-files.mk`、`build-files-image.sh`、ws070 の構成に
  zdesktop-files と試験の file を足す）。`files-guest.sh start` の後、Phase ごとの `files-p00N.sh` が zdesktop --glass と zdesktop-files
  を起動し、QMP の pointer・key で操作し、zdesktop-files の log（`ZFILES READY`、`ZFILES LOCATION`、`ZFILES TASK`、`ZFILES MENU` 等、
  guest の中の file を SSH で読む）で待ち合わせ、画面（QMP の screendump）を Read で自分で見て判定する。QEMU の console・serial の
  log では判定しない。
- 起動の確認は `plan/tools/boot-test.sh`（最後の Phase）。i915 の実機は `flock /tmp/i915-hw.lock` の下でだけ（しなければ「未実施」）。
- 規約: 新しい file は `python3 plan/tools/style-check.py` が 0。変える既存の file（zdesktop の menu.c・menu-shell.c・protocol.c・
  home.c、libwayland の menu-protocol.c、libzdesktop）は悪化させない（`plan/ws070/tests/style-compare.sh` の方法）。

## 15. 判断が要る点（既定で進めた）

戻せる既定を選んで先へ進めた。ユーザーの判断があれば変える。

1. **program の名前と置き場所・描画の共有**（ws.md の 1）: `zdesktop-files`（`/bin/zdesktop-files`、題名 Files）。描画は CPU の canvas
   を Vulkan で貼る（§2）。共有の UI library は 2 つ目の使い手が出るまで作らない。
2. **タグ・最近のファイルの保存先**（ws.md の 2）: タグは xattr `user.zdesktop.tags` + 定義と索引の file（§6.1、アプリの中）。recent は
   libzdesktop の新しい API と `~/.local/share/zdesktop/recent`（§6.2、アプリ横断）。
3. **ゴミ箱の形式**（ws.md の 3）: freedesktop.org Trash specification の home trash（§5.4）。
4. **既定のアプリ**（ws.md の 4）: 拡張子の表 + 小さな magic（`file` の database を使わない）、関連付けは `open-with` の file（§7）。
5. **サムネイルの decoder**（ws.md の 5）: PNG と PPM。PNG は WS035 の決定どおり libz-compat と libpng-compat の decode を自前で作る
   （§11、WS035 p040・p041 の decode の半分を先に作ることになる）。JPEG 等は Future Work。
6. **DnD**（ws.md の 6）: zdesktop に `wl_data_device` が無いので窓の中だけ（§12）。窓・アプリの間は zdesktop の Phase が要る（Future Work）。
7. **Quick Look**（ws.md の 7）: client の窓の中の overlay（§9）。compositor の system preview は Future Work。
8. **ヒーローカードの絵・文言**（ws.md の 8）: 絵は壁紙（commit しない既存の資産）の切り出し、無ければ描いた山と湖。文言は挨拶と
   要約（mockup の宣伝文句は使わない）。
9. **UI の言語と日本語**: label は英語（menu の label が ASCII の制限、font の Inter に日本語が無い）。file 名の日本語は fallback font
   （例: Droid Sans Fallback、Apache-2.0、host の `/usr/share/fonts/truetype/droid/`。commit せず `build/ws071-fonts/` から試験の image に
   入れる）があれば描く。日本語の UI の文言と入力（IME）は Future Work。
10. **folder の変化の検出**: 2 秒ごとと画面に戻るときの mtime の比較（通知の仕組みを使わない）。
11. **名前の衝突**: 常に Keep Both（「name 2」）。Replace・Skip を聞く dialog は Future Work。
12. **context menu の fallback**: version 1 の zdesktop では出さない（client 側の menu は描かない）。
13. **複数窓**: New Window は新しい process（§1）。窓の間の共有は clipboard・タグ・recent・sidebar の file。
14. **menubar の shortcut**: 修飾 key の付いた組だけ登録（§10.1）。F2・Delete・Space 等は menu に shortcut として出ない。
15. **作成日時**: `struct stat` に birth time が無いので Info では「—」、list の列は Changed（ctime）。

## 16. Phase への割り当て

| Phase | 内容 | 主な file |
| --- | --- | --- |
| p002 | 骨格: window（pointer・keyboard）、present（Vulkan の canvas）、canvas、text、icons、toolbar・sidebar・content の静的な配置、host の render 試験、guest の image と起動 | `zdesktop-files/{main,window,present,canvas,text,icons,ui}.c`、`plan/ws071/tests/` |
| p003 | 一覧と移動: dir、nav（履歴・パンくず・Back/Forward/Home）、icon・list 表示、並べ替え、選択（click・Ctrl・Shift・矩形・keyboard）、scroll、開く（folder）、Ctrl+L | `dir.c`、`nav.c`、`select.c`、`ui-grid.c`、`ui-list.c` |
| p004 | file 操作: task（copy・move・delete・duplicate・link）、clipboard、new folder、rename（inline）、ゴミ箱（trash・Trash の場所・Put Back・Empty）、完全削除の確認、undo・redo、進みの輪と popover、status の pill | `task.c`、`clip.c`、`trash.c`、`undo.c`、`ui-dialog.c` |
| p005 | 検索（scope、語、協調の走査）、タグ（xattr、定義、索引、sidebar の絞り込み）、recent（libzdesktop の API）、Favorites の編集、Locations（mount） | `search.c`、`tags.c`、`places.c`、`libzdesktop/recent.c` |
| p006 | Home の dashboard（hero、folder cards、recent files、recent folders） | `ui-home.c` |
| p007 | preview pane、Quick Look、Get Info（checksum、xattr）、MIME、開く・別のアプリで開く（起動） | `ui-preview.c`、`mime.c`、`apps.c` |
| p008 | menubar（System Menu）、タブ、New Window、keyboard の shortcut の全体（spec §35）、Help の card | `menu.c`、`ui-tabs.c` |
| p009 | context menu: WS070 protocol version 2（libwayland、zdesktop の menu.c・menu-shell.c、libzdesktop `zdesktop_menu_popup`）と file manager の context menu | `libwayland/menu-protocol.c`、`zdesktop/menu*.c`、`libzdesktop/menu.c` |
| p010 | サムネイル（libz-compat の inflate、libpng-compat の decode、thumb.c）と窓の中の DnD（folder・sidebar・Trash・並べ替え） | `libz-compat/`、`libpng-compat/`、`thumb.c`、`ui-drag.c` |
| p011 | App Home の項目、規約の全文との照合（変えた file 全部）、回帰（WS070 の menu、zdesktop の既存の試験の一部、boot test）、i915 実機（任意） | — |

## 17. 共有される file と衝突の危険（merge のとき）

- `userland/base/zdesktop/`（menu.c・menu-shell.c・menu.h・protocol.c・zwl.h・home.c）: p009 と p011。WS035 の subagent（damage、blur、
  題名の文字、toolkit の protocol）も zdesktop を変える。変更は新しい関数・新しい object の種類の追加に留め、既存の code は並べ替えない。
- `userland/base/libwayland/`（menu-protocol.c、xdg-toplevel-menu-v1-client-protocol.h、event.c）: p009。
- `include/libc/zdesktop.h`、`userland/base/libzdesktop/`（menu.c、recent.c、exports.map、Makefile）: p005・p009。`ZDESKTOP_VERSION` を 3 に。
- `platform/amd64/vmunix.mk`（zdesktop-files の link、libz-compat・libpng-compat の shared library）: p002・p010。
- `include/libc/compat/`（zlib.h、png.h）: p010（WS035 p040・p041 と同じ置き場。WS035 の記録の更新は main session）。
