<!-- awesome-plan project=zedbsd record=ws074p080 -->

# ws074-p080: Encoding API（TextEncoder・TextDecoder）と Web Storage（sessionStorage・localStorage）

Phase ID: `ws074-p080`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行、2026-09-29。最初 p079 と名付けたが、別のエージェントの
destructuring 等が先に p079 になったので p080 に付け直した）
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

## 設計と実装

- `bind/encoding.c`（新）: `TextEncoder`（mark の cell を持つ platform object）と `TextDecoder`（cell に fatal・ignoreBOM と UTF-8 の
  復号器の状態）。復号は Encoding Standard の UTF-8 decoder（lead byte の範囲で overlong と surrogate を除き、続かない byte は誤りとして
  次の列の頭に読み直す）。BOM は stream の最初の code point だけで落とす。`encode` は数の配列、`decode`・`encodeInto` は配列のような
  object（length と要素、各 byte は 256 の剰余）を受ける。label は utf-8 の 6 つ（前後の空白を除き小文字にして比べる）、他は RangeError。
- `bind/storage.c`（新）: `Storage` と window の `localStorage`・`sessionStorage`（window ごとに一つ、最初に問われた時に作り、window の
  tracer が持つ）。method は host に尋ねる。`QuotaExceededError` は host の ENOSPC から。
- `bind/bind.h`: `enum bind_storage_area`、`struct bind_storage_calls`（length・key・get・set・remove・clear）、`bind_host.storage`。
  `internal.h`・`window.c`: interface（`TextEncoder`・`TextDecoder`・`Storage`）、window の member、`bind_window` の storage の object。
- `page/storage.c`（新）: process に一つの area の表（origin と種類ごと）。item は key の code unit の順（`key(n)` の順）で、二分探索。
  origin は http・https の origin、file: の page は一つ（`file://`）、他（about:blank）は保存しない空の origin。localStorage は最初に
  使う時に file から読み、変更のたびに一時 file に書いて rename で置き換える（書けなければ memory にだけ残る）。file:
  `$XDG_DATA_HOME/keiland/browser/local-storage/<origin の byte の 16 進>.ls`（無ければ `~/.local/share/…`、directory は 0700）、中身は
  magic の行と、item ごとの key・value の長さ（32 bit LE、UTF-16 の unit）と unit（LE）。上限は origin ごとに key と value の UTF-16 で
  5 MiB（他の browser と同じ）。
- `page/script.c`: host に `page_storage_calls()`。page の URL を読む `script_url` を `page_url`（`page.h`）として公開した。
- `libbrowser/Makefile` に 3 つ。`js/`・`vm/`・`include/libc/browser.h` は変えていない。

## 試験

- `plan/ws074/tests/dom/encoding.html`（15 行）・`storage.html`（8 行）と Chromium 153 の expected（`run-dom-tests.py --reference`、他の
  expected は不変）。Chromium の `TextDecoder.decode` と `encodeInto` は `Uint8Array` しか受けないので、試験の頁は `Uint8Array` があれば
  それを、無ければ配列を使い、`Array.prototype.join` で出す。storage は最初に両方を clear し（Chromium の profile は localStorage を
  残す）、key を並べ替えて出す。
- `run-dom-tests.py`: 私たちの browser を `XDG_DATA_HOME=build/ws074-dom-tests/data` で走らせ、利用者の localStorage を触らない。

## 確認（host は Debian の cc と Chromium 153。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: 新しい 3 つの file と変えた file に指摘 0。
- 回帰（plain と ASan、同じ結果。main（p079）を取り込んだ後）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、
  host-position 19/19、host-text 20/20、host-relayout 161/161、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、
  run-dom-tests **14/14**（2 つを追加）、run-js-tests 11/11、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、
  run-font-tests 8/8。ASan の `--render`（top-local・search-local）に報告 0。
- localStorage の保存: 別の process の 2 回目の run で値が読めた（UTF-16 の surrogate pair を含む）。1 MiB の値を 3 つ入れると 3 つ目が
  `QuotaExceededError`。
- Amazon（capture、`--run`、main の p079 の後）: top-local の Uncaught は `TextEncoder` が 0 になり、残りは async functions 1・for-of 1
  （`js/` の側）。search-local は `TextEncoder`・`sessionStorage` の 2 つが 0 になり、残りは async 2・classes 2・for-of 1 と `(at 1:1)` の
  `Cannot read properties` 2。
- **`(at 1:1)` の原因（main の依頼で調べた）**: search の inline script の 70 番目（と 77 番目）が `D=class{static listenForSFIFrameLoad…}`
  を含み、classes の SyntaxError で丸ごと走らない。そのため `window.grandprix.wrappers` が定義されず、直後の 72 番目（と 79 番目）の
  1 行の script `window.grandprix.wrappers.listenForSFIFrameLoad("ape_Search_…_iframe", …)` の 1 行 1 桁で `Cannot read properties of
  undefined` になる。DOM の側の未実装ではなく、`js/` の classes が入れば消える見込み。
- `live-compare.py`（script 付き）: top-local 画素 64.47%・ink 57.66%、search-local 76.04%・ink 33.01%（不変）。
- guest（QEMU、worktree の image を作り直した。clang・libcxx を含まない config）: run-dom-tests `--outputs` **14/14**（`XDG_DATA_HOME=/tmp/xdg`）。
  live の `https://www.amazon.co.jp/` の `--run`（取得 1 回）: **Uncaught 0**。localStorage は `/root/.local/share/keiland/browser/
  local-storage/<https://www.amazon.co.jp の 16 進>.ls` に保存された（156 byte）。
- boot test: PASS（worktree の `build/p031/boot-test-p080/login.png`）。
- 実機: 未実施。guest の窓（zdesktop）での表示: 未実施。

## 写真

- host: worktree の `build/ws074-shots/p080-20260929-amazon-top-local-side.png`・`…-search-local-side.png`。

## 残り・制限

- 型付き配列（`Uint8Array`・`ArrayBuffer`・`DataView`）が engine に無い（`js/` の側の仕事。main に依頼）。それまで `encode` は配列。
- `localStorage.name` の形の読み書き、`storage` event、UTF-8 以外の encoding、tab ごとの sessionStorage。
- 注意: 試験の道具のうち、script を走らせる host-relayout と live-compare は `XDG_DATA_HOME` を決めず、利用者の
  `~/.local/share/keiland/browser/local-storage/` に file: の origin の file を作る（この Phase の確認の途中で一度作り、消した）。
  run-dom-tests は分けた。他の道具も分けるかは main の判断。

## 結果

- cleared。Amazon の `TextEncoder is not defined` と `sessionStorage` の `Cannot read properties` が無くなり、guest の live の top は
  Uncaught 0。Chromium と一致する試験 2 つを足した。
