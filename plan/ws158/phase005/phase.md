<!-- awesome-plan project=zedbsd record=ws158-p005 -->
# ws158-p005: 日本語の翻訳と用語集、review（ベータ2 の目標）

Parent: [WS158](../ws.md)
Status: in-progress（2026-10-06 q821 P2: 用語集を書き、今ある 3 つの catalog（wayland・settings・files）を review して揃えた。tr-host-test PASS。ユーザーの日本語の review と QEMU の PNG（p006）は残り）
Disposition: normal
Queue: q821（P2 の第 1 段の列、2026-10-06 Q1）
依存: [p003](../phase003/phase.md)・[p004](../phase004/phase.md)

## 範囲（ws.md の 4）

用語集（Files・Settings の頁名・操作の名前の統一）と翻訳の review。第 1 段の規則（2026-10-06 ユーザー）で正常系だけ。p004b（menu の言語の追従・
複数形の検索の要約・Files の message と context menu・Settings の他の頁の本文）はベータ2 の後（Q1 の決定）なので、この Phase の対象は今ある catalog。

## 実装（2026-10-06 P2）

- **用語集** `userland/desktop/locale/ja/glossary.md`（新規、catalog と同じ所。install は `*.tr` だけなので image には入らない）: 文体（です・ます、名前と
  button は名詞句、「...」、全角の括弧、和欧の間の空白、人の名前に「さん」、`{1}` の位置）、英語のままの名前（Kei・App Home・Wiseview・Wi-Fi など）、
  語の表（ウインドウ・フォルダ・接続する・パスワード・外観・壁紙・表示の言語・入力方式・情報・ようこそ・今日 など 50 語）。
- **review と直し**: 3 つの catalog の全部の訳（wayland 約 120・settings 105・files 33）を用語集と照らした。揃えた所: 「つなぐ」→「接続する」（Settings の
  Wi-Fi・Bluetooth・VPN の要約、Welcome の Network の段）、「Connecting...」の「接続中...」→「接続しています...」（他の進行中の文と同じ形）。
- **ws164-p002 の Welcome の文**: `settings.keys` に段の見出し・要約・key の表の 16 件を足し、`tools/i18n/tr.py update` で catalog に入れて 23 件を訳した。
  挨拶は `kl_tr_format("Welcome to Kei, {1}")`（「Kei へようこそ、{1} さん」、Files の挨拶と同じ形）。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws158/tests/tr-host-test.sh`（catalog 3 つの `check --strict`、tr.py の自己試験） | PASS（settings 105/105 訳済み） |
| `sh plan/ws164/tests/run-host-welcome.sh` | PASS（welcome.c の挨拶の変更の後） |
| build: zedBSD の `bin/settings` | warning 0 |

## 未実施・残り

- 日本語の母語話者（ユーザー）の review: 用語集と訳の採否。人間の判断なので Q1 経由でユーザーへ。
- QEMU の日本語の PNG（p006）。
