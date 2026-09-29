<!-- awesome-plan project=zedbsd record=ws095 -->

# WS095: IME（Wayland の標準の方法、まず日本語）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）の design.md を全節書いた（2026-09-29、wrap up で中断）。次: design.md の review と §14 の判断の提示（phase001 の「残り」）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「IMEの追加（Wayland標準の方法にて。単一のIMEがシステムにデフォルトで入っており、言語は切り替えでき、1つのIMEが複数の言語に対応している。まずは日本語のみの変換を可能にする。日本語の形態素解析は非常に簡単でよく、名詞＋助詞、動詞＋送り仮名、くらいの分解でよい。REmacsに入っている日本語辞書をベースにする。足りない分は要相談。）」

- Wayland の標準: compositor が `zwp_input_method_v2`（IME の process）と `zwp_text_input_v3`（app の側）を仲介する。IME は 1 つの program が
  system の既定として入り、言語（まず日本語、切り替えの仕組みは複数言語の前提）を持つ。
- 日本語: ローマ字 → かな、変換（名詞＋助詞、動詞＋送り仮名 程度の簡単な分割）、候補の窓、確定・取り消し。辞書は REmacs の辞書
  （`dict/SKK-JISYO.remacs`・`SKK-JISYO.X`、SKK の形式）を土台にする。
- 辞書の license（2026-09-29）: REmacs の辞書の header は「remacs と同じ license（GPL）」とあるが、ユーザー「REmacsは私が著作権者なので、気にしなくていいです。」→ 著作権者の許可として Kei の IME の辞書に使う。
- 使う app の側: Terminal・text editor（WS092）・ブラウザの text field・Notes・Settings の検索（text-input-v3 の対応）。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws095-p001](phase001/phase.md) | 設計（[design.md](design.md)） | in-progress（design.md 全節、review と判断の提示が残り） | — |
| ws095-p002 | 日本語の engine（Wayland 無し）: ローマ字・辞書・分割・候補・利用者の辞書、host の試験 | planning | p001 |
| ws095-p003 | 辞書の package（X の取得と install、LICENSE）、変換の品質の計測と補いの辞書の案（ユーザーと相談） | planning | p002 |
| ws095-p004 | protocol の client の記述と zdesktop の仲介、IME の program（候補の窓・indicator）、ime-probe の guest の試験 | planning | p002 |
| ws095-p005 | libkeiland の text-input の helper と Terminal の対応 | planning | p004 |
| ws095-p006 | Text Editor の対応（WS092 の口） | planning | p004・p005、WS092 |
| ws095-p007 | zdesktop の自前の field（App Home・titlebar の検索）と Files の field | planning | p004 |
| ws095-p008 | Browser の text field | planning | p004・p005 |
| ws095-p009 | 全体の規約の適合、guest の回帰、実機の確認 | planning | p002〜p008 |
