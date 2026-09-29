<!-- awesome-plan project=zedbsd record=ws095 -->

# WS095: IME（Wayland の標準の方法、まず日本語）

<!-- awesome-plan-current:start -->
Status: incomplete（p001 cleared）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p004（protocol・zdesktop の仲介・IME の program・guest の試験 status=0 を 2 回、boot-test PASS）cleared（2026-09-29）。次: p012（補いの辞書の千語と活用の注釈、held-out）か p005（候補の窓と indicator）
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
| [ws095-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| [ws095-p002](phase002/phase.md) | 日本語の engine（Wayland 無し）: ローマ字・辞書・活用の規則・分割・候補・利用者の辞書、固定の辞書で host の試験 | cleared（2026-09-29） | p001 |
| [ws095-p003](phase003/phase.md) | 辞書の package（pin した tarball の取得・検証）、100 文での品質の計測、補いの辞書の案（ユーザーと相談） | cleared（2026-09-29） | p002、D1・D3 |
| [ws095-p004](phase004/phase.md) | protocol の記述、zdesktop の仲介（起動と信頼・key の経路・Alt+Space・watchdog）、IME の program（日本語の engine の結線を含む）、ime-probe の guest の試験 | cleared（2026-09-29） | p002 |
| ws095-p005 | 候補の窓の合成、indicator と status の languages、IME の中の key の repeat、guest の試験（日本語の engine の結線は p004 に移した） | planning | p003・p004 |
| ws095-p006 | libkeiland の text-input の helper と Terminal（password の検出） | planning | p004・p005、Terminal の CJK の font（D14） |
| ws095-p007 | Text Editor の対応（WS092 の口） | planning | p006、WS092 |
| ws095-p008 | zdesktop の自前の field（titlebar の検索）と Files の field | planning | p005・p006 |
| ws095-p009 | Browser の text field | planning | p006 |
| ws095-p010 | PS/2 の日本語の key の写し（条件付き: JIS の PS/2 keyboard の利用者が出た時、F-058 と一緒に。5330 は PS/2 だが US 配列で日本語の key が無い。main 2026-09-29） | planning | JIS の PS/2 の利用者（F-058） |
| ws095-p011 | 全体の規約の適合、guest の回帰（実機の確認は別に記録） | planning | p002〜p009・p012 |
| ws095-p012 | 補いの辞書を千語へ広げ、補いの辞書の候補に活用の種類の注釈（SKK の `;…`）を足して engine が読む。held-out の文 100 以上を書き下ろし、拡張の前後で測る（ユーザーの答え 2026-09-29 夜） | planning | p003・p004 |
