# WS074 デモの目標: amazon.co.jp（調査と到達の計画）

2026-09-28 ユーザー: デモの目標を Google から **amazon.co.jp** に変更（トップの page と検索、限定的な CSS・基本的な JS、WebGL・動画なし。
master.md の決定の行）。この文書は [ws074-p067](phase067/phase.md) の調査の結果、選んだ版、そこまでの Phase の列。Google の調査
（[google-goal.md](google-goal.md)、p059）は経緯として残す。Amazon の page・CSS・画像は tree に入れない（取得物は `build/ws074-amazon/`、
写真は `build/ws074-shots/` だけ）。

## 1. Amazon が返すもの（2026-09-28、host の curl と Chromium 153、地域は日本）

| User-Agent | トップ（`/`） | 検索（`/s?k=kei`） |
| --- | --- | --- |
| 私たちの `browser/0.1 (Kei)` | 200、約 1.0 MB（desktop の版）: script 105（inline 630 KB）、inline の `<style>` 297 KB、外の stylesheet 8 本（計 0.93 MB） | 200、約 1.4 MB: **結果 57 件が server で描かれている**（`data-component-type="s-search-result"`）、inline の style 32 KB、外の stylesheet 11 本（計 1.95 MB） |
| Chrome の UA（curl） | **202、本文 0**（`x-amzn-waf-action: challenge`: AWS WAF の JS の challenge） | 200、1.6 MB（同じ版、script と style が少し多い） |
| Chrome の UA に Kei を足したもの | 202（同じ challenge） | 200 |
| lynx | 200（私たちと同じ版） | 200 |

- 私たちの正直な UA には challenge 無しに desktop の版が返る。Chrome の UA は、JS を走らせない client には AWS WAF の challenge
  （JS で token を作る）になる。**目標は私たちの UA の版**（ユーザーは Chrome の UA でもよいと言ったが、その方が難しい）。
- **Chromium は script を全部除いた同じ HTML でも、トップも検索もほぼ同じ配置に描く**（`amazon-*-noscript-before.png` の中央）。
  配置は CSS だけで決まり、JS は付け足し（配達先の popover、価格の slider の動き、carousel の送り、候補）。Google と違い、
  結果の page に JS の関門は無い。
- 検索の form（`#nav-search-bar-form`）は普通の GET の form: `action="/s/ref=nb_sb_noss"`、`accept-charset="utf-8"`、
  `<input type=hidden name=__mk_ja_JP value=カタカナ>`、`<select name=url>`（`search-alias=aps` 等）、`<input type=text
  name=field-keywords placeholder=…>`、`<input type=submit value=検索>`。**JS なしで検索できる**。

## 2. 今の browser での表示（p058 の後の engine、host の `--render`、保存した capture）

- 崩れ: `<link rel=stylesheet>` を読まないので Amazon の CSS の 9 割（外の 1〜2 MB）が効かない。`<noscript>` の中の `<style>`
  が生の文字で出る。header の帯（`#navbar`）は色だけ、「キーボードショートカット」など隠す要素が見え、商品の画像は並ばず縦に長い。
- 画素の比較（`live-compare.py`、Chromium は私たちの UA と同じ font、1280x900、白でない画素の一致）:
  トップ 11.56%（script を除いた capture）・5.75%（script 付き）、検索 2.42%（script 除き）・2.41%（script 付き）。
- JS: 約 70 の Uncaught（`Date` が無い、RegExp の literal、AUI の `P`・`ue_csm` が未定義になる連鎖）。
- 速さ: 検索の capture（要素 約 6000）の `--render` は host で 3.7 s（外の CSS 無しで）。外の CSS 2 MB を読むと selector の照合
  （rule の索引が無い）が支配的になる見込み。

## 3. 足りない機能（CSS は検索の page の inline と外の sheet 2 MB、トップ 1.2 MB から数えた）

### 3.1 CSS

- **外の stylesheet**（`<link rel=stylesheet>`、`@import`）と、2 MB の CSS に耐える rule の索引（最右の compound の id・class・tag）。
- 値: `var()` と custom property（1600 箇所、`:root` に定義）、`calc()`（1560、`calc(.16667 * (100vw - 28px))` のような列の幅）、
  `min()`・`max()`・`clamp()`、`!important`（2115）、`@media`（306、幅の条件）、`@supports`、論理 property（`padding-inline-*`、
  `border-start-start-radius` など）。
- 表示: **flex**（494。`sg-row` は `flex-wrap: wrap` の結果の格子、`#navbar` の帯）、`inline-block`（245）、`-webkit-box`（118、
  line clamp）、`inline-flex`、`table`・`table-cell`（48）、`grid`（23、`repeat(12,1fr)`）。
- property: `border-radius`（582）、`box-shadow`（336）、`opacity`（246）、`vertical-align`（255）、`transform`（203）、`outline`、
  `text-overflow`（106）、`object-fit`（72）、`gap`（108）、`linear-gradient`（103）、`-webkit-line-clamp`、`content`（138）。
- selector: `:not()`（980）、`:root`（788）、`:hover`・`:active`・`:focus`・`:focus-visible`、`::before`・`::after`（352）、`:has()`
  （200）、`:last-child`・`:nth-child`、`:is()`、`:disabled`・`:checked`。
- font: `@font-face` の Amazon Ember（WOFF2 と WOFF。WOFF は libz-compat で読める）。日本語は fallback の font。

### 3.2 form

`<input>` の text・submit・hidden、`<select>` の最小（選ばれた option を描き、送信に入れる）、placeholder、focus と caret、文字の
入力、Enter と submit の button での送信（`accept-charset=utf-8`）。p032 の範囲に `<select>` の最小を足す。

### 3.3 DOM・JS（デモの配置には要らない。操作の付け足しに要る）

`Date`、RegExp、Promise、`localStorage`、`performance`、`MutationObserver`、`IntersectionObserver`、`XMLHttpRequest`・`fetch`、
`querySelector`・`classList`・`getBoundingClientRect`。inline の script には ES2015 の構文（arrow・let・const・template・async）も
ある（p028・p029）。

### 3.4 画像・network

商品の画像は `m.media-amazon.com` の JPEG（既存の libjpeg-compat）。sprite の PNG（`nav-sprite`）は背景の位置で出る。cookie
（`session-id` 等）は既存の HTTP の cookie で足りる。

## 4. 到達の Phase の列（順と大きさ）

各 Phase は保存した capture（`build/ws074-amazon/top.html`・`search.html` と script を除いた `*-noscript.html`）を
`plan/ws074/tests/live-compare.py` で Chromium と比べ、写真を `build/ws074-shots/` に残す。live の取得は各 Phase の最後の確認の
1〜2 回に限る。

| 順 | Phase | 内容 | 目安 |
| --- | --- | --- | --- |
| 1 | [ws074-p032](phase032/phase.md) | form: `<input>`（text・password・submit・button・reset・hidden・checkbox・radio）・`<textarea>`・`<select>` の最小の描画、focus と caret、入力と編集、Enter と submit の送信（GET、`accept-charset`・document の encoding）、`noscript` を隠す、`box-sizing` | **cleared**（2026-09-28。guest で live の Amazon の検索が通る。検索 2.42% → 39.08%、トップ 11.56% → 9.19%） |
| 2 | ws074-p068 | 外の stylesheet（`<link rel=stylesheet>` と `@import`、非同期の loader、読み終えてから再計算）、rule の索引（最右の id・class・tag） | 3 h |
| 3 | ws074-p061 | CSS の値: `var()` と custom property、`calc()`・`min()`・`max()`・`clamp()`、`@media`（幅・`prefers-*`）、`@supports`、`!important` の確認、論理 property、`-webkit-` の別名 | 3〜4 h |
| 4 | ws074-p069 | selector と pseudo-element: `:not()`・`:is()`・`:where()`・`:has()`（子孫の最小）、`:root`、`:nth-child`・`:last-child`・`*-of-type`、`:hover`・`:focus`・`:disabled`・`:checked`、`::before`・`::after`（`content` の文字列） | 3 h |
| 5 | ws074-p035（最小） | flexbox: row・column・wrap、`flex` の shorthand、grow・shrink・basis、`align-items`・`justify-content`・`gap`、`inline-flex`、`-webkit-box` の別名 | 4 h |
| 6 | ws074-p060 | `inline-block` の atomic な inline、`vertical-align` | 2 h |
| 7 | ws074-p062 | 描画: `border-radius`、`opacity`、`box-shadow`、`outline`、`linear-gradient`、`object-fit` | 3 h |
| 8 | ws074-p070 | `@font-face`（WOFF、libz-compat）と Amazon Ember | 2 h |
| 9 | ws074-p071 | 大きな page の速さ（1.4 MB の HTML と 2 MB の CSS、guest で操作できる時間まで）、測って直す | 2〜3 h |
| 10 | ws074-p037（最小） | table の auto layout の最小 | 3 h |
| 11 | ws074-p072 | grid の最小（`repeat(N,1fr)`、`grid-column`） | 3 h |
| 12 | ws074-p027・p065 の一部 | RegExp、`Date`・Promise 等（Amazon の script の Uncaught を減らし、popover 等を動かす） | 6 h〜 |

順の理由: 1 で検索できる（デモの操作）。2〜5 が見た目の大部分（外の CSS、変数と calc と @media、selector、flex）。6〜11 は細部と速さ。
JS（12）はデモの配置に要らないので最後。Google 用の p063（SVG）・p065（challenge）・p066（Google の結果）はデモの列から外す（planned のまま）。

## 5. 判定の道具

- `plan/ws074/tests/live-compare.py URL|FILE --tag TAG --shots build/ws074-shots`: 私たちの `--render` と Chromium（私たちの UA、
  同じ font）の白でない画素の一致。基準（p067）: トップ 11.56%、検索 2.42%（script を除いた capture）。
- guest: `browser` の窓で live の URL を開き、zdesktop の画面を撮る（p059 の手順）。
