<!-- awesome-plan project=zedbsd record=ws092-p003 -->

# ws092-p003: 共有の file chooser（libkeiland の `keiland_file_chooser_*`）と editor の Open・Save As

Status: cleared（2026-09-29、subagent の worktree `wt/ws092`）
Disposition: normal
Parent: [WS092](../ws.md)
Queue: main の依頼（2026-09-29「ws092-p003（共有の file chooser）: 設計 → 実装 → text editor の `te_host.choose` と `TE_EVENT_CHOSEN` の受け口に結ぶ → host と QEMU の試験」）
依存: p002（editor の受け口 `te_host.choose`・`TE_EVENT_CHOSEN`）

## 目的と受け入れ

ユーザーの方針（2026-09-29）「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」により、Open・Save As の
chooser を app の中に作らず、共有の library の部品として作る。今は libkeiland に `keiland_file_chooser_*` として置く（WS090 の library の設計の時に
そこへ移す）。WS091（画像 viewer）・WS089（Settings）・Notes・PDF Viewer が後で使うので、API は app に依らない。

受け入れ:

1. `include/libc/keiland.h` に API（開く・名前を付けて保存・folder の移動・sidebar・拡張子の filter・上書きの確認・取り消し・答えの返し方）、
   `KEILAND_VERSION` を main の最新の次に上げ、`exports.map` に `keiland_file_chooser_*`。
2. libkeiland の実装（Wayland の窓、shm と libtruetype で自分で描く、pointer・keyboard・touch、Files の list に揃えた見た目）。
3. text editor の Open・Save As・Untitled の Save が chooser で動く（`te_host.choose` → `TE_EVENT_CHOSEN`）。
4. host の試験（folder の読み・並べ・filter・key と pointer の操作・上書きの確認・描画）と QEMU の desktop での画面の確認。

## 設計

### 描画の方式（決定）

**chooser は library が作る別の xdg_toplevel の窓**（`xdg_toplevel_set_parent` で親を示す）で、library が **wl_shm の ARGB8888 の buffer に
libtruetype で自分で描く**。app の surface に重ねる案は app ごとに描画（Vulkan・canvas）と入力の転送が要り、app に依るので採らない。
xdg_popup の案は親の窓の大きさ・位置に縛られ、zdesktop では外の press で閉じるので採らない。

調べた事実（zdesktop の側、変更しない）:

- `xdg_toplevel.set_parent` は受け付けるが窓は独立（`wayland/toplevel.c`）。新しい窓は上に出て、keyboard の focus は上の窓に行く。
- wl_pointer・wl_keyboard の event は client の全ての pointer・keyboard の object に送られる（`wayland/seat.c`）。library は自分の wl_seat を bind して
  自分の pointer・keyboard・touch を持つ。**app は自分の surface 以外の enter・key・touch を無視する必要がある**（Wayland の client の普通の義務。
  API の文書に書き、text editor の `window.c` をそうした）。
- ARGB8888 の shm の buffer は alpha で blend され（`wayland/shm.c`）、glass の panel は surface ごと（`keiland_glass`）なので、shm の窓も
  すりガラスの card を持てる。glass の無い compositor では不透明の淡い地（Files と同じ判断）。
- zdesktop は titlebar の無い toplevel にも浮いた titlebar（title と close）を描く。close は取り消し。

library の依存に libtruetype を足す（`libkeiland.so` の NEEDED に `libtruetype.so`、`platform/amd64/vmunix.mk` の link の規則と package の依存）。

### event の流れ

chooser の proxy は全て app の既定の queue に置く（menu・titlebar と同じ）。app が `wl_display_dispatch*` で queue を回すと、chooser の入力・
configure・frame の callback が動き、答えは listener の `done` で返る。chooser は自分の描画を frame の callback で間引き（1 frame に 1 回）、
指が触れている間・慣性で動いている間は frame を求め続ける（long press と glide の時計）。app に timer の義務は無い。

### API（`include/libc/keiland.h`、KEILAND_VERSION 12）

```c
struct keiland_file_chooser;

#define KEILAND_FILE_CHOOSER_OPEN	0U	/* 既存の file を 1 つ選ぶ */
#define KEILAND_FILE_CHOOSER_SAVE	1U	/* folder と名前を選ぶ（上書きは確かめる） */

/* 答え */
#define KEILAND_FILE_CHOOSER_CHOSEN	0U
#define KEILAND_FILE_CHOOSER_CANCELLED	1U

/* 1 つの filter: 表示の名前と、空白で区切った拡張子（"txt md c h"、"." なし、大文字小文字を無視）。extensions が NULL か "" なら全ての file。 */
struct keiland_file_filter {
	const char *label;
	const char *extensions;
};

struct keiland_file_chooser_options {
	unsigned mode;				/* KEILAND_FILE_CHOOSER_OPEN か _SAVE */
	const char *title;			/* 窓の題（NULL: "Open" か "Save As"） */
	const char *application;		/* 窓の app_id（NULL 可。zdesktop が app の印を出す） */
	const char *folder;			/* 始めの folder（NULL か無い folder: $HOME、無ければ "/"） */
	const char *name;			/* Save の始めの名前（NULL: 空）。拡張子の前までを選択した状態 */
	const struct keiland_file_filter *filters;	/* NULL 可 */
	size_t filter_count;
	size_t filter;				/* 始めの filter の番号 */
	const char *font;			/* NULL: /usr/share/fonts/keiland.ttf */
	const char *fallback_font;		/* NULL: /usr/share/fonts/keiland-fallback.ttf（無くてよい） */
};

struct keiland_file_chooser_listener {
	/* 一度だけ、最後に: result と、CHOSEN なら絶対 path、選んでいた filter の番号。app はこの後に destroy する。 */
	void (*done)(void *data, struct keiland_file_chooser *chooser, unsigned result, const char *path, size_t filter);
};

struct keiland_file_chooser *keiland_file_chooser_open(struct wl_display *display, struct xdg_toplevel *parent,
	const struct keiland_file_chooser_options *options, const struct keiland_file_chooser_listener *listener, void *data);
void keiland_file_chooser_destroy(struct keiland_file_chooser *chooser);
```

- open は NULL と errno を返す: EINVAL（mode・filter の番号・listener）、ENOTSUP（wl_shm・xdg_wm_base・wl_compositor の無い compositor）、
  ENOENT など（font が開けない）、ENOMEM。窓は open が返った後の最初の dispatch で configure を受けて描かれる。
- destroy は開いている窓を答えずに閉じる（app が Quit で待つのをやめた時など）。`done` の中から呼んでよい。
- 1 つの chooser は 1 つの答え。複数の file の選択・folder の選択・新しい folder の作成は今は無い（要る app が来た時に足す）。

### 見た目（Files の list に揃える、Kei の決まり）

- 窓 760×480（configure の大きさに従う、最小 520×340）。すりガラスの card 2 枚（窓の縁から 8 px、角 16）: 左の sidebar（幅 184）と右の内容。
  glass が無ければ Files の地（#eef2f7→#e6ebf3 の縦の階調）に白の card。
- **sidebar**: Recent（Open の時だけ。`keiland_recent_list` の file で filter に合うもの）、Home、Desktop・Documents・Downloads（有る時だけ）、
  Computer（`/`）。行 30 px、角 9 の今の場所は `#2f7cf6` の 16% の地に accent の文字、hover は淡い灰。
- **内容の上の段**（40 px）: 上へ（↑）の丸い button と、今の場所（`Home › Documents` の形、狭ければ前を省く）。
- **list**（Files の list の型）: 見出し 30 px（Name・Size・Modified、11 px の淡い大文字でなく Files と同じ 12 px の 2 次の色）、行 28 px、
  名前 13 px、folder は青（#5aa2f5）の folder の絵、file は白い紙の絵。選んだ行は accent（#2f7cf6）の地に白の文字（窓に focus が無ければ淡い灰）、
  hover は淡い灰。folder が先、名前の順（大文字小文字を無視）。`.` で始まる名前は隠す（Ctrl+H で見せる）。filter に合わない file は出さない。
  空の folder は「This folder is empty」、読めない folder は「Can't open this folder: <理由>」。
- **下の段**（52 px）: Save は「Name」の field（角 8、focus の時は accent の縁）、filter の pill（2 つ以上の時。click で次、名前と ▾）、
  Cancel と主の button（Open・Save、accent の地に白。選べない時は淡い）。Open も filter の pill と 2 つの button。
- **上書きの確認**: 内容の上に淡い幕と中央の card「Replace "<名前>"?」「A file with that name already exists in "<folder>".」、
  Cancel と Replace（Replace は赤 #e5484d の地）。Enter で Replace、Esc で Cancel。
- **message**（名前に `/`、書けない folder 等）: 下の段の左に赤の文字で 1 行（次の操作で消える）。

### 操作

- pointer: 行の click で選ぶ（Save で file を選ぶと名前の field にその名前）、double click で folder に入る・file を選んで終える、
  sidebar の click で場所へ、↑ で親へ、wheel で list の scroll、button の click。右の窓の close は取り消し。
- keyboard: ↑・↓・Home・End・PageUp・PageDown で選択、Enter（folder を選んでいれば入る、でなければ主の button）、Backspace（Open）と
  Alt+↑ で親へ、Esc で取り消し、Ctrl+H で隠し file、Ctrl+L で path を打つ field（Open でも。打った path が folder なら入り、file なら選ぶ）。
  Save の名前の field は常に文字を受ける（←・→・Home・End・Backspace・Delete、US の配列の表、zdesktop は keymap を送らない）。
- touch（`keiland_gesture`・`keiland_scroller`）: 1 本指の drag で list の scroll と慣性・rubber band、tap で選ぶ（folder の tap は入る、
  sidebar・button の tap は click と同じ）、double tap は double click と同じ。glide 中の press は止めるだけ（tap にしない）。
- 主の button の条件: Open は file が選ばれている時。Save は名前が空でない時。Save で名前が既にある folder なら入る、既にある file なら確認、
  書けない folder なら message、`/` を含む名前も message。答えの path は folder と名前を `/` でつないだ絶対 path（`realpath` で正規化した folder）。

### 構成（`userland/desktop/libkeiland/`）

| file | 役割 |
| --- | --- |
| `chooser.c` | 公開の API、Wayland（registry・seat・shm の buffer 2 枚・xdg の窓・frame・glass）、入力の転送 |
| `chooser-model.c` | Wayland を知らない部分: folder の読み・並べ・filter・sidebar の場所・key と pointer と touch の解釈・答えの判断 |
| `chooser-draw.c` | 配置と描画（hit の矩形を model に記録） |
| `paint.c`・`paint-text.c`・`paint.h` | CPU の canvas（矩形・角丸・線・円・glyph の mask）と文字（libtruetype、glyph の cache、fallback）。textedit の `canvas.c`・`text.c` を元にした library の内部の部品（WS090 の widget が使う） |
| `chooser.h` | 内部の型と関数（export しない） |

`chooser-model.c`・`chooser-draw.c`・`paint*.c` は host（Linux）でも build でき、host の試験が直接呼ぶ。

### text editor の結線（`userland/desktop/textedit/`）

- `main.c` の `main_host` に `choose`: `keiland_file_chooser_open`（Save As・Open、filter は「Text Files」（txt text md markdown c h cc cpp hpp py sh
  mk conf cfg ini json xml html css js log csv tsv rst yaml yml toml）と「All Files」、始めは All Files ではなく Text Files、app_id `textedit`）。
  答えは `done` で `TE_EVENT_CHOSEN`（取り消しは空）を積み、chooser を destroy。editor が待つのをやめた（Quit）後に残る chooser は main の loop が閉じる。
- `window.c`: 自分の surface 以外への pointer・keyboard・touch の event を無視する（chooser の窓の入力が editor に入らない）。

## 試験の計画

- host（`plan/ws092/tests/host-chooser.sh`）: 一時の folder の木で、読み・並べ（folder が先、大文字小文字）・隠し・filter・sidebar・key の移動・
  Enter で入る・親へ・Save の名前と上書きの確認・書けない folder・`/` の名前・Ctrl+L の path・pointer の click と double click・touch の tap、
  描画を PPM → PNG に書いて見る（`build/ws092-shots/host-chooser-*.png`）。
- QEMU（Venus の desktop、main の `build/ws035-sq/hdd-image.img` の複写）: 新しい `libkeiland.so` と `textedit` を置き、Ctrl+O で chooser、
  folder の移動、file を開く、Save As で新しい名前・既にある名前（確認）で保存、SSH で保存した byte を確かめる。画面は `build/ws092-shots/`。

## 結果（2026-09-29）

### 設計からの違い・分かったこと

- **zdesktop は client に生きている xdg_surface が 1 つでもあると `xdg_wm_base.destroy` を protocol error にする**（`userland/desktop/wayland/protocol.c` の
  ZWL_WM の opcode 0。protocol の定義では「その binding から作った xdg_surface」だけが対象）。最初の実装は chooser の destroy で自分の xdg_wm_base を
  destroy し、editor の接続が切れた（QEMU で観測: `ZWL ERROR client=1 object=99 reason=invalid or unsupported request`、editor は `DONE reason=disconnected`）。
  library は **xdg_wm_base の binding を process に 1 つ持ち続け**、ping に答え続ける形にした（`chooser.c` の `chooser_kept_shell`）。zdesktop の側を
  binding ごとの判定に直すのは WS035 の範囲なので main に報告する（変えていない）。
- 窓を閉じた後の答えは `wl_display.sync` の callback から伝える（app が `done` の中で destroy しても、配送中の object を壊さない）。
- 2 回目の tap は TAP と DOUBLE_TAP の組で届く（`gesture.c`）。組は double tap だけとして扱い、1 回目の tap が別の folder を見せた時は捨てる（`generation`）。
- Save の窓が狭い時（既定の 760 でも名前の欄が 110 px 未満になる時）は filter の pill を出さない。filter の幅は 136。
- key の repeat は chooser では無い（押すごとに 1 回）。Files からの drag and drop・複数選択・新しい folder は範囲外（要る app が来たら足す）。

### 変えた file

- 新規（libkeiland）: `chooser.c`（Wayland の窓・seat・shm・frame・glass・答えの配送）、`chooser-model.c`（folder・並べ・filter・sidebar・key・pointer・
  tap・Save の確かめ）、`chooser-draw.c`（配置と描画、Files の色と大きさ）、`chooser.h`、`paint.c`・`paint-text.c`・`paint.h`（CPU の canvas と文字。
  textedit の canvas.c・text.c を元に、角丸の縁・線・円・環・縦の階調を足した）。
- `include/libc/keiland.h`: `KEILAND_VERSION` 11 → **12**（main の最新は 11 を 2026-09-29 の c4268cc0 で確認）、`keiland_file_chooser_*` の API（追加だけ）。
- `userland/desktop/libkeiland/exports.map`（`keiland_file_chooser_*`）、`Makefile`（source と `desktop/libtruetype` の依存）、
  `platform/amd64/vmunix.mk`（`libkeiland.so` の link に `libtruetype.so`、NEEDED の確認にも）。
- text editor: `main.c`（`main_choose`・`main_chosen`、Text Files と All Files の filter、Quit で待たなくなった chooser を閉じる）、
  `window.c`・`window.h`（自分の surface 以外の pointer・keyboard・touch の event を無視する）。

### 試験

- host（`sh plan/ws092/tests/host-chooser.sh`、Linux の cc で model・draw・paint・recent と libtruetype を build）: **75/75**。
  並べ（folder が先・大文字小文字を無視）、隠し file と Ctrl+H、filter（pill の click）、sidebar（Recent・Home・Desktop・Documents・Computer、無い Downloads は出ない）、
  ↓・Enter・Backspace・Alt+↑・↑ の button・文字での選択、click と double click（400 ms を超えた 2 回は開かない）、folder の tap で入る・file の tap と double tap、
  Esc と Cancel、Recent（filter に合わない file は出ない）、悪い option（EINVAL）、Save（拡張子の前までの選択・上書き・新しい名前・既にある file の確認と
  Cancel・Esc・Replace・folder の名前で入る・`/` の拒否と message・書けない folder の拒否・空の名前）、Ctrl+L の path（`~/Desktop`、相対の file、Save の path）。
  絵: `build/ws092-shots/host-chooser-{open,open-glass,save,replace,narrow,path}.png`。host の text editor の試験 `host-core.sh` も 34/34（回帰）。
- build（amd64、`make -j64 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/textedit build/amd64/dynamic/libkeiland.so build/amd64/bin/wayland`、`-Werror`）:
  exit 0、warning 0（main を merge した後も）。
- 規約: `python3 plan/tools/style-check.py userland/desktop/libkeiland/*.c userland/desktop/textedit/*.c` → **違反 0**（最初の 64 件を直した）。全文との照合は p004。
- QEMU（Venus の desktop、`build/ws092/zd-start.sh`、main の `build/ws035-sq/hdd-image.img` の複写に新しい `/lib/libkeiland.so`・`/tmp/textedit`・`/tmp/wayland --glass --timeout=3600`。
  入力は QMP（`plan/ws092/tests/qmp-keys.py`）、画面は VNC（`plan/ws035/tests/zdesktop-shot.py --runtime <絶対 path>`）、結果は editor の log と SSH で読んだ file）:
  - Ctrl+O で chooser の窓（題「Open」、editor の印、すりガラスの card 2 枚）: `q-open.png`。↓↓ Enter で folder に入る（`q-notes.png`）、Backspace で戻る、
    hover（`q-hover.png`）、filter の pill で All Files（`picture.png` が出る、`q-filter.png`）、click で選んで Open が有効に（`q-select.png`）、Open の button で
    README.md を開いた（`q-opened.png`、log `CHOSEN result=0 path=/root/Documents/README.md`・`OPEN`）。↓↓↓ Enter でも開いた。
  - Save As（Ctrl+Shift+S）: 名前の欄が `README.md` の `README` を選んだ状態、`copy` を打って `copy.md`（`q-saveas-typed.png`）、Enter で保存（SSH で
    `/root/Documents/copy.md` = `# readme edited`）。既にある file の選択 → Save → 確認の card（`q-replace.png`）→ Enter で置き換え（22 byte）。
  - Untitled の Ctrl+S → Home の Save As（`q-save-untitled.png`、filter の pill あり）、Tab で list、↓↓ Enter で Documents（`q-save-folder.png`）、Tab で名前、
    `note` → `note.txt` を保存（SSH で `a new note`）。
  - Esc と窓の close の button で取り消し（log `CHOSEN result=1 path=`）、その後 editor に keyboard が戻る（`q-cancelled.png`）。同じ process での 2 回目以降の
    chooser も動く（shell の binding の共有）。zdesktop の log に ERROR なし（修正の後）。
- 未実施: touch（guest に touch の device が無い。model の tap は host で確かめた。慣性の scroll は実機）、wheel の scroll の目視、Quit の時に開いている chooser を
  閉じる経路、Ctrl+L の path の欄の guest での操作（host では確かめた）、日本語の名前の guest での表示（host の絵では出る）、実機。

### 残り

- p004: 規約の全文との照合（p002・p003 の全 source）と回帰（boot test）。
- main への依頼: zdesktop の `xdg_wm_base.destroy` の判定（client 全体ではなく binding ごと）を WS035 で直すか判断。直っても library はそのままで動く。
