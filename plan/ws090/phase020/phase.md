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

## 2026-10-06 ユーザーの決定（fallback）

「Fallbackフォントを利用できるようにして、Fallbackで2種類のフォントを残します。可変ピッチで1つ、monospaceで1つです。日本語はMohraに徐々に足していくので、遠い将来にFallbackフォントがなくても動くようにしたいです。」と、Q1 のクリックの質問への回答「Droid Sans Fallback と JetBrains Mono」。
→ 残す fallback は 2 つ: 可変ピッチ＝Droid Sans Fallback、monospace＝JetBrains Mono。Inter は使わない（ユーザーが外観を見て消す）。
- P1 の調べ（fontTools）: Droid Sans Fallback は CJK・Hangul・Thai だけで、Latin-1 と記号（· → … × © — 等）を持たない。JetBrains Mono は持つ。
- よって連鎖は、UI: Mahora → JetBrains Mono（記号・Latin-1）→ Droid Sans Fallback（日本語）。Terminal: Mahora Mono → JetBrains Mono → Droid Sans Fallback（日本語は 2 セル）。可変ピッチの文の中の記号は JetBrains Mono の字形になる（ユーザーに報告済み）。
- fallback の file が無くても動く（開けなければ飛ばし、無い字は □）。Mahora に日本語が足されれば fallback を引かない。
- 行の高さ・baseline は font に依らない決まった値か Mahora の値で、fallback の有無で layout が動かない。
