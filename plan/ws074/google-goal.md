# WS074 デモの目標: Google の検索（調査と到達の計画）

**2026-09-28: デモの目標は amazon.co.jp に変わった（[amazon-goal.md](amazon-goal.md)）。この文書は経緯として残す。** 比較の道具は `tests/live-compare.py` に改名した。

2026-09-28 ユーザー:「Googleの検索トップページと検索が、レイアウトを崩さずに表示できたら、ゴールにしましょう！限定的なCSSと、基本的なJS、WebGLなし、ビデオなしです。」
この文書は [ws074-p059](phase059/phase.md) の調査の結果、選んだ目標の形、そこまでの Phase の列。Google の page の内容と画像は
tree に入れない（取得した HTML・screenshot は `build/ws074-google/`・`build/ws074-shots/` にだけ置く）。

## 1. Google が返すもの（2026-09-28 の観測、host の curl と Chromium 153、地域は日本）

### 1.1 トップの page（`https://www.google.com/`）

User-Agent で 3 通りに分かれる（`Accept-Encoding: identity`、cookie なしの 1 回目）。

| User-Agent | 返る page |
| --- | --- |
| 私たちの `browser/0.1 (Kei)`、curl、Dillo、NetSurf | **基本の HTML の版**（約 85 KB、`gbv=1`）: `<center>` と `<table>` の配置、`<form action="/search">` に `<input name=q size=57>` と 2 つの `<input type=submit>`（`btnG`「Google 検索」・`btnI`）と hidden、`<img>` の logo（PNG、272x92）、上の帯（`#gb`、flex と角丸の「ログイン」、inline SVG の apps の icon）、16 KB の `<style>`、10 個の inline の script と ES5 の bundle 2 つ（`og.qtm…es5.O` 271 KB と `xjs.hp…es5.O` 370 KB、script 要素を足して読む） |
| Chrome・iPhone の Safari | 現代の版（200〜230 KB、`<textarea>` の検索の欄、AI モードの button、角丸の枠、重い CSS と JS） |
| lynx・w3m・links・elinks、IE6/8、Firefox 3 | 「ブラウザを更新してください」の小さな page（2 KB） |
| Nokia の feature phone・Opera Mini | 13 KB の mobile の版（検索は 403） |

HTTP の `Content-Type` は `ISO-8859-1`（本文は ASCII と `&#NNNNN;` の文字参照）、`Set-Cookie` は `AEC`・`NID`・`__Secure-STRP`。
`<link rel=stylesheet>`（`/xjs/_/ss/…m=sb_he,d`）は私たちの UA には空の 200 が返る。

### 1.2 検索の結果（`/search?q=kei`、home の form からの送信も同じ）

**基本の HTML の結果はもう無い**（2025 年の Google の変更）。

- text browser と古い browser: 上の「ブラウザを更新してください」。feature phone: 403。
- 私たちの UA を含む他の全て（Chrome の UA も）: **SearchGuard の challenge の page**（約 90 KB）。5 つの script: 難読化した JS の VM
  （`knitsail`、63 KB、`(0,eval)` で自分の本体を組み立てる、trustedTypes の確認）と、その結果の token を `SG_SS` の cookie に置いて
  同じ URL を開き直す script（27 KB。`Promise`、`performance.timing`、`navigator.sendBeacon`、`window.onerror`、`document.cookie`）。
  `<noscript>` は `/httpservice/retry/enablejs` への meta refresh。2 秒後に「アクセスできない場合はここをクリック」の div を出す。
  `gbv=1`・`udm=14`・`/m`・`/xhtml`・`tbm=`・`google.co.jp` も同じ challenge。
- challenge の script が使う API（静的な数え上げ）: `Error`（`stack`）、`Symbol`、`Promise`、`Map`、`RegExp`（literal を含む）、`Date`、
  `performance`、`screen`、`navigator`、`sessionStorage`、`CustomEvent`・`dispatchEvent`・`addEventListener`、`document.cookie`、
  `location`、`setTimeout`、`atob`・`btoa`、`encodeURIComponent`・`decodeURIComponent`・`escape`、`JSON`、`Image`。
  VM の bytecode（`p`、base64）が実行時に何を調べるか（canvas・WebGL・font の指紋など）は静的には分からない。
- **Google の bot の判定**: この host からは、headless と headful（Xvfb、GPU は SwiftShader）の Chromium がどちらも、home で文字を打って
  Enter を押す人と同じ手順でも `/sorry/index`（reCAPTCHA「I'm not a robot」）に送られた（IPv4・IPv6 とも、3 回）。同じ時刻に
  `news.google.com` は curl にも 429 を返した（後で 302 に戻った）。この network の IP の評価が下がっている（この調査の自動の
  request と他の自動の作業）か、Xvfb の Chromium の指紋が判定されたか、区別できていない。**結果の page の Chromium の参照の描画は
  この host では取れていない。**

### 1.3 今の browser での表示（p058 の後の main、host の `--render` と guest の窓）

- home: logo の画像、上の帯の文字、footer の文字は出る。崩れ: (1) `<input>` が描かれない（検索の欄も button も無い）、(2) `<table>` が
  block として積まれ、検索の欄の行が全幅の 3 段になる、(3) `display:inline-block`（footer の link、`.ds`）が全幅の block になり縦に並ぶ、
  (4) 上の帯の flex が block（Gmail と画像が縦、「ログイン」が四角で下へずれる）、(5) apps の SVG の icon が無い、(6) JS が
  「regular expressions は未対応」の SyntaxError で 5 本止まり、言語の行（「Google 検索は次の言語でも…」）と候補の欄が出ない。
  画素の比較（Chromium・私たちの UA・同じ font、1280x900）: 白でない画素の一致 20.1%（logo だけが合う）。
  写真: `build/ws074-shots/p059-20260928-survey-home-before.png`（左が私たち、中が Chromium、右が違う画素）、guest の窓
  `p059-20260928-guest-google-home.png`（host の headless と同じ崩れ）。
- 検索: challenge の script は RegExp の literal で SyntaxError、`navigator` が無く ReferenceError で止まる。さらに `<noscript>` の中身が
  生の文字として表示される（script が有効なときの `noscript { display: none }` が UA の stylesheet に無い）。
  写真: `p059-20260928-survey-search-ours.png`。
- JS の engine（`--js` での確認）: ES5 の構文、getter、`eval`・間接の eval・`Function`、JSON、try/catch、label は動く。無いもの:
  RegExp（literal と構築子）、`Date`、`Promise`、`Symbol`、`Map`・`Set`・`WeakMap`、typed array と `ArrayBuffer`、`encodeURIComponent`、
  `Reflect`、ES2015 の構文（let・const・arrow・class・template・destructuring・spread・for-of・generator・async）。Google は私たちの
  UA に **ES5 の bundle**（`.es5.O`）を返すので、構文は ES5 で足りる。組み込みと RegExp が足りない。

## 2. 足りない機能（崩れと操作を妨げるもの）

### 2.1 CSS の property と selector（home の inline の style の 16 KB と style 属性から数えた。結果の page は未取得）

- 表示の型: `inline-block` の atomic な inline（縮めて行に置く）、`table`・`table-row`・`table-cell`（HTML の `width`・`align`・`valign`・
  `nowrap`・`cellpadding`・`cellspacing`）、`flex`（`align-items`・`justify-content`・`flex` の shorthand・`-webkit-box` の別名）。
- 値: `var()` と custom property（67 箇所。多くは直前に fallback の宣言があるので、無視しても色は合う）、`calc()`、`!important`、
  `-webkit-` の別名（`box-sizing`・`flex`・`box-shadow`・`transform`）。
- property: `border-radius`（12）、`box-sizing`（9）、`opacity`（14）、`vertical-align`（11）、`box-shadow`（7）、`outline`（8）、
  `text-overflow`（3）、`transform`（2、scale）、`letter-spacing`、`text-transform`、`fill: currentColor`（SVG）、`content`（`::before`）。
  無視してよい: `cursor`、`user-select`、`animation`・`@keyframes`、`text-rendering`、font smoothing。
- selector: `:not()`（6）、`:hover`・`:active`・`:focus`・`:visited`、`::before`・`::after`、`@media`（`max-width`、`min-resolution`、
  `forced-colors`、`prefers-color-scheme`。今は @media の中を全部読み飛ばす）。
- UA の stylesheet: script が有効なときの `noscript { display: none }`。

### 2.2 DOM と JS の API

- engine: RegExp（ES5 の構文と flag `g i m`、`String.prototype.replace`・`match`・`split`・`search`）、`Date`、`Promise` と microtask、
  `Symbol`（Closure の ES5 の polyfill が native を探す）、`Map`・`Set`・`WeakMap`、`Uint8Array` などの typed array、`encodeURIComponent`
  の類、`Error.prototype.stack`、`atob`・`btoa`。
- DOM（home の ES5 bundle が使う）: `querySelector`、`classList`、`dataset`、`getBoundingClientRect`、`getComputedStyle`、`innerHTML`、
  `insertBefore`、`matchMedia`、`requestAnimationFrame`、`MutationObserver`（空でよい）、`CustomEvent`、`performance`、`navigator`、
  `screen`、`sessionStorage`・`localStorage`、`XMLHttpRequest`（候補の欄、`/complete/search`）。
- challenge: 上の一覧と `document.cookie` の書き込み、`location.replace`・`reload`、`navigator.sendBeacon`、`trustedTypes` が無いときの経路。

### 2.3 form

`<input>` の text・submit・hidden（と button・checkbox の描画）、focus と caret、文字の入力・削除・左右、Enter と submit の button での
送信（GET の `application/x-www-form-urlencoded`、page の文字 encoding で符号化し、表せない文字は `&#NNNNN;` にする: home は
`ie=ISO-8859-1`）、`<input>` の `value`・`size`・`maxlength`、`autocomplete=off`。

### 2.4 cookie と redirect

HTTP の cookie の保存と送信は p016 にある。足りないのは JS からの `document.cookie` の書き込み（`SG_SS`）と、JS の `location.replace` に
よる navigation、`<meta http-equiv=refresh>`（noscript の中でだけ使われるので、script が有効なら不要）。

### 2.5 font と画像

- font: Google の CSS は `arial,sans-serif`・`Roboto`・`Google Sans` を指す。今は Inter に落ちる（Chromium も同じ fontconfig で比べる）。
  足りないものは無い。
- 画像: logo の PNG と `nav_logo229.png` の sprite（`background-position`）は既存の機能で出る。**inline の SVG**（apps の icon、結果の
  page の icon 類）は未対応。結果の page の thumbnail は `data:` の JPEG か WebP の可能性がある（WebP は未対応、取得してから確かめる）。

## 3. 目標にする形（選択）

- **home は、私たちの正直な UA（`browser/0.1 (Kei)`）に Google が返す基本の HTML の版を目標にする。** UA を Chrome に偽ると現代の版
  （230 KB、textarea と重い CSS・JS）が返り、易しくならない。基本の版は Chromium が同じ UA で描く絵（`p059-…-home-chromium-keiua.png`）
  を参照にし、「崩れない」の判定はこの絵との比較（白でない画素の一致と、要素の箱の位置）で行う。
- **結果の page は、Google が私たちの UA に返す、challenge を通った後の page を目標にする。** 基本の HTML の結果は存在しないので、
  challenge の JS を engine で走らせることが前提になる。
- **危険（人の判断が要る）**: challenge を正しく走らせても、`SG_SS` を受け取った Google が bot と判定すれば `/sorry`（reCAPTCHA）に送る。
  この host の Chromium も今は送られている。判定は Google の側で、私たちの実装だけでは保証できない。デモの前に、判定されていない
  network から試す必要がある。代わりの案（ユーザーの決定待ち）: (a) 実の結果の page を目標のまま進め、`/sorry` のときはデモで
  そう説明する、(b) 加えて、利用者の desktop の Chrome で保存した結果の page（`build/` にだけ置く、tree に入れない）を
  `file:` で開き、配置の忠実さの確認とデモの予備にする。既定は (a) と (b) の両方を準備する。

## 4. 到達の Phase の列（順と大きさ）

大きさは 1 回の作業の時間の目安。各 Phase は Chromium の同じ page との比較の写真を `build/ws074-shots/` に残す。

| 順 | Phase | 内容 | 目安 | 目に見える効果 |
| --- | --- | --- | --- | --- |
| 1 | [ws074-p032](phase032/phase.md)（2026-09-28 に範囲を form に絞った。fetch・XHR・Location・History・localStorage は p064 へ） | form の部品（`<input>` の text・submit・hidden・button・checkbox の描画、`<button>`、`<textarea>` の最小）、focus と caret、文字の入力と編集、Enter と submit の button による送信（GET と POST の urlencoded、page の encoding）、`noscript` を隠す | 3〜4 h | 検索の欄と button が出て、打って検索できる |
| 2 | ws074-p060 | `inline-block` を atomic な inline に（shrink-to-fit、baseline、`vertical-align` の top・middle・bottom・baseline）、`<center>` の block の中央寄せ | 2 h | footer の link が横に並ぶ、`.ds` の枠 |
| 3 | ws074-p037（最小の版を先に） | table の auto layout の最小（列の幅、`width` の % と px、`cellpadding`・`cellspacing`、`align`・`valign`・`nowrap`）。border-collapse は後 | 3 h | 検索の欄の行が Chromium の配置になる |
| 4 | ws074-p027 | RegExp の engine（backtracking、ES5 の構文と `g i m`）と String の regex の method | 4 h | home の script が SyntaxError で止まらない |
| 5 | ws074-p065 | challenge と Google の ES5 bundle の環境: `Date`、`Promise`（p029 の一部）、`Symbol`・`Map`・`Set`・`WeakMap` の最小、typed array の最小、`encodeURIComponent` の類、`Error.stack`、`atob`・`btoa`、`navigator`・`screen`・`performance`・`sessionStorage`・`CustomEvent`、`document.cookie` の書き込み、`location.replace`。保存した challenge の page を host で走らせ、`SG_SS` を置いて開き直すまで | 4〜6 h | **関門**: 実の結果の page が取れるか（§3 の危険） |
| 6 | ws074-p066 | 結果の page: 保存（`build/` だけ）、Chromium との比較、足りない CSS と DOM の洗い出しと直し。大きさは page を取ってから決める（ここで分ける） | 取得後に決める | 結果の一覧が崩れずに出る、link を開ける |
| 7 | ws074-p035（最小の版） | flexbox（row・column、`align-items`、`justify-content`、`flex` の shorthand、`gap`、`-webkit-box` の別名） | 3 h | 上の帯、結果の page の多くの部品 |
| 8 | ws074-p061 | CSS の値と selector（p008・p009 から Google が使う部分）: `@media`（幅、`prefers-color-scheme: light`、`forced-colors: none`）、`var()`、`calc()`、`!important`、`:not()`・`:hover`・`:active`・`:focus`・`:visited`、`::before`・`::after` の `content`、`box-sizing`、`-webkit-` の別名、`text-overflow`、`letter-spacing`、`text-transform` | 3 h | 細部の一致 |
| 9 | ws074-p062 | 描画（p038 から）: `border-radius`（背景と border、CPU と GPU）、`opacity`、`box-shadow`（blur の近似）、`outline` | 2〜3 h | 「ログイン」の pill、結果の page の角丸 |
| 10 | ws074-p063 | inline の SVG の最小（`svg`・`path`・`circle`・`rect`、`viewBox`、`fill`・`currentColor`、塗りだけ） | 3 h | apps の icon、結果の page の icon |
| 11 | ws074-p031 | DOM の API（Google の ES5 bundle が使う分: querySelector、classList、dataset、geometry、getComputedStyle、innerHTML、matchMedia、rAF、MutationObserver の空の実装） | 3 h | 言語の行など script が足す部品 |
| 12 | ws074-p064（p032 から分けた） | fetch・XHR（same-origin・CORS）、Location・History、localStorage | 3 h | 検索の候補の欄 |

順の理由: 1〜3 で home が Chromium と同じ配置になり、打って検索できる（デモの前半）。4〜5 は結果の page に届くかを決める最大の危険なので、
CSS の細部より先に確かめる。6 以降は結果の page の実物を見てから大きさと順を直す。7〜12 は home と結果の page の両方に効く。

## 5. 判定の道具

- home: `build/ws074-google/` の script（p059 の phase.md の手順）で、live の page を私たちの `--render` と Chromium（私たちの UA、
  同じ font の fontconfig）で描き、白でない画素の一致を出す。p059 の時点 20.1%。
- 結果の page: 取得できた時点で同じ比較を足す。
- guest: `browser` の窓で live の URL を開き、zdesktop の画面を撮る（p059 の手順）。
