<!-- awesome-plan project=zedbsd record=ws080-p004 -->

# ws080-p004: LLP64 の UAPI の header の生成

Status: in-progress（計画と方式まで。実装は未着手。2026-10-04 ユーザーの全担当のラップアップで中断）
Disposition: normal
Parent: [WS080](../ws.md)
Queue: q676（P1 generation14、worktree `/home/awe/zedBSD-worktrees/p1`、branch `agent/p1`、base fc43184）
Approval: 2026-10-04 ユーザー「PE/COFFローダも進めてオーケーです。」（q676: p004 → p005）。p001 の review・D1〜D17・HAL の A1/A2 は未決（ws.md の Resume point のとおり p004 は待たずに始めてよい）。

## 範囲

design.md §15.3 の生成の script と生成物。kernel・HAL・toolchain・`include/uapi` は変えない（読むだけ）。

| 物 | path |
| --- | --- |
| 生成の script（Python 3） | `userland/desktop/w64/tools/gen-llp64-uapi.py` |
| 入力の一覧（header の名前、`exclude NAME`） | `userland/desktop/w64/tools/llp64-uapi.list` |
| 生成物（tree に置く） | `userland/desktop/w64/include/zuapi/<元の名前>.h` |
| 試験 | `userland/desktop/w64/tests/llp64-uapi/run.sh`（出力は `BUILD`、既定 `build/ws080-p004/`） |

## 受け入れの条件

1. 生成物の全部を include した TU が `clang --target=x86_64-pc-windows-msvc -ffreestanding -nostdlibinc -std=c11 -Wall -Wextra -Werror -fsyntax-only` で通る（生成物の `_Static_assert` が LLP64 の layout = LP64 の値を保証）。
2. 照合の TU（元の `include/uapi` と生成物を両方 include、`--target=x86_64-unknown-zedbsd`）で、全ての struct・union の `sizeof`・`_Alignof`、全ての名前のある member の `offsetof`・`sizeof`、全ての macro の値と `sizeof` が一致する（script が `--emit-crosscheck` で書く）。
3. `gen-llp64-uapi.py --check` が tree の生成物と差が無いことを確かめる。複写した UAPI に変更（member の型・member の追加・macro の値）を入れると `--check` が非 0 で止まる（UAPI の変更の検出の試験）。
4. 生成は決定的（absolute path・clang の版を出力に入れない）。

## 方式（2026-10-04 に決めた。実装の時はこれに従う）

LP64 の正は共有の clang（`build/llvm/bin/clang`、読むだけ）の `--target=x86_64-unknown-zedbsd -ffreestanding -nostdlibinc -Iinclude -DKERN_USER_ABI_LP64 -std=c2x -w`。sysroot に頼らない（UAPI は stddef/stdint だけを要る）。

1. **pass A（構造）**: 一覧の header を include した probe を `-fsyntax-only -Xclang -ast-dump=json` で読む。RecordDecl（名前のあるもの、`typedef struct {...} x_t` は typedef の名前）・FieldDecl・EnumDecl・TypedefDecl を集める。定義の file は JSON の location の `"file"` を文書の順に追って決める（`includedFrom` の file は無視。`loc` の後の現在の file が decl の file）。一覧に無い header の record は出さない。匿名の struct・union の member は、record の `inner` で直前に現れた匿名の RecordDecl に対応する（clang の順）。bitfield・`long double` は未対応として止まる（今の UAPI に無い）。
2. **pass M（macro）**: 同じ probe の `-E -dD` を読み、`# n "file"` の行で file を追い、一覧の header の object-like の macro（値が空の include guard を除く）を集める。同じ名前は最初の 1 つ。
3. **pass B（値）**: 生成した probe を同じ flag で JSON に dump する。項目ごとに `enum : unsigned long long { probe_N = (unsigned long long)(式) };` を 1 行に置き、`ConstantExpr` の `"value"` を読む（2026-10-04 に試作で確かめた）。式: record の `sizeof`・`_Alignof`、member の `offsetof`・`sizeof`、macro・enum の定数。macro の型は `__typeof__((M)) probe_tN;` の VarDecl の `desugaredQualType`。整数の定数式でない macro（文字列・pointer・member の別名）は compile の error の行（probe の行番号）で外して再試行し（最大 3 回）、外した名前を生成物の先頭の comment に列挙する。record・member の行の error は致命。
4. **型の写し**: typedef を自分の表で canonical へ解き、`char`→`char`、`signed/unsigned char`→`int8_t/uint8_t`、`short`→16、`int`→32、`long`・`long long`→`int64_t`、`unsigned long`・`unsigned long long`→`uint64_t`、`_Bool`→`uint8_t`、`float`・`double` はそのまま、pointer（関数 pointer を含む）→`uint64_t`、`enum X`→全ての値が 0 以上なら `uint32_t`、でなければ `int32_t`、配列は要素を写して次元を保つ、`struct/union X`→`struct/union zuapi_X`。元の型と違う member には行末に元の型の comment（`/* ino_t */`）。
5. **layout**: member の間と末尾の詰め物を全て `uint8_t _zuapi_padN[n]` で明示する。LP64 の offset が自然な配置より前に来る record（`dirent_record` の `packed, aligned(4)`）は `__attribute__((packed, aligned(N)))`、LP64 の整列が自然より大きければ `aligned(N)`。匿名の member は内側の最初の名前のある member の offset から配置する。
6. **名前**: struct・union は `zuapi_<tag>`、macro と enum の定数は `ZUAPI_<名前>`（全て macro として literal で出す。型は LP64 の幅を保つ suffix: int は無し、unsigned int は `U`、long・long long は `LL`、unsigned long 系は `ULL`。負の値は `(-N)`、最小値は `(-N - 1)`）。typedef は出さない。file 名は元と同じ（`zuapi/stat.h`）、include guard は `ZUAPI_STAT_H`。他の生成物の record を使う header はその生成物を include する（一覧に無ければ致命）。
7. **一覧の初版**（一般の POSIX の部分。GPU・input・wlan・netif・audio 等は後で一覧に足す）: errno、syscall、fcntl、stat、time、mman、unistd、dirent、thread、process、wait、usync、poll、signal、socket、un、limits、resource、statvfs、termios、select、ioctl、auxv、rename、tls。

## 確認

| 確認 | 結果 |
| --- | --- |
| 試作（`build/ws080-p004/proto/`）: `enum : unsigned long long` の `ConstantExpr` の `"value"` と `__typeof__` の VarDecl の `desugaredQualType` が JSON に出る | 確かめた（`ST_RDONLY` → value 1、型 `unsigned long`） |
| 受け入れの 1〜4 | 未実施（実装は未着手） |

## Resume point

上の方式で script・一覧・生成物・試験を作る。途中の成果は無い（commit は phase.md だけ）。調べた UAPI の注意: `signal.h` の `struct sigaction` は匿名の union、`siginfo_t`・`mcontext_t`・`ucontext_t` は typedef の struct、`dirent.h` の `dirent_record` は `packed, aligned(4)`、`fcntl.h` に `typedef char ..._check[...]` の静的な検査（typedef は出さないので無視）、`time.h` の `timespec.tv_nsec`・`resource.h` の `rusage` は `long`（→ `int64_t`）。bitfield は 58 の header のどこにも無い。
