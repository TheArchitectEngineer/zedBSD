<!-- awesome-plan project=zedbsd record=ws145-design -->

# WS145 の設計: 印刷（ws145-p001、2026-10-05 P2 g15、q754）

[WS145](ws.md) の単一目標「app が libkeiland に PDF の場所を渡すと network の printer（IPP か LPD）で印刷される。一覧と既定の printer を libkeiland から compositor 経由で取れ、Settings の Printers の頁で IP address・port・protocol を設定できる」の設計。ユーザーの指示（2026-10-04 夜、ws.md に原文）に従う。

## 0. 判断の要点（先に）

| 番号 | 決めたこと（案） | 理由 | ユーザーの判断 |
| --- | --- | --- | --- |
| D1 | printer の daemon `keiland-printd` は**利用者の権限**で動く（root の daemon・setuid・init の service にしない）。compositor の libkeiland-backend が必要な時に起動し、仕事が無くなって 60 秒で終わる。 | network の printer に送るだけなら特権が要らない。root の daemon の口（sessiond の SERVICE の拡張など）を足さずに済む。ユーザーの指示「libkeiland-backend がプリンタデーモンが未起動なら起動」にそのまま合う。 | 要らない（特権の口を足さない案） |
| D2 | printer の設定は**利用者ごと**（`~/.config/keiland/printers.conf`、書くのは compositor だけ）。 | 管理者の権限も root の書き込みも要らない。system 全体の printer（`/etc`）は管理者の操作と root の書き込みの口が要るので後の段。 | **要確認**: 複数の利用者で printer を共有したいか（したいなら system の設定の段を足す。root の口が要るので別に設計して承認を取る） |
| D3 | 設定の項目は address・port・protocol だけ（ユーザーの指示）。IPP の resource の path は `/ipp/print`（IPP Everywhere の既定）→ `/ipp` → `/` の順に試す。LPD の queue の名前は `lp` に固定。名前は自動（IPP で取れれば `printer-info` か `printer-make-and-model`、取れなければ「address (IPP)」）。 | 指示のとおり 3 項目に保ち、多くの printer で通る既定を選ぶ。 | **要確認（小）**: LPD の queue の名前（`lp` で通らない printer がある。`raw`・`BINARY_P1` など）を入力の項目に足すか |
| D4 | 文書は PDF だけ（段 1）。printer が PDF を受けなければ job は失敗（理由 format）。PostScript・vendor の形式は p005。 | 指示のとおり。 | 要らない |
| D5 | Linux・FreeBSD の Keiland も同じ `keiland-printd`（POSIX の socket だけで書く）。CUPS は使わない（外部の package に依らない）。CUPS の queue を一覧に出すのは後の候補。 | 3 つの OS で同じ code。CUPS を包むのは外部の仕組みへの依存なので、やるなら別に判断を取る。 | 要らない（CUPS を使う案は出さない） |
| D6 | app の Print の menu（PDF Viewer・Notes など）は範囲の外。WS145 は libkeiland の口と試験の client（`userland/tests/printtest`）まで。 | 単一目標は「libkeiland に渡すと印刷される」。app への組み込みは app の WS で。 | **要確認（小）**: PDF Viewer の File > Print を WS145 に足すか（足すなら p007） |

外部の実装は取り込まない（IPP・LPD の符号化は RFC から自分で書く）。HAL・UAPI・toolchain には触れない。

## 1. 全体の流れ

```
app ──kl_system_printers_print(printer, path, title)──▶ libkeiland
       （libkeiland が path を O_RDONLY で開き、先頭の "%PDF-" を確かめて fd を送る）
libkeiland ──kl_system_printers_v1.print(request, printer, title, fd)──▶ compositor
compositor ──kl_backend_print_submit(...)──▶ libkeiland-backend（共通の code、userland/desktop/libkeiland-backend/print/）
backend ──（起動していなければ fork/exec、socketpair）── keiland-printd
backend ──"JOB ..." + fd（SCM_RIGHTS）──▶ printd ──spool に複写──▶ IPP（HTTP POST）/ LPD（TCP 515）──▶ printer
printd ──"STATE job state detail"──▶ backend ──▶ compositor ──job(...) event──▶ 全ての printers の object
```

- app は OS の口も daemon への接続も持たない（Guardrail「app と設定」）。libkeiland は fd を作るだけで daemon に繋がない。
- path でなく fd を compositor に渡す理由: app が読める file だけが印刷される（compositor が app の相対 path を解いたり、app が読めない file を読んだりしない）。app の API は指示どおり path を受ける。
- compositor は file を読まない。fd を backend に渡し、backend はそのまま daemon に渡す。

## 2. libkeiland の口（KL_VERSION を 1 つ上げる、`<keiland.h>`）

既存の kl_system_* と同じ形（`kl_system` の object から、状態は get、変化は listener の `changed`、依頼は request の番号と result）。

```c
#define KL_SYSTEM_HAS_PRINTERS   0x100U   /* capabilities の bit */
#define KL_PRINTER_IPP           1U
#define KL_PRINTER_LPD           2U
#define KL_PRINTER_DEFAULT       0x1U     /* flags */
#define KL_PRINTERS_MAX          16U
#define KL_PRINT_JOBS_MAX        16U

struct kl_printer {
	uint32_t id;                 /* compositor が振る、設定の file に残る */
	unsigned protocol;           /* KL_PRINTER_IPP / _LPD */
	char host[256];              /* IPv4・IPv6 の literal か host 名 */
	unsigned port;               /* 1〜65535（IPP 631、LPD 515 が既定） */
	char name[128];              /* 表示の名前（自動） */
	unsigned flags;              /* KL_PRINTER_DEFAULT */
};

struct kl_print_job {
	uint32_t job;                /* compositor の中で一意 */
	uint32_t printer;
	unsigned state;              /* KL_PRINT_QUEUED, _SENDING, _DONE, _FAILED, _CANCELLED */
	char title[128];
	char detail[64];             /* 失敗の語: unreachable, refused, format, busy, io, ... */
};

int kl_system_printers_get(const struct kl_system *system, struct kl_printer *printers, size_t capacity, size_t *count);
int kl_system_print_jobs_get(const struct kl_system *system, struct kl_print_job *jobs, size_t capacity, size_t *count);
int kl_system_printers_add(struct kl_system *system, unsigned protocol, const char *host, unsigned port, uint32_t *request);
int kl_system_printers_remove(struct kl_system *system, uint32_t printer, uint32_t *request);
int kl_system_printers_set_default(struct kl_system *system, uint32_t printer, uint32_t *request);
int kl_system_printers_print(struct kl_system *system, uint32_t printer, const char *path, const char *title, uint32_t *request);
int kl_system_print_cancel(struct kl_system *system, uint32_t job, uint32_t *request);
```

- `printer` に 0 を渡すと既定の printer。既定が無ければ ENOENT。
- `print` は libkeiland の中で path を開き、`%PDF-` で始まらなければ EINVAL（compositor に送らない）。大きさの上限 256 MiB（超えれば EFBIG）。title は 1 行・127 byte まで（改行は空白に）。
- result の `applied`・`saved` は既存の形。print の result が applied=1 なら job が受け付けられた（その job は job の一覧に出る）。

## 3. 拡張の protocol（kl_system_manager_v1 version 9）

```
kl_system_manager_v1
  request 9 get_printers(new_id kl_system_printers_v1)      since version 9 (ws145)

kl_system_printers_v1
  request 0 destroy
  request 1 add(uint request, uint protocol, string host, uint port)
  request 2 remove(uint request, uint printer)
  request 3 set_default(uint request, uint printer)
  request 4 print(uint request, uint printer, string title, fd document)
  request 5 cancel(uint request, uint job)
  event   0 printer(uint id, uint protocol, string host, uint port, string name, uint flags)
  event   1 job(uint job, uint printer, uint state, string title, string detail)
  event   2 done(uint serial)                 the printers and jobs before it are the whole state
  event   3 result(uint request, uint applied, uint saved)
```

- 作られた時に全ての printer と job（終わった job は最後の 16 個まで）と done。変化のたびに全体と done（devices と同じ）。
- 誰でも（その compositor の利用者の client なら）add・remove・set_default・print・cancel ができる。利用者ごとの設定なので管理者の判定は要らない。
- 不正な値（protocol、host の文字、port の範囲、16 個を超える）は result の applied=0（protocol の error にしない）。fd が PDF でなくても compositor は中を見ない（printd が読んで確かめる）。

## 4. compositor と libkeiland-backend

- compositor（`userland/desktop/wayland/system.c` に printers の object、または新しい `printers.c`）: 要求を backend に渡し、backend の通知で全ての printers の object に state と done を送る。fd は backend へ移したら閉じる。
- **backend の共通の code**（`userland/desktop/libkeiland-backend/print/`、OS に依らない POSIX）:
  - 設定の file `~/.config/keiland/printers.conf` の読み書き（compositor の thread から、他の設定の file と同じく一時 file から rename）。形:
    ```
    # Keiland printers
    printer 1 ipp 192.168.1.20 631
    printer 2 lpd printer.local 515
    default 1
    ```
    名前は file に残さず、IPP の名前は printd が問い合わせて job と同じ通路で返す（`NAME id text`）。
  - printd の起動: 最初の print の時、または名前の問い合わせの時に `fork` と `exec(KEILAND_LIBEXECDIR "/keiland-printd")`、`socketpair` の片方を fd 3 として渡す（入力 method と同じ形）。printd が終わったら（EOF）、次の要求で起動し直す。起動は 10 秒に 3 回まで（それ以上は job を failed（daemon））。
  - backend の poll の一覧に printd の socket を加え、行を読んで job の状態を更新し、compositor に `kl_backend_print_changed` を知らせる。
- OS ごとの tree（zedbsd・linux・freebsd）には何も足さない（OS の境界の checker の C 系に触れない）。

## 5. keiland-printd（`userland/desktop/printd/`、`/usr/libexec/keiland-printd`）

### 5.1 backend との約束（fd 3 の socketpair、1 行 1 命令、UTF-8、行は 1024 byte まで）

| 向き | 行 | 意味 |
| --- | --- | --- |
| backend → printd | `JOB <seq> <ipp\|lpd> <host> <port> <title>` + SCM_RIGHTS で fd 1 つ | 印刷。title は最後の項で空白を含みうる |
| printd → backend | `ACCEPTED <seq> <job>` / `REJECTED <seq> <detail>` | spool に複写できたか |
| printd → backend | `STATE <job> <sending\|done\|failed\|cancelled> <detail>` | job の状態 |
| backend → printd | `CANCEL <job>` | 送る前なら取り消し、送っている間なら接続を切る（IPP で job-id があれば Cancel-Job） |
| backend → printd | `NAME <seq> <host> <port>` | IPP の Get-Printer-Attributes で名前を問う |
| printd → backend | `NAMED <seq> <text>` / `NAMED <seq>`（取れない） | |

### 5.2 spool

- `$XDG_RUNTIME_DIR/keiland-print/`（0700）。JOB を受けたら fd から `job-<n>.pdf` に複写し（上限 256 MiB、先頭が `%PDF-`）、fd を閉じて ACCEPTED。app はその後 file を消してよい。
- spool は login の間だけ（runtime dir）。logout と再起動で未送の job は消える（段 1 の制限）。
- job は printer ごとに 1 つずつ順に、printer が違えば並行に（最大 4）送る。終わった file は消す。

### 5.3 IPP（RFC 8010・8011、IPP/2.0、平文の HTTP/1.1）

- 送る前に Get-Printer-Attributes（`printer-state`・`document-format-supported`・`printer-info`・`printer-make-and-model`）。`application/pdf` が無ければ failed（format）。resource は `/ipp/print`、404 か `client-error-not-found` なら `/ipp`、次に `/`。
- Print-Job: operation-attributes に `attributes-charset`=utf-8、`attributes-natural-language`=en、`printer-uri`=`ipp://host:port/path`、`requesting-user-name`（login 名）、`job-name`（title）、`document-format`=application/pdf。本体は HTTP の chunked で spool の file を流す。
- 応答の status が successful-ok 系なら job-id を取り、Get-Job-Attributes で `job-state` を 2 秒ごとに最大 10 分見る（completed→done、aborted→failed（printer）、canceled→cancelled）。printer が job-state を返さなければ受け付けで done とする。
- 失敗の語: 接続できない unreachable、HTTP の 4xx/5xx や IPP の client-error refused、server-error-busy は 30 秒後に 3 回まで再送の後 busy。
- ipps（TLS）は範囲の外（後の段。TLS の library の判断が要る）。

### 5.4 LPD（RFC 1179）

- TCP の port（既定 515）に接続。RFC は送り元の port を 721〜731 と定めるが、利用者の権限では使えないので普通の port で送る（それを拒む printer では失敗する、段 1 の制限）。
- `\x02lp\n` → 応答 0、control file（`H<host>`・`P<user>`・`J<title>`・`N<title>`・`ldfA<nnn><host>`・`UdfA...`）を `\x02<長さ> cfA<nnn><host>\n` → 0 → 本体 → `\0` → 0、data file を `\x03<長さ> dfA<nnn><host>\n` → 0 → spool の file → `\0` → 0。0 以外は refused。状態の問い合わせは無く、送り終えたら done。

### 5.5 その他

- 待ち受けの socket を持たない（外向きの接続だけ）。setuid でない。名前解決は `getaddrinfo`。
- 仕事（spool の job・送信・名前の問い合わせ）が無くなって 60 秒で終わる。fd 3 が EOF なら直ちに終わる（送信中の job は failed で捨てる）。
- log は syslog（job の番号・printer・結果。文書の中身と title は残さない）。

## 6. Settings の Printers の頁（p004）

- 今の stub（`se_soon_draw`）を、printer の一覧（名前・address・protocol・既定の印）と「Add Printer...」・選んだ printer の「Make Default」・「Remove」にする。Add の form は address・port（protocol で既定を入れる）・protocol（IPP / LPD の 2 択）と Add・Cancel。結果は語ごとの文（invalid の address、16 個まで、など）。
- 一覧の下に job の一覧（最後の 16、状態と失敗の語）と送る前・送る間の job の Cancel。
- 頁は `kl_system_printers_*` だけを使う（file を読まない）。

## 7. 試験（細かい修正ごとに回さない。WS の最後に T1）

| 層 | 試験 | 場所 |
| --- | --- | --- |
| printd の符号化 | host: IPP の要求の byte 列を RFC の例と比べる、応答の解析、LPD の control file | `plan/ws145/tests/host-ipp.c` など |
| printd の通し | host: Python の模擬の IPP server（Get-Printer-Attributes・Print-Job・Get-Job-Attributes、PDF を受けない・404・busy の printer を選べる）と模擬の LPD server に、printd を socketpair で動かして送る。受けた文書が spool の元と同じ（SHA-256） | `plan/ws145/tests/mock-ipp.py`・`mock-lpd.py`・`run-host-printd.sh` |
| backend | host: printers.conf の読み書き（壊れた行、16 個、既定の削除） | `run-host-print-backend.sh` |
| QEMU | T1: guest の Settings で printer（host の模擬の server、user-net の 10.0.2.2）を足し、`printtest` で PDF を印刷、模擬の server が受けた文書の SHA-256 と job の done、Printers の頁の PNG | `plan/ws145/tests/print-guest.sh` |
| 実機の printer | UAT（ユーザーの printer の機種で） | — |

## 8. Phase の改訂（ws.md の表へ）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | keiland-printd（約束の行・spool・IPP・LPD・終わり方）と host の通しの試験 | p001 |
| p003 | backend の print（設定の file・printd の起動と通信）、compositor の printers の object、protocol version 9、libkeiland の口（KL_VERSION +1）、`printtest` | p002 |
| p004 | Settings の Printers の頁 | p003 |
| p005 | 変換の filter（後） | p002 |
| p006 | 全文の規約の確認と回帰、T1 の QEMU の試験 | p002〜p004 |
| （p007 案） | PDF Viewer の File > Print（D6 の判断次第） | p003 |

## 9. 危険と制限

- 段 1 は平文の IPP と LPD だけ。printer が ipps だけを受けると使えない。
- LPD の特権の port を使わないので、それを要する古い printer では失敗する。
- spool は login の間だけ。
- PDF を直接受けない printer（多くの安価な laser・inkjet は PCL・PWG raster だけ）は p005 まで使えない。家庭の printer では IPP Everywhere の PWG raster が要ることが多いので、p005 の最初の候補は PDF → PWG raster（libpdf の CPU の rasterizer を使える）。
