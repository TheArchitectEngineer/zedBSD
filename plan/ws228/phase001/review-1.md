# ws228-p001 review-1（6af970de4 の設計の敵対的 review、2026-10-14、Q1 が起動した design-reviewer）

## ws228-p001 の設計 review（commit 6af970de4 の plan/ws228/phase001/phase.md の「設計」節）

対象は `git -C /home/awe/zedBSD-worktrees/p3 show 6af970de4:plan/ws228/phase001/phase.md` です（以下、「設計 L番号」はこの file の行）。source と plan は変えていません。build・QEMU も流していません。根拠は全て code を読んで得たもので、実際の動きは観測していません。

注意: p3 の worktree には、この commit の後の未 commit の変更（`M plan/ws228/phase001/phase.md`、`?? userland/base/vi/`）があります。そちらは見ていません。

### 致命的

**F1. zedBSD のテキストの VT console では `cup` が効かないので、vi の画面が描けない**
- 根拠:
  - `src/kern/tty.c:1528-1569`（`tty_render`）は、CSI の中の `;`（0x20-0x3f の数字でない byte）を見ると `console_escape_ignored=1` にして、その列を丸ごと捨てます。
  - `tty_console_csi`（tty.c:1415-1493）は引数を 1 つしか持ちません。`H` は常に (0,0) へ移ります（tty.c:1436）。`m`（SGR）は扱いません。
  - 対話の sh は TERM が無いと `zed` にします（`userland/base/sh/main.c:479-481`）。`userland/base/terminfo/zed.zti` は `cup=\E[%i%p1%d;%p2%dH` を持つと書いているので、設計 L29 の「cup が無ければ断る」の確認は通ってしまいます。
- 起こること: console で vi を開くと、cursor の移動が全部捨てられ、画面が壊れます。base の editor が一番要る console（救援・desktop の無い image）で使えません。p002 の試験（設計 L104、serial で `TERM=vt100`、`cat` で判定）では見つかりません。
- 直し方（**[ユーザーの判断]**）:
  - (a) kernel の `tty_render`/`tty_console_csi` に、引数 2 つの `H` と `m` の最小限を足す。`src/kern` なので HAL の API は変わりませんが、WS228 の範囲の外です。別の WS か Phase にして、p001/p002 の依存にする。
  - (b) vi の側で、`cup` の代わりに `\E[H`＋`\E[nB`＋`\E[nG`（console が扱える物）を組み立てる。TERM で分ける場当たりの策です。
  - (c) `zed.zti` から `cup` を外し、console では vi が断る。
  - どれでも、p002 に「VT console で vi を開いた画面の PNG」の確かめを足す。

**F2. 読み込みに失敗した file の名前のまま、空の buffer で編集が始まりうる（`:w` で中身を消す）**
- 根拠:
  - 設計 L48 は「無い file は新しい file」とだけ書き、ENOENT 以外の失敗（EACCES、EIO、途中の read の失敗、directory、L49 の「file too large」）の後の状態を決めていません。
  - L49 の「超える file は開かずに」が、起動を止めるのか、空の buffer で始めるのかが分かりません。
- 起こること: 100 MiB の log、または mode 0200 の file を開く → 空の buffer → 何か打って `:wq` → 元の file が数 byte になります。`:e file` と `:e!` が失敗した時に、今の buffer を捨てるかどうかも書かれていません。
- 直し方:
  - 起動の時: ENOENT 以外の失敗は、編集を始めずに状態 1 で終える。
  - `:e`・`:e!`: 失敗したら、今の buffer・名前・undo を一切変えない。
  - 例外を設けるなら、buffer に「読み込み失敗」の印を付け、その名前への `:w` は `!` 無しでは断る。
  - 上限の確かめは `st_size` ではなく、読んだ byte の数で行う（読む間に伸びる file、size が 0 の特殊な file があるため）。

### 重大

**H1. 保存の時に持ち主と group が黙って変わる。sticky の directory で保存できない**
- 根拠: 設計 L90「`fchown`（EPERM は無視）」。L91 の在来の書き方への切り替えは、`nlink>1` と一時 file が作れない時だけです。
- 起こること:
  - group で書ける他人の file を mkstemp→rename で置き換えると、持ち主が自分に、group が自分の主 group に変わります。元の持ち主と group の人が書けなくなります。
  - `/tmp` のような sticky の directory で、他人の 0666 の file を保存すると、rename が EPERM になり、L90 のとおり error で終わって保存できません。
- 直し方: 書く前に `st_uid != geteuid()`（root を除く）、または group を戻せない（`st_gid` が自分の group でない）と分かったら、L91 の在来の書き方を使う。rename の EPERM・EXDEV も在来の書き方に回す。

**H2. `fchmod` の後に `fchown` をすると、set-id の bit が消える**
- 根拠: 設計 L90 の順番。kernel は chown で S_ISUID/S_ISGID を落とします（`src/kern/syscall.c:6578-6585`）。
- 直し方: `fchown` → `fchmod` の順にする。

**H3. `:w 他の名前` で変更の印が落ちる → `:q` で編集が失われる**
- 根拠: 設計 L92「書けたら…buffer の変更の印を落とす」が、どの書き込みにも掛かっています。
- 起こること: `:w /tmp/bak` → `:q` が断られずに終わり、元の file は保存されていません。
- 直し方: 印を落とすのは、今の file の名前に buffer の全部を書いた時（または、名の無い buffer に名が付いた時）だけにする（POSIX もこの振る舞い）。

**H4. 保存した時点を「group の位置」で覚えると、変更があるのに「変更なし」と判定される**
- 根拠: 設計 L56。
- 起こること: 変更 A → `:w`（位置 1）→ `u`（位置 0）→ 変更 B（位置 1、redo を捨てる）→ 位置が保存の時と同じなので「変更なし」→ `:q` で B が失われます。L55 の 1000 の上限で古い group を捨てる時も、位置がずれます。
- 直し方: group ごとに単調に増える通し番号を振り、保存した時の番号と、今の先頭の group の番号を比べる。保存した時点の group を捨てたら、ずっと「変更あり」にする。

**H5. 行の挿入と削除が 1 行ずつの memmove だと、大きな file で O(n²) になる**
- 根拠: 設計 L47 は「行の挿入・削除は配列の memmove」、L50 は基本操作を 1 行の単位で書いています。
- 起こること: 4M 行の file で `ggdG`・`1000p`・大きな削除の undo をすると、1 行ごとに数十 MB の memmove が走ります。数分から数時間かかり、その間 ISIG が落ちているので Ctrl-C でも止められません。
- 直し方: 基本操作を「n 行の範囲」の挿入・削除にし、memmove を 1 回にする。undo の記録も範囲の単位にする。

**H6. 画面より長い 1 行の中で cursor が表示できない**
- 根拠: 設計 L62 は「入り切らない行は `@`」とだけ書いています。上限（L49）では 64 MiB の 1 行の file も開けます。
- 起こること: 縮めた JS や base64 の 1 行の file では、cursor のある行が画面に入らず、cursor が見えないか、描画が壊れます。
- 直し方: cursor の行が画面より高い時は、その行の中の表示の行の offset から描く（nvi の方式）。

**H7. 在来の書き方（`O_TRUNC`）では、disk full の時に file が壊れ、全部の hard link に及ぶ**
- 根拠: 設計 L91。
- 直し方: `O_TRUNC` を使わずに開く → 新しい size が大きければ `posix_fallocate(fd, 0, newsize)` を呼ぶ（`src/libc/fallocate.c`、既にある中身は残す実装）→ 失敗したら `ftruncate(fd, 旧 size)` で戻して知らせる（元の中身は無傷）→ 成功したら先頭から書き、最後に `ftruncate(newsize)` と `fsync`。

**H8. hangup の後に read が 0 を返し続け、CPU を 100% 使い続けうる**
- 根拠: 生の mode の read は、hangup の後すぐ 0 を返します（`src/kern/tty.c:2285-2287`）。write は EIO です（tty.c:2151-2153）。設計 L39-43 は read の 0 と EIO の扱いを書いていません。SIGHUP を無視する設定（`nohup vi`）だと、終わる契機がありません。
- 直し方: read の 0 と EIO を SIGHUP と同じに扱う。復元の write は EIO で打ち切る。

### 中

**M1. SIGTERM・SIGHUP・SIGWINCH を受け損なう窓がある**
- 根拠: 設計 L43「SA_RESTART 無しで EINTR」。印を見た後、`read` で待ち始める前に signal が来ると、次の key まで気付きません。`kill` された vi が残り、`kill -9` すると端末が raw のまま残ります。
- 直し方: signal を block しておき、`ppoll(..., &空の mask)` で待つ（`include/libc/poll.h:20` にある）。または self-pipe を使う。

**M2. Ctrl-Z で止めるのが vi だけだと、呼んだ側が固まる**
- 根拠: 設計 L42「自分に送る」。
- 起こること: `git`・`crontab -e` のような親が vi を待っていると、親は止まらないので shell に戻れません。vim と nvi は `kill(0, SIGTSTP)` で process group 全体を止めます（vim・nvi の source との照合はしておらず、記憶による）。
- 直し方: `kill(0, SIGTSTP)` にする。再開の時は `smcup` を出し直し、窓の大きさを測り直す。

**M3. auto margin（`am`）の端末で、行末の書き方が画面を壊す**
- 根拠: `zed`・`xterm`・`linux` の記述は `am=1` で、`xenl` がありません。desktop の terminal は、最後の桁で折り返しを保留します（`userland/desktop/terminal/screen.c:1307-1335`）。
- 起こること: 行がちょうど cols 桁で埋まった後に `el` を出すと、xterm 系では最後の文字が消え、xenl の無い端末では次の行が消えます。最下行の最後の桁に書くと、画面が scroll します。
- 直し方: 幅が cols ちょうどの行には `el` を出さない。状態の行は cols-1 桁までにし、右下の 1 桁には書かない。

**M4. 表示の幅が負の文字の扱いが決まっていない（端末の制御列を送り込まれうる）**
- 根拠: libc の `wcwidth` は C1（U+0080-009F）に -1 を返します（`src/libc/wide.c:155-176`）。設計 L61 は byte の制御文字と、不正な列しか扱っていません。
- 起こること: xterm を ssh 越しに使うと、file の中の U+009B などが CSI として解かれる恐れがあります。file の名前と message にも同じ穴があります。
- 直し方: 正しい UTF-8 で幅が 0 未満の code point は、`<C2><9B>` などの escape で表示する。message の中の file の名前も escape する。

**M5. libc の `wcwidth` と zedBSD の terminal の幅が食い違い、cursor がずれる**
- 根拠: terminal は結合文字を 1 桁で置き、Ambiguous を 2 桁にする設定も持ちます（`userland/desktop/terminal/width.c`、`screen.c:1307-1360`）。libc は結合文字を 0、`0x2e80-0xa4cf` を全部 2 にします（wide.c:142-176）。
- 直し方: 行ごとに `cup` で位置を決めるので、ずれは 1 行の中に留まります。既知の制限として phase.md に書き、libc 側の表を直すことを Future Work か bug として Q1 に回す。

**M6. 知らない escape の列が途中までしか読まれないと、残りが命令として実行される**
- 根拠: 設計 L33。zedBSD の端末は `\E[2~`（tty.c:384、keys.c:113）、`\E[15~`、`\EOP` を送り、修飾の付いた `\E[1;5A` もありえます。
- 起こること: 例えば `;5A` が残ると、`;` と `5A`（5 回の append）として実行されます。
- 直し方: CSI は引数（0x30-0x3F）、中間（0x20-0x2F）、終端（0x40-0x7E）まで、SS3 は 1 byte まで読んで捨てる。

**M7. 編集で上限を超えられるので、vi が後で開けない file を書けてしまう。count に上限が無い**
- 根拠: 設計 L49 の上限は読み込みにしか掛かりません。L67 の count は「掛け合わせる」だけです。
- 起こること: `ggyG` と `P` を繰り返すと 64 MiB を超え、`:w` の後は「file too large」で開けません。`999999999p` は止められません（ISIG が落ちているため）。
- 直し方: 編集でも上限を確かめて断る。count は飽和させ、上限を設ける（例 1e6）。

**M8. undo の memory に byte の上限が無い。memory の確保に失敗した時の戻し方が書かれていない**
- 根拠: 設計 L55 は 1000 group の上限しか持たず、64 MiB の削除が 1000 個残りえます。L49 は「buffer を変えず」と書くだけで、方法がありません。
- 直し方: undo に byte の予算（例 buffer と同じ大きさ）を設ける。undo の記録を先に確保してから buffer を変える。途中で失敗したら、その group を巻き戻す。

**M9. 検索の落とし穴**
- 根拠:
  - zedBSD の regex は、行の中に不正な UTF-8 があると、その行の照合が REG_NOMATCH になります（`src/libc/regex/regexec.c:53-64`）。
  - `REG_STARTEND` は `strlen` で範囲を確かめて写すので、NUL を含む行は途中で切れます（regexec.c:987-1001）。
- 起こること: 長さ 0 で一致する pattern（`/^`、`/x*`）で `n` が同じ位置に留まり、止まらない恐れがあります。
- 直し方:
  - 長さ 0 の一致の後は、1 文字進めてから探す。
  - `?` は行の中の一致を順に辿り、cursor より前の最後の一致を取る。
  - 不正な byte を含む行と NUL を含む行は一致しないことを、制限として書く。

**M10. 書き込みに失敗しても `:wq`・`:x`・`ZZ` が終わってしまう恐れがある**
- 根拠: 設計 L77・L74 に、失敗した時の扱いがありません。
- 直し方: 書き込みに失敗したら、vi を終えずに留まると明記する。

**M11. 試験で誤りを見つけられない**
- p002 の `stty -a が icanon echo を含む`（設計 L104）は必ず通ります。zedBSD の stty は、切った mode を `-icanon`・`-echo` と書くので（`userland/base/stty/main.c:1000-1001`）、文字列 `icanon` は常に含まれます。`stty -g` の前後の比較か、`-icanon` が無いことで判定してください。
- host 試験（設計 L98）に次を足すよう勧めます:
  - 読み込みの失敗の後の `:wq`（F2）
  - `:w other` の後の `:q`（H3）
  - 保存 → `u` → 別の変更 → `:q` が断られること（H4）
  - binary の round-trip（NUL・不正な byte・CRLF・末尾の改行無し → 改行を 1 つ足すだけ）
  - `nlink>1` の file の保存
  - pty を閉じた後に CPU を使い続けないこと（H8）
- host の試験は glibc の regex と `wcwidth` を使うので、M5・M9 は host の試験では見えません。

**M12. Phase と依存の抜け**
- CI の image に vi を入れる `config/ci/config-*.mk` の変更（設計 L24）を持つ Phase がありません。WS の受け入れ（`/bin/vi` が入ること）が満たされません。
- その中の `config/ci/config-amd64-server.mk` はユーザーが持つ file です（**[ユーザーの判断]**、触るかどうか）。
- F1 の kernel の console の依存も、Phase の表にありません。

**M13. `f`・`F`・`t`・`T` の引数が UTF-8 の 1 文字か決まっていない**
- `f日` が効かないと、日本語の利用者が驚きます。`r` と同じく、先頭の byte から長さを決めて読むと明記してください。
- ex の行の BS も、文字の単位で消すと書いてください。

### 軽微
- 復元に `TCSAFLUSH`（設計 L39）を使うと、`:wq` の後に打ち込んだ shell の入力が捨てられます。`TCSADRAIN` を勧めます。
- raw mode では `INLCR`・`IGNCR`・`ISTRIP` も落としてください。Enter として `\r` と `\n` の両方を受けてください。
- `smcup` の無い端末（zed・vt100）で終える時は、最下行へ移って `el` を出してください。今のままだと、shell の prompt が本文の途中に出ます。
- 出力の buffer の 4 KiB（設計 L34）は、全部を描き直す 1 回ぶんより小さくなりえます。満ちたら書き出し、短い write は続きを書き、EIO で打ち切ってください。
- 入力が残っている間は描き直さないでください（貼り付けが遅くなる）。保存は行ごとの write ではなく、まとめて書いてください。
- 行き先の無い symlink では `realpath` が ENOENT になり、新しい file を作れません。一時 file の名前が NAME_MAX を超えうる問題もあります。rename の後は directory も fsync してください。
- 環境変数 `TERMINFO` を読むことを明記してください（host 試験はこれに頼っています。tput と curses は読んでいます）。
- 行頭の結合文字の扱い、`x` が結合文字ごと消すこと、非 ASCII の文字の語の分類、`/pat/` の末尾の区切りと `\/`、`5i-` の count、`r<Enter>`、`J` が次の行の先頭の空白を除くこと（POSIX の細部は推測）、`:N` が行の数を超えた時、mode の表示を、それぞれ決めてください。
- 外から送られた SIGINT・SIGQUIT は既定の処理のままなので、端末が raw のまま残ります。

### P3 の判断 (1)〜(6) への意見
1. **多段の undo と Ctrl-R**: 推しに賛成です。ただし POSIX では Ctrl-R は「描き直し」です。この食い違いは phase.md に書いてください。
2. **`.` を入れない**: 使う頻度が非常に高い命令です。最後の変更の key の列を記録して、同じ dispatcher で再生すれば安く作れます。入れることを勧めます（**[ユーザーの判断]**）。
3. **`gg`**: 賛成です。
4. **file は 1 つ**: 賛成です。
5. **64 MiB・4M 行**: 上限が読み込みにしか掛からない点（M7）と、O(n²)（H5）を直す前提で賛成します。小さな memory の機種（pcat・pc98・rpi4）では、上限より先に memory の確保の失敗が起こるので、M8 の戻し方が要ります（memory の見積もりは推測）。
6. **SIGHUP・SIGTERM で回復 file を作らない**: POSIX の ex は、SIGHUP で buffer の保存を試みることを求めています（記憶による）。init は停止の時に SIGTERM を送るので（`userland/base/init/main.c:1562`）、回復 file が無いと shutdown で編集が失われます。最小の案は、変更がある時だけ `$HOME/.vi-recover.<name>.<pid>` に `O_CREAT|O_EXCL`、mode 0600 で書くことです（`/tmp` は避ける）。**[ユーザーの判断]**

### ユーザーの判断が要る点
- F1 の直し方: (a) kernel を直す、(b) vi で組み立てる、(c) `zed.zti` から `cup` を外す。
- `.` を入れるか（判断 2）。
- SIGHUP・SIGTERM の回復 file（判断 6）。
- `config-amd64-server.mk` に vi を足すか（M12）。

### 実施しなかったこと
- build・host 試験・QEMU は流していません。
- F1 は code を読んだ結論で、QEMU では確かめていません。
- vim・nvi・POSIX の細部（Ctrl-R が描き直しであること、`kill(0, SIGTSTP)`、ex の SIGHUP の保存）は、記憶との照合で、原文は確かめていません。

### 主な根拠の file
- /home/awe/zedBSD-worktrees/p3/src/kern/tty.c
- /home/awe/zedBSD-worktrees/p3/userland/base/terminfo/zed.zti
- /home/awe/zedBSD-worktrees/p3/userland/base/sh/main.c
- /home/awe/zedBSD-worktrees/p3/src/kern/syscall.c
- /home/awe/zedBSD-worktrees/p3/src/libc/fallocate.c
- /home/awe/zedBSD-worktrees/p3/src/libc/wide.c
- /home/awe/zedBSD-worktrees/p3/src/libc/regex/regexec.c
- /home/awe/zedBSD-worktrees/p3/userland/desktop/terminal/screen.c
- /home/awe/zedBSD-worktrees/p3/userland/desktop/terminal/width.c
- /home/awe/zedBSD-worktrees/p3/userland/base/stty/main.c
- /home/awe/zedBSD-worktrees/p3/include/libc/poll.h
