<!-- awesome-plan project=zedbsd record=ws066p001 -->

# ws066-p001: 動的 link の起動の費用の内訳と設計

Phase ID: `ws066-p001`
Parent: [WS066](../ws.md)
Status: in-progress（host の分析と設計の案は済み、guest の時間の測定は T1 待ち）
Queue: q729 の後、Q1（2026-10-05）
Disposition: normal

## 範囲

guest で `/bin/true`・`sh -c :`・`cc t.c -o t` の起動を段階ごとに測る（kernel の exec、`ld.so` の mapping、再配置、symbol の探索、TLS、`libc` の初期化）。再配置と探索の数を object ごとに数える。WS066 の候補から効果の大きいものを選び、実装の Phase に分ける。

## 受け入れ

内訳の表（QEMU の証拠）と、選んだ候補・見込み・影響（toolchain の rebuild の要否、ABI）を書いた設計。実装は含まない。

## 方法（2026-10-05、P2）

QEMU の console log は使わない。数は host で ELF を読み、時間は guest で測る（T1）。

- `plan/ws066/tests/reloc-model.py ROOT PROGRAM...`（host）: program と DT_NEEDED の閉包を、ld.so と同じ順にたどる。順は、program・ld.so・依存を深さ優先（`load_dependencies`）、探索は program → 他 → ld.so（`lookup_symbol_version`）。全ての再配置について、探索の回数、object ごとの probe、bloom の判定、chain の歩み、名前の比較を数える。GNU hash は bloom・bucket・hash の比較・名前、SysV は chain の全ての項目で名前を比べる（`lookup_in_object_hashed`）。版の比較はしない（名前が合えば一致とみなす）。候補ごとの残りも数える（cache、symbolic、lazy、gnu-exe）。入力は staged の rootfs（`/home/awe/zedBSD-claude1/build/uat-0505b/rootfs`、2026-10-05 08:56 の main。読むだけ）。
- `plan/ws066/tests/interpose-check.py ROOT`（host）: libc.so と同じ名前の大域の関数を定義する object を列べる（-Bsymbolic-functions の危険の確かめ）。
- `plan/ws066/tests/startbench.c`・`true.c`・`build-measure.sh`・`startup-measure.sh`（guest、T1）: `posix_spawn` と `waitpid` で N 回起動し、中央値を出す（予熱は N/5 回）。比べる物は次のとおり。
  - `true.c` を 3 通りに link する: 静的、動的で SysV（base の program と同じ）、動的で GNU。
  - 上の動的の 2 つを、`-Bsymbolic-functions` で link し直した libc.so（`LD_LIBRARY_PATH`）でも測る。
  - image の `/bin/true` と `sh -c :`（`sh` は上の libc.so でも測る）。
  - `clang --version` と `cc t.c -o t`（それぞれ上の libc.so でも測る）。

## 内訳（host の数、2026-10-05）

| program | object | 探索 | probe | 名前の比較 | 同じ symbol を除いた探索（cache） | -Bsymbolic-functions の後の探索 | JUMP_SLOT を除いた探索（lazy） |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `/bin/true` | 3 | 517 | 1,036 | 859 | 497 | 21 | 39 |
| `/bin/sh` | 3 | 639 | 1,280 | 1,494 | 618 | 143 | 54 |
| `/bin/ls` | 3 | 568 | 1,138 | 1,256 | 548 | 72 | 42 |
| `/bin/awk` | 3 | 579 | 1,160 | 1,225 | 559 | 83 | 43 |
| `/bin/terminal` | 9 | 1,740 | 6,140 | 4,112 | 1,591 | 841 | 362 |
| `/bin/wayland` | 9 | 1,772 | 6,396 | 4,142 | 1,623 | 873 | 361 |
| `/bin/files` | 11 | 1,885 | 6,811 | 4,639 | 1,736 | 980 | 365 |
| `/usr/bin/ld.lld` | 7 | 10,021 | 42,835 | 21,920 | 7,115 | 8,270 | 8,009 |
| `/usr/bin/clang` | 8 | 14,338 | 72,697 | 32,992 | 10,908 | 12,587 | 10,635 |

- **base の小さな program の探索は、ほとんど libc.so の自分の関数への再配置**: `/bin/true` の 517 回のうち 515 回が libc.so の再配置で、476 が JUMP_SLOT。libc.so を `-Bsymbolic-functions` で link し直すと、loader に残る symbol の再配置は 515 → **19** になった（`build-measure.sh` の実測、`llvm-readelf -r`）。
- **全ての探索が最初に program の SysV hash を引く**: base の program は `--hash-style=sysv` で link される（`platform/amd64/vmunix.mk` の `AMD64_APP_LINK` ほか。toolchain ではなく base の build の規則）。どの名前の探索も、まず program の SysV の chain を名前の比較で歩き、ほぼ全てが外れる（`/bin/ls` で 568 回）。GNU hash なら、外れる探索は bloom の 1 語で済む。
- **大きな C++ の program（clang・ld.lld）は libLLVM・libclang-cpp の再配置が主**: 同じ symbol を指す再配置の cache で探索が 24〜29% 減る。lazy binding では 26% 減にとどまる。RELATIVE が 22 万件（libclang-cpp 167,326、libLLVM 59,475）あるが、symbol を探さない。
- **interposition**（`interpose-check.py`）: libc.so の関数と同じ名前の関数を定義するのは openssh の 7 個（`strvis`・`strnvis`・`vis`、openbsd-compat）だけで、malloc の族を置き換える program は無い。ld.so の `memcpy`・`memset` は探索の最後なので関係しない。`/usr/lib/libc.so` は `/lib/libc.so` と同じ内容の複写。

## 設計の案（guest の時間を見て確定する）

| 順 | 候補 | 見込み（host の数から） | 変える所 | toolchain・ABI |
| --- | --- | --- | --- | --- |
| 1 | libc.so（と base の他の共有 library）を `-Bsymbolic-functions` で link する。malloc の族（`malloc`・`free`・`calloc`・`realloc`・`aligned_alloc`・`posix_memalign`・`memalign`・`valloc`・`malloc_usable_size`）は `--dynamic-list` で置き換え可能のまま残す | base の program の探索が 90% 前後減る（true 517 → 21） | `platform/*/vmunix.mk` の libc.so などの link の規則 | toolchain は変えない。ABI は変わらない（export の表は同じ）。libc の中の呼び出しは、program が同じ名前の関数を定義しても libc の物へ行く（openssh の vis の 3 つ。意味は同じ） |
| 2 | base の program を `--hash-style=gnu`（または both）で link する | 外れる探索ごとに SysV の chain を歩く代わりに bloom の 1 語（ls で 568 回） | `platform/*/vmunix.mk` の `*_APP_LINK` ほか。`tools/build/check-dynamic-elf.py` の role の確かめを合わせる | toolchain は変えない |
| 3 | ld.so で、object ごとに symbol の番号から値への cache（同じ symbol を指す再配置の 2 回目以降は探さない） | clang 14,338 → 10,908、ld.lld 10,021 → 7,115 | `src/rtld/rtld.c` | なし |
| 案 | clang の driver の既定を `--hash-style=both` にする（`cc` で link する利用者の program と、clang・LLVM 自身） | 大きな program の program の側の SysV の探索 | toolchain（Q1 の指示で変えない。案に留める） | toolchain の rebuild |
| 見送り | lazy binding（`-z now` を外す） | 小さな program では 1 と同じ程度だが、clang では 26% 減 | PLT の解決の asm が architecture ごとに要り、relro と thread の扱いが重い | — |

- 受け入れの目標（仮）: `sh -c :` と `/bin/true` の起動が、静的 link との差の半分以上縮む。`cc t.c -o t` が縮む。loader の試験（dyntest・rtld-many）、sh の差分試験、make の差分試験、expat の build は変わらない。数字は T1 の測定で確定する。
- 実装の Phase の案: p002 = 候補 1 と 2（build の規則と ELF の確かめ）、p003 = 候補 3（ld.so）、p004 = 規約。

## 試験の依頼（T1）

- image: `plan/tools/guest/build-full-image.sh BUILD`（clang を含む、`config-amd64-full.mk`）。
- `GUEST_RUNTIME=… sh plan/ws066/tests/startup-measure.sh BUILD` → 出力の `STARTBENCH … median=N us` の 12 行（clang が無ければ 8 行）と `startup-measure: done`。見込みは 2 分以内（推測）。
