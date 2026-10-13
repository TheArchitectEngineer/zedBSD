<!-- awesome-plan project=zedbsd record=ws228-p001 -->
# ws228-p001: 簡単な vi の範囲の設計と実装

Status: in-progress（2026-10-14 P3、q971、設計を Q1 の review へ）
Disposition: normal
Parent: [WS228](../ws.md)
Queue: q971
依存: 無し

## 手順

1. 範囲の設計を phase.md に書く（命令の一覧、入れない物、端末の扱い、UTF-8、file の保存の安全（一時 file と rename、権限の保持）、大きな file の上限、signal と端末の復元）。Q1 が design-reviewer を通す。ユーザーの判断が要る点は印を付けて Q1 に返す。
2. review の後に実装（`userland/base/vi/`、Makefile の `ZEDBSD_USERLAND_PACKAGE`、menu は base、表示名は `vi`）、`plan/coding-style.md` の全文を適用。
3. 確かめ: amd64 の build warning 0、host で pty を使う短い試験（挿入・削除・保存・undo・日本語）。QEMU は p002 で T1。

## 設計（手順 1、2026-10-14 P3）

印 **[判断]** はユーザーの判断が要る点（括弧の中が P3 の推し。推しのまま実装を進め、違えば直す）。

### 1. 置き場所と build

- `userland/base/vi/`。file: `vi.h`（共有の型と宣言）、`main.c`（引数・main loop・signal）、`terminal.c`（termios・窓の大きさ・terminfo の文字列・出力の buffer・key の読み取り）、`buffer.c`（行の配列と編集の基本操作）、`undo.c`（undo/redo の記録）、`file.c`（読み込みと保存）、`screen.c`（UTF-8 の幅・行の折り返し・差分の描画）、`command.c`（normal mode の命令・移動・operator）、`ex.c`（`:` の命令）、`search.c`（`/`・`?`）。
- Makefile: `$(eval $(call ZEDBSD_USERLAND_PACKAGE,vi,vi,*,y,basic,$(ZEDBSD_VI_SOURCES),y,base,base/vi,base/terminfo))`。source に `userland/base/common/terminfo.c` を含める（tput と同じ）。既定 y、menu は Base、表示名 `vi`、`/bin/vi`。
- `config/ci/config-*.mk` は `ZEDBSD_USER_PROGRAMS` を列挙しているので、CI の image に入れるには `vi` を足す必要がある。担当の範囲外なので Q1 に依頼する（T1 の試験は `plan/ws228/tests/config.mk` で作る）。

### 2. 端末

- **libcurses は使わない。** base の curses（`userland/base/curses/curses.c`）は出力を直に書くだけで、画面の写し・窓の大きさ（TIOCGWINSZ）・wide 文字・key の列の解読を持たない。vi はそれらが全部要るので、termios を直に使い、端末の能力は tput と同じく `common/terminfo.c`（`terminfo_load`・`terminfo_find`・`terminfo_expand`）で引く。
- 使う能力: `cup`（必須）、`el`（必須）、`clear`、`smcup`/`rmcup`、`civis`/`cnorm`、`rev`/`sgr0`、`bel`。TERM が無い、または `.zti` が読めない時は VT100（ANSI）の文字列を内蔵の既定として使う（base の terminfo は全て VT100 互換）。読めた記述に `cup` か `el` が無い時（`dumb` など）は「vi: terminal 'X' cannot move the cursor」で終える。
- 標準入力と標準出力が端末でなければ、編集を始めずに終える（`vi: standard input is not a terminal`）。
- raw mode: `ICANON`・`ECHO`・`ISIG`・`IEXTEN`・`IXON`・`ICRNL` を落とし、`VMIN=1`・`VTIME=0`。`ISIG` を落とすので Ctrl-C・Ctrl-Z は key として届く。
- 窓の大きさ: TIOCGWINSZ。取れなければ terminfo の `lines`/`cols`、それも無ければ 24x80。最小 2 行・10 桁（未満なら最小として扱う）。SIGWINCH で印を立て、main loop が測り直して全部を描き直す。
- key: 1 byte ずつ読み、ESC の後 50 ms（poll）以内に `[` か `O` が続けば列として解く。解く列: 矢印（`ESC [ A-D`・`ESC O A-D`）、Home（`ESC [ H`・`ESC O H`・`ESC [ 1 ~`・`ESC [ 7 ~`）、End（`F`・`4 ~`・`8 ~`）、Delete（`ESC [ 3 ~`）、PageUp/PageDown（`5 ~`・`6 ~`）。知らない列は捨てる。続かなければ ESC 単独。
- 出力は自前の buffer（4 KiB）に溜め、1 回の描画の終わりに `write` で全部書く（EINTR は書き直す）。
- 描画: 画面の最後の行が状態と命令の行、残りが本文。行ごとに前回の出力の写しを持ち、変わった行だけを `cup`＋本文＋`el` で書き直す。描画の間は `civis`、終わりに cursor を置いて `cnorm`。

### 3. signal と端末の復元

- 開始: `tcgetattr` で元の mode を保存 → raw → `smcup`。終了（`:q` 等・error・signal）: `rmcup`・`cnorm`・`sgr0` を書き、元の mode を `tcsetattr(TCSAFLUSH)` で戻す。復元は 1 つの関数にまとめ、`atexit` にも登録する（二重に呼んでも 1 回だけ働く）。
- SIGWINCH: 印（`volatile sig_atomic_t`）→ 描き直し。SIGCONT: 印 → raw に戻して全部を描き直す。
- SIGHUP・SIGTERM: 印 → main loop が端末を戻して終了（状態 1）。**[判断]** 保存されていない変更は失う（推し: 回復 file は作らない。代案は `$HOME` か `/tmp` に `vi.recover.<pid>` を書く）。
- Ctrl-Z（key）と外からの SIGTSTP（印）: 端末を戻す → SIGTSTP を既定の処理にして自分に送る → 再開したら handler を戻し、raw に戻して描き直す。
- handler は印を立てるだけ。`SA_RESTART` を付けないので、`read` が EINTR で戻って main loop が印を見る。SIGPIPE・SIGSEGV などは扱わない（既定のまま）。

### 4. buffer と上限

- 行の配列（`struct vi_line { char *text; size_t length; size_t capacity; }` の配列、行の挿入・削除は配列の memmove）。行の中身は byte の列で、改行を含まない。NUL も 1 byte として保つ（`^@` と表示）。
- 読み込み: 末尾の行に改行が無ければ「incomplete last line」と知らせ、保存の時に改行を足す（POSIX の vi と同じ）。空の file は 1 行の空の buffer。無い file は新しい file として空の buffer（「new file」）。
- **[判断]** 上限（推し: 64 MiB の file、行の数は 4M（4194304）。超える file は開かずに「file too large」）。メモリの確保の失敗は、その命令を失敗にして buffer を変えず「out of memory」と知らせる（終了しない）。
- 編集は全て 4 つの基本操作を通す: byte 列の挿入（行・桁）、byte 列の削除、行の分割、行の連結（と、行の挿入・削除）。基本操作が undo の記録を書く。

### 5. undo

- 基本操作ごとに逆の操作（消した byte 列の写しを含む）を記録し、1 つの命令（または 1 回の insert の区間）を 1 つの group にする。`u` は最後の group を逆順に戻し、cursor を変更の始めへ。
- **[判断]** 多段の undo と redo（推し: `u` を繰り返すと更に前へ戻り、`Ctrl-R` で redo。nvi・vim と同じ使い勝手。POSIX の vi は `u` が直前の変更 1 つだけで、もう一度の `u` は undo の取り消し）。group の数の上限は 1000（超えたら古い物から捨てる）。新しい変更で redo の列は捨てる。
- 保存した時点の group の位置を覚え、undo/redo で戻れば「変更なし」と扱う。

### 6. UTF-8 と表示の幅

- 文字の区切りは UTF-8 を自前で解く（locale に頼らない。Kei は UTF-8 だけ、2026-09-29 ユーザー）。幅は `setlocale(LC_CTYPE, "C.UTF-8")` の後の `wcwidth`（zedBSD の libc は CJK・全角を 2）。
- 不正な byte（途中で切れた列・余分な継続 byte・overlong・サロゲート）は 1 byte を 1 文字とし `<HH>`（4 桁）と表示。制御文字は `^X`（2 桁、DEL は `^?`）。tab は 8 桁ごと。幅 0 の文字（結合文字）は前の文字に付け、cursor はその組を 1 つとして動く。
- 長い行は折り返す（伝統の vi と同じ）。幅 2 の文字が右端の 1 桁に入らない時は、その桁を空けて次の行へ。画面の下に入り切らない行は `@` の行で示す。空の画面の行は `~`。
- 移動・`x`・`r` は文字（UTF-8 の列）の単位。insert の入力は byte のまま入れる（多 byte の文字は続く byte が来てから 1 文字として表示）。`r` は 1 文字ぶん（UTF-8 の先頭 byte の長さ）を読む。

### 7. 命令（入れる物）

- count: 数字の前置（`0` は先頭の時だけ移動）。operator と移動の両方に付けられ、掛け合わせる。
- 移動: `h` `j` `k` `l`、矢印、BS（=`h`）、space（=`l`）、`0` `^` `$`、Home/End、`w` `b` `e` `W` `B` `E`、`G`（`nG`）、**[判断]** `gg`（推し: 入れる。POSIX には無いが手が覚えている人が多い）、`H` `M` `L`、`Ctrl-F` `Ctrl-B` `Ctrl-D` `Ctrl-U`、PageUp/PageDown、`f` `F` `t` `T` `;` `,`。`j`・`k` は望みの桁（表示の桁）を保つ。
- insert: `i` `a` `I` `A` `o` `O`。insert の中: ESC（と Ctrl-C）で終わる、BS は前の 1 文字を消す（行頭では前の行と連結）、Enter で行を分ける、Delete は次の 1 文字、Ctrl-W は前の語、矢印・Home・End は cursor を動かす（insert は続け、undo の group をそこで区切る）。自動の字下げはしない。
- 削除・変更: `x` `X`（Delete は `x`）、`dd` `D` `d{移動}`、`cc` `C` `c{移動}`（`cw` は `ce` と同じ）、`s` `S`、`r{文字}`（count 付きで n 文字）、`J`（count 付き、間に space 1 つ）、`~`（大小の入れ替え、ASCII だけ）。
- yank と put: `yy` `Y` `y{移動}`、`p` `P`。register は名前の無い 1 つだけ（行の単位か文字の単位かを覚える）。`d`・`c`・`x` も register に入れる。
- undo: `u`、`Ctrl-R`（§5）。
- 検索: `/pat`、`?pat`、`n` `N`、`/`・`?` だけで前の pattern。pattern は POSIX の基本正規表現（libc の `regcomp`/`regexec`、zedBSD は TRE）。行の中で一致し、行を越える一致は無い。端で折り返し「search wrapped」と知らせる。見つからなければ「pattern not found」。
- その他: `Ctrl-L`（描き直し）、`Ctrl-G`（file 名・行の数・位置・変更の有無）、`ZZ`（`:x` と同じ）、`ZQ`（`:q!`）。知らない key は `bel`。
- ex（`:` の行。BS で編集、BS で空になったら・ESC で取り消し、Enter で実行）:
  - `:N`（行 N へ）、`:$`（最後の行）、`:.`
  - `:w`、`:w file`、`:w!`、`:w! file`、`:q`、`:q!`、`:wq`、`:wq file`、`:wq!`、`:x`、`:e file`、`:e!`（file を読み直す）、`:e! file`、`:f`（`Ctrl-G` と同じ）。
  - `:q`・`:e file` は変更が保存されていなければ断る（「no write since last change (add ! to override)」）。
  - 知らない命令は「unknown command: ...」。
- 起動: `vi [+N | +] [file]`（`+N` は行 N、`+` は最後の行）。引数の無い vi は名の無い buffer（`:w` は「no file name」で断り、`:w file` で名が付く）。`--` を受ける。知らない option は使い方を出して状態 2。**[判断]** file は 1 つだけ（推し: 2 つ以上は「vi: only one file can be edited at a time」で状態 2。`:n` は入れない）。

### 8. 入れない物

ex mode（`Q`・`:` の全部の ex の命令）、`:s` `:g` `:r` `:!` `:map` `:ab` `:set`、`.`（繰り返し、**[判断]** 推し: 入れない）、名前付きの register、mark（`m` `'`）、visual mode、text object、`%`、複数の file・window・buffer、tag、行の番号の表示、自動の字下げ、回復 file、`U`（行の undo）、`R`（上書き mode）、`-R`/view、option の file（`.exrc`・`EXINIT`）。

### 9. file の保存の安全

- 名前が symlink なら `realpath` で行き先に書く（symlink を通常の file に置き換えない）。
- 書く前の確かめ: 既存の file が通常の file でなければ断る。他の名前（`:w other`）が既にあれば `!` 無しでは断る（「file exists (add ! to override)」）。既存の file に書けない（`access(W_OK)` が失敗）なら `!` 無しでは断る（「file is read-only (add ! to override)」）。`!` 付きでも OS が拒めばその error を知らせる。
- 書き方（既定）: 同じ directory に `mkstemp` で `.<name>.vi-XXXXXX` を作る → 全部の行と改行を書く（短い write は続きを書く）→ 既存の file の mode（`st_mode & 07777`）を `fchmod`、所有者と group を `fchown`（EPERM は無視）、新しい file は `0666 & ~umask` → `fsync` → `close` → `rename` で置き換える。途中で失敗したら一時 file を `unlink` し、元の file は変わらない。
- 置き換えられない時の代わり: 既存の file の hard link が 2 つ以上（`st_nlink > 1`）、または directory に一時 file を作れない（EACCES・EROFS など）時は、元の file を `O_WRONLY|O_TRUNC` で開いて直に書く（link と inode を保つ。途中の失敗では file が壊れうるので、失敗を明示して知らせる）。
- 書けたら「"name" N lines, M bytes written」。buffer の変更の印を落とす。
- 読み込みは `open`＋`fstat`（通常の file でなければ「not a regular file」で開かない、directory も）＋`read` の繰り返し。

### 10. 試験（手順 3）

- amd64 の build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk ZEDBSD_USER_PROGRAMS=vi BUILD=… <BUILD>/bin/vi` の形で個別の target を名指す（target 無しの make・disk-image は流さない）。warning 0。
- host 試験: `plan/ws228/tests/run-host-vi.py`。host の cc で vi を build（`common/terminfo.c` を含め、`include/libc/terminfo.h` だけを写した directory を include の道に置く。regex は host の libc）、python の `pty` で起動し `TERM=xterm`・`TERMINFO=userland/base/terminfo` で key を送り、保存された file の中身で判定する（画面は判定に使わない）。項目: 挿入と保存（`:wq`）、`x`・`dd`・`dw`・`D`、`yy`＋`p`、`u` と `Ctrl-R`、日本語の行で `l`・`x`・`r`（多 byte の文字を 1 つとして消す・置き換える）、`/` の検索の後の編集、`:q` が変更ありで断ること、`:q!`、保存で mode（0640）が保たれること、symlink の先に書くこと、端末の mode が終了の後に戻ること（`stty -g` の前後の比較）。出力は `build/tmp/` の mktemp の directory（消すのは Q1）。
- QEMU（p002、T1 への依頼の文）: §11。

### 11. T1 への依頼の文（p002 用、Q1 経由）

- image: 依頼の時点の main の SHA で、`plan/ws228/tests/config.mk`（amd64、base の既定＋`vi`）の build。個別の file の複写は無し。
- 試験（serial の対話、`plan/tools/guest/serial.py`）: login → `TERM=vt100 vi /tmp/a.txt` → `i` `hello` `Enter` `日本語` `ESC` `:wq Enter` → `cat /tmp/a.txt` が `hello\n日本語\n`。続けて `vi /tmp/a.txt` → `jx:wq` → 2 行目が `本語`。`vi /tmp/a.txt` → `ddu:wq` → 変わらない。`vi /tmp/a.txt` → `:q`（変更なしで終わる）→ `stty -a` が `icanon echo` を含む（端末が戻った）。
- 合否: 上の `cat` の出力が一致し、vi の後に shell の入力が普通に echo されること。PNG（boot-test の login prompt）を添える。
