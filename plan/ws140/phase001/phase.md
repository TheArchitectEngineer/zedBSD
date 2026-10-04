<!-- awesome-plan project=zedbsd record=ws140-p001 -->

# ws140-p001: object の数・依存の数・初期化の順・dlsym の印の上限を無くす

Status: planned
Disposition: normal
Parent: [WS140](../ws.md)
Queue: none
設計: [ws.md](../ws.md) の D1〜D5・D8・D9
依存: なし
時限の目安: 実装 4 h、試験の program 2 h

## 範囲

- **入る**: object の chunk の列（D1・D2）、依存の可変長（D3）、初期化の順の list（D4）、dlsym の訪問の印（D5）。
  多数の依存の試験 program と、その build・実行の script。
- **入らない**: handle・TLS module・dtv・静的な TLS の並び（p002）。dyntest の書き換え（p002）。他の固定の上限（ws.md の U3）。
- **所有する path**: `src/rtld/rtld.c`、`src/rtld/rtld.h`、`plan/ws140/`。

## 始める前に読む物

1. `AGENTS.md`、`plan/guardrail.md`、[ws.md](../ws.md)（事実と設計）。
2. `plan/coding-style.md` の §3（forward 宣言）・§4（局所の宣言）・§6（条件の中で関数を呼ばない）・§8・§10（comment）・§14（checklist）。
   新しい関数と変える関数はこれに従う。変えない関数は直さない。
3. `src/rtld/rtld.c` の次の部分。
   - 先頭〜260 行: struct と大域の変数。
   - `initialize_object`（1840-1879）、`__rtld_process_fini`（870-900）。
   - `new_object`（2540-2577）、`find_identity`（2517-2537）。
   - `parse_dynamic`（2770 から。`DT_NEEDED` は 2845-2849、名前の確かめは 3224-3233）、`load_dependencies`（3646-3658）。
   - `unload_object_locked`（4756-4848）、`remove_initialization_record`（4851-4870）。
   - `rtld_dlsym_common`（4900-4944）、`lookup_handle_graph`（5032-5082）。

## 手順

### 1. object の chunk の列（D1・D2）

1. `rtld.h`: `RTLD_OBJECT_MAX` を消して `#define RTLD_OBJECT_CHUNK 32U` を置く。
   `RTLD_NEEDED_MAX` を消して `#define RTLD_NEEDED_INLINE 16U` を置く。
2. `rtld.c`: `struct rtld_object_chunk { struct rtld_object_chunk *next; struct rtld_object slots[RTLD_OBJECT_CHUNK]; };` を struct の群に足す。
   comment に「chunk は process の終わりまで解放しない。要素の address は動かない」と書く。
3. 大域の `objects[RTLD_OBJECT_MAX]` を、次の 2 つに置き換える。
   - `static struct rtld_object_chunk object_chunk_first;`（bss。今の `objects` と同じ大きさ）
   - `static struct rtld_object_chunk *object_chunk_last = &object_chunk_first;`
   comment に「`object_count` は使ったことのある slot の数。列と数は loader の lock の下で伸ばす。
   lock の外の読み手（`__rtld_dl_iterate_phdr`）は数を acquire で読む」と書く。
4. 関数 `static struct rtld_object *object_at(unsigned index)` を足す。
   - 最初の chunk から `index / RTLD_OBJECT_CHUNK` 回 `__atomic_load_n(&chunk->next, __ATOMIC_ACQUIRE)` でたどる。
   - `&chunk->slots[index % RTLD_OBJECT_CHUNK]` を返す。
   - 呼ぶ側は `index < object_count` を守る。
5. 関数 `static void object_chunk_grow(void)` を足す（loader の lock の下か、起動の時だけ呼ぶ）。
   - `tls_map(sizeof(struct rtld_object_chunk))` で chunk を取る。取れなければ `rtld_fatal("cannot allocate shared-object table")`。
   - `tls_map` は 0 で埋まった匿名の memory を返すので、memset は要らない。
   - `__atomic_store_n(&object_chunk_last->next, chunk, __ATOMIC_RELEASE)` でつなぎ、`object_chunk_last = chunk` とする。
6. `new_object`（2547-2560）の書き換え。
   - 空いた slot を `object_at(i)` で探す。
   - 無ければ、`object_count` が `RTLD_OBJECT_CHUNK` の倍数の時に `object_chunk_grow()` を呼ぶ。
   - その後 `__atomic_store_n(&object_count, object_count + 1U, __ATOMIC_RELEASE)` で数を増やす。
   - `rtld_fatal("too many shared objects")` を消す。
7. `objects[i]`・`&objects[i]` の参照を全て `object_at(i)` にする。順（slot の番号の順）は変えない（大域の symbol の探索の順が変わらない）。
   今ある場所（main 908a030）は次のとおり。
   - 283-294（`debug_map_publish`）
   - 957-962（`__rtld_dlopen`）
   - 1077-1078（`__rtld_dl_iterate_phdr`）: ここは loop の前に `count = __atomic_load_n(&object_count, __ATOMIC_ACQUIRE)` で数を読み、`count` まで回す。
   - 1131-1135（`__rtld_dladdr`）
   - 1460-1463・1479-1482・1490-1493（`rtld_main`）
   - 2523-2531（`find_identity`）
   - 2547-2560（`new_object`）
   - 4180-4181（大域の探索）
   - 4645-4646（`resolve_tls_symbol` の付近）
   - 5006-5007（`lookup_global_optional`）
   - 5046-5051（`lookup_handle_graph`、手順 4 で扱う）
   - 確かめ: `grep -n 'objects\[\|&objects' src/rtld/rtld.c` が 0 行。
8. `tls_modules[RTLD_OBJECT_MAX + 1U]`・dtv の `RTLD_OBJECT_MAX + 1U`・`layout_static_tls` の `order[RTLD_OBJECT_MAX + 1U]` は p002 で変える。
   この Phase では、`rtld.c` の先頭に `#define RTLD_TLS_MODULE_MAX 33U`（p002 で消す仮の名前）を置き、`RTLD_OBJECT_MAX + 1U` を置き換えるだけにする。
   置き換えは `tls_modules[]`（182）、dtv（554・578・580・636）、`order[]`（1729）の 6 か所。
   `register_tls_module` の `tls_module_count == RTLD_OBJECT_MAX`（3618）は `tls_module_count + 1U == RTLD_TLS_MODULE_MAX` にする。
   - 理由: 今の id は 1〜32 で、`tls_modules[]` の 33 項目に収まる。この Phase で TLS の挙動を変えない。
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
   - 既存の switch の loop の前に、`DT_NULL` までの `DT_NEEDED` を数える loop を足す。
   - 数が `RTLD_NEEDED_INLINE` を越えたら、`tls_map(count * (sizeof(uint32_t) + sizeof(struct rtld_object *)))` で取る。
     取れなければ `rtld_fatal("cannot allocate dependency table")`。
   - pointer の配列を先頭に、offset の配列をその後ろに置く（pointer の整列のため）。`needed_mapping`・`needed_mapping_size` に記録する。
   - `DT_NEEDED` の case の `if (object->needed_count == RTLD_NEEDED_MAX) rtld_fatal(...)` を消す（数は数え済み）。
3. `unload_object_locked`（4756-）の書き換え。今は `dependencies[RTLD_NEEDED_MAX]` に写してから `rtld_memset(object, 0, …)` している。
   - 局所に `struct rtld_object *inline_copy[RTLD_NEEDED_INLINE]`・`struct rtld_object **dependencies`・`void *mapping`・`size_t mapping_size` を置く。
   - 外の配列が無い（`needed_mapping == NULL`）時は、`inline_copy` に写して `dependencies = inline_copy` とする。
   - 外の配列がある時は、`dependencies = object->needed` とし、`mapping`・`mapping_size` を取ってから object の側を NULL・0 にする。
   - memset の後の、依存の参照の数を減らす loop と、再帰の unload は今のまま。
   - 全てが終わってから `tls_unmap(mapping, mapping_size)`（NULL なら何もしない）。
   - 注意: 再帰の `unload_object_locked` の中で同じ slot が再利用されることはない（lock の中で `new_object` は呼ばれない）。
     それでも、外の配列は object の外の memory なので、memset で消えない。

### 3. 初期化の順の list（D4）

1. `struct rtld_object` に `struct rtld_object *init_prev` と `struct rtld_object *init_next` を足す。
   大域の `initialization_order[]`・`initialization_count` を、`initialization_head`・`initialization_tail` にする。
2. `initialize_object`: `initialization_count == RTLD_OBJECT_MAX` の確かめを消し、尾に足す
   （`object->init_prev = initialization_tail; object->init_next = NULL;` と、前の尾の `init_next` か head を更新）。
3. `remove_initialization_record`: list から外す（prev・next を互いにつなぎ、head・tail を更新）。
   list に入っていない object（初期化の前）を外しても壊れないようにする。
   例えば `object != initialization_head && object->init_prev == NULL` なら何もしない。
4. `__rtld_process_fini`: `while (initialization_tail != NULL)` で尾から外しながら fini を呼ぶ。外す時は `remove_initialization_record` を使う。
5. 確かめ: `grep -n 'initialization_order\|initialization_count' src/rtld/rtld.c` が 0 行。

### 4. dlsym の訪問の印（D5）

1. `struct rtld_object` に `uint32_t lookup_mark` を足す。大域に `static uint32_t lookup_generation;` を置き、comment に「loader の lock の下だけで使う」と書く。
2. `rtld_dlsym_common` から `visited` を消す。lock を取った後で `lookup_generation` を 1 増やす。0 になったら次のようにする。
   - 全 object の `lookup_mark` を 0 にする（`object_at` で回す）。
   - `lookup_generation = 1` にする。
3. `lookup_handle_graph` の引数 `uint32_t *visited` を消す（forward 宣言 470 行も直す）。
   - 範囲の確かめ（5046）を `object == NULL || !object->active || object->unloading` にする。
   - bitmask の 2 行を、`if (object->lookup_mark == lookup_generation) return 0; object->lookup_mark = lookup_generation;` の意味の code にする。

### 5. 試験の program（`plan/ws140/tests/`）

次の file を作る。C は全文規約、script は既存の `plan/ws073/tests/p038/run-tls-check.sh` の書き方に倣う。

1. `many-lib.c`: 1 つの関数だけの library の source。名前と値は compile の時の object 形の macro で決める。
   - `-DMANY_SYMBOL=many_value_07 -DMANY_NUMBER=8` なら `int many_value_07(void)` が 8 を返す。
   - 関数形の macro は使わない（規約 §14）。
2. `many-hub.c`: 中身の無い library。`int hub_marker(void)` だけを持ち、依存（leaf）を `DT_NEEDED` に持たせるためだけに使う。
3. `rtld-many.c`: 試験の program。`libmany00.so`〜`libmany39.so` の 40 個を直接 link する（実行 file の `DT_NEEDED` が 40）。
   各段は、成功で `RTLD-MANY step=<名前> ok` を、失敗で `RTLD-MANY step=<名前> FAIL <詳細>` を出して exit 1 する。
   最後に `RTLD-MANY: PASS` を出す。
   - **startup**: 起動できた（ここに来た）。
   - **direct**: `many_value_00() == 1` と `many_value_39() == 40`（直接の呼び出し。32 番目より後の object への再配置）。
   - **global**: `dlopen(NULL, RTLD_NOW)` の handle で、`dlsym` が `many_value_00`〜`many_value_39` の全てを見つけ、値が番号 + 1。
   - **count-startup**: `dl_iterate_phdr` で数えた object が 43 以上（実行 file・40 個・libc.so・ld.so）。数を出す。
   - **hubs**: `dlopen("libhub0.so")`・`"libhub1.so"`・`"libhub2.so"`（各 40 個の leaf を `DT_NEEDED` に持つ）。
     `dl_iterate_phdr` の数が 166 以上。
   - **graph**: `dlsym(hub0, "leaf_value_039") == 40` 番目の leaf の関数、`dlsym(hub2, "leaf_value_119")`、
     `dlsym(hub0, "leaf_value_119")` は NULL（hub0 の graph に無い）。関数を呼んで値を確かめる。
   - **close**: 3 つを `dlclose`。`dl_iterate_phdr` の数が count-startup の数に戻る。
   - **reopen**: `libhub2.so` をもう一度開き、`leaf_value_100` を見つけて呼べる。閉じる。
4. `build-many.sh BUILD OUT`: host で、target の clang を使って作る。clang は `build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=build/amd64/sysroot`、
   flag は `run-tls-check.sh` の dynamic の program と同じにする。作る物は次のとおり。
   - `libmany00.so`〜`libmany39.so`（`many-lib.c` を 40 回）。
   - `libleaf000.so`〜`libleaf119.so`（`many-lib.c` を `MANY_SYMBOL=leaf_value_NNN`・`MANY_NUMBER=NNN+1` で 120 回）。
   - `libhub0.so`（leaf 000〜039 を link）、`libhub1.so`（040〜079）、`libhub2.so`（080〜119）。
   - `rtld-many`（`-Wl,-rpath,'$ORIGIN'`、`-L"$OUT" -l:libmany00.so … -l:libmany39.so`、`-L"$BUILD/dynamic" -l:libc.so`）。
   共有の library の link は `-shared -Wl,-soname,<名前> -Wl,--hash-style=gnu,-z,now,-z,relro`。
   lld は `-l` で渡した物を既定で全て `DT_NEEDED` にする（`--as-needed` を付けない）。
   最後に host の `build/llvm/bin/llvm-readelf -d OUT/rtld-many | grep -c NEEDED` が 41（40 ＋ libc.so）であることを確かめて表示する。
5. `rtld-many.sh [BUILD]`（T が流す。guest は先に起動しておく）の流れ。
   - `build-many.sh` を呼ぶ。
   - `guest.py run 'mkdir -p /root/ws140'` の後、作った file を `guest.py put` で `/root/ws140/` に置く（164 個）。
     遅ければ、guest に `tar`（`pax`）があるかを `command -v tar` で確かめ、1 つの archive で送ってよい。
   - `guest.py run 'cd /root/ws140 && chmod +x rtld-many && LD_LIBRARY_PATH=/root/ws140 ./rtld-many'` を走らせ、出力を `OUT/rtld-many.txt` に残す。
   - `RTLD-MANY: PASS` の有無で `rtld-many: PASS|FAIL` を出す。
   - `LD_LIBRARY_PATH` は `dlopen` の探索に使われる（rtld.c 1368 と `open_dependency` の 36 行目付近）。効かなければ `/usr/lib` に置く。
6. `config-amd64-rtld.mk`: `include plan/tools/guest/config-amd64-ssh.mk` の 1 行と、目的の comment。
   image は `plan/tools/guest/test-image.sh plan/ws140/tests/config-amd64-rtld.mk BUILD` で作る（出力は `BUILD/hdd-image.img`）。

## 確かめ（この Phase で自分が行う物。QEMU は起動しない）

```sh
B=build/ws140-p001                      # 自分の worktree の中
C=ZEDBSD_CONFIG=config/ci/config-amd64.mk
make $C BUILD=$B $B/dynamic/ld.so $B/dynamic/libc.so $B/dynamic/dyntest 2>&1 | tee $B.log
grep -c 'warning:' $B.log                # 0
make $C BUILD=$B dynamic-userland-check  # "zedBSD amd64 dynamic userland artifacts: PASS"（target が無いと言われたら記録して省く）
size $B/dynamic/ld.so                    # bss を記録（今 217,944）
make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws140-p001-arm64 \
    build/ws140-p001-arm64/dynamic/ld.so build/ws140-p001-arm64/dynamic/libc.so   # warning 0
sh plan/ws140/tests/build-many.sh $B build/ws140-p001-many   # NEEDED 41 の表示、warning 0
python3 plan/tools/style-check.py plan/ws140/tests/*.c       # 違反 0
grep -n 'RTLD_OBJECT_MAX\|RTLD_NEEDED_MAX\|objects\[\|initialization_order' src/rtld/*.c src/rtld/*.h   # 0 行
git diff --check
```

- `style-check.py` を `src/rtld/rtld.c` 全体に掛けると、変えていない古い関数の違反が多数出る（D9）。
  変えた関数の違反だけを数えて 0 にする。`--summary` の前後の数を比べ、増えていないことを記録する。
- host で ld.so を動かす試験は無い（ld.so は zedBSD の system call を直接呼ぶ）。動作の確かめは T の guest で行う。

## 試験の依頼（T1/T2）

p001 だけでは T に依頼しない。p002 の後に、p001 と p002 の試験をまとめて 1 回で依頼する（[protocol](../../agents/protocol.md)「試験の担当 T1」）。
p001 の commit を Q1 に merge 依頼してから p002 に進む。p001 は T の結果が出るまで cleared にしない（in-progress のまま、「実装済み・試験待ち」）。

## 受け入れの条件

1. 手順 1〜4 の grep の確かめが全て 0 行。amd64 と arm64 の build が warning 0。`dynamic-userland-check` が PASS。
2. T の guest で `rtld-many: PASS`（startup・direct・global・count-startup・hubs・graph・close・reopen）。
3. T の guest で既存の `dyntest` が最後まで走る（`plan/ws073/tests/p038/run-tls-check.sh` の dyntest の部分）。
   p001 は handle を変えないので、handle の上限の試験（64 個）もそのまま通る。
4. bss の値と、変えた関数の一覧を下の「結果」に書く。

## 結果

（未実施）
