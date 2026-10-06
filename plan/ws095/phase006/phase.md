<!-- awesome-plan project=zedbsd record=ws095-p006 -->

# ws095-p006: Terminal の text-input と CJK の font

Status: test-wait（q804、P1、2026-10-06 実装済み・T1 の試験待ち。下の「q804（P1）」）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: q804（Q1 の beta1/2 の残りの Queue、P1）
Prerequisites: p005（候補の窓）。D14 は本 Phase の中で小さく足す案（design §14）
Investigation bound: timebox 3〜4h

## 範囲

- Terminal（`userland/desktop/terminal/`、libkeiui の window）で `kui_window_text_input`（WS090 p013）を使い、preedit を cursor の位置に表示、commit を pty へ書く。cursor の矩形を text-input に送る（候補の窓の位置）。
- password の入力の検出（tty の ECHO が off の時は text input を無効にし、content purpose を password にする。design §4）。
- D14: Terminal の CJK の fallback の font（DroidSansFallback 等、既に image にある font を使う）を足す。Terminal の担当の WS（WS128 標準アプリ全般）と file が重なる場合は main が調整。

## 受け入れ

zedBSD QEMU の Terminal で日本語の入力・確定・表示（`echo 日本語` の結果）、`passwd` 等の ECHO off の間は IME が切替わらない、PNG。Terminal の既存の試験（host の試験があればそれ）に回帰無し、`boot-test.sh`。

## 所有 path

`userland/desktop/terminal/`、必要なら `userland/desktop/libkeiui/text-input.c`（共有 library、main に確認）、`plan/ws095/`

## 未決の判断

D14 の font を WS095 で足すか Terminal の WS に任せるか（design §14 の既定は WS095 で小さく足す）

## Standards / evidence

[Guardrail](../../guardrail.md)、[C全文規約](../../coding-style.md)、[design.md](../design.md) を変更前に読む。新しい C は全文規約。zedBSD の起動は `plan/tools/boot-test.sh` だけ、guest の操作は serial/SSH、画面は PNG で目視しユーザーに見せる。serial/console log を判定に使わない。共有 `build/` と toolchain は読取専用。全体の `make check` は禁止。QEMU の証拠と実機の証拠を分け、やっていない確認は未実施と書く。具体コマンド/結果は実行時に記録する（今は計画のみ）。

2026-10-02 / ws095-beta1-plan-20261002: 計画担当が作成。

## q804（P1、2026-10-06）: 実装

調べた事実（source の照合）:
- Terminal の text-input（preedit を cursor に、commit を pty へ、cursor の矩形）は既に入っている: `terminal/window.c`（`kui_window_text_input(…, 1)`、
  `KUI_WINDOW_TEXT_COMMIT`・`PREEDIT`）、`terminal/main.c` の `main_text_cursor`。[ws128-p011](../../ws128/phase011/phase.md)（BUG-155、T1-012 PASS で cleared）。
- D14 の CJK の fallback の font も入っている: `terminal/font.c` の `terminal_font_fallback`、既定は `KEILAND_DATADIR/fonts/keiland-fallback.ttf`。→ 未決の判断「D14 を WS095 で足すか」は不要になった。
- 残っていたのは **password の入力の検出** だけ。pty の master の ioctl は slave の tty に回る（`src/kern/tty.c` の `pty_master_ioctl`）ので、master の側から `tcgetattr` で読める。

変更（`userland/desktop/terminal/main.c`）:
- `main_secret_follow`: 毎回の loop で active な tab の master の termios を読み、**ECHO が off で ICANON が on**（sudo・su・ssh・passwd の prompt、`getpass` の形）の間は
  window の text input を off にする（`kui_window_text_input(…, 0)`、IME は activate されず key はそのまま shell へ）。ECHO が戻れば on。log `ZTERM IME secret=0|1`。
  ICANON も off の全画面の program（editor、行編集の shell）は対象外で IME が使える。
- design §4.1 は content hint（sensitive_data・hidden_text）を付ける案だったが、libkeiland の API の変更（共有 library）が要る。text input を off にすれば IME は field を受け持たず
  同じ結果（IME を通さない）になるので、library を変えない方を選んだ。

試験: 新しい `plan/ws095/tests/ime-p006.sh`（IME の Settings の image、`config-amd64-settings-ime.mk`）: 日本語の「日本語」の変換・確定・`echo` の表示（shown.png）と file、
`stty -echo; read secret` の間は `secret=1`・`ZWL IME deactivate`・Alt+Space の後の abc が preedit にならず file は abc、`stty echo` で `secret=0`・再 activate・かな の preedit。
確認: zedBSD amd64 の `bin/terminal` の build、keiland-linux の host build（どちらも `-Werror`、warning 0）、`sh -n`。QEMU は T1 に依頼する。実機は未実施。
