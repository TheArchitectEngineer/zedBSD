<!-- awesome-plan project=zedbsd record=ws102-p019 -->

# ws102-p019: 色付きの絵文字 その 1（font・libtruetype・文字の描画の fallback）

Status: cleared（2026-09-30、host と QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main（Q1）の依頼（2026-09-30、P4、worktree `.claude/worktrees/ws090-widgets`、branch `wt/ws090`、`git merge main -m WIP` の後）

## 範囲と受け入れ

ユーザーの判断「色付きにする」（design §2.10 の末尾）。app の中で色の絵文字が描けるところまで作る。keyboard の絵文字の面は p022。

1. font の選定（Noto Color Emoji の CBDT か COLRv1）と license の監査。外部の file は、外部 package の規則（取得・検証）に準じて取得し、license の file も置く。
2. libtruetype に色の glyph を足す。
3. libkeiui と compositor の文字の描画で、font に無い絵文字を絵文字の font に落とし、色で描く。
4. 確かめ:
   - host の試験（glyph の画素）。
   - Text Editor で絵文字を含む file が色で出る画面。
   - 文字の描画の回帰（libkeiui の host 試験、C9、C7）。
   - boot test と image の大きさの増え。

## 1. font の選定と license

| | CBDT（`NotoColorEmoji.ttf`） | COLRv1（`Noto-COLRv1.ttf`） |
| --- | --- | --- |
| 大きさ | 10,643,852 byte | 4,813,824 byte |
| 中身 | glyph ごとの PNG（109 ppem、136×128 px） | 図形と塗り（線形・放射・掃引の gradient、変換、合成、clip）の木 |
| libtruetype の実装の量 | CBLC の索引（format 1〜5）と CBDT の image の record（format 17・18・19）の読み取り: `color.c` の約 350 行。PNG の decode は既存の libpng-compat を使う | 塗りの木の評価、gradient の 3 種、affine の変換、合成の mode、clip の box、variable の対応を足した rasterizer が要る（数千行の見込み） |
| 画質 | 大きい文字では bitmap の拡大でぼける（今の UI の文字の大きさ 13〜36 px では縮小だけ） | どの大きさでもくっきり |

**CBDT を選んだ**。image は約 6 MB 大きくなるが、実装が小さく（CBLC・CBDT の読み取りと既存の PNG の読み手）、今の UI の文字の大きさでは縮小だけなので画質の差が出ない。
COLRv1 は、大きな絵文字（例: keyboard の絵文字の面の大きな key）で画質が要るようになったら再検討する。

- 取得（外部 package の規則、`ca-certificates` と同じ形の `ZEDBSD_EXTERNAL_FILE`）: `userland/packages/fonts/noto-color-emoji/Makefile`
  - URL: `https://github.com/googlefonts/noto-emoji/raw/v2.047/fonts/NotoColorEmoji.ttf`（Noto Emoji v2.047、Unicode 16.0）
  - 大きさ 10643852 byte、SHA-256 `39ee3c587e10e89669b9ff32703261d10d5f9c4dd5ad147b6b5a1c5200591817`
  - `make … noto-color-emoji-download` が取得と検証を行い、`build/distfiles/NotoColorEmoji-2.047.ttf` に置く。
  - ソースツリーには取り込まない。
- license: SIL Open Font License 1.1（release の `fonts/LICENSE`）。
  - 本文を `userland/packages/fonts/noto-color-emoji/OFL-1.1.txt` に置き、image の `/usr/share/licenses/noto-color-emoji/OFL-1.1.txt` に入れる。
  - OFL は、font を他の software と一緒に配ることと、改変しない同梱を許す。font だけを売ることは禁じる。
  - Reserved Font Name（「Noto」）は、改変した版に使えない。ここでは file を改変せず、名前も変えずに配るので当たらない。image の path の名前（`keiland-emoji.ttf`）は file の置き場所で、font の名前（name table）は変えていない。
- image での置き場所: `/usr/share/fonts/keiland-emoji.ttf`（mode 0644）。
  - package `noto-color-emoji` は compositor（`wayland`）の依存にした。desktop の image には必ず入る。

## 2. libtruetype の色の glyph

- `color.c`（新規）: `truetype_color_glyph(face, glyph, out)`（`truetype.h`、exports に追加）。
  - 顔の pixel の大きさに近い strike（それ以上の最小、無ければ最大）を選ぶ。
  - CBLC の索引の format 1〜5 から、glyph の image を探す。
  - CBDT の record（format 17: small metrics、18: big metrics、19: 索引の metrics）から、PNG の byte 列と、strike の大きさでの幅・高さ・位置・advance・ppem を返す。
  - 範囲の外や壊れた表は EINVAL、無い glyph は ENOENT。
  - PNG の decode は呼ぶ側に任せる（libtruetype は依存を持たないまま）。
- `face.c`: CBLC と CBDT があれば覚える（`read_color`）。
  - loca・glyf が無くても、色の bitmap の font なら開ける（Noto Color Emoji には輪郭が無い）。
  - 輪郭の関数は、その font では今までどおり失敗する（loca の大きさ 0 で EINVAL）。

## 3. 描画の fallback

- `userland/desktop/picture/color-glyph.c`・`.h`（新規、p013 の `picture.c` と同じ共有の source）: `keiland_color_glyph(face, glyph, pixels, out)`。
  - PNG を libpng-compat で BGRA に読み、乗算済みの 0xAARRGGBB にする。
  - 大きさ（pixels / ppem）に面積の平均で縮め、位置と advance も同じ比で丸める。
  - libkeiui・compositor・Text Editor の 3 つに compile して入れる。3 つとも libpng-compat・libz-compat を link する（`platform/amd64/vmunix.mk` の link の規則と、package の依存）。
- **libkeiui**（`text.c`、`keiui.h`）:
  - 3 つ目の face として、絵文字の font（`KUI_TEXT_EMOJI`）を持つ。
  - 主と fallback の font に無い文字が最初に出たときに開く（絵文字を描かない app は読み込まない）。
  - cache の key の face を 2 bit に広げた。
  - `kui_glyph.pixels` に色を持ち、`kui_text_draw` は `kui_canvas_image` で 1:1 に描く（文字の色の alpha を不透明度にする）。
  - **KUI_VERSION 8**（main の最新 7 の次）。`struct kui_glyph`・`struct kui_text` の大きさが変わる。
- **compositor**（`glass.c`、P1 の範囲なので最小の差分）:
  - 主と fallback に無い文字が最初に出たときに、絵文字の font を開く（`GLASS_FACES` 3）。
  - 色の glyph は、atlas の cache の cell に乗算済みの色のまま置き、`glass_glyph.color` を立てる。
  - `glass_draw_glyph_at` は、その glyph を `MODE_IMAGE` で描く（shader は変えない）。
  - log: `ZWL GLASS emoji font: path= errno=`、`ZWL GLASS glyph codepoint= … face=emoji … color=1`。
- **Text Editor**（`textedit/text.c`・`canvas.c`・`textedit.h`）:
  - 本文は libkeiui ではなく自分の text.c（libkeiui と同じ形の複写）で描くので、同じ fallback を足した。
  - 色の画素を混ぜる `te_canvas_pixels` を足した。
- 変えていないもの: keyboard.c（P3）、protocol.c（P6）、他の app（P7）の描画。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| 取得と検証 | `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 noto-color-emoji-download` | 取得、大きさと SHA-256 が一致 |
| build | `make … bin/wayland bin/textedit dynamic/libkeiui.so libtruetype.so …`、`plan/ws099/tests/build-criteria-image.sh build/amd64` | rc 0、この変更の file の warning 0（image の build の warning は openssh と perl の locale の既存のもの） |
| style | `plan/tools/style-check.py`（color.c・color-glyph.c/.h・host-emoji.c・textedit の text.c と canvas.c・keiui の text.c・glass.c と、変えた header） | 新しい code は 0 件。face.c・truetype.h・keiui.h の既存の件は変更の前と同じか減った |
| **host（glyph の画素）** | `plan/ws102/tests/host-emoji.sh`（新規） | PASS。下の表 |
| 回帰: libkeiui の host | `plan/ws090/tests/host-draw.sh`、`plan/tools/keiui/host-chooser.sh`（color-glyph.c と libpng・zlib の source を足した） | 13/13（「pixels that differ: 0」）、85/85 |
| **guest: Text Editor** | Venus の guest に新しい library・wayland・textedit・font を入れ、`/root/emoji-😀.txt`（絵文字の 3 行）を開く | 本文の絵文字、compositor の title の 😀、libkeiui の file chooser の file 名の 😀 が、どれも色で出た（`textedit-emoji.png`・`chooser-emoji.png`） |
| 回帰: C7・C9 | `plan/ws099/tests/criteria.sh build/ws102-p019-criteria.img build/ws102-p019/criteria C7 C9`（emoji の font の入った criteria の image） | C7 PASS（72 の組、最小の contrast 4.68）、C9 の 10 本すべて PASS |
| boot test | `OUTPUT=build/ws102-p019-boot plan/tools/boot-test.sh build/ws102-p019-criteria.img` | PASS（`build/ws102-p019-boot/login.png`） |
| image の大きさ | criteria の image の rootfs（`du -sb build/amd64/rootfs`）の前後 | 117,667,984 → 128,328,532 byte（+10,660,548 byte、ほぼ font の 10,643,852 byte）。disk image は固定の大きさ（2.2 GB）で変わらない |

host-emoji.sh で確かめたこと:
- 絵文字の font が開ける（輪郭無し）。U+1F600 に glyph があり、A には無い。
- U+1F600 の image は PNG で、136×128、109 ppem。
- 32 px では 40×38 px。顔の部分は不透明の黄色（252,219,47）、隅は透明（乗算済み）、advance と top は大きさに合う。
- U+2764 は 20 px で、中央が赤（244,67,54）。
- 文字の font（Inter）の A には色の image が無い（ENOENT）。
- CBLC の strike の数を壊した font は開けるが、色の glyph は EINVAL で断る。

## 制限と残り

- 文字の組み合わせ（shaping、GSUB）が無い。そのため次のものは 1 文字ずつ出る:
  - 国旗（🇯🇵 が regional indicator の J・P の 2 文字で出る）
  - ZWJ の列（👨‍👩‍👧）
  - 肌の色の修飾
- Text Editor の等幅の本文では、1 文字 1 cell（幅の広い文字は 2 cell）で、絵文字の幅（文字の大きさの約 1.25 倍）が隣に少しかかる。
- compositor の絵文字は、窓の title の文字の不透明度に従わない（`MODE_IMAGE` は色の alpha を使わない）。
- libkeiui の新しい struct の大きさ（KUI_VERSION 8）なので、libkeiui を使う app は同じ build で揃える必要がある（image は揃う）。
- 実機での確認は未実施。keyboard の絵文字の面は p022。
