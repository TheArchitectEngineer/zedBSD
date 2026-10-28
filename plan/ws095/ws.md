<!-- awesome-plan project=zedbsd record=ws095 -->

# WS095: IME（Wayland の標準の方法、まず日本語）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。IME（日本語・辞書・候補の窓・status・app ごとの記憶）は main。p009（Browser の欄の IME）は ws090-p025 で達成、p010 は JIS の PS/2 の利用者が出た時（F-058））
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-09-29 ユーザー「IMEの追加（Wayland標準の方法にて。単一のIMEがシステムにデフォルトで入っており、言語は切り替えでき、1つのIMEが複数の言語に対応している。まずは日本語のみの変換を可能にする。日本語の形態素解析は非常に簡単でよく、名詞＋助詞、動詞＋送り仮名、くらいの分解でよい。REmacsに入っている日本語辞書をベースにする。足りない分は要相談。）」

compositor が `zwp_input_method_v2`（IME の process）と `zwp_text_input_v3`（app の側）を仲介し、system の既定の 1 つの IME がローマ字 → かな → 変換・候補の窓・確定を行う。設計は [design.md](design.md)。

## ユーザーの決定

- 2026-09-29（辞書の license）: REmacs の辞書の header は GPL とあるが「REmacsは私が著作権者なので、気にしなくていいです。」→ 著作権者の許可として Kei の IME の辞書に使う。
- 2026-09-29 夜: 補いの辞書を千語へ広げ、held-out の文 100 以上で拡張の前後を測る（p012）。
- 2026-10-02「IMEは人間の作業を完了したので、あなたが担当します。」
- 2026-10-02: 日本語入力が実機でできることを確認。「IMEのステータスを画面右上の通知領域に追加してください」→ p005 の indicator。「入力文字のサイズが小さくておかしかった…テキスト本文と同じ文字の大きさにしたい。UI/UXの完成度の要」→ p013（BUG-139）。
- 2026-10-02「IME の辞書の大きさはこのままで大丈夫です。もっと増やしてもいいくらいですが、キリがないので、ひとまずこれでいいです。もし増やしても問題ないとだけ伝えておきます。」→ 1,478 見出しで確定。
- 2026-10-02: ブラウザ（WS074）はこの session の対象外（Codex の担当）。
- 2026-10-04: 確定のたびの保存をやめ、入力の無い 3 分の後と IME の終了の時に保存（p015）。
- 2026-10-04: IME の状態を app ごとに記憶（p016）。
- 2026-10-05: IME の辞書を /usr/share/keiland/ime へ移し 1 つの file にまとめる（p017）。
- 2026-10-07「…IMEの続き…」をベータ3 へ、2026-10-13「WS095はベータ2に入れます。優先度低めでスケジューリングします。」
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p011 は WS の外の一括の見直しへ。

## 結果

- 日本語の engine（ローマ字・辞書・活用の規則・分割・候補・利用者の辞書）、辞書の package（pin した tarball の取得・検証）、zdesktop の仲介（起動と信頼・key の経路・Alt+Space・watchdog）、IME の program、候補の窓・system bar の indicator（A／あ）・key の repeat、preedit の文字の大きさ（p013）が main にある。
- 使う側: Terminal（text-input と password の時の停止、CJK の fallback の font）・Text Editor・Files の field・titlebar の検索（p006〜p008）、Notes は ws079-p017、Browser の form は ws090-p025。
- 補いの辞書は 1,478 見出し。100 文 92→97、held-out A（125 文）64→102（盲検）・109、B（110 文）47→81（p012、host）。
- 利用者の辞書の保存を別の thread・3 分の無入力の後に（p014・p015、BUG-143）、app ごとの状態の記憶（p016）、辞書の場所の一本化（p017）。
- 証拠は host 試験と QEMU の Venus の guest 試験（T1-024・T1-051・T1-119・T1-241・T1-283 ほか）。実機（5330）の日本語の入力は 2026-10-02 のユーザーの確認。

## 制限・移管

- p010（PS/2 の日本語の key の写し）は JIS の PS/2 keyboard の利用者が出た時（F-058）。
- 規約の全文と guest の回帰（p011）は WS の外の一括の見直しへ。
- 直接入力の遅れは BUG-143 に残した。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws095-p001 | 設計（design.md） | cleared（2026-09-29） |
| ws095-p002 | 日本語の engine（Wayland 無し） | cleared（2026-09-29） |
| ws095-p003 | 辞書の package と 100 文の品質の計測 | cleared（2026-09-29） |
| ws095-p004 | protocol・zdesktop の仲介・IME の program・ime-probe | cleared（2026-09-29） |
| ws095-p005 | 候補の窓・indicator・key の repeat | cleared（2026-10-02、q604、QEMU） |
| ws095-p006 | Terminal の text-input と CJK の font | cleared（2026-10-06 Q1、QEMU の T1） |
| ws095-p007 | Text Editor と Notes の確認 | cleared（2026-10-06 Q1、T1-241。Notes は ws079-p017） |
| ws095-p008 | titlebar の検索と Files の field | cleared（2026-10-07、T1-283） |
| ws095-p009 | Browser の text field | ws090-p025 で達成（Phase の directory なし） |
| ws095-p010 | PS/2 の日本語の key の写し（条件つき） | 着手せず（F-058、Phase の directory なし） |
| ws095-p011 | 全体の規約の適合と guest の回帰 | planning のまま WS を閉じた（規約の全文は WS の外の一括の見直しへ） |
| ws095-p012 | 補いの辞書を千語へ、held-out の計測 | cleared（2026-10-02、q593） |
| ws095-p013 | preedit の文字の大きさ（BUG-139） | cleared（2026-10-02、q603、QEMU） |
| ws095-p014 | 利用者の辞書の保存を別の thread へ（BUG-143） | cleared（2026-10-03 Q1、T1-024） |
| ws095-p015 | 保存を 3 分の無入力の後と終了の時に | cleared（2026-10-04、q652、T1-051） |
| ws095-p016 | IME の状態を app ごとに記憶 | cleared（2026-10-05 Q1、T1-119） |
| ws095-p017 | 辞書を /usr/share/keiland/ime の 1 file に | cleared（2026-10-05 Q1、T1-119） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）と `p005-wip.patch` は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws095/` にある。Queue の履歴は [plan/history](../history/index.md)。
