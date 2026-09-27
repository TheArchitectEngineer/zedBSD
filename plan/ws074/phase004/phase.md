<!-- awesome-plan project=zedbsd record=ws074p004 -->

# ws074-p004: HTML の tokenizer

Phase ID: `ws074-p004`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

WHATWG HTML Standard の tokenizer の全状態（80）、文字参照（名前の表 2231 件、数値参照の置き換え）、入力の stream（UTF-16、
CR LF の正規化、入力が足りない時の待ち）、parse error の種類。html5lib-tests の tokenizer の runner。目標 ≥ 98%。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check 0 件。
2. html5lib の tokenizer の試験が ≥ 98%（host）。guest でも同じ結果。
3. boot test。

## 結果（2026-09-27）

cleared。

- `userland/base/zdesktop-browser/html/{html.h,input.c,tokenizer.c,entities.h}`。状態は standard の順の enum、family ごとの関数
  （text、tag、RCDATA/RAWTEXT の end tag、script data、attribute、comment、DOCTYPE、DOCTYPE の識別子、CDATA、文字参照）。文字は
  1 つの run に集めて次の tag などの前に出す。入力が尽きて stream が閉じていなければ `HTML_TOKEN_NONE` を返し、続きから再開する
  （lookahead の `DOCTYPE`・`[CDATA[`・`PUBLIC`・`SYSTEM`・文字参照の名前、surrogate の半分も待つ）。
- 文字参照の表: build のときに WHATWG の `entities.json`（SHA-256 `d741d877…`、`userland/base/zdesktop-browser/distfiles/`、
  `.gitignore` に 1 行）を取得・検証し、`tools/gen-entities.py` が `$(BUILD)/zdesktop-browser-gen/html-entities.c` を作る
  （CC BY 4.0 の表示は生成物の先頭）。表は commit しない（main の D4 の指示）。
- 試験の道具: `plan/ws074/tests/host-tokenizer.c`（batch の driver: 全部を入れてから閉じる mode と、1 unit ずつ入れて待ちを通る
  mode）、`run-html5lib-tokenizer.py`（`--guest` で guest の driver）、`list-sources.sh`、`fetch-distfiles.sh`、`boot-check.sh`。
- 結果（html5lib-tests `224991ec`、7032 case = test × 初期状態、両 mode）:
  - host plain: **7032/7032（100%）**、parse error の一致 7032（順は問わない。1 件は同じ位置の 2 つの error の順だけが違った）。
  - host ASan・UBSan: 7032/7032。
  - guest（zedBSD の build、GPU 無しの guest）: 7032/7032、error 7032。
  - `plan/ws074/results/html5lib-tokenizer.txt` に記録。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p004-20260927-boot-login.png`）。
- 目標 98% を超えたので M1 の tokenizer の目標は達成（design.md §15）。
