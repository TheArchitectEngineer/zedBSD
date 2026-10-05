<!-- awesome-plan project=zedbsd record=ws140-p002 -->

# ws140-p002: handle・TLS module・dtv の上限を無くす

Status: cleared（2026-10-05 Q1: T1-163 の tls-check・dyntest・boot PASS と、T1-164 で rtld-many（ld.so の segment の予約の直しの後）も PASS（amd64）。arm64・i386・sparcv9 の build は sysroot が要り未実施）。以前: in-progress（実装済み・T の試験待ち。p001 とまとめて依頼）
Disposition: normal
Parent: [WS140](../ws.md)
Queue: q729（Q1、2026-10-05）
設計: [ws.md](../ws.md) の D1・D2・D6・D7・D8・D9
依存: p001 の commit が main に統合済みであること（同じ `rtld.c` に重ねるため。p001 の clearance は要らない。試験は p001 と p002 をまとめてこの Phase の後に依頼する）
時限の目安: 実装 4 h、試験 2 h

## 範囲

- **入る**:
  - handle の chunk の列、TLS module の chunk の列、dtv を伸ばす（D6）、`layout_static_tls` の局所の配列をやめる（D7）。
  - `userland/tests/dyntest.c` の handle の上限の試験の書き直し。
  - `rtld-many` に handle と TLS の段を足す。
  - p001 と合わせた試験の T への依頼。
- **入らない**: 失敗を dlerror にする変更（U2）。U3 の上限は p001 で済んだ。全文規約の通しの見直し（p003）。
- **所有する path**: `src/rtld/rtld.c`、`src/rtld/rtld.h`、`userland/tests/dyntest.c`、`plan/ws140/`。

## 始める前に読む物

1. [ws.md](../ws.md)、[p001](../phase001/phase.md)（p001 で入った `object_at`・`object_chunk_grow` の書き方に揃える。p001 の手順 0 の sysroot の確かめもこの Phase で同じく行う）。
2. `plan/coding-style.md` の §14（checklist）と、critical section の空行の規則（§5）。
3. p001 の後の `src/rtld/rtld.c` の次の関数。行番号は main 908a030 の物で、p001 の後はずれるので、関数名で探す。
   - `__rtld_thread_alloc`・`__rtld_thread_free`・`__tls_get_addr`・`d_tlsdesc_resolve`（520-800）
   - `allocate_handle`（1946-1977）・`validate_handle`（4947-4979）・`__rtld_dlclose`
   - `layout_static_tls`（1725-1805）・`static_tls_displacement`（1818 付近）
   - `register_tls_module`（3578-3643）・`install_tlsdesc`（4673 付近）・`unload_object_locked` の TLS の部分
   - `loader_lock`（1608-1636）と `__rtld_fork_child`（834）: lock の作り。dtv を伸ばす時の lock と矛盾しないことを確かめる。

## 手順

### 1. handle の chunk の列

1. `#define RTLD_HANDLE_MAX 64U` を `#define RTLD_HANDLE_CHUNK 64U` にする。
   `struct rtld_handle_chunk { struct rtld_handle_chunk *next; struct rtld_handle slots[RTLD_HANDLE_CHUNK]; };` を足す。
   - 大域は `handle_chunk_first`（bss）と `handle_chunk_last` の 2 つ。
   - handle は loader の lock の下だけで使う（comment に書く）。
2. `allocate_handle`: chunk ごとに空いた slot を探す。
   - 無ければ `tls_map(sizeof(struct rtld_handle_chunk))` で次の chunk を取り、つないで、その最初の slot を使う。
   - `tls_map` が NULL なら、今と同じく NULL を返す。
3. `__rtld_dlopen` の 2 か所（main 908a030 の 938・1002 行）の `set_loader_error("too many dynamic-loader handles")` を、
   `set_loader_error("cannot allocate dynamic-loader handle")` にする（ws.md の完了の条件 1）。
4. `validate_handle`: chunk ごとに次を見る。
   - `address` が `&chunk->slots[0]` 以上、`&chunk->slots[RTLD_HANDLE_CHUNK]` 未満であること。
   - 差が `sizeof(struct rtld_handle)` の倍数であること。
   - どの chunk にも入らなければ NULL を返す。magic などの確かめは今のまま。
5. 確かめ: `grep -n 'RTLD_HANDLE_MAX\|handles\[\|too many dynamic-loader handles' src/rtld/rtld.c` が 0 行。

### 2. TLS module の chunk の列

1. p001 の仮の `RTLD_TLS_MODULE_MAX 33U` を消し、`#define RTLD_TLS_CHUNK 33U` を置く。
   `struct rtld_tls_chunk { struct rtld_tls_chunk *next; struct rtld_tls_module slots[RTLD_TLS_CHUNK]; };` を足す。
   - id は 1 から。最初の chunk の slot 0 は使わない（今の `tls_modules[0]` と同じ）。
   - 大域は `tls_chunk_first`（bss）と `tls_chunk_last` の 2 つ。
2. `static struct rtld_tls_module *tls_module_at(uintptr_t id)` を足す。
   - `id < RTLD_TLS_CHUNK` なら、`&tls_chunk_first.slots[id]` を直接返す（`__tls_get_addr` の速い道）。
   - それ以外は、`id / RTLD_TLS_CHUNK` 回 `__atomic_load_n(&chunk->next, __ATOMIC_ACQUIRE)` でたどり、`&chunk->slots[id % RTLD_TLS_CHUNK]` を返す。
3. `register_tls_module`:
   - 空いた id を 1〜`tls_module_count` で探す。
   - 無ければ新しい id = `tls_module_count + 1`。`id % RTLD_TLS_CHUNK == 0` なら、次の chunk を取ってつなぐ（release）。
     取れなければ `rtld_fatal("cannot allocate TLS module table")`。
   - 書く順を変える。今は `id = ++tls_module_count` の後で中身を書く。新しい順は次のとおり。
     1. module の中身を書く。
     2. `__atomic_store_n(&module->active, 1, __ATOMIC_RELEASE)`。
     3. 新しい id の時だけ、`__atomic_store_n(&tls_module_count, id, __ATOMIC_RELEASE)`。
   - `rtld_fatal("too many TLS modules")` を消す。
4. `tls_modules[` の参照を全て `tls_module_at(…)` にする。場所は次のとおり。
   - `__rtld_thread_free`、`__tls_get_addr`、`layout_static_tls`、`static_tls_displacement`、`register_tls_module`
   - `install_tlsdesc`（4710 付近）、`unload_object_locked`（4800 付近）
   - 確かめ: `grep -n 'tls_modules\[' src/rtld/rtld.c` が 0 行。
5. `__tls_get_addr`:
   - 数を `__atomic_load_n(&tls_module_count, __ATOMIC_ACQUIRE)` で読んでから、`index->module` と比べる。
   - `active` も acquire で読む。

### 3. dtv を伸ばす（D6）

1. dtv の最初の大きさ（`__rtld_thread_alloc` の `tls_map`・`rtld_memset`・`dtv_count` の 3 か所）を次にする。
   - 「`RTLD_DTV_INITIAL`（= `RTLD_TLS_CHUNK`）」と「`tls_module_count + 1`」の大きい方。
   - `tls_module_count` は、`__rtld_thread_alloc` が tcb を list に足す時にすでに取っている lock の中で読む。
     その lock の前に dtv を取っているので、取る所を lock の中へ移すか、lock の中で読んだ数で足りなければ取り直す。
2. `struct __rtld_tcb` は ABI なので変えない（`include/libc/rtld-abi.h`）。退いた dtv の鎖は、**dtv の項目の後ろに 2 つ余分に取った場所**に置く。
   - dtv は常に `dtv_count + 2` 項目で取る。`[dtv_count]` に「1 つ前の（退いた）dtv の pointer、無ければ NULL」、`[dtv_count + 1]` に「その dtv の `dtv_count`」を置く。
   - 退いた dtv の中身は一切書き換えない。割り込まれた速い道が古い dtv を読んでも、正しい（古い）値が見える。
   - `__rtld_thread_alloc` の最初の dtv も `+ 2` で取り、後ろの 2 つは 0（NULL）。
3. `static void dtv_grow(struct __rtld_tcb *tcb, uintptr_t module)` を足す。
   1. `loader_lock()`。
   2. lock の中で `module < tcb->dtv_count` なら何もしない（他の道で伸びた）。
   3. 新しい数 = max(`module + 1`, `tcb->dtv_count * 2`)。overflow の確かめを付ける。
   4. `tls_map((新しい数 + 2) * sizeof(void *))` で取る。取れなければ `rtld_fatal("cannot allocate TLS vector")`。
   5. 古い dtv の項目 1〜`dtv_count - 1` を写す。
   6. 新しい dtv の `[新しい数]` に古い dtv の address を、`[新しい数 + 1]` に古い `dtv_count` を書く（上の 2）。古い dtv は **unmap しない**。
   7. `tcb->dtv` と `tcb->dtv_count` を差し替える。
   8. `loader_unlock()`。critical section の前後に空行を置く（規約 §5）。
4. `__tls_get_addr`: 今の `tcb->dtv == NULL || index->module >= tcb->dtv_count` の `rtld_fatal` を分ける。
   - `tcb->dtv == NULL` は、今のまま fatal。
   - `index->module >= tcb->dtv_count` は、`dtv_grow(tcb, index->module)` を呼んでから進む。
5. `__rtld_thread_free`:
   - 今の dtv から鎖（`[dtv_count]`・`[dtv_count + 1]`）をたどり、退いた dtv を全て unmap する（大きさは `(その数 + 2) * sizeof(void *)`）。
   - 最後に今の dtv を `(tcb->dtv_count + 2) * sizeof(*tcb->dtv)` で unmap する。
6. 残る危険は ws.md の「残る危険」に書いてある（信号の handler の中で lock の取り途中だと自分を待つ）。この Phase では直さず、「結果」に再び記録する。

### 4. 静的な TLS の並び（D7）

`layout_static_tls`（amd64・i386 の版）の局所の `order[]` をやめる。

1. 置き場所を決める loop を 2 回にする。
   - 1 回目: main の module（`main_object->tls_module_id != 0` の時）。
   - 2 回目: id 1〜`tls_module_count` の active な module のうち、main の物を除く。
   - 「置き場所を 1 つ決める」を小さな static 関数にして、2 回の loop から呼ぶ。
2. template に写す loop（今の 1793-1801）は、`static_offset` を使うので順に依らない。
   id 1〜`tls_module_count` の、`is_static` で active な module を 1 回の loop で写す。
3. 試験の穴: 起動の時に 33 個を越える静的な TLS の module（2 つ目の chunk を通る `layout_static_tls`）は、この WS の試験に無い。「結果」に未実施と書く。

### 5. dyntest の書き直し

`userland/tests/dyntest.c` の handle の試験（宣言は 54 行の `void *exhausted_handles[64];`、本体は 248-276 行、`DL:05I:HANDLE-OOM-RECOVERED`）を書き直す。

- 宣言を `void *many_handles[200];` にする。
- 200 個の handle を開き（全て非 NULL）、全てを閉じ、もう 1 つ開いて閉じる。
- 「65 個目が NULL になる」の確かめ（return 36）は消す。
- 表示は `DL:05I:HANDLES-200`。
- return の番号は 35・37・38 を同じ意味で使う。

### 6. `rtld-many` に段を足す（`plan/ws140/tests/`）

1. `many-tls.c`: `__thread int` を 1 つ持つ library。
   - `MANY_SYMBOL` に「その変数の address を返す関数」の名前を、`MANY_NUMBER` に初期値を渡す。
   - `build-many.sh` で `libtls00.so`〜`libtls39.so` を作る。
   - 偶数番は `-mtls-dialect=gnu2`（TLSDESC。前例は `platform/amd64/vmunix.mk:811`）、奇数番は既定（`__tls_get_addr`）。
   - link は p001 の共有の library と同じ。`__tls_get_addr` は未定義のまま残してよい（ld.so が解決する）。link が未定義で止まる時は、`-Wl,--allow-shlib-undefined` を足して記録する。
2. `rtld-many.c` に次の段を足す（p001 の段の後）。
   - **handles**: `dlopen("libmany00.so", RTLD_NOW)` を 200 回。全て非 NULL。全て `dlclose` して 0。
   - **tls**: `libtls00.so`〜`libtls39.so` を `dlopen` する。dlopen の module は静的ではないので、dtv が 33 を越える。
     主の thread で全ての変数を読み（初期値）、番号 × 10 を書いて読み戻す。
   - **tls-thread**: もう 1 つの thread（`pthread_create`）でも同じことをする。値が初期値から始まる（thread ごとの block）ことを確かめる。
     thread の終わりで、主の thread の値が変わっていないことも確かめる。
   - **tls-close**: 40 個を閉じ、もう一度 `libtls39.so` を開いて読める（id の再利用）。
3. `build-many.sh` に libtls の build と archive への追加を足す。`rtld-many.sh` は archive を送るので変えない。

## 確かめ（自分で行う物。QEMU は起動しない）

```sh
B=build/ws140-p002; C=ZEDBSD_CONFIG=config/ci/config-amd64.mk
make -n $C BUILD=$B $B/dynamic/ld.so 2>&1 | grep -c 'sysroot.mk\|zedbsd-sysroot-complete'   # 0（p001 の手順 0）
python3 plan/tools/style-check.py --summary src/rtld/rtld.c > build/ws140-p002-style-before.txt
make $C BUILD=$B $B/dynamic/ld.so $B/dynamic/libc.so $B/dynamic/dyntest $B/dynamic/tlstest.so 2>&1 | tee $B.log
grep -c 'warning:' $B.log                     # 0
make $C BUILD=$B dynamic-userland-check
size $B/dynamic/ld.so                         # bss を記録
# arm64: p001 の手順 0 の結果に従う
sh plan/ws140/tests/build-many.sh $B build/ws140-p002-many
grep -n 'RTLD_HANDLE_MAX\|RTLD_TLS_MODULE_MAX\|handles\[\|tls_modules\[' src/rtld/rtld.c   # 0 行
grep -n '"too many shared objects"\|"too many dependencies"\|"too many TLS modules"\|"initialization order overflow"\|"too many dynamic-loader handles"' src/rtld/rtld.c   # 0 行
python3 plan/tools/style-check.py plan/ws140/tests/*.c     # 違反 0
python3 plan/tools/style-check.py --summary src/rtld/rtld.c userland/tests/dyntest.c > build/ws140-p002-style-after.txt
git diff --check
```

- `"too many object mappings"`（p001 で容量の不変の確かめになった）・symbol の版の 2 つは残す。消さない。`"too many TLSDESC relocations"` は p001 で消した（U3）。
- style は p001 と同じ扱い（関数ごとに増えていない、新しい関数と新しい行は 0）。

## 試験の依頼（T1/T2、p001 と p002 をまとめて 1 回）

commit して Q1 に merge 依頼をした後、T に次を依頼する。依頼の後は待たずに、Q1 が投入した次の仕事に移る。

- **image**: commit の SHA と、`plan/tools/guest/test-image.sh plan/ws140/tests/config-amd64-rtld.mk BUILD`（出力 `BUILD/hdd-image.img`）。
- **sysroot の symlink**: `run-tls-check.sh` は `$BUILD/sysroot` を sysroot として使う（14-15 行）。make は `$BUILD/sysroot` を作らない。
  流す前に `ln -sfn "$PWD/build/amd64/sysroot" BUILD/sysroot` を作る（読むだけ）。
- **guest**: `GUEST_RUNTIME=$PWD/build/ws140-run python3 plan/tools/guest/guest.py start BUILD/hdd-image.img` と `wait`。
- **流す物**: 合わせて 10 分以内の見込み（推測）。
  1. `GUEST_RUNTIME=… sh plan/ws140/tests/rtld-many.sh BUILD`: `rtld-many: PASS`（p001 の 8 段と p002 の 4 段）。
  2. `GUEST_RUNTIME=… sh plan/ws073/tests/p038/run-tls-check.sh BUILD`: `tls-check`（動的・静的）が PASS。
     この script は dyntest の結果で status を変えない（51-56 行）。だから次も確かめる:
     `grep -q 'DL:05I:HANDLES-200' build/ws073-p038/dyntest.txt && grep -q 'DL:06:PLUGIN-TLS' build/ws073-p038/dyntest.txt`。
  3. guest を止める（`guest.py stop`）。その後に `OUTPUT=build/ws140-boot plan/tools/boot-test.sh BUILD/hdd-image.img`: login prompt（PNG）。
     boot-test は自分で QEMU を起こすので、先に止めないと QEMU が 2 つになる。
- **合否**: 上の 3 つが全て PASS。FAIL の時は、出力の file（`rtld-many.txt`・`dyntest.txt` など）と、`RTLD-MANY step=… FAIL` の行を返す。

desktop の回帰（Files の PDF の縮小表示 `dlopen("libpdf.so")`、`plan/ws127/tests/files-p002.sh` の 4）は p003 で依頼する。

## 受け入れの条件

1. 手順の grep の確かめが全て 0 行。amd64 の build が warning 0（arm64 は p001 の手順 0 の結果に従う）。
2. T の guest で、上の 3 つが PASS。
3. 次を「結果」に書く: bss の値（p001 と比べて）、dtv を伸ばす道の lock の扱い、退いた dtv の扱い、信号の handler の危険、静的な TLS の 33 個を越える場合の試験の穴、i386・sparcv9 を build していないこと。

## 結果

実装（2026-10-05、P2、worktree p2、p001 の main 統合 9dba1f95 の上）。

- **handle**: `RTLD_HANDLE_CHUNK` 64 の chunk の列（`handle_chunk_first`・`handle_chunk_last`）。`allocate_handle` は空きを `handle_free_slot` で探し、無ければ chunk を足す（取れなければ NULL → `dlopen` は `"cannot allocate dynamic-loader handle"`）。`validate_handle` は chunk ごとに範囲と slot の境を見る。
- **TLS module**: `RTLD_TLS_CHUNK` 33 の chunk の列と `tls_module_at`（id < 33 は最初の chunk を直に）。`register_tls_module` は `tls_module_new_id`（空いた id か次の id、新しい chunk が要れば release でつなぐ。取れなければ `rtld_fatal("cannot allocate TLS module table")`）を使う。中身を書く → `active` を release → 新しい id なら `tls_module_count` を release、の順。`__tls_get_addr` は数と `active` を acquire で読む。
- **dtv（D6）**: 最初の大きさは max(`RTLD_DTV_INITIAL` = 33, `tls_module_count + 1`) で、常に `+ 2` 項目を取る（後ろの 2 つが退いた dtv の鎖）。`tls_module_count` は lock の外で acquire で読む（phase の案の「lock の中で読む」から変えた。後から足された module は `__tls_get_addr` が伸ばすので、古い数でも正しい）。`dtv_grow` は loader の lock の下で、2 倍か module + 1 の大きい方に写し、新しい dtv の `[数]`・`[数 + 1]` に古い dtv の address と数を書き、dtv → 数の順に release で差し替える。古い dtv は書き換えず unmap もしない。`__rtld_thread_free` は今の dtv の block を外し、鎖をたどって全ての dtv を unmap する。dtv を伸ばすのはその thread 自身だけで、他の thread の項目を消す `unload_object_locked` とは loader の lock で順が付く。
- **静的な TLS（D7）**: `layout_static_tls` の `order[]` をやめ、main の module → 他の active な module（id の順）の 2 回で `static_tls_place` を呼ぶ。template への写しは id の順の 1 回の loop（`is_static` で active な物）。
- **dyntest**: `many_handles[200]`。200 個を開いて全て閉じ、もう 1 つ開いて閉じる。表示は `DL:05I:HANDLES-200`。return 35・37・38 は同じ意味で、36 は消した。
- **rtld-many**: `many-tls.c`（static の `__thread int`、偶数は `-mtls-dialect=gnu2` の TLSDESC、奇数は `__tls_get_addr`）で `libtls00.so`〜`libtls39.so`。段は `handles`（`libmany00.so` を 200 回）、`tls`（40 個を開き、初期値を読み、番号 × 10 を書いて読み戻す）、`tls-thread`（もう 1 つの thread でも初期値から。終わった後に主の thread の値が変わっていないこと）、`tls-close`（40 個を閉じ、`libtls39.so` を開き直して初期値 40）。全 16 段。
- **bss**: 94,076（p001 と同じ。handle と TLS の最初の chunk は前と同じ大きさ）。text 31,688 → 33,049、data 412 → 428。
- **確かめ（host、QEMU は起動していない）**: 手順 0 の sysroot の確かめは 0。`make … ld.so libc.so dyntest tlstest.so` は warning 0。`dynamic-userland-check` は PASS。grep（`RTLD_HANDLE_MAX`・`RTLD_TLS_MODULE_MAX`・`handles[`・`tls_modules[`・`too many TLS modules`・`too many dynamic-loader handles` ほか）は 0 行。`build-many.sh build/ws140-p001 build/ws140-p002-many/out` は通った（TLSDESC=300、phnum=31、libtls00 の TLSDESC と libtls01 の DTPMOD64 を確かめた、NEEDED=41）。試験の C の style は違反 0。rtld.c の違反は 236 → 231（増えた関数は無い。減ったのは `__rtld_thread_alloc`・`layout_static_tls`・`register_tls_module`・`validate_handle`）。dyntest は 44 → 40。`git diff --check` も問題無し。
- **残る危険（ws.md の「残る危険」に加えて記録）**:
  - 信号の handler の中で `__tls_get_addr` が dtv を伸ばすと、loader の lock の取り途中の自分を待ちうる（今もある、直していない）。
  - 割り込まれた速い道が古い dtv を読んだ後、handler が dtv を伸ばした場合: 割り込まれた側がその後に新しい block を古い dtv に書くと、新しい dtv にはその block が無い。次の access は別の block を取り、値が分かれる（handler の中で、まだ dtv に入っていない dynamic の module に初めて触った時だけ）。
- **試験の穴**: 起動の時に 33 個を越える静的な TLS の module（2 つ目の chunk を通る `layout_static_tls`）は試験に無い（未実施）。
- **未実施**: T の guest の試験（下の依頼）。arm64・i386・sparcv9 の build（sysroot が要る。Q1 2026-10-05: subagent は sysroot を作らず、amd64 の build と host・guest の試験で判定する）。

## 着手前に直す点（2026-10-04 ユーザーの判断の反映、Q1。詳細は [ws.md](../ws.md) の「判断」）

- U3: **他の固定の上限も入れる**。`tlsdesc_argument[64]`（object ごとの TLSDESC の再配置の数）・`phdr[64]`・`RTLD_NAME_MAX` 64 もこの WS で動的にする（または上限を十分に大きくし、越えたら dlerror で返す。決めは担当の設計で、phase.md に書いてから実装）。範囲・受け入れ・試験（rtld-many に TLSDESC 65 個以上・長い名前の dlopen を足す）へ反映してから着手する。
- U2: memory が取れない時の `dlopen` は今と同じ fatal のまま。
- U5: `dlpi_subs`（`rtld_object_removals`）を TLS を持たない object の unload でも増やす直しを、この WS で入れる。

## T1-163 の後（2026-10-05、P2）

- PASS: tls-check（動的・静的）、dyntest（`DL:05I:HANDLES-200`・`DL:06:PLUGIN-TLS`）、boot-test。
- FAIL ×2: rtld-many。起動の時に `ld.so: cannot map shared object segment`、`exit=127` で止まった（`startup` の段より前）。
- 原因（code を読んでの判断。ld.so は host では動かないので再現はしていない）: `load_object` は最初の load segment だけを場所を指定せずに map し、残りの segment を `MAP_FIXED_NOREPLACE` で base からの位置に置いていた。kernel は空いた範囲を下から first-fit で探す（`vmspace_find_free_range_locked`）。そのため、最初の segment が後の segment の入らない小さな隙間に置かれると、次の segment の場所が埋まっていて `EEXIST` になる。1 page ずつの小さな library が 40 個並ぶと、この隙間に当たる。WS140 の前からある欠陥で、上限が無くなって表に出た。
- 直し: `object_reserve_span` で、load segment 全体の範囲を `PROT_NONE` の匿名 mapping として 1 度に取る（kernel は触れない mapping を commit しない、`vmspace_map_anon_locked`）。そこから base を決め、各 segment は `MAP_FIXED` でその中に置く（0 埋めの匿名の page も同じ）。object が記録する mapping は予約の 1 つだけで、unload はその 1 回の munmap で全部を外す。`map_one_segment` から `choose_base` を外した。
- 確かめ（host）: amd64 の ld.so・libc.so・dyntest は warning 0。dynamic-userland-check は PASS。style は `map_one_segment` が 13 → 9 で、新しい `object_reserve_span` は 0。guest の rtld-many・dyntest・boot-test は T1 の再試験を待つ（startup と dlopen の両方の道が変わったため）。

## Q1 の判定（2026-10-05）

T1-163 の tls-check・dyntest・boot PASS と、T1-164 で rtld-many（ld.so の segment の予約の直しの後）も PASS（amd64）。arm64・i386・sparcv9 の build は sysroot が要り未実施。**cleared**。
