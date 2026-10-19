<!-- awesome-plan project=zedbsd record=ws276-p001 -->

# ws276-p001 m68k の静的な TLS の配置（elf.c・static-tls.c）と名前の食い違いの調べ

- 親: [ws276](../ws.md)、Queue: q1304 / q1304-i01（P2、worktree `/home/awe/zedBSD-worktrees/p2`、branch agent/p2-q1258、main 2b9733149 を merge してから）
- Status: cleared（2026-10-19 Q1 の判定、T1-741: amd64・pcat の静的な TLS の道を書き直した後の起動の回帰 PASS、x68k の build PASS。m68k は host 試験だけ（MAME・実機は範囲外））
- 規約: [coding-style.md](../../coding-style.md) の全文を新しい code に当てた。出典の規則（guardrail）: 外の source の code・値は使っていない。m68k の 0x7000 は m68k の TLS の ABI（compiler と GNU ld の振る舞い）で、下の probe で確かめた事実。

## 設計

- m68k の ABI（TLS variant I）: compiler は `__m68k_read_tp` を呼んで A0 の thread pointer（TP）から負の offset で読む。GNU ld（m68k-linux-gnu-ld、x68k の user.ld）は local-exec の offset を「変数の vaddr − PT_TLS の p_vaddr − 0x7000」にする。よって TLS の block の先頭は TP − 0x7000 で、block の先頭は p_vaddr と同じ整列でなければならない。probe で確かめた（`TLS 0x401000 align 0x10 memsz 0x14` で `probe_word`（offset 0xc）は `%a0@(-0x6ff4)`、16 byte 整列の `probe_aligned`（offset 0）は `%a0@(-0x7000)`）。
- zedBSD の配置（q1301 の m68k-read-tp.c の前提に合わせた）: mapping の先頭に control block（`struct __rtld_tcb`、`thread_self(GET_TLS)` の値）とその予約 `KERN_TLS_TCB_RESERVE`（4096）、その直後（page 境界）から TLS の block。TP = control + 4096 + 0x7000。block の先頭は page 境界なので、p_vaddr が自分の整列に揃っていない segment は変数が揃わない → kernel は `ENOEXEC` で拒む（m68k だけ。GNU ld の出力は揃っている）。
- 共通化: 配置の計算を `include/uapi/tls.h`（loader と libc の共通の prefix の header）の `static __inline` 2 つに置き、kernel と libc が同じ計算を使う。
  - `KERN_TLS_VARIANT_I`・`KERN_TLS_VARIANT_II`、`KERN_TLS_STATIC_VARIANT`（`__m68k__` だけ I、他は今までどおり II）。
  - `kern_tls_static_distance(vaddr, memsz, align, variant)`: II は今までの `memsz + ((0 − vaddr − memsz) & (align − 1))`、I は `memsz`（`kern_tls_prefix.distance` に入る値。libc の clone の検査 `distance >= memory_size` はそのまま成り立つ）。
  - `kern_tls_static_layout(distance, page, variant, &layout)`: `mapping_size = round_page(distance) + 4096`。II は control = payload の末、block = payload − distance（今までどおり）。I は control = 0、block = 4096。
- elf.c: `load_static_tls` の宣言・定義・呼び出しの guard に `HAL_ARCH_M68K` を足し、本文は layout の offset で control と block を置く（amd64・i386 の算術は同じ）。thread pointer として返す値は control（amd64・i386 は今までどおり TP = control）。誤って関数の上にあった `validate_and_load` の注記を本来の位置へ戻した。
- static-tls.c: `__rtld_thread_alloc` が同じ layout で mapping を作り、template を `mapping + block_offset` に写す。`static_thread_pointer` は m68k では今までどおり `thread_self` に聞く（m68k に TP の register は無い）。
- m68k-read-tp.c の「kernel はまだその配置を作らない」の注記を直した。docs/reference/tls.md に m68k の段落（題を「Static executable TLS (x86, m68k)」）。
- 範囲外: rtld.c（動的、m68k に rtld は無い）は変えていない。arm64・riscv64・sparcv9 などは variant II のまま（kernel の静的 TLS も無いまま）。hal.h は変えていない。

## commit

| SHA | 中身 |
| --- | --- |
| 73fde3c30 | include/uapi/tls.h、src/kern/elf.c、userland/base/libc/static-tls.c、userland/base/libc/m68k-read-tp.c、docs/reference/tls.md、plan/ws276/tests/tls-layout/（host 試験） |

## 確かめ（host、2026-10-19）

- host 試験: `plan/ws276/tests/tls-layout/run.sh`（repo の root から、出力 build/ws276-p001）→ `PASS: tls layout (pointer 64 bits, 5 m68k accesses)`、`PASS: tls layout (pointer 32 bits, 5 m68k accesses)`。中身:
  - variant II（amd64・i386）: size 13 種（0〜1 MiB）× 整列 8 種（1〜4096）× vaddr 6 種で、distance・mapping_size・control・block の offset が前の elf.c の算術と一致、block の先頭が vaddr と同じ剰余。64 bit と `-m32`。
  - variant I（m68k）: control 0、block 4096、block が mapping に収まる、m68k-read-tp.c の TP − 0x7000 = block の先頭、整列。
  - GNU ld の実際の解決: probe.c（整列 4・1・16 と .tbss の変数）を x68k の user.ld で link し、readelf・nm・objdump から 5 つの読みの (vaddr, align, memsz, 変数の位置, 変位) を取り、TP + 変位 = block の先頭 + (変数 − p_vaddr) を確かめた。変位を 1 ずらすと FAIL になる事も見た。
- amd64・i386 の object の比較: 変更前（HEAD の elf.c・static-tls.c・tls.h を build/p2-old に取り出し）と後を LTO 無しで同じ flag で compile して llvm-objdump を比べた。register の割当と順序が違う（同じ値の計算）。バイト一致ではないので、振る舞いの同一の根拠は上の host 試験の算術の一致。
- build（warning 0、`warning`・`error:` の数 0）:
  - x68k の既定の goal: `make -j16 BUILD=build/p2-x68k ZEDBSD_CONFIG=plan/tools/ports/config-x68k.mk` exit 0、`X68k image check: PASS`（build/tmp/ws276-x68k.log）。elf.c・static-tls.c・m68k-read-tp.c を compile し直した。m68k の preprocess で `load_static_tls` の呼び出しと整列の検査が有効な事を確かめた。
  - m68k-virt: `make -j16 BUILD=build/p2-m68k-virt ZEDBSD_CONFIG=plan/tools/ports/config-m68k-virt.mk build/p2-m68k-virt/vmunix` exit 0（build/tmp/ws276-m68kv.log）。
  - amd64: `make -j16 BUILD=build/p2-amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p2-amd64/vmunix build/p2-amd64/user64/userland/base/libc/static-tls.o build/p2-amd64/user64/userland/base/libc/pthread.o` exit 0、`amd64 vmunix check: PASS`（build/tmp/ws276-amd64.log）。
  - pcat: `make -j16 BUILD=build/p2-pcat ZEDBSD_CONFIG=config/ci/config-pcat.mk build/p2-pcat/vmunix build/p2-pcat/userland/base/libc/static-tls.o` exit 0（build/tmp/ws276-pcat.log）。
- x68k の root で空でない PT_TLS を持つ program は `bin/lp`・`bin/lpr`・`sbin/ntpdate` の 3 つ（vaddr 0x40a32c・0x409320、align 2、memsz 0x28c、filesz 0）。どれも整列に揃っていて、今は exec が block を作る（以前は ENOEXEC）。他の 42 の PT_TLS は memsz 0（exec は無視する）。
- `git diff --check` 問題なし。
- 未実施: x68k の MAME・実機での lp・ntpdate の実行（ws.md で範囲外、ユーザーの指示まで）。m68k-virt は userland を持たないので QEMU で exec を確かめる手段が無い。sysroot（build/amd64/sysroot など）の作り直し（libc.a に static-tls.c が入る。object だけを作った）。

## ZEDBSD_STATIC_TLS と KERN_STATIC_TLS（調べ）

- 事実:
  - `KERN_STATIC_TLS` を見る source: userland/base/libc/pthread.c（fenv・errno の 2 箇所）、utmpx.c、account.c、crypt.c、src/libc/random.c、src/libc/ftw.c。どれも `#if defined(KERN_DYNAMIC_LIBC) || defined(KERN_STATIC_TLS)` で、真なら状態（errno・fenv・random の状態・ftw・utmpx・account・crypt の buffer）を `_Thread_local` にする。偽なら pthread の tcb か static の変数。どこも定義しない（`KERN_DYNAMIC_LIBC` は amd64・pcat・pc98・arm64・riscv64 の動的 libc の build が定義する）。
  - `-DZEDBSD_STATIC_TLS` を渡す所: toolchain/llvm/sysroot.mk の 2 行（amd64・i386・arm64・riscv64 の sysroot の libc と compiler-rt）、platform/x68k/vmunix.mk の `M68K_USER_CPPFLAGS`、platform/sparcv9/vmunix.mk。読む source・header・LLVM の patch・clang の header は無い（grep で 0）。
  - 履歴: 両方とも orphan commit 846464d1e（2026-10-03）に既にある。その commit の plan/ws035 の参照の表（refs.tsv）に sysroot.mk の `-DZEDBSD_STATIC_TLS` の行が記録されている（reset の前から sysroot.mk にあった）。x68k（e932a5431）と sparcv9（e0ee0e1e1、どちらも 2026-10-19）は sysroot.mk を写した。
- 判断: **source の側の `KERN_STATIC_TLS` が正しい名前**（compile の時の switch の接頭辞は `KERN_`、例 `KERN_DYNAMIC_LIBC`・`KERN_USER_ABI_*`）。`ZEDBSD_STATIC_TLS` は接頭辞の改名（ZEDBSD_ → KERN_）で makefile が取り残された物と推定（改名の記録は reset の前の履歴にしか無く、未確認）。
- 直し方（推奨）: **`-DZEDBSD_STATIC_TLS` を消すだけにし、`-DKERN_STATIC_TLS` に書き換えない。** 消しても誰も読まないので object は変わらない（振る舞いの変化 0）。書き換えると次が起きる:
  - sysroot の静的な libc の errno などが TLS になり、errno を使う全部の静的な program が空でない PT_TLS を持つ。arm64・riscv64（と sparcv9）の kernel は静的 TLS の block を作らない（elf.c は amd64・i386・m68k だけ）ので、それらの静的な program が全部 `ENOEXEC` になる。
  - x68k では errno を読むたびに `__m68k_read_tp`（thread_self の syscall）を呼ぶので遅い。
  - amd64・i386 でも静的な program の TLS の大きさと thread の作り方が変わる（意味のある ABI の変更）。
- 担当: sysroot.mk の 2 行は Q1（toolchain）。sparcv9 の 1 行はユーザー。x68k の 1 行は 3 つを揃えるため今回は消していない（Q1 の判断で P2 が消せる。`platform/x68k/vmunix.mk` の `M68K_USER_CPPFLAGS` の末の `-DZEDBSD_STATIC_TLS`）。`KERN_STATIC_TLS` を arch ごとに有効にするかは別の判断（kernel の静的 TLS がある arch だけ、x68k は速さを測ってから）。

## T1 への依頼

- m68k: **host 試験だけ**（m68k-virt は userland を持たず、x68k の MAME・実機は範囲外）。
- 任意（amd64・i386 の elf.c の静的 TLS の道を書き換えたので、短い回帰）: Q1 が merge した main で、amd64（`config/ci/config-amd64.mk`）と pcat（`config/ci/config-pcat.mk`）の既定の起動の回帰を他の依頼とまとめて 1 回。合格: 両方で login prompt（`plan/tools/boot-test.sh`、PNG）。login までの exec が空でない PT_TLS の静的な program を通るかは未確認。TLS を使う静的な program を名指しで流す試験は今の tests/ に無い。

## 残り

- Q1: merge と判定、sysroot.mk の `-DZEDBSD_STATIC_TLS` の削除（上）、x68k の 1 行の扱い、sparcv9 の 1 行をユーザーへ。
- Q1 に片付けを頼む path（P2 は rm しない）: build/p2-old/、build/ws276-p001/、build/p2-x68k/elf-check.i。
- x68k で lp・lpr・ntpdate が実際に動く確かめは MAME・実機で（ユーザーの指示まで）。
