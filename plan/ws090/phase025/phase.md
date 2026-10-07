<!-- awesome-plan project=zedbsd record=ws090-p025 -->

# ws090-p025: Browser の web の form の欄で IME を受け付ける

Status: planned（2026-10-07 Q1 が作成、ws090-p022 の項目 6 を分けた）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: q833（P1、WS031 の後）

## 範囲

- ws090-p022 の項目 6: Browser の page の `<input>`・`<textarea>` に focus がある時、IME の preedit・commit が欄に届く（正常系）。
- libbrowser の公開の browser.h の ABI は変えない（要るなら Q1 へ）。

## 確認

- host の試験（headless の page に preedit と commit を渡す）、QEMU は T1（AAT の image で Browser の form に「nihon」→ 日本）。
