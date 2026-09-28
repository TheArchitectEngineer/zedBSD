<!-- awesome-plan project=zedbsd record=ws074p050 -->

# ws074-p050: 非同期の loader（核）と部品化の手順 4

Phase ID: `ws074-p050`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p016、p017、p053

2026-09-28 に main の承認で分けた: 持続接続（host ごとの接続の pool、`Connection: close` をやめて長さ・chunked で応答の
終わりを知る）と memory の cache（Cache-Control の max-age・no-store、ETag と If-None-Match の再検証、304）は
[ws074-p058](../ws.md)。

## 範囲（正常系のワンパス）

- `net/loader.c`（新）: http・https の GET を block しない request の状態機械。名前の解決は resolver の thread（`getaddrinfo`、
  pthread、job の queue と答えの list は mutex の下、答えは pipe の 1 byte で main loop を起こす）、non-blocking の connect
  （EINPROGRESS → 書けるようになったら `SO_ERROR` と `getpeername` で確かめる、失敗は次の address）、https の TLS の handshake、
  request の送信、応答を接続の終わりまで読む（`Connection: close`）、`net_http_parse_response` で解析、redirect（20 回まで、
  http・https だけ）、30 秒進まない request は ETIMEDOUT。API: `net_loader_create`・`_destroy`・`_fetch`（callback と context）・
  `net_request_cancel`・`_error`・`_response`・`net_loader_poll_fds`・`_timeout`・`_process`・`net_loader_takes`。callback の
  中で request を始める・取り消す（自分も）ことができ、終わった・取り消した request は list を歩き終えてから解放する。
- `net/tls.c`: `net_tls_start`（handshake 無しの準備）、`net_tls_handshake`・`net_tls_read_some`・`net_tls_write_some`
  （待つときは EAGAIN と POLLIN・POLLOUT）。`net/http.c`: 要求の文と応答の解析を loader と共有する関数に
  （`net_http_request_text`・`net_http_parse_response`・`net_http_is_redirect`・`net_http_is_web`）。
- 部品化の手順 4（p053）: `page/network.c`（新）の `page_net_create`・`_destroy`・`_poll_fds`・`_timeout`・`_process`・
  `_is_remote`・`_fetch`・`_cancel`・`_result` と `page_set_loader`・`page_load_bytes`。shell と main は `net/` を直接使わない。
- page: loader を持つ page の http・https の画像（`<img>` と背景）は block せずに取得し、届いたら decode して
  `images_generation` を数え、`page_needs_layout` が layout をやり直させる。file・data: と loader の無い page は今までどおり
  その場で読む。page を壊すと取得中の画像の request を取り消す。
- shell: 起動の時に loader を作る。http・https の navigation（link・場所の欄・履歴・再読み込み）は block せずに取得し、届くまで
  今の page を表示したまま（`ZBROWSER LOADING url=…`）。Esc で中止（`ZBROWSER STOPPED url=…`）。失敗は今の page のまま
  `ZBROWSER ERROR load …`。main loop の `poll` は compositor と loader の descriptor を一緒に待ち、timeout は loader の期限も
  見る（`shell_window_dispatch` が追加の descriptor を受ける）。最初の page は窓を開く前にその場で読む（今までどおり）。
- main: `--async`（headless の mode で document と画像を loader で取得し、画像が届いてから layout をやり直す）。
- 試験: `http-server.py` に `/images/NAME`（build/ws074-images、`?delay=MS`）、`run-http-tests.py --async`（全部の case と、
  http の画像の layout・背景画像の display list の 2 case）、`browser-p050.sh`（Venus の窓: http の画像の page、遅い page の
  LOADING と Esc の STOPPED、背景画像の page、guest の `run-http-tests.py --guest --async`）。`make-test-ca.sh` の証明書を
  1 日前から有効に（guest の時計が host より少し遅いと「not yet valid」で断られた）。

## 受け入れ

1. amd64 の build（warning 0）、新しい file と変えた file の style-check。
2. host（plain・ASan）: `run-http-tests.py --async` と同期の `run-http-tests.py` が全部通る。golden の dump が下がらない。
3. guest（Venus）: http の画像が後から届く、LOADING と Esc の中止、http の navigation、guest の `--async` の HTTP の試験。
   前の窓の試験（p016・p017・p045）が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- host: `run-http-tests.py --async` 16/16（plain と ASan・UBSan、sanitizer の報告を失敗に数える形にした）、同期の
  `run-http-tests.py` 14/14。golden の dump 28/28。
- guest（Venus）: `browser-p050.sh` status 0: http の images.html の画像が loader で届いて表示（写真
  `/home/awe/zedBSD-rpi4/build/ws074-shots/p050-20260928-window-http-images.png`）、`?delay=4000` の page で LOADING、Esc で
  STOPPED、images.html が残る、backgrounds.html を http で表示（`p050-20260928-window-http-backgrounds.png`）、ERROR なし、
  guest の `run-http-tests.py --guest --async` 16/16（https、実在の証明書の検証の失敗 2 種を含む）。
- 最初の guest の実行で http（https でなく）が全部 ENOTCONN で失敗した: resolver の答えの直後に connect の完了を待たずに
  次の段へ進み、まだ SYN_SENT の socket に `send` した（zedBSD の TCP は ESTABLISHED でないと ENOTCONN。https は `write` の
  経路で待てていた）。書けるようになるまで待ち、`getpeername` でも確かめるよう直した。host は localhost の connect がすぐ終わる
  ので出なかった。
- 前の窓の試験: `browser-p016.sh`（http の link・Back・場所の欄の redirect と cookie、今は非同期の navigation）status 0、
  `browser-p017.sh`（https、別名の証明書の拒否、`https://example.com/`）status 0（証明書の日付を直した後）、`browser-p045.sh`
  status 0。
- boot test: PASS、`p050-20260928-boot-login.png`。
- build: amd64 の image（browser の warning 0）。style-check: 新しい file と変えた file は 0。ただし `net/loader.c` の
  mutex の critical section（取得・空行・本体・空行・解放、coding-style.md §5 の形）の本体と解放の行を style-check が
  「段落の comment が無い」と数える 10 件は残した（道具の既知の限界: kernel の `tcp.c` の critical section も同じく数える）。

## 後回し（follow-up）

- 持続接続と memory の cache: ws074-p058。
- parser を止める `<script src>` と headless の mode の既定はその場で読む（同期）。script の非同期化は event loop の作業と一緒に。
- 読み込み中の表示（titlebar の進み具合、Reload を Stop に）、request の優先度と同時の数の上限、HTTP/2。
- 最初の page も非同期に（窓を先に開く）。
