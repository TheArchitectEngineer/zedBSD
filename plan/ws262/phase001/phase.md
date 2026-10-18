<!-- awesome-plan project=zedbsd record=ws262-p001 -->

# ws262-p001 boot loader を規約の全文に

Status: cleared（2026-10-19 Q1 判定: bootloader/ の全部を規約の全文に（走査で違反 0）、前後の比べで code の差は C の分解・分割の所だけで host の同値の試験が一致、動作の直しは console_ascii の 1 件（ASan で確かめた）、T1-712 で amd64 UEFI・amd64 hybrid の BIOS・PC/AT・PC-98 の起動が PASS（c0c84eefd、merge 452e53f99）。x68k・sparcv9 の起動は対象外（WS268・WS267））（旧: in-progress（2026-10-18 q1256-i01、P2））

- 親: [WS262](../ws.md)。Queue: q1256-i01。
- 範囲: `bootloader/` の全部（uefi・pcat・pc98・bios・x68k・common・include・tests、最後に sparcv9）を
  [plan/coding-style.md](../../coding-style.md) の全文に合わせる。書き方だけを直し、動作は変えない。
- 守ること: source の comment に WS・Phase の番号、plan/ の path、試験の進め方を書かない（guardrail「source の中の開発の管理情報」）。
  既存の `ws035-p096`・`ws035-p112`・`ws174` などの印はこの Phase で消す。

## 確かめの方法

1. **基準の build**: ed2f1b265 の source（Q1 の uefi の commit cc6aa1256 の前）で loader ごとに
   `make ZEDBSD_CONFIG=<config> BUILD=build/p2-ws262/<p>-before bootloader -j8`（`<p>` = amd64・pcat・pc98。config は `config/ci/config-<p>.mk`）。
   x68k（`plan/ws035/tests/config-x68k.mk`）は基準の時点で build できない（下の「既知の欠け」）。
2. **後の build**: 同じ命令で `BUILD=build/p2-ws262/<p>-after`。warning を数える（`grep -ci warning`）。
3. **binary の比べ**: [tests/compare-build.sh](../tests/compare-build.sh) BEFORE AFTER。全部の出力を byte で比べ、違う ELF・object は
   `llvm-objdump -d -s` の中身で比べ直す（`DIFF-NONCODE` は記号表・file 名だけの差、`DIFF-CODE` は code か data の差）。
   object は BUILD の directory の名前が入るので、同じ source でも `DIFF-NONCODE` になる（基準と main 709e6ff12 の比べで確かめた。BOOTX64.EFI は byte で同じ）。
4. **動作の同値の host 試験**: 条件の分解・関数の分割で code の並びが変わる file は、byte の一致を求めず、
   [tests/diff-run.sh](../tests/diff-run.sh) BASE OUT で基準の source と今の source の純粋な C の helper を host で build し、
   同じ乱数の入力（[tests/diff-driver.c](../tests/diff-driver.c)、20000 回）の出力の全部が一致する事を確かめる。

## Q1 の uefi の commit（main cc6aa1256）

- main の checkout にあった bootloader/uefi/boot-keys.h・bootx64.c の差分（ユーザー「あなたがコミットしてOK」で Q1 が commit）。
- 確かめ: ed2f1b265 の基準と main 709e6ff12 の amd64 の build を比べ、`BOOTX64.EFI` は byte で同じ、object は全部 `DIFF-NONCODE`（code の差なし）。
  この差分は動作を変えていない。

## 既知の欠け（直さない）

- x68k: `bootloader/x68k/stage2.c` が `struct x68k_boot_handoff` を使うが、tree のどこにも定義が無く、基準の ed2f1b265 で既に build できない
  （Q1: 2026-10-03 のリポジトリの作り直しの時からの既知の欠け、WS266 m68k に記録）。比べは build/ の中の仮の header で型を補った前後の object で行う。

## file ごとの点検

| file | 直した事 | 比べ |
| --- | --- | --- |
| common/memory-map.h | file の説明、macro・enum・typedef の役の comment | — |
| common/memory-map.c | file の説明。条件の中の関数の呼び出し（`range_attributes_equal`）を外へ、`?:` と `ranges[used++]`・`return used == 0 ? …` を if と文に分け、成功の return を最後に。`get_range` の `result != OK \|\| size == 0` を失敗と空に分けた。`range_attributes_equal` の Boolean の式を if に。各段落・if・return の comment | DIFF-CODE（register の割り当て・命令の並び）。host の同値の試験で一致 |
| common/logo-path.h | `ws035-p096` を消した | — |
| common/logo-path.c | `ws035-p096` を消した。3 つ以上の節の条件を 1 行 1 節に、関数の呼び出しの comment、if の前の空行、loop の意図の comment、return の comment | byte で同じ |
| common/boot-override.h・.c | 点検のみ（規約に合う） | byte で同じ |
| bios/memory-map.h | file の説明、struct・enum の役の comment | — |
| bios/memory-map.c | file の説明。複数の節の条件を 1 行 1 節に、`?:` の return を if に、`zbl_memory_normalize` の結果の直の return を失敗と成功に分けた。各段落・return の comment | byte で同じ |
| bios/fat-directory.h | macro・struct の役の comment、宣言の comment を動詞の形に | — |
| bios/fat-directory.c | 全部の static 関数の前方宣言、公開の関数を先に。`for` の初期化の宣言・block の中の宣言を関数の先頭へ。条件の中の関数の呼び出し（`read_le16`・`short_name_checksum`・`ascii_fold`）を外へ（LFN の項の検査は `long_name_entry_continues` に分けた、評価の順は同じ）。`matches = … && (… \|\| …)` を if の連鎖に、`?:` の checksum を if に、`visible[length++]` を文に分けた。macro・各段落・return の comment | DIFF-CODE（全部が 1 つの関数に inline され、register の割り当てが変わる）。host の同値の試験で一致 |
| bios/logo.h | `ws035-p096`・`ws035-p112` を消した | — |
| bios/logo.c | `ws035-p096`・`ws035-p112` を消した。3 つ以上の節の条件を 1 行 1 節に、閉じた if の後の段落の空行と comment、`logo_contain_first` の return の comment の位置 | byte で同じ |
| include/amd64-handoff.h | file の説明を直し、全部の struct の役の comment、macro の群ごとの comment。「The UEFI version preserve」の文法 | code の差なし |
| include/amd64-kernel-image.h | 著作権の header を規約の形に、file の説明 | code の差なし |
| include/mbr.inc・fat16.inc・elf.inc | 著作権の header と file の説明を足した | code の差なし |
| include/stage2-header.inc | 著作権の header と file の説明 | code の差なし |
| include/disk-layout.inc | 複数行の comment の区切りを独立の行に | code の差なし |
| include/boot-parameter-record.inc・boot-parameter-handoff.h | 点検のみ | — |
| pcat/bootzbsd.S | 複数行の comment の区切り（3 か所、[tests/comment-fix.py](../tests/comment-fix.py)）、`ws035-p096`・「p003 parser」を消した | bootzbsd.o は code の差なし |
| pcat/stage1.S・stage2-chain.S・partition-pbr.S・vbe.inc | 点検（著作権の header・file の説明・複数行の comment の区切り [tests/comment-check.py](../tests/comment-check.py)・開発の管理情報の grep）。直す所なし。命令の行ごとの comment は C の規約の対象外として変えていない | byte で同じ |
| pcat/*.ld（stage1・stage2・bootzbsd） | 著作権の header と説明を足した | 出力は byte で同じ |
| pc98/bootzbsd.S | 複数行の comment の区切り（14 か所）、「p003 parser」を消した | bootzbsd.o は code の差なし |
| pc98/disk-ipl.S | 「QEMU debug port E9」を「I/O port E9 (an emulator's debug console)」に | byte で同じ |
| pc98/lba2.S・partition-pbr.S・bootstrap.inc | 点検（上の pcat と同じ項目）。直す所なし | byte で同じ |
| pc98/*.ld（stage1・stage2） | 著作権の header と説明 | 出力は byte で同じ |
| x68k/boot-layout.h | UTF-8 の BOM を消した、著作権の header、file の説明、macro の群と struct の役の comment | — |
| x68k/stage1.S・stage2-start.S・iocs.S | BOM を消した、著作権の header、file の説明。stage1.S の file の先頭の「IOCS register contracts follow NetBSD/x68k stand/libiocs」を最初の IOCS の block の「Procedure referenced:」へ移した（guardrail「出典は該当箇所に block 単位で」） | stage1.o・stage2-start.o・iocs.o・stage1.elf は byte で同じ |
| x68k/*.ld | BOM を消した（ld の「ignoring invalid character」の warning 3 行が消えた）、著作権の header と説明 | stage1.elf は byte で同じ |
| x68k/stage2.c | BOM を消した。著作権の header と file の説明。file の構成の順（macro・extern・table の初期化に要る前方宣言・file-scope 変数・前方宣言・公開の関数・static 関数）、全部の static 関数の前方宣言。C99 の指示付きの初期化子を ANSI C の位置の初期化子に（`struct x68k_spc_bus` の field の順と同じ）。`(void)cookie` を局所の `UNUSED_PARAMETER`。関数の定義の形（引数を 1 行ずつ）。block の中・初期化子の中の宣言と関数の呼び出しを関数の先頭と文へ。条件の中の関数の呼び出し（`be16`・`be32`・`read_sectors`・`read_bytes`・`allowed_segment`・`drv_x68k_spc_pio_init`）を外へ（manifest の検査は `manifest_valid`、PT_LOAD の検査は `segment_acceptable`・`segment_overlaps` に分け、評価の順は同じ）。`?:` の return・Boolean の式の return を if に、`*d++ = *s++` を文に。関数の呼び出しの直の return を失敗と成功に。各段落・if・loop・return の comment | 下の「x68k の比べ」 |
| tests/payload32-pcat.S・payload64-pcat.S・payload32-pc98.S | 著作権の header を規約の形に、file の説明 | object は code の差なし（旧の source の object と比べた） |
| tests/*.ld | 著作権の header と説明 | — |
| README.md | `q032`・「compatibility Phase」・`WS013 p006` を消した | — |
| uefi/boot-keys.h・boot-keys.c | 点検のみ（Q1 の commit cc6aa1256 の形のまま、規約に合う） | code の差なし |
| uefi/bootx64.c | file の説明、macro の群・struct・typedef・extern・file-scope 変数の役の comment、`ws035-p112`・`ws174`・`ws035-p096` を消した。全部の static 関数の前方宣言、`efi_main` を先に。block の中・`for` の初期化の宣言と scope だけの block（CR4）を関数の先頭へ。条件の中の関数の呼び出し（`framebuffer_from_gop`・`guid_equal`・`zbl_uefi_partition_paths_same_disk`・`zbl_uefi_volume_selection_take`・`zbl_uefi_partition_identity_copy`・`zbl_uefi_framebuffer_mapping_plan`・`token_is`・`kernel_path_utf16`・`zbl_elf64_plan`・`zbl_elf64_place`・`kernel_placement_parse`）を外へ。`?:` で呼び出しを選ぶ所（`Close`・`FreePages`・`FreePool`）を `close_file`・`free_pages`・`free_pool` に、文字列・状態の `?:` を if に。関数の呼び出しの結果の直の return を失敗と成功に分けた（`probe_readable_file` の最後の `SetPosition` の警告の値は EFI_SUCCESS になる。呼び出し側は `EFI_ERROR` だけを見るので動作は同じ）。`x[i++] = …`・`*p++` を文に分けた。`discover_config_volume` の候補の 1 つを調べる loop の本体を `examine_candidate`（`continue` は `EFI_SUCCESS` の return、`return discovery_abort(…)` は誤りの return）に、媒体の検査を `block_media_usable` に、kernel の読み込みを `load_kernel` に、置き場の確定を `place_kernel_at` に分けた（評価の順と出力の順は同じ）。最後の transition の呼び出しを、入口の offset を選んでから 1 回呼ぶ形に。各段落・if・loop・return の comment、分けた呼び出しは 1 行 1 引数 | DIFF-CODE（関数の分割と命令の並び）。BOOTX64.EFI は 29696 byte のまま |
| uefi/elf64.h・elf64.c | 著作権の header、macro・struct の役の comment。大きな条件を `header_acceptable`・`segment_acceptable` の guard に（評価の順は同じ）。`plan->segment[count++]`・Boolean の式の return を文と if に | DIFF-CODE。host の同値の試験で一致 |
| uefi/framebuffer.h・framebuffer.c | 著作権の header、comment、条件を 1 行 1 節に | code の差なし |
| uefi/memory-map.h・memory-map.c・memory-map-v6.c | 著作権の header（v6 の editor の mode 行を消した）、macro・enum・struct の comment、宣言の位置、関数の定義の形、呼び出しの結果の直の return を分けた、各段落の comment | memory-map.o は DIFF-CODE、memory-map-v6.o は差なし。host の同値の試験で一致 |
| uefi/zedbsd-config.h・zedbsd-config.c | 著作権の header、macro・struct・enum の comment。全部の static 関数の前方宣言、公開の関数を先に。`for` の初期化の宣言を先頭へ。条件の中の呼び出し（`selected_uuid_valid`・`line_name_is`・`unsupported_syntax`・`component_is_dot`・`text_starts_with`・`kernel_selector_prefix`・`boot_reference`・`raw_swap_selector`・`fat_capacity_ok`）を外へ。Boolean の式の return を if に、`?:` を if に、呼び出しの直の return を分けた | DIFF-CODE（UEFI と BIOS の 16 bit の両方）。host の同値の試験で一致。PC/AT の BOOTZBSD.EXE の `stage2_end` は 0xb596→0xb622（file は 1 sector 増えて 47168 byte、stack 0x1f000 まで余裕あり）、PC-98 は 24128 byte のまま |
| uefi/volume-discovery.h・volume-discovery.c | 著作権の header、全部の型・macro の群の comment、「p003」を消した。公開の関数を先に、前方宣言。宣言の位置、`?:` の連鎖（sector の倍率・FAT の大きさ）を if に、条件の中の呼び出し（`supported_fat_block_size`・`byte_equal`・`fat_media_byte`・`fat_capacity_ok`・`read_le16`）を外へ、`*p++`・`x[i++]` を文に、for の中の if の括弧 | DIFF-CODE。host の同値の試験で一致 |
| uefi/logo.h・logo.c | `ws035-p096`・`ws035-p112` を消した。条件を 1 行 1 節に、`logo_pixel` の直の return を分けた、閉じた if の後の段落、各 if の comment | DIFF-CODE。host の同値の試験（framebuffer を host の buffer にして描いた画素の hash）で一致 |
| uefi/video.h・video.c | 著作権の header（editor の mode 行を消した）、`ws035-p112` を消した、`video_set` の結果の直の return を分けた、条件を 1 行 1 節に、各 if の comment | code の差なし |
| uefi/include/uefi.h | 著作権の header、全部の型・関数の型の群・GUID の定数の役の comment | — |
| uefi/transition.S | 著作権の header、「Stage 3」を「the third progress block」に、`ws035-p096` を消した | code の差なし |
| uefi/README.md | `ws035-p096`・`ws174` を消した | — |
| sparcv9/ofw.h | 型の役の comment、宣言の間の空行 | — |
| sparcv9/ofw.c | file-scope 変数の役の comment、`OFW_FAILED` の定数、全部の static 関数の前方宣言、公開の関数を先に。条件の中の `ofw_call`・`ofw_getprop` を外へ（`/chosen` の handle の読みを `ofw_chosen_handle`、virtual の claim を `ofw_claim_virtual`、map を `ofw_map` に分けた、呼び出しの順は同じ）。`?:` の return を if に、`ofw_one_node` の結果の直の return を分けた、各段落の comment | DIFF-CODE（関数の分割）。handoff の host の同値の試験で ofw.c も一緒に一致 |
| sparcv9/handoff.h | 点検のみ（P3 の WS261 の形、規約に合う） | — |
| sparcv9/handoff.c | 全部の static 関数の前方宣言、公開の関数を先に。block の中の宣言と初期化子の中の呼び出しを先頭と文へ。カンマ演算子（`cells64`・`same_string`）を文に。条件の中の呼び出し（`property_u32`・`ofw_getprop`・`same_string`・`memory_ranges`・`walk_node`）を外へ（CPU と memory の調べを `discover_cpu`・`discover_memory` に分け、短絡の順は同じ）。Boolean の式の return を if に、`ports[found++]` を文に。各段落・return の comment | DIFF-CODE。host の同値の試験（下）で一致 |
| sparcv9/stage1.c | file の説明、macro の comment、前方宣言、公開の関数を先に。条件の中の呼び出し（`ofw_bootpath`・`ofw_seek`・`ofw_read`・`get16`・`get32`・`checksum`）を外へ（header の 4 つの検査は同じ message の別々の guard に、`fail` は firmware へ戻って返らない）。`value += *bytes++` を文に | DIFF-CODE。stage1.bin 2997→3141 byte（check-sparcv9-boot.py PASS） |
| sparcv9/stage2.c | 著作権の header を規約の形に、file の説明、macro・struct・typedef（`kernel_entry_t` を file scope へ）・file-scope 変数の comment、前方宣言、公開の関数を先に。`goto failed` を `stage2_boot()` の早い return に（失敗の message と exit は `sparcv9_stage2_main` に 1 つ）。大きな条件を `elf_header_acceptable`・`segment_acceptable`・`fat_entry_is_kernel`・`read_kernel` の guard に、cluster の鎖の一歩を `fat_follow_chain` に（評価の順は同じ）。`++steps` を条件の外へ、`?:` を if に、`path[length++]` を文に。GCC の maybe-uninitialized（`file.size`、分割で見えなくなった）を避けて `file.size = 0` を先に置いた | DIFF-CODE。stage2.bin 8243→8379 byte（PASS） |
| sparcv9/stage2-format.h | macro の群の comment | — |
| sparcv9/stage1-entry.S | 点検のみ | 同じ |
| sparcv9/stage2-entry.S | file の説明を足した | code の差なし |
| sparcv9/stage1.ld・stage2.ld | 著作権の header と説明 | — |

## 結果

- 2026-10-18 common・bios: amd64・pcat・pc98 の build は warning 0。比べ（`DIFF-CODE` の object）: amd64 `uefi/common-memory-map.o`・`bootloader/common-memory-map.i386.o`・
  `bootloader/bios-fat-directory.i386.o`（と、それを含む BOOTZBSD.EXE・BOOTX64.EFI）、pcat 同じ 2 つ、pc98 `bios-fat-directory.o`。他は code の差なし。
  host の同値の試験 `diff-run.sh ed2f1b265 build/p2-ws262/diff1`: 195141 行の出力が一致（PASS）。

- 2026-10-18 include・pcat・pc98・tests・README: amd64・pcat・pc98 の build は warning 0、比べの `DIFF-CODE` は上の common・bios の 3 つ
  （とそれを含む image）のままで、include・.S・.ld の直しによる code の差は無い。tests の payload の object は旧の source の object と code が同じ。

- 2026-10-19 uefi: amd64・pcat・pc98 の build は warning 0。比べ（[tests/compare-build.sh](../tests/compare-build.sh)）の `DIFF-CODE` は uefi の
  bootx64.o・elf64.o・logo.o・memory-map.o・volume-discovery.o・zedbsd-config.o・common-memory-map.o と BIOS の bios-fat-directory・bios-zedbsd-config・common-memory-map。
  boot-keys.o・framebuffer.o・memory-map-v6.o・video.o・transition.o・common-boot-override.o・common-logo-path.o は code の差なし。
  host の同値の試験 `diff-run.sh ed2f1b265 build/p2-ws262/diff4`（common・bios・uefi の純粋な helper の 17 種、20000 回）: 375058 行の出力が一致（PASS）。
  bootx64.c は firmware の呼び出しが主で host の試験は無い。分けた関数と条件は元の評価の順・出力の順と 1 つずつ照らした。
- 条件の中に残した呼び出しの形は `EFI_ERROR()`（macro、関数の呼び出しではない）と `sizeof` だけ（grep で数えた）。
- **動作の直し**（規約の直しとは別の commit、2026-10-19 Q1 の指示「Bug にせず、今あなたが直して」）: `console_ascii` の UTF-16 の buffer（128）は、
  125 文字目に改行があると `\r` を足した後に 127 になり、`used == 126U` の書き出しを飛び越えて buffer を超えていた（基準の source でも同じ）。
  判定を `used >= 126U` にした（書き出しの時の終端は最大で index 127）。確かめ: [tests/console-ascii-run.sh](../tests/console-ascii-run.sh) BOOTX64_C OUT
  （bootx64.c を host で ASan 付きで build し、debug port の OUT は SIGSEGV の handler で飛ばす。125 文字＋改行＋7 文字を書く）。
  直す前の source: ASan の stack-buffer-overflow（`console_ascii` の WRITE of size 2）で exit 1。直した後: 報告なし、134 文字が届いて exit 0。

- 2026-10-19 sparcv9（main 483b5b7ad を取り込んだ後。P3 の WS261 p002 の後の source を基準にした）:
  `make ZEDBSD_CONFIG=plan/ws035/tests/config-sun4u.mk BUILD=build/p2-ws262/sparcv9-{before,after} bootloader`。前後とも warning 0、
  `check-sparcv9-boot.py` PASS（stage1 2997→3141、stage2 8243→8379）。比べは stage1/stage2 の object と bin の全部が DIFF-CODE（stage1-entry.o・stage2-entry.o は同じ）。
  host の同値の試験 [tests/sparc-handoff-run.sh](../tests/sparc-handoff-run.sh) 483b5b7ad build/p2-ws262/sparc2（handoff.c と ofw.c を偽の client interface の
  乱数の device tree 20000 個で走らせ、handoff の全部の field を印字）: 20000 行の出力が一致（PASS）。成功は 26 回、他は CPU・memory・PCI・serial・IDE の
  どれかが欠けた木で、各 discover の道を通る（IDE の発見 7460 回）。stage1.c・stage2.c は firmware と disk の I/O が主で host の試験は無い（review で照らした）。
  sparcv9 の vmunix は前から build できず（WS267）、起動の確かめは T1 への依頼に入れない。

## x68k の比べ

- `make -k ZEDBSD_CONFIG=plan/ws035/tests/config-x68k.mk BUILD=build/p2-ws262/x68k-after bootloader`: stage2.o は既知の欠けで作れない（前後とも）。
  作れる stage1.o・stage2-start.o・iocs.o・mb89352.o・stage1.elf は基準と byte で同じ（compare-build.sh、OBJDUMP=m68k-linux-gnu-objdump）。
  build の log の warning: BOM による「ignoring invalid character」3 行は消えた。ld の「stage1.elf has a LOAD segment with RWX permissions」は前後とも出る
  （`-N` で link する platform/x68k の規則によるもの、bootloader の範囲外）。
- stage2.c: [tests/x68k-stage2-compare.sh](../tests/x68k-stage2-compare.sh) BASE OUT で、基準と今の stage2.c を build/ の中に書いた仮の
  `struct x68k_boot_handoff` で同じ flag（-Os -Wall -Wextra -Werror）で compile して比べた。どちらも warning なしで compile でき、
  code は違う（`x68k_stage2_main` 0x290→0x282、`load_kernel` 0x2ae→0x2be、他の関数は同じ大きさ。text 全体 1766→1768 byte）。
  条件の分解・関数の分割による命令の並びの差で、評価の順と結果は review で 1 つずつ照らした（SCSI の I/O を伴うので host の試験は無い）。

## 既存の試験の欠け（報告のみ）

- `bootloader/tests/payload*.ld` は今の ld.lld で link できない（`/DISCARD/ : { *(*) }` に「discarding .shstrtab section is not allowed」、基準でも同じ）。
  platform/pcat・pc98 の vmunix.mk の `payload32.elf`・`payload64.elf` の規則からしか参照されず、試験の一覧から参照されていなければ削除の候補（Q1 に報告）。

## T1 への依頼（2026-10-19 に Q1 へ送った文面）

- image: P2 の最後の commit を含む main の tree（72ddbf242 以降）で T1 が作る。boot loader の変更だけで kernel・userland は変えていない。
- 流す試験（`plan/tools/boot-test.sh`、login prompt の PNG）:
  1. amd64 UEFI（`config/ci/config-amd64.mk` の hdd-image、`BOOT_MODE=uefi-nvme`）: BOOTX64.EFI → login。logo の表示（logo= があれば）と login prompt。
  2. amd64 の hybrid image を BIOS で（`BOOT_MODE=bios-ide`）: BOOTZBSD.EXE（ELF64）→ login。
  3. i386 PC/AT（`config/ci/config-pcat.mk`、`BOOT_MODE=bios-ide`）: BOOTZBSD.EXE（ELF32）→ login。
  4. PC-98（`config/ci/config-pc98.mk`）: T1 に PC-98 の起動の道具があれば login まで。無ければ未実施と返す。
- 合否: 各 image で login prompt が PNG に出れば PASS。boot loader の段で止まる（UEFI の `A64 ...` の失敗の表示、BIOS の 1 文字の fatal）なら FAIL。
- 対象外: x68k（stage2 が既知の欠けで build できない）、sparcv9（vmunix が build できない、WS267）、Pi 4（boot loader を持たない）。

## 再開の手順

- 規約の直しは bootloader/ の全部で済み（common・bios・include・pcat・pc98・tests・x68k・uefi・sparcv9）。残りは T1 の結果の判定（Q1）だけ。
- FAIL が返ったら: 該当の loader の前後の比べ（`compare-build.sh`）と、host の同値の試験（`diff-run.sh`・`sparc-handoff-run.sh`）を見直し、
  host の試験で覆っていない bootx64.c・stage1.c・stage2.c・x68k/stage2.c の分けた関数を先に疑う。

## 未実施

- QEMU・実機の起動の確かめ（上の T1 への依頼）。x68k・sparcv9 の起動の確かめ（build できないため）。
