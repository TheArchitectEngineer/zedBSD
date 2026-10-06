<!-- awesome-plan project=zedbsd record=ws128-p004 -->

# ws128-p004: PDF Viewer の文字の検索と選択・copy

Status: in-progress（2026-10-06 q821 P2: 正常系を実装、build warning 0 と host 試験 PASS。T1-268 の AAT find-select が FAIL → q826 で修正、T1 の再試験待ち）
Disposition: normal
Parent: [WS128](../ws.md)
Queue: q821（P2 の第 1 段の列、2026-10-06 Q1）
依存: p001。`userland/base/libpdf`（WS079 の source）に文字の抽出（ToUnicode・font の encoding からの Unicode）を足す必要
目安: 4h 以上（2 Phase に分ける見込み: libpdf の抽出 → Viewer の検索と選択）（1 Queue）。実行者の目安: phase-runner
所有 path: `userland/base/libpdf`、`userland/desktop/pdfviewer/`

## 範囲

libpdf で page の文字と位置を Unicode で取り出し、PDF Viewer に Find（Ctrl+F、一致の強調と次・前）と、drag の選択と Ctrl+C の copy を足す。

## 受け入れ

（採用されたら分割して確定）: 代表の PDF（埋め込みの TrueType・CFF・Type1・CJK）で検索が当たり、copy の文字が正しい（host 試験）。

## 検証の方法と範囲

host と QEMU の Venus。 やっていない確認は「未実施」と書く。

## 未決の判断

ベータ1 に入れるか（ユーザー、規模が大きい）。

## Event

2026-10-02 / ws128-beta1-plan: fg019 の計画で新設。

2026-10-06 ユーザー（クリック）「採る（ベータ2）」: 採る。文字の抽出は WS175 の p002b（D5 で WS175 が持ち主）の後に、Viewer の検索と選択・copy を実装する。

## 実装（2026-10-06 P2、q821。第 1 段の規則「正常系だけ」）

- **libpdf の文字の位置**（`content.c`・`internal.h`）: scan は shown string の符号ごとに、その符号の文字（`pdf_font_unicode`）と glyph の四隅
  （符号の前の text matrix から後の text matrix まで、descent から ascent、shown space）を記録する（`scan_code`、`pdf_scan.character_quads`）。
  文字の並びは今までと同じ（scan_string は開始の行列と font の種類だけになった）。
- **libpdf の公開の API**（`include/libc/pdf.h`・`editor.c`・`exports.map`）: `pdf_page_text_open(document, index, &text)`・`pdf_page_text_close`。
  頁の editor の行（WS175 の行のまとめ）の順に、文字と四隅（`struct pdf_text_character`）、行の中の離れた string の間に空白（四隅は隙間）、
  各行の最後の文字に `PDF_TEXT_LINE_END`。form XObject の中の文字は読まない（editor の scan と同じ範囲）。
- **PDF Viewer の Find**（新規 `find.c`、`titlebar.c`・`main.c`・`menu.c`・`view.c`）: titlebar に find field（`KL_CONTROL_SEARCH`、Text Editor と
  同じ。IME も titlebar の field のもの）。Ctrl+F・Edit > Find… で field に keyboard、打つたびに表示中の頁の頭から最初の一致を探して表示
  （頁へ移り、一致を view の中央・上から 1/3 に）、Enter・F3・Find Next で次、Shift+F3・Find Previous で前（文書を一周）。ASCII の大文字・
  小文字を区別しない。描いた頁の一致を黄色、表示中の一致を橙で塗る。無ければ「Not found」。
- **選択と copy**: 頁の文字の上の press は view の drag ではなく選択を始め、drag で同じ頁の一番近い文字まで（頁の文字の順）、Kei の青で塗る。
  文字の外の press は今までどおり view の drag（選択は外れる）。Ctrl+C・Edit > Copy で UTF-8（行の終わりに改行）を `kl_window_copy` で
  clipboard へ。Esc で選択と一致の印を消す。
- 頁の文字は頁ごとに 1 回読み（`pv_page.text`）、文書を閉じる時に解放。
- **log**: `PDFVIEWER FIND found query="…" page= from= length=` / `FIND none query=`、`TEXT page= characters=`、`SELECT page= from= to=`、
  `COPY bytes= text="…"`。
- **build**: viewer の 3 つの Makefile に `find.c`。`plan/ws079/tests/run-pdfviewer-host.sh` は libpdf 全部と `find.c` を build する形に直し、
  `-I.`（`text.c` の `paths.h`、この直しの前から壊れていた）を足した。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws128/tests/run-host-page-text.sh <scratch>`（新規: edit-basic.pdf の 1 頁の 4 行・最初の文字の四隅・次の文字の続き、3 頁（/Rotate 90）の行と向き、edit-images.pdf の 7 頁で離れた string の間の空白。content.c・editor.c の C89 -pedantic） | 8/8 ×3（plain・ASan・UBSan）、PASS |
| `sh plan/ws128/tests/run-host-pdfviewer-find.sh <scratch>`（新規: viewer の core と find.c で、「LAZY」の一致（大小無視）、F3 の一周、「line」の次・3 頁・Shift+F3 で戻る、無い語、drag の選択と Ctrl+C の「The quick」、文字の外の drag は scroll、Esc。frame を PNG） | 14/14 ×2（plain・ASan+UBSan）、PASS。frame は目視で一致の橙・黄、回転した頁の一致 |
| `sh plan/ws079/tests/run-pdfviewer-host.sh` | ok（plain・asan） |
| WS175 の `run-host-edit-scan.sh`・`run-host-notes-edit.sh`（scan の変更の回帰） | PASS |
| build: zedBSD の libpdf.so・`bin/pdfviewer`・`bin/notes`（-Werror）、keiland-linux の `bin/pdfviewer` | warning 0 |
| style-check（find.c・titlebar.c・menu.c・view.c・draw.c・document.c の変えた所、content.c・editor.c） | 新しい違反 0（main.c・menu.c・titlebar.c の既存の 3 件は前から） |

## T1-268 の FAIL と q826 の修正（2026-10-06 P2）

- T1-268（証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-267/`、`records/apps.pdfviewer.find-select.md`、`logs/apps.pdfviewer.find-select.log`）: lazy は ok。「line」を打って Enter を 2 回押すと、1 回目は field の Enter（`FIND found query="line" page=0 from=127`）になった。2 回目は `PDFVIEWER KEY key=28` として window に届き、何も起きなかった。
- 原因: compositor の titlebar の field は Enter（KL_TEXT_SUBMITTED）で編集を終え、keyboard を window に返す。pdfviewer は次の場所を出すだけで、field に keyboard を戻していなかった。
- 修正: `pdfviewer/titlebar.c` の `pv_titlebar_input`。Enter の後に field の text を query に置き（compositor の field は control の text から始まるため）、`kl_window_focus_control` で field に keyboard を戻す。log は `PDFVIEWER FIND keep error=`。browser の検索と同じく、Enter を続けて押せば次々に進む。field は query 全体を選んだ状態で戻るので、文字を打つと置き換わる。
- 合わせて、同じ file の既存の規約の指摘（`titlebar_send` の後の空行）を直した。
- 確認: build（zedBSD の pdfviewer、Linux の keiland-linux.mk all）は warning 0、style-check は 0。host では titlebar の field を再現できないので、QEMU（T1 の AAT `apps.pdfviewer.find-select` の再試験）が要る。

## 未実施・残り

- QEMU（titlebar の find field・IME での検索・clipboard への copy・選択の見た目）は未実施。T1 の依頼は Q1 に送る。
- 準正常系・異常系の未実装は [WS177 の P2 の一覧](../../ws177/backlog-p2.md)。
