# ws090-p020: UI の font を Mahora へ（Regular・Mono・Bold）

Status: test-wait（q812、P1、2026-10-06 実装済み・T1 の PNG とユーザーの外観の判断待ち。下の「q812（P1）」）
WS: [WS090](../ws.md)
Related: [BUG-205](../../bugs/BUG-205.md)（太字を輪郭の太らせで作っていた）

## 出典

2026-10-06 ユーザー:「userland/desktop/fonts/にMahoraフォント群を追加しました。私が著作権を持つもので、プロジェクト全体のツリーのzlibライセンスで利用可能です。UIをMahora Regularに移行し、ターミナルはMahora Mono、太字フォントはMahora Boldを利用するように修正してください。移行後、外観が問題ないと私が考えれば、ほかのフォントはライセンスが異なるので削除します。」
font の file は main 712a16880 で `userland/desktop/fonts/Mahora-{Regular,Mono,Bold}.ttf`（各 95 glyph、U+0020〜U+007E の ASCII だけ。Q1 が fontTools で確認）。

## 範囲

- install の対応（zedBSD の `userland/desktop/wayland/Makefile` の KEILAND_FONT_DATA、Linux・FreeBSD の Makefile と package）を `keiland.ttf`=Mahora-Regular、`keiland-mono.ttf`=Mahora-Mono に替え、新しく `keiland-bold.ttf`=Mahora-Bold を入れる。Mahora の license の注記（tree の Zlib、ユーザーの著作）を licenses に足す。
- 太字: libkeiland の text（`ui/text.c`、BUG-205 の `truetype_set_bold` の太らせ）と、自前で文字を描く app・compositor・IME の popup・libbrowser の太字を、Mahora Bold の face で描く。Mahora Bold に無い字（ASCII の外）は、今の fallback に太らせを残す（日本語など）。
- ASCII の外の字（`·`・`→`・`…`・`×`・`©` など UI の記号、日本語）は Mahora に無いので、今の fallback の連鎖（keiland-fallback.ttf ほか）で描かれることを確かめる。字の幅・行の高さ・baseline の違いで崩れる所（title bar・bar・list・button）を直す。
- 試験: host の描画（Settings・Files・Phone などの PNG）、build（3 OS、warning 0）、QEMU は T1（desktop・各 app・Terminal の PNG をユーザーが見る）。

## ユーザーへの報告事項

- Mahora は ASCII の 95 字だけなので、Inter・JetBrains Mono・Droid Sans Fallback を消すと、UI の記号と日本語の字が無くなる。消す前に、記号と日本語の fallback をどうするか（Mahora に足すか、別の font を残すか）を決める必要がある。

## 2026-10-06 ユーザーの決定（fallback）

「Fallbackフォントを利用できるようにして、Fallbackで2種類のフォントを残します。可変ピッチで1つ、monospaceで1つです。日本語はMohraに徐々に足していくので、遠い将来にFallbackフォントがなくても動くようにしたいです。」と、Q1 のクリックの質問への回答「Droid Sans Fallback と JetBrains Mono」。
→ 残す fallback は 2 つ: 可変ピッチ＝Droid Sans Fallback、monospace＝JetBrains Mono。Inter は使わない（ユーザーが外観を見て消す）。
- P1 の調べ（fontTools）: Droid Sans Fallback は CJK・Hangul・Thai だけで、Latin-1 と記号（· → … × © — 等）を持たない。JetBrains Mono は持つ。
- よって連鎖は、UI: Mahora → JetBrains Mono（記号・Latin-1）→ Droid Sans Fallback（日本語）。Terminal: Mahora Mono → JetBrains Mono → Droid Sans Fallback（日本語は 2 セル）。可変ピッチの文の中の記号は JetBrains Mono の字形になる（ユーザーに報告済み）。
- fallback の file が無くても動く（開けなければ飛ばし、無い字は □）。Mahora に日本語が足されれば fallback を引かない。
- 行の高さ・baseline は font に依らない決まった値か Mahora の値で、fallback の有無で layout が動かない。

## q812（P1、2026-10-06）

### 仕組み

- **libtruetype の companion**（`companion.c`、`truetype_open_companions(face, bold_path, next_path)`・`truetype_glyph_bold_face`）: face の横に入れた file を一つの font のように使う。
  - next（monospace の fallback、JetBrains Mono）: face に無い字は next で探し、glyph の番号を face の後に続けて返す。その番号を渡した全ての call（metrics・描画・design の advance・outline・colour）は next の face で、face の大きさと太さ（太らせ）で描く。design 単位の call は next の em を face の em に換算（JetBrains Mono の 1000 → Mahora Mono の 2048）。
  - bold（Mahora Bold）: face が bold の間、face 自身の glyph は bold の face の同じ番号の glyph をそのまま描く（太らせない）。glyph の数・em・全 glyph の幅が face と同じ時だけ受ける（Mahora Mono の横では断る）。
  - 行（`truetype_metrics`・`truetype_design_metrics`）は face 自身の値のまま。companion の file が有っても無くても行・baseline は動かない。読めない file は飛ばす（無い字は face の □）。
  - Mahora に字が足されれば、face の glyph が先に引かれるので自然に Mahora を使う。
- **path**: `userland/desktop/paths.h` に `KEILAND_FONT_BOLD`（keiland-bold.ttf）と `KEILAND_FONT_FALLBACK_MONO`（keiland-fallback-mono.ttf）。
- **install**（zedBSD `wayland/Makefile`、`Makefile.linux`・`.freebsd`）: keiland.ttf=Mahora-Regular、keiland-bold.ttf=Mahora-Bold、keiland-mono.ttf=Mahora-Mono、keiland-fallback-mono.ttf=JetBrains Mono、keiland-fallback.ttf=Droid Sans Fallback（そのまま）。licenses に `Mahora-LICENSE.txt`（新規、tree の Zlib、ユーザーの著作）、Inter-OFL は入れない（Inter は使わない、tree の file は残す）。`tools/release/license-components.json` の keiland-fonts を Mahora に。
- **各描画**（主 font を開いた直後に 1 行）: libkeiland `ui/text.c`（設定・Phone・Mailer・Calendar・Monitor・Video Player・IME の popup・chooser）、Files の `text.c`（Settings も）、Text Editor、PDF Viewer・Image Viewer の UI、Notes の `ui.c`、compositor の `glass.c`、X server の core font（next だけ）、libbrowser の sans（bold と next）と mono（next）、libpdf の代わりの font（next、bold は既存の keiland-bold.ttf の名前の探しで Mahora Bold が当たる）。
- **libbrowser の太字**: Mahora Bold にある glyph は bold の face でそのまま、無い glyph（記号・日本語）は今の 1 pixel の太らせ（`truetype_glyph_bold_face` で分ける）。web font は変えない。
- **Terminal**: cell の行は font に依らない決まった値（ascent 1.02・descent 0.30 em、JetBrains Mono の時の値）。grid の間隔と罫線の繋がりを今のまま保つ。太字は描かない（bold は渡さない）。
- **他の UI の行**: Mahora 自身の値（ascent 0.8・descent 0.2 em、Inter の 0.97・0.24 より詰まる）。Q1 の了解（2026-10-06）。

### 確認

- host: `sh plan/ws090/tests/host-mahora.sh`（rm 無し）→ `host-mahora: PASS`（companion の 23 項目: 字の番号、bold の face と Mono での拒否、bold の A の advance が regular と同じ、行が Mahora のまま、em の換算 1229、file が無い時）。libkeiland の text で描いた見本 `build/ws090-p020/host/mahora.png`（ASCII・記号・Latin-1・日本語、両方の太さ、Mono と罫線）。
- build: zedBSD amd64 の libtruetype・libkeiland・libpdf・libbrowser・textedit・terminal・files・settings・pdfviewer・imageview・notes・wayland・xserver・browser（exit 0、warning 0）。`make keiland-linux` の gcc と clang（exit 0、warning・error 0、share/fonts に新しい名前）。
- 未実施: FreeBSD の build（環境が無い）、`license-inventory.py`（image が要る）、QEMU（T1 に依頼: desktop・各 app・Terminal の PNG をユーザーが見る）。

### ユーザーへの報告

- 可変ピッチの文の中の記号・Latin-1（· → … × © é ü ß €）は JetBrains Mono の字形（幅 0.6 em）で描かれる。
- UI の行が Inter の時より詰まる（16 px で行 16 px）。Mahora の hhea（lineGap など）で調整できる。
- Mahora は Inter より cap height が低く（0.65 対 0.73 em）、同じ px で小さく見える。
