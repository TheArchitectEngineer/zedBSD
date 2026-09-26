# ws001-p025: cp と mv

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #24 cp と #83 mv を XCU（POSIX.1-2024）の要求に合わせる。mv が別の file system へ移すときに使う
階層の copy を cp と共有する。

## 範囲

- `cp`: `-R`（`-r`）、`-H`・`-L`・`-P`（最後が勝つ。`-R` だけなら link を link として、`-R` 無しなら operand の link を辿る。
  `-R` 無しの `-P` は link を link として copy）、`-f`（開けない destination を消して作り直す）、`-i`（上書きの前に問う。
  断ると状態 >0）、`-p`（owner・mode・時刻。owner を与えられないときは set-ID bit を落として続ける）。
  新しい file の mode は source の bit から umask を引いたもの、既存の destination は mode を保つ。
  directory の copy（作った directory は中身の後に最終の bit）、自分の中への copy の拒否、同じ file の拒否、
  `-R` 無しの directory は診断して続ける、`-R` 無しの FIFO や device は中身を読む、`-R` では FIFO・device・link を作り直す。
- 既存の zedBSD 拡張を保つ（installer が使う）: `-a`、`-n`/`--no-clobber`、`--update=none-fail`、`-T`/`--no-target-directory`、
  `--attributes-only`、`--preserve=mode`、`--report-file=PATH`（`CPCOPY1`・`F`/`D` と hex の record・`END`）。
- `mv`: `-i`・`-f`（最後が勝つ）、書けない destination を端末では問う、同じ file の拒否、`rename()` と、`EXDEV` のときの
  `cp -pRP` 相当の copy（hard link を保つ）と source の削除。directory と非 directory の置き換えの拒否、空でない directory の拒否。
  既存の `-n`・`--update=none`・`--update=none-fail`・`-T`・`--force` を保つ。

範囲外: `-i` の yesexpr の locale（POSIX locale の y/Y だけ）、GNU の他の option（`-u`・`-v`・`-b` ほか。WS045 の方針に従う）。

## 受け入れ条件

1. host の差分試験: `plan/tools/utils/cases/{cp,cp-user,mv,files}.sh` が全件一致。
2. 端末の case（mv の書けない destination の問い）: `plan/ws001/tests/tty-host-test.py` が全件一致。
3. installer の cp の呼び方（`cp -a -T --report-file=`、`cp -T --attributes-only --preserve=mode --update=none-fail`）で、
   旧 cp と新 cp の tree・mode・report が同じ（`plan/ws001/tests/cp-installer-compare.sh OLD NEW`）。
4. `userland/base/cp/{main.c,copy.c,copy.h}`・`userland/base/mv/main.c` が `plan/tools/style-check.py` で違反 0。
5. amd64 guest で cases を流して一致、guest image の build で warning 0（既存の noct の warning を除く）。

## 記録

### 変更

- `userland/base/cp/copy.c`・`copy.h`（新設）: 階層の copy の核。cp と mv が共有する（`userland/base/mv/Makefile` の source に足した）。
- `userland/base/cp/main.c`: 書き直し。option の読み取り（束ねた文字の option と長い option）、target の判定、report の開閉。
- `userland/base/mv/main.c`: 書き直し。問い、`rename`/`renameat2(RENAME_NOREPLACE)`、`EXDEV` の copy と削除。
- `plan/tools/utils/build-host-utils.sh`（共有の道具）: mv に `userland/base/cp/copy.c` を足した（mv の link のため）。
- 追加の修正（coordinator の依頼、2026-09-27）: `src/libc/string.c` の `strerror` が `include/uapi/errno.h` の 81 個の error 番号のうち
  約 55 個しか説明を持たず、`ECONNREFUSED` などが "Unknown error" になっていた。表にして 81 個すべてに説明を与え、
  error 番号でない値は `Unknown error N`（`strerror_r` は `EINVAL`）にした。`perror`・`err`・`warn` は `strerror` を使うので同じ表になる。
  host の試験 `python3 plan/ws001/tests/strerror-host-test.py`（string.c を zedBSD の header で host 用の共有 object にして ctypes で呼ぶ。
  errno.h の全 `E*` を読み、説明が "Unknown error" でないこと・重複しないこと・`strerror_r` の EINVAL/ERANGE を確かめる）:
  新 0 failures/81、旧 string.c は 61 failures。

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験（`util-diff.py --bin build/ws001/bin --only <case>`） | cp 45/45、cp-user 2/2、mv 29/29、files（WS043 の case）42/42 |
| 端末の case `tty-host-test.py` | 10/10（mv の問い 3 件を追加） |
| installer の呼び方の比較 `cp-installer-compare.sh`（旧 cp は HEAD の main.c を host で build） | 同じ（tree・mode・seed の状態・report） |
| amd64 guest `sh plan/ws001/tests/guest-run.sh build/ws001/guest-p025.out cp mv files` | 116/116（最初は 110/118。case が GNU の `ls --time-style` と `mktemp` を使い、root は書けない file にも書けるため。case を `test -nt/-ot` と `$$` に直し、root で成り立たない 2 件を `cp-user.sh` に分けた） |
| style（4 file） | 違反 0。`src/libc/string.c` は 14 → 13（新しい部分は 0） |
| strerror の host 試験 | 81/81 |
| guest image の build | warning は `userland/base/noct/noct/src/core/interpreter.c:2395` の既存の 1 件だけ（この Phase と無関係） |
| 実機 | 未実施 |

### 残り

- `-i` の yesexpr の locale。`cp -R` で destination の中の link を辿らない（lstat）ことは POSIX の範囲で実装定義として扱った。
- guest は root で走るので、権限の失敗（書けない destination）の case は host だけ（`cp-user.sh`）。
