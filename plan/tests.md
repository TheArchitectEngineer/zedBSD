# 試験の計画（シナリオ試験と AAT）

2026-10-05 Q1。ユーザー:
「AATはテストシナリオのドキュメントを元にエージェントが操作できるように設計してください。特殊な設計というわけではないですが。シナリオテスティングのシナリオは、ソースコードと同じように、我々の大きな財産です。tests/以下に整理して管理しましょう。AATにおいて、どのシナリオを実行するかは、考えてやっていきたいですね。OSの機能性ごととか、アプリごとの回帰試験のシナリオとか。新規実装した場合は、シナリオテストの合格を目指す、テスト駆動を取り入れるとか。リグレッションの範囲を考えてシナリオを選んだり、あるシナリオ集合に名前をつけてたまにフル回帰テストをしたり。」
「plan/tests.md にまとめるのがいいですね。実際のシナリオはtests/以下に、ディレクトリ分けして、markdownでもJSONでも、エージェントが理解しやすい形式で、目的と操作と確認事項と正解と確認方法を記載する感じです。」

## 1. 位置づけ

| 試験 | 誰が | どこで | 中身 |
| --- | --- | --- | --- |
| host の試験 | 実装の担当 | host | 変えた所の短い単体の試験（AGENTS.md の試験の方針） |
| QEMU の試験 | T1 | QEMU | 各 Phase の guest の script（`plan/wsNNN/tests/`） |
| **シナリオ試験（AAT）** | エージェント（T1 か専任） | 素の 5330（ユーザーが USB で起動）か QEMU | `tests/scenarios/` の文書を読み、AAT の道具（[plan/tools/aat](tools/aat/README.md)）で操作して確かめる |
| UAT | ユーザー | 素の 5330 | 機器（電源 button・蓋・USB の抜き差し・touchpad の指・YubiKey と NFC の実物・音・Wi-Fi の電波・S0 idle）と全体の使用感だけ |

シナリオはソースと同じ財産として `tests/` に置き、振る舞いを変える時は一緒に直す。

## 2. 置き場所

```text
tests/
  README.md
  scenarios/
    os/<分野>/<名前>.md|.json        起動・login・network・storage・電源・入力・音・security key …
    desktop/<分野>/<名前>.md|.json   窓・bar・App Home・screen keyboard・IME・通知・見た目 …
    apps/<app>/<名前>.md|.json       files・settings・phone・calendar・mailer・notes・pdfviewer …
  suites/
    <名前>.suite                     名前を付けたシナリオの集合
```

シナリオの id は `tests/scenarios/` からの path から拡張子を除き `/` を `.` にした物（`apps/files/mount-confirm.md` → `apps.files.mount-confirm`）。

## 3. シナリオの中身

Markdown でも JSON でもよい。エージェントが読んで迷わないことを優先する。どちらでも次の項目を持つ。

| 項目 | 内容 |
| --- | --- |
| id・題 | 上の id と 1 行の題 |
| **目的** | 何を確かめるか、なぜ要るか（関係する WS・Phase・bug） |
| 状態 | `draft`（実装の前に書いた、テスト駆動）・`active`（通るはず）・`retired`（履歴） |
| 分野・path | 関係する分野（`files`・`volumed` …）と source の path（回帰の選択に使う） |
| 環境 | `qemu`・`hardware`・`either`。人が要るか: `none`・`look`（画面を見て判断）・`hands`（物理の操作） |
| 準備 | 最初の操作の前に成り立っていること（image、差した機器、login した利用者、在る file） |
| **操作** | 番号つきの 1 つずつの操作。画素の座標でなく名前で書く（「Mount の button」「2 行目」）。画面の大きさや配置が変わっても使えるように |
| **確認事項** | 各操作の後に確かめること |
| **正解** | 確認事項の期待の値・見え方（文言、状態、窓の有無、log の行） |
| **確認方法** | どう確かめるか: 撮影（どこを見るか）、log の行（`wait-log` の pattern）、file の中身、command の出力 |
| 合格 | 全体の判定の条件 |
| 注記 | 既知の制限、人が見るべき所 |

Markdown の雛形:

```markdown
---
id: apps.files.mount-confirm
title: USB の記憶装置の mount の前に確認が出る
status: active
areas: [files, volumed]
paths: [userland/desktop/files/, userland/base/volumed/]
machine: either
human: none
since: ws132-p009
---

## 目的
...
## 準備
...
## 操作と確認
1. 操作: App Home から Files を開く。
   確認事項: Files の窓。正解: Today の頁で開く。確認方法: 撮影（窓の題と左の pane）、log `ZFILES` の行。
2. 操作: Devices の USB の記憶装置を double-click。
   確認事項: 確認の card。正解: 「Mount "STICK"?」と大きさと file system、Cancel と Mount。確認方法: 撮影、log `ZFILES DEVICE mount confirm`。
## 合格
...
## 注記
...
```

JSON なら同じ項目を `{"id":…, "purpose":…, "status":…, "areas":[…], "paths":[…], "machine":…, "human":…, "setup":[…], "steps":[{"action":…, "check":…, "expect":…, "how":…}], "pass":…, "notes":…}` の形で持つ。

## 4. suite

`tests/suites/<名前>.suite` に 1 行 1 つ（id か pattern、`#` は注記）。pattern は id（`apps.files.*`）か分野（`area:volumed`）。

| suite | 目的 |
| --- | --- |
| `smoke` | 数分。どの試験の image でも最初に流す |
| 分野・app ごと（`files`・`settings`・`ime` …） | その分野・app の作業の時 |
| `hardware` | 素の実機が要るシナリオ |
| `full` | 全部の active。たまに（定期）とリリースの前のフル回帰 |

## 5. どれを流すか

- **新しい機能**: Phase の設計で、まず `draft` のシナリオを書く。実装はそれの合格を目指し、合格したら `active` にする（テスト駆動）。Phase の受け入れに「シナリオ X が pass」を入れる。
- **変更**: 変えた file に `paths` が重なるシナリオ＋ `smoke`。選ぶのは `plan/tools/aat/select-scenarios.py RANGE --explain`（`--gaps` はどのシナリオにも当たらない変更、シナリオを足す候補）、流すのは `run-aat.sh TARGET OUTDIR changed:RANGE`（ws173-p006）。
- **bug の修正**: 再現のシナリオを足すか広げ、直ったままを保つ。
- **分野・app の作業**: その suite。
- **定期・リリースの前**: `full`。
- AAT の各回で、どの suite・シナリオを流したかと理由をこの文書の §7 に記録する。

## 6. 実行と記録

エージェントはシナリオを読み、AAT の道具（click・type・key・撮影・log の待ち）で操作し、各操作で「したこと・見た物・撮影・判定」を記録する。判定は `pass`・`fail`（どの操作で、何が違ったか、証拠）・`needs-person`（`look`・`hands` の操作）。suite の実行は、各シナリオの判定と撮影の path の一覧を作る。`needs-person` は UAT に回す。記録は `plan/ws173/runs/<日付>-<suite>.md`（撮影は build の中、path を書く）。

## 7. AAT の実行の記録

（AAT の各回の日付・image・suite・結果の要約・記録への link をここに足す）
