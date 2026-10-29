<!-- awesome-plan project=zedbsd record=ws281-survey-source -->

# WS281 の source の棚卸し（設計の入力、2026-10-29）

Parent: [WS281](ws.md)。q1666 の P5 の配下の調べ（Opus、調べだけ、途中で止めた）の結果を P5 がまとめた。tree は main 61630bb0b。
「推測」は確かめていない。p001 の設計はこれを出典の一つにして行を引き直す。

## 1. 今の malloc（`src/libc/heap.c` 1021 行。kernel の写しは `src/kern/heap.c` に別）

- header（heap.c:31-40）: magic・state・capacity・used・物理の前後・free list の前後。LP64 で 64 B（推測）、ILP32 で 32 B。alignment 16（:26）、最小 16 B、0 は 1 B（:505-507）。割るのは残りが header＋16 B 以上の時（:191-193）。
- free list は 1 本・順序なし・LIFO（:175-183）。malloc は頭から first fit（:514-516）、無ければ伸ばして全部をもう一度（:517-522）。free は両隣と即合体（:696-708）、pointer の確かめは定数時間（:272-330、BUG-033 で直した）。
- realloc（:712-766）: 縮めるはその場、次が空いて足りればその場で伸ばす、他は新しく取り memcpy。calloc（:654-670）は常に memset（新しい page は 0 なので削れる）。aligned（:542-652）は揃う位置を探し前の余りを free に。
- libc の口: `aligned_alloc`（`src/libc/stdlib-extra.c:149-162`、size が alignment の倍数でないと EINVAL）、`posix_memalign`（:169-181）、`reallocarray`・`reallocf`・`recallocarray`（:214-224）、`freezero`（`src/libc/openbsd.c:158-168`）。`malloc_usable_size`・`memalign`・`valloc`・`<malloc.h>` は無い（gtk4 の patch 0004 が無いことを前提にする）。
- 大きな割り当ても全部 brk（mmap の経路なし）。最初 64 KiB（`userland/base/libc/posix.c:8292-8299`）、伸ばすのは 64 KiB 単位（:10018-10041）。前の break が heap の端でなければ伸ばせず、他の code の sbrk の後は NULL が続く。OS へ返さない。
- lock は大域の 1 本（heap.c:912-949）、`word_lock`（pthread.c:157-171、CAS → `LIBC_LOCK_SPIN` 既定 100 回 → usync、:2981-3003, 3726-3747）。再帰しない。
- **fork の穴**: `__pthread_fork_prepare`（pthread.c:703-719）は heap の lock を取らず、child は `heap_lock = 0` に戻すだけ（:759）。他の thread が heap を書いている最中の fork で child の heap が壊れうる（推測）。
- thread の cache なし。double free・知らない pointer は `errors++` だけで abort しない（:686-689, 730-732）。canary なし。`heap_allocator_validate`（:806）、統計・失敗の注入・observer は libc の外で使う所なし。
- interpose: `userland/base/libc/interpose.list` の物だけ（`-Bsymbolic-functions`、platform/amd64/vmunix.mk:1129-1132）。`strdup`（`src/libc/string.c:561-565`）と aligned の関数は heap を直に呼ぶ: program が malloc・free だけを置き換えると食い違う（推測）。

## 2. kernel の口

- brk（`src/kern/vm/vmspace-unmap.c:1043-1156`）の上限 `vm_layout.brk_limit`（vmspace.c:136-141）: LP64 4 GiB、32 bit は 256 MiB。RLIMIT_DATA も効く。commit は strict。
- anon の mmap は region の線形の list（vmspace-map.c:671、vmspace-region.c:378）: mmap の数が増えると費用が増える。`MAP_NORESERVE` は効かない（syscall.c:3725-3732）。
- `madvise` は libc の中で引数を確かめるだけで何もしない（posix.c:3247-3282）。DONTNEED・FREE で memory は返らない。
- anon の fault は 1 page ずつ kernel が 0 にする（vmspace-fault.c:692-698）、fault-around は anon に効かない（:653-657）。
- TLS: 動的な libc は `_Thread_local`（pthread.c:321, 369）、`__tls_get_addr` の thread pointer を register で読むのは amd64・arm64・riscv64・armv7 だけ（rtld.c:6342-6388）、他は syscall。静的な libc は条件つき（static-tls.c:219-254）。**thread の cache は arch と静的・動的の組で syscall の費用を負う**。

## 3. 利用者

- C++: libc++abi の `operator new` は malloc、delete は free（`stdlib_new_delete.cpp:50, 119`）、aligned の new は `aligned_alloc`（`aligned_alloc.h:35-57`）。
- libvulkan: 往復ごとに writer の realloc（wire.c:196）・reply の malloc（context.c:799）・free（wire.c:423）、自前の揃え（objects.c:92-126）。短命の同じ大きさの繰り返し（thread の cache 向き、推測）。
- compositor: pthread_create が 13 か所、thread は 10 本を超える見込み（推測）。libbrowser は自前の arena と GC の heap（推測、未精査）。

## 4. 計測と試験

malloc の benchmark・heap.c の host 試験は無い（syscall-smoke.c:174、dyntest.c:73 だけ）。手本: `plan/ws299/tests/cpuloop.c`（rdtsc）、`plan/ws299/tests/host/memcpy-test.sh`。`clock_gettime` は 1 ms 刻み（vDSO なし）なので TSC か長い反復で測る。

## 5. 設計で決める問いの候補

1. 大きな割り当てを mmap に分けるか（32 bit の 256 MiB と sbrk の穴が消える。region の線形の list の費用）。
2. OS へ返すか（madvise が効かないので munmap か brk の縮め）。
3. thread の cache を、register の読めない arch・静的な libc でどうするか。
4. fork の prepare で heap の lock（arena の lock 全部）を取る形に直すか（今の穴、直すのは推し）。
5. `malloc_usable_size`・`<malloc.h>` を足すか（ABI と interpose の一覧）。
6. strdup・aligned を置き換えられる symbol 経由に直すか。
7. 統計・失敗の注入・observer を残すか。
8. header（LP64 64 B）を小さくするか。
9. 新しい page の calloc の memset を省くか。

作るべき試験: heap の host 試験（正しさ・境界・aligned・realloc・validate・乱数の列）、thread の stress（別の thread の free を含む）、fork の child、大きさの分布の micro bench（thread 1・4・16、TSC）、断片化、libvulkan の往復の模倣、全 arch の build の warning 0。

## 6. 未調査（調べを止めた、2026-10-29 Q1 の指示）

i386・mips・ppc・sparcv9・m68k の thread pointer の読み、32 bit の brk_start の実の値（elf.c:1283 付近）、256 MiB に当たる program の有無、libc の中の利用者の詳細、libbrowser の arena、Keiland の割り当ての分布（未計測）、signal の経路の malloc、usync の費用。
