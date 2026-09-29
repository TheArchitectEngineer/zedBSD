<!-- awesome-plan project=zedbsd record=ws095 -->

# WS095: IME（Wayland の標準の方法、まず日本語）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
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
| ws095-p001 | 設計 | planning | — |
