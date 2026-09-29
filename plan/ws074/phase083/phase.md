<!-- awesome-plan project=zedbsd record=ws074p083 -->

# ws074-p083: DOMException

Phase ID: `ws074-p083`
Parent: [WS074](../ws.md)
Status: planned
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行する予定、2026-09-29）
依存: p030（DOM の binding）、p081、p082
由来: p081 の報告（`bind_throw_dom` が `Error` を投げ `e.name` が "Error"）。main の指示（2026-09-29）で p082 の後に。

## 範囲

- `DOMException` の interface（WebIDL: 構築子 `new DOMException(message, name)`、`name`・`message`・`code`、旧来の code の定数
  `INDEX_SIZE_ERR` ほか、prototype は `Error.prototype` を継ぐ）。
- `bind_throw_dom` を DOMException の object を投げるように（name から code を引く）。
- 既存の試験の頁（dom/）で例外を `instanceof Error` だけで見ている行を、`e.name`・`e.code`・`instanceof DOMException` で見るように
  直し、Chromium の expected を作り直す。

## Resume point

- 2026-09-29: 計画のみ（p082 の後に着手）。
