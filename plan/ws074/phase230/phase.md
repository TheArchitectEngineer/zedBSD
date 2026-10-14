<!-- awesome-plan project=zedbsd record=ws074-p230 -->

# ws074-p230: yahoo.co.jp の top（gzip の script、SVG の画像、transform、dynamic link ほか）

B1 (2026-10-14)。ユーザーの指示（原文「ブラウザでyahoo.co.jpも表示できるようにしてください。」、続けて「Yahooは未対応のCSSがあれば追加して修正してください。webpはaaceptしないようにしてると思いますが、もししてなければPNG,JPEGのみacceptにします。それでもYahooが送ってくるなら、今回はとりあえずはYahooはunclearにします。」）。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。google.com の続き（[p231](../phase231/phase.md)）。

## 取得物（再現できる形）

- [yahoo-co-jp-20261014.html](evidence/yahoo-co-jp-20261014.html)（この browser の UA、`Accept-Language: ja`、209,067 byte）。JS: `bundle_20261005-040738.js`・`yads-async.js`・`managed-ual.min.js`・`tmual-4.2.0.min.js`・`ual-2.10.2.min.js`・`ds-custom-logger-1.1.0.min.js`、CSS: `bundle_20261005-040738.css`（1.8 MB）。
- 画像の取得（`BROWSER_NET_DUMP`）: 返ってきた形式は `image/png`・`image/jpeg`・`image/gif`（広告の 1 件）だけで、webp・avif は 0 件。`<picture><source type="image/webp">` は engine が `<source>` を見ず `<img src>`（jpg）を使うので選ばない。画像の request の Accept は下の 3。

## 直した物と原因

1. **gzip の body**: `Accept-Encoding: identity` を送っているのに、`yads.c.yimg.jp`・`s.yimg.jp` の JS（`Content-Encoding: gzip`）が圧縮のまま返り、script が `unexpected character`（1:1）で止まっていた。`net/http.c` に content coding の復号（gzip の header を読んで raw deflate を `libz-compat` の `inflateInit2(-15)` で、`deflate` は zlib の形と raw の両方）。同期の fetch と非同期の loader の両方（`net_http_parse_response`）。
2. **bundle の SyntaxError**（`bundle_….js:151:1138`、「missing ; before statement」）: `for (var k = function (e) { … e in k … }, O = …;;)` の initializer の中の関数の本体で、`in` が for の initializer の禁止（no-in）のまま残っていた。関数の本体で no-in を外した（`js/parse_stmt.c`）。試験 `plan/ws074/tests/js/syntax-in.js`（Chromium の出力と一致）。
3. **SVG の画像**: Yahoo のアイコンとロゴは CSS の `background-image:url("data:image/svg+xml,…")` と `<img src=*.svg>`。画像の復号が png・jpeg・gif だけで、SVG が出なかった。`page_svg_decode`（`page/svg.c`）: 画像の bytes が SVG の文書なら、scratch の document に parse して p228 の inline svg の描画に渡す（`page/images.c` の復号の 2 箇所）。
   - 続けて svg の描画で見つかった不足を直した: **stroke**（`stroke`・`stroke-width`・`stroke-opacity`・`stroke-linecap`、join は丸で近似、dash なし）、`<line>`、**clipPath**（`clip-path="url(#id)"`、mask の合成）、**`<use>`**（`xlink:href`・`href`）、**角丸の rect の角の位置が間違っていた**（`rx` つきの rect が割れて出ていた。p228 から）。
4. **CSS `transform`（translate）**: engine が `transform` を全く解釈していなかった（Yahoo の検索 button の中身は `position:absolute; top:50%; left:50%; transform:translateY(-50%) translateX(-50%)` で置いている）。`transform`・`-webkit-transform` を shorthand として parse（`translate`・`translateX`・`translateY`・`translate3d` の平行移動だけを 2 つの longhand `transform-x/y` へ。他の関数は受けて何もしない）、layout で box とその子孫を相対位置のように動かす（%は自分の border box）。回転・拡大・skew は未対応。
5. **画像の request の Accept**: 以前は全ての request が `Accept: text/html,…,*/*;q=0.8`（`*/*` のため webp も受ける形）だった。画像（`<img>`・CSS の background・`<object>`）の request は `Accept: image/png,image/jpeg` だけにした（`net_loader_fetch_kind(..., NET_FETCH_IMAGE)` と、同期の fetch の `net_http_set_kind`）。文書・script・CSS は従来のまま。確認: 手元の HTTP server で `/index.html` は従来の Accept、`/a.png`（img）・`/b.png`（CSS の background）は `image/png,image/jpeg`。
6. **font の fallback**: 文章中の「…」（U+2026）が豆腐だった。Mahora と DroidSansFallback に無い字を等幅の face（JetBrains Mono）から探す（Chromium の fontconfig と同じ結果、`text/font.c`）。
7. google の続きで足した物（`DOMPointReadOnly`・`DOMPoint`、gzip、`document.currentScript` など）は p231。

## 確かめ（host）

- yahoo.co.jp live（1024x768、`live-compare.py`）。ours・Chromium・差分: [before](../../../build/review/b1-yahoo-before.png)、[after](../../../build/review/b1-yahoo-after.png)。広告の枠は request ごとに違う広告が来て差分になるので、全体の数は動かない。広告の外の部分の一致（agree、ink）:

  | 領域 | 前 | 後 |
  | --- | --- | --- |
  | 全体 | 74.40%、39.99% | 73.79%、40.01%（広告で動く） |
  | 左（x<640） | 87.86%、66.75% | 89.55%、70.77% |
  | header（y<130） | 88.69%、24.45% | 92.84%、53.48% |

  見た目: header の logo・nav の icon・検索の button（「検索」の文字と虫眼鏡）・news の comment の icon・「NEW」「LIVE」の badge・「ビックアップ」の box の icon・weather の icon・広告の画像が出る。前は icon と logo がなく、検索の button は文字なし、右の広告の枠が空だった。
- build warning 0。Acid3 score 100・差 0、Acid2 差 **873**（前 1,865。font の fallback か transform による。後退ではない）。host JS 17/17（`bigint`・`typed`・`syntax-in` を追加）、CSS rule model 51/51、Grid 48/48。固定 WPT の 1,455 ID（`build/b1y/runall.py` と同じ条件）: 前後とも 483 pass、callback 10,982→10,983、後退 0。google.com の描画は p231 の水準を保つ（ink 67〜73%、script errors 0）。
- 未実施: QEMU・実機。

## 残り（yahoo）

- 広告の iframe の viewability の script が `Cannot read properties of undefined (reading 'elementFromPoint')` を投げ続ける（50 件、`yads-async.js`、広告の画像自体は出る）。原因は未特定（frame の `defaultView.document` の経路）。
- 左下の広告の feed（Chromium では記事風の広告が出る）は空の灰色の枠のまま（広告 network の応答待ちの形）。右上の「ワンクリックで Yahoo! JAPAN を開くには」の popup は Chromium だけに出る（script が cookie か storage で判断）。
- 見出しの前の「»」の marker（`background-image` の SVG が inline の `<span>` に付く）: **inline の box の background・border・padding を engine が描かず、line の幅にも入れていない**（`<span style="background:#cfc;padding-left:11px">` で確認、Chromium は描く）。layout の本体（`layout/inline.c`）の仕事なので別の Phase。
- 回転・拡大などの transform、`srcset`・`<picture>`、WebP・AVIF の decoder（作らない）。
