<!-- awesome-plan project=zedbsd record=ws154 -->

# WS154: Settings の Languages の頁で IME を選ぶ（日本語・SKK・なし=英語）と、SKK の IME の新しい実装

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p004 cleared（Languages の頁と SKK、T1-173b・T1-174b）、UAT は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Settings に Languages の頁を足して、使う IME を「日本語」「SKK」「なし（英語の mode）」から選べるようにし、SKK の入力方式の IME を新しく実装する。

## ユーザーの決定

- 2026-10-04 夜「SettingsにLanguages画面を追加して、IMEを選択できるようにします。また、選べるIMEは、日本語、SKKです。何もIMEを選ばないときは英語モードです。SKK IMEは新規実装します。辞書はEmacsのものを使ってください。」
- 2026-10-04 夜「Emacsの辞書は重複して持ってください。別々に管理します。」（SKK の IME 用に Emacs の辞書の複写を持ち、日本語の IME の辞書・`userland/base/emacs/dict/` とは独立に更新する）
- 2026-10-04: IME の状態は app ごとに記憶する（ws095-p016）。SKK の mode も同じ単位で記憶する。
- 2026-10-05 ユーザー「SettingsのLanguagesタブで選択」（UI の言語の選択も Languages の頁に置く、[WS158](../ws158/ws.md) の翻訳）
- 2026-10-05 夜: SKK の `>`・`/`・`#`・Tab・注釈は後回し（ユーザーの決定、[future-work.md](../future-work.md) の F-079）
- 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- Settings の Languages の頁（日本語・SKK・なし）、compositor の input method による IME の切り替え、SKK の IME（状態機械・送り仮名・候補の選択・辞書の登録・利用者の辞書の保存）、辞書の package `ime-dict-skk`（`tools/release/license-components.json` に Zlib で登録）が main にある。
- 証拠: host の SKK の試験、QEMU の T1-173b（languages-p002: ja・none・SKK の変換と確定・頁の switch）と T1-174b（languages-p004: skk-katakana・app ごとの記憶・skk-latin）。5330 の UAT の操作感は 2026-10-13 のユーザーの一括の cleared に含まれる（個別の観察の記録は無い）。

## 制限・移管

- SKK の接頭・接尾辞（`>`）・略語（`/`）・数値（`#`）・補完（Tab）・注釈は F-079。
- 規約の全文は WS の外の一括の見直しへ（Phase は未作成）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws154-p001 | 設計（IME の切り替え、Languages の頁、SKK の状態機械と範囲、辞書の path、試験） | cleared |
| ws154-p002 | IME の選択の仕組みと Languages の頁（日本語・なし） | cleared（T1-173b） |
| ws154-p003 | SKK の IME の実装（host の試験） | cleared（host、T1-173b・T1-174b） |
| ws154-p004 | SKK を選択に加え、QEMU と実機の UAT | cleared（T1-174b、UAT は 2026-10-13 の一括） |

## 記録の所在

Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws154/` にある。WS158 が使う image の config `config-amd64-languages.mk` は plan/tools/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
