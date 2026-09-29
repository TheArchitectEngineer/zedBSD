<!-- awesome-plan project=zedbsd record=ws074p080 -->

# ws074-p080: Encoding API（TextEncoder・TextDecoder）と Web Storage（sessionStorage・localStorage）

Phase ID: `ws074-p080`
Parent: [WS074](../ws.md)
Status: in-progress
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom`（main a66edf2d を取り込んだ後）で実行、2026-09-29）
依存: p030（DOM の binding）、p031（Amazon の DOM の API）
由来: p031 の後、Amazon の top・search の AUI の script が `TextEncoder is not defined`、search の EWC の cache が `sessionStorage` の
無い `Cannot read properties` で止まる（p031 の phase.md「Amazon の次の blocker」）。main の指示（2026-09-29）で bind/・page/ の側の
Phase として立てた。p064 の行の localStorage はこの Phase が受け持つ（fetch・XHR・Location・History は p064 に残る）。
並行: WS074 の別のエージェントが `js/`・`vm/`（destructuring・spread・optional chaining）を進めている。この Phase は `js/`・`vm/` を
変えない。

## 範囲

1. **Encoding API**（UTF-8 だけ）: `TextEncoder`（`encoding`・`encode`・`encodeInto`）、`TextDecoder`（label は utf-8 とその別名、他の
   label は RangeError。`fatal`・`ignoreBOM`・`decode` の `stream`）。WHATWG の UTF-8 の復号（不正な列は U+FFFD、fatal なら TypeError）と
   符号化（孤立した surrogate は U+FFFD）。
2. **Web Storage**: `Storage`（`length`・`key`・`getItem`・`setItem`・`removeItem`・`clear`）、window の `sessionStorage`・`localStorage`。
   origin ごと（http・https は origin、file: は一つ）。sessionStorage は process の間だけ、localStorage は file に保存
   （`$XDG_DATA_HOME/keiland/browser/local-storage/`、無ければ `$HOME/.local/share/…`）。origin ごとの上限 5 MiB を超える `setItem` は
   QuotaExceededError。

この Phase に無いもの: 型付き配列（`Uint8Array`・`ArrayBuffer`。engine に無い: `js/` の側の仕事）。`encode` は数の配列を返し、
`decode` と `encodeInto` は配列のような object を受ける。`localStorage.foo` のような名前での読み書き（exotic object が無い）、`storage`
event、UTF-8 以外の encoding、他の tab との sessionStorage の分離（process ごとに一つ）。

## Resume point

実装（`bind/encoding.c`・`bind/storage.c`・`page/storage.c`）と試験（`plan/ws074/tests/dom/` に Chromium の expected）。
