<!-- awesome-plan project=zedbsd record=ws168-p001 -->

# ws168-p001: 隔離されたプレビューの command の要件と設計

Phase ID: `ws168-p001`
Parent: [WS168](../ws.md)
Status: planning（2026-10-05 P1 generation17。設計の第 1 版。code は Q1 がこの設計を見てから。§8 の人間の判断が要る）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（WS138 の T1 の待ちの間に WS168 の p001 の設計、kernel の capability mode の UAPI の案を含む、HAL は不変）

## 範囲

- ユーザーの要件（ws.md の 1〜5）を満たす、縮小表示を作る専用の command（仮の名前 `/usr/libexec/keiland-preview`）の設計。
- kernel に足す「その process は fd 0・1 の読み書きと計算しかできない」mode（以下 **sandbox mode**）の UAPI と実装の方針。
- 呼び出し側（Files の縮小表示、Settings の背景の tile）の変え方、時間・memory の上限、失敗の扱い。
- Linux・FreeBSD の版（Keiland は 3 つの OS で動く）で何が当てられるか。
- 試験の方針。
- 由来: ユーザーの 2026-10-05 の追加（ws.md）。関係する記録: [Future Work](../../future-work.md) の F-035（縮小表示、「decoder の package の監査が要る」）。
  ws.md の「設計で決めること」の権限の順序（chroot → uid を落とす → capability mode）と静的 link は、この設計で見直した（§3.1、§8 の H2・H6）。
- 範囲外: 実装（p002 以降）。compositor の背景そのものの復号（§8 の H4 で判断）。Image Viewer・PDF Viewer・browser（WS074 の D1 で process の分離は
  後回し）など、利用者が開いて見る app の隔離（別の WS の題）。

## 1. 要件の読み替え

| ユーザーの要件 | この設計での形 |
| --- | --- |
| 専用の command | `keiland-preview`。入力を 1 つ読み、縮小表示を 1 つ書いて終わる。常駐しない。1 回の起動で 1 file |
| 入力は fd 0、出力は fd 1 で開いた状態で起動、出力は規定の場所に保存 | 呼び出し側が入力の file を読み取りで開いて fd 0、cache の記録の一時 file を書き込みで開いて fd 1 にして起動する。command は名前を一切知らない。書き終わったら呼び出し側が一時 file を規定の名前に rename する（今の cache の記録の場所と形のまま、§5） |
| 最低でも chroot で隔離 | sandbox mode に入る時に、kernel が root と cwd を空の directory（`/var/empty`、image に既にある root の 0555）に替える（§3.3） |
| そのほか可能な限りの jail | sandbox mode（許す system call の表）、rlimit（memory・CPU の時間・書く大きさ・core）、fd 0・1 以外を閉じる、呼び出し側の時間の上限 |
| 他の file を open できない、network を使えない、fork できない | sandbox mode の表に open・socket・fork・vfork・exec・thread_create を入れない。path を取る call は全部断る |
| 純粋に fd 0・1 の入出力と計算のみ | 許すのは既存の fd の read・write・pread・lseek・fstat・close、匿名の memory の確保と解放、時刻、exit だけ（§3.2） |
| RCE されても乗っ取られる権限を最小に | 乗っ取った code にできるのは、fd 0 を読む・fd 1 に書く・CPU と memory を rlimit の範囲で使う・自分を終わらせる、だけ。他の process・file・network・device に届く道が無い |

原則: **信頼できない入力の byte を解釈する前に sandbox mode に入る**。入る前に行うのは、引数（呼び出し側が作った信頼できる文字列）の解釈、
fd の確かめ、PDF の代わりの font の読み込み（§4.3）、入力の先頭の数 byte の形式の判定（固定の signature との比較だけ）に限る。

## 2. 今の kernel（2026-10-05 の main を読んだ）

| 項目 | 今 | 場所 |
| --- | --- | --- |
| system call の入口 | HAL が登録された dispatcher（`kernel_syscall_handler`）を呼ぶ。そこで credential を固定し、`syscall_dispatch_body` の大きな switch に入る。**全部の call が 1 か所を通る** | `src/kern/syscall.c` 9962〜（`kernel_syscall_handler`）、9377〜（`syscall_dispatch_body`） |
| call の番号 | `enum syscall_number`（1〜169、17・18 は欠番）。最後は `KERN_SYS_vfork = 169` | `include/uapi/syscall.h` |
| libc の stub | `call(KERN_SYS_…, …)` を呼ぶ関数（例 `chroot`） | `userland/base/libc/posix.c` 4952 |
| chroot | `fs_chroot`: euid 0 だけ。root と cwd を一緒に替える | `src/kern/namei.c` 562、`syscall.c` 3413 |
| process | `struct process` に `flags`（今は `PROCESS_AUTOREAP` などの 3 bit）、`limits`、`cred`、`fd`、`cwdi`。fork（`process_create` の系統）で `limits`・`cred` を親から写す | `include/kern/process.h` 68〜、`src/kern/process.c` 1148 |
| rlimit | `RLIMIT_NOFILE`・`STACK`・`AS`・`CORE`・`CPU`・`DATA`・`FSIZE` | `include/uapi/resource.h` |
| 新しい fd を得る道 | open・openat・socket・socketpair・accept・pipe・pipe2・dup・dup2・dup3・fcntl（F_DUPFD）・recvmsg（SCM_RIGHTS）・fexecve など | `include/uapi/syscall.h` |
| set-ID の exec | exec が image の set-user-ID の bit を取る（traced の process は `MOUNT_NOSUID` の扱い） | `src/kern/exec.c` 299〜329・1355 |
| rlimit の実施 | `RLIMIT_CPU` は soft で SIGXCPU、hard で SIGKILL。`RLIMIT_FSIZE` は write の系統で実施 | `src/kern/resource.c` 315、`syscall.c` 2995 ほか |
| process の生成 | fork・vfork（libc の `posix_spawn` は vfork の上）・execve・fexecve・thread_create。user に見える spawn の call は無い | `syscall.c` 8409・8427・8896・8935・7455 |
| `/var/empty` | image の作成で作る（sshd・greeter の home） | `tools/build/make-arch-overlay-ufs.noct` 59 |

capability mode に当たる物（Capsicum の `cap_enter`、Linux の seccomp、OpenBSD の pledge）は無い。

## 3. kernel: sandbox mode（UAPI の案、HAL は不変）

### 3.1 UAPI

新しい system call を 1 つ足す。

```c
/* include/uapi/syscall.h */
	KERN_SYS_sandbox_enter = 170,

/* include/uapi/sandbox.h（新） */
#define SANDBOX_KILL_ON_DENY	0x00000001U	/* 断る call で process を SIGKILL で終わらせる（無ければ EPERM を返す） */
#define SANDBOX_FLAGS_ALL	0x00000001U

/* libc（include/libc/sandbox.h、userland/base/libc/posix.c） */
int sandbox_enter(int root_fd, unsigned flags);
```

- `root_fd`: 新しい root にする directory の fd（読み取りで開いた directory）。kernel はその directory を root と cwd にし（`fs_chroot` と同じ入れ替え）、
  続けて同じ call の中で sandbox mode に入る。fd は閉じない（呼び出し側が後で閉じる。閉じるのは許す call）。
- 戻り値: 0、または -1 と errno（EBADF: fd が無い、ENOTDIR: directory でない、EINVAL: 知らない flag、EPERM: 既に sandbox mode）。
- **一方通行**: 一度入ったら出る道は無い。fork・exec は断るので子に引き継ぐ場面は無いが、念のため `process_create` で子に写し、exec でも消さない。
- **root の権限が要らない**: 普通の chroot が root に限られるのは、chroot の中で set-user-ID の program を exec して偽の `/etc` を読ませる攻撃を防ぐため。
  sandbox mode は exec と path の解決を全部断るので、その攻撃が成り立たない（FreeBSD 14 が `PROC_NO_NEW_PRIVS` の process に root でない chroot を許すのと同じ理由）。
  root と mode を **同じ call で同時に** 替えるので、chroot だけが済んで exec ができる中間の状態が無い。

### 3.2 許す call の表

`syscall_dispatch_body` の先頭（switch の前）で、`process->flags & PROCESS_SANDBOX` なら番号を表で引く。`kernel_syscall_handler` ではなく
dispatch の側に置くのは、sigreturn の後や停止の後に **再 dispatch される call** も同じ表を通すため（sandbox に入る前に始まった call が入った後に
再開される道を塞ぐ）。表に無い番号は、
`SANDBOX_KILL_ON_DENY` があれば SIGKILL を自分に送って終わる（シグナルの処理を経ない）、無ければ -EPERM を返す。表は `src/kern/sandbox.c`（新）の
static な bitmap（番号 170 までの 1 bit ずつ）にする。

| 区分 | 許す call | 引数の制限 |
| --- | --- | --- |
| 終わる | `exit`、`thread_exit` | — |
| 既存の fd の入出力 | `read`・`write`・`pread`・`pwrite`・`readv`・`writev`・`lseek`・`fstat`・`close` | 無し（fd の表に有る物だけが対象。新しい fd は作れない） |
| memory | `mmap`・`munmap`・`mprotect`・`brk` | `mmap` は `MAP_ANONYMOUS \| MAP_PRIVATE` で fd が -1 の物だけ（file・device の map と、匿名でも `MAP_SHARED` は EPERM。今は開いた fd の `MAP_SHARED` の書き込みの map ができるので塞ぐ）。`mmap`・`mprotect` は `PROT_EXEC` を断る（新しい実行可能な page を作らせない） |
| 時刻・待ち | `clock_gettime`・`clock_getres`・`nanosleep`・`sched_yield` | — |
| 自分の状態 | `getpid`・`thread_self`（TLS、rtld と libc が使う）・`getrlimit`・`sigprocmask`・`sigreturn` | — |
| 自分を止める | `thread_kill` | 自分の process の thread だけ（今の `sys_thread_kill_call` が既に他の process を断る）。libc の `raise`・`abort` が使う |
| libc の内部 | `usync`・`atomic`（libc の lock）、`getentropy`（malloc・乱数の種） | `usync` は自分の address の範囲だけ（共有の map が無いので他の process に届かない） |

断る主な物と理由:

- path を取る全部の call（`open`・`openat`・`stat`・`chdir`・`mkdir`・`unlink`・`rename`・`chroot`・`mount`・xattr の系統…）: 他の file を開けない。
- `socket`・`socketpair`・`accept`・`connect`・`sendmsg`・`recvmsg`（SCM_RIGHTS で fd を受け取れる）: network と fd の受け渡し。
- `fork`・`vfork`・`execve`・`fexecve`・`thread_create`: 新しい実行の流れ。
- `dup`・`dup2`・`dup3`・`fcntl`・`pipe`・`pipe2`: fd を増やさない（fcntl の F_DUPFD を含む。fcntl は全部断る）。
- `ioctl`・`sysctl`: device と kernel の状態に届く。`ioctl` は fd 0・1 が端末でも断る（libc の `isatty` は ioctl の TCGETS を使うので、sandbox の中で
  stdio を使うと KILL の mode で終わる。command は stdio を使わない）。今の通常の file の fd にも `KERN_FILE_FORMAT_RESERVE`（容量の予約）の ioctl が通る。
- `kill`・`sigqueue`・`thread_kill`・`ptrace`・`setpgid`・`setsid`・`setpriority`: 他の process に届く、または process の集まりを変える。
- `set*uid`・`set*gid`・`setrlimit`・`umask`・`setproctitle`・`timer_*`・`setitimer`・`sigaction`: 要らない（`setrlimit` は上限を下げることも含めて断る。上限は入る前に掛ける）。
- `ftruncate`・`fsync`・`fchmod`・`fchown`・`futimens`・`flock`・`fstatvfs`: fd 1 の file の属性を変える・調べる必要が無い。

表は「許す物を書く」形（新しく足した call は自動的に断られる）。表の変更はこの設計の改訂として扱う。

### 3.3 root の入れ替え

- `fs_chroot` を 2 つに分ける: path を解決して directory を得る部分と、`cwdinfo` の root と cwd を入れ替える部分（`fs_chroot_path`・`cwdinfo_set_root`）。
  `sandbox_enter` は fd の file から directory の `struct path` を取り、検索の権限を確かめてから後者を呼ぶ。
- 入れ替えと `PROCESS_SANDBOX` の set は、process の lock の中で続けて行う。複数の thread がある process は EBUSY で断る（他の thread が途中の
  call で古い root を使い続けるのを避ける。command は thread を作らない）。

### 3.4 そのほかの kernel の変更

- `PROCESS_SANDBOX`（`include/kern/process.h` の flags の新しい bit）。今の `flags` は fork で写らない（`process_create` が写すのは `umask`・`nice_value`・
  `limits`・`cred`）。fork は断るので実際には写る場面が無いが、kernel の中の他の経路に備えて、`fork_process` の `set_id` を写す所（`src/kern/process.c` 1415）で
  写す。exec でも消さない（exec は断るが、同じく備えとして、traced の process と同じく set-ID を効かせない `MOUNT_NOSUID` の扱いにする、`exec.c` 1355）。
- `sys_mmap_call`・`sys_mprotect_call` に、sandbox の時の引数の制限（§3.2）。
- 試験の口: 断った時に `klog` へ 1 行（`SANDBOX deny pid=… call=…`）。回数を絞る（1 process に 8 行まで）。
- HAL（`include/hal/hal.h`・`src/hal/`）は変えない。system call の入口の HAL の責務は今のまま。

### 3.5 足さない物（理由）

- 細かい権限の組み合わせ（Capsicum の fd ごとの権利、pledge の promise の組み合わせ）: 使う program が 1 つで、要件が「fd 0・1 と計算だけ」なので、
  表は 1 つで足りる。必要になった時に flag を足す。
- seccomp のような program を載せる filter: kernel に interpreter が要り、攻撃面が増える。
- 専用の uid への切り替え（§8 の H2 で判断。案は「切り替えない」）。

## 4. command（`keiland-preview`）

### 4.1 起動と引数

```
keiland-preview --width=N --height=N [--fit=contain|cover] [--stamp=TEXT]  < 入力 > 出力
```

- `--width`・`--height`: 縮小表示の最大の大きさ（Files は 256×256 の contain、Settings は 240×150 の cover、Quick Look は 1600×1600
  （`ui-preview.c` 52 の `LOOK_PICTURE_SIDE`）、hero は 1920×1080）。上限は一辺 4096。
- `--stamp`: 出力の PPM の comment に書く文字列（cache の記録の「元の file の更新時刻と大きさ」、今の `CACHE_MARK` の行）。呼び出し側が作る。
- 終了の status: 0 成功、1 形式が分からない、2 壊れている、3 大きすぎる、4 memory が足りない、5 出力に書けない、64 引数の誤り、
  70 sandbox に入れない（kernel が古い・mode が無い、§4.4）。

### 4.2 手順

1. 引数を解釈する。fd 0 と fd 1 を `fstat` で確かめる（fd 0 は通常の file、fd 1 は通常の file。pipe は §8 の H5）。
2. `closefrom(2)`: fd 2 以降を全部閉じる（stderr も閉じる。log は exit の status だけ）。
3. rlimit を下げる: `RLIMIT_AS` 1 GiB（16M 画素の RGBA の作業に足りる量、§6 で見直す）、`RLIMIT_CPU` 10 秒、`RLIMIT_FSIZE` 48 MiB（4096×4096 の PPM と
   comment に足りる）、`RLIMIT_CORE` 0、`RLIMIT_NOFILE` 3。
4. 入力の先頭の 8 byte を `pread` で読み、固定の signature と比べて形式を決める（PNG・JPEG・GIF・PPM/PGM・PDF）。PDF なら §4.3 の font を読む。
5. `open("/var/empty", O_RDONLY | O_DIRECTORY)` → `sandbox_enter(fd, SANDBOX_KILL_ON_DENY)` → `close(fd)`。ここから先は信頼できない byte を扱う。
6. 入力を全部読む（上限は今の Files と同じ `THUMB_FILE_MAX`。超えたら status 3）。
7. 復号して縮小し、PPM（P6、comment に `--stamp`）を fd 1 に `write` で書く（stdio を使わない）。
8. `exit`。

### 4.3 復号の library と link

- 画像: libpng-compat・libjpeg-compat・libgif-compat・libz-compat と、Files の `picture.c`（Image Viewer と共有の復号）。縮小は Files の今の
  code（`thumb.c` の縮小）を移す。
- PDF: libpdf（今の Files は dlopen）。command は **直接 link** する（dlopen は sandbox の中では file を開けないので使えない）。
  libpdf は埋め込まれていない font の代わりに `/usr/share/fonts/keiland*.ttf` を **必要になった時に開く**（`userland/base/libpdf/font.c` 1202〜）。
  sandbox の中では開けないので、libpdf に「代わりの font を memory で渡す」口（`pdf_font_provider` の設定、userland の library の API の追加）を足し、
  command は手順 4 で PDF の時だけ、`/usr/share/fonts` にある代わりの font（今の image では `keiland.ttf` 0.9 MB・`keiland-mono.ttf` 0.3 MB、
  serif と bold・italic の file は無いので libpdf は family の既定に落ちる）を読んでから sandbox に入る。代わりの font が無い文字は描かない（今と同じ）。
- link は今の動的 link のまま（`-z now` で全部の記号を起動の時に結ぶので、sandbox の中で dynamic linker が file を開く場面は無い）。静的 link は
  前例がある（`platform/amd64/vmunix.mk` の `posix-phase5-helper`・`phase85-curses-test`: `crt0.o` と `libc.o` を `-static -T platform/amd64/user.ld`）が、
  compat の library と libpdf は `.so` だけで `.a` が無いので、source を command 用に compile し直す規則が要る（§8 の H6）。
- **呼び出し側は復号の library を link しなくなる**（Files の縮小表示、Settings の tile）。復号の bug が呼び出し側の process で起きる道が消える。

### 4.4 sandbox に入れない時

- zedBSD では、`sandbox_enter` が ENOSYS（古い kernel）や失敗を返したら **復号せずに** status 70 で終わる（隔離無しで復号しない）。
- Linux・FreeBSD は §7。

## 5. 呼び出し側

（Files・Settings の今の形の調べは §5.1、変え方は §5.2）

### 5.1 今の形（2026-10-05 の main を読んだ）

| 呼び出し側 | 今 | 場所 |
| --- | --- | --- |
| Files の縮小表示 | `fm_thumb_get` が 1 file を頼み、**main loop の次の回**（`fm_thumb_tick`）で cache の記録を読むか、無ければ `fm_image_thumbnail` で復号して縮小（256 の正方形に contain）し、記録に書く。**復号は Files の main の thread で同期に走る**（その間 window は止まる） | `userland/desktop/files/thumb.c` 191・228、`files.h` 663（`FM_THUMB_SIDE`） |
| Files の復号 | `fm_image_load`: file を全部読み（64 MiB まで）、PPM・PGM・PNG・JPEG（`kl_picture_jpeg`）・GIF の最初の 1 枚（`kl_picture_gif_first`）・PDF の 1 頁目（`fm_thumb_pdf`、libpdf を dlopen）。画素の上限は一辺 8192・16M 画素 | `thumb.c` 84・39〜43、`thumb-cache.c` 151 |
| Files の cache の記録 | `$XDG_CACHE_HOME`（無ければ `~/.cache`）`/keiland/thumbnails/<path の SHA-256>.ppm`、P6 と comment（`# keiland-thumbnail` と元の file の更新時刻・大きさ）。2000 個を超えたら古い物から 1800 個まで消す | `thumb-cache.c` 8〜25・270・357・446・515〜 |
| Files の Quick Look・preview の欄 | `fm_peek_picture` が `fm_image_thumbnail(path, side)`（Quick Look は 1600 の正方形）。cache しない | `peek.c` 98〜115、`ui-preview.c` 52・760 |
| Files の Today の hero | `fm_image_load(app->wallpaper)`（背景の file を全部の大きさで復号） | `ui-home.c` 410 |
| Settings の背景の tile | `look_thumbnail`: loader の thread で file を読み、`kl_wallpaper_decode`（WS138）で全部を復号して 240×150 に cover で縮小 | `userland/desktop/settings/look.c`（`look_thumbnail`） |
| compositor の背景 | prefetch・loader の thread で `kl_wallpaper_decode`（WS138） | `userland/desktop/wayland/glass.c`（§8 の H4） |

### 5.2 変え方

- 共通の小さな helper（`libkeiland` ではなく、呼び出し側の program に compile する `userland/desktop/preview/preview-client.c`）:
  1. cache の記録の一時 file（`<記録>.tmp.<pid>`、0600）を `O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC` で開く。入力を `O_RDONLY | O_CLOEXEC | O_NONBLOCK` で開き、
     通常の file でなければ断る（FIFO・device を command に渡さない）。
  2. `posix_spawn`（libc の、`vfork` の上に作った物）の file actions で入力を 0、一時 file を 1 に dup2 し、他は CLOEXEC で閉じる。環境は空。argv は
     §4.1。command の path は `KEILAND_LIBEXECDIR "/keiland-preview"`（`userland/desktop/paths.h`。文字列の `/usr/libexec` は boundary の C4 に反する）。
  3. 時間の上限（Files の縮小表示 5 秒、PDF 10 秒）まで `waitpid(WNOHANG)` を見て、超えたら `SIGKILL` して回収する（sessiond の `greeter_wait`・
     `greeter_kill`、`sessiond/greeter.c` 646〜728 の形。ただし Files は main loop を止めないので、次の段落の tick で見る）。
  4. status 0 なら一時 file を記録の名前に rename し、記録を読む（今の `fm_thumb_cache_read`）。それ以外は一時 file を消し、縮小表示を「無し」にする
     （今の失敗と同じ扱い: icon を出す）。同じ file で続けて失敗しないよう、失敗も cache に「失敗」の印の記録（空の PPM と stamp）で残す。
- **待ち方**: Files は今、main loop の中で同期に復号している。command にすると起動と終了で数十 ms かかるので、同期に待たない:
  `fm_thumb_tick` は「走っている子が無ければ起動する」「走っている子を `waitpid(WNOHANG)` で見る」だけを行い、終わった回に記録を読んで slot に入れ、
  window を描き直す。同時に走らせる子は 2 つまで（Files の main loop の 1 回で 1 つ起動）。時間の上限は tick の時刻で見る。
  これで、今の「大きな画像・PDF の復号の間 window が止まる」も無くなる。
- Quick Look・preview の欄: 同じ helper で `--width=1600 --height=1600`（preview の欄は今の side）、出力は cache の folder の一時 file（読んだら消す）。Today の hero も同じ
  （`--width=1920 --height=1080 --fit=contain`）。これで **Files は復号の library を一切 link しない**（libpng・libjpeg・libgif・libz-compat と
  `picture.c`、libpdf の dlopen が Files から消える）。
- Settings の tile: loader の thread で同じ helper（`--width=240 --height=150 --fit=cover`）。記録は Files と同じ cache に、名前に大きさと fit を
  混ぜた SHA-256 で置く（Files の 256 contain の記録と別の名前）。Settings も復号の library を link しなくなる（WS138 で足した link を外す）。
- 1 つの command は 1 file。

### 5.3 残る危険

- **出力の記録は信頼できない**: 乗っ取られた command は fd 1 に任意の byte を書ける。呼び出し側はその記録（P6 の PPM）を読むので、呼び出し側に残る
  解釈の code は「P6 の header と comment と画素の数を確かめて写すだけ」の小さな reader（今の `fm_thumb_cache_read`・`cache_header`、`thumb-cache.c` 270・588、
  一辺 1024 の上限、§4.1 の上限に合わせて 4096 に）に限る。この reader は p004 で見直し、壊れた記録・大きすぎる記録・短い記録の host 試験を足す。
- **fd 0 の file の中身**: command は入力を読むだけで、他の file には届かない。入力の file を書き換える道も無い（fd 0 は読み取りで開く）。
- **資源**: memory（`RLIMIT_AS`）・CPU（`RLIMIT_CPU`）・出力の大きさ（`RLIMIT_FSIZE`）・時間（呼び出し側の SIGKILL）・同時の数（呼び出し側が 2 つまで）で抑える。
- **kernel の bug**: 許す call（read・write・mmap など）の kernel の実装の bug は sandbox の外に出る道になりうる。表を小さく保つことで面を減らす。

## 6. 試験（p002 以降で作る）

- host（Linux）: command の復号と縮小の正しさ（今の Files・Settings の host 試験の画像を入れて出力の PPM を比べる）。
- zedBSD の kernel の試験の program（`userland/tests/sandboxtest`、T1 が QEMU で流す）:
  1. `sandbox_enter` の後、表の外の call を 1 つずつ試し、全部 EPERM（KILL 無しの mode）であること: open・openat・stat・socket・socketpair・pipe・dup・
     fcntl(F_DUPFD)・fork・vfork・execve・thread_create・ioctl・sysctl・kill(親)・chdir・chroot・setuid・setrlimit・recvmsg・mount。
  2. 許す call が動くこと: read・write・pread・lseek・fstat・close、mmap（匿名）、munmap、brk、clock_gettime、getentropy。
  3. 引数の制限: `mmap` の file の map、`PROT_EXEC` の mmap・mprotect が EPERM。
  4. KILL の mode: 表の外の call で process が SIGKILL で終わり、親の `waitpid` がそれを見ること。
  5. root: sandbox の中の process の root が `/var/empty` であること（試験の口: 断られる前の段で `getcwd` は断るので、kernel の klog の行と、
     試験用の build の sysctl で確かめる、詳細は p002）。
  6. 2 回目の `sandbox_enter` が EPERM、thread が 2 つある時 EBUSY。
- command の試験: 正常な PNG・JPEG・GIF・PPM・PDF、壊れた file、大きすぎる file、時間の上限（終わらない入力の代わりに試験用の build の「無限 loop」の
  flag）、`RLIMIT_AS` を超える入力。
- 「埋め込まれた攻撃」の代わり: 試験用の build で、復号の途中で open・socket・fork を呼ぶ code を通す flag を command に持たせ、process が SIGKILL で
  終わり、出力が無く、何も開かれていないことを確かめる。

## 7. Linux・FreeBSD

Keiland の Files・Settings は Linux・FreeBSD でも動くので、command も 3 つの OS で build する。OS ごとの隔離の部分は command の中の `zedbsd/sandbox.c`・
`linux/sandbox.c`・`freebsd/sandbox.c` に分け、OS ごとの Makefile（`Makefile`・`Makefile.linux`・`Makefile.freebsd`）が 1 つを選ぶ。
`keiland-os-boundary` の L1（OS の macro の block は `*/zedbsd`・`*/linux`・`*/freebsd` の directory の中だけ）に合い、Files の `freebsd/mounts-freebsd.c`・
`mntent/mounts-mntent.c` と同じ形なので、checker の変更は要らない。libkeiland-backend の op にはしない（B3: backend は compositor だけが使う）。

| OS | 当てる物 | 足りない所 |
| --- | --- | --- |
| zedBSD | §3 の sandbox mode（root を `/var/empty` に、表）と rlimit | — |
| Linux | `prctl(PR_SET_NO_NEW_PRIVS)`、seccomp-bpf（§3.2 と同じ表を BPF で: `read`・`write`・`pread64`・`lseek`・`fstat`・`close`・`mmap`（匿名だけ、`PROT_EXEC` 無し）・`munmap`・`mprotect`・`brk`・`exit_group`・`rt_sigreturn`・`clock_gettime`・`getrandom`・`futex`、違反は `SECCOMP_RET_KILL_PROCESS`）、可能なら `unshare(CLONE_NEWUSER \| CLONE_NEWNS \| CLONE_NEWNET)` の後に空の directory へ chroot、rlimit | 利用者の名前空間が無効な system（Ubuntu の AppArmor の制限など）では chroot ができない。seccomp が path の call を全部断るので要件 4 は満たす。chroot ができなかったことは exit の前に status では区別しない（§8 の H5） |
| FreeBSD | `procctl(PROC_NO_NEW_PRIVS_CTL)`、`security.bsd.unprivileged_chroot` が 1 なら `/var/empty` へ chroot、`cap_enter()`（Capsicum: path の解決・socket の作成・fork を断る）、rlimit。fd 0・1 は `cap_rights_limit` で read・seek・fstat と write・fstat に絞る | `unprivileged_chroot` の既定は 0 なので、多くの system で chroot はできない。Capsicum が全部の path を断るので要件 4 は満たす |

## 7.1 Linux・FreeBSD で隔離が当てられない時

seccomp・Capsicum が使えない（kernel が古い、設定で無効）時は、zedBSD と同じく復号せずに status 70 で終わり、呼び出し側は縮小表示を出さない（icon）。

## 8. 人間の判断が要る点

| ID | 問い | 案 | 理由 |
| --- | --- | --- | --- |
| H1 | kernel に `sandbox_enter`（root の権限無しで root を空の directory に替え、同時に表の外の call を全部断る）を足してよいか（新しい system call 170 と `include/uapi/sandbox.h`） | 足す | 要件 3・4 を kernel で保証する道が他に無い。HAL は不変。root を要らないのは exec と path の解決を同時に断るため安全（§3.1） |
| H2 | command を呼び出し側の uid のまま走らせるか、専用の uid（例 `_preview`）に落とすか | 呼び出し側の uid のまま | sandbox mode の中では uid で得られる物が無い（path・signal・ptrace・socket を全部断る）。専用の uid に落とすには set-user-ID の helper か root の仲介（sessiond）が要り、その helper 自身が新しい攻撃面になる |
| H3 | 断った call の扱い: その場で SIGKILL か、EPERM を返すか | command は SIGKILL（`SANDBOX_KILL_ON_DENY`）。kernel は両方を持つ（試験は EPERM の mode を使う） | 乗っ取った code に「何が断られるか」を探らせない。command は小さく、表の外の call を呼ばないことを試験で確かめられる |
| H4 | 対象: Files の縮小表示と Settings の背景の tile（案）。compositor の背景の復号（WS138 で compositor の中で PNG・JPEG を復号している）もこの command に任せるか | v1 は Files と Settings。compositor の背景は v2 の候補（出力が縮小でなく画面の大きさの RGB になり、起動の時間が延びる） | compositor は session の中で最も権限の大きい process なので、そこでの復号の bug は重い。ただし背景は利用者が自分で選んだ file に限られ、縮小表示（受け取った file を開いただけで復号される）より危険が小さい |
| H5 | Linux で利用者の名前空間が無く chroot ができない、FreeBSD で `unprivileged_chroot` が 0 の時、seccomp・Capsicum だけで復号してよいか | よい（path の call が全部断られるので要件 4 は満たす） | 「最低でも chroot」を文字どおりに守ると、多くの Linux・FreeBSD の system で縮小表示が出なくなる |
| H6 | 静的 link にするか | しない（動的 link と `-z now` で、sandbox に入る前に全部を読み込む） | ws.md の案は静的 link だったが、sandbox に入る前に library は全部読み込まれ、入った後は file を開けないので、静的 link で減る攻撃面は無い。静的 link の前例はあるが、compat の library・libpdf・libtruetype を command 用に compile し直す規則が要る |

## 9. 段（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | kernel の sandbox mode（§3）と libc の wrapper、`userland/tests/sandboxtest`（§6 の 1〜6）。T1 で QEMU | H1・H3 |
| p003 | `keiland-preview`（§4）と libpdf の font の口、host 試験。Linux・FreeBSD の隔離（§7） | p002、H2・H5・H6 |
| p004 | Files・Settings を command に切り替え（§5）、復号の library の link を外す。T1 で QEMU | p003、H4 |
| p005 | 全文規約の見直し（code を作る WS の最後の段） | p004 |

## 結果

（設計の第 1 版。Q1 の確認待ち）
