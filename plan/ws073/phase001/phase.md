<!-- awesome-plan project=zedbsd record=ws073p001 -->

# ws073-p001: BUG-061 — `/dev/fd/N` を lstat・readlink で symbolic link に

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-061](../../bugs/BUG-061.md)

## 目的と受け入れ

`lstat("/dev/fd/N")` が descriptor の file（directory なら directory）を返すので、link をたどらない walk（`du /dev`・`find /dev`）が
`/dev/fd/N` から自分の開いた directory へ入り、cycle と言って失敗する。受け入れ: link をたどらない問い合わせでは `/dev/fd/N` が
symbolic link、たどる問い合わせ（stat・open）はこれまでどおり descriptor の file。`du /dev`・`find /dev` が 0 で終わる。

## 再現（修正前、QEMU）

main の測定用 guest image に、この tree の修正前の vmunix（`build/ws073-base`）を入れて: `find /dev` が `find: /dev/fd/3: directory cycle`、
`du /dev` が `du: /dev/fd/3: Not a directory` で 1。`ls -l /dev/fd` は各 entry を pipe などの型で出す。
回帰試験 `tests/devfd-link.c` は 17 件中 13 件 FAIL。

## 設計

Linux の `/proc/self/fd` と同じ見せ方にした（4.4BSD の fdescfs は lstat でも file を返すが、そのため walk が cycle に入る）。

- lstat・`fstatat(AT_SYMLINK_NOFOLLOW)`: `S_IFLNK`。permission は descriptor の access mode（読みは `r-x`、書きは `-wx`、両方は `rwx`、owner だけ）、
  size は target の長さ、owner は process の euid・egid。たどる stat は従来どおり descriptor の `fstat`（BUG-054）。open（dup と同じ）は不変。
- readlink の target: directory は getcwd と同じ上への walk で絶対 path。directory 以外は、name cache に残る最近の lookup（親と名前）、
  それが無い device node は devfs の最上位と 1 段下の directory の inode 番号の走査（lookup しないので node を作らない）。名前が分からないものは
  Linux と同じく種類と番号（`pipe:[N]`、`socket:[N]`、`file:[N]`）。
- `/dev/stdin`・`stdout`・`stderr` は `fd/0` などへの link（`lrwxrwxrwx`）。
- readdir の `/dev/fd` と standard の名前の d_type は link。
- 変えた file: `src/kern/syscall.c`（`descriptor_link_target`・`descriptor_link_getattr`、stat と readlinkat の分岐）、
  `src/kern/namei.c`（`fs_path_of` と walk・走査の helper）、`src/kern/namecache.c`（`namecache_parent`）、`src/kern/devfs.c`（alias の種類、readdir の型）、
  `include/kern/inode.h`（`INODE_DESCRIPTOR_ALIAS_NUMBER`・`_STANDARD`）、`include/kern/namei.h`・`namecache.h`。HAL は触れていない。

## 検証（QEMU、KVM、8 GiB、4 vCPU、NVMe。実機は未実施）

- build: `make -j48 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-a vmunix` warning 0、`amd64 vmunix check: PASS`。
- guest（`tests/kernel-image.sh` で main の測定用 image の vmunix だけ差し替え）: `tests/devfd-link.c` 20 件全て PASS
  （directory・regular・pipe・pts の slave の link と target、たどる stat、`fstatat` の nofollow、stdin、短い buffer の readlink）。
  `du /dev` 0、`find /dev` 0。`ls -l /dev/fd </etc/passwd` は `0 -> /etc/passwd`、`1 -> pipe:[..]`。`readlink /dev/fd/0 </dev/null` は `/dev/null`、
  `</dev/console` は `/dev/console`、`realpath /dev/stdin </etc/passwd` は `/etc/passwd`。`diff <(echo 1) <(echo 1)` 0・`<(echo 2)` 1、
  `echo hi | cat /dev/stdin`、`ls -lL /dev/fd/0` は regular。`ls -l /dev/fd` の 30 回の繰り返しの後も SSH が生きる。
- boot test: `OUTPUT=build/ws073-a/boot-test plan/tools/boot-test.sh build/ws073-a/guest.img` PASS（login.png を確認）。
- 規約: `tests/style-diff.py`（変えた行の style-check）0。新しい test の file は style-check 0。全文の規約で見直した。
- sh の差分試験（`plan/tools/sh`）は流していない（未実施）。

## 制限

- 通常の file の名前は name cache（128 entry）に残っているときだけ分かる。残っていなければ `file:[ino]`（Linux は常に path を返す）。
- `realpath("/dev/stdin")` は pipe のとき `pipe:[N]` を解決できず ENOENT（Linux と同じ）。以前は `/dev/stdin` を返していた。
