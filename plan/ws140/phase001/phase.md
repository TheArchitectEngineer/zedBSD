<!-- awesome-plan project=zedbsd record=ws140-p001 -->

# ws140-p001: object の数・依存の数・初期化の順・dlsym の印の上限を無くす

Status: planned
Disposition: normal
Parent: [WS140](../ws.md)
Queue: none
設計: [ws.md](../ws.md) の D1〜D5・D8・D9
依存: なし（ws.md の U2・U3 のユーザーの決めは要る）
時限の目安: 実装 4 h、試験の program 2 h

## 範囲

- **入る**:
  - object の chunk の列（D1・D2）、依存の可変長（D3）、初期化の順の list（D4）、dlsym の訪問の印（D5）。
  - U5 が「直す」なら、`rtld_object_removals++` を TLS の分岐の外へ出す 1 行。
  - 多数の依存の試験 program と、その build・実行の script。
- **入らない**: handle・TLS module・dtv・静的な TLS の並び（p002）。dyntest の書き換え（p002）。他の固定の上限（ws.md の U3）。
- **所有する path**: `src/rtld/rtld.c`、`src/rtld/rtld.h`、`plan/ws140/`。

## 始める前に読む物

1. `AGENTS.md`、`plan/guardrail.md`、[ws.md](../ws.md)（事実・設計・残る危険）。
2. `plan/coding-style.md` の §3（forward 宣言）・§4（局所の宣言）・§6（条件の中で関数を呼ばない）・§8・§10（comment）・§14（checklist）。
   新しい関数と、新しく書く行・変える行はこれに従う（D9）。
3. `src/rtld/rtld.c` の次の部分（行番号は main 908a030）。
   - 先頭〜260 行: struct と大域の変数。
   - `initialize_object`（1840-1879）、`__rtld_process_fini`（870-900）。
   - `new_object`（2540-2577）、`find_identity`（2517-2537）。
   - `parse_dynamic`（2770 から。switch の loop は 2835 から、`DT_NEEDED` は 2845-2849、名前の確かめは 3224-3233）、`load_dependencies`（3646-3658）。
   - `unload_object_locked`（4756-4848）、`remove_initialization_record`（4851-4870）、`finalize_object_unlocked`（4873-4897）。
   - `rtld_dlsym_common`（4900-4944）、`lookup_handle_graph`（5032-5082）。
   - `loader_lock`（1608-1636、再帰の lock）。

## 0. 作業の前の確かめ（build が sysroot を作らないこと）

subagent は toolchain（`toolchain/llvm/sysroot.mk` を含む）を build しない（AGENTS.md）。
dynamic の object は sysroot の stamp（`$(ZEDBSD_SYSROOT_AMD64)/.zedbsd-sysroot-complete`）に依存する（`platform/amd64/vmunix.mk:793`、arm64 は `platform/arm64/vmunix.mk:338`）。
sysroot の path は worktree の `build/<arch>/sysroot`（`toolchain/llvm/sysroot.mk:5-7`）。

```sh
C=ZEDBSD_CONFIG=config/ci/config-amd64.mk
make -n $C BUILD=build/ws140-p001 build/ws140-p001/dynamic/ld.so 2>&1 | grep -c 'sysroot.mk\|zedbsd-sysroot-complete'
```

- 0 なら進む。
- 1 以上なら sysroot が作り直される。止めて Q1 に知らせる（worktree の `build/amd64/sysroot` の stamp は 2026-10-04 00:08、main は 10:13）。
- arm64 も同じく確かめる。worktree に `build/arm64` が無ければ、arm64 の build は Q1 に相談する。
  - 案: main の `build/arm64/sysroot` を読み取り専用で使う。`make … ZEDBSD_SYSROOT_ARM64=/home/awe/zedBSD-claude1/build/arm64/sysroot` で `make -n` の確かめが 0 になる時だけ。
  - 相談の結果が出なければ、arm64 の build は「未実施」と書く。

style の基準を取る（D9）:

```sh
python3 plan/tools/style-check.py --summary src/rtld/rtld.c > build/ws140-p001-style-before.txt
```

## 手順

### 1. object の chunk の列（D1・D2）

1. `rtld.h`: `RTLD_OBJECT_MAX` を消して `#define RTLD_OBJECT_CHUNK 32U` を置く。
   `RTLD_NEEDED_MAX` を消して `#define RTLD_NEEDED_INLINE 16U` を置く。
2. `rtld.c`: struct の群に `struct rtld_object_chunk { struct rtld_object_chunk *next; struct rtld_object slots[RTLD_OBJECT_CHUNK]; };` を足す。
   comment に「chunk は process の終わりまで解放しない。要素の address は動かない」と書く。
3. 大域の `objects[RTLD_OBJECT_MAX]` を、次の 2 つに置き換える。
   - `static struct rtld_object_chunk object_chunk_first;`（bss。今の `objects` と同じ大きさ）
   - `static struct rtld_object_chunk *object_chunk_last = &object_chunk_first;`
   comment に次を書く。
   - `object_count` は使ったことのある slot の数。
   - 列と数は、loader の lock の下か起動の時に伸ばす。
   - lock の外の読み手（`__rtld_dl_iterate_phdr`）は、数を acquire で読む。
4. 関数 `static struct rtld_object *object_at(unsigned index)` を足す。
   - 最初の chunk から、`index / RTLD_OBJECT_CHUNK` 回 `__atomic_load_n(&chunk->next, __ATOMIC_ACQUIRE)` でたどる。
   - `&chunk->slots[index % RTLD_OBJECT_CHUNK]` を返す。
   - 呼ぶ側は `index < object_count` を守る。
5. 関数 `static void object_chunk_grow(void)` を足す。
   - `tls_map(sizeof(struct rtld_object_chunk))` で chunk を取る。取れなければ `rtld_fatal("cannot allocate shared-object table")`。
   - `tls_map` は 0 で埋まった匿名の memory を返すので、memset は要らない。
   - `__atomic_store_n(&object_chunk_last->next, chunk, __ATOMIC_RELEASE)` でつなぎ、`object_chunk_last = chunk` とする。
6. `new_object`（2547-2560）の書き換え。
   - 空いた slot を `object_at(i)` で探す。
   - 無ければ、`object_count != 0 && object_count % RTLD_OBJECT_CHUNK == 0` の時だけ `object_chunk_grow()` を呼ぶ。
     **0 の時は呼ばない**。最初の chunk は bss にあるので、呼ぶと全ての process が要らない chunk を 1 つ map してしまう。
   - その後 `__atomic_store_n(&object_count, object_count + 1U, __ATOMIC_RELEASE)` で数を増やす。
   - `rtld_fatal("too many shared objects")` を消す。
7. `objects[i]`・`&objects[i]` の参照を全て `object_at(i)` にする。順（slot の番号の順）は変えない。大域の symbol の探索の順が変わらないようにするため。
   今ある場所（main 908a030）は次のとおり。
   - 283-294（`debug_map_publish`）
   - 957-962（`__rtld_dlopen`）
   - 1077-1078（`__rtld_dl_iterate_phdr`）: loop の前に `count = __atomic_load_n(&object_count, __ATOMIC_ACQUIRE)` で数を読み、`count` まで回す。
   - 1131-1135（`__rtld_dladdr`）
   - 1460-1463・1479-1482・1490-1493（`rtld_main`）
   - 2523-2531（`find_identity`）
   - 2547-2560（`new_object`）
   - 4180-4181（大域の探索）
   - 4645-4646（`resolve_tls_symbol` の付近）
   - 5006-5007（`lookup_global_optional`）
   - 5046-5051（`lookup_handle_graph`、手順 4 で扱う）
   - 確かめ: `grep -n 'objects\[\|&objects' src/rtld/rtld.c` が 0 行。
8. `tls_modules[RTLD_OBJECT_MAX + 1U]`・dtv の `RTLD_OBJECT_MAX + 1U`・`layout_static_tls` の `order[RTLD_OBJECT_MAX + 1U]` は、p002 で変える。
   この Phase では、`rtld.c` の先頭に `#define RTLD_TLS_MODULE_MAX 33U`（p002 で消す仮の名前）を置き、`RTLD_OBJECT_MAX + 1U` を置き換えるだけにする。
   - 置き換えは 6 か所: `tls_modules[]`（182）、dtv（554・578・580・636）、`order[]`（1729）。
   - `register_tls_module` の `tls_module_count == RTLD_OBJECT_MAX`（3618）は、`tls_module_count + 1U == RTLD_TLS_MODULE_MAX` にする。
     文字列 `"too many TLS modules"` はこの Phase では残す（p002 で消す）。
   - 理由: 今の id は 1〜32 で、`tls_modules[]` の 33 項目に収まる。この Phase では TLS の挙動を変えない。
   - 確かめ: `grep -n 'RTLD_OBJECT_MAX' src/rtld/*.c src/rtld/*.h` が 0 行。

### 2. 依存の可変長（D3）

1. `struct rtld_object` の `needed_offset[RTLD_NEEDED_MAX]`・`needed[RTLD_NEEDED_MAX]` を、次の 4 つにする。
   - `uint32_t needed_offset_inline[RTLD_NEEDED_INLINE]`
   - `struct rtld_object *needed_inline[RTLD_NEEDED_INLINE]`
   - `uint32_t *needed_offset`
   - `struct rtld_object **needed`
   外の配列の memory を持つため、`void *needed_mapping` と `size_t needed_mapping_size` も足す。
   既存の `object->needed[i]`・`object->needed_offset[i]` の書き方は、そのまま compile できる。
2. `parse_dynamic` の書き換え。
   - 始めに `object->needed = object->needed_inline; object->needed_offset = object->needed_offset_inline;` とする。
   - 既存の switch の loop の前に、`DT_NEEDED` を数える loop を足す。
     **loop の範囲は、既存の switch の loop（2835-2841）と全く同じにする**（`i < object->dynamic_count` で、`DT_NULL` で止まる）。範囲が違うと外の配列が溢れうる。
   - 数が `RTLD_NEEDED_INLINE` を越えたら、`tls_map(count * (sizeof(struct rtld_object *) + sizeof(uint32_t)))` で取る。
     取れなければ `rtld_fatal("cannot allocate dependency table")`。
   - pointer の配列を先頭に、offset の配列をその後ろに置く（pointer の整列のため）。`needed_mapping`・`needed_mapping_size` に記録する。
   - `DT_NEEDED` の case の `if (object->needed_count == RTLD_NEEDED_MAX) rtld_fatal(...)` を消す（数は数え済み）。
3. `unload_object_locked`（4756-）の書き換え。今は、`dependencies[RTLD_NEEDED_MAX]` に写してから `rtld_memset(object, 0, …)` している。
   この関数は fini の間 lock を放す（4793-4795）。その間に、fini や他の thread が `dlopen` して、memset した slot を再び使うことがある。
   だから次の順を守る。
   1. 局所に次の 4 つを置く: `struct rtld_object *inline_copy[RTLD_NEEDED_INLINE]`・`struct rtld_object **dependencies`・`void *mapping`・`size_t mapping_size`。
   2. 依存を写す所（今の 4788-4790）で、次のようにする。
      - 外の配列が無い（`needed_mapping == NULL`）時: `inline_copy` に写して `dependencies = inline_copy`。
      - 外の配列がある時: `dependencies = object->needed` とし、`mapping`・`mapping_size` を局所に取る。
      - どちらの時も、object の側の `needed`・`needed_count` はまだ NULL・0 にしない（fini の間に使われうる）。
   3. memset（今の 4843）で object は消える。外の配列は object の外の memory なので残る。
   4. memset の後の、依存の参照の数を減らす loop と、再帰の unload（4845-4847）は、今のまま `dependencies` を使う。
   5. **再帰の loop が終わった後で** `tls_unmap(mapping, mapping_size)` を呼ぶ（NULL なら何もしない）。
4. U5 が「直す」なら、`rtld_object_removals++`（4815-4816、TLS の分岐の中）を、TLS の分岐の外（unload する全ての object が通る所）へ移す。

### 3. 初期化の順の list（D4）

1. `struct rtld_object` に `struct rtld_object *init_prev` と `struct rtld_object *init_next` を足す。
   大域の `initialization_order[]`・`initialization_count` を、`initialization_head`・`initialization_tail` にする。
   comment に「loader の lock の下だけで変える」と書く。
2. `initialize_object`:
   - `initialization_count == RTLD_OBJECT_MAX` の確かめを消す。
   - constructor を呼んだ後に、`loader_lock()` → 尾に足す → `loader_unlock()` とする。critical section の前後に空行を置く（規約 §5）。
   - 尾に足す手順:
     1. `object->init_prev = initialization_tail; object->init_next = NULL;`
     2. 前の尾の `init_next` を object にする。尾が無ければ head を object にする。
     3. 尾を object にする。
3. `remove_initialization_record`（lock の下で呼ばれる）: list から外し、外した後に `init_prev`・`init_next` を NULL にする。
   - list に入っていない object（`object != initialization_head && object->init_prev == NULL`）なら何もしない。2 回呼ばれても壊れない。
4. `__rtld_process_fini`: 次を繰り返す。
   1. `loader_lock()`。
   2. `object = initialization_tail`。NULL なら `loader_unlock()` して終わる。
   3. `remove_initialization_record(object)`。
   4. `loader_unlock()`。
   5. その object の fini_array（後ろから）と fini を、今と同じ順で呼ぶ。
   - 次の object の pointer を前もって覚えない（fini の中の dlclose が list を変えるため）。
   - fini を 2 回呼ばないため、既存の `finalize_object_unlocked`（`initialized` を 0 にしてから呼ぶ）を使ってよい。使ったら「結果」に書く。
5. 確かめ: `grep -n 'initialization_order\|initialization_count' src/rtld/rtld.c` が 0 行。

### 4. dlsym の訪問の印（D5）

1. `struct rtld_object` に `uint32_t lookup_mark` を足す。大域に `static uint32_t lookup_generation;` を置き、comment に「loader の lock の下だけで使う」と書く。
2. `rtld_dlsym_common` から `visited` を消す。lock を取った後で `lookup_generation` を 1 増やす。0 になったら次のようにする。
   - 全 object の `lookup_mark` を 0 にする（`object_at` で回す）。
   - `lookup_generation = 1` にする。
3. `lookup_handle_graph` の引数 `uint32_t *visited` を消す（forward 宣言の 470 行も直す）。
   - 範囲の確かめ（5046）を `object == NULL || !object->active || object->unloading` にする。
   - bitmask の 2 行を、次の code にする: `object->lookup_mark == lookup_generation` なら 0 を返す。違えば `object->lookup_mark = lookup_generation` にして進む。

### 5. 試験の program（`plan/ws140/tests/`）

C は全文規約に従う。script は `plan/ws073/tests/p038/run-tls-check.sh` の書き方に倣い、先頭で repo の root に `cd` する。

1. `many-lib.c`: 関数が 1 つだけの library の source。名前と値は、compile の時の object 形の macro で決める。
   - 例: `-DMANY_SYMBOL=many_value_07 -DMANY_NUMBER=8` なら、`int many_value_07(void)` が 8 を返す。
   - 関数形の macro は使わない（規約 §14）。
2. `many-hub.c`: 中身の無い library。`int hub_marker(void)` だけを持つ。依存（leaf）を `DT_NEEDED` に持たせるためだけに使う。
3. `rtld-many.c`: 試験の program。`libmany00.so`〜`libmany39.so` の 40 個を直接 link する（実行 file の `DT_NEEDED` が 40）。
   - 各段は、成功で `RTLD-MANY step=<名前> ok` を出す。失敗で `RTLD-MANY step=<名前> FAIL <詳細>` を出して exit 1 する。
   - 最後に `RTLD-MANY: PASS` を出す。

   段は次のとおり。
   - **startup**: 起動できた（ここに来た）。
   - **direct**: `many_value_00() == 1` と `many_value_39() == 40`（直接の呼び出し。32 番目より後の object への再配置）。
   - **global**: `dlopen(NULL, RTLD_NOW)` の handle で、`dlsym` が `many_value_00`〜`many_value_39` の全てを見つけ、値が番号 + 1。
   - **count-startup**: `dl_iterate_phdr` で数えた object が 43 以上（実行 file・40 個・libc.so・ld.so）。数を出す。
   - **hubs**: `dlopen("libhub0.so")`・`"libhub1.so"`・`"libhub2.so"` を開く（各 40 個の leaf を `DT_NEEDED` に持つ）。
     `dl_iterate_phdr` の数が、count-startup の数 + 123 以上（hub 3 ＋ leaf 120）。
   - **graph**: 次を確かめる。見つけた関数は呼んで、値も確かめる。
     - `dlsym(hub0, "leaf_value_039")` が 40 番目の leaf の関数。
     - `dlsym(hub2, "leaf_value_119")` が見つかる。
     - `dlsym(hub0, "leaf_value_119")` は NULL（hub0 の graph に無い）。
   - **close**: 3 つを `dlclose`。`dl_iterate_phdr` の数が count-startup の数に戻る。
   - **reopen**: `libhub2.so` をもう一度開き、`leaf_value_100` を見つけて呼べる。閉じる。
4. `build-many.sh BUILD OUT`: host で target の clang を使って作る。
   - 変数: `sysroot=$PWD/build/amd64/sysroot`、`cc="$PWD/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"`。
   - **compile**（全ての .c）: run-tls-check.sh の動的の program の compile と同じ flag に、`-DMANY_SYMBOL=… -DMANY_NUMBER=…` を足す。
     flag は `-nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -c`。
   - **共有の library の link**: `$cc -m64 -nostdlib -shared -Wl,-soname,<名前> -Wl,--hash-style=gnu,-z,now,-z,relro <.o> [-L"$OUT" -l:<依存>…] -o "$OUT/<名前>"`。
     - `libmany00.so`〜`libmany39.so`（`many-lib.c` を 40 回）。
     - `libleaf000.so`〜`libleaf119.so`（`many-lib.c` を `MANY_SYMBOL=leaf_value_NNN`・`MANY_NUMBER=NNN+1` で 120 回）。
     - `libhub0.so`（leaf 000〜039 を `-l:` で link）、`libhub1.so`（040〜079）、`libhub2.so`（080〜119）。
     - lld は `-l` で渡した物を既定で全て `DT_NEEDED` にする（`--as-needed` を付けない）。
   - **program の link**: `platform/amd64/vmunix.mk:1760-1767` の dyntest の規則と同じ flag に、library の指定を足す。
     - flag: `-m64 -nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" <.o>`。
     - 足す物: `-L"$OUT" -l:libmany00.so … -l:libmany39.so -L"$BUILD/dynamic" -Wl,-rpath-link,"$BUILD/dynamic" -l:libc.so`。
     - rpath は付けない（実行の時は `LD_LIBRARY_PATH` を使う。下の 5）。
   - 最後に、`build/llvm/bin/llvm-readelf -d "$OUT/rtld-many" | grep -c NEEDED` が 41（40 ＋ libc.so）であることを確かめて表示する。
   - 全ての file（library 163・program 1）を 1 つの archive にする: `(cd "$OUT" && tar --format=ustar -cf ../rtld-many.tar *.so rtld-many)`。
     guest の `pax -r` が ustar を読める。
5. `rtld-many.sh [BUILD]`（T が流す。guest は先に起動しておく）の流れ。
   1. `build-many.sh` を呼ぶ。
   2. `guest.py run 'rm -rf /root/ws140 && mkdir -p /root/ws140'`。
   3. archive を `guest.py put` で 1 回だけ送る（`/root/rtld-many.tar`）。file ごとの `put` は 1 回ずつ SSH を張るので遅い。
   4. `guest.py run 'cd /root/ws140 && pax -r -f /root/rtld-many.tar'`。guest の image には pax がある（`plan/ws035/tests/config-amd64-userland.mk:28`）。
   5. `guest.py run 'cd /root/ws140 && chmod +x rtld-many && LD_LIBRARY_PATH=/root/ws140 /root/ws140/rtld-many'` を走らせ、出力を `OUT/rtld-many.txt` に残す。
      - 実行は絶対 path で行う。
      - `dlopen` は requester が NULL で rpath を見ない（rtld.c:968）ので、`LD_LIBRARY_PATH` が要る（1368 行・`open_dependency`）。
   6. `RTLD-MANY: PASS` の有無で、`rtld-many: PASS|FAIL` を出す。
6. `config-amd64-rtld.mk`: `include plan/tools/guest/config-amd64-ssh.mk` の 1 行と、目的の comment。
   image は `plan/tools/guest/test-image.sh plan/ws140/tests/config-amd64-rtld.mk BUILD` で作る（出力は `BUILD/hdd-image.img`）。

## 確かめ（この Phase で自分が行う物。QEMU は起動しない）

```sh
B=build/ws140-p001; C=ZEDBSD_CONFIG=config/ci/config-amd64.mk
make $C BUILD=$B $B/dynamic/ld.so $B/dynamic/libc.so $B/dynamic/dyntest 2>&1 | tee $B.log
grep -c 'warning:' $B.log                 # 0
make $C BUILD=$B dynamic-userland-check   # "zedBSD amd64 dynamic userland artifacts: PASS"
size $B/dynamic/ld.so                     # bss を記録（今 217,944）
# arm64: 手順 0 の確かめが通った時だけ。通らなければ「未実施」
sh plan/ws140/tests/build-many.sh $B build/ws140-p001-many   # NEEDED 41 の表示、warning 0
python3 plan/tools/style-check.py plan/ws140/tests/*.c       # 違反 0
python3 plan/tools/style-check.py --summary src/rtld/rtld.c > build/ws140-p001-style-after.txt
grep -n 'RTLD_OBJECT_MAX\|RTLD_NEEDED_MAX\|objects\[\|initialization_order' src/rtld/*.c src/rtld/*.h   # 0 行
grep -n '"too many shared objects"\|"too many dependencies"\|"initialization order overflow"' src/rtld/rtld.c   # 0 行
git diff --check
```

- style: before と after の数を関数ごとに比べ、増えていないこと。新しい関数と、新しく書いた行の違反は 0（D9）。
- host で ld.so を動かす試験は無い（ld.so は zedBSD の system call を直接呼ぶ）。動作の確かめは T の guest で行う。

## 試験の依頼

p001 だけでは T に依頼しない。p002 の後に、p001 と p002 の試験をまとめて 1 回で依頼する（[protocol](../../agents/protocol.md)「試験の担当 T1」）。
p001 の commit を Q1 に merge 依頼してから p002 に進む。p001 は T の結果が出るまで cleared にしない（in-progress のまま、「実装済み・試験待ち」）。

## 受け入れの条件

1. 手順の grep の確かめが全て 0 行。amd64 の build が warning 0。arm64 は手順 0 の結果に従い、build したか未実施かを書く。`dynamic-userland-check` が PASS。
2. T の guest で `rtld-many: PASS`（startup・direct・global・count-startup・hubs・graph・close・reopen）。p002 とまとめた依頼で流す。
3. T の guest で、p002 で書き直した `dyntest` が最後まで走る（p002 とまとめた依頼）。
4. bss の値、変えた関数の一覧、style の前後の数を「結果」に書く。

## 結果

（未実施）

## 着手前に直す点（2026-10-04 ユーザーの判断の反映、Q1。詳細は [ws.md](../ws.md) の「判断」）

- U3: **他の固定の上限も入れる**。`tlsdesc_argument[64]`（object ごとの TLSDESC の再配置の数）・`phdr[64]`・`RTLD_NAME_MAX` 64 もこの WS で動的にする（または上限を十分に大きくし、越えたら dlerror で返す。決めは担当の設計で、phase.md に書いてから実装）。範囲・受け入れ・試験（rtld-many に TLSDESC 65 個以上・長い名前の dlopen を足す）へ反映してから着手する。
- U2: memory が取れない時の `dlopen` は今と同じ fatal のまま。
- U5: `dlpi_subs`（`rtld_object_removals`）を TLS を持たない object の unload でも増やす直しを、この WS で入れる。
