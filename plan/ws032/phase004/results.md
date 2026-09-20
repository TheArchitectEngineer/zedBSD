# ws032-p004 結果（進行中）: libc・ヘッダの不足補完（2026-09-21）

q315-i04。**p004 は一度で閉じない**。不足は後続 Phase の実ビルドが見つけるたびにここへ
戻して閉じる。以下は p006（OpenSSL）のビルドが実際に要求したものだけで、推測で足した
ものは無い。

## 1. 追加・修正したもの

| 対象 | 内容 | 根拠 |
| --- | --- | --- |
| `libc/include/sys/param.h`（新規） | `NBBY`、`MAXPATHLEN`、`MAXNAMLEN`、`MAXHOSTNAMELEN`、`howmany`/`roundup`/`rounddown`、`powerof2`、`MIN`/`MAX` | POSIX 外だが移植性のある Unix ソフトが広く include する。OpenSSL `include/internal/sockets.h` |
| `libc/include/netinet/tcp.h`（新規） | `TCP_NODELAY` のみ | **POSIX がこのヘッダに要求する名前**。同 sockets.h |
| `include/uapi/netinet.h` | `in_addr_t`、`in_port_t`、`INADDR_LOOPBACK` を追加 | **いずれも POSIX が `<netinet/in.h>` に要求**。欠落していた |
| `include/uapi/socket.h` | `SO_KEEPALIVE` を追加 | **POSIX が `<sys/socket.h>` に要求**。欠落していた |
| `libc/include/netdb.h` | `NI_MAXHOST`、`NI_MAXSERV` を追加 | POSIX 外だが getnameinfo の buffer 寸法に広く使われる |
| `libc/include/arpa/inet.h` + `userland/base/libc/socket.c` | `inet_ntoa` を宣言・実装（`inet_ntop` の上に、thread-local な静的領域を返す） | **POSIX が要求**。宣言も実装も無かった |
| `libc/include/atomic-compiler.h` | C11 atomic の facade を `__atomic_*` から **clang の `__c11_atomic_*`** へ変更 | 下記 §2 |

## 2. 直した不具合: `<stdatomic.h>` が `_Atomic` オブジェクトに対して使えなかった

`libc/include/atomic-compiler.h` は C11 の atomic 操作を GCC の `__atomic_*` 組込みへ
写していたが、**clang は `_Atomic` 修飾されたオブジェクトをこれらの組込みに渡すことを
拒否する**（`_Atomic(int) *` invalid）。`_Atomic` こそが C11 の atomic 型なので、
**標準的な `<stdatomic.h>` の使い方が 1 つも通らない**状態だった。

確認（`atomic_fetch_add_explicit` / `atomic_load_explicit` /
`atomic_compare_exchange_strong` / `atomic_flag_*` / `atomic_init` / `atomic_exchange`）:

- 修正前: すべて compile error。
- 修正後: すべて compile（`-std=c11 -Wall -Wextra`）。

clang 用の対応する組込みは `__c11_atomic_*` で、これは `_Atomic` 修飾を要求する。
zedBSD は project 所有の LLVM だけでビルドするため、この写像でよい。
`atomic-compiler.h` を include するのは `libc/include/stdatomic.h` だけ、
`<stdatomic.h>` の tree 内利用者は `userland/base/tests/posix-r2-remaining.c` だけで、
そこも `_Atomic` オブジェクトを使っているため、修正で通るようになる側である。

## 3. 記録した制限（実装したと誤解しないこと）

- `TCP_NODELAY`：**transport は IPPROTO_TCP のオプションを受け付けない**
  （`setsockopt`/`getsockopt` は EOPNOTSUPP）。ヘッダは名前を定義するだけで、
  機能を提供しない。ヘッダのコメントにも書いた。
- `SO_KEEPALIVE`：同様に kernel は未実装（`src/kern/net/socket.c` が扱うのは
  `SO_REUSEADDR`、送受信 buffer、timeout、`SO_ERROR` など）。
- `MAXHOSTNAMELEN`：POSIX の `HOST_NAME_MAX` が `<limits.h>` に無いため、
  伝統的な 256 を直接書いた。`<limits.h>` へ `HOST_NAME_MAX` を入れるのは
  POSIX 準拠側（WS001）の作業として残す。

## 4. 停止: `struct sigaction` が POSIX の形をしていない（UAPI 変更が必要）

`include/uapi/signal.h` の `struct sigaction` は

```c
struct sigaction {
	uint64_t sa_handler;      /* POSIX: void (*sa_handler)(int) */
	sigset_t sa_mask;
	uint32_t sa_flags;        /* POSIX: int */
	uint32_t __sa_reserved;
	uint64_t sa_restorer;
};
```

で、`sa_handler` が**関数ポインタではなく整数**であり、`sa_sigaction` が**存在しない**。
そのため移植性のあるソフトの `sa.sa_handler = handler;` は必ず compile error になる
（OpenSSL `crypto/ui/ui_openssl.c:582`。OpenSSH はこれより多くの箇所で使う）。

kernel（`src/kern/syscall.c:6502-6528`）と libc（`userland/base/libc/signal.c:200`）は
この整数表現を前提に読み書きしている。POSIX の形にするには、layout（LP64 で 8 byte、
ILP32 でも union なら 8 byte のまま）を保ったまま公開型を変え、kernel 側と libc 側の
参照を新しい名前へ直す必要がある。**これは UAPI の変更**であり、Queue の停止条件に
当たるため、ここで停止してユーザーへ提示する。

提案（承認された場合の最小変更）:

```c
struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
		uint64_t __sa_handler_value;   /* kernel と libc が使う生の値 */
	};
	sigset_t sa_mask;
	uint32_t sa_flags;
	uint32_t __sa_reserved;
	uint64_t sa_restorer;
};
```

binary layout は不変。影響は `src/kern/syscall.c` の 5 箇所と
`userland/base/libc/signal.c` の 2 箇所を `__sa_handler_value` へ書き換えるだけ。
`sa_flags` の型（POSIX は `int`）と `sa_sigaction` の意味論（`SA_SIGINFO`）は別問題として残る。

代替案は OpenSSL 側にキャストのパッチを当てることだが、同じ問題は sigaction を使う
すべての外部ソフトで再発するため、対症療法にしかならない。

### 4.1 ユーザー承認と実施（2026-09-21）

ユーザーが「UAPI を POSIX 準拠に直す」を選択した。実施した内容:

- `include/uapi/signal.h`: `struct sigaction` を匿名 union にし、`sa_handler`
  （`void (*)(int)`）、`sa_sigaction`（`void (*)(int, siginfo_t *, void *)`）、
  `__sa_handler_value`（kernel と libc が使う生の 64bit 値）を重ねた。
  **binary layout は不変**（LP64・ILP32 とも 8 byte、以降の field も不変）。
- `include/uapi/signal.h`: `SIG_DFL`/`SIG_IGN` を `0ULL`/`1ULL` から
  `((void (*)(int))0)`/`((void (*)(int))1)` へ変更。POSIX は sigaction の
  disposition として使えることを要求する。kernel は元から `(uintptr_t)` へ
  cast して比較しているので影響しない。
- `src/kern/syscall.c`（5 箇所）と `userland/base/libc/signal.c`（2 箇所）を
  `__sa_handler_value` へ変更。

この変更は、**ツリー各所にあった zedBSD 専用の回避コードを不要にした**。回避コードは
「`sa_handler` が整数なので関数ポインタを `(uint64_t)(uintptr_t)` で詰める」形で、
次の箇所にあった。すべて素直な代入へ戻した:

`libc/xsi-process.c`（3 箇所）、`userland/base/libc/timer.c`、
`userland/base/common/pager.c`、`userland/base/dd/main.c`、`userland/base/sh/main.c`、
`userland/base/timeout/main.c`（`ACTION_HANDLER` の arch 分岐ごと削除）、
`userland/base/tests/syscall-smoke.c`（4 箇所）、`userland/base/tests/posix-r2.c`（2 箇所）。

型の取り違えを 1 件検出した: `userland/base/libc/timer.c` と
`userland/base/tests/syscall-smoke.c` の 3 箇所は `SA_SIGINFO` を立てながら
3 引数の handler を `sa_handler` へ入れていた。新しい struct では型が合わないため
compile error になり、`sa_sigaction` へ直した。**整数 field がこの誤りを隠していた**。

検証: `make vmunix` と `make world` が warning 0 で通る（既存の全ユーザーランド）。
`build/amd64/dynamic/libc.so` も再生成。`git diff --check` PASS。

### 4.2 この変更で壊れる、ツリー外の箇所（記録）

- `userland/base/noct/noct/src/api/api-term-ansi.c` と `src/cli/cli-repl.c` は
  **upstream Noct 側**に `#if defined(NOCT_TARGET_ZEDBSD)` の回避分岐を持っており、
  その分岐は compile できなくなる（`#else` 側が正しくなった）。noct は既定で
  選択されない（`USERLAND_noct_DEFAULT = n`）ため本 Phase のビルドには現れないが、
  **Noct upstream 側の修正が要る**。zedBSD 側の patch で潰すことはしていない。
- `userland/base/tests/syscall-smoke.c` は本変更と無関係に compile できない状態だった。
  `catch_fault_siginfo` が `ucontext_t *context;` を初期化せずに使っており
  （`context = opaque_context;` が無い）、`-Werror=uninitialized` で落ちる。
  HEAD の内容と同一であることを確認した既存の不具合で、本 Phase では直していない。
