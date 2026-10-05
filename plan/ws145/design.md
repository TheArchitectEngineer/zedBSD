<!-- awesome-plan project=zedbsd record=ws145-design -->

# WS145 の設計: 印刷（ws145-p001、第 2 版 2026-10-05 P2 g15、q754）

[WS145](ws.md) の単一目標「app が libkeiland に PDF の場所を渡すと network の printer（IPP か LPD）で印刷される。一覧と既定の printer を libkeiland から compositor 経由で取れ、Settings の Printers の頁で IP address・port・protocol を設定できる」の設計。ユーザーの指示（2026-10-04 夜、ws.md に原文）に従う。

第 2 版は初版への敵対的レビュー（[phase001](phase001/phase.md) に要旨）の重大 7・中 11・軽 8 の指摘を反映した。番号は拡張の protocol を **version 10**、libkeiland を **KL_VERSION 34** とする（version 9・KL 33 は ws132-p009 の媒体の情報が使った。merge の時に他の WS と重なれば Q1 が詰め直す）。

## 0. 判断の要点

| 番号 | 決めたこと（案） | 理由 | ユーザーの判断 |
| --- | --- | --- | --- |
| D1 | printer の daemon `keiland-printd` は**利用者の権限**で動く。compositor の libkeiland-backend が必要な時に起動し、仕事が無くなれば合意の手順（§5.1）で終わる。root の daemon・setuid・init の service（ws.md の範囲 3 が想定した「WS002 の service の仕組み」）を使わない。 | network の printer に送るだけなら特権が要らない。root の daemon の口を足さない。ユーザーの指示「libkeiland-backend がプリンタデーモンが未起動なら起動」に合う。ws.md の範囲 3 の記述はこの版に合わせて直す（Q1 に報告済み）。 | 要らない（特権の口を足さない） |
| D2 | printer の設定は**利用者ごと**（`~/.config/keiland/printers.conf`、書くのは compositor だけ）。 | 管理者の権限も root の書き込みも要らない。 | **要確認**: 複数の利用者で printer を共有したいか（共有なら system の設定の段を別に設計し、root の口の承認を取る） |
| D3 | 設定の項目は address・port・protocol だけ。IPP の resource の path は `/ipp/print` → `/ipp` → `/` の順に試し、通った path を設定に覚える。LPD の queue の名前は `lp`。名前は足した時に IPP で問い合わせた `printer-info`（無ければ `printer-make-and-model`、どちらも無ければ「address (IPP)」）を設定に覚える。 | 指示どおり 3 項目に保つ。多くの printer で通る既定を選ぶ（`/ipp/printer`・`/ipp/port1` などの機種は通らない）。 | **要確認（小）**: IPP の path と LPD の queue の名前を「詳しい設定」として入力できるようにするか |
| D4 | 文書は PDF だけ（段 1）。printer が PDF を受けなければ job は失敗（format）。 | 指示どおり。 | **要確認**: 受け入れの printer の機種。多くの家庭の printer は PDF を受けず PWG raster・PCL が要る。その機種なら p005 の PDF → PWG raster（libpdf の rasterizer を使える）を受け入れの前に入れる |
| D5 | Linux・FreeBSD の Keiland も同じ `keiland-printd`。CUPS は使わない（外部の package に依らない）。 | 3 つの OS で同じ code。 | **要確認（小）**: Linux・FreeBSD で CUPS に既にある printer を一覧に出すか（出すなら CUPS の IPP（localhost:631）を読む段を別に足す。CUPS の library は使わない） |
| D6 | app の Print の menu は範囲の外。WS145 は libkeiland の口と試験の client（`userland/tests/printtest`）まで。 | 単一目標は「libkeiland に渡すと印刷される」。 | **要確認（小）**: PDF Viewer の File > Print を WS145 に足すか（足すなら p007） |
| D7 | printer に利用者の login 名を送る（IPP の `requesting-user-name`、LPD の `P` と `H` の host 名）。 | printer の job の一覧で誰の job か分かる（IPP・LPD の通常）。 | **要確認（小）**: 送らない（固定の "kei"）方がよいか |
| D8 | spool の上限: 1 文書 256 MiB、合計 512 MiB、job 16 個（待ち・送信中の合計）。超えた依頼は busy で断る。 | spool は RAM の tmpfs（`/run/user/UID`）なので、上限なしでは利用者の client が RAM を使い切れる。 | **要確認（小）**: 上限の値 |

外部の実装は取り込まない（IPP・LPD の符号化は RFC から自分で書く）。HAL・UAPI・toolchain には触れない。zedBSD の kernel には IPv6 が無い（`include/uapi/socket.h`、EAFNOSUPPORT）ので、address は IPv4 の literal と host 名（DNS で引ける物）に限る。

## 1. 全体の流れ

```
app ──kl_system_printers_print(system, printer, path, title, &request)──▶ libkeiland
       libkeiland: path を O_RDONLY|O_NONBLOCK|O_NOCTTY|O_CLOEXEC で開く → fstat で S_ISREG と大きさ → pread で先頭 1024 byte に "%PDF-"
       → title を検める → fd を送る（libwayland は 'h' を dup するので marshal の後に自分の fd を閉じる）
libkeiland ──kl_system_printers_v1.print(request, printer, title, fd)──▶ compositor（system.c）
       compositor: 検証の前に必ず zwl_take_fd で fd を取る。不正なら fd を閉じて result（INVALID）
       → kl_backend_print_submit（成否に関わらず fd の所有を受け取る）
backend（userland/desktop/libkeiland-backend/print/、OS に依らない）: job の ID を振る → queued(request, job) と job の一覧の変化
       → printd が無ければ起動 → "JOB ..." + fd（SCM_RIGHTS、nonblocking、MSG_NOSIGNAL）
printd: fstat・pread で検めて spool に複写 → "ACCEPTED job" → IPP / LPD で送る → "STATE job ..."
backend ──job の状態──▶ compositor ──job・done──▶ 全ての printers の object
```

- app は OS の口も daemon への接続も持たない（Guardrail「app と設定」）。libkeiland は file を開いて fd を作るだけ。
- path でなく fd を compositor に渡す: app が読める file だけが印刷され、compositor が app の相対 path を解いたり file を読んだりしない。app の API は指示どおり path を受ける。
- **fd の所有**（レビュー S6）: compositor が `zwl_take_fd` で取った fd は、`kl_backend_print_submit` を呼んだ時点で成否に関わらず backend の物（compositor は以後触らない）。backend は ACCEPTED・REJECTED を受けるか、依頼を諦めるまで fd を持ち、その時に閉じる。backend が同時に持つ fd は 16 まで（超えれば BUSY）。compositor の他の fd（DRM・seat・sessiond・client の socket）を printd に渡さない（番号の再利用を避けるため、backend は自分の持つ fd の表の番号だけを送る）。

## 2. libkeiland の口（KL_VERSION 34、`<keiland.h>`）

既存の kl_system_* と同じ形: 状態は get、変化は `kl_system_dispatch` の changed の bit、依頼は request の番号と `kl_system_take_result` の errno。

```c
#define KL_SYSTEM_HAS_PRINTERS      0x200U   /* kl_system_printers_*（KL_VERSION 34） */
#define KL_SYSTEM_CHANGED_PRINTERS  0x100U   /* printer・job の一覧 */
#define KL_PRINTER_IPP              1U
#define KL_PRINTER_LPD              2U
#define KL_PRINTER_DEFAULT          0x1U     /* flags */
#define KL_PRINTERS_MAX             16U
#define KL_PRINT_JOBS_MAX           32U      /* 待ち・送信中 16 と、終わった最後の 16 */
#define KL_PRINTER_HOST_MAX         64U
#define KL_PRINTER_NAME_MAX         128U
#define KL_PRINT_TITLE_MAX          128U
#define KL_PRINT_DETAIL_MAX         32U

struct kl_printer {
	uint32_t id;                        /* backend が振る（1 から、設定の file に残る） */
	unsigned protocol;                  /* KL_PRINTER_IPP / _LPD */
	char host[KL_PRINTER_HOST_MAX];     /* IPv4 の literal か host 名 */
	unsigned port;                      /* 1〜65535（IPP 631、LPD 515 が既定） */
	char name[KL_PRINTER_NAME_MAX];
	unsigned flags;
};

/* job の状態 */
#define KL_PRINT_QUEUED     1U   /* 受け付けた、送る前 */
#define KL_PRINT_SENDING    2U
#define KL_PRINT_WAITING    3U   /* printer が受け取り、処理を待つ・処理中（IPP の pending・processing） */
#define KL_PRINT_DONE       4U
#define KL_PRINT_FAILED     5U
#define KL_PRINT_CANCELLED  6U

struct kl_print_job {
	uint32_t job;                       /* backend が振る（compositor の寿命の間一意） */
	uint32_t printer;
	unsigned state;
	char title[KL_PRINT_TITLE_MAX];
	char detail[KL_PRINT_DETAIL_MAX];   /* §5.6 の語の一つ、または "" */
};

size_t kl_system_printers_get(const struct kl_system *system, struct kl_printer *printers, size_t capacity);
size_t kl_system_print_jobs_get(const struct kl_system *system, struct kl_print_job *jobs, size_t capacity);
int kl_system_printers_add(struct kl_system *system, unsigned protocol, const char *host, unsigned port, uint32_t *request);
int kl_system_printers_remove(struct kl_system *system, uint32_t printer, uint32_t *request);
int kl_system_printers_set_default(struct kl_system *system, uint32_t printer, uint32_t *request);
int kl_system_printers_print(struct kl_system *system, uint32_t printer, const char *path, const char *title, uint32_t *request);
int kl_system_print_job_of(const struct kl_system *system, uint32_t request, uint32_t *job);
int kl_system_print_cancel(struct kl_system *system, uint32_t job, uint32_t *request);
```

- `printer` に 0 を渡すと既定の printer（無ければ result が EINVAL）。
- `print` の libkeiland の中の検め: 開けない（errno のまま）、正規の file でない（EINVAL）、大きさ 0 か 256 MiB 超（EFBIG）、先頭 1024 byte に `%PDF-` が無い（EINVAL）。title は UTF-8 として正しく、C0 の制御文字と DEL を含まず、127 byte 以内（文字の境界で切る）。外れれば EINVAL（libkeiland は直さずに断る。compositor と printd も同じ規則で検める）。
- `kl_system_print_job_of`: print の result の前に compositor が送る `queued(request, job)` を覚えておき、依頼した app が自分の job の ID を知る（`kl_system_devices_busy_program` と同じ形）。1 で job、0 で無し。
- result の errno: 0、EINVAL（INVALID）、EBUSY（BUSY: 上限）、ENOTSUP（UNSUPPORTED）、EIO（FAILED）。

## 3. 拡張の protocol（kl_system_manager_v1 version 10）

```
kl_system_manager_v1
  request 10 get_printers(new_id kl_system_printers_v1)      since version 10 (ws145)
  capabilities の bit KL_SYSTEM_CAPABILITY_PRINTERS 0x200（login・lock の画面の compositor は出さない）

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
  event   3 queued(uint request, uint job)    before print's result, when the job was taken
  event   4 result(uint request, uint applied, uint saved)    applied is a KL_SYSTEM_RESULT_* number
```

- 作られた時に全ての printer と job と done。変化のたびに全体と done（devices と同じ）。
- 利用者ごとの設定なので、その compositor の client なら誰でも add・remove・set_default・print・cancel ができる。
- compositor の検め（protocol の入口）: protocol は 1・2、host は `[A-Za-z0-9.-]` で 1〜63 byte（IPv4 の literal を含む）、port は 1〜65535、title は §2 の規則（`SYSTEM_WIRE_TEXT_MAX` を超える string は従来どおり protocol の error）、printer・job は在る物。外れれば fd を閉じて result INVALID（protocol の error にしない）。
- 定数は `kl-system-protocol.h` に `KL_SYSTEM_PRINTER_IPP`・`_LPD`、`KL_SYSTEM_PRINT_*` の状態、`KL_SYSTEM_SINCE_PRINTERS 10` を置く（libkeiland の `KL_PRINTER_*` と同じ値）。
- 古い組み合わせ: version 9 以前の compositor では libkeiland の capability に PRINTERS が無く、全ての call が ENOTSUP。古い libkeiland（version 9）は get_printers を送らない。
- printers の object は他の object と同じく `kl_system_open` で作られる（全ての app に 1 つずつ。object は状態を送るだけで、printd も問い合わせも起こさない）。

## 4. compositor と libkeiland-backend

- compositor（`wayland/system.c` に printers の object）: 要求を backend に渡し、backend の changed の bit で全ての printers の object に state と done、依頼した object に queued と result。
- **backend の口**（volumes と同じ型、`keiland-backend.h`）:
  ```c
  struct kl_backend_print *kl_backend_print_open(const char *config_path, const char *runtime_dir);
  void kl_backend_print_close(struct kl_backend_print *print);
  int  kl_backend_print_fd(const struct kl_backend_print *print);        /* printd の socket、-1 なら無し */
  unsigned kl_backend_print_update(struct kl_backend_print *print);      /* poll の後に呼ぶ、changed の bit */
  size_t kl_backend_print_printers(const struct kl_backend_print *print, struct kl_backend_printer *list, size_t capacity);
  size_t kl_backend_print_jobs(const struct kl_backend_print *print, struct kl_backend_print_job *list, size_t capacity);
  int  kl_backend_print_add(struct kl_backend_print *print, uint32_t request, unsigned protocol, const char *host, unsigned port);
  int  kl_backend_print_remove(struct kl_backend_print *print, uint32_t request, uint32_t printer);
  int  kl_backend_print_set_default(struct kl_backend_print *print, uint32_t request, uint32_t printer);
  int  kl_backend_print_submit(struct kl_backend_print *print, uint32_t request, uint32_t printer, const char *title, int fd, uint32_t *job);
  int  kl_backend_print_cancel(struct kl_backend_print *print, uint32_t request, uint32_t job);
  int  kl_backend_print_take_result(struct kl_backend_print *print, uint32_t *request, unsigned *applied);
  ```
  `config_path` は `~/.config/keiland/printers.conf`、`runtime_dir` は `XDG_RUNTIME_DIR`（printd に渡す）。
- **設定の file**（backend の共通の code）: 読みは open の時。書きは settings-store と同じく writer の thread と lock（event loop で disk を待たない、`wayland/system.c` の方針）で、一時 file から rename、`flock` で同じ利用者の別の session と重ならない。形:
  ```
  # Keiland printers
  printer 1 ipp 192.168.1.20 631 /ipp/print Office-Printer
  printer 2 lpd 192.168.1.30 515 lp 192.168.1.30 (LPD)
  default 1
  ```
  （id・protocol・host・port・IPP の path か LPD の queue・名前（行の残り、§2 の title と同じ規則））。壊れた行は飛ばす。最初に足した printer は既定になる。既定を消すと残りの最小の id が既定になる。
- **printd の起動**: `posix_spawn`（compositor は複数の thread の process なので fork の後の処理を避ける）で `KEILAND_LIBEXECDIR "/keiland-printd"` を、socketpair の片方を **fd 3** に dup して起動する（`posix_spawn_file_actions_adddup2`。他の fd は全て CLOEXEC であることを前提にせず、spawn の attribute で signal の mask と disposition を既定に戻す）。終わりは SIGCHLD でなく socket の EOF で知り、`waitpid(pid, WNOHANG)` で回収する。起動に失敗し続ける時は 10 秒に 3 回まで、その後 60 秒は起動せず、その間の依頼は failed（daemon）。
- **job の状態遷移**（backend が持つ。レビュー S4・M8）:

  | 今 | 出来事 | 次 | 備考 |
  | --- | --- | --- | --- |
  | （無し） | print を受けた | QUEUED | job の ID を振り queued(request, job) と result OK。fd を持つ |
  | QUEUED | printd へ JOB を送れた | QUEUED（printd 待ち） | fd は ACCEPTED まで持つ |
  | QUEUED | ACCEPTED | QUEUED（spool） | fd を閉じる |
  | QUEUED | REJECTED detail | FAILED detail | fd を閉じる |
  | QUEUED | printd の EOF（ACCEPTED 前） | QUEUED | 新しい printd に JOB を送り直す（1 job 2 回まで、超えたら FAILED daemon） |
  | QUEUED・SENDING・WAITING | printd の EOF（ACCEPTED 後） | FAILED daemon | spool の file は新しい printd が起動の時に消す |
  | QUEUED | cancel | CANCELLED | ACCEPTED 前なら fd を閉じ、後なら CANCEL を送る |
  | SENDING・WAITING | cancel | CANCELLED（printd の STATE で確定） | printd が LPD は abort、IPP は Cancel-Job |
  | DONE・FAILED・CANCELLED | cancel | 同じ | result INVALID |
  | 任意 | その printer を remove | 待ち・送信中の job は CANCELLED | remove は result OK |

  待ち・送信中は 16 個まで、終わった job は最後の 16 個を一覧に残す（古い物から消す）。
- OS ごとの tree（zedbsd・linux・freebsd）には何も足さない。

## 5. keiland-printd（`userland/desktop/printd/`、`/usr/libexec/keiland-printd`）

### 5.1 backend との約束（fd 3 の socketpair、1 行 1 命令、UTF-8、行は 1024 byte まで）

| 向き | 行 | 意味 |
| --- | --- | --- |
| backend → printd | `JOB <job> <ipp\|lpd> <host> <port> <path-or-queue> <title>` + SCM_RIGHTS で fd 1 つ | job の ID は backend の物。title は最後の項（空白を含みうる、空でもよい） |
| printd → backend | `ACCEPTED <job>` / `REJECTED <job> <detail>` | spool に複写できたか |
| printd → backend | `STATE <job> <sending\|waiting\|done\|failed\|cancelled> <detail>` | |
| backend → printd | `CANCEL <job>` | |
| backend → printd | `NAME <seq> <host> <port>` | 足した時だけ。IPP の Get-Printer-Attributes で名前と path を問う |
| printd → backend | `NAMED <seq> <path> <text>` / `NAMED <seq>`（取れない） | |
| printd → backend | `IDLE` | 仕事が無くなって 60 秒 |
| backend → printd | `BYE` | IDLE の後、backend が JOB・NAME を送っていなければ。printd は BYE を受けてから終わる。BYE の前に JOB が来たら IDLE を取り消す |

- 両側とも、行の各項に C0 の制御文字・DEL が無く、host・path・queue に空白が無いことを検め、外れた行は捨てて log に残す（backend が printd の行を信じ切らない、printd も backend の行を検める）。printer から来た text（名前）は printd が §2 の規則で直して（制御文字は空白に、UTF-8 の不正な byte は落とし、文字の境界で切る）から NAMED に入れる。
- socket は両側とも nonblocking。送れなかった行は送信の queue に置く。backend の送信は `MSG_DONTWAIT | MSG_NOSIGNAL`（compositor は SIGPIPE を無視していない）。

### 5.2 spool

- `$XDG_RUNTIME_DIR/keiland-print/`。起動の時、`XDG_RUNTIME_DIR` が無い・利用者の物でない・077 が 0 でない時は起動を断る（終了の status で。backend は以後の依頼を failed（daemon））。dir は 0700 で作り、起動の時に中の残り（前の printd の file）を消す。
- JOB を受けたら fd を fstat して S_ISREG・大きさ（0 と 256 MiB 超は REJECTED toobig）、合計 512 MiB と 16 job の上限（超えれば REJECTED busy）、`job-<job>.pdf` を `O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY`、0600 で開き、`pread` で offset 0 から 64 KiB ずつ複写する（複写の間も poll の loop に戻る）。先頭 1024 byte に `%PDF-` が無ければ REJECTED format。終われば fd を閉じて ACCEPTED。
- spool は RAM（zedBSD の `/run` は kernel の tmpfs、`src/kern/vfs.c`）。sessiond は logout の時に `/run/user/UID` を消さないので、printd が終わる時（BYE・EOF）と起動の時に spool を空にする。未送の job は logout で失われる（段 1 の制限）。

### 5.3 printd の内部

- 1 つの thread の poll の loop。connect は nonblocking、送信は分けて書く。名前解決は blocking の `getaddrinfo` を避けるため、host が IPv4 の literal ならそのまま、host 名なら名前解決を 1 つの補助の thread で行う（結果を pipe で loop に返す）。
- 同時に送るのは printer ごとに 1 つ、全体で 4 つ。
- network から来る応答の上限: HTTP の状態の行・header の行は 1024 byte、header の合計 16 KiB、応答の本体 64 KiB、IPP の attribute は 256 個・各値 1024 byte。超えれば failed（protocol）。HTTP の応答は Content-Length か chunked を解く（応答の解析の fuzz を host 試験に入れる）。
- 接続 10 秒、応答 60 秒の timeout（timeout）。

### 5.4 IPP（RFC 8010・8011、平文の HTTP/1.1）

| 項目 | 値 |
| --- | --- |
| operation | Print-Job 0x0002、Cancel-Job 0x0008、Get-Job-Attributes 0x0009、Get-Printer-Attributes 0x000B |
| version | 2.0 で送り、`server-error-version-not-supported`（0x0503）なら 1.1 で送り直す |
| tag | operation-attributes 0x01、job-attributes 0x02、end-of-attributes 0x03、printer-attributes 0x04 |
| value tag | integer 0x21、enum 0x23、uri 0x45、charset 0x47、naturalLanguage 0x48、mimeMediaType 0x49、keyword 0x44、nameWithoutLanguage 0x42、textWithoutLanguage 0x41 |
| 属性（順） | `attributes-charset`=utf-8、`attributes-natural-language`=en、`printer-uri`=`ipp://host:port/path`、`requesting-user-name`（login 名）、`job-name`（title、空なら "Document"）、`document-format`=application/pdf（Print-Job）、`requested-attributes`（Get-*） |

- HTTP の本体は IPP の message（attribute の群と end-of-attributes）に続けて文書。`Content-Type: application/ipp`、大きさが分かるので `Content-Length`（chunked は使わない）。
- 送る前に Get-Printer-Attributes（`printer-state`・`document-format-supported`・`printer-info`・`printer-make-and-model`）。`application/pdf` が無ければ failed（format）。`printer-state` が stopped（5）でも送る（printer が受け付けて待つ）。path は設定に覚えた物を使い、HTTP 404 か `client-error-not-found` なら §0 D3 の順に試す（NAME の時に決まっていれば試さない）。
- 応答の status: successful-ok 系（0x0000〜0x00FF）なら job-id を取り WAITING。`client-error-document-format-not-supported`（0x040A）は format、他の client-error は refused、`server-error-busy`（0x0507）は 30 秒後に 3 回まで送り直して busy、HTTP 401・403 は auth、426（TLS が要る）は tls、他の HTTP の 4xx・5xx は refused。
- その後 Get-Job-Attributes で `job-state` を 5 秒ごとに見る: pending（3）・processing（5）は WAITING、pending-held（4）・processing-stopped（6）も WAITING（detail は held・stopped、紙切れなど）、canceled（7）は CANCELLED、aborted（8）は FAILED printer、completed（9）は DONE。30 分たっても終わらなければ DONE（detail unconfirmed）にして見るのをやめる。printer が job-state を返さなければ受け付けで DONE（detail unconfirmed）。
- ipps（TLS）は範囲の外（後の段。TLS の library の判断が要る）。

### 5.5 LPD（RFC 1179）

- TCP の port（既定 515）に接続。RFC は送り元の port を 721〜731 と定めるが、利用者の権限では使えないので普通の port で送る（それを拒む printer では refused、段 1 の制限）。
- `\x02<queue>\n` → 応答 0 → control file を `\x02<長さ> cfA<nnn><host>\n` → 0 → 本体 → `\0` → 0 → data file を `\x03<長さ> dfA<nnn><host>\n` → 0 → spool の file → `\0` → 0。`<nnn>` は job の ID の 1000 の剰余の 3 桁。`<host>` は送り手の host 名（31 byte まで）。0 以外の応答は refused。
- control file の行: `H<host>`（31 byte まで）、`P<user>`（31 byte まで）、`J<title>`（99 byte まで）、`N<title>`（131 byte まで）、`ldfA<nnn><host>`、`UdfA<nnn><host>`。title は §2 の規則で検めた後、`\n` を含みえない。
- 送信の途中の取り消し: subcommand `\x01\n`（abort job）を送ってから切る。送り終えた後の取り消しは `\x05<queue> <user> <nnn>\n`（remove jobs）を別の接続で送る（効かない printer もある）。
- 状態の問い合わせ（03・04）は使わず、送り終えたら DONE。

### 5.6 その他

- 待ち受けの socket を持たない（外向きの接続だけ）。setuid でない。
- detail の語（固定の一覧、Settings の文と試験の照合に使う）: `unreachable`・`refused`・`format`・`toobig`・`busy`・`auth`・`tls`・`timeout`・`protocol`・`printer`・`held`・`stopped`・`daemon`・`io`・`unconfirmed`・`cancelled`。
- log は syslog: job の ID・printer の id・protocol・結果の語。printer の address・文書の中身・title は残さない。

## 6. Settings の Printers の頁（p004）

- 今の stub（`se_soon_draw`）を、printer の一覧（名前・address・protocol・既定の印）と「Add Printer...」・選んだ printer の「Make Default」・「Remove」にする。Add の form は address・port（protocol で既定を入れる）・protocol（IPP / LPD の 2 択）と Add・Cancel。結果は語ごとの文。
- 一覧の下に job の一覧（状態と detail の文）と、終わっていない job の Cancel。
- 頁は `kl_system_printers_*` だけを使う（file を読まない）。

## 7. 試験（細かい修正ごとに回さない。WS の最後に T1）

| 層 | 試験 | 場所 |
| --- | --- | --- |
| 符号化 | host: printd の IPP の要求を独立の decoder（Python）で解いて属性と順を比べる、応答の解析（壊れた長さ・上限の超過の fuzz）、LPD の control file（長さの切り詰め、制御文字の拒否） | `plan/ws145/tests/host-ipp.c`・`ipp-decode.py` |
| printd の通し | host: Python の模擬の IPP server（高い番号の port。Get-Printer-Attributes・Print-Job・Get-Job-Attributes。PDF を受けない・404・busy・1.1 だけ・426・chunked を拒む・processing-stopped が続く・改行を含む printer-info の printer を選べる）と模擬の LPD server（abort を確かめる）に、printd を socketpair で動かして送る。受けた文書が元と同じ（SHA-256） | `plan/ws145/tests/mock-ipp.py`・`mock-lpd.py`・`run-host-printd.sh` |
| printd の寿命 | host: printd の kill（ACCEPTED の前・後）、IDLE と JOB の行き違い、起動し直した後の spool の掃除、合計と数の上限、FIFO・socket の fd、offset を進めた fd、XDG_RUNTIME_DIR の権限 | 同上 |
| backend | host: printers.conf の読み書き（壊れた行、16 個、既定の削除）、状態遷移の表の各行、fd の数が依頼の前後で変わらない（漏れ）、printd が死んだ後の送信で SIGPIPE が起きない | `run-host-print-backend.sh` |
| protocol | host: capability と CHANGED の bit が他と重ならない、却下の経路で `zwl_take_fd` と close、version 9 の compositor と新しい libkeiland（ENOTSUP）（`plan/ws131/tests/host-system.sh` の形） | `plan/ws145/tests/host-system-printers.c` |
| QEMU | T1: guest の Settings で printer（host の模擬の server、user-net の 10.0.2.2 の高い番号の port）を足し、`printtest` で PDF を印刷、模擬の server が受けた文書の SHA-256 と job の DONE、Printers の頁の PNG | `plan/ws145/tests/print-guest.sh` |
| Linux | T1・T2: Debian の QEMU+KVM の guest で Keiland の printd と printtest（同じ模擬の server） | p006 |
| 実機の printer | UAT（D4 の機種で） | — |

## 8. Phase の改訂（ws.md の表へ）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | keiland-printd（約束の行・spool・IPP・LPD・寿命）と host の符号化・通し・寿命の試験 | p001 |
| p003 | backend の print（設定の file・printd の起動と通信・状態遷移）、compositor の printers の object、protocol version 10、libkeiland の口（KL_VERSION 34）、`printtest`、host の backend と protocol の試験 | p002 |
| p004 | Settings の Printers の頁 | p003 |
| p005 | Linux・FreeBSD の build と install（Makefile.linux・freebsd、`keiland-linux.mk`・`keiland-freebsd.mk`、`KEILAND_LIBEXECDIR` が `/opt/keiland` の時）と Debian の QEMU+KVM の確認 | p003 |
| p006 | 全文の規約の確認と回帰、T1 の QEMU の試験（最後） | p002〜p005 |
| （別の WS の案） | 変換の filter（PDF → PWG raster・PostScript・PCL）。単一目標の外なので新しい WS（D4 の機種が PDF を受けなければ、受け入れの前に必要） | p002 |
| （p007 案） | PDF Viewer の File > Print（D6 の判断次第） | p003 |

## 9. 危険と制限

- 段 1 は平文の IPP と LPD だけ。printer が ipps だけを受けると使えない。
- LPD の特権の port を使わないので、それを要する古い printer では失敗する。
- spool は login の間だけ。
- PDF を直接受けない printer は filter の WS まで使えない。
- IPP の path が `/ipp/print`・`/ipp`・`/` 以外の printer は、D3 の「詳しい設定」が無ければ使えない。
- zedBSD では IPv6 の printer に送れない（kernel に IPv6 が無い）。
