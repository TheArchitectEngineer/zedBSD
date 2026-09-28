<!-- awesome-plan project=zedbsd record=ws073p026 -->

# ws073-p026: libc の qsort を O(n log n) の introsort に（BUG-090）

Status: cleared（2026-09-28）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-090](../../bugs/BUG-090.md)（main の依頼、2026-09-28）

## 目的と受け入れ

`src/libc/stdlib-extra.c` の `qsort`・`qsort_r` は 1 byte ずつ swap する挿入の sort で O(n²)。O(n log n) の sort にし、要素の大きさと
配列の alignment が許す最も広い word で交換する。C 標準の意味（安定は要らない）を保つ。同じ code を使う `heapsort`・`mergesort` も直す。
受け入れ: host 試験（host の qsort と同じ key の並び、等しい key の組の集合の一致。random・sorted・reverse・all-equal・organ-pipe・
重複の多い入力、n = 0〜1e6、要素 1・3・4・8・16・24 byte、ASan・UBSan）、旧実装との時間の比較、amd64 の image の build（warning 0）と
boot test、安い guest の確認。

## 変更

- 新しい `src/libc/sort.c`（coding-style の全文、`style-check.py` 0）: `qsort`・`qsort_r`・`heapsort`・`mergesort`。
  - `qsort`・`qsort_r`: introsort。pivot は両端と中央の median of three（65 要素以上は 3 つの median of three の median）、Hoare 型の
    分割（両側の走査が pivot と等しい要素で止まるので all-equal も中央で割れる）、短い側を再帰・長い側を loop（再帰の深さは log n）、
    深さが 2⌊log2 n⌋ を超えた範囲は heapsort、16 要素以下は挿入。走査は範囲で上限を持つので、順序にならない比較関数でも範囲の外を触らない。
  - 交換と複写の単位: `(address | size)` が `unsigned long` の倍数なら machine word、4 の倍数なら 32 bit、他は byte（`may_alias` の typedef）。
  - `heapsort`: 本物の in-place の heapsort（以前は `qsort` を呼ぶだけ）。size 0 は EINVAL。
  - `mergesort`: 安定な bottom-up の merge sort（8 要素の run を挿入で、配列の写しと交互に merge、既に順の run の組は一括で複写）。
    以前も挿入の sort で安定だったので安定性は保つ。新たに、写しを確保できないと ENOMEM で -1（BSD と同じ）。size 0 は EINVAL。
- `src/libc/stdlib-extra.c`: 旧 `swap_bytes`・`insertion_sort`・`insertion_sort_r`・`qsort`・`qsort_r`・`heapsort`・`mergesort` を削除
  （`bsearch` は残す）。`tests/style-diff.py` の変更行 0。
- `src/libc/libc.mk`: `ZEDBSD_LIBC_USER_EXTRA_SOURCES` と `ZEDBSD_LIBC_SOURCES` に `src/libc/sort.c`（全 platform と sysroot がこの一覧を使う）。
- 意味の変化: `qsort` は以前（挿入の sort）は結果として安定だったが、今は等しい要素の順は不定（C 標準どおり）。安定を暗に頼る呼び手が
  あれば等しい要素の順が変わる（呼び手の全数の監査は未実施）。

## 試験

- 追加: [tests/qsort-host.sh](../tests/qsort-host.sh)（[qsort-host.c](../tests/qsort-host.c)、[qsort-host-sort.c](../tests/qsort-host-sort.c) が
  `sort.c` を `zed_*` の名前で host 向けに C89 `-pedantic -Werror` で compile）、[tests/qsort-guest.sh](../tests/qsort-guest.sh)
  （[qsort-guest.c](../tests/qsort-guest.c) を image の libc.so に link して guest で実行、`OLD_LIBC` で旧 libc.so と比較）。
- image の build: [tests/build-image-noclang.sh](../tests/build-image-noclang.sh)（[config-amd64-zdesktop-noclang.mk](../tests/config-amd64-zdesktop-noclang.mk)
  = zdesktop の config から clang と libcxx の package を除く）。worktree の `build/llvm-source` が共有への symlink なので、clang の package は
  host の LLVM を作り直し（BUG-089）、libcxx の package の `cp -al build/llvm-source` は symlink を写して共有の source を patch しうるため除いた
  （`make -n` で確認）。libc の変更には要らない。この image には lldb・libc++ が無い。

## 検証（host と QEMU。実機は未実施）

- host 試験 `sh plan/ws073/tests/qsort-host.sh`（出力 `build/ws073-p026/qsort-host/`）: plain・ASan・UBSan（`-fsanitize=undefined,alignment`、
  recover なし）の 3 つとも `QSORT:PASS`。範囲: 要素 1・3・4・8・16・24 byte × 8 つの pattern（random、sorted、reverse、equal、organ-pipe、
  duplicates（key 10 種）、sawtooth、nearly-sorted）× n = 0〜16 の全部と 17・31・32・33・63・64・65・100・127・1000・4097・10000・100000・1e6
  （1e6 は qsort を全 size、qsort_r・heapsort・mergesort は 8・24 byte）× 4 つの sort。host の qsort と位置ごとの key と要素の多重集合が一致、
  mergesort は等しい key の元の順を保つ。size 0・n 0・比較関数 NULL・mergesort の巨大な n（ENOMEM）、+1・+2・+4 byte ずらした配列（UBSan の
  alignment が狭い単位への切り替えを確認）、乱数を返す比較関数（要素が失われず範囲の外を触らない、ASan）、McIlroy の adversary
  （n=1e4: 481851 比較 = 3.71 n log2 n、n=1e5: 6124108 = 3.83 n log2 n。素の quicksort なら二次）。
- 時間（host、x86_64、`-O2`、比較関数は key の読み出しの呼び出し。ms）:

  | 入力 | n | size | 旧 | 新 | glibc | 新の比較 / n log2 n |
  | --- | --- | --- | --- | --- | --- | --- |
  | random | 30000 | 8 | 1987 | 4.63 | 4.20 | 1.14 |
  | reverse | 30000 | 8 | 3947 | 5.23 | 1.57 | 1.80 |
  | organ-pipe | 30000 | 8 | 1974 | 3.55 | 1.61 | 1.18 |
  | duplicates | 30000 | 8 | 1762 | 2.99 | 3.07 | 0.92 |
  | sorted | 30000 | 8 | 0.19 | 2.23 | 1.44 | 0.86 |
  | random | 1e6 | 8 / 24 / 3 | — | 226 / 239 / 112 | 187 / 264 / 145 | 1.13 / 1.12 / 0.96 |
  | reverse | 1e6 | 8 | — | 199 | 70 | 1.59 |

  全体は `build/ws073-p026/qsort-host/time.txt`。旧は整列済みの入力だけ O(n) で速い（n=30000 で 0.19 ms 対 2.23 ms）。
- 各 arch の compile: `make -j16 sysroots`（amd64・i386（`-march=i386 -msoft-float`）・arm64 の sysroot の libc.a に `sort.c.o`、`-Wall -Wextra -Werror`）
  warning 0。i386 の `sort.c.o` に post-i386 の opcode 無し（`libc-opcode-check` と同じ grep）。
- image: `sh plan/ws073/tests/build-image-noclang.sh build/amd64` 成功（`build/amd64/hdd-image.img`、`check-amd64-native-image: OK`）。
  zedBSD 自身の source の warning 0。log の warning は外部 package（openssl・openssh の deprecated など）、既存の
  `userland/base/noct/noct/src/core/interpreter.c:2395` の -Wreturn-type と `Makefile:769`（他の agent の build の log にも同じものがある）だけ。
- boot test: `OUTPUT=build/ws073-p026/boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` PASS（`build/ws073-p026/boot-test/login.png`）。
- guest（QEMU、`tests/g.sh start build/amd64/hdd-image.img`、SSH）: `OLD_LIBC=build/ws073-p026/old-libc.so sh plan/ws073/tests/qsort-guest.sh`
  `QSORT-GUEST:PASS`。新: 1e6 × 8 byte random 431 ms、sorted 247 ms、reverse 445 ms、250000 × 24 byte（key 1000 種）qsort 29 ms・qsort_r 32 ms・
  heapsort 71 ms・mergesort 48 ms（安定）。旧 libc.so（main の `build/amd64/dynamic/libc.so`、2026-09-26 の build を `LD_LIBRARY_PATH` で）と
  n=20000 で比較: random 1819 ms → 11 ms、reverse 3591 ms → 6 ms、5000 × 24 byte の 4 つの sort 115〜128 ms → 0〜1 ms。
- PDF の host 試験 `sh plan/ws079/tests/run-pdf-render.sh 300`: `run-pdf-render: ok`（ただしこの試験は host の libc の qsort で動くので、
  今回の変更を通らない。回帰の確認としての意味は薄い）。
- libc の既存の試験で qsort を扱うもの: `plan/*/tests`・`userland/base/tests` に無し（`plan/ws074/tests/host-tree.c` は browser の host 試験で host の qsort）。

## 未実施・残り

- 実機、amd64 以外の image の起動（i386・arm64 は sysroot の compile だけ）。PDF Viewer の guest での描画の時間の再測定（GUI の操作が要る）。
- `userland/base/libpdf/raster.c` の 84 行目の comment（「C library's qsort is an insertion sort」）は古くなった。WS079 の file なので触れていない。
  raster の自前の merge sort を libc の qsort・mergesort に戻すかは WS079 の判断。
- qsort の安定を暗に頼る呼び手の監査（上の「意味の変化」）。
