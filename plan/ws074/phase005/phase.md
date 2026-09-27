<!-- awesome-plan project=zedbsd record=ws074p005 -->

# ws074-p005: DOM の核と tree builder

Phase ID: `ws074-p005`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

DOM の核（GC の cell の Document・Element・Text・Comment・DocumentType・DocumentFragment、属性、namespace、tag の番号）と、
WHATWG の tree construction（全部の insertion mode、open element の stack と scope、active formatting element と adoption agency、
foster parenting、template、foreign content の SVG・MathML の名前と属性の補正）。html5lib の tree の試験（WPT に移ったもの）の
runner。当初の案では tree builder を p005・p006 の 2 つに分けていたが、全部の mode を p005 で通した（p006 は fragment の parse と
serializer と残りの失敗に絞る）。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check 0 件。
2. tree construction の文書の試験が動き、数を記録する（目標は M1 の 90%）。
3. guest で同じ結果。boot test。

## 結果（2026-09-27）

cleared。

- `dom/{dom.h,node.c,names.c}`: node は heap の cell（document・parent・兄弟・子の端を trace）、text は GC の外の UTF-16 の buffer
  （finalize で解放）、element は atom の名前と tag の番号（154 の既知の名前の二分探索）、属性の配列。
- `html/{parser.h,parser.c,modes.c,body.c,foreign.c}`: 23 の insertion mode、character の run を空白・NUL・他の 3 種に分けて処理、
  parser の stack は heap の tracer で mark。select は今の standard（customizable select）どおり in body で parse（in select の
  mode は in body に回す）。
- 試験: `plan/ws074/tests/host-tree.c`（batch の driver、html5lib の木の形で出力）、`run-html5lib-tree.py`（`--guest`）。
- 結果（WPT `2d66b9b7` の `html/syntax/parsing/resources/*.dat`、文書 1753 件）:
  - host plain: **1648/1753（94.0%）**。host ASan・UBSan: 1648/1753。guest（zedBSD の build）: 出力が host と byte 単位で同じ
    （runner の表示の 1647 は guest の結果の file を改行の変換つきで読んだため。runner を直した）。
  - `plan/ws074/results/html5lib-tree.txt` に記録。M1 の目標（script-off ≥ 90%）は達成。
  - fragment の 206 件は未実行（p006）。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p005-20260927-boot-login.png`）。

## 後回し（follow-up）

- processing instruction の 84 件（`processing-instructions.dat`、`<?x>` を PI として木に入れる standard の新しい変更）と
  `html5test-com.dat` の 1 件。
- `webkit02.dat` 5 件（select の新しい parse の細部）、`tests1.dat` 4 件、`template.dat` 4 件、`adoption02.dat` 1 件。
- scripting on の 6 件（`scripted_*`: script の実行が要る）。
- fragment の parse（206 件）と serializer（innerHTML）は p006。
