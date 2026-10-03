<!-- awesome-plan project=zedbsd record=ws127-p006 -->

# ws127-p006: DnD の自動の scroll と spring-loaded（F-039）

Status: cleared（q666、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS127](../ws.md)
Queue: q666（Q1 の dispatch、2026-10-04。user「任せます」→ Q1 の採否）
依存: p001 でユーザーが採用（2026-10-04 Q1 が委任で採用）
目安: 1h（範囲の照合の後）。実行者の目安: phase-runner-mid
所有 path: `files/ui-drag.c`・`dnd.c`・`ui-grid.c`・`ui-list.c`・`ui-tabs.c` の該当、`plan/ws127/tests/`

## 範囲

窓の中の DnD で、一覧の上下の端で自動の scroll、tab・folder・sidebar の上で 0.8 秒待つと開く（spring-loaded）。

## 受け入れ

guest の手順: 1000 項目の folder の下端へ drag して scroll が進む、folder の上で待って開いて落とせる。既存の DnD の試験（files-p010 ほか）PASS。
変えた source の style-check の違反 0、build の warning 0、`files-regress.sh` の 14 本と boot test PASS。

## 範囲の照合（2026-10-04 P2、Q1 了承）

一覧の上下の端の自動 scroll と、item・sidebar の folder の spring-loaded（750 ms）は [ws127-p002](../phase002/phase.md)（q616、QEMU の
files-p002・host-spring PASS）で実装済み。この Phase で実装したのは tab の spring（drag が別の tab の上で待つとその tab が前に出る）だけ。

## 未決の判断

なし。

## 実装（2026-10-04、q666）

- `files/ui-drag.c`: `drag_spring_tab()`（drag の hit が表示中でない tab の本体か閉じる button ならその番号）。`drag_target` はその tab の上に
  来た時に spring の時計を始め（tab が folder でも Trash などでも）、`drag_spring_tick` は 750 ms で `fm_tabs_select` して target を探し直す
  （log `DRAG spring tab=N`）。前に出た tab の folder の空きへ落とせる（p002 の「開いた先の空き」の規則）。外からの drop は従来どおり folder の
  tab だけが target なので、folder の tab だけが前に出る。
- 同じ file の既存の style-check の指摘 8 件（p002 の code: 条件の中の呼び出し、閉じ括弧の後の空行、三項演算子）も直した（振る舞いは同じ）。
- 試験: [host-spring.sh](../tests/host-spring.sh) に 4（Ctrl+T の 2 つ目の tab で docs、1 つ目に戻って README.md を 2 つ目の tab の上で
  850 ms 待つと前に出て、空きへの release で docs へ移る）と 5（素通りでは前に出ない）。guest の手順 [files-p006.sh](../tests/files-p006.sh)。

## 確認（host。QEMU は T1 待ち、実機は未実施）

- `sh plan/ws127/tests/host-spring.sh` → `host-spring: PASS`（1〜5）。`host-p010.sh` PASS。
- zedBSD amd64 の build（`BUILD=build/p2-files`）rc=0、warning 0。`plan/tools/style-check.py userland/desktop/files/ui-drag.c` 0。
- 未実施（T1 へ）: guest の `files-p006.sh`（tab の bar の座標 y=8 は host の 27 から guest の item の差 20 を引いた推定）、`files-regress.sh`、
  boot test。1000 項目の folder の下端の scroll（受け入れの文）は p002 の 300 項目と host の 154 項目で確かめた形で、1000 項目では未実施。

## 検証の方法と範囲

QEMU の Venus（`plan/tools/files/files-guest.sh`、`files-regress.sh`）と host の試験。console・serial の log では判定しない。各 Phase の最後に `files-regress.sh` の 14 本と boot test。
Settings と共有の `canvas.c`・`text.c`・`icons.c` を変えたら `sh plan/ws089/tests/host-build.sh` と Settings の host 試験も流す。

## Event

2026-10-02 / ws127-beta1-plan: fg019 の計画で新設（planning）。p001 の結果とユーザーの選択で範囲を確定し planned にする。

## 判定（Q1、2026-10-04）

cleared。T1-077：files-p006・files-regress 14 本・boot-test PASS（QEMU）。実機は未実施
