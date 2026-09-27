<!-- awesome-plan project=zedbsd record=ws074p010 -->

# ws074-p010: font と text の最小（ワンパス）

Phase ID: `ws074-p010`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

正常系のワンパス: zdesktop の image の font（Inter・JetBrains Mono・Droid Sans Fallback）を libtruetype で開き、style から font を選ぶ
（generic family、整数の pixel の大きさ、600 以上は太字）、code point の glyph（無ければ fallback の face）、advance と coverage の
bitmap の cache、行の分割の機会（空白の後、CJK の間、基本の禁則）。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check 0 件。
2. host の試験（`host-text`）が plain と ASan で通る。guest でも。
3. boot test。

## 結果（2026-09-27）

cleared。

- `text/{text.h,font.c,linebreak.c}`。browser は libtruetype を link する（`platform/amd64/vmunix.mk` の link、package の REQUIRE
  `base/libtruetype`）。libtruetype の API は変えていない（足す関数は無し。小数の大きさと kerning は後回し）。
- 太字は zdesktop-files と同じく regular の glyph を 1 pixel 太らせる（Inter は可変 font だが libtruetype は既定の instance を描く）。
- 試験 `plan/ws074/tests/host-text.c`（20 検査: 選択、metrics、幅、太字、monospace、fallback の「あ」、bitmap、改行の規則）:
  host plain・ASan 20/20、guest（`/usr/share/fonts/zdesktop*.ttf`）20/20。16px の Inter: ascent 16・descent 4・行 20、"Hello" 40px
  （太字 45、mono 50、32px 79）。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p010-20260927-boot-login.png`）。

## 後回し（follow-up）

- 小数の font の大きさと 1/64 px の advance、kerning（`kern`・GPOS）、合字と複雑な script の shaping: libtruetype への関数の追加が
  要る（加える前に main に伝える。desktop のサブエージェントの了解は済み）。
- 本物の太字・斜体（可変 font の軸、または別の font の file）。serif の font（image に無いので sans で代用、D10）。
- font の一覧（`/usr/share/fonts/` の走査、`name`・`OS/2` の表からの family 名の照合）と `@font-face`。
- UAX #14 の全部の Line_Break の class（Unicode の表と一緒に）。
