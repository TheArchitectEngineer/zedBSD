<!-- awesome-plan project=zedbsd record=ws142-p006 -->

# ws142-p006: 全文の規約の確認と QEMU の回帰

Status: cleared（2026-10-05 Q1: WS142 の C の全文の規約の確認（style-check 0、build warning 0、host の試験 PASS）と T1-136 の回帰 p002〜p005 が全部 PASS。FreeBSD の build は未実施（host に環境が無い））。以前: in-progress（2026-10-05 P1 generation17 / q724。全文の規約の確認と直し、build、host の試験まで。QEMU の回帰は Q1 経由で T1 に依頼。結果の判定と実機の UAT まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q724（Q1 の投入）

## 範囲

WS142 の p002〜p005 で足し・変えた C の source（`8775aadb^..HEAD` のうち WS142 の分）を [plan/coding-style.md](../../coding-style.md) の全文で確かめ、違反を直し、build・host の試験・境界の検査を回し、QEMU の回帰（p002〜p005 の guest の試験）を T1 にまとめて依頼する。

対象: `userland/desktop/wayland/` の `super-tap.c`・`.h`、`apps.c`・`.h`、`apps-bar.c`・`.h`、`switcher.c`・`.h`、`switcher-shell.c`、`touchpad.c`・`.h` の gesture、`shell.c`・`seat.c`・`home.c`・`input.c`・`display.c`・`touch.c`・`zwl.h`・`glass.h` の WS142 の hunk、`plan/ws142/tests/host-*.c`。

## 確認の方法

- `python3 plan/tools/style-check.py`（段落の comment、閉じ括弧の後の空行、条件の中の呼び出し、三項演算子、goto など）を全部の対象に。
- script で拾えない規則を、WS142 で足した行に限って補助の走査（3 つ以上の節・`&&` と `||` の混ざった 1 行の条件、比較の式をそのまま返す return、呼び出しの結果をそのまま返す return、値を変数に入れて返すだけの return、公開の関数の 1 行の comment、static 関数の前方宣言）で拾い、残りは目で読んだ（§2 の file の順、§4 の宣言の位置、§7 の loop・switch の comment、§8 の括弧、§10 の comment の中身、§11 の成功の return が最後）。

## 直したもの（2026-10-05）

| 規則 | 直した所 |
| --- | --- |
| §6 3 節以上・混ざった条件は節ごとに行を分ける | `apps-bar.c` 7 か所、`switcher-shell.c` 3、`touchpad.c` 6、`shell.c` 9、`super-tap.c` 1、`seat.c` 1、host の試験 2 |
| §6 Boolean を式で作らない（return を含む） | `apps.c` の `comes_before`、`switcher-shell.c` の `zwl_switch_on`・release の答え 2 か所、host の試験 2 |
| §11 呼び出しの結果をそのまま返さない、成功の return を最後に | `apps.c` の `zwl_apps_find`、`apps-bar.c` の `zwl_apps_bar_panel`・切り替え中の motion、`switcher-shell.c` の `bar_panel`・Alt+Tab で開く所、host の試験 1 |
| §3 公開の関数の前は複数行の comment | `apps.c` 2、`apps-bar.c` 9、`switcher.c` 3、`switcher-shell.c` 7、`super-tap.c` 1、`shell.c` 3 |
| §5 段落・if の前の comment | `switcher-shell.c` の鍵の処理を `switch` にまとめ、各段落に comment |

動きは変えていない（host の試験の数と結果が同じ）。

## 確認

| 確認 | 結果 |
| --- | --- |
| style-check（対象の全 file） | 指摘 0 |
| 補助の走査 | 残りは field・変数・単純な算術の return（§11 で許される）だけ |
| static 関数の前方宣言 | 全部ある |
| `git diff --check` | ok |
| zedBSD の compositor（`make BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| FreeBSD の build | 未実施（この host に FreeBSD の build 環境が無い。source の一覧に新しい file を足しただけ） |
| host の試験 | `run-host-apps.sh` 32、`run-host-gesture.sh` 50、`run-host-switcher.sh` 20、`run-host-super-tap.sh` 16、ws159 `run-host-touchpad.sh` 25（全部 ok、apps・switcher は ASan・UBSan でも）、ws131 `host-system.sh`・ws102 `host-keyboard.sh` PASS |
| `plan/tools/keiland-os-boundary/check.sh` | PASS |
| QEMU の回帰 | **未実施**。T1 に依頼（Q1 経由、下） |
| 実機（5330） | **未実施**（UAT） |

## QEMU の回帰（T1 への依頼）

pen の image（`plan/ws079/tests/build-pen-image.sh BUILD`、main の最新）を `plan/ws079/tests/pen-guest.sh start IMAGE` で起動し、同じ guest で次を順に流す（BUILD は main の最新の `bin/wayland` を含む build）:

1. `plan/ws142/tests/p002-guest.sh BUILD`（Windows キー）
2. `plan/ws142/tests/p003-guest.sh BUILD`（gesture）
3. `plan/ws142/tests/p004-guest.sh BUILD`（バーのアプリ）
4. `plan/ws142/tests/p005-guest.sh BUILD`（切り替え）

合格: 各 script の全行 ok（行の一覧は p002〜p005 の phase.md）。PNG（home-open、wiseview-pad、preview-hover、docked、switch-bar、switch-center）を目で見る。

## 残り

- QEMU の回帰の判定、実機の UAT（WS142 の全部）。
- WS142 の完了（ws.md の受け入れの確認）は Q1。

## Q1 の判定（2026-10-05）

WS142 の C の全文の規約の確認（style-check 0、build warning 0、host の試験 PASS）と T1-136 の回帰 p002〜p005 が全部 PASS。FreeBSD の build は未実施（host に環境が無い）。**cleared**。
