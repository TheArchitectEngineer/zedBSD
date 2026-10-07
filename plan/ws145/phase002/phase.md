<!-- awesome-plan project=zedbsd record=ws145-p002 -->

# ws145-p002: keiland-printd（約束の行・spool・IPP・LPD）

Status: cleared（2026-10-07 Q1 の判定: T1-318 の AAT settings.printers（needs-person）で IPP・LPD の printer の追加と 3 つの job が Done、PDF Viewer の Printed、Printers の頁の PNG（Mock Printer が Default、LPD の printer、form）を Q1 が目視。T1-320 の full の中の fail は前の scenario の状態の残りの疑いで P2 が切り分ける）（旧: in-progress（2026-10-07 q831 P2））
Disposition: normal
Parent: [WS145](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)（[design.md](../design.md) 第 3.1 版、D2〜D9 は 2026-10-05 夜にユーザーが決定）

## 実装（正常系）

`userland/desktop/printd/`（新規、package `keiland-printd`、`/usr/libexec/keiland-printd`）。

- `main.c`: fd 3 の backend との行（`JOB`+fd・`CANCEL`・`NAME`・`BYE` を受け、`SPOOL`・`ACCEPTED`・`REJECTED`・`STATE`・`PATH`・`NAMED`・`IDLE` を返す）、受けた fd の FIFO（JOB の行ごとに 1 つ）、行の検め（制御文字・空白・数）、`closefrom(4)`・SIGPIPE を無視、fd 3 の EOF で全 job を止め spool を消して終わる、BYE は数が合い job が無い時だけ。
- `spool.c`: `$XDG_RUNTIME_DIR/keiland-print/<pid>-<乱数>/`（lock file を flock で持つ。持ち主のいない dir は起動で消す）、runtime dir は利用者の物で 077 が 0、文書は fstat の大きさまで（256 MiB、合計 512 MiB）複写し、先頭 1024 byte に `%PDF-`。
- `ipp.c`: Get-Printer-Attributes（2.0、`server-error-version-not-supported` なら 1.1）で path を探す（設定の path → `/ipp/print` → `/ipp` → `/`、見つけた path を `PATH`）、`application/pdf` が一覧に無ければ failed format、Print-Job（HTTP POST、Content-Length、Connection: close、本体は message と文書）、busy は 30 秒後に 3 回まで、job-id で Get-Job-Attributes を 5 秒ごと（completed・canceled・aborted、30 分で done unconfirmed、job-state の無い printer は done unconfirmed）、取り消しは Cancel-Job。応答の HTTP は 1xx を読み飛ばし、Content-Length・chunked・接続の終わり。textWithLanguage は言語を除く。NAME は info → make-and-model → 「host (IPP)」。
- `lpd.c`: `\002queue` → data file を先に（`\003<len> dfA<nnn><host>`）→ control file（H・P・J・N・l・U、title は 99 byte まで文字の境界で）、各 0 の応答、最後の応答は 5 分待ち（無ければ done unconfirmed）、control file の前の取り消しは接続を切って cancelled。
- `net.c`: getaddrinfo の順に接続（10 秒）、以後は blocking の送受信（進みの無い 60 秒で timeout）、文書は 64 KiB ずつ（取り消しはその間で）。
- 設計との違い（正常系の簡略）: 送信は job ごとの thread の blocking I/O（設計の §5.3 の単一の poll の loop と補助の名前解決の thread の代わり）、行は mutex の下で送る。同時の送信の数（printer ごと 1・全体 4）は数えない。→ backlog。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws145/tests/run-host-printd.sh` → PASS 15（ASan・UBSan の printd を `printd-test.py` が backend の代わりに fd 3 で動かし、`mock-printers.py`（IPP は `/ipp/print` だけ、LPD）へ: SPOOL、IPP の ACCEPTED・PATH（誤った path から探す）・waiting・done・文書の SHA-256・job-name、LPD の ACCEPTED・done・data file の SHA-256・control file、NAMED、PDF でない文書の REJECTED format、知らない job の CANCEL、BYE で終わり spool が消える）。
- zedBSD: `make ZEDBSD_CONFIG=plan/ws145/tests/config-amd64-print.mk BUILD=build/ws120-zed build/ws120-zed/bin/keiland-printd` warning 0。style-check 0（printd の全 file）。
