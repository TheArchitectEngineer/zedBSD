<!-- awesome-plan project=zedbsd record=ws168-p002 -->
# ws168-p002: kernel の sandbox_spawn と libc の wrapper と sandboxtest

Phase ID: `ws168-p002`
Parent: [WS168](../ws.md)
Status: in-progress（2026-10-06 P1、q804 で再開。libc・試験まで書いた。libc の起動の isatty（ioctl TCGETS）の扱いが判断待ち、下の「2026-10-06」）
設計: [p001](../phase001/phase.md) §3（H1〜H7 はユーザーが案のとおり承認、2026-10-05 夜。`sandbox_spawn` の system call 170 と `include/uapi/sandbox.h` の追加を含む）

## 済んだこと（commit は下の SHA）

- `include/uapi/sandbox.h`（新）: `struct sandbox_spawn`（64 byte、size が版）、`struct sandbox_fd`、`SANDBOX_ALLOW_THREADS`、`SANDBOX_SPAWN_DENY_ERRNO`、
  `SANDBOX_FD_MAX` 16、`SANDBOX_SPAWN_SIZE_MAX` 4096。`include/uapi/syscall.h` に `KERN_SYS_sandbox_spawn = 170`。
- `include/kern/sandbox.h`・`src/kern/sandbox.c`（新）: 許す call の bitmap（§3.3 の基本の集合と THREADS）、`sandbox_permits`（mmap は匿名・private・fd -1・
  PROT_EXEC 無し、mprotect は PROT_EXEC 無し）、`sandbox_deny`（klog に 8 行まで、既定は SIGKILL、DENY_ERRNO なら EPERM）。
- `struct process` に `sandbox`、`process_release` で解放。`syscall_dispatch_body` の先頭で sandbox の process の call を確かめる（redispatch も通る）。
- `src/kern/exec.c` に `process_spawn_sandbox`（`exec_sandbox_start`）: image の確かめ（script は ENOEXEC、PT_INTERP は ENOEXEC）、set-ID の bit を
  効かせない credential、`cwdi` を NULL に、上限（AS・CPU・FSIZE は親と要求の小さい方、NOFILE 16、CORE 0）、新しい address space、環境の無い stack、
  fd の対応、sandbox を付けてから publish。
- `src/kern/syscall.c` に `sys_sandbox_spawn_call`（要求の版の読み、知らない byte は 0 でなければ E2BIG、知らない flag・allow は EINVAL、fd の対応の確かめ、
  argv の写し）。
- `platform/{amd64,arm64,sparcv9,x68k}/vmunix.mk` の kernel の source に `src/kern/sandbox.c`。
- 確認: kernel（`build/ws161`、amd64）の link が -Werror で通った。style-check: `sandbox.c`・`include/kern/sandbox.h` は 0、`include/uapi/sandbox.h` に
  `_Static_assert` の前の paragraph-comment が 1 件（直す）。

## 未着手（再開の点）

1. 小さな直し: `include/uapi/sandbox.h` の `_Static_assert` の前に comment。`src/kern/exec.c` の `struct exec_sandbox_build` を file の先頭の型の所へ移す。
   `sys_sandbox_spawn_call` の 3 節以上の条件を行に分ける（規約 §6）。arm64・sparcv9・x68k の kernel の build を確かめる。
2. libc: `userland/base/libc/posix.c` に `sandbox_spawn()`（`call(KERN_SYS_sandbox_spawn, request, 0…)`）、`include/libc/sandbox.h`（`<uapi/sandbox.h>` と
   宣言）。**sysroot の `usr/include` は toolchain の lock の下にあるので、`<sandbox.h>` を sysroot に入れるのは main の許可が要る**。それまで試験は
   `#include "include/libc/sandbox.h"` で読む。
3. `userland/tests/sandboxtest`（親、動的 link、class basic）と `sandbox-child`（静的 link、新しい class `static`、`usr/libexec`。`platform/amd64/vmunix.mk` に
   `posix-phase5-helper` と同じ形の規則（sysroot の `crt0.o`・`libc.o`、`-static -T user.ld`））。子は argv[1] で: echo（fd 0→1）、fds（fd 0〜15 の有無）、
   limits（getrlimit）、signals（既定か）、allowed（匿名の mmap・munmap・malloc・clock_gettime・getentropy・getpid・nanosleep）、memory（PROT_EXEC・file の
   map・匿名の MAP_SHARED・mprotect の exec が EPERM）、deny NAME（open・stat・getcwd・chdir・socket・pipe・dup・fcntl・fork・execve・ioctl・kill・setuid・
   setrlimit・sandbox_spawn を 1 つずつ、`__syscall6` で直接）。出力は snprintf と write だけ（stdio は isatty の ioctl で断られる）。
   親は §6 の 1〜7（要求の確かめ・子の状態・DENY_ERRNO の EPERM・許す call・引数の制限・既定の SIGKILL・動的 link の image の ENOEXEC）を流し、
   `SANDBOX PASS` を出す。静的 libc の起動が集合の外の call を呼ばないかは、子の最初の run の klog（`SANDBOX deny`）で確かめる。
4. `plan/ws168/tests/config-amd64-sandbox.mk`（Files の image ＋ sandboxtest・sandbox-child・runas）と `plan/ws168/tests/sandbox-p002.sh`、T1 の依頼。

## 2026-10-06（P1、q804 で再開）

- 再開の点 1 の直し: `include/uapi/sandbox.h` の `_Static_assert` の前に comment、`struct exec_sandbox_build` を `src/kern/exec.c` の型の所へ、
  `sys_sandbox_spawn_call` の 6 節の条件を 2 つの判定に分けて行に（ba7015eeb）。amd64・arm64（rpi4）の kernel の build（-Werror）。
  sparcv9・x68k: `ZEDBSD_CONFIG=/dev/null` では `src/hal/pmem-constraints.c` が無く（この Phase と無関係の今の tree の状態）、platform の config も要るので未実施。
- 2: libc の `sandbox_spawn()`（`userland/base/libc/posix.c`）と `include/libc/sandbox.h`（c114c814d）。sysroot には入れない（toolchain の lock）。
  試験は `#include "include/libc/sandbox.h"`。libc.so に `sandbox_spawn` が export されるのを `llvm-nm -D` で確かめた。
- 3: `userland/tests/sandbox-child`（新しい class `static`、`usr/libexec`、`platform/amd64/vmunix.mk` に `AMD64_USER_STATIC_COMMAND` と image への追加）と
  `userland/tests/sandboxtest`（basic、`sandbox-child` を require）。§6 の 1〜6（要求の確かめ 11、子の状態 7、DENY_ERRNO の 23 の call、許す call と threads、
  memory の 4、既定の KILL と集合を守る子）。どちらも build（warning 0）。
- 4: `plan/ws168/tests/config-amd64-sandbox.mk`・`sandbox-p002.sh`（root と root でない利用者で `SANDBOX PASS`、klog の `SANDBOX deny`）。

### 判断待ち（Q1 に報告済み）

静的 link の子の libc の起動（sysroot の libc.o の `__libc_init`）が `isatty(STDOUT_FILENO)` = `ioctl(fd, TCGETS)` を呼ぶ。ioctl は集合の外なので、既定の
KILL では**全ての子が main の前に SIGKILL** で終わる（preview の command も）。案: (a) sandbox の中では `ioctl(fd, TCGETS)` だけを sandbox の確かめが
ENOTTY で答える（file にも driver にも届かない、klog も書かない、他の ioctl は今どおり断る）、(b) libc が sandbox の process では isatty を飛ばす（sysroot の
libc.o の作り直し = toolchain、main の許可が要る）、(c) 試験の子だけ自前の `_start`（preview の command に効かないので勧めない）。推奨は (a)。
試験の `test_kill` の 2 つ目（KILL の子が集合を守って走る）がこの判断の確かめになる。
