# zedBSD の計画（plan/）

Awesome Plan の記録。規則はリポジトリ直下の [AGENTS.md](../AGENTS.md)。どれを読むかは AGENTS.md §1。

| file | 中身 | いつ読むか |
| --- | --- | --- |
| [master.md](master.md) | 今の状態（担当・Queue・Focus・優先順位・未決・次の番号）、目的・Milestone、未完の WS の一覧 | 毎回の始め |
| [queue.md](queue.md) | 今の Queue（承認・範囲・担当・結果） | Q1 は毎回、担当は自分の行 |
| [agents/protocol.md](agents/protocol.md)・[agents/registry.md](agents/registry.md) | 担当の運用、担当の台帳 | 担当の起動・Q1 の割当の時 |
| `wsNNN/ws.md`・`wsNNN/phaseNNN/phase.md` | WS と Phase（決定の原文・再開点・結果） | 担当の Phase の時 |
| [guardrail.md](guardrail.md) | 範囲・HAL・出典の規則・承認済みの HAL 差分 | source を変える前に該当の節 |
| [coding-style.md](coding-style.md) | C の規約（全文が正本） | C を書く前に |
| [master-design-policy.md](master-design-policy.md) | 設計方針、全体に効く決定（§11）、対象 platform | 設計の前に |
| [tools.md](tools.md) | 道具・回帰の試験・実機・QEMU の解析・環境の作り直し | 試験の前に |
| [tests.md](tests.md) | シナリオ試験（AAT、`tests/scenarios/`） | AAT を使う時 |
| [known-bugs.md](known-bugs.md)・`bugs/` | Bug Board と ticket | Bug の時 |
| [future-work.md](future-work.md) | 将来の案（実行の許可ではない） | 計画の時 |
| [workstreams-closed.md](workstreams-closed.md) | 完了・取り下げた WS の一覧 | 参照の時だけ |
| [record-integrity.md](record-integrity.md) | 記録の整合を保つ手順 | ラップアップの前 |
| [config.md](config.md)・`records.json`・`milestones.json` | Awesome Plan の設定、GitHub の Issue の対応 | GitHub の同期の時 |
| [history/](history/index.md) | 終わった Queue・Past Log・古い記録（指示ではない） | 経緯を調べる時だけ |
| `standards/` | 個別の標準（browser component など、guardrail から link） | guardrail の指示の時 |
