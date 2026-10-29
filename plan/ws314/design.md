<!-- awesome-plan project=zedbsd record=ws314-design -->

# WS314 設計: post・`kl_process`・`kl_launch`・`kl_instance` の event・libbrowser の network の thread

- 親: [WS314](ws.md)（ws314-p003、2026-10-30 P2）。ユーザーの決定は ws.md の 1〜10 の原文。
- 目的: app と libkeiland の公開の API から fd・pipe・socket・fork・poll を外す（決定 1〜6）。Windows に移せる形（決定 1）。
- 状態: 案（design-reviewer の指摘 23 件を反映、§10）。§9 の「ユーザーの判断」は Q1 経由で問い合わせ中。

## 0. 要約

| 今の仕組み（app の側） | 置き換え | Phase |
| --- | --- | --- |
| Mailer の sync の thread → 窓の pipe（done）と `kl_app_watch_fd` | `kl_app_post`（他の thread から app の queue へ） | p004 |
| （無い）窓への post | `kl_window_post` を他の thread からも呼べる形に | p004 |
| Installer の `fork`・`pipe`・`execv`・`waitpid` と stdout の watch | `kl_process_*`（stdin へ行、stdout から行、終了の code） | p005 |
| Terminal の `forkpty`・`TIOCSWINSZ`・`tcgetattr`・`kill`・`waitpid`・master の watch | `kl_process_*` の terminal の mode | p005 |
| Files・Settings・PDF Viewer・Terminal の新しい窓の `fork`・`posix_spawn`・`waitpid` | `kl_launch`（切り離した起動） | p005（§12 で分ける案） |
| Phone の viewer の `posix_spawn`・`waitpid(pid)`（最大 16） | stream 無しの `kl_process`（終了の event で数を保つ） | 同 |
| Preview の API の fd・pid（`preview_start(path, fd, ...)`、`struct preview_job` の `pid_t`）と `client.c` の `waitpid`・`kill` | Preview の API を path と不透明な job に、`waitpid`・`kill` を Preview の platform の file へ（sandbox の起動は Preview の物のまま） | 同 |
| Settings の `kl_instance_fd` と watch | `kl_instance_bind` と `KL_APP_INSTANCE` の event | p006 |
| Browser の shell の `browser_view_poll_fds`・`pollfd`・watch | libbrowser の network の thread と wake の callback、`browser_view_process(view)` | p007 |
| `kl_app_watch_fd`・`KL_APP_FD*`・`kl_app_event.fd`・`kl_instance_fd` | 公開の API から外す | p008 |

## 1. 棚卸し（2026-10-30、worktree の acd1b807c で実測）

path は `userland/desktop/` から。

- **libkeiland**: `ui/app.c` の `kl_app_dispatch`（:185）が wayland の fd と watch の表（最大 64）を 1 回の `poll`（:242）で待つ。
  `KL_APP_FD` の event は `app_fd_events`（:653）。app の queue は 512 の ring（`ui/window.h` の `KEIUI_APP_EVENTS`）、満ちると入力を捨てる（`app_slot`）。
  header の約束は「全ての呼び出しは app を開いた 1 つの thread から」。他の thread からの post・起こしは無い。
  `kl_window_post`（`ui/window.c:501`）は同じ thread で入力の queue に `KL_WINDOW_POST` を積むだけ。**呼び手は 0**（libkeiland の中にも app にも無い）。
  `instance.c` は `$XDG_RUNTIME_DIR/keiland-NAME.instance` の UNIX socket（listen・accept、client は connect して 2 行を書く）。
- **Mailer**: `sync.c` が pipe 2 本（窓 → thread の wake、thread → 窓の done）と sync の thread を作る。thread は IMAP の socket（IDLE）と wake を
  `poll`（:460）で待つ。結果は mutex の list、done に 1 byte。窓は `ml_sync_fd()` を watch（`main.c:881`）し `ml_sync_take` で list を取る。
  network は `conn.c`（`getaddrinfo`・`socket`・非 blocking の `connect`・`poll`）と `tls.c`、全て sync の thread の上。
- **Installer**: `helper.c` が `pipe` 2 本・`fork`・`dup2`・`closefrom(3)`・`execv("/usr/libexec/install-admin", probe|session)`。stdout を byte で読み
  行に切り（4 KB、長すぎる行は丸ごと捨てる、`helper.c:328-340`）、要求は stdin へ行で書く（probe の stdin は受け継ぐ）。session は stdin を
  閉じて終える。HANGUP の後に blocking の `waitpid`、wait の status を `inst_flow_ended` へ（画面に「(%d)」で出る）。install-admin は setuid root（mode 4550）で呼び手（wheel か autologin の利用者）を確かめ、
  `setresuid(0)`・`setsid` し SIGTERM などを無視する（親は止められない）。中の `admin/` は root の別 program で範囲の外。
- **Terminal**: `main.c:908` の `forkpty` → 子で `setenv("TERM","xterm")`・`execv("/bin/sh", sh [-c cmd])`。大きさは `TIOCSWINSZ`（:974、
  起動の後・窓の大きさ・font の変更）。出力は byte（8 KB、1 回の loop で最大 16 回）。入力は非 blocking の `write`、EAGAIN の残りは次の loop。
  `tcgetattr`（:1017）で ECHO 無し・ICANON 有りを見て password の入力と判じ IME を切る。tab を閉じる時は master を閉じ `kill(SIGHUP)`・`waitpid`。
  新しい窓は二重の fork で `execv(program, --token=X-new [--display=D])`。
- **Settings**: `kl_instance_open("settings", page)`、`kl_instance_fd` を watch、loop ごとに `kl_instance_take` → page を開き `kl_activate`。
  `posix_spawn(BINDIR"/files")` は刈り取らない（zombie が残る）。
- **Files**: `apps.c` の二重の fork 2 つ。`apps_run` は `setsid`・stdio を /dev/null・`/bin/sh -c 展開した command`（`@terminal ` の opener は
  `terminal --command=`）、`apps_exec` は `setsid`・stdio を保つ・`execv(argv)`（新しい Files の窓、Settings の壁紙の page）。
- **PDF Viewer**: `posix_spawn(BINDIR"/notes", doc)`、loop で `waitpid(-1, WNOHANG)`（`pdfviewer/main.c:569`）。**Phone**:
  `posix_spawn(imageview|videoplayer, path)`、pid を 16 まで持ち `waitpid(pid, WNOHANG)`、16 を超えると利用者に「閉じてから」と告げる（`phone/main.c:1532-1546`）。
- **Preview**: 起動は OS ごと（`preview/linux/spawn.c` の fork・execve、`preview/zedbsd/spawn.c` の `sandbox_spawn`（資源の上限つき）、
  `preview/freebsd/`）。`client.c:114` の `preview_poll` が `waitpid(WNOHANG)`、時間切れで `kill(SIGKILL)`。**API が app に fd と pid を見せる**:
  `preview_start(const char *input, int output, ...)`（Files は `files/thumb.c:527-534` で一時 file を開いて fd を渡す）、`struct preview_job` の
  `pid_t pid`、`struct preview_pending` の `int output`。`make.c:244` の socket は sandbox の自己試験（実の I/O ではない）。
- **Browser**: shell（`shell/shell.c:314`・`shell/window.c`）が `browser_view_poll_fds` の `pollfd`（最大 256）を `kl_app_watch_fd` に写し、
  64 を超える分は 0 秒の `poll` と 10 ms の上限で見る。libbrowser の中: DNS の thread 1 本（`net/loader.c:276`、pipe で起こす）、それ以外の
  socket・TLS・HTTP は呼び手の thread で非 blocking。libmedia の engine の thread は pipe で起こす（`media_engine_wake_fd`、使うのは libbrowser だけ）。
  同期の取得（`net/http.c` の `net_http_fetch`、`page/link.c`・`page/script.c` から）と headless の `view_settle_network` は libbrowser の中で `poll`。
- 範囲の外（fd を公開しない app の thread）: settings の look・storage、music・videoplayer・photos の worker は flag と dispatch の時間切れで拾う。
  ime は kl_app を使わない自前の loop（pipe と `sigaction`）。どちらもこの WS では変えない（§11）。
- app の source に残る UNIX の物（この設計の後も）: Mailer の network の層（`conn.c`・`tls.c`・`sync.c` の socket・`poll`・wake の pipe）、
  app の worker の pthread、file の I/O（`open`・`read`）。境界は §9 の D でユーザーに確かめる。

## 2. 置き場: platform の層

- **公開の API**（`include/keiland/`）は fd・pid・signal・`pollfd`・UNIX の path の約束を持たない。process は `struct kl_process *`、終わり方は
  `KL_PROCESS_*` の値、環境は文字列の配列。
- **実装**は libkeiland の中の platform の file に閉じる: `libkeiland/platform/`（新）。
  - `platform.h`（内部）: 起こし（wake）、thread の作成と名前、mutex、process の起動・待ち・停止、pty、切り離した起動。
  - `platform-posix.c`: zedBSD・Linux・FreeBSD の共通（POSIX）。OS の違いは §8 の表の数か所を `#if` で。
  - Windows に移す時は `platform-win32.c` を足す（§8 の写像）。ただし libkeiland の他の部分（wayland の client、`poll` の dispatch）も
    Windows では別の実装が要る。この WS が保証するのは**公開の API と app の source が Windows に移せる形**であること。
- **libkeiland-backend-* に置かない理由**: `libkeiland-backend/keiland-backend.h` は「compositor だけが呼び、app は見ない」静的 library で、
  compositor の callback（`struct kl_backend_host`）に結ばれている。app が link する `libkeiland.so` に入れると compositor の部品が app に入る。
  よって compositor・`libkeiland-backend-*`・sessiond は変えない（Q1 への所有の追加は不要）。
- libbrowser・Mailer・Preview は自分の network・sandbox の層を持つ（Keiland に依存しない library の中の platform の部分）。
  app の窓の側（Keiland の shell）はそれらの fd を見ない。
- host の試験は wayland の server を最小に立てる（`libwayland-server` で `wl_compositor` と `xdg_wm_base` の global を名前だけ出す）ので、
  post の list を wayland から分けた file にしなくても `kl_app_open` から通しで試せる（§12、p004 で実施）。

## 3. `kl_app_post` と `kl_window_post`（p004、KL_VERSION 97）

### 3.1 API（`keiland_app.h`・`keiland_window.h`）

```c
#define KL_APP_POST		4U
#define KL_APP_PROCESS		5U	/* §4（field は p004 で一度に足す） */
#define KL_APP_INSTANCE	6U	/* §6 */

struct kl_app_event {
	unsigned kind;
	struct kl_window *window;
	struct kl_window_event input;
	int fd;				/* p008 で外す */
	unsigned ready;
	uint32_t code;			/* KL_APP_POST */
	void *data;			/* KL_APP_POST */
	struct kl_process *process;	/* KL_APP_PROCESS */
	struct kl_instance *instance;	/* KL_APP_INSTANCE */
};

int kl_app_post(struct kl_app *app, uint32_t code, void *data);
int kl_window_post(struct kl_window *window, uint32_t code, void *data);	/* 意味の変更は §9 B */
```

`struct kl_window_event` に `void *data`（`KL_WINDOW_POST` の data）を足す。`kl_app_event` の field は p004 で全部足し、大きさの変更を 1 回にする
（review 21）。約束の文は header の comment（p004 で実装済み）。要点:

- 他の thread から呼べる。app の thread からは、list に先の post が無く queue に空きがあれば即座に積む（窓宛てなら窓の入力の中の順を保つ）。
  先の post があればその後ろへ（追い越さない、review 20）。
- 1 つの thread の post は順に届き、捨てない（queue に入らない分は次の dispatch まで list で待つ）。
- 窓宛ての post が、届く前に窓が閉じた・queue の中で窓が閉じた場合は `KL_APP_POST`（window NULL、同じ code と data）で届く。
- 作成中の窓（`app_quiet`）への post は起こらない（窓の pointer は作成が終わるまで program に渡らない）。
- `kl_app_close` の約束: post する thread を先に止める。閉じた時に残る post は捨てる（data も解放しない）。閉じた後の post は未定義。

### 3.2 実装（p004、`ui/app.c`・`ui/window.c`・`platform/`）

- `struct kl_app`: post の list（`malloc` の node、node は `struct kl_app_event` そのもの）、その lock、wake、app の thread の id。
- post: 自分の thread で list が空なら直に queue へ。そうでなければ lock の中で list の末尾へ、list が空 → 空でない時だけ wake を鳴らす。
- dispatch: `poll` の集合の最後に wake。wayland の event の後、毎回（wake の有無に依らず）wake を空にしてから list を丸ごと取り、queue の空きの分を
  入れ、残りは list の先頭へ戻して wake を鳴らし直す（queue を取り切った program が次の dispatch で眠らない）。
- 窓の close（`keiui_app_forget`）: list の中のその窓宛ての node と、queue の中のその窓の `KL_WINDOW_POST` を `KL_APP_POST`（window NULL）に
  変える。窓の他の入力は今のとおり捨てる。
- platform の wake（POSIX）: `pipe2(O_CLOEXEC|O_NONBLOCK)`。Linux の eventfd は使わない（3 つの OS で 1 つの実装）。

### 3.3 Mailer の移行（p004）

- `sync.c` の done の pipe を外す。`ml_sync_start(accounts, count, ready, context, &sync)`: sync の thread は結果を list に足し、「知らせ済み」
  の印が無ければ立てて `ready(context)` を呼ぶ。窓の側は `KL_APP_POST` の `ML_POST_RESULTS` で `ml_sync_take` を 0 まで呼び、0 を返した時に
  印が下りる。`ready` が失敗（`kl_app_post` の ENOMEM）を返せば印を下ろし、次の結果で再び知らせる（review 16）。`ml_sync_fd` を外す。
  sync.c は Keiland に依らない（callback だけ）。
- 窓 → thread の wake の pipe と IMAP の socket の `poll` は sync の thread の中（Mailer の network の層）に残す。thread が socket を待つ間に
  起こすには socket と同じ待ちに入る物が要るため。窓の側は fd を見ない。境界は §9 D。

## 4. `kl_process_*`（p005、KL_VERSION 98）

### 4.1 API（新 `keiland_process.h`）

```c
struct kl_process;

/* The streams the program uses (the others are inherited). */
#define KL_PROCESS_INPUT	0x1U	/* writes the process's standard input */
#define KL_PROCESS_OUTPUT	0x2U	/* reads its standard output */
#define KL_PROCESS_ERROR	0x4U	/* reads its standard error */
#define KL_PROCESS_TERMINAL	0x8U	/* a pseudo-terminal: input, output and error are the terminal's (read as OUTPUT) */

/* event.ready of a KL_APP_PROCESS event. */
#define KL_PROCESS_READY_OUTPUT	0x1U
#define KL_PROCESS_READY_ERROR		0x2U
#define KL_PROCESS_READY_INPUT		0x4U	/* room again for input after a short write */
#define KL_PROCESS_READY_EXIT		0x8U	/* it ended and its output was read (kl_process_exit) */

/* How a process ended. */
#define KL_PROCESS_RUNNING	0U
#define KL_PROCESS_EXITED	1U	/* code: its exit code */
#define KL_PROCESS_SIGNALED	2U	/* code: the signal (POSIX only) */

/* How it is asked to end. */
#define KL_PROCESS_HANGUP	1U	/* its terminal hangs up (without one: as TERMINATE) */
#define KL_PROCESS_TERMINATE	2U	/* asks it to end */
#define KL_PROCESS_KILL	3U	/* ends it */

/* The terminal's modes (kl_process_terminal_mode). */
#define KL_PROCESS_TERMINAL_ECHO	0x1U	/* what is typed is echoed */
#define KL_PROCESS_TERMINAL_LINES	0x2U	/* input is edited and taken by the line */

/* A line kl_process_read_line returns. */
#define KL_PROCESS_LINE	1	/* a whole line */
#define KL_PROCESS_LINE_CUT	2	/* the start of a longer line (or of a full buffer without a line end) */

#define KL_PROCESS_BUFFER	65536U	/* bytes held for one stream before its reading pauses, and for input */

struct kl_process_options {
	const char *const *argv;		/* argv[0]: a path, or a name searched in PATH */
	const char *const *environment;	/* NULL: inherited; "NAME=value" sets, "NAME" removes */
	const char *directory;			/* NULL: the program's */
	unsigned streams;			/* KL_PROCESS_INPUT | _OUTPUT | _ERROR, or _TERMINAL; 0: none */
	unsigned columns;			/* the terminal's size */
	unsigned rows;
	void *data;				/* the program's (kl_process_data) */
};

int kl_process_start(struct kl_app *app, const struct kl_process_options *options, struct kl_process **process);
int kl_process_write(struct kl_process *process, const void *bytes, size_t size, size_t *written);
int kl_process_close_input(struct kl_process *process);
size_t kl_process_read(struct kl_process *process, unsigned stream, void *buffer, size_t size);
int kl_process_read_line(struct kl_process *process, unsigned stream, char *line, size_t size);
unsigned kl_process_exit(const struct kl_process *process, int *code);
int kl_process_kill(struct kl_process *process, unsigned how);
int kl_process_resize(struct kl_process *process, unsigned columns, unsigned rows);
int kl_process_terminal_mode(const struct kl_process *process, unsigned *mode);
void *kl_process_data(const struct kl_process *process);
void kl_process_close(struct kl_process *process);
```

`kl_process_options` から窓を外した（窓の close で process の event が捨てられる問題、review 3。program は `data` で窓へ結ぶ）。

### 4.2 約束

- 全て app の thread から。event は `KL_APP_PROCESS`（`event.process`・`event.ready`）。
- **start**: 返る時に process は動いている。誤り: EINVAL、ENOENT・EACCES（親で path を解いて分かる物）、ENOMEM、EAGAIN。子が exec に失敗した
  時（OS が子の中で初めて分かる時）は EXITED 127。子は app の他の descriptor を受け継がず、signal の処理は既定・mask は空で始まる（review 11）。
  使わない stdin は null device、使わない stdout・stderr は受け継ぐ。
- **出力は level**（review 1）: stream の buffer に byte がある間、dispatch のたびに（その process の `KL_APP_PROCESS` が queue に無ければ）
  event を積む。program は 1 回にいくら読んでもよい（Terminal の 1 回 128 KB の上限を保てる）。buffer が満ちれば thread はその stream を読まない。
- **行**: `read_line` は `KL_PROCESS_LINE`（"\n" と直前の "\r" を除き NUL で終える）、`KL_PROCESS_LINE_CUT`（`size - 1` より長い行の頭、
  または改行の無いまま buffer が満ちた時の頭。残りは次の呼び出しで続きとして返る）、0（完全な行が無い）。stream の終わりの最後の行は終わり無しで
  `KL_PROCESS_LINE`。Installer は CUT を捨て、続きを行の終わりまで捨てる（今の扱いと同じ、review 10）。byte と行を同じ stream で混ぜてよい。
- **入力**: `write` は buffer に写して返る（`*written`）。満ちれば短く受け、後で `READY_INPUT`。誤り: EPIPE（入力が閉じた・process が終わった）、
  EINVAL。`close_input` は buffer を書き終えた後に閉じる。
- **終了**: wait の thread は `waitid(P_PID, pid, WEXITED | WNOWAIT)` で待ち、process の lock の下で「終わった」を立ててから刈る（pid の再利用で
  他人に signal を送らない、review 12）。刈った後、io の thread は pipe の残りを読み切る。terminal は EIO（slave を持つ全員が閉じた）まで、
  ただし最長 100 ms 読む（最後の出力を落とさない、review 15）。その後 `READY_EXIT`（出力の event が先、終了が後）。孫が pipe を持ち続けても待たない。
- **kill**: HANGUP（terminal を閉じる → 前面の process group に SIGHUP）、TERMINATE（SIGTERM）、KILL（SIGKILL）。終わった後は 0。EPERM（setuid の子）。
- **terminal**: `resize`（`TIOCSWINSZ`、pixel は 0）と `terminal_mode`（`tcgetattr` の ECHO・ICANON）。terminal でなければ ENOTSUP。
- **close**: program はもう見ない。queue と library の中のその process の event を除き（review 2）、入力・出力を閉じ（terminal なら hang-up）、
  以後 event は来ない。殺さない。刈り取りは library が続け、記録は両方の thread が終わり close された時に放す（参照の数）。
- **`kl_app_close`**: 残る process を全て close し、各 process の app への link を process の lock の下で切る（review 5）。thread は app を触る前に
  lock の下で link を確かめる。lock の順は「process の lock → app の wake」だけで、app の post の lock とは重ねない。
- 禁止: kl_process を使う program は `waitpid(-1)`・SIGCHLD の SIG_IGN をしない（PDF Viewer の `waitpid(-1)` は §5 で消える）。

### 4.3 thread と event の届け方（数を明示）

| thread | 数 | 何で止まって待つか | 役目 |
| --- | --- | --- | --- |
| `kl-process-io` | 動いている kl_process 1 つに 1（stream 0 の時は 0） | `poll`（子の stdout・stderr か pty の master・stdin（書く物がある時）・制御の pipe） | buffer への読み、stdin への書き |
| `kl-process-wait` | 動いている kl_process 1 つに 1 | `waitid(WNOWAIT)`、印の後に `waitpid` | 終わりの印、刈り取り、io への知らせ |

- event は post の list の node を使わない（malloc しない、review 16）: thread は process の記録の中の「準備の印」（出力・入力の空き・終了）を
  process の lock の下で立て、印が 0 → 非 0 の時に app の wake を鳴らす。dispatch は app の process の list を見て、印のある process で
  queue に未取得の event が無い物に `KL_APP_PROCESS` を積む（level。出力は buffer に byte がある限り毎回）。
- io の thread は全ての signal を止めて始まる（platform の thread の既定）ので、stdin への書きは EPIPE で返る（SIGPIPE で app が死なない）。
- 2 本とも detach。

### 4.4 platform（POSIX）

- path の解決（PATH の検索、`access(X_OK)`）、argv・envp の配列（親の `environ` に上書き・削除を当てた物）は親で作る。
- pipe の起動: `posix_spawn`（`addopen`（null device）・`adddup2`・`addchdir_np`・`addclosefrom_np`、`POSIX_SPAWN_SETSIGDEF`（全 signal）と
  `POSIX_SPAWN_SETSIGMASK`（空））。
- terminal の起動: `forkpty(&master, NULL, NULL, &winsize)`（3 つの OS に有り、制御端末と大きさまで済む。今の Terminal と同じ）。子は
  async-signal-safe な呼び出しだけ: signal の処理を既定へ・mask を空に・`chdir`・`closefrom(3)`・`execve`、失敗は `_exit(127)`。
  header は zedBSD・Linux が `<pty.h>`、FreeBSD が `<libutil.h>`（`-lutil`）。

### 4.5 移行（p005）

- **Installer**: `kl_process_start({"/usr/libexec/install-admin", "probe"|"session"}, OUTPUT | (session なら INPUT))`。stdout は `read_line`
  （CUT は続きごと捨てる）、要求は `write`、session の終わりは `close_input`、終了は `READY_EXIT` の `kl_process_exit` を今の wait の status と
  同じ値（EXITED なら code << 8、SIGNALED なら signal）にして `inst_flow_ended` へ。probe の stdin は今どおり受け継ぐ（INPUT を付けない時は
  null device になる点だけが違う。install-admin の probe は stdin を読まない）。`signal(SIGPIPE, SIG_IGN)` は他の書き込みのために残す。
- **Terminal**: tab ごとに `kl_process_start({"/bin/sh"[, "-c", cmd]}, TERMINAL, environment {"TERM=xterm"}, columns, rows)`。
  `main_read_shell` は `kl_process_read`（上限はそのまま）、`main_write_shell` は `kl_process_write`（短い時は `READY_INPUT` で続き）、
  `main_tell_size` は `kl_process_resize`、`main_secret_follow` は `kl_process_terminal_mode`、tab を閉じる時は `kl_process_kill(HANGUP)` と
  `kl_process_close`、`READY_EXIT` で tab を閉じる。`window.c` の watch の同期を外す。
- **Phone**: viewer を stream 0 の `kl_process_start` で起こし、`READY_EXIT` で数を減らして close（16 の上限と利用者への告知を保つ、review 13）。
- **Preview**（review 14）: API から fd と pid を外す。`preview_start(const char *input, const char *output_path, ...)` か、出力の一時 file を
  client が自分で作る形にし、`struct preview_job` の `pid_t` を不透明な platform の記録へ。`client.c` の `waitpid`・`kill` は OS ごとの
  spawn の file の隣（POSIX 共通の `preview/posix/`）へ。sandbox（資源の上限）は Preview に固有なので kl_process にしない。

## 5. `kl_launch`（p005）

```c
struct kl_launch_options {
	const char *const *argv;		/* or */
	const char *command;			/* a command line for the system's command interpreter (exactly one of the two) */
	const char *const *environment;
	const char *directory;
	struct kl_window *from;			/* the window the user works in (NULL for none): an activation token for it */
};

int kl_launch(struct kl_app *app, const struct kl_launch_options *options);	/* 0, ENOENT, EACCES, EINVAL, ENOMEM */
```

- 切り離した起動: app より長く生き、stdin は null device、出力は app の物を受け継ぎ、何も返らない。数を数えたい program（Phone）は kl_process。
- platform（POSIX、review 6）: path の解決・argv・envp・`from` の token（`kl_activation_token` → `XDG_ACTIVATION_TOKEN=`）は**親で**作る。
  `fork` → 中間の子は async-signal-safe な呼び出しだけ: `setsid`・`fork`・孫で signal を既定へ・mask を空に・null device を 0 へ・`closefrom(3)`・
  `chdir`・`execve`。exec の失敗は CLOEXEC の pipe で errno を親へ書いて `_exit`。親は中間の子を `waitpid` で即座に刈り、pipe で結果を読む。
  孫は init に引き取られ zombie にならない。thread は 0 本。command は `/bin/sh -c command`。
- 移行: Files の `fm_apps_launch`（command、`@terminal ` は argv）と `fm_apps_spawn`（argv）、Terminal の新しい窓、Settings の `files`、
  PDF Viewer の `notes`。PDF Viewer の `waitpid(-1)` を外す。Files の `apps_run` は stdout・stderr を /dev/null にしていたが、kl_launch は
  app の出力（desktop の log）を受け継ぐ（変更点）。

## 6. `kl_instance` の event（p006、KL_VERSION 99）

```c
int kl_instance_bind(struct kl_instance *instance, struct kl_app *app);	/* 0, EINVAL (bound already), ENOMEM */
```

- 後の起動の要求は `KL_APP_INSTANCE`（`event.instance`）で届き、program は `kl_instance_take` を 0 まで呼ぶ。`kl_instance_open`・`take`・`close` は
  今のまま（open は compositor に繋ぐ前に呼び、引き渡したら program を終える）。
- thread `kl-instance`（bind した instance 1 つに 1）: listen の socket と制御の pipe を `poll` で待ち、accept した接続から 2 行を 500 ms の上限で
  読み、instance の lock の下の list に積み、印を立てて app の wake を鳴らす（§4.3 と同じ印の方式、malloc しない）。
- close の順（review 2・4）: `kl_instance_close` は制御の pipe で thread を止めて join し、queue からその instance の event を除く。
  `kl_app_close` は bind された instance を unbind する（thread を止めて join）ので、Settings の今の順（app を先に閉じる）でも安全。
- `kl_instance_fd` は p008 で外す（今の唯一の利用者 Settings が p006 で bind に移る）。
- Settings の移行: `kl_instance_bind(inst, app)`、`KL_APP_INSTANCE` で `main_handed_over()`、loop ごとの呼び出しと watch を外す。
- Windows の当て: 名前付き pipe（利用者の SID を名前に、ACL で利用者だけ）、thread は `ConnectNamedPipe` で待つ。

## 7. libbrowser の network の thread（p007、BROWSER_API_VERSION 3）

### 7.1 API（`include/browser/browser.h`）

```c
typedef void (*browser_wake)(void *context);	/* from the library's threads: call browser_view_process soon; once until then */

struct browser_view_options {
	...（今の物）
	browser_wake wake;
	void *wake_context;
};

int browser_view_timeout(const struct browser_view *view);	/* page の timer だけ（network の時間切れは net の thread の中） */
void browser_view_process(struct browser_view *view);		/* pollfd の引数を外す */
```

`browser_view_poll_fds` を外す。shell の wake は `kl_app_post(app, SHELL_POST_NET, NULL)`、`KL_APP_POST` で `browser_view_process`。

### 7.2 thread（数を明示）

| thread | 数 | 待つ物 | 役目 |
| --- | --- | --- | --- |
| `browser-net` | view 1 つに 1（新） | `poll`（loader の socket・DNS の答えの pipe・media の engine の wake・制御の pipe） | 接続・TLS・HTTP の読み書き、完了を list に積み wake、network の時間切れ |
| `browser-resolve` | view 1 つに 1（今の DNS の thread） | 条件変数 | `getaddrinfo`、答えを list と pipe へ（今のまま） |
| media の engine | 再生中の media 1 つに 1（今のまま、libmedia） | libmedia の中 | 状態・絵の変化で wake の pipe（libmedia は変えない） |

- 分け方: request の状態の機械（`net/loader.c` の接続・TLS・HTTP の framing・redirect・時間切れ）は `browser-net` だけが触る。UI の thread は
  fetch・send・cancel を命令の list（mutex）に積み、制御の pipe で起こす。完了（応答・誤り）は完了の list に積み、未処理の印が 0 → 1 の時だけ
  `wake`。`browser_view_process` が UI の thread で完了を取り `net_request_done` を呼ぶ（page・DOM・script は UI の thread だけ）。
- request の寿命（review 9）: `struct net_request` の記録は UI の thread だけが放す。cancel は命令で net の thread に伝え、cancel の後に届いた
  完了の callback は呼ばない。net の thread は自分の側の状態（socket・TLS）を放したら「済み」を返し、UI の thread がその後に記録を放す。
- 大域の状態（review 7）: TLS の遅延の初期化（`net/tls.c` の `tls_loaded`・`tls_context`）は once で守る。誤りの理由（`tls_reason`）は request
  ごとに持つ。cookie の jar 全体（process で 1 つ）に mutex。UI の thread の同期の取得（`net_http_fetch`）も同じ TLS と cookie を使うので
  これで両方が安全になる。（HSTS は libbrowser に無い。）
- media（review 8）: engine の wake の fd は命令の list で net の thread の poll に登録・解除する。鳴れば net の thread が pipe を読み捨ててから
  wake を呼ぶ（status は engine の lock で読むので UI の側は困らない）。UI の thread は engine を close する前に解除を命じ、net の thread の
  「解除済み」を待ってから close する（fd の番号の再利用を避ける）。
- 同期の取得（`net_http_fetch`）と headless の `view_settle_network` は libbrowser の中に留まる。settle は wake の代わりに条件変数で待つ。
- shell（`shell/shell.c`・`shell/window.c`）: `net_fds`・`window_watch`・`window_ready`・`window_poll_unwatched` と 10 ms の上限を外す。
- 実装（p007、[phase007](phase007/phase.md)）での形: 命令の list は「incoming の list と request の cancel の印（loader の lock の下）＋制御の pipe」、
  完了の list と `notified`（0 → 1 で wake）。TLS の誤りの理由は thread ごと（`pthread_key`、`__thread` は zedBSD の共有 library で link できない）と
  request ごと。headless の待ちは `net_loader_wait`（条件変数）。shell は view を窓（app）より先に閉じる。

## 8. platform の当て

| 項目 | zedBSD | Linux | FreeBSD | Windows（移す時の当て） |
| --- | --- | --- | --- | --- |
| post の起こし | `pipe2` | `pipe2` | `pipe2` | event object（`SetEvent`）を待ちの集合に |
| thread・mutex | pthread | pthread | pthread | Win32 thread・SRWLOCK |
| thread の名前 | libc に無い: 付けない（`#if`） | `pthread_setname_np` | `pthread_setname_np`（`<pthread_np.h>`） | `SetThreadDescription` |
| pipe の起動 | `posix_spawn`・`addchdir_np`・`addclosefrom_np`・`SETSIGDEF` | 同（glibc 2.34 以降） | 同（13.1 以降） | `CreateProcessW`、handle の継承の list |
| terminal | `forkpty`（`<pty.h>`）、`TIOCSWINSZ`・`tcgetattr` | 同（`<pty.h>`） | `forkpty`（`<libutil.h>`、`-lutil`） | ConPTY（`CreatePseudoConsole`・`ResizePseudoConsole`）。`terminal_mode` は ENOTSUP |
| 刈り取り | `waitid(WNOWAIT)` の thread、`waitpid` | 同 | 同 | `WaitForSingleObject(process)` の thread、`GetExitCodeProcess` |
| kill | `kill`（SIGHUP・SIGTERM・SIGKILL） | 同 | 同 | HANGUP は `ClosePseudoConsole`（古い Windows では出力を読み切るまで戻らないので、io の thread が読み続ける間に呼ぶ）、TERMINATE・KILL は `TerminateProcess`（console の無い GUI の app から CTRL_BREAK は送れない、review 18） |
| 切り離した起動 | 二重の fork と `setsid` | 同 | 同 | `CreateProcessW`（`DETACHED_PROCESS`）、handle を閉じる |
| command | `/bin/sh -c` | 同 | 同 | `cmd.exe /c`（Files の opener の command は POSIX の shell の書き方で、Windows へはそのまま移らない） |
| instance | UNIX socket（今のまま） | 同 | 同 | 名前付き pipe |
| 終わり方 | EXITED・SIGNALED | 同 | 同 | EXITED だけ（code は 32 bit） |

zedBSD の libc には `posix_spawn`・`POSIX_SPAWN_SETSID`・`addclosefrom_np`・`pipe2`・`forkpty`・`setsid`・`TIOCSWINSZ` が有り
（`include/libc/spawn.h`・`pty.h`・`unistd.h`、`include/uapi/termios.h`）、kernel は `waitid` の `WNOWAIT` を持つ（review の確かめ）。
`POSIX_SPAWN_SETSIGDEF` の有無と、FreeBSD の `addchdir_np` は p005 の build で確かめ、無ければ fork の経路に寄せる。

## 9. ユーザーの判断（Q1 経由で問い合わせ中、2026-10-30）

- **A. 権限を上げる起動を API に含めるか** — 推し: 含めない。Installer は OS の仕組み（setuid root の install-admin が呼び手を確かめる）を
  使い、kl_process で普通に起動できる。昇格の方針（誰に・何を・どう確かめるか）は system（sessiond・compositor の system 拡張）の物で、
  app ごとの API にすると全ての app が昇格を求める形になる。Windows では UAC（`ShellExecute` の runas）で利用者との対話が違う。
  要るなら将来、compositor が利用者に確かめる別の API として設計する。
- **B. `kl_window_post` の意味の変更** — 推し: 同じ名前で §3.1 の形に変える（data を足し int を返し、kl_app の窓へは他の thread からも）。
  理由: 今の呼び手が 0、似た事に 2 つの名前を作らない、app の thread からの順の約束は保つ。代案: 今の物を残し別名を足す。
  （p004 は推しの形で実装した。別名に決まれば名前を戻して足し直す。）
- **C. 名前** — 推し: `kl_app_post`・`kl_process_*`・`kl_launch`・`kl_instance_bind`・`KL_APP_POST`・`KL_APP_PROCESS`・`KL_APP_INSTANCE`、
  header `keiland_process.h`。
- **D. 「UNIX 依存を削除」の境界**（review 14、新） — 推し: この WS は **Keiland の公開の API と、app と desktop・他の program との
  やりとり（event・process・起動・instance・network の待ち）** から fd・pid・signal・socket を外す。app の中で閉じる物（Mailer の network の層の
  socket と `poll`、app の worker の pthread、file の `open`・`read`）は、その app の「platform の部分」として残し、Windows に移す時に置き換える。
  理由: thread と file の I/O まで外すには `kl_thread`・`kl_file` の類の大きな API が要り、決定 1〜6 の具体の対象（Mailer・Installer・Terminal・
  Settings・Browser の fd）を越える。代案: pthread と file の I/O も Keiland の API で包む（別の WS）。

答えを待つ間、判断に依らない部分から進める。B・C の答えで名前・引数を直す。

## 10. design-reviewer の指摘と反映（2026-10-30、23 件）

| # | 重さ | 指摘 | 反映 |
| --- | --- | --- | --- |
| 1 | high | 出力の event が edge だと Terminal の読み上限で lost wakeup、洪水で UI が戻らない | level に（§4.2・§4.3） |
| 2 | high | close の後に queue・list に残る event の process・instance が解放済み | close が queue から除く（§4.2・§6） |
| 3 | high | 窓の close で process の event が捨てられる | options から窓を外す（§4.1） |
| 4 | high | Settings は app を先に閉じ、instance の thread が解放済みの app へ post | `kl_app_close` が unbind（§6） |
| 5 | high | detach の thread と `kl_app_close` | process の lock の下で link を切る、lock の順（§4.2） |
| 6 | high | fork の後の子で `posix_spawn`・PATH の検索（malloc） | 親で準備、子は async-signal-safe だけ（§5） |
| 7 | medium | TLS の大域の遅延初期化・誤りの理由・cookie | once・request ごと・jar の mutex（§7.2） |
| 8 | medium | media の wake で net の thread が busy loop、fd の再利用 | net が読み捨て、登録・解除は命令（§7.2） |
| 9 | medium | net_request の寿命と cancel、timeout | UI だけが放す、cancel の後は callback 無し、時間切れは net（§7.1・§7.2） |
| 10 | medium | 長い行の扱いが今と違う、buffer 満ちで dead lock | `KL_PROCESS_LINE_CUT`（§4.1・§4.2） |
| 11 | medium | 子が SIGPIPE の無視を受け継ぐ | `SETSIGDEF`・空の mask、forkpty の子も（§4.2・§4.4） |
| 12 | medium | 刈った直後の kill で pid の再利用 | `waitid(WNOWAIT)`（§4.2） |
| 13 | medium | Phone の viewer の上限 | Phone は kl_process（§4.5） |
| 14 | medium | Preview の API の fd・pid、Mailer の残り、境界 | Preview の API を直す（§4.5）、境界は §9 D |
| 15 | medium | Terminal の最後の出力 | pty は EIO まで最長 100 ms（§4.2） |
| 16 | medium | ENOMEM で Mailer の結果が届かない | Mailer の印を戻す、library の内部は malloc しない（§3.3・§4.3） |
| 17 | medium | 試験で誤りを見つけられない | 最小の wayland server で通しの host の試験（§2・§12） |
| 18 | medium | Windows の写像の誤り | §8 を直した |
| 19 | low | §1 の記述の誤り（Settings の waitpid、Installer の status と stdin） | §1 を直した |
| 20 | low | 自分の thread の post の順の文 | §3.1 を直した |
| 21 | low | `KL_TERMINAL_*` の名前、`kl_app_event` の大きさの変更の回数、userland/tests の作り直し | `KL_PROCESS_TERMINAL_*`、field は p004 で一度に、§12 |
| 22 | low | B の答えを待たずに実装 | §9 B に注記（推しで実装、違えば直す） |
| 23 | low | p005 が大きすぎる | §12 で 3 つに分ける案 |

## 11. 範囲の外（この WS では変えない）

- settings・music・videoplayer・photos の worker の thread が dispatch の時間切れで結果を拾う形（fd を公開しない。`kl_app_post` に寄せると
  時間切れの待ちが減るが、別の WS で）。
- ime の自前の loop（kl_app を使わない、pipe と signal）。
- libbrowser の同期の取得（`net_http_fetch`）を loader に寄せること。
- Installer の root の helper（`installer/admin/`）の中の fork・exec（root の別 program、app の API ではない）。
- compositor・`libkeiland-backend-*`・sessiond・libmedia。
- §9 D で「境界の外」とした物（app の中の network の層・pthread・file の I/O）。

## 12. Phase の細目と確かめ

| Phase | 変える物 | KL_VERSION | 確かめ |
| --- | --- | --- | --- |
| p004 | `ui/app.c`・`ui/window.c`・`ui/window.h`・`platform/`・`keiland_app.h`・`keiland_window.h`・exports、Mailer | 97 | build（3 arch・Linux）、host の試験 `tests/post-test.c`（最小の wayland server、4 thread × 5000 と自分の thread の 1000（queue の 512 を超える）、順・数・眠った dispatch の起こし、TSan の build でも）、Mailer の同期は QEMU（IMAP の相手が要る、未定） |
| p005 | `process.c`・`platform/`・`keiland_process.h`、Installer・Terminal・Phone（(a)） | 98 | host の試験（行・CUT・byte・level・入力の満ち・終了の順・kill・pty の大きさと mode・close と queue、Linux）、QEMU で Terminal と Installer の probe |
| p013 | `kl_launch`（`launch.c`）と Files・Settings・PDF Viewer・Terminal の新しい窓（review 23 で p005 から分けた） | 次の版 | QEMU で Files の「開く」と Terminal の新しい窓 |
| p014 | Preview の API から fd・pid を外す（review 23 で p005 から分けた） | — | Files・Photos の thumbnail（QEMU） |
| p006 | `instance.c`・`keiland_instance.h`、Settings | 99 | host の試験（2 つ目の起動の引き渡し、close の順）、QEMU で Settings の 2 回目の起動 |
| p007 | libbrowser の `net/`・`page/network.c`・`page/media.c`・`view/`・`browser.h`、shell | — | libbrowser の host の試験（headless の取得、cancel・redirect・TLS）、QEMU で Browser の https の page |
| p008 | `kl_app_watch_fd`・`KL_APP_FD*`・`KL_APP_FDS_MAX`・`kl_app_event.fd`・`kl_instance_fd` を外す | 100 | grep で利用者 0、3 arch・Linux・FreeBSD の build（userland/tests の probe も）、exports |
| p009 | `docs/howto/keiland-programming.md` に post・process・launch・instance の節（p011・p012 の後） | — | 例の compile |
| p010 | 規約の全文の見直し | — | build |

- `kl_app_event` と `kl_window_event` の大きさが変わる（ABI の変更。WS131 の方針で source の互換だけを保つ）。p004 で全ての app と
  userland/tests を作り直す。

## 13. `KL_APP_LANGUAGE`（p015、KL_VERSION 100 の見込み、決定 12・13）

### 13.1 API（`keiland_app.h`）

```c
#define KL_APP_LANGUAGE	7U

struct kl_app_event {
	...（§3.1 の物）
	char language[KL_TR_NAME_MAX + 1U];	/* KL_APP_LANGUAGE: the language changed to ("en", "ja", ...) */
};

/* The desktop's language when the application opened ("en" without the compositor's settings). */
const char *kl_app_language(const struct kl_app *app);
```

- 決定 13 のとおり、event は変わった先の言語の名前を運ぶ。program と kl_tr はその値をそのまま使い、kl_settings を読み直さない。
- 変更が続けて来れば event も同じ順に来る。最後の event が勝つ。

### 13.2 kl_settings との関係（上に載せる）

- 言語の値は今と同じく compositor の設定の拡張の `ui.language`（`kl_settings_*` の watch）から来る。watch の callback は**変わった値を引数で
  受ける**（`kl_settings_watch_fn` の `value`）。kl_app はその値を言語の名前（`kl_tr_language_code`）に直して event に入れる。callback の中で
  `kl_settings_get_*` を読み直さないので、短い間に続いた変更でも各 event の値はその変更の物（決定 13 の理由に応える）。
- 実装: `kl_app_open` が app の display で `kl_settings_open` を 1 つ持ち、`ui.language` を watch する（`kl_appearance` と同じく app が持つ）。
  `kl_app_dispatch` が wayland の event の後に `kl_settings_dispatch` を呼び、callback が `KL_APP_LANGUAGE` を積む（queue が満ちれば §3 の
  post の list へ回し、捨てない）。compositor が設定の拡張を持たなければ event は来ず、`kl_app_language` は "en"。
- `kl_tr_follow`（kl_settings の上の helper）は kl_app を使わない program のために残す。kl_app の program は `KL_APP_LANGUAGE` を使う
  （Files・Settings の look・Installer の `kl_tr_follow` を移すかは p015 の中で決める。移すなら `kl_tr_follow` と同じ動きになることを確かめる）。

### 13.3 翻訳（kl_tr）の読み直し

- 同じ thread で: `KL_APP_LANGUAGE` で `kl_tr_open(domain, event.language)`（失敗は "en"）、窓を描き直す。
- 別の thread で（読みが遅い時、compositor の `wayland/language.c` と同じ形）: program は event ごとに番号（世代）を 1 つ進め、
  worker の thread に（番号、言語）を渡す。worker は `kl_tr_read` の結果を `kl_app_post(app, CODE, job)` で返す。app の thread は
  job の番号が最新の番号と違えば `kl_tr_discard`、同じなら `kl_tr_use`。古い言語の読みが新しい言語の後に着いても使われない（決定 13 の「最後が勝つ」）。
- 解説（p009）に両方の形の完全な例を足す。
