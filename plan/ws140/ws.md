<!-- awesome-plan project=zedbsd record=ws140 -->

# WS140: ld.so の依存の数・object の数・handle の数を動的に伸ばす

<!-- awesome-plan-current:start -->
Status: planned
Primary Milestone: MG002
Related Milestones: MG006（GTK4 の起動、[ws115-p010](../ws115/phase010/phase.md)）
Objectives: O1
Parent: [Master](../master.md)
Queue: none（Q1 が割り当てる）
Resume point: p001 から。下の「ユーザーの判断」の U1〜U4 は、どれも p001 の着手を止めない（推奨案のまま進めてよい）。
<!-- awesome-plan-current:end -->

## 目標

2026-10-04 ユーザー:「F-071、F-072、F-070、はWSを立てて計画を作り、他の能力が低いセッションで処理できるようにしてください。」

[F-070](../future-work.md) の内容（2026-10-03、ws115-p010 から）: GTK 4.18.6 で `RTLD_NEEDED_MAX`（16）と `RTLD_OBJECT_MAX`（32）を越えた。
静的な配列を動的な確保に変えて上限を無くす。

ld.so（`src/rtld/`）の次の数の上限を無くす。上限を無くした後に失敗してよいのは、memory が取れない時（`mmap` の失敗）だけとする。

- object ごとの `DT_NEEDED` の数
- process の全 object の数
- `dlopen` の handle の数
- TLS の module の数（object の数に従うので、同時に伸ばす）

## 事実（2026-10-04、P1 が source と build を読んで確かめた）

**F-070 の記述と違う点**: F-070 には「当面は定数を 64・128 に上げた（Q1 の許可）」とある。しかし今の main（908a030）の `src/rtld/rtld.h:27-28` は
`RTLD_OBJECT_MAX 32U`・`RTLD_NEEDED_MAX 16U` のままである。許可は出たが、変更は入っていない
（[ws115-p010](../ws115/phase010/phase.md) の「rtld は未変更（ラップアップの指示が先に来た）」）。Q1 が F-070 の文を直すこと。

### 上限と、その値を使う場所

行番号は main 908a030 の物。

| 上限 | 定義 | 使う場所（`src/rtld/rtld.c`） | 越えた時 |
| --- | --- | --- | --- |
| `RTLD_NEEDED_MAX` 16 | `rtld.h:28` | `struct rtld_object` の `needed_offset[]`・`needed[]`（87-88）。`parse_dynamic` の `DT_NEEDED`（2846）。`unload_object_locked` の局所の配列 `dependencies[]`（4761） | `rtld_fatal("too many dependencies")` |
| `RTLD_OBJECT_MAX` 32 | `rtld.h:27` | `objects[]`（164）、`initialization_order[]`（171）、`tls_modules[RTLD_OBJECT_MAX + 1]`（182）。`new_object`（2556）。`initialize_object`（1874）。`register_tls_module`（3618）。dtv の大きさ: `__rtld_thread_alloc`（554・578・580）と `__rtld_thread_free`（636）。`layout_static_tls` の局所の `order[]`（1729）。`lookup_handle_graph` の範囲の確かめ（5046・5051） | `rtld_fatal("too many shared objects")`、`"initialization order overflow"`、`"too many TLS modules"` |
| `RTLD_HANDLE_MAX` 64 | `rtld.c:139` | `handles[]`（175）。`allocate_handle`（1953）。`validate_handle` の address の範囲（4956-4961） | `dlopen` が NULL を返し、dlerror は `"too many dynamic-loader handles"` |
| 32 bit の bitmask | — | `rtld_dlsym_common` の `uint32_t visited`（4907）と `lookup_handle_graph` の `1U << index`（5054-5056） | object の番号が 32 以上になると shift が未定義になる。上限を上げただけでは壊れる |

### 挙動と制約

- **越えると process が死ぬ**: `dlopen` の中で依存か object の数が越えた時も、`rtld_fatal` で process が終わる。`dlopen` が NULL を返すのではない。
  handle だけは NULL と dlerror を返す。
- **lock を取らずに読む所**: `__tls_get_addr`（713-762）は loader の lock を取らずに `tls_module_count` と `tls_modules[]` を読む。
  `__rtld_dl_iterate_phdr`（1063-1103）も lock を取らずに `objects[]` と `object_count` を読む（C++ の例外の unwind が使う）。
  このため、表を伸ばす時に**今ある要素の address を動かしてはいけない**。realloc のように写して古い領域を捨てる方式は使えない。
- **constructor は lock の外で走る**: `__rtld_dlopen` は `initialize_object` を lock の外で呼ぶ（987-990）。constructor の中から `dlopen` が再び呼ばれうる。
- **ld.so に malloc は無い**: memory は `tls_map`・`tls_unmap`（1532-1605、匿名の `mmap`・`munmap`、page 単位）で取る。
- **大きさ**（main の `build/amd64/dynamic/ld.so`、2026-10-04 10:13 の build、host の `size` と `llvm-nm -S`）:
  - text 29,841・data 400・bss 217,944 byte。
  - bss の内訳: `objects` 209,152（`sizeof(struct rtld_object)` = 6,536 × 32）、`tls_modules` 2,376、`handles` 2,048。
- **libc との ABI**（`include/libc/rtld-abi.h`）: `struct __rtld_tcb` はすでに `dtv` と `dtv_count` を持つ。dtv を伸ばしても struct は変わらない。
  ABI の版（`KERN_RTLD_ABI_VERSION` 6）は上げない（上げる必要が出たら Q1 に相談）。dtv を読むのは rtld.c だけ（libc・asm は読まない、grep で確認）。
- **既存の試験が上限を前提にしている**: `userland/tests/dyntest.c:249-275` は、64 個の handle を開き、65 個目が NULL になることを確かめる（return 35・36）。
  上限を無くすと、この試験は FAIL になる。p002 で書き直す。
- **今回は扱わない固定の上限**: object ごとの `phdr[64]`・`mapping_start/size[64]`・`tlsdesc_argument[64]`、`RTLD_PATH_MAX` 256、`RTLD_NAME_MAX` 64
  （`dlopen` の名前は 63 byte まで）。F-070 の範囲（依存・object・handle の数）の外である（U3）。

## 設計（p001・p002 の共通の決め）

- **D1 chunk の列**: object・handle・TLS module は、固定の大きさの chunk を `next` でつないだ列に入れる。
  - 最初の chunk は今の静的な配列と同じ大きさで bss に置く。object は 32、handle は 64、TLS module は 33（id 0〜32）。
    普通の process は今と同じ memory で動き、`mmap` が増えない。
  - 足りなくなったら、次の chunk を `tls_map` で取って列の後ろにつなぐ。
  - chunk は process の終わりまで解放しない。要素の address は動かない。
  - 列の後ろにつなぐ時は、chunk の中身を 0 にしてから `__atomic_store_n(&last->next, chunk, __ATOMIC_RELEASE)` で公開する。
    その後で数（`object_count`・`tls_module_count`）を `__ATOMIC_RELEASE` で増やす。
  - lock を取らずに読む所（`__tls_get_addr`・`__rtld_dl_iterate_phdr`）は、数を `__ATOMIC_ACQUIRE` で読んでから列をたどる。
- **D2 番号で引く関数**: `objects[i]` の形の参照は全て、`object_at(i)`（i 番目の slot。列を i / chunk の大きさ だけたどる）に置き換える。
  同じように handle は `handle_at(i)`、TLS module は `tls_module_at(id)` で引く。
  - chunk は数個（object が 256 で 8 個）なので、たどる費用は無視できる。
  - `__tls_get_addr` は最初の chunk（id ≤ 32）を、たどらずに直接引く。
- **D3 依存の配列**: `needed_offset[]`・`needed[]` は、object の中の 16 個（`RTLD_NEEDED_INLINE`）を普段使い、越えた object だけ外の配列を使う。
  - `parse_dynamic` で先に `DT_NEEDED` を数える。16 を越える時は、`tls_map` で「数 × (4 + pointer の大きさ)」の配列を取る。
  - 外の配列は `unload_object_locked` で解放する。
- **D4 初期化の順**: 配列 `initialization_order[]` をやめ、`struct rtld_object` の `init_prev`・`init_next` の双方向の list にする。
  - `initialize_object` は list の尾に足す。
  - `remove_initialization_record` は list から外す。
  - `__rtld_process_fini` は尾から頭へたどる。
  - 上限が無くなり、外す操作は O(1) になる。
- **D5 dlsym の訪問の印**: bitmask をやめる。`struct rtld_object` に `lookup_mark`（uint32）を置き、`rtld_dlsym_common` が呼ばれるたびに
  大域の `lookup_generation` を 1 増やす。`lookup_handle_graph` は「mark == generation なら訪問済み、違えば mark = generation にして進む」で判定する。
  - generation が 0 に戻る時は、全 object の mark を 0 にしてから 1 にする。
  - これは loader の lock の中で行う（`rtld_dlsym_common` は lock を取っている）。
  - `lookup_handle_graph` の `objects[]` の範囲の確かめ（5046）は、「NULL でない・active・unloading でない」の確かめに置き換える。
- **D6 dtv を伸ばす**: dtv の最初の大きさは今と同じ 33 項目（定数の名前を `RTLD_DTV_INITIAL` にする）。
  - `__tls_get_addr` で `index->module >= tcb->dtv_count` の時は fatal にせず、loader の lock を取って伸ばす。
    新しい大きさは「module + 1」と「今の大きさ × 2」の大きい方。新しい dtv を取り、中身を写し、古い dtv を `tls_unmap`（大きさは古い `dtv_count`）する。
  - lock を取るのはこの遅い道だけにする。他の thread の `unload_object_locked` も lock の中で `tcb->dtv` を読むので、lock の下で差し替える。
  - `__rtld_thread_free` の `tls_unmap(tcb->dtv, …)` は、定数ではなく `tcb->dtv_count` で大きさを出す。
- **D7 静的な TLS の並び**: `layout_static_tls` の局所の配列 `order[RTLD_OBJECT_MAX + 1]` をやめる。2 回の loop にする。
  1 回目は main の module、2 回目は main 以外の active な module。template を写す loop も同じ 2 回にする。
- **D8 失敗の扱い**: 上限が無いので、失敗は `tls_map` の失敗（memory が取れない時）だけになる。
  - 起動の時と `dlopen` の中の失敗は、今と同じ `rtld_fatal` にする（U2）。
  - handle の chunk が取れない時だけは、今と同じく `dlopen` が NULL と dlerror を返す。
- **D9 規約**: rtld.c の既存の code は全文規約の前の書き方（`/* Returns the computed result. */`・`function_result` など）が多い。
  - 変える関数と新しい関数は、[全文規約](../coding-style.md) に従う。
  - 変えない関数は直さない（無関係な大量の書き換えをしない、Awesome Plan §6）。
  - 新しい static 関数には、file の先頭の forward 宣言の群（rtld.c:350-480 付近）に 1 行の宣言を足す。

## 完了の条件

1. `RTLD_NEEDED_MAX`・`RTLD_OBJECT_MAX`・`RTLD_HANDLE_MAX` が上限ではなくなる。名前は最初の chunk の大きさや object の中に持つ数として残ってよい。
   `rtld_fatal("too many …")`・`"initialization order overflow"` と、handle の数で `dlopen` が NULL を返す道が無い。
2. `plan/ws140/tests/` の多数の依存の試験が、QEMU の guest で PASS する（T1/T2 が流す）。試験は次の全てを確かめる。
   - 40 個の `DT_NEEDED` を持つ library を link した program が起動し、全ての依存の関数を呼べる。
   - 起動と `dlopen` を合わせて object が 160 個を越える。
   - handle を 200 個開いて閉じられる。
   - TLS を持つ module を 40 個 `dlopen` して、2 つの thread から読み書きできる（dtv が伸びる）。
   - 33 個以上の object がある handle の graph で `dlsym` が見つける。
3. 回帰が PASS する: 書き直した `dyntest`、`plan/ws073/tests/p038/run-tls-check.sh`、`plan/tools/boot-test.sh`、Files の PDF の縮小表示
   （`dlopen("libpdf.so")`、`plan/ws127/tests/files-p002.sh` の 4）。
4. amd64 と arm64 の `ld.so`・`libc.so` の build が warning 0 で通る。i386 は 2026-10-02 のユーザーの指示で build しない。
   ld.so の bss の増減を記録する（目安: 今の 217,944 byte から大きく増えない）。
5. 変えた code が全文規約に合う（p003）。
6. GTK4 の `gtk4-widget-factory` が `ld.so: too many dependencies` で止まらないことの確認は、[ws115-p010](../ws115/phase010/phase.md) の再開の時に行う。
   この WS の完了の条件には入れない。GTK の image の build は重く、WS115 の範囲だからである。

## 関係する source の path

- `src/rtld/rtld.c`・`src/rtld/rtld.h`（所有）。`src/rtld/string.c`・`entry-*.S`・`tlsdesc-*.S` は変えない見込み。
- `userland/tests/dyntest.c`（handle の上限の試験の書き直し、p002）。
- `plan/ws140/`（試験・記録）。
- 読むだけ: `include/libc/rtld-abi.h`、`platform/amd64/vmunix.mk`（ld.so の link の規則 835-837、dyntest の規則 1757-1806）、`platform/arm64/vmunix.mk`（383）。

## Guardrail の注意

- rtld は HAL ではない。hal.h は変えない。
- libc の ABI（`rtld-abi.h`）を変える必要が出たら、止めて Q1 に相談する。
- toolchain（`build/llvm` など）は変えない。target の clang は使うだけ。
- 自分の worktree の `build/<名前>/` だけを使う。共有の `build/` を消さない。
- QEMU は自分で起動しない。T1/T2 に依頼する（AGENTS.md「検証」、[protocol](../agents/protocol.md)）。
- 細かい修正ごとに回帰を回さない。build（warning 0）と短い host の確かめまでにする。

## ユーザーの判断

- **U1 対象の範囲**: TLS の module の数（と dtv）も一緒に動的にする（推奨）。object の数を伸ばすと TLS の module も 33 で止まるので、外すと完了の条件 2 を満たせない。
- **U2 失敗の時**: memory が取れない時の `dlopen` を、今と同じ fatal のままにする（推奨。最小の変更）。
  dlerror で NULL を返すようにするなら、読み込みかけの object の巻き戻しが要る。その場合は別の Phase にする。
- **U3 他の固定の上限**: `tlsdesc_argument[64]`（object ごとの TLSDESC の再配置の数）・`phdr[64]`・`RTLD_NAME_MAX` 64 を、この WS に入れるか。
  - 推奨は入れない（F-070 の範囲の外）。
  - 大きな library で TLSDESC が 64 を越える可能性はある（推測、未確認）。越えた時は `rtld_fatal` が出るので見分けられる。
- **U4 F-070 の文**: 「定数を 64・128 に上げた」は事実と違う。Q1 が F-070 を直す（この WS の作業ではない）。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | object の chunk の列（D1・D2）、依存の可変長（D3）、初期化の順の list（D4）、dlsym の印（D5）と、多数の依存の試験（startup と dlopen の object・依存・dlsym） | planned | — |
| [p002](phase002/phase.md) | handle と TLS module の chunk の列（D1・D2）、dtv を伸ばす（D6）、静的な TLS の並び（D7）、dyntest の handle の試験の書き直し、TLS と handle の多数の試験 | planned | p001（同じ file。p001 の commit の上に重ねる） |
| p003 | 全文規約の見直し（変えた関数と試験）、amd64・arm64 の build、bss の記録、T への回帰の依頼（完了の条件 2・3）、F-070 と ws115-p010 への結果の反映の案 | planned | p001・p002 |
