<!-- awesome-plan project=zedbsd record=ws035p120 -->

# ws035-p120: デモの名前のある利用者 kei（「Kei」）と、root 以外で壊れていたもの（BUG-097・BUG-098）

Phase ID: `ws035-p120`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 ユーザーの決定「デモの利用者・KVM の検討の時期」、main 経由の割り当て。p116 の main への一覧の 6）
Bugs: [BUG-097](../../bugs/BUG-097.md)（mkdir）、[BUG-098](../../bugs/BUG-098.md)（pty）。作業中の報告では ws035-p118 と書いた。

## 範囲

デモの image（実機 `plan/ws075/demo/build-demo-image.sh`、Venus `plan/ws035/tests/build-demo-venus-image.sh`）に普通の利用者 `kei`
（表示名「Kei」、home あり）。greeter が出し、Files の Home の挨拶は表示名（「Good morning, Kei」）。root の空の password の login を外す。
session の利用者は `network` の group（p104）。Notes の保存先が kei の `~/Documents/Notes`、PDF Viewer・Files・Terminal が root 以外で動くこと
（/dev/gpu0、入力、socket）。demo-walk を kei で通し、壊れたものを直す。

## 決定: password なし（demo の image だけ）、root は lock

- `plan/ws035/demo/demo-accounts.sh OUTDIR`: base の passwd・group・shadow から demo 用を作る（base の file は変えない）。
  `kei:x:1000:1000:Kei:/home/kei:/bin/sh`、`network:x:69:kei`、`kei:x:1000:`、shadow は `kei::…`（空）、**root は `*`（lock）**。
  二つの build script が `ZEDBSD_TEST_EXTRA_FILES` で base の上に置く（後の `--file` が勝つ）。
- password なしにした理由: greeter と lock の Enter で入れる（デモで打たない）。login の検査（`userland/base/login/verify.c`）は空の shadow には空の
  password だけを通す。sshd は `PermitEmptyPasswords no` なので kei の password なしの ssh は無い（seat の login だけ）。root は lock なので
  root の空の password の login は無い。greeter は人の account があると root を出さない。
- 注意: 実機の demo の image には root で入る手段が無くなる（su も無い）。保守が要るなら shadow の root を戻した別の image を作る（main の判断）。
  Venus の image は harness の SSH の鍵で root に入れる（lock でも鍵の login は通る、確認済み）。

## 実装

- `userland/desktop/sessiond/session.c` `session_home()`: home が無い account は最初の login で作る（0700、利用者の所有。親が root の所有で
  group・other が書けない directory のときだけ、`O_NOFOLLOW` で開いて `fchown`）。image は directory に所有者を付けられないため。
  log `SESSIOND SESSION home=/home/kei made uid=1000`。
- `userland/desktop/files/ui.c`: Home の挨拶の名前は GECOS の最初の field（greeter と同じ）。root は GECOS が役目（System Administrator）なので
  account の名前のまま。
- `plan/ws035/tests/demo-walk.sh`: kei で通す（session の log `/run/user/1000`、PDF は `/home/kei/Documents` に kei の所有で、Notes の保存を
  `ls -l` で確かめる、Terminal に `id`、05b で X terminal、07 で bar hidden の log、logout は `user=kei`）。
- **BUG-097**（main の許可、`src/kern/syscall.c`）: mkdir は先に `inode_lookup`、あれば EEXIST、ENOENT なら権限の検査と作成。修正前は kei の
  `mkdir("/home")` が EACCES で、session.sh の `mkdir -p $HOME/Documents` と Notes の journal・保存の folder 作りが失敗していた
  （「Could not keep the stroke」、`p118-20260929-bug097-before-notes-could-not-keep.png`）。一時的に入れた session.sh の回避は戻した（`mkdir -p` のまま）。
- **BUG-098**（main の許可、`src/kern/tty.c`）: `pty_master_open` は新しい pty の slave を開いた process の実の uid の所有に（gid は 0、mode 0620。
  main の指示: 呼び出し側の group にしない。tty の group はこの system に無い）。修正前は slave が常に root の所有で、kei の Terminal が
  `ZTERM FAILED operation=forkpty errno=25`（EACCES）で起動しなかった。

## 検証（2026-09-28、QEMU の Venus。実機は未実施）

- demo-walk（最終の image、1920x1280）: 全段 ok、`demo-walk: done`（rc 0）。greeter `users=1 selected=kei`、`SESSIOND SESSION start user=kei uid=1000`、
  home made、Documents は kei の所有、`ZTERM START`、X terminal（`/bin/zterm`・`/bin/xserver` が動く、`id` の出力）、Notes の保存
  `-rw-r--r-- 1 kei kei 7210 … /home/kei/Documents/Notes/note-20260928-115128.pdf`、bar hidden、lock・unlock（Enter）、`SESSION end user=kei`。
  画面 `/home/awe/zedBSD-rpi4/build/ws035-shots/p118-20260929-kei-{00-splash,01-greeter,02-desktop,03-apphome,04-files,05-terminal,05b-xterminal,06-notes,07-pdfviewer,08-wiseview,09-lock,10-unlocked,11-logout}.png`。
  Files の Home「Good morning, Kei」（04）、Terminal と X terminal の `id` は `uid=1000(kei) gid=1000(kei) groups=1000(kei),69(network)`。
- `plan/ws035/tests/p120-kei-mkdir.sh`（BUG-097）PASS: kei で `mkdir /home`・`mkdir /etc` → File exists、`mkdir /p118-new`・`mkdir /etc/p118-new` →
  Permission denied（作られない）、`mkdir -p $HOME/Documents/p118/a/b` → 作られ kei の所有、もう一度 rc 0。
- pty の probe（BUG-098、scratch の `build/p118-pty/`、未 commit）: 修正前 kei は slave が uid 0 で open EACCES。修正後 kei は `mode 20620 uid=1000 gid=0`
  で open 成功、root も成功、kei が持つ slave を別の非 root の利用者（uid 1001）が開くと EACCES、root は開ける。sshd の pty の login（kei、`ssh -tt`）は
  `/dev/pts/0` が `crw------- kei kei`（sshd の chown のまま）。
- boot test（demo の image、GPU の無い QEMU は console の login）: PASS、`/home/awe/zedBSD-rpi4/build/ws035-shots/p118-20260929-boot-test.png`。
- build warning 0（-Werror）、`plan/tools/style-check.py` ui.c・session.c 0、syscall.c の変更の範囲 0（周りの既存の指摘は残る）。
- 未実施: 実機（5330 + HDMI）の demo の image の build と起動（`plan/ws075/demo/build-demo-image.sh` は変えたが build していない）。

## main への一覧（範囲外）

1. Terminal の prompt が `root@kei:/home/kei$`: `userland/base/sh/main.c` の既定の prompt が「root@」を固定で書く（PS1 が無いとき）。kei でも root と
   見える（`p118-20260929-kei-05-terminal.png`）。base の sh の担当へ。
2. 実機の demo の image に root の保守の手段が無い（上の決定の注意）。
3. `plan/ws075/demo/build-demo-image.sh`（WS075）を変えた（demo-accounts.sh を呼ぶ 3 行と注釈）。割り当ての範囲だが WS075 の file。
