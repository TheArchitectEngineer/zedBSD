<!-- awesome-plan project=zedbsd record=ws074p017 -->

# ws074-p017: TLS（OpenSSL の dlopen、D2）、https、自前の CA の host の server、guest で実在の site

Phase ID: `ws074-p017`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p016（cleared）
Bug: [BUG-083](../../bugs/BUG-083.md)（rtld の dlopen が /usr/lib を探さない。この Phase の中で修正、2026-09-28 main の判断）

## 範囲（正常系のワンパス）

- `net/tls.c`（新）: OpenSSL の package の `libcrypto.so`・`libssl.so` を最初の https で `dlopen`（無ければ host の
  `.so.3`）。関数は 25 個を表（`tls_symbols`）で `dlsym` し、不透明な pointer で呼ぶ（OpenSSL の header は使わない）。
  context: TLS 1.2 以上、`SSL_VERIFY_PEER`、既定の root（ca-certificates の `/etc/ssl/cert.pem`）と `--ca-file` の CA、
  close_notify の無い close を終わりとして読む。接続: 名前は SNI と `SSL_set1_host`、IPv4・IPv6 の address は
  `X509_VERIFY_PARAM_set1_ip_asc` で照合。失敗は `EPROTO` で、理由（証明書の検証の文、OpenSSL の error queue）を
  `net_tls_error` が返す。package が無ければ `EPROTONOSUPPORT`（loader の理由付き）。
- `net/http.c`: https を受ける（`http_connection` の socket と TLS、TLS が持つ byte があれば poll を飛ばす）、既定の port は
  scheme ごと（443）。http から https への redirect。
- page: `page_fetch` が https も取る。main: `--ca-file=PEM`（試験の CA）。`cannot load` の行と窓の `ZBROWSER ERROR load`
  の行に TLS の理由を付ける。
- guest の image（`config-amd64-browser.mk`）に ca-certificates を足した（OpenSSL の package は元から入っている）。
- [BUG-083](../../bugs/BUG-083.md): rtld の dlopen を直した（bare name を DT_NEEDED と同じ順で /lib → /usr/lib から探し、
  読み込み済みの判定を basename で比べる。他の絶対 path は今までどおり断る）。
- 試験: `make-test-ca.sh`（host の openssl で CA と、10.0.2.2 等の証明書と別名の証明書を作る。commit しない）、
  `http-server.py` の TLS の port、`run-http-tests.py` の https の 6 件、`browser-p017.sh`（窓）、`rtld-dlopen.sh`
  （`rtld-dlopen.c`・`rtld-probe.c`、BUG-083 の確認）。

## 受け入れ

1. amd64 の build（warning 0）、新しい file と変えた file（rtld.c は変えた範囲）の style-check 0。
2. host（plain・ASan）と guest で https の page、chunked、http → https の redirect、Secure の cookie が http に送られない、
   信頼しない CA と別名の証明書を断る。http の試験は下がらない。
3. 実在の site: host と guest で `https://example.com/`。
4. 窓: https の page と link、別名の証明書の error で page が残る、実在の site。
5. 前の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- HTTP/HTTPS の試験 14/14（host plain・ASan、guest）。https: 長さ・chunked・http → https・Secure の cookie（`"p=2"` だけが
  http に届く）・信頼しない CA（`certificate verify failed: unable to get local issuer certificate`）・別名
  （`IP address mismatch`）。ASan で leak の報告なし。
- 実在の site（host）: `https://example.com/` を表示。badssl.com の self-signed・expired・wrong.host・TLS 1.0（port 1010）を
  それぞれの理由で断る。guest: `https://example.com/` を表示し、`https://expired.badssl.com/` を
  `certificate has expired` で断る（ca-certificates の root で検証）。
- 窓（Venus guest、zdesktop `--glass` と壁紙）: `browser-p017.sh` status 0。https の first.html、link で second.html、
  別名の証明書の場所は `ZBROWSER ERROR load ... tls=handshake: certificate verify failed: IP address mismatch` で断り
  second.html が残る、場所の欄の `https://example.com/` を表示。
  - 写真: `/home/awe/zedBSD-rpi4/build/ws074-shots/p017-20260928-window-https-first.png`・`-https-link.png`・
    `-https-example-com.png`。
- BUG-083: `rtld-dlopen.sh` が修正前 2/7、修正後 7/7（guest）。
- 前の試験: host-link 22/22（ASan）、DOM 6/6（host・guest）、golden 28/28（ASan）、URL 896/896・data 72/72（ASan）、guest の
  JS 7/7、窓の browser-p016・p045 status 0。
- style-check: `net/tls.c`・`net/http.c`・`net/net.h`・`main.c`・`shell/shell.c`・`page/link.c`・`rtld-dlopen.c`・`rtld-probe.c`
  0。`src/rtld/rtld.c` は変えた行に新しい違反なし（file 全体の既存の違反は 246 → 245）。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p017-20260928-boot-login.png`）。実機: 未実施。
- commit: `d04885e2`（TLS と試験）、`4eed896b`（rtld の修正、rtld の確認、窓の試験、dlopen の理由）、この記録の commit。

## 後回し（follow-up）

- 証明書の error の page（今は窓が ERROR の行を書き、前の page が残る。design.md §9 の「失敗は error の page」）、
  混在 content の遮断（https の page の http の script）。
- TLS の session の再利用、ALPN、OCSP・CRL（失効の確認はしていない）、証明書の透明性。
- 非同期の loader の中の TLS（p050: non-blocking の handshake と read）。
- base の libssl・libcrypto の互換品（Future Work の候補、`net/tls.c` は読む名前の順を変えるだけ）。
