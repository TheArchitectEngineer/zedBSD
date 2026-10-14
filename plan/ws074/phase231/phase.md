<!-- awesome-plan project=zedbsd record=ws074-p231 -->

# ws074-p231: google.com の script と Grid の高さ（BigInt、型付き配列ほか、Grid の max-height と auto margin、dynamic link）

B1 (2026-10-14)。Q1 の追加指示（原文「Googleでロゴと検索の箱が、Chromium より約 75px 上に寄っていますので、Grid の高さの扱いの差を修正してください。」「Googleで右上の「Gmail」「画像」と、下の「English」の行が出ませんので、ページの script が `BigInt` の未対応を修正してください。」）。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p228 の続き。

## 原因と直し

1. **Grid の高さ（ロゴが約 75px 上）**: 原因は 2 つ。
   - `layout/grid.c` `grid_relay_item`: item の `height:100%`（area の高さ）に、item 自身の `max-height`・`min-height` を掛けていなかった。Google のロゴは `.naW5gc{height:100%;margin-top:auto}` + `.uZekje{max-height:92px}` で、wrapper が 208px のまま出ていた。`grid_clamp_height` を足した。
   - block 軸の auto margin（`margin-top:auto`）は、row が px の長さの時だけ配られていた。`grid-template-rows:minmax(0,1fr)`（または `1fr`）で container の高さが定まっている時も配るようにした（item は下へ寄る）。最小例 [repro2.html](evidence/grid-auto-margin-repro.html)（Chromium と 100.00% 一致）。
2. **BigInt**: 新規 `vm/bigint.c`（任意精度の整数、四則・剰余・べき・比較・bit 演算（2 の補数）・shift・`asIntN/asUintN`・基数つき文字列）と `js/builtin_bigint.c`（`BigInt()`、`BigInt.prototype.toString/valueOf/toLocaleString`、`asIntN/asUintN`）。literal（`123n`・`0x1fn`・`1_000n`）、`typeof`、`==`/`===`/大小比較（Number・string との混在）、`++`/`--`/`-`/`~`（新 opcode `VM_OP_TO_NUMERIC`）、Number との算術は TypeError、`+x` は TypeError、`JSON.stringify` は TypeError、Map/Set の key、`Number(1n)`。試験 `plan/ws074/tests/js/bigint.js`（Chromium の出力と一致）。
3. **型付き配列**（BigInt の次に script が止まったのは `Uint8Array is not defined`）: 新規 `js/builtin_typed.c`: `ArrayBuffer`、11 種の typed array（BigInt64 を含む。index の読み書きは native property operations、`set`・`subarray`・`slice`・`buffer`・`byteOffset` は C、`map`・`filter`・`sort`・`forEach` など Array の総称 method に被せる分は page の言語の prelude）、`DataView`。`vm_set` が typed array の代入を realm で変換（BigInt array へ Number を入れると TypeError）。試験 `tests/js/typed.js`。`WeakRef`・`FinalizationRegistry`（強参照）。
4. **Array.from の iterable**: 従来は array-like（length）だけで、`Array.from(new Set(..))` が空だった。`Symbol.iterator` を持つ値は iterator で集める。
5. **script が止まる／効かない原因（google.com の xjs）**:
   - `document.currentScript` を実装（script 実行中だけ）。`defer` の外部 script を parse の後に順番に実行（以前は parse 中に実行され `document.body` が null で止まった）。
   - `structuredClone`、`crypto.getRandomValues`・`randomUUID`（`/dev/urandom`）、XHR の `addEventListener`・`removeEventListener`・`dispatchEvent`。
   - 新規 `bind/prelude.c`（元の JS は [prelude.js](../phase230/prelude.js)、`make-prelude.py` で `.inc` を作る）: `AbortController`/`AbortSignal`、`URL`/`URLSearchParams`（`net_url_parse` を使う）、`Intl`（locale data なしの骨格）、`Reflect`、`matchMedia`、`history`、`MessageChannel`、`requestIdleCallback`、`Object.fromEntries`、`String.prototype.matchAll`、`AggregateError`、`Error.captureStackTrace`、`PerformanceObserver`、`customElements`（骨格）ほか。
   - class が `EventTarget`・`Event` を継承した時の prototype（`new.target`）が反映されなかった（`bind/event.c`）。
   - window の named property（id が `o` の要素）が、同名の `var o` への代入を隠していた（`bind/window.c`、writable にした）。
   - **dynamic な `<link rel=stylesheet>`**: `HTMLLinkElement` の `href`・`rel`・`media`・`type` などが属性へ反映されなかった（プロパティを入れただけ）。`<link>` に interface を足した。さらに sheet が届いたら link に `load`/`error` を発火（Google は `media="print"` で入れて onload で `all` に変える）。これで右上の「Gmail」「画像」が出る。
   - 診断の道具: `BROWSER_NET_DUMP=DIR`（取得した body を保存、XHR の結果を stderr）、`BROWSER_STACK`/`BROWSER_STACK_ALL`（throw 時の frame の line:col）、`BROWSER_EVAL_DUMP=FILE`（eval・Function の source）、`Cannot read properties of null (reading 'x')` の property 名。
6. 「English」の行は script の欠落ではなく server が出し分ける物だった。live-compare の Chromium は `Accept-Language` が `en-US` で、ours（`ja,en`）と違う言語の版が返っていた。`plan/ws074/tests/live-compare.py` の Chromium に `--accept-lang=ja,en-US;q=0.9,en;q=0.8` を足した（比較の条件を揃える。参照の画像の書き換えではない）。

## 確かめ（host）

- google.com live の一致（ink、全体）: p228 の終わり 23.47%（97.30%）→ **73.28%（99.36%）**。script errors 2 → 0。左から ours・Chromium・差分: [after](../../../build/review/b1-google-p231-after.png)、[before](../../../build/review/b1-google-p231-before.png)。
- 残りの差: 検索の箱の左の icon（Chromium は虫眼鏡、ours は「+」。server の A/B）、「Gmail」「画像」の横の位置が数 px、text の縁の antialias。
- build warning 0（`sh plan/ws074/tests/host-build.sh plain`）。Acid3 score 100・差 0、Acid2 差 1,865（不変）。host の JS 16/16（bigint・typed を追加）、CSS rule model 51/51、Grid 48/48。固定 WPT の 1,455 ID（`build/b1y/runall.py`、同条件）: 前（この変更の前の worktree）483 pass・callback 10,982、後 483 pass・callback 10,982、whole・callback の後退 0。
- 未実施: QEMU・実機。

## 残り

- XHR は同期の GET のみ（POST・header・status の実値なし）。google の `ogads-pa` の POST は GET として送られ 404 になる（画面に影響なし）。
- `Proxy` 未実装。`Blob`・`File`・`FileReader`・`FormData`・`Headers`・`Request`・`Response`・`WebSocket`・`IndexedDB`・`Worker` なし。
