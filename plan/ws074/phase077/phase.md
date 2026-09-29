<!-- awesome-plan project=zedbsd record=ws074p077 -->

# ws074-p077: Amazon が要る window の環境の最小（navigator・screen・performance・location・Image）

Phase ID: `ws074-p077`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行。main の指示で p065 の残りから切り出した）
依存: p030（DOM の binding）、p076（`Date`）
由来: p076 の後、Amazon の AUI の loader（capture の 5 番目の script）が `navigator is not defined` で止まり、`P`（AmazonUIPageJS）が
定義されず、後の script が `P is not defined`（top 37、search 26）で落ちていた。main の指示（2026-09-29）で p065 の残りから、
Amazon が要る DOM の環境の最小を Uncaught を追って決める Phase として切り出した。p065 は planned のまま
（Promise・Symbol・Map・Set・`Error.stack`・`atob` 等が残る）。

## 範囲

- **Uncaught の位置**（診断）: 実行時の Uncaught を「(at 行:桁)」付きで console に出す。ここまで実行時の誤りは場所が分からず、
  どの script のどこで止まるかを追えなかった。
- `navigator`（`Navigator`）: `userAgent`（HTTP の User-Agent と同じ）・`appVersion`・`appCodeName`・`appName`・`platform`・`product`・
  `productSub`・`vendor`・`language`・`languages`・`cookieEnabled`・`onLine`・`webdriver`・`pdfViewerEnabled`・`hardwareConcurrency`・
  `maxTouchPoints`・`doNotTrack`・`plugins`・`mimeTypes`・`javaEnabled()`・`sendBeacon()`（受け取るだけで送らない）。
- `screen`（`Screen`）: `width`・`height`・`availWidth`・`availHeight`（viewport の大きさ）・`colorDepth`・`pixelDepth`・`availLeft`・`availTop`。
- `performance`（`Performance`）: `now()`（page の時計）・`timeOrigin`・`timing`（全て window を作った時刻）・`navigation`・
  `getEntries*`（空）・`mark`・`measure`・`clear*`・`setResourceTimingBufferSize`（何も記録しない）。
- `location`（`Location`）: `href`・`origin`・`protocol`・`host`・`hostname`・`port`・`pathname`・`search`・`hash`・`toString()`（読むだけ。
  代入と `assign`・`replace`・`reload` による移動は p064）。
- `Image` 構築子と `HTMLImageElement`（`img` 要素の prototype）: `src`・`alt`・`width`・`height`（属性の反映）・`complete`（true）・
  `naturalWidth`・`naturalHeight`（0）。script が作った画像は読み込まない。
- `document`: `cookie`（読み書き。HttpOnly の cookie は見えず、置き換えられない）・`URL`・`documentURI`・`domain`・`location`・
  `referrer`（空）・`hidden`（false）・`visibilityState`（visible）・`compatMode`・`characterSet`・`charset`・`inputEncoding`（UTF-8）。
- window: `devicePixelRatio`・`outerWidth`・`outerHeight`・`screenX`・`screenY`・`screenLeft`・`screenTop`・`top`・`parent`・`frames`・
  `opener`・`length`・`name`・`closed`。

この Phase に無いもの: `querySelector`・`getBoundingClientRect` 等（p031）、let/const・arrow・template（p028）、location による移動と
History・fetch・XHR・localStorage（p064）、performance の実際の entry と timing、画像の読み込み（`new Image().src`）、document の実際の
encoding（parser が UTF-8 だけを読むので常に UTF-8）。

## 設計と実装

- Uncaught の位置: `vm_code` に位置の表（`struct vm_position`: 語の offset・行・桁、offset の昇順）を足した（`vm/bytecode.h`・
  `vm/code.c` の `vm_code_position`）。compiler は式と文を compile する間、その node の位置を現在の位置にし（`js_emit_at`）、子の
  compile の後で親の位置に戻す。`js_emit` は位置が変わった命令で表に 1 行を足す（`js/emit.c`・`compile.c`・`compile_expr.c`・
  `compile.h`）。interpreter の `interpreter_unwind` は、新しい例外（realm の `throw_value` と違う値）の位置を投げた命令から記録し、
  handler が受けたら忘れる（`vm/interpreter.c`・`vm/realm.c`・`vm/vm.h`）。`vm_throw_site`（`vm/function.c`）で埋め込み側が読み、
  `bind_report_exception` が「Uncaught …（at 行:桁）」と書く。行と桁はその関数が書かれた script の text の中の位置（script の名前は
  出さない）。`run-dom-tests.py` はこの後置を除いて Chromium の出力と比べる。`--js` の出力は変えていない。
- `bind/environment.c`（新）: `Navigator`・`Screen`・`Performance`・`Location` の interface と window ごとの object、`Image`、window の
  plain な property。変わらない値は interface の prototype の data property（instance の own property でないのは他の browser と同じ）、
  変わる値は accessor（getter の関数の data が部分を示す）。
- `bind/element.c`・`bind/node.c`: `HTMLImageElement`（`HTMLElement` の子）と、HTML の `img` 要素の wrapper の prototype。
- `bind/document.c`: 上の document の attribute。
- `bind/bind.h`: `bind_host` に `user_agent`・`location`（`BIND_LOCATION_*` の部分を書く）・`cookie_get`・`cookie_set`。
  `bind/internal.h`・`bind/window.c`: interface の登録、`bind_window` の `location`（trace する）と `time_origin`。
- `page/script.c`: host の callback。page の `base`（URL、または file の絶対 path → `file://`）を `net_url_parse` し、
  `net_url_component` で部分を書く。location の無い page は `about:blank`。
- `net/cookie.c`・`net/net.h`: `net_cookie_string`（document.cookie が読む、HttpOnly を除いた `a=b; c=d`）と
  `net_cookie_store_script`（HttpOnly の cookie を置かず、置き換えもしない）。`net_cookie_header` は共通の `cookie_pairs` を使う
  （出力は同じ）。User-Agent の定数を `NET_USER_AGENT` として `net.h` に移し、`http.c` と navigator が同じ文字列を使う。
- `libbrowser/Makefile` に `bind/environment.c`。`include/libc/browser.h`・`view/` は変えていない。

## 試験

- `plan/ws074/tests/dom/environment.html`（新）と `environment.expected`（Chromium 153 の出力、`run-dom-tests.py --reference`、他の
  expected は不変）: navigator・screen・performance・location・document の attribute・window の relatives・`new Image(8, 9)`・`img` の
  反映。file: の page で、どの browser でも同じになるものだけを出す（agent・大きさ・時刻は出さない）。`document.characterSet` は
  `<meta charset="utf-8">` の page で比べる（parser は UTF-8 だけを読む）。

## 確認（host は Debian の cc。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。zedBSD の amd64 の build と image の build: browser の warning 0。
- style-check: `bind/environment.c` と変えた file に新しい指摘 0（前からの vm.h の 3 件、bytecode.h の 1 件は残る）。
- 回帰（plain と ASan、どちらも同じ結果）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、
  host-text 20/20、host-relayout 147/147、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、
  run-dom-tests **7/7**（environment を追加）、run-js-tests 9/9、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。
- test262: 16393/47792、ES5 7732/8087（p076 と同じ。新しく落ちた試験 0。compiler の位置の表の追加による変化なし）。
- Amazon（2026-09-28 23:52 の capture。外の script は host から live に取得される）:
  - script のある top-local の Uncaught **49 → 10**: `navigator` 1・`Image` 1・`P is not defined` 37・`Cannot read properties` 3 が 0 に。
    残りは let/const 3、arrow 2、template 1、`value is not a function` 4（`getBoundingClientRect`（GWMMetrics の `f`）・
    `document.querySelector`、p031）。
  - search-local の Uncaught **44 → 15**（`P` 26 等が 0 に。残りは let/const 10、template 2、arrow 1、`Cannot read properties` 2）。
  - `live-compare.py`（script 付きの capture、1280x900、p077 の前の engine を一時の worktree で build して同じ条件で比べた）:
    search-local 画素 **72.92% → 76.04%**、ink **26.51% → 33.01%**。top-local は 64.47%・ink 57.66% で不変。
  - ASan の `--render`（top-local、search-local、top-local-noscript）: 報告 0。
- guest（QEMU、worktree の image、headless の guest）: run-dom-tests `--outputs` **7/7**、run-js-tests `--outputs` 9/9。live の
  `https://www.amazon.co.jp/` の `--run`（取得 1 回）: Uncaught は `value is not a function` 4 と arrow 1 だけで、`navigator`・`P` の
  誤りは無い（host の capture より script の誤りが少ない理由（live の page の違いか、外の script の取得の違いか）は未調査）。
- `plan/tools/boot-test.sh`（`boot-check.sh p077`）: PASS、login prompt を画面で確認
  （`/home/awe/zedBSD-rpi4/build/ws074-shots/p077-20260929-boot-login.png`）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p077-20260929-amazon-search-local.png`・`p077-20260929-amazon-top-local.png`（左が私たち、中が Chromium、右が差）。

## 未実施・制限・残り

- 実機は未実施。guest の窓（zdesktop）で live の Amazon を開く確認は未実施（headless の guest の `--run` で確かめた）。
- Uncaught の位置は行と桁だけで、関数が書かれた script の名前は出ない（関数の code は名前を持たない）。
- `navigator.sendBeacon` は送らない。performance の entry と timing は実際の値でない。`screen` の大きさは viewport。
- location による移動、`document.characterSet` の実際の encoding、画像の読み込みは残り（上の「無いもの」）。
- Amazon の次の blocker: let/const・arrow・template（p028）、`querySelector`・`getBoundingClientRect`（p031）。
