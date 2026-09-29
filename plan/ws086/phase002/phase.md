<!-- awesome-plan project=zedbsd record=ws086-p002 -->

# ws086-p002: 実装と host・guest の試験（GNU ls の出力との比較）

Status: cleared
Disposition: normal
Parent: [WS086](../ws.md)
Queue: main が subagent（worktree `wt/ws086`）へ依頼（2026-09-29、p001 の merge の後）
Approval: ユーザー（2026-09-29）「lsの結果が1つずつ改行されています。GNU lsと同じにしたいです。」

## 範囲と受け入れ

- [p001 の設計](../phase001/phase.md)の「含める」を `userland/base/ls/main.c` に実装する。
- host: GNU ls 9.7 と我々の ls を、端末と pipe、`LC_ALL=C` と `C.UTF-8` で byte 単位に比べ、一致する（下の既知の差を除く）。
- 回帰: `plan/tools/utils/util-diff.py`（POSIX の case）と `ls-compare.sh`（旧 ls との比較）。
- guest（amd64、QEMU）: 端末で `ls` が列に並ぶこと、pipe で 1 行に 1 つ。Terminal の画面。

## 判断（2026-09-29）

- **locale（p001 の判断 1）のユーザーの決定**:「我々のOSはutf-8のみをサポートしており、Escapeは不要と思います。LANG=CをUtf-8と
  解釈するのが乱暴というなら、C.UTF-8を設定するのでもいいです。」→ ls は locale に関わらず名前を UTF-8 として扱う
  （`setlocale(LC_CTYPE, "C.UTF-8")`。C library に UTF-8 の locale が無ければ 1 byte の扱いに戻る）。表示できる UTF-8 の文字は
  escape しない。端末での escape・quote は制御文字・不正な UTF-8 の byte・shell で特別な文字だけ。幅は UTF-8 の表示幅。
  **GNU との差**: C locale の GNU は 0x80 以上の byte を全て `$'\ooo'` で escape し（端末）、pipe の `-C`・`-x`・`-m` では 1 byte を
  1 列と数える。我々は C locale でも C.UTF-8 の GNU と同じ出力になる。
- 不明な option: `command_options` の message（`ls: invalid option -- 'z'`）の後に今の `usage:` の行、終了状態 1 のまま（p001）。

## 実装（`userland/base/ls/main.c`）

- format（`-1`・`-C`・`-x`・`-m`・`-l`、最後が勝つ）と、端末なら `-C`・shell-escape の quote・`?`。
- 幅: `-w`（`--width`）→ `TIOCGWINSZ`（端末、0 でない値）→ `COLUMNS` → 80。数は基数 0。桁あふれは 0（無制限）。不正は GNU と同じ message。
  tab: `-T`（`--tabsize`）→ `TABSIZE`（幅を使う format のときだけ読む）→ 8。
- 列: GNU の `calculate_columns` と同じ（列ごとの幅、最小 3、区切り 2）。`indent` の tab。幅 0 の `-C`・`-x` は空白の区切りの 1 行。
- 名前: shell-escape（`'…'`、`'` だけなら `"…"`、制御文字は `$'\t'` か `$'\ooo'`、`'` は `'\''`）、`-F` のとき `*=>@|`、見出しでは `:`
  を含む名前も quote。一覧に quote された名前があれば他の名前の前に空白（`-C`・`-x`（幅あり）・`-l`）。`-N`・`-q`。
- `-i` は一覧の最大の桁に右寄せ（`-m` は詰める）、status の無い名前は `?`。`-F` の印は印の付く名前の幅だけ、`*` は通常の file だけ。
  `-lF` は名前と link の先に印。
- GNU の比較で見つかり直した差（p001 の一覧に無かったもの）:
  - pipe の literal の名前に印字できない文字（制御文字・不正な UTF-8）があると、GNU は幅を数えられず `(size_t)-1` とし、そのまま
    足し算する（`-m` ではその名前の後で改行、`-C` ではその名前の後に空白を置かない）。同じ計算にした。
  - 端末でない `-C` などでも、operand の directory は file の群と一緒に測られる（`-l` の列の幅、`-i` の桁、quote の揃え）。
  - command line の symbolic link が directory を指すとき、`-d`・`-F`・`-l`・`-L` が無ければ directory として中身を列挙する
    （POSIX の規定。旧 ls は link として列挙していた）。
  - `-R` では operand が 1 つでも（無くても）見出し（`.:` など）を書く。
- option の読み取りは `command_options`（GNU の順、`POSIXLY_CORRECT` で POSIX の順、長い形）。

## 試験

### host（Debian、GNU coreutils 9.7、gcc で host 向けに build）

- build: `cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-format-truncation -I. -Iinclude userland/base/ls/main.c userland/base/common/command.c -o build/ws086/host/ls`
  → warning 0（`-Wformat-truncation` は旧 ls からの `human_size` の 3 件で、guest の clang の build では出ない）。
- [tests/compare-gnu.py](../tests/compare-gnu.py) `build/ws086/host/ls`: 7 つの tree（多数の短い名前、長い名前の混在、quote の要る ASCII の
  名前 45、UTF-8 の名前、空、1 つ、200 個）× 37 の option の組 × 端末（幅 0・20・40・80・132）と pipe × `C`・`C.UTF-8`、operand の
  組み合わせ、`COLUMNS`・`TABSIZE`・`POSIXLY_CORRECT`、不正な値の message、`/` の `-i` → **compared=3362 differ=0**、既知の差 48
  （`utf8-in-c-locale` 24、`dangling-with-L` 24）。
- `plan/tools/utils/build-host-utils.sh build/ws086/bin` と `util-diff.py --bin build/ws086/bin` → **TOTAL 1080/1080**（ls を使う case を含む）。
- `plan/tools/utils/ls-compare.sh`（pipe、240 件）: 旧 ls との差は、`-C` の列の配置、`-i` の右寄せ、`-R` の見出し、operand の link の先の
  directory、operand の群の `-l` の幅（全て GNU に合わせた変更）。GNU（`TZ=UTC LC_ALL=C`）との差は、`t/missing` の message の文、
  `-L` の dangling、`-t`（下）、`-h`（下）だけ。

### guest（QEMU、amd64、KVM）

- image: `make -j32 ZEDBSD_CONFIG=plan/tools/guest/config-amd64-ssh.mk BUILD=build/amd64 $(guest.py extra-files) disk-image`
  （worktree の中。sysroot は main の `build/amd64/sysroot` の複写が build の途中で作り直された）。ls は `-Wall -Wextra -Werror` で
  compile され通った。
- [tests/guest-compare.py](../tests/guest-compare.py): guest（SSH、`ssh -tt` の擬似端末と `stty cols`、pipe）の ls と host の GNU ls
  （`C.UTF-8`）を 3 つの tree × 12 の option × 5 の幅で比べた → **compared=180 differ=0**。guest の session には `LANG` が無い（C locale）。
- 観察（ls の外）: `ssh -tt` で ls が最後の command（shell が exec する）のとき、出力が 8〜25 回に 1 回ほど空か途中で切れる。後に
  command（`; true`、`; echo`）を置くと 25/25 回とも完全で、ls の終了状態は 0。`cat` を最後にしたときは 20/20 回とも完全。pty か sshd
  の終わり方の問題と思われ、WS086 の範囲外（main に報告）。試験は `; true` を足して避けた。
- boot test: `OUTPUT=build/ws086/boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS（`build/ws086/boot-test/login.png`）。

## 残る GNU との差（範囲外。p001 の一覧に無く、比較で見つけた。main に扱いを依頼）

- `-t`: GNU は時刻を nanosecond まで比べる。我々は秒で比べて同じなら名前の順（同じ秒に作った file の順が違う）。旧 ls から。
- `-h`: GNU は切り上げ（23.4K → 24K）。我々は四捨五入。旧 ls から。
- `-L` で dangling link: GNU は `cannot access` を出して `l?????????` の行、終了状態 1。我々は link として列挙（旧 ls の意図した振る舞い）。
- message の文: `cannot access 'x'` の形、UTF-8 の locale での `‘’` の quote、不明な option の終了状態 2。

### Terminal（QEMU、amd64、Venus の desktop）

- image: `plan/tools/titlebar/build-menu-image.sh build/amd64`（lean な desktop。font と wallpaper は main の `build/ws035-fonts`・
  `build/ws035-wallpaper` への読み取り専用の symlink）。`GUEST_RUNTIME=build/ws086/desk plan/tools/titlebar/menu-guest.sh start`、
  wayland と terminal を起こし、`qmp-keys.py` で打ち、`zdesktop-check.py` で撮った。
- `build/ws086-shots/terminal-ls.png`: Terminal（90 桁、`stty size` は `28 90`）で `ls` が縦の順の 8 列に並ぶ。
- `build/ws086-shots/terminal-odd.png`: `ls -x | head -2`（pipe でも `-x` は 80 桁の列）、`ls -F`（`beta*`・`sub/`・`link@`）、quote の要る名前の
  一覧（`'with space'`、`"it's"`、`'ctl'$'\001''x'`、quote されない名前の前の空白）。日本語の名前は escape されずに出る（この lean な image の
  font に CJK の字形が無いので豆腐の四角で描かれる。font の問題で ls の外）。
- `build/ws086-shots/terminal-bin.png`: `/bin` の 150 余りの名前が 7 列に並ぶ。
- 実機: 未実施。

## 結果

- cleared。受け入れ（host で GNU と一致、回帰、guest の端末と Terminal）を満たした。
- 次: ws086-p003（規約の全文との照合、回帰）。
