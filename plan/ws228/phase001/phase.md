<!-- awesome-plan project=zedbsd record=ws228-p001 -->
# ws228-p001: 簡単な vi の範囲の設計と実装

Status: in-progress（2026-10-14 P3、q971、実装・host 試験済み、review-1 反映済み。QEMU は p002 で T1）
Disposition: normal
Parent: [WS228](../ws.md)
Queue: q971
依存: 無し

## 手順

1. 範囲の設計を phase.md に書く（命令の一覧、入れない物、端末の扱い、UTF-8、file の保存の安全（一時 file と rename、権限の保持）、大きな file の上限、signal と端末の復元）。Q1 が design-reviewer を通す。ユーザーの判断が要る点は印を付けて Q1 に返す。
2. review の後に実装（`userland/base/vi/`、Makefile の `ZEDBSD_USERLAND_PACKAGE`、menu は base、表示名は `vi`）、`plan/coding-style.md` の全文を適用。
3. 確かめ: amd64 の build warning 0、host で pty を使う短い試験（挿入・削除・保存・undo・日本語）。QEMU は p002 で T1。

## 設計（手順 1、2026-10-14 P3）

印 **[判断]** はユーザーの判断が要る点（括弧の中が P3 の推し）。

**決定（2026-10-14 ユーザー、クリック、Q1 経由）**: `.`（直前の変更の繰り返し）は「入れる」。他の判断（多段の undo と Ctrl-R、`gg`、file は 1 つ、64 MiB・4M 行）は推しのまま（「このまま」）。

**決定（2026-10-14 ユーザー、クリック、review-1 の後、Q1 経由）**: 回復の file は「変更がある時だけ作る」（上の「このまま」を変更）。F1（kernel の VT console が引数 2 つの `H` と `m` を捨てる）は「kernel の console を直す」: 別の Phase ws228-p004（P1、src/kern の tty）で Q1 が投入する。vi は terminfo の `cup` をそのまま使い、場当たりの回避を入れない。`config/ci/*.mk`（config-amd64-server.mk を含む）への vi の追加は Q1 が merge の時に行う。他の指摘は review の直し方の案で技術判断として直す。

### 1. 置き場所と build

- `userland/base/vi/`。file: `vi.h`（共有の型と宣言）、`main.c`（引数・main loop・signal）、`terminal.c`（termios・窓の大きさ・terminfo の文字列・出力の buffer・key の読み取り）、`buffer.c`（行の配列と編集の基本操作）、`undo.c`（undo/redo の記録）、`file.c`（読み込みと保存）、`screen.c`（UTF-8 の幅・行の折り返し・差分の描画）、`command.c`（normal mode の命令・移動・operator）、`ex.c`（`:` の命令）、`search.c`（`/`・`?`）。
- Makefile: `$(eval $(call ZEDBSD_USERLAND_PACKAGE,vi,vi,*,y,basic,$(ZEDBSD_VI_SOURCES),y,base,base/vi,base/terminfo))`。source に `userland/base/common/terminfo.c` を含める（tput と同じ）。既定 y、menu は Base、表示名 `vi`、`/bin/vi`。
- `config/ci/config-*.mk` は `ZEDBSD_USER_PROGRAMS` を列挙しているので、CI の image に入れるには `vi` を足す必要がある。担当の範囲外なので Q1 に依頼する（T1 の試験は `plan/ws228/tests/config.mk` で作る）。

### 2. 端末

- **libcurses は使わない。** base の curses（`userland/base/curses/curses.c`）は出力を直に書くだけで、画面の写し・窓の大きさ（TIOCGWINSZ）・wide 文字・key の列の解読を持たない。vi はそれらが全部要るので、termios を直に使い、端末の能力は tput と同じく `common/terminfo.c`（`terminfo_load`・`terminfo_find`・`terminfo_expand`）で引く。
- 使う能力: `cup`（必須）、`el`（必須）、`clear`、`smcup`/`rmcup`、`civis`/`cnorm`、`rev`/`sgr0`、`bel`。TERM が無い、または `.zti` が読めない時は VT100（ANSI）の文字列を内蔵の既定として使う（base の terminfo は全て VT100 互換）。読めた記述に `cup` か `el` が無い時（`dumb` など）は「vi: terminal 'X' cannot move the cursor」で終える。
- 標準入力と標準出力が端末でなければ、編集を始めずに終える（`vi: standard input is not a terminal`）。
- raw mode: `ICANON`・`ECHO`・`ISIG`・`IEXTEN`・`IXON`・`ICRNL`・`INLCR`・`IGNCR`・`ISTRIP` を落とし、`VMIN=1`・`VTIME=0`。`ISIG` を落とすので Ctrl-C・Ctrl-Z は key として届く。
- 窓の大きさ: TIOCGWINSZ。取れなければ terminfo の `lines`/`cols`、それも無ければ 24x80。最小 2 行・10 桁（未満なら最小として扱う）。SIGWINCH で印を立て、main loop が測り直して全部を描き直す。
- key: 1 byte ずつ読み、ESC の後 50 ms（poll）以内に `[` か `O` が続けば列として解く。解く列: 矢印（`ESC [ A-D`・`ESC O A-D`）、Home（`ESC [ H`・`ESC O H`・`ESC [ 1 ~`・`ESC [ 7 ~`）、End（`F`・`4 ~`・`8 ~`）、Delete（`ESC [ 3 ~`）、PageUp/PageDown（`5 ~`・`6 ~`）。知らない列は捨てる。続かなければ ESC 単独。
- 出力は自前の buffer（4 KiB）に溜め、1 回の描画の終わりに `write` で全部書く（EINTR は書き直す）。
- 描画: 画面の最後の行が状態と命令の行、残りが本文。行ごとに前回の出力の写しを持ち、変わった行だけを `cup`＋本文＋`el` で書き直す。描画の間は `civis`、終わりに cursor を置いて `cnorm`。

### 3. signal と端末の復元

- 開始: `tcgetattr` で元の mode を保存 → raw → `smcup`。終了（`:q` 等・error・signal）: `rmcup`・`cnorm`・`sgr0` を書き、元の mode を `tcsetattr(TCSADRAIN)` で戻す（先打ちの key は shell に残す）。復元は 1 つの関数にまとめ、`atexit` にも登録する（二重に呼んでも 1 回だけ働く）。
- SIGWINCH: 印（`volatile sig_atomic_t`）→ 描き直し。SIGCONT: 印 → raw に戻して全部を描き直す。
- SIGHUP・SIGTERM・SIGINT・SIGQUIT（外から）と、端末の read の 0・EIO（hangup）: 印 → main loop が端末を戻して終了（状態 1）。保存されていない変更がある時だけ `$HOME/.vi-recover.<名前>.<pid>` を `O_CREAT|O_EXCL`・0600 で書き、場所を標準 error に出す（決定 2026-10-14。HOME が無ければ作らない、`/tmp` は使わない）。
- Ctrl-Z（key）と外からの SIGTSTP（印）: 端末を戻す → SIGTSTP を既定の処理にし、block を外して `kill(0, SIGTSTP)`（process group 全体。vi を待つ親も止まる）→ 再開したら block と handler を戻し、raw・`smcup` に戻して窓を測り直し、描き直す。
- handler は印を立てるだけ。扱う signal は普段 block し、key を待つ間だけ `ppoll` の mask で通す。key を待つ前に毎回印を見るので、印を見た後・待つ前に来た signal も取りこぼさない（review M1）。SIGPIPE・SIGSEGV などは扱わない（既定のまま）。

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
- 繰り返し: `.`（決定 2026-10-14）。buffer を変えた直前の命令（count の後の key の列。insert の間に打った key を含む）を記録し、`.` はその key の列を入力の前に積んで流し直す。`.` に count が付けば元の count の代わりに使う。`u`・`Ctrl-R`・移動・`y`・`:` の命令は記録を変えない。記録は 4096 key まで（超えた命令は繰り返せず、前の記録を保つ）。
- その他: `Ctrl-L`（描き直し）、`Ctrl-G`（file 名・行の数・位置・変更の有無）、`ZZ`（`:x` と同じ）、`ZQ`（`:q!`）。知らない key は `bel`。
- ex（`:` の行。BS で編集、BS で空になったら・ESC で取り消し、Enter で実行）:
  - `:N`（行 N へ）、`:$`（最後の行）、`:.`
  - `:w`、`:w file`、`:w!`、`:w! file`、`:q`、`:q!`、`:wq`、`:wq file`、`:wq!`、`:x`、`:e file`、`:e!`（file を読み直す）、`:e! file`、`:f`（`Ctrl-G` と同じ）。
  - `:q`・`:e file` は変更が保存されていなければ断る（「no write since last change (add ! to override)」）。
  - 知らない命令は「unknown command: ...」。
- 起動: `vi [+N | +] [file]`（`+N` は行 N、`+` は最後の行）。引数の無い vi は名の無い buffer（`:w` は「no file name」で断り、`:w file` で名が付く）。`--` を受ける。知らない option は使い方を出して状態 2。**[判断]** file は 1 つだけ（推し: 2 つ以上は「vi: only one file can be edited at a time」で状態 2。`:n` は入れない）。

### 8. 入れない物

ex mode（`Q`・`:` の全部の ex の命令）、`:s` `:g` `:r` `:!` `:map` `:ab` `:set`、名前付きの register、mark（`m` `'`）、visual mode、text object、`%`、複数の file・window・buffer、tag、行の番号の表示、自動の字下げ、回復 file からの読み戻し（`-r`）、`U`（行の undo）、`R`（上書き mode）、`-R`/view、option の file（`.exrc`・`EXINIT`）。

### 9. file の保存の安全

- 名前が symlink なら `realpath` で行き先に書く（symlink を通常の file に置き換えない）。
- 書く前の確かめ: 既存の file が通常の file でなければ断る。他の名前（`:w other`）が既にあれば `!` 無しでは断る（「file exists (add ! to override)」）。既存の file に書けない（`access(W_OK)` が失敗）なら `!` 無しでは断る（「file is read-only (add ! to override)」）。`!` 付きでも OS が拒めばその error を知らせる。
- 書き方（既定）: 同じ directory に `mkstemp` で `.<name>.vi-XXXXXX`（名前が 200 byte を超えれば `.vi-XXXXXX`）を作る → 全部の行と改行を 64 KiB の塊で書く（短い write は続きを書く）→ 既存の file は `fchown`（所有者と group）の後に `fchmod`（`st_mode & 07777`。chown が set-id を落とすため、この順）、新しい file は `0666 & ~umask` → `fsync` → `close` → `rename` で置き換え → directory も `fsync`。途中で失敗したら一時 file を `unlink` し、元の file は変わらない。
- 直に書く場合: 既存の file の hard link が 2 つ以上、root でなく file の所有者か group が自分（`geteuid`・`getegid`）と違う（置き換えると持ち主が変わる）、または置き換えが EACCES・EROFS・EPERM・EXDEV で失敗した（sticky の directory など）時は、元の file を `O_TRUNC` 無しで開き、新しい大きさが大きければ先に `posix_fallocate` で場所を取る（失敗なら `ftruncate` で元の大きさに戻し、中身は無傷のまま知らせる）→ 先頭から書く → `ftruncate(新しい大きさ)` → `fsync`（link・inode・所有者・mode を保つ）。
- 名前が行き先の無い symlink なら、link の文字列（相対なら link の directory から）の file を作る。
- 書けたら「"name" N lines, M bytes written」。buffer の変更の印を落とす。
- 読み込みは `open`＋`fstat`（通常の file でなければ「not a regular file」で開かない、directory も）＋`read` の繰り返し。

### 10. 試験（手順 3）

- amd64 の build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk ZEDBSD_USER_PROGRAMS=vi BUILD=… <BUILD>/bin/vi` の形で個別の target を名指す（target 無しの make・disk-image は流さない）。warning 0。
- host 試験: `plan/ws228/tests/run-host-vi.py`。host の cc で vi を build（`common/terminfo.c` を含め、`include/libc/terminfo.h` だけを写した directory を include の道に置く。regex は host の libc）、python の `pty` で起動し `TERM=xterm`・`TERMINFO=userland/base/terminfo` で key を送り、保存された file の中身で判定する（画面は判定に使わない）。項目: 挿入と保存（`:wq`）、`x`・`dd`・`dw`・`D`、`yy`＋`p`、`u` と `Ctrl-R`、日本語の行で `l`・`x`・`r`（多 byte の文字を 1 つとして消す・置き換える）、`/` の検索の後の編集、`:q` が変更ありで断ること、`:q!`、保存で mode（0640）が保たれること、symlink の先に書くこと、端末の mode が終了の後に戻ること（`stty -g` の前後の比較）。出力は `build/tmp/` の mktemp の directory（消すのは Q1）。
- QEMU（p002、T1 への依頼の文）: §11。

### 11. T1 への依頼の文（p002 用、Q1 経由）

T1-NNN（ws228-p002、vi の guest の試験）:
- image: 依頼の時点の main の SHA（ws228-p001 の merge を含む）で、`make ZEDBSD_CONFIG=plan/ws228/tests/config-amd64-vi.mk` の image（CI amd64 から長い外部 package を除き、serial の mirror を有効にし、`vi` を足した物）。個別の file の複写は無し。
- 起動の確認: `plan/tools/boot-test.sh` で login prompt の PNG。
- 試験 A（serial）: `python3 plan/ws228/tests/vi-guest.py --socket <serial の socket>`。段: 2 行（1 行は日本語）の入力と `:wq`、日本語の 1 文字の `x`、`dd`→`u`、`x..`（`.` の繰り返し）、`:q` の後の `stty -g` が vi の前と同じ。合否: 最後の行が `vi-guest: ok (5 steps)`。
- 試験 B（VT console の画面、**ws228-p004 の kernel の修正が main に入った後**）: QEMU の画面の console（TERM=zed、login の既定）で `vi /etc/rc.conf` を開き、`j` を数回と `$` を打った画面を QMP の `screendump` で PNG に撮る。合否: 本文の行が左端から 1 行ずつ並び、最下行が空か message、cursor が `$` の行の末尾にあること（目視、PNG を Q1 に）。`:q` で shell に戻り、prompt が最下行にあること。
- 時間の目安: build の後、A は約 1 分、B は約 2 分。

## review-1 の反映（2026-10-14 P3）

review: [review-1.md](review-1.md)（design-reviewer、6af970de4 の設計）。決定は上の「決定」。各指摘の扱い:

| 指摘 | 扱い |
| --- | --- |
| F1 VT console の `cup` | ユーザーの決定で kernel を直す（ws228-p004、P1）。vi は変えない。p002 の確かめは p004 の後に VT console の PNG を足す |
| F2 読めない file | 起動の時の ENOENT 以外の失敗は編集を始めず状態 1（unreadable・directory を host 試験）。`:e`・`:e!` の失敗は buffer・名前・undo を変えない。上限は `st_size` と読んだ byte の数の両方で確かめる |
| H1 所有者・sticky | §9 の「直に書く場合」。rename の EPERM・EXDEV も直に書く |
| H2 set-id | `fchown` → `fchmod` の順 |
| H3 `:w 他の名前` | 変更の印は今の file の名前に書いた時（と名の無い buffer に名が付いた時）だけ落とす（w-other-then-q を host 試験） |
| H4 保存の判定 | group に単調に増える通し番号を振り、buffer の状態の名前（最後に適用した group の番号、無ければ base）と保存の時の名前を比べる。古い group を捨てる時は base をその番号にし、捨てた前の状態で保存していれば以後ずっと変更あり（saved-state を host 試験） |
| H5 O(n²) | 行の挿入・削除の基本操作を n 行の範囲にし、table の memmove を 1 回にした。undo の記録も範囲（`count` 行の text）。`dG`・`p` の count は 1 つの変更（20000 行の dG・u・yG・p を host 試験） |
| H6 長い 1 行 | `top_skip`（top の行の中の隠す表示の行の数）で cursor の部分を描く（screen-tall-line を host 試験） |
| H7 直に書く時の disk full | §9（`posix_fallocate` → 書く → `ftruncate` → `fsync`） |
| H8 hangup | read の 0 と EIO は VI_KEY_STOP（SIGHUP と同じ、状態 1、回復 file）。出力の write の error で打ち切る（hangup-recover・hangup-clean を host 試験） |
| M1 signal の窓 | §3（block と `ppoll`） |
| M2 Ctrl-Z | `kill(0, SIGTSTP)`、再開で `smcup` と測り直し |
| M3 auto margin | 幅が cols ちょうどの行には `el` を出さない。最下行は cols-1 桁まで |
| M4 幅が負の文字 | 正しい UTF-8 で幅が負（C1 など）の文字は byte ごとに `<C2><9B>` で表示する。状態の行（file の名前・message）も同じ描き方を通す |
| M5 幅の食い違い | 制限（下）。libc の表を直すのは Q1 に回す |
| M6 知らない escape の列 | 終端の byte（0x40-0x7E）まで読み、`VI_KEY_UNKNOWN` として捨てる（bell）。ESC としては扱わないので insert も終わらない |
| M7 編集の上限・count | 挿入と分割は buffer の大きさ（行と改行の byte の和）が 64 MiB を超えるなら EFBIG で断る（「File too large」）。count の上限は 1000000 |
| M8 undo の memory | undo の text の合計に 64 MiB の予算を設け、超えたら古い group から捨てる（最新は残す）。undo の記録の場所は buffer を変える前に確保する。命令の途中で失敗した時は、それまでの変更が group に残り `u` で戻せる |
| M9 検索 | 前方は cursor の次の文字から探すので長さ 0 の一致で止まらない。`?` は行の中の一致を辿り cursor より前の最後を取る。不正な byte・NUL を含む行は制限（下） |
| M10 `:wq` の失敗 | 書けなければ終えずに留まる（実装済み） |
| M11 試験 | p002 は `stty -g` の比較。host 試験に F2・H3・H4・binary の round trip・hard link・hangup を足した |
| M12 Phase と依存 | config/ci は Q1、F1 は ws228-p004（Q1 が投入） |
| M13 `f` の UTF-8 | `f`・`F`・`t`・`T`・`r` は先頭の byte から長さを決めて 1 文字を読む。`:`・`/` の行の BS は文字の単位 |
| 軽微 | `TCSADRAIN`、`ISTRIP` 等、Enter は `\r`・`\n`、smcup の無い端末は最下行へ移って `el`、出力は満ちたら書き出す、先打ちの key がある間は描かない、保存はまとめて書く、行き先の無い symlink、一時 file の名前の長さ、directory の fsync、`TERMINFO` を読む、`5i-` の count（1 行の中で打った text を繰り返す。`o`・`O` の count は無視）、外からの SIGINT・SIGQUIT は SIGTERM と同じ |

### 制限（既知、p001 の時点）

- **幅（M5）**: 表示の幅は libc の `wcwidth`（結合文字は 0、CJK の範囲は 2）。端末が違う幅で描く文字（Ambiguous を 2 にする設定、結合文字を 1 桁に置く端末）では、その行の中で cursor と表示がずれる。行ごとに `cup` で位置を決めるので、ずれは 1 行の中に留まる。libc の表を端末と合わせるのは Q1 に回す（Future Work か bug）。
- **検索（M9）**: zedBSD の regex は不正な UTF-8 を含む行を照合しない（REG_NOMATCH）。NUL を含む行は NUL の前までしか照合しない。行を越える一致は無い。
- **Ctrl-R**: POSIX の vi では Ctrl-R は描き直し。この vi では redo（決定の (1)）。描き直しは Ctrl-L。
- **count**: `o`・`O` の count、insert の中の改行を含む text の count は無視する。
- **hangup の回復 file**: 書くのは buffer の中身だけで、読み戻す命令（`vi -r`）は無い（file を手で開く）。
- **F1**: p004 が入るまで、zedBSD の VT console（TERM=zed）では画面が崩れる。serial（TERM=vt100）と desktop の terminal は影響なし。

## 実装の記録（2026-10-14 P3）

- source: `userland/base/vi/`（main.c・command.c・ex.c・search.c・screen.c・terminal.c・buffer.c・undo.c・file.c・vi.h・Makefile、`common/terminfo.c` を共有）。外部の vi の source は使っていない。
- amd64 の build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk ZEDBSD_USER_PROGRAMS=vi -o <worktree>/build/amd64/sysroot/.zedbsd-sysroot-complete build/amd64/bin/vi`（worktree の中）。warning 0（`-Wall -Wextra -Werror`）。
- host 試験: `python3 plan/ws228/tests/run-host-vi.py`（host の cc で build、pty で key を送り、書かれた file と、簡単な xterm の模倣で読んだ画面で判定）。`host-vi: ok (44 cases)`。`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"` でも PASS。host の試験は glibc の regex と wcwidth を使うので、M5・M9 は見えない。
- p002 の試験の道具: `plan/ws228/tests/config-amd64-vi.mk`（image）、`plan/ws228/tests/vi-guest.py`（serial で vi を操作して `cat` と `stty -g` で判定）。
