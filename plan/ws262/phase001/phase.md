<!-- awesome-plan project=zedbsd record=ws262-p001 -->

# ws262-p001 boot loader を規約の全文に

Status: in-progress（2026-10-18 q1256-i01、P2）

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

## 結果

- 2026-10-18 common・bios: amd64・pcat・pc98 の build は warning 0。比べ（`DIFF-CODE` の object）: amd64 `uefi/common-memory-map.o`・`bootloader/common-memory-map.i386.o`・
  `bootloader/bios-fat-directory.i386.o`（と、それを含む BOOTZBSD.EXE・BOOTX64.EFI）、pcat 同じ 2 つ、pc98 `bios-fat-directory.o`。他は code の差なし。
  host の同値の試験 `diff-run.sh ed2f1b265 build/p2-ws262/diff1`: 195141 行の出力が一致（PASS）。

## 未実施

- QEMU・実機の起動の確かめ（最後に T1 への依頼の文面を Q1 に送る）。
