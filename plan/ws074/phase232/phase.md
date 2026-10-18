<!-- awesome-plan project=zedbsd record=ws074-p232 -->

# ws074-p232: yahoo.co.jp のニュース feed（fetch・非同期 XHR ほか）

Status: cleared（2026-10-18 Q1: Status の行が無かったので写した。元: cleared（host、2026-10-14 B1。QEMU・実機は未実施。広告の viewability と Proxy は残り））

B1 (2026-10-14)。ユーザーの指摘（原文「yahooはニュース記事がJSで取得できていなように見えますね」）。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p230・p231 の続き。

## 原因（観測した順）

1. feed は bundle の redux の thunk（`fetchFirstContents`）が `fetch("https://quriosity.yahoo.co.jp/v2/recommend/list?…", {credentials, headers})` で取る JSON を React が描く。前の `fetch` は GET の URL だけで、option（header・method・body）、Headers、status の実値、Request を持たなかった。
2. それより前に、bundle の module `1idS` が `Object.freeze` の中で **EPERM** を投げずに返して、script 全体が黙って止まっていた（`vm/object.c` の `vm_object_define`: 拡張不可の配列の既存の要素の attribute を変える定義を「新しい property」と見て拒否。immer の `freeze` が引っかかる）。`bind_run_script` は EPERM のような「例外でない失敗」を黙って捨てていたので、`Uncaught InternalError: the script stopped (...)` を console に出すようにした（同様に microtask の queue の失敗）。診断に `BROWSER_STACK`。
3. 実装した後も fetch の promise が解決しなかった。bundle の core-js が **Promise を自分の物へ差し替える**（`PromiseRejectionEvent` が無いと native を信用しない）うえ、その microtask は `queueMicrotask` が global の own property であることを前提にする（この engine は Window の prototype にあった）。`PromiseRejectionEvent` を足し、`queueMicrotask` を global の own property にして、native の Promise が残るようにした。
4. `HTMLElement.prototype.focus` が無く、検索の箱の `e.focus({preventScroll})` が例外を投げていた。`focus`・`blur`・`document.activeElement`・focus/blur/focusin/focusout の event を足した。
5. `CSSStyleRule` などの `instanceof` 用の interface（google の script が参照）を、rule の持つ property で判定する形で足した。

## 実装

- `net/`: loader に method・header 行・body を持つ request（`net_loader_send`）、response の header 全部（Set-Cookie を除く、`net_response.headers`）、redirect の 303・301/302 の POST→GET、GET 以外・header つきは cache しない。blocking の `net_http_fetch_send` も同じ。script の request の既定の header は Accept `*/*`（script が与えた Accept・Accept-Language を優先）。Host・Content-Length・Connection・Cookie などは script から変えられない。
- `page/script.c`・`bind/bind.h`: host の口を `fetch` から `request`・`request_cancel` に変更（loader があれば非同期、無ければ blocking。data:・file: は GET のみ）。
- `bind/prelude.c`・[net.js](net.js)（`make-prelude.py` で `prelude-net.inc`）: native は `send`・`cancel`・`syncGet` の 3 つだけで、`fetch`・`Headers`・`Request`・`Response`・`Blob`・`File`・`FormData`・`TextEncoder`・`TextDecoder`・`ProgressEvent`・`XMLHttpRequest`（非同期、readyState 1〜4、status、statusText、responseURL、getResponseHeader・getAllResponseHeaders、POST・header・body、responseType text/json/arraybuffer/blob/document、timeout、abort、upload、同期の GET）、`navigator.sendBeacon`、`URL.createObjectURL` は JS（旧 C の XHR・fetch・Response は削除）。`AbortSignal` は fetch が受ける。
- 限界: body は UTF-8 の文字列で持つので、binary の body・`arraybuffer` の応答は壊れる。`ReadableStream`（`response.body`）なし。CORS は検査しない（cookie は常に送る）。`Proxy` 未実装。
- 試験: [fetch.html](../tests/fetch/fetch.html)・[run-fetch-tests.py](../tests/run-fetch-tests.py)（手元の server に対し blocking と `--async` の両方で 38 項目、GET・POST・JSON・form・multipart・header・404・redirect・302 の POST→GET・拒否・abort・XHR の state・error・abort・sync・Blob）。

## 確かめ（host）

- yahoo.co.jp live（1024x1600）: 左の列の下と中央の下の feed に記事（見出しと画像、広告つき）が出る。ours・Chromium・差分: [before](../../../build/review/b1-yahoo-feed-before.png)、[after](../../../build/review/b1-yahoo-feed-after.png)、[ours](../../../build/review/b1-yahoo-feed-ours.png)、[chromium](../../../build/review/b1-yahoo-feed-chromium.png)。数は広告が request ごとに違うので動く（前 pixels 72.28%・ink 32.90%、後 69.98%・35.34%）。feed の記事は Chromium と同じ書式で出るが、記事の中身は毎回違う。
- build warning 0（`sh plan/ws074/tests/host-build.sh plain`）、host JS 17/17、DOM 23/23、http 15/15、loader 11/11、CSS rule model 51/51、fetch 38/38（blocking・`--async`）、Acid3 score 100・差 0、Acid2 差 873（不変）。固定 WPT 1,455 ID: 483 pass（p231 と同じ）、callback 10,983（p231 は 10,982）。Grid の `test-grid-abspos-static`・`test-grid-auto-track-css` は main の build（古い binary）でも同じ出力で失敗する（この変更による後退ではない）。
- google.com live: ink 73.35%（p231 は 73.28%）、全体 99.37%。script error 1（後続の chunk の `for await` の構文、前は読み込まれなかった）。
- 未実施: QEMU・実機。

## 残り

- 広告の viewability の script が `Cannot read properties of undefined (reading 'elementFromPoint')` を投げ続ける（yads-async、script error の数が 8→51 に増えたのは広告の script が前より先まで動くため）。`e.ownerDocument.defaultView.document` の経路が iframe の中で undefined になる所までは絞れていない（試験の frame では取れる）。
- `Proxy` 未実装（Yahoo の別の script が参照）、`for await` 未実装。
- 右上の「ワンクリックで Yahoo! JAPAN を開くには」の popup は、ours でも位置が違うが出る。
- CSSOM の rule list の index アクセス（`cssRules[0]`）が undefined を返す（`item(0)` のみ？）。未調査。
