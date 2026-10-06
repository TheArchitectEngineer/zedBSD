# ws090-p020: UI の font を Mahora へ（Regular・Mono・Bold）

Status: planned（2026-10-06 Q1 が作成）
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
