<!-- awesome-plan project=zedbsd record=ws074p031 -->

# ws074-p031: Amazon の script が要る DOM の API（querySelector・getBoundingClientRect ほか）

Phase ID: `ws074-p031`
Parent: [WS074](../ws.md)
Status: in-progress
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom`（main 59235c9f から）で実行、2026-09-29）
依存: p030（DOM の binding）、p077（window の環境と Uncaught の位置）
並行: WS074 の別のエージェントが p028（`js/`・`vm/` の compiler と interpreter）を進めている。この Phase は `bind/`・`css/`・`page/`・
`view/`・`layout/` の問い合わせだけを変え、`js/`・`vm/` は変えない。

## 範囲（2026-09-29 に絞った）

ws.md の元の p031 の行（event・innerHTML・querySelector・classList・CSSOM の inline style・getComputedStyle・geometry・変更の後の再計算）
のうち、main の指示「Amazon の script が要る DOM の API」に沿って、p077 の後に Amazon の top に残る Uncaught（`value is not a function`
4 件: `document.querySelector` と GWMMetrics の `getBoundingClientRect`）と、Amazon の inline script が使う回数の多いものに絞る。

1. **Selectors API**: `Document`・`Element`・`DocumentFragment` の `querySelector`・`querySelectorAll`（静的な一覧、今の
   `getElementsBy*` と同じく配列）、`Element` の `matches`・`webkitMatchesSelector`・`closest`。構文の誤りは `SyntaxError` の DOMException。
   照合は CSS の cascade の selector の照合をそのまま使う（`css/` に公開の入口を足す）。
2. **geometry**（layout への問い合わせ）: `Element` の `getBoundingClientRect`・`getClientRects`（`DOMRect` の x・y・width・height・top・
   right・bottom・left・`toJSON`）、`clientWidth`・`clientHeight`・`clientTop`・`clientLeft`、`scrollWidth`・`scrollHeight`・`scrollTop`・
   `scrollLeft`、`HTMLElement` の `offsetWidth`・`offsetHeight`。window の `scrollX`・`scrollY`・`pageXOffset`・`pageYOffset`。
   問われた時に layout が古ければ、page はその場で（view の大きさと font で）layout し直す（Chromium の強制 layout と同じ）。
3. `classList`（`DOMTokenList`: `length`・`value`・`item`・`contains`・`add`・`remove`・`toggle`・`replace`・`toString`）と `dataset`
   （その時の `data-*` 属性の accessor を持つ object。無い名前への代入は属性にならない: engine に exotic object が無いため）。

この Phase に無いもの（main に計画を依頼する）: `style`（CSSOM の inline の CSSStyleDeclaration）・`getComputedStyle`・`innerHTML`・
`outerHTML`・`insertAdjacentHTML`、`offsetTop`・`offsetLeft`・`offsetParent`、scroll する要素の中の位置と `scrollTo`、`classList[0]` の
添字と iterator（exotic object と Symbol が要る）、event の残り（p031 の元の行の event の dispatch と入力は p030・p056 で済んでいる）。

## Resume point

実装（`css/`・`bind/`・`page/`・`view/`）と試験（`plan/ws074/tests/dom/` に Chromium の expected）。
