<!-- awesome-plan project=zedbsd record=ws074p058 -->

# ws074-p058: 持続接続と memory の cache

Phase ID: `ws074-p058`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p050

2026-09-28 に main の承認で [ws074-p050](../phase050/phase.md) から分けた。順序は p050 → p058 → p054。

## 範囲（正常系のワンパス）

- `net/http.c`: 応答の終わりを接続の終わりでなく framing で知る `net_http_framing_update`（Content-Length、chunked
  （拡張と trailer を含む）、長さの無い応答は接続の終わりまで、HEAD の無い GET だけ扱う。1xx・204・304 は body 無し）。
  `Connection: close` と HTTP/1.0 は接続を返さない。`net_http_request_text` に keep_alive（偽のとき `Connection: close`）と
  追加の header の行。応答の `Cache-Control`（max-age・no-store・no-cache）と `ETag` を `net_response` に読む
  （`net_response_init`）。同期の `net_http_fetch` は今までどおり `Connection: close`。
- `net/loader.c`:
  - 接続の pool: 応答を読み終えて keep-alive の接続は scheme・host・port の key で idle の list へ（TLS の session ごと）。
    次の request は同じ key の idle の接続を使う（resolver・connect・handshake を省く）。idle が 30 秒を超えた接続は閉じる。
  - host ごとの上限 6 本: 接続を持つか開いている request が 6 あれば、新しい request は `LOADER_WAITING` で待ち、接続が
    pool に戻る・閉じる時に最初の待ちが始まる。
  - 古い接続: 再利用した接続への送信が失敗した、または応答の 1 byte 目の前に閉じた request は 1 度だけ新しい接続でやり直す。
  - memory の cache（64 MiB、LRU）: max-age か ETag を持ち no-store でない 200 を URL（fragment 無し）で保つ。max-age の
    間は network に出ずに答える（`LOADER_READY`、次の `process` で callback）。古い・no-cache で ETag のある entry は
    `If-None-Match` で再検証し、304 なら保った body で 200 として答え、鮮度を更新する。
- 試験: `http-server.py` に `/cached/NAME`（`?max-age=N`・`&etag=1`・`?no-store`）、`/stats`・`/stats/reset`（接続と path
  ごとの request と 304 の数）、`--idle`（idle の接続を閉じる秒、既定 1）。`host-loader.c`（新、loader を直接使う driver:
  seq・par、`--ca`・`--pause`）と `run-loader-tests.py`（新、11 case）。`browser-p058.sh`（新、Venus の窓）。

## 受け入れ

1. amd64 の build（warning 0）、新しい file と変えた file の style-check。
2. host（plain・ASan・UBSan）: `run-loader-tests.py` が全部通る（接続の再利用、上限、chunked・close の後、redirect、古い
   接続のやり直し、fresh の cache、ETag の再検証、no-store、期限切れの再検証、HTTPS の再利用）。`run-http-tests.py --async`
   と同期の `run-http-tests.py` が下がらない。golden の dump が下がらない。
3. guest（Venus）: 窓で画像の page の画像が接続を再利用して届く、max-age の page の 2 回目は request 無し、ETag の page の
   2 回目は 304、server が idle の接続を閉じた後の navigation。前の窓の試験（p050・p016・p017）が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- build: `build-browser-image.sh`（amd64 の image、browser の warning 0、log `build/p058-image.log` は worktree 内）。
- style-check: `net/http.c`・`net/net.h`・`plan/ws074/tests/host-loader.c` は 0。`net/loader.c` は p050 からの mutex の
  critical section の 10 件（道具の既知の限界、p050 の記録を参照）だけで、この Phase で足した行の指摘は 0。
- host: `run-loader-tests.py` 11/11（plain と ASan・UBSan、LeakSanitizer も報告なし）。`run-http-tests.py --async` 16/16
  （ASan）、同期の `run-http-tests.py` 14/14（plain と ASan）。golden の dump 28/28。
- guest（Venus、QEMU）: `browser-p058.sh` status 0。
  1. http の images.html: 最初の page は窓を開く前に同期で読む（1 接続）、画像 8 枚は 6 接続（上限どおり、7 接続・9 request）。
     写真 `/home/awe/zedBSD-rpi4/build/ws074-shots/p058-20260928-window-http-images.png`。
  2. `/cached/fresh?max-age=60` を場所の欄から 2 回（間に images.html）: NAVIGATE 2 回、server の request 1 回。写真
     `p058-20260928-window-cached.png`。
  3. `/cached/tagged?etag=1` を 2 回: request 2 回、304 が 1 回、表示された。
  4. 3 秒待って（server は 1 秒で idle の接続を閉じる）`/pages/first.html`: 表示された。
  5. zdesktop・browser の ERROR なし。guest の `run-http-tests.py --guest --async` 16/16。
- 前の窓の試験: `browser-p050.sh` status 0（LOADING・Esc の STOPPED を含む、guest の HTTP 16/16）、`browser-p016.sh`
  status 0、`browser-p017.sh` status 0（`https://example.com/` を含む）。
- boot test: PASS、`/home/awe/zedBSD-rpi4/build/ws074-shots/p058-20260928-boot-login.png`。
- 実機: 未実施。

## 後回し（follow-up）

- 古い接続のやり直しは送信の失敗と応答の前の EOF だけ。受信の ECONNRESET、TLS の書き込みの失敗での再試行は未。
- 再検証の最中に entry が LRU で消えた場合の 304 はそのまま 304 として返る（通常は起きない）。
- 再読み込み（Reload）は cache を迂回しない（fresh な page は cache から出る）。`Cache-Control: no-cache` を要求に付ける
  再読み込み、`Vary`、`Expires`・`Last-Modified`（`If-Modified-Since`）、heuristic な鮮度は未。
- 同じ URL の同時の request を 1 つにまとめる（coalescing）、request の優先度、HTTP/2、disk の cache は未。
- 同期の経路（`net_http_fetch`、最初の page と `<script src>`）は pool と cache を使わない。
- 1xx の中間の応答（100 Continue 等）は body の無い最後の応答として扱う（その後の本当の応答を読まない）。GET だけを送るので
  通常は来ない。
