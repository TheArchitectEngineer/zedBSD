<!-- awesome-plan project=zedbsd record=ws074p031 -->

# ws074-p031: Amazon の script が要る DOM の API（querySelector・getBoundingClientRect・classList・inline style ほか）

Phase ID: `ws074-p031`
Parent: [WS074](../ws.md)
Status: in-progress
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom`（main 59235c9f から）で実行、2026-09-29）
依存: p030（DOM の binding）、p077（window の環境と Uncaught の位置）
並行: WS074 の別のエージェントが p028・p078（`js/`・`vm/`）を進めた。この Phase は `bind/`・`css/`・`page/`・`view/` だけを変え、
`js/`・`vm/` は変えていない（途中で main（p078 の merge の後）を取り込んだ）。

## 範囲（2026-09-29 に絞った）

ws.md の元の p031 の行（event・innerHTML・querySelector・classList・CSSOM の inline style・getComputedStyle・geometry・変更の後の再計算）
のうち、main の指示「Amazon の script が要る DOM の API」に沿って、p077 の後に Amazon の top に残っていた Uncaught（`value is not a
function` 4 件: `document.querySelector` と GWMMetrics の `getBoundingClientRect`）と、それを直すと次に出た AUI の機能検出の
`element.style[...]`（`Cannot read properties of undefined`）、Amazon の inline script が使う回数の多いものに絞った。

1. **Selectors API**: `Document`・`Element`・`DocumentFragment` の `querySelector`・`querySelectorAll`（静的な一覧、今の
   `getElementsBy*` と同じく配列）、`Element` の `matches`・`webkitMatchesSelector`・`closest`。構文の誤りは `SyntaxError` の例外。
2. **geometry**（layout への問い合わせ）: `Element` の `getBoundingClientRect`・`getClientRects`（`DOMRect`: x・y・width・height・top・
   right・bottom・left・`toJSON`、構築子）、`clientWidth`・`clientHeight`・`clientTop`・`clientLeft`、`scrollWidth`・`scrollHeight`・
   `scrollTop`・`scrollLeft`、`HTMLElement` の `offsetWidth`・`offsetHeight`。window の `scrollX`・`scrollY`・`pageXOffset`・`pageYOffset`。
3. **`classList`**（`DOMTokenList`）と **`dataset`**（`DOMStringMap`）。
4. **inline style**（`HTMLElement.style`、`CSSStyleDeclaration`）: style 属性の読み書き、property ごとの accessor、`cssText`・`length`・
   `item`・`getPropertyValue`・`getPropertyPriority`・`setProperty`・`removeProperty`、`element.style = "..."`。

この Phase に無いもの（main に計画を依頼する）: `getComputedStyle`、`innerHTML`・`outerHTML`・`insertAdjacentHTML`、`offsetTop`・
`offsetLeft`・`offsetParent`、scroll する要素の中の位置と `scrollTo`・`scrollIntoView`、`classList[0]` の添字と iterator（engine に
exotic object と Symbol の iterator が無い）、dataset の新しい名前の代入の反映、inline style の shorthand と longhand の対応と値の
正規化、event の残り（元の行の event の dispatch と入力は p030・p056 で済んでいる）。

## 設計と実装

- **CSS の公開の入口**（`css/`）:
  - `css_query_parse`・`css_query_destroy`（`parser.c`）: selector の一覧を規則の prelude と同じ parser で自分の arena に読む。名前は
    atom（heap と同じだけ生きる）なので trace は要らない。構文の誤りは EINVAL。知らない pseudo-class は今の parser と同じく何にも
    合わない（例外にしない）。
  - `css_engine_query_begin`・`css_engine_query_matches`（`cascade.c`）: cascade の `cascade_selector_matches` をそのまま使う。
    query の engine は styling より長く生きるので、query ごとに class 属性の分割の cache を忘れる（query の class 名は parse の時に
    初めて atom になり、前の分割はそれを持たないため）。
  - `css_declaration_valid`（`parser.c`）・`css_property_name`（`values.c`）: inline style が「engine が読む値」だけを残すための判定と、
    accessor を作る property の名前の一覧。
- **bind**:
  - `bind/query.c`（新）: Selectors API、`DOMTokenList`（class 属性の語を集合として読み、変えて書き戻す。空の語は `SyntaxError`、
    空白を含む語は `InvalidCharacterError`。属性の無い要素に何も残らない `remove` は属性を作らない）、`DOMStringMap`（その時の
    `data-*` 属性ごとの accessor。名前は camel case）。`mixin.c` の class の分割を `bind_split_classes` として共有した。
  - `bind/geometry.c`（新）: `DOMRect`（cell に x・y・width・height）と geometry の getter。host が node の box（border box の union、
    最初の box が block か、その border）を CSS px で答え、document の scroll を引いて viewport の座標にする。root 要素の client の
    大きさは viewport、scroll の大きさは document と viewport の大きい方、scrollTop は document の scroll。他の要素の scroll の大きさは
    client の大きさ（layout が overflow の大きさを持たない）、scrollTop・scrollLeft は 0（setter は受けるだけ）。整数の measure は
    四捨五入。
  - `bind/style.c`（新）: `CSSStyleDeclaration`。style 属性を宣言（名前・値・!important）に切り、変更は `name: value;` を空白で
    つないで書き戻す（Chromium の直列化と同じ形）。値は `css_declaration_valid` が真のものだけ残す（`width = "abc"` は無視、custom
    property は残る）。prototype に engine が知る property ごとの accessor（CSS の名前・camel case、`-webkit-` には `webkit…`・`Webkit…`）。
    知らない property（`transition` など）は undefined（Chromium は ""）。
  - `bind.h`: `bind_host` に `selector_engine`・`node_box`・`document_size`・`scroll`、`struct bind_box`。`internal.h` に interface
    （`DOMTokenList`・`DOMStringMap`・`DOMRect`・`CSSStyleDeclaration`）と関数。`element.c`・`document.c`・`text.c`・`window.c` の表に
    member を足した。
- **page**（`page/geometry.c`（新）と `page.c`・`page.h`・`script.c`）: host の callback。geometry を問われた時に document が変わって
  いれば、view がくれた font と大きさでその場で layout する（`page_layout`。Chromium の強制 layout と同じ）。font の無い page
  （view がまだ font を渡していない、または font が開けない）は layout できず、box は無い（0）。selector の照合用の style engine は
  page ごとに一つ（最初に要る時に作る）。`page_set_fonts`・`page_set_scroll` と page の `viewport_width`・`viewport_height`。
- **view**（`view/view.c`）: page を作る時に font の path を渡し、scroll を clamp するたびに page に scroll を教える。
- `libbrowser/Makefile` に `bind/query.c`・`bind/geometry.c`・`bind/style.c`・`page/geometry.c`。`include/libc/browser.h` は不変。

## 試験

- `plan/ws074/tests/dom/` に 4 つ（新）と Chromium 153 の expected（`run-dom-tests.py --reference`、他の expected は不変）:
  `query.html`（15 行）、`geometry.html`（14 行: margin・border・padding・小数の大きさ・flex・absolute・display:none・box-sizing、DOM
  を変えた後の再 layout、DOMRect の構築子と toJSON、scroll）、`classlist.html`（13 行）、`style.html`（10 行）。viewport の大きさと
  font に依る値は出さない。
- `run-dom-tests.py`: host に system の font（`/usr/share/fonts/keiland*.ttf`）が無いので、`build/ws035-fonts/Inter.ttf` があれば
  `--font=` で渡す（geometry の試験は layout が要る）。guest は image の font を使う。

## 確認（host は Debian の cc と Chromium 153。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: 新しい 4 つの file は 0、変えた file は前と同じ数（css/cascade.c の 1 は
  前から）。
- 回帰（plain と ASan、同じ結果。main（p078）を取り込んだ後）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、
  host-position 19/19、host-text 20/20、host-relayout 161/161（test page と Amazon の 2 つ）、host-base 2038・heap 31・interp 45・
  number 71・object 98（全て 0 failed）、run-dom-tests **12/12**（4 つを追加）、run-js-tests 10/10、run-loader-tests 11/11、
  run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8。ASan の `--render`（top-local・search-local・top-local-noscript）に報告 0。
  test262 は `js/`・`vm/` を変えていないので流していない。
- Amazon（2026-09-28 23:52 の capture、外の script は live に取得、`--run` に font を渡す）:
  - p031 だけ（main の p078 の前）: top-local の Uncaught **10 → 6**（`value is not a function` 4 が 0。途中で出た AUI の
    `style[...]` の `Cannot read properties` も inline style で 0）。残りは let/const・arrow・template（p078）だけ。search-local 15 → 15
    （変わらず。let/const の後の連鎖）。
  - main の p078 を取り込んだ後: top-local の Uncaught 3（`TextEncoder is not defined` 1、destructuring・default・rest 2）、
    search-local 9（destructuring 等 4、optional chaining 1、`TextEncoder` 1、`sessionStorage` が無い `Cannot read properties`（EWC の
    cache）1、`(at 1:1)` の `Cannot read properties` 2（let/const の script の失敗の後の連鎖と見られ、場所は未特定））。
  - `live-compare.py`（script 付き、1280x900）: p031 の前後で top-local 画素 64.47%・ink 57.66%、search-local 76.04%・ink 33.01% と
    不変（p031 の前の engine を `git archive 59235c9f` から build して同じ条件で比べた）。p078 の後も同じ値。top-local-noscript 79.80%。
  - `--run` の時間（host）: top-local 0.6 s → 1.0 s（getBoundingClientRect が layout を強いる）、main の取り込み後 1.4 s、search-local 1.8 s。

## 写真

- host: `build/ws074-shots/p031-20260929-amazon-top-local-side.png`・`…-search-local-side.png`・`…-top-local-noscript-side.png`
  （worktree の build。左が私たち、中が Chromium、右が差）。

## Amazon の次の blocker（main への報告）

- `TextEncoder`（Encoding API。top・search の AUI の script）: この Phase の範囲外（DOM でない）。新しい Phase が要る。
- `sessionStorage`・`localStorage`・`Storage`（p064）。
- destructuring・default・rest、optional chaining（`js/` の側）。

## Resume point

guest の確認（`plan/ws074/tests/build-browser-image.sh build/amd64` の image、`browser-guest.sh plain` で run-dom-tests `--outputs`）と
boot test。
