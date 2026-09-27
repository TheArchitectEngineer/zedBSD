<!-- awesome-plan project=zedbsd record=ws074p016 -->

# ws074-p016: HTTP/1.1 の client（同期のワンパス）、cookie、http の page と script

Phase ID: `ws074-p016`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p014（cleared）、ws074-p015（cleared）

## 範囲（正常系のワンパス）

計画の p016 は「非同期、持続接続、resolver の thread、memory の cache」も含んでいたが、正常系のワンパスを優先する実行の順
（design.md §18）に従い、同期の fetch で page と script を http で開くところまでを p016 とした。非同期の loader・resolver の
thread・持続接続・memory の cache は [p050](../ws.md) に分けた（2026-09-28）。

- `net/http.c`（新）: `net_http_fetch`（GET、HTTP/1.1、`Connection: close`、`Accept-Encoding: identity`、User-Agent
  `zdesktop-browser/0.1 (zedBSD)`）。`getaddrinfo` の各 address へ順に connect、`poll` で 30 秒の timeout、256 MiB の上限。
  応答: status 行、Content-Type・Content-Length・Transfer-Encoding（chunked の復号、chunk の拡張と trailer は読み捨て）・
  Location・Set-Cookie。redirect（301・302・303・307・308、最大 20 回、Location は今の URL に対して解決）。http 以外の scheme は
  `EPROTONOSUPPORT`（https は p017）。
- `net/cookie.c`（新）: RFC 6265 の最初の pass。process の間だけの jar（512 個、古いものから捨てる）。Domain（先頭の点、
  host の domain-match、IP address は subdomain を持たない）、Path（無ければ request の directory）、Max-Age ≤ 0 による削除、
  Secure、HttpOnly（保持だけ）、host-only。同じ name・domain・path は置き換え。Cookie header の組み立て。
- page: `page_load_location`（file の path か URL。URL は `page_fetch` で取り、`page->base` は redirect 後の最終の URL）。
  `page_fetch(base, href, bytes, final_url)` が http を扱う（`<script src>` も http で読める）。`page_resolve_location` で link を
  http の URL に対しても解決する。
- shell: 窓の場所・履歴・link の解決は page の最終の location（redirect 後）を使う。場所の欄に http の URL を入れて開ける。
- 試験: `plan/ws074/tests/http-server.py`（host の test server。Content-Length・chunked・close で終わる本文・redirect の連鎖・
  cookie・script・status）、`run-http-tests.py`（host と `--guest`）、`browser-p016.sh`（窓）。

## 受け入れ

1. amd64 の build（warning 0）、新しい file と変えた file の style-check 0。
2. host（plain・ASan）と guest で、長さ・chunked・close・redirect 6 回・cookie（path の合うものだけ）・http の script・404・
   接続の拒否が期待どおり。
3. 窓: http の page を開き、相対の link を辿り、戻る。redirect と cookie の page を場所の欄から開く。
4. 前の試験（host-link、DOM、golden、URL、JS）が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- host の HTTP の試験 8/8（plain・ASan）。guest（zedBSD、host の server に 10.0.2.2:8074 で接続）8/8。接続の拒否（port 1）も
  guest で「cannot load」を報告する。
- host で実在の site: `http://example.com` を headless で描いた（`/home/awe/zedBSD-rpi4/build/ws074-shots/p016-20260928-example-com-host.png`）。
- 窓（Venus guest、zdesktop `--glass` と壁紙）: `browser-p016.sh` status 0。first.html を http で表示、link で second.html、
  Back で戻る、場所の欄の `/cookie/set` が redirect の後の `/cookie/echo` に着き `session=abc123; theme=dark` を表示
  （Path=/elsewhere の cookie は送られない）。
  - 写真: `/home/awe/zedBSD-rpi4/build/ws074-shots/p016-20260928-window-http-first.png`・`-http-link.png`・`-http-cookies.png`。
  - 最初の実行で、窓の場所が redirect 前の URL のままだった（`/cookie/set`）。shell が page の最終の location を使うように直した。
- 前の試験: host-link 22/22（ASan）、DOM 6/6（host・guest）、golden 28/28（ASan）、URL 896/896・data 72/72（ASan）、
  browser-p045 status 0（file の path の履歴は変わらない）。
- guest の JS の試験 7/7。WS076 の libm の書き直しの後、`operators.js` が GUEST_KNOWN（BUG-078）なしで通る（commit `1b8ffdb0` で
  GUEST_KNOWN を空にした）。
- style-check: `net/http.c`・`net/cookie.c`・`net/net.h`・`page/link.c`・`page/page.c`・`page/page.h`・`page/script.c`・
  `shell/shell.c`・`main.c` 0。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p016-20260928-boot-login.png`）。実機: 未実施。
- commit: `53aa5147`（code と試験）、`baca98a8`（窓の location の修正と窓の試験）、この記録の commit。

## 後回し（follow-up）

- p050: 非同期の loader（event loop の中の non-blocking な socket）、resolver の thread、持続接続と接続の pool、memory の
  cache（Cache-Control・ETag）、読み込み中の表示と中止。今は fetch の間、窓が止まる。
- Expires の日付の parse（今は Max-Age だけが期限を決め、残る cookie は session の間だけ）、public suffix の確認、
  SameSite、cookie の上限（domain ごと）。`document.cookie` は p032。
- Content-Type の charset と MIME sniffing（今は HTML として parse し、文字は UTF-8 の既定のまま）。
- gzip・deflate（今は `Accept-Encoding: identity`）、HTTP/1.1 の `100 Continue`・`1xx`、proxy。
- https は p017。
