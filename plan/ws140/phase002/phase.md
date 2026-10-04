<!-- awesome-plan project=zedbsd record=ws140-p002 -->

# ws140-p002: handle・TLS module・dtv の上限を無くす

Status: planned
Disposition: normal
Parent: [WS140](../ws.md)
Queue: none
設計: [ws.md](../ws.md) の D1・D2・D6・D7・D8・D9
依存: p001（同じ `rtld.c`。p001 の commit の上に重ねる。p001 の T の結果は待たなくてよい。試験はこの Phase の後にまとめて依頼する）
時限の目安: 実装 4 h、試験 2 h

## 範囲

- **入る**: handle の chunk の列、TLS module の chunk の列、dtv を伸ばす（D6）、`layout_static_tls` の局所の配列をやめる（D7）。
  `userland/tests/dyntest.c` の handle の上限の試験の書き直し。`rtld-many` に handle と TLS の段を足す。
  p001 と合わせた試験の T への依頼。
- **入らない**: 他の固定の上限（ws.md の U3）。失敗を dlerror にする変更（U2）。全文規約の通しの見直し（p003）。
- **所有する path**: `src/rtld/rtld.c`、`src/rtld/rtld.h`、`userland/tests/dyntest.c`、`plan/ws140/`。

## 始める前に読む物

1. [ws.md](../ws.md)、[p001](../phase001/phase.md)（p001 で入った `object_at`・`object_chunk_grow` の書き方に揃える）。
2. `plan/coding-style.md` の §14（checklist）と、critical section の空行の規則（§5）。
3. p001 の後の `src/rtld/rtld.c` の次の部分。行番号は main 908a030 の物で、p001 の後はずれるので、関数名で探す。
   - `__rtld_thread_alloc`・`__rtld_thread_free`・`__tls_get_addr`・`d_tlsdesc_resolve`（520-800）
   - `allocate_handle`（1946-1977）・`validate_handle`（4947-4979）・`__rtld_dlclose`
   - `layout_static_tls`（1725-1800）・`static_tls_displacement`（1818 付近）
   - `register_tls_module`（3578-3643）・`install_tlsdesc`（4673 付近）・`unload_object_locked` の TLS の部分
   - `__rtld_fork_child`（834）: lock の作り直し。dtv を伸ばす時の lock と矛盾しないことを確かめる。

## 手順

### 1. handle の chunk の列

1. `#define RTLD_HANDLE_MAX 64U` を `#define RTLD_HANDLE_CHUNK 64U` にする。
   `struct rtld_handle_chunk { struct rtld_handle_chunk *next; struct rtld_handle slots[RTLD_HANDLE_CHUNK]; };` を足す。
   大域は `handle_chunk_first`（bss）と `handle_chunk_last` の 2 つ。handle は loader の lock の下だけで使う（comment に書く）。
2. `allocate_handle`: chunk ごとに空いた slot を探す。
   - 無ければ `tls_map(sizeof(struct rtld_handle_chunk))` で次の chunk を取り、つないで、その最初の slot を使う。
   - `tls_map` が NULL なら今と同じく NULL を返す。dlerror は呼ぶ側（`__rtld_dlopen`）が今のまま出す。
3. `validate_handle`: chunk ごとに、`address` が `&chunk->slots[0]` 以上 `&chunk->slots[RTLD_HANDLE_CHUNK]` 未満で、
   差が `sizeof(struct rtld_handle)` の倍数かを見る。どの chunk にも入らなければ NULL。magic などの確かめは今のまま。
4. 確かめ: `grep -n 'RTLD_HANDLE_MAX\|handles\[' src/rtld/rtld.c` が 0 行。

### 2. TLS module の chunk の列

1. p001 の仮の `RTLD_TLS_MODULE_MAX 33U` を消す。`#define RTLD_TLS_CHUNK 33U` を置く。
   `struct rtld_tls_chunk { struct rtld_tls_chunk *next; struct rtld_tls_module slots[RTLD_TLS_CHUNK]; };` を足す。
   - id は 1 から。最初の chunk の slot 0 は使わない（今の `tls_modules[0]` と同じ）。
   - 大域は `tls_chunk_first`（bss）と `tls_chunk_last` の 2 つ。
2. `static struct rtld_tls_module *tls_module_at(uintptr_t id)` を足す。
   - `id < RTLD_TLS_CHUNK` なら `&tls_chunk_first.slots[id]` を直接返す（`__tls_get_addr` の速い道）。
   - それ以外は、`id / RTLD_TLS_CHUNK` 回 `__atomic_load_n(&chunk->next, __ATOMIC_ACQUIRE)` でたどり、`&chunk->slots[id % RTLD_TLS_CHUNK]` を返す。
3. `register_tls_module`:
   - 空いた id を 1〜`tls_module_count` で探す。
   - 無ければ新しい id = `tls_module_count + 1`。`id % RTLD_TLS_CHUNK == 0` なら次の chunk を取ってつなぐ（release）。
     取れなければ `rtld_fatal("cannot allocate TLS module table")`。
   - 順を変える。今は `id = ++tls_module_count` の後で中身を書く。新しい順は次のとおり。
     module の中身を書く → `__atomic_store_n(&module->active, 1, __ATOMIC_RELEASE)` →（新しい id の時だけ）`__atomic_store_n(&tls_module_count, id, __ATOMIC_RELEASE)`。
   - `rtld_fatal("too many TLS modules")` を消す。
4. `tls_modules[` の参照を全て `tls_module_at(…)` にする。場所は次のとおり。
   - `__rtld_thread_free`、`__tls_get_addr`、`layout_static_tls`、`static_tls_displacement`、`register_tls_module`
   - `install_tlsdesc`（4710 付近）、`unload_object_locked`（4800 付近）
   - 確かめ: `grep -n 'tls_modules\[' src/rtld/rtld.c` が 0 行。
5. `__tls_get_addr`:
   - 数を `__atomic_load_n(&tls_module_count, __ATOMIC_ACQUIRE)` で読んでから `index->module` と比べる。
   - `active` も acquire で読む。

### 3. dtv を伸ばす（D6）

1. `RTLD_TLS_MODULE_MAX`（p001 の仮の名前）を使っていた dtv の大きさを、`RTLD_DTV_INITIAL`（= `RTLD_TLS_CHUNK`）にする。
   場所は `__rtld_thread_alloc` の `tls_map`・`rtld_memset`・`dtv_count` の 3 か所。
2. `__rtld_thread_free`: `tls_unmap(tcb->dtv, …)` の大きさを `tcb->dtv_count * sizeof(*tcb->dtv)` にする。
3. `static void dtv_grow(struct __rtld_tcb *tcb, uintptr_t module)` を足す。
   1. `loader_lock()`。
   2. lock の中で `module < tcb->dtv_count` なら何もしない（他の道で伸びた）。
   3. 新しい数 = max(`module + 1`, `tcb->dtv_count * 2`)。overflow の確かめを付ける。
   4. `tls_map(新しい数 * sizeof(void *))` で取る。取れなければ `rtld_fatal("cannot allocate TLS vector")`。
   5. 古い dtv の `dtv_count` 項目を写す。
   6. `tcb->dtv` と `tcb->dtv_count` を差し替える。
   7. 古い dtv を `tls_unmap`（古い数 × pointer）。
   8. `loader_unlock()`。critical section は前後に空行を置く（規約 §5）。
4. `__tls_get_addr`: `tcb->dtv == NULL || index->module >= tcb->dtv_count` の `rtld_fatal` を分ける。
   - `tcb->dtv == NULL` は今のまま fatal。
   - `index->module >= tcb->dtv_count` は `dtv_grow(tcb, index->module)` を呼んでから進む。
5. 注意: 遅い道は loader の lock を取る。
   - **constructor の中**: loader の lock は再帰の lock（`loader_lock` の owner と depth）なので、同じ thread が持っていても止まらない。
   - **signal handler の中**: 同じ thread が lock を持ったまま dtv を書き換えている最中に signal handler が初めて TLS の module に触れると、
     書き換えの途中の dtv を見る恐れがある。これは今の作りにも無い新しい危険である。対策はこの Phase では行わず、「結果」に記録する
     （glibc にも同種の制限がある、推測）。

### 4. 静的な TLS の並び（D7）

`layout_static_tls`（amd64・i386 の版）の局所の `order[]` をやめる。

1. 置き場所を決める loop を 2 回にする。
   - 1 回目: main の module（`main_object->tls_module_id != 0` の時）。
   - 2 回目: id 1〜`tls_module_count` の active な module のうち、main の物を除く。
2. template に写す loop も、同じ 2 回にする。
3. 「置き場所を 1 つ決める」と「1 つ写す」を小さな static 関数にして、2 回の loop から呼ぶ（重複を避ける）。

### 5. dyntest の書き直し

`userland/tests/dyntest.c` の 248-276 行（`exhausted_handles[64]` の段、`DL:05I:HANDLE-OOM-RECOVERED`）を書き直す。

- 200 個の handle を開き（全て非 NULL）、全てを閉じ、もう 1 つ開いて閉じる。
- 「65 個目が NULL になる」確かめ（return 36）は消す。
- 変数名は `exhausted_handles` から `many_handles[200]` にする。表示は `DL:05I:HANDLES-200`。
- return の番号は 35・37・38 を同じ意味で使う。

### 6. `rtld-many` に段を足す（`plan/ws140/tests/`）

1. `many-tls.c`: `__thread int` を 1 つ持つ library。`MANY_SYMBOL` に「その変数の address を返す関数」の名前を、`MANY_NUMBER` に初期値を渡す。
   `build-many.sh` で `libtls00.so`〜`libtls39.so` を作る。半分（偶数番）は `-mtls-dialect=gnu2`（TLSDESC）、残りは既定（`__tls_get_addr`）。
2. `rtld-many.c` に次の段を足す（p001 の段の後）。
   - **handles**: `dlopen("libmany00.so", RTLD_NOW)` を 200 回。全て非 NULL。全て `dlclose` して 0。
   - **tls**: `libtls00.so`〜`libtls39.so` を `dlopen` する。dlopen の module なので静的ではなく、dtv が 33 を越える。
     主の thread で全ての変数を読み（初期値）、番号 × 10 を書いて読み戻す。
   - **tls-thread**: もう 1 つの thread（`pthread_create`）でも同じことをし、値が初期値から始まる（thread ごとの block）ことを確かめる。
     thread の終わりで主の thread の値が変わっていないことも確かめる。
   - **tls-close**: 40 個を閉じ、もう一度 `libtls39.so` を開いて読める（id の再利用）。
3. `rtld-many.sh` は変えない（送る file が増えるだけ）。`build-many.sh` に libtls を足す。

## 確かめ（自分で行う物。QEMU は起動しない）

```sh
B=build/ws140-p002; C=ZEDBSD_CONFIG=config/ci/config-amd64.mk
make $C BUILD=$B $B/dynamic/ld.so $B/dynamic/libc.so $B/dynamic/dyntest $B/dynamic/tlstest.so 2>&1 | tee $B.log
grep -c 'warning:' $B.log                     # 0
make $C BUILD=$B dynamic-userland-check
size $B/dynamic/ld.so                         # bss を記録
make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws140-p002-arm64 \
    build/ws140-p002-arm64/dynamic/ld.so build/ws140-p002-arm64/dynamic/libc.so   # warning 0
sh plan/ws140/tests/build-many.sh $B build/ws140-p002-many
grep -n 'RTLD_HANDLE_MAX\|RTLD_TLS_MODULE_MAX\|handles\[\|tls_modules\[\|too many' src/rtld/rtld.c   # 0 行
python3 plan/tools/style-check.py plan/ws140/tests/*.c userland/tests/dyntest.c   # dyntest は変えた部分の違反 0
git diff --check
```

## 試験の依頼（T1/T2、p001 と p002 をまとめて 1 回）

commit して Q1 に merge 依頼をした後、T に次を依頼する。依頼の後は待たずに、Q1 が投入した次の仕事に移る。

- **image**: commit の SHA と、`plan/tools/guest/test-image.sh plan/ws140/tests/config-amd64-rtld.mk BUILD`（出力 `BUILD/hdd-image.img`）。
- **guest**: `GUEST_RUNTIME=<T の runtime> python3 plan/tools/guest/guest.py start BUILD/hdd-image.img` と `wait`。
- **流す物**: 合わせて 10 分以内の見込み。
  1. `sh plan/ws140/tests/rtld-many.sh BUILD`: `rtld-many: PASS`（p001 の 8 段と p002 の 4 段）。
  2. `sh plan/ws073/tests/p038/run-tls-check.sh BUILD`: `tls-check`（動的・静的）の PASS と、dyntest が最後（`DL:06:PLUGIN-TLS` の後）まで走る。
     `DL:05I:HANDLES-200` が出る。
  3. `plan/tools/boot-test.sh BUILD/hdd-image.img`: login prompt（PNG）。
- **合否**: 上の 3 つが全て PASS。FAIL の時は、出力の file（`rtld-many.txt` など）と、`RTLD-MANY step=… FAIL` の行を返す。

desktop の回帰（Files の PDF の縮小表示 `dlopen("libpdf.so")`、`plan/ws127/tests/files-p002.sh` の 4）は p003 で依頼する。

## 受け入れの条件

1. 手順の grep の確かめが全て 0 行。amd64 と arm64 の build が warning 0。
2. T の guest で、上の 3 つが PASS。
3. bss の値（p001 と比べて）、dtv を伸ばす道の lock の扱い、signal handler の危険（手順 3 の 5）を「結果」に書く。

## 結果

（未実施）
