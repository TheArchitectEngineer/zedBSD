# ws032-p005 結果: C++ ランタイム（2026-09-21）

q315-i05。libunwind → libc++abi → libc++ をクロスビルドし、**実機で例外・RTTI・
static initializer が動くことを確認した**。

## 1. 成果物

`userland/packages/devel/libcxx/` に新規パッケージ。compiler と同じ
LLVM 23.1.0 tarball（`toolchain/llvm/version.mk` で pin 済み）の `runtimes` ツリーを
使い、**二重取得しない**。

| 成果物 | SONAME | DT_NEEDED | 契約検査 |
| --- | --- | --- | --- |
| `libunwind.so.1` | `libunwind.so.1` | `libc.so` | PASS |
| `libc++abi.so.1` | `libc++abi.so.1` | `libc.so`, `libunwind.so.1` | PASS |
| `libc++.so.1` | `libc++.so.1` | `libc++abi.so.1`, `libunwind.so.1`, `libc.so` | PASS |

配置は `/usr/lib`（ユーザー指示）。`/lib` はベースシステム自身が要るもの、`/usr/lib` は
パッケージで来るもの、という切り分け。ローダは両方を探す。

## 2. 実機での確認（QEMU/KVM）

```
CXX ok   static initialiser
CXX ok   vector
CXX ok   string
CXX ok   unique_ptr
CXX ok   exception through 5 frames
CXX ok   out_of_range from the library
CXX verdict: PASS (5/5)
```

`static initialiser` は main より前に走る大域オブジェクト、`exception through 5 frames`
は再帰5段を巻き戻して `catch` に到達すること、`out_of_range` は**ライブラリ側**
（libc++.so）で投げた例外が実行ファイルの `catch` に届くことを見ている。
つまり**オブジェクトをまたぐ巻き戻しが成立している**。

## 3. このために libc へ足したもの

libc++ のビルドが実際に要求したものだけ。すべて POSIX.1-2008 または C++ ABI が要求する。

| 追加 | 理由 |
| --- | --- |
| `<link.h>` と `dl_iterate_phdr` | 巻き戻し器が `PT_GNU_EH_FRAME` を探すのに使う。ローダ側は `__rtld_dl_iterate_phdr` として実装し、private ABI を版 5→6 へ |
| `strerror_r` | POSIX |
| `mbsnrtowcs` / `wcsnrtombs` | POSIX。長さ上限つきの変換 |
| `_l` 系 36 関数 | POSIX.1-2008。`strcoll_l`・`strxfrm_l`・`toupper_l`・`tolower_l`・`isw*_l` 15種・`wcscoll_l`・`wcsxfrm_l`・`localeconv_l`・`strtof_l`/`strtod_l`/`strtold_l`・`mbrtowc_l` ほか変換7種・`snprintf_l`・`asprintf_l`・`nl_langinfo_l` |
| `MB_CUR_MAX_L` | 同上 |
| `struct lconv` の `int_*` 6 メンバ | C99。国際通貨表記の配置 |
| `fileno` の宣言 | 実装はあったが `<stdio.h>` に宣言が無かった |
| `__cxa_atexit` / `__cxa_finalize` | Itanium C++ ABI。static オブジェクトの破棄登録。既存の atexit 機構を拡張して1本の列で逆順に走らせる |
| `__cxa_thread_atexit(_impl)` | 同。thread_local の破棄を pthread key で持つ |

**locale の `_l` 系は C ロケールのみ対応**で、ロケール引数を検査したうえで
ロケール非依存の相当関数へ委譲する。これはロケール対応の制限であって関数の制限ではない。
他ロケールの照合順序が入るときに、ここがその適用点になる。

## 4. 公開ヘッダの移植性修正（C++ から使えるようにする）

| 問題 | 対応 |
| --- | --- |
| `restrict` は C++ のキーワードではないため、`char *restrict` がパラメータ名の再定義になる | `libc/include` の 5 ファイル 31 箇所を `__restrict` へ（C/C++ 両方で使える） |
| 宣言が `extern "C"` で囲まれておらず、C++ から見ると全部 mangle される | 166 公開ヘッダのうち**宣言を持つ 95 本**に `#ifdef __cplusplus extern "C" {` を機械的に追加（既に持つ 9 本、宣言の無い 62 本は対象外） |

C 側の `make world` が warning 0 で通ることを確認済み。

## 5. クロスビルド契約の追加（wrapper）

| 追加 | 理由 |
| --- | --- |
| `-ftls-model=global-dynamic` | 共有オブジェクトの既定として安全な側に倒す。dlopen で後から来たオブジェクトは静的 TLS 領域に入れないため、initial-exec だとリンク時に落ちる。実行ファイル側はリンカがどのみち local-exec へ緩和するので、この指定は効かない（「9. 実行ファイルの TLS」参照） |
| `-Wl,--eh-frame-hdr` | `PT_GNU_EH_FRAME`（巻き戻し器が引く索引）を作る。**これが無いと例外は handler を見つけられず terminate する**。実測で確認した |

`--eh-frame-hdr` は本来 clang driver の linker job に属する。wrapper に置いたのは、
driver へ入れると `ZEDBSD_LLVM_PATCH_LEVEL` を上げてツールチェイン全体の再ビルドと
キャッシュ資産の再公開が要るため。**次にツールチェインを更新するときに driver へ移す**。

## 6. libc++ へ当てたパッチ（1本）

`0001-zedbsd-locale-backend.patch`: `__locale_dir/locale_base_api.h` に zedBSD の分岐を
追加し、`support/bsd_like.h` を選ばせる。zedBSD の libc は同ファイルが呼ぶ POSIX の
`_l` 群を持つため、この backend が実態に合う。分岐が無いと移行期の AIX 用ヘッダに
落ちて compile できない。

## 7. 記録した制限

- ~~**実行ファイル自身が TLS セグメントを持てない**~~ **解消した**。ローダに静的 TLS
  領域を実装した（後述「9. 実行ファイルの TLS」）。試験プログラムに `thread_local`
  を戻し、実機で 7/7 通ることを確認済み。p008（clang）への制約も無くなったので、
  `LLVM_LINK_LLVM_DYLIB` は性能・サイズだけで決めてよい。
- libc++ のリンクから `-Wl,-z,defs` を外している。ローダが `__tls_get_addr` を供給し、
  共有 libc も同じ理由でそれを未定義のまま持つため、未定義シンボルを禁じるリンクとは
  両立しない（`build.ninja` から sed で除去。記録した適応）。
- `LIBCXX_HAS_MUSL_LIBC` は OFF。これは「glibc でない」ではなく「musl である」の意味で、
  ON にすると musl 内部ヘッダ `bits/alltypes.h` を要求して失敗する。
- `_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE` を指定。ctype facet の文字分類表を
  システム側（BSD の rune table や glibc の `__ctype_b_loc`）から取れないため、
  libc++ が持つ既定表を使う。

## 9. 実行ファイルの TLS（静的 TLS 領域の実装）

p005 で「実行ファイルは `PT_TLS` を持てない」と記録したが、これは
`tools/build/check-dynamic-elf.py` が古いのではなく、**実装が無かった**。実機で
切り分けた結果、独立した欠陥が 2 つあった。

| 症状 | 原因 |
| --- | --- |
| `__thread int counter = 42; counter++` が `43` でなく **`1`** を返す（クラッシュしない） | リンカは実行ファイル自身の TLS アクセスを必ず local-exec（`movq %fs:0x0` + 固定変位）へ緩和する。再配置は残らない。しかし動的実行ファイルには TP の下に領域が無く、無関係なメモリを読んでいた |
| `thread_local std::string`（`filesz == 0` の `PT_TLS`）が `ENOEXEC` | `src/kern/elf.c` がテンプレートの `PT_LOAD` 包含を要求する。全部 `.tbss` の `PT_TLS` はファイル実体を持たず、どの `PT_LOAD` にも一致しない |

静的リンク実行ファイルでは動いていた。カーネルは `interp_count == 0` のときだけ
`load_static_tls()` で TP を用意し、`libc` の `static-tls.c` が `kern_tls_prefix` を
複製する。動的実行ファイルでは rtld が `tcb->tls` をゼロ埋めするだけだった。

### 直したもの

| 箇所 | 変更 |
| --- | --- |
| `src/kern/elf.c` | `filesz == 0` の `PT_TLS` は包含要求を免除。`filesz > 0` の経路は挙動不変 |
| `userland/base/rtld/rtld.c` | 起動時集合（実行ファイル＋`DT_NEEDED` 閉包）を 1 つの静的 TLS 領域へ配置する `layout_static_tls()` を追加。variant II、実行ファイルを先頭に置く（リンカが既にその変位に合意しているため）。テンプレート像を 1 度だけ作り、各スレッドが複製する |
| 同上 | `__rtld_thread_alloc` が TCB と静的領域を 1 つのマッピングに確保し、`kern_tls_prefix` を埋める（従来は TCB 単体、`distance = 0`）。`__rtld_thread_free` は `mapping_base`/`mapping_size` を解放する |
| 同上 | `__tls_get_addr` は静的モジュールを DTV を介さず `TP - static_offset + offset` で返す。dlopen 後のオブジェクトは従来どおり DTV |
| 同上 | `R_X86_64_TPOFF64`（initial-exec）に対応。静的領域に無いモジュールへの IE は明示的に失敗させる |
| `tools/build/check-dynamic-elf.py` | `PT_TLS` の一律拒否をやめ、`PT_TLS` は 1 本まで・`filesz <= memsz`・`memsz <= 1 MiB`・アラインは 4096 以下の 2 冪、という実際の上限（`include/uapi/tls.h`）の検査に置き換えた |

variant I（arm64・sparc）は TP の上へ数え、TCB を基点に置くため `kern_tls_prefix` で
記述できない。カーネルが同じ理由で静的 TLS を断っているので、ローダも
`#if !AMD64 && !I386` では従来どおり全モジュールを動的のままにする。TLSDESC は
実アドレスの差を返すので、この配置でも正しく動く。

### 実機での確認

```
$ /usr/bin/tlsinit
main counter=43 name=main      <- テンプレートが複製されている
thread counter=42 name=main    <- 新スレッドは初期値から始まる
thread after=142
main again=43                  <- スレッド間で独立している

$ /usr/bin/cxxtls
CXX ok   thread_local          <- filesz == 0 の PT_TLS、非自明なコンストラクタ/デストラクタ
CXX verdict: PASS (7/7)
```
