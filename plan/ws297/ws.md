<!-- awesome-plan project=zedbsd record=ws297 -->

# WS297 completed の WS に残った「規約の全文の見直し」を一括で行う

- Master: [plan/master.md](../master.md)（2026-10-27 作成）。Primary Milestone: MG001。Related: [WS296](../ws296/ws.md)（drivers の comment の整理、同じ形）、[coding-style.md](../coding-style.md)。
- Status: planned（2026-10-27 新設。時期は Q1 とユーザーの整理で決める）
- 出典: 2026-10-27 ユーザーの回答（クリック）「1 つの WS にまとめる」（P3 の精査 q1608 の問い 1「各 WS の『規約の全文の見直し』の Phase を、1 つの WS にまとめて後でやるか」、[ws-audit.md](../agents/P3/ws-audit.md)）。
  元の WS は completed のまま（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」で残っていた物。ベータ3 の段は 2026-10-27 に取り消し）。

## 単一の到達目標

下の一覧の WS が変えた C の code を [coding-style.md](../coding-style.md) の全文に合わせる（書き方だけ、動作は変えない）。
comment の中の開発の管理情報（[Guardrail](../guardrail.md)「source の中の開発の管理情報」）も同時に消す。

## 対象（候補、p001 で確定）

completed の WS の Phase の表で、「規約」を含み cleared でない行（2026-10-27 P4 の機械の数え、101 行・89 WS。canceled の行を除く）。
完了の形にした WS は Phase の directory が消えるので、ここが対象の一覧の本体になる。

- ws031-p012
- ws031-p048
- ws033-p003
- ws035-p058
- ws035-p017
- ws037-p009
- ws039-p010
- ws045-p009
- ws046-p005
- ws050-p006
- ws051-p006
- ws056-p001
- ws061-p011
- ws075-p010
- ws078-p005
- ws079-p007
- ws079-p009
- ws081-p002
- ws081-p009
- ws083-p009
- ws089-p006
- ws089-p018
- ws090-p012
- ws094-p014
- ws094-p007
- ws095-p011
- ws099-p022
- ws101-p013
- ws102-p014
- ws103-p007
- ws104-p008
- ws105-p011
- ws106-p003
- ws107-p004
- ws109-p005
- ws109-p007
- ws111-p002
- ws112-p007
- ws114-p006
- ws130-p009
- ws131-p024
- ws132-p006
- ws132-p007
- ws134-p010
- ws141-p007
- ws143-p009
- ws145-p006
- ws154-p004
- ws155-p005
- ws156-p005
- ws157-p007
- ws157-p009
- ws157-p011
- ws159-p005
- ws161-p007
- ws162-p004
- ws164-p003
- ws165-p004
- ws166-p004
- ws167-p003
- ws169-p007
- ws170-p005
- ws174-p005
- ws175-p011
- ws190-p004
- ws191-p005
- ws192-p003
- ws193-p003
- ws194-p003
- ws196-p005
- ws202-p012
- ws202-p014
- ws203-p004
- ws206-p007
- ws207-p005
- ws208-p006
- ws210-p012
- ws211-p004
- ws213-p006
- ws214-p002
- ws215-p007
- ws216-p008
- ws217-p008
- ws220-p009
- ws228-p003
- ws229-p007
- ws231-p002
- ws232-p002
- ws236-p012
- ws236-p007
- ws237-p006
- ws245-p015
- ws246-p003
- ws253-p002
- ws254-p002
- ws260-p015
- ws269-p012
- ws271-p005
- ws272-p004
- ws278-p008
- ws279-p009

## 受け入れ

- p001 で確定した対象の file が coding-style.md の全文に合う（`plan/tools/style-check.py`・clang-format と目の確かめ）。動作の変更が無いことを diff の通読で確かめる。
- 変えた file の build の warning 0（amd64 と、その code を build する platform）、関係する host 試験。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 対象の確定: 各 WS が変えた file（git の履歴・ws.md の記録）と、既に済んだ物を除いた一覧、分け方 | — | planned |
| p002〜 | 見直し（p001 の分け方で、conformance-reviewer などで） | p001 | planning |
