<!-- awesome-plan project=zedbsd record=ws074p030 -->

# ws074-p030: JS の接続: page の script、DOM の binding、console、timer、event loop

Phase ID: `ws074-p030`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 依存の縮小（2026-09-28、main の指示で記録）

表の依存は p014・p029。p029（generator、Promise と microtask、async・await、Proxy・Reflect、TypedArray、Date、BigInt）は未 clear。
正常系のワンパス（`<script>` が DOM を変え、event と timer が動く）に要るのは **microtask の queue だけ**で、それは VM の最小の job の
queue（`vm_enqueue_job`・`vm_run_jobs`、`queueMicrotask`）としてこの Phase で作った。Promise は p029 がこの queue の上に作る。
よって p030 の依存は **p014・p046（組み込みの Array・String・JSON）** に縮めた。

WPT の testharness の runner はこの Phase から外し、新しい **ws074-p047** にした（依存 p028・p029・p030）。理由: WPT の
`resources/testharness.js`（固定の SHA `2d66b9b7`）は arrow 関数（20 箇所）、let・const（32 行以上）、class を使い、Promise も要る。
今の compiler は ES5 の核だけ（p028 の前）なので testharness.js が compile できない。M2 の「WPT dom/nodes ≥ 40%」は p047 で測る。

## 範囲（正常系のワンパス）

- VM: realm の microtask の queue（`struct vm_job`、checkpoint）、embedder の data（`realm->host`）、platform object の kind
  （`VM_KIND_PLATFORM`: `internal` が DOM の node か event の cell）。
- DOM: node が script の object（wrapper）と listener の cell を持つ（GC は node から辿る）。document の世代（木・text・属性の変更で
  増える）で style と layout の古さを知る。属性の設定・削除、text の置き換え。
- HTML の parser: script 要素の end tag で page の hook を呼ぶ（parser-blocking の script として、parser が待つ間に走る）。page の
  parse は scripting を有効にする（`<noscript>` の扱いが変わる）。
- `bind/`（新）: interface の表（名前、親、attribute の getter・setter、operation、定数）から interface object と prototype を作る。
  EventTarget、Node、CharacterData・Text・Comment、DocumentType、DocumentFragment、Element・HTMLElement、Document、Window、
  Event・UIEvent・MouseEvent・CustomEvent。ParentNode・ChildNode・NonDocumentTypeChildNode の mixin、getElementsByTagName・
  ClassName、getElementById、createElement 等、textContent、cloneNode、title、readyState。console（log・info・warn・error・debug）、
  setTimeout・setInterval・clear*、requestAnimationFrame（16 ms の timer）、queueMicrotask、innerWidth・innerHeight。
- event: 経路（target → 祖先 → document → window、load は window へ行かない）、capture・target・bubble、stopPropagation・
  stopImmediatePropagation、once、handleEvent、preventDefault、dispatchEvent、`new EventTarget()`。event handler（onclick 等 34 種の
  IDL attribute、`onclick="..."` の content attribute、body の window の handler）は listener の列の中の自分の位置で走る。
- page（`page/script.c`、新）: realm と window を page ごとに作る。inline と `src`（page の file からの相対、`file:` だけ）の script、
  type の判定（JavaScript の MIME だけ）、DOMContentLoaded と load（target は document）、timer を page の時計で走らせる、click の
  hit test（`layout_hit_node`: text、無ければ最も深い block）と MouseEvent の click。変更の後の style の集め直しと layout。
- headless: `--run PAGE`（script と timer を走らせ console を標準出力へ）。他の headless の mode も script と timer（仮想の時計で
  5000 ms まで）を走らせてから dump・描画する（console は標準エラーへ）。
- 窓: page の timer を実時間で（compositor の待ちの timeout に次の timer を入れる）、script の変更の後に layout と描画をやり直し題名を
  更新、click は先に page の click event へ（cancel されなければ link を開く）、console は `ZBROWSER CONSOLE level=N` の行。

## 受け入れ

1. amd64 の build（warning 0）、新しい file の style-check 0。
2. 自前の DOM・page の試験（`plan/ws074/tests/dom/*.html`）の console が Chromium 153 の console と同じ（host plain・ASan、guest）。
3. 前の試験が下がらない（test262、JS の試験、golden、html5lib）。
4. guest（Venus、`--glass`）: page の script が DOM を作り、interval が時計を進め、click が listener に届いて表示が変わる。画面を撮る。
5. boot test。

## 結果（2026-09-28）

cleared。

- 実装: `bind/`（window.c・node.c・mixin.c・element.c・text.c・document.c・event.c・handler.c・timer.c・bind.h・internal.h、新）、
  `page/script.c`（新）、`vm/realm.c`・`vm/vm.h`（job の queue、host、platform kind）、`dom/node.c`・`dom/dom.h`（wrapper・listeners・
  世代・`created`・属性と text の変更）、`html/`（script の hook）、`layout/hit.c`（`layout_hit_node`）、`page/page.c`（scripting の
  parse、load の event、style の集め直し）、`main.c`（`--run`、settle）、`shell/shell.c`（timer、再 layout、click、CONSOLE）、
  `js/js.h`（組み込みを作る helper を公開の header へ）。
- 試験:
  - `plan/ws074/tests/run-dom-tests.py`（新）と `dom/` の 5 page（tree・elements・events・loop・gc、計 約 130 行の console）:
    Chromium 153（`--virtual-time-budget=5000` の console）と全部同じ。host plain・ASan（UBSan 込み）で 5/5、guest で 5/5。
  - JS の試験 7/7、golden 16/16（`pages/script.html` の 4 つを追加）、host-base 2038、html5lib tokenizer 7032/7032・tree 1648/1753
    （変わらず）、test262 14255/47792・ES5 6786/8087（p046 と同じ）。
  - guest（Venus、`plan/ws074/tests/browser-p030.sh`）: `pages/script.html` の script が行を書き換え list を作り、load の listener が
    題名を「zdesktop-browser: scripts ran」に、interval が「Ticks: N」を 1 秒ごとに進め（10 frame）、「Click me」の click で
    listener が走り（CONSOLE clicked 1・2）、箱が緑になり list が伸びる。zdesktop と browser の log に ERROR 無し。
    画面: `/home/awe/zedBSD-rpi4/build/ws074-shots/p030-20260928-{start,ticks,clicked,clicked-twice}.png`。
  - 回帰: `browser-p045.sh` status 0、`browser-p014.sh` status 0（p045 で titlebar の中央が場所の control になったため、drag の
    開始点を題名の上に直した）。
- style-check: 新しい file（`bind/*`、`page/script.c`）0。変更した file に新しい違反なし（`vm/vm.h`・`js/js.h` の既存の 4 件は元から）。
- boot test: PASS（D4 の切り替えの後の image、`/home/awe/zedBSD-rpi4/build/ws074-shots/p030-20260928-boot-login.png`）。
- 実機: 未実施。
- commit: `93234a56`・`629b719c`（code と DOM の試験）、`222ff6ba`（窓の試験・page・golden）、この記録の commit。

## 同じ流れで行った D4 の切り替え（2026-09-28 ユーザー「文字の表は、生成した表をコミットしていいてす。」）

- 生成した表を commit した: `html/entities-table.c`（WHATWG の entities.json）、`base/unicode-case-table.c`（UCD 16.0.0）。各 file の
  先頭に出典・ライセンス・再生成の方法。`tools/regenerate.sh`（新）が一覧を `build/zdesktop-browser-lists/` に固定の SHA-256 で取得して
  生成器を走らせる。package の Makefile から取得と生成の規則を外した（base の build は network に依存しない）。
- notice: `userland/base/licenses/zdesktop-browser/`（`WHATWG-entities`: CC BY 4.0 と、source に取り込んだ部分の BSD 3-Clause、
  `Unicode-License-V3`: 全文）。program と一緒に `/usr/share/licenses/zdesktop-browser/` へ入る。
- `plan/ws074/tests/fetch-distfiles.sh` は不要になり削除、`host-build.sh`・`guest-build.sh` は commit した表を使う。
- commit: `aa48a9ac`。

## 後回し（follow-up）

- WebIDL の生成器（design.md §12.4）: 今は interface の表を手で書いた（生成器が出す形と同じ表）。interface が増える p031・p032 で
  生成器にするか、表のままにするかを決める。
- live な NodeList・HTMLCollection（今は呼ぶたびの Array）、DOMException（今は `Error` で message の先頭に名前）、
  `Symbol.toStringTag`（Symbol は p028）、HTMLDivElement 等の個別の interface、名前付きの property（window の id）。
- `document.write`、動的に挿入した script の実行、async・defer、module、`nomodule`、http の script（p016 の後）、script の
  文字 encoding（今は UTF-8）。
- window.onerror と error event、unhandled rejection（Promise は p029）、`console` の object の書式（今は ToString）。
- mousedown・mouseup・mousemove・key の event、focus、`click()` の後の既定の動作（link）、event の `composedPath`、passive。
- requestAnimationFrame を描画の機会に合わせる（今は 16 ms の timer）。timer の入れ子の 4 ms の clamp。
- 文書の読み込み中に parser が timer を走らせる（Chromium は script の後に parser を譲ることがある。今は parse が終わるまで timer は
  走らない）。
