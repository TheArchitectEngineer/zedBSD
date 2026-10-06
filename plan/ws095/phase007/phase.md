<!-- awesome-plan project=zedbsd record=ws095-p007 -->

# ws095-p007: Text Editor と Notes の確認と不足の修正

Status: cleared（2026-10-06 Q1 判定: T1-241 で Text Editor の ime-p007 PASS。Notes の text input はユーザーの決定で別の Phase ws079-p017 に移した。Settings の検索の日本語は記録だけ）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: q804（P1）
Prerequisites: p005。Text Editor は既に `kui_window_text_input` を呼んでいる（textedit/main.c）
Investigation bound: timebox 2〜3h

## 範囲

- Text Editor（WS092）で preedit の表示・cursor の矩形・確定・取り消し・複数行を確認し、不足を直す。
- Notes（libkeiui の window）に text input を足す。
- Settings の検索の field が text input を求めるかを確認（求めない場合は記録だけ、修正は WS089 の担当へ）。

## 受け入れ

zedBSD QEMU で Text Editor と Notes に日本語を入力・確定・保存し、再読み込みで一致。PNG。既存の textedit の host 試験（`plan/tools/textedit/`）に回帰無し。

## 所有 path

`userland/desktop/textedit/`・`userland/desktop/notes/`、`plan/ws095/`

## 未決の判断

なし

## Standards / evidence

[Guardrail](../../guardrail.md)、[C全文規約](../../coding-style.md)、[design.md](../design.md) を変更前に読む。新しい C は全文規約。zedBSD の起動は `plan/tools/boot-test.sh` だけ、guest の操作は serial/SSH、画面は PNG で目視しユーザーに見せる。serial/console log を判定に使わない。共有 `build/` と toolchain は読取専用。全体の `make check` は禁止。QEMU の証拠と実機の証拠を分け、やっていない確認は未実施と書く。具体コマンド/結果は実行時に記録する（今は計画のみ）。

2026-10-02 / ws095-beta1-plan-20261002: 計画担当が作成。

## q804（P1、2026-10-06）

調べた事実:
- **Text Editor**: preedit の表示（下線・変換中の segment）・cursor の矩形・候補・確定・複数の segment は [p013](../phase013/phase.md)（BUG-139、cleared）で
  QEMU の確認済み（`ime-p013.sh`）。source に足りない所は見つからなかった（`textedit/main.c` の `KUI_WINDOW_TEXT_COMMIT`・`PREEDIT`・`DELETE`、`draw.c` の `draw_preedit`、
  dialog の間は text input を off）。受け入れの残り（取り消し・複数行・保存して再読み込みで一致）の試験を足した: `plan/ws095/tests/ime-p007.sh`
  （漢字の確定、かな を Esc で取り消し（空の preedit、commit 無し）、直接入力の改行、日本語の確定、Ctrl+S、file が Hello／漢字／日本語、editor を起こし直して同じ表示と file）。
- **Notes**（`userland/desktop/notes/`）は手書きの notebook（strokes・pages、`notes.h`）で、文字を打つ field が無い。text input を「足す」には文字の入力の機能（text box 等）の
  新設が要り、それは製品の判断。→ **Q1 の判断待ち**（範囲から外すか、Notes の文字の機能を別の Phase にするか）。
- **Settings の検索の field** は text input を求めない（`userland/desktop/settings/` に text-input の呼び出しが無い）。範囲の通り記録だけ: 日本語で検索するには WS089 の担当が
  `kl_window_text_input` を検索の field の focus の間だけ on にする（keyword は英語なので、日本語の検索語は locale の keyword と合わせて要る）。

source の変更なし。確認: `sh -n`。QEMU は T1 に依頼する。
