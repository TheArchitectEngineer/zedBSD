<!-- awesome-plan project=zedbsd record=ws160-p001 -->

# ws160-p001: su・sudo・passwd の設計と実装

Status: cleared（2026-10-05 Q1: T1-127 の p001-guest.sh PASS（試験の直し e777d3ba の後、全部の行）、T1-118 の他の 23 行 ok）。以前: in-progress（2026-10-05 P1 generation17 / q721-i01。設計・実装・build・host の試験まで。QEMU は Q1 経由で T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS160](../ws.md)
Queue: q721 / q721-i01（Q1 の投入、ベータ1）

## 今の状態（2026-10-05 に読んだ）

| 所 | 今 |
| --- | --- |
| password の確かめ | `userland/base/login/verify.c` の `login_verify`（passwd と shadow の両方にあり、shadow が `!`・`*` で始まらず、crypt が一致）。login と sessiond が使う |
| crypt | libc の `crypt`（SHA-512 crypt `$6$`、`rounds=`）、`getspnam_r`。`getentropy`・`readpassphrase`（`RPP_STDIN`・`RPP_REQUIRE_TTY`）・`initgroups`・`syslog`・`issetugid` もある |
| 帳簿 | `/etc/passwd`（kei 1000）、`/etc/group`（wheel 0 は root だけ）、`/etc/shadow`（mode 0400、kei の password は `kei`）。release は root を `*` で lock（`ZEDBSD_ROOT_LOCKED`、U10） |
| setuid | kernel の exec が S_ISUID・S_ISGID を取る（nosuid の mount と script は取らない）、saved は effective に、AT_SECURE を渡し、rtld は AT_SECURE で環境の library path を見ない。set*id の system call は POSIX の通り。package の Makefile の 11 番目の引数が mode（newgrp が 4755） |
| **穴（kernel）** | **PT_ATTACH に資格の確かめが無い**: 誰でも誰の process でも trace できる（root の daemon も）。**trace されている process の set-id の exec が昇格する**（tracer が root の process を握る）。su・sudo を入れる前に直す必要がある |

## 設計

### K. kernel（setuid を安全にする）

- `struct process` に `set_id`: exec が set-id の bit を取った時（AT_SECURE と同じ値）、または set*id の system call が実 ID・実効 ID・saved ID のどれかを変えた時に 1。set-id でない exec で 0。fork で子に受け継ぐ。
- `cred_may_trace(tracer, target, set_id)`（`src/kern/cred.c`）: superuser は誰でも。それ以外は、tracer の実 ID と実効 ID が同じで、target の実・実効・saved の user と group の ID が全部 tracer の実 ID と同じで、target の `set_id` が 0 の時だけ。PT_ATTACH はこれで EPERM を返す（`src/kern/ptrace.c`）。
- trace されている process の exec は set-id の bit を取らない（nosuid の mount と同じ扱い、`src/kern/exec.c`）。
- （残す）signal は POSIX の通り（利用者は自分の sudo を kill できる）。core dump は kernel に無い。

### U. 共通の核（`userland/base/common/account.c`・`.h`）

- 新しい password の規則 `account_password_check`: 利用者が選ぶ時は 8 文字以上（root が設定する時は空でなければよい）、256 文字以下、制御文字なし、前の password と違う（D1）。
- hash `account_password_hash`: SHA-512 crypt、`rounds=65536`、`getentropy` の 16 文字の salt。
- `/etc/shadow` の書き換え `account_shadow_set`: lock file（`/etc/shadow.lock` を O_EXCL で作る。1 分より古ければ crash の残りとして消す）、全体を読み、その利用者の行の 2 番目（hash）と 3 番目（最後に変えた日）だけを替え（他の byte はそのまま、純粋な関数 `account_shadow_replace`）、`/etc/.shadow.XXXXXX` に mode 0400 で書いて fsync、`/etc/shadow` へ rename、`/etc` を fsync。途中で止める signal は保留。読み手は古い物か新しい物かを見る。
- wheel の判定 `account_in_wheel`: 名前 `wheel` の group が primary か、member に名がある。
- 別の利用者で動かす command の環境 `account_environment`: 呼んだ側の TERM・COLORTERM・LANG・LC_*・TZ だけを残し、HOME・SHELL・USER・LOGNAME・PATH（`/bin:/sbin:/usr/bin:/usr/sbin`）を目標のものに、sudo では SUDO_USER・SUDO_UID・SUDO_GID・SUDO_COMMAND。

### P. 道具（全部 set-user-ID root、mode 4755、`login_verify` と核を使う）

| 道具 | 動き |
| --- | --- |
| `passwd [user]` | 端末で今の password（root は不要）、新しい password を 2 回。root だけが他の利用者を指定できる。間違った今の password は 2 秒待つ。syslog（auth） |
| `passwd -s [user]` | 標準入力から 1 行ずつ（今・新、root が設定する時は新だけ）、prompt なし。**Settings の経路**（下）。終了 status: 0 変えた、1 失敗、2 使い方、3 今の password が違う、4 新しい password が規則に合わない、5 2 回が違う |
| `su [-] [-l] [user] [-c command]` | 既定は root。root は password 不要、それ以外は目標の password（端末、無ければ標準入力）。lock された account（release の root）は間違いと同じく拒む。`-` で login shell（環境を作り直し、home へ、`-sh`）、無しは呼んだ側の環境から LD_*・IFS・ENV・BASH_ENV・CDPATH・PS4・SHELLOPTS を消し HOME・SHELL・USER・LOGNAME を目標に。失敗は 2 秒、syslog |
| `sudo [-S] [-u user] command…`・`-s`・`-i` | **規則**: root と wheel の member は誰としてでも何でも動かせる、他は拒む（sudoers の file は無い、D2）。自分の password（端末で 3 回、`-S` で標準入力から 1 回）。**記憶しない**（毎回聞く、D3）。command は secure PATH から探す（呼んだ側の PATH は使わない）。環境は作り直す。拒否は 2 秒、全部を syslog（`kei : TTY=… ; USER=root ; COMMAND=…`） |

### G. 帳簿

- `/etc/group`: `wheel:x:0:root,kei`（base の file。release もこれを使う）。
- `config/ci/config-amd64.mk` の program に passwd・su・sudo（release は CI の config を include するので入る）。

### S. Settings から（p002 の経路、ここで決める）

- **Settings は root の権限を持たない。** Users の頁は `/bin/passwd -s` を起動し、pipe で今の password と新しい password を 1 行ずつ渡し、終了 status（0・3・4・5・1）で結果を出す（3 は「今の password が違う」、4 は規則の理由）。passwd が自分を確かめ（実 user ID の利用者だけを変える）、書き換えるので、Settings に新しい setuid の helper は要らない（攻撃の面が増えない）。
- Linux・FreeBSD の Settings では libkeiland-backend の口にする（p002 で決める。ベータ1 では zedBSD だけ）。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `include/kern/process.h`・`src/kern/process.c` | `set_id`、fork で受け継ぐ |
| `include/kern/cred.h`・`src/kern/cred.c` | `cred_ids_differ`・`cred_may_trace` |
| `src/kern/ptrace.c` | PT_ATTACH を `cred_may_trace` で絞る（EPERM） |
| `src/kern/exec.c` | commit で `process->set_id = secure`（2 つの経路）、trace されている process の exec は set-id を取らない |
| `src/kern/syscall.c` | set*id が ID を変えたら `set_id = 1` |
| `userland/base/common/account.c`・`.h`（新） | 上の U |
| `userland/base/passwd/`・`su/`・`sudo/`（新） | 上の P、Makefile は mode 4755 |
| `userland/base/etc/group` | kei を wheel に |
| `config/ci/config-amd64.mk` | passwd・su・sudo |

## 確認

| 確認 | 結果 |
| --- | --- |
| amd64 の vmunix（kernel include check を含む） | 成功、warning 0 |
| `build/q713/bin/passwd`・`su`・`sudo`（CI の config） | 成功、warning 0 |
| `sh plan/ws160/tests/run-host-cred-trace.sh` | 15 checks passed（superuser は誰でも、自分の普通の process だけ、root・他人・set-id・setuid root・saved や group の違い・tracer の混ざった ID は拒む、`cred_ids_differ`） |
| `sh plan/ws160/tests/run-host-account.sh`（host の libc と crypt、ASan・UBSan） | 32 checks passed（規則、hash を crypt が返す・別の password では違う・毎回別の salt、shadow の行の置き換え（kei だけ、keiko は別、最後の行、無い利用者、名の頭だけ一致、field の足りない行、colon の入った hash、小さすぎる出力）、環境（LD_PRELOAD・IFS・ENV が無い、secure PATH、目標の HOME、SUDO_*）） |
| style-check（新しい file、kernel の変えた hunk） | 指摘 0 |
| QEMU（`plan/ws160/tests/p001-guest.sh`、image は `plan/ws160/tests/config-amd64-accounts.mk`） | **未実施**。T1 に依頼（Q1 経由） |
| 実機 | **未実施**（release の image で UAT） |

## QEMU の試験（T1 への依頼）

- image: `plan/tools/guest/test-image.sh plan/ws160/tests/config-amd64-accounts.mk BUILD`（SSH guest＋passwd・su・sudo、追加の file 無し）。起動 `plan/tools/guest/guest.py start IMAGE` → `wait`。
- 試験: `plan/ws160/tests/p001-guest.sh [OUTDIR]`（root の SSH から `su kei`・`su tester` で利用者を替える。password は標準入力）。
- 合格: 全行 ok（setuid の 3 つ、sudo-root・sudo-wrong・sudo-env の 4 つ、passwd-root-sets、sudo-not-wheel、passwd-wrong-current・passwd-short・passwd-changes、shadow-mode・shadow-hash、su-new-password・su-old-password・su-root-locked、log の 3 つ。host に sshpass があれば ssh の 2 つ）。

## 人間の判断が要る点（既定で実装した）

| D | 問い | 既定（実装した） |
| --- | --- | --- |
| D1 | 新しい password の最短 | 8 文字（root が設定する時は 1 文字から）。今の `kei` は 3 文字なので、kei が自分で変える時は 8 文字以上 |
| D2 | sudo の規則 | sudoers の file 無し: root と wheel の member は全部できる |
| D3 | sudo の認証の記憶（一定時間聞かない） | 記憶しない（毎回聞く）。要るなら後で（tty と session ごと、5 分など） |
| D4 | wheel の gid が 0 | 今のまま（kei が group 0 の file を読めるようになる。/etc/shadow は 0400 なので読めない） |

- 2026-10-05 T1-118: 23 行 ok、`sudo-env-home` だけ FAIL。sudo の出力は `HOME=/root`・`USER=root`・`LOGNAME=root`・`SUDO_USER=kei` で、設計（U: 環境を作り直し HOME は目標の利用者の home、sudo の always_set_home と同じ）と試験の期待（`^HOME=/root$`）は一致していた。原因は試験の取り方: `-S` の prompt `[sudo] password for kei: `（改行なし、標準エラー）が guest の出力で `HOME=/root` と同じ行に付き、`^HOME=` に合わなかった。試験の `sudo -S env` の標準エラーを捨てるように直した（`plan/ws160/tests/p001-guest.sh`）。sudo は変えない。再試験は T1。

## 残り

- QEMU の結果の判定。
- docs/release の手引きの「Beta 1 has no way to change it」（password を変えられない）を、passwd・sudo ができたことに合わせて直す（WS129 の文書、Q1 へ）。
- p002（Settings の Users の頁）は上の S の経路で。

## Q1 の判定（2026-10-05）

T1-127 の p001-guest.sh PASS（試験の直し e777d3ba の後、全部の行）、T1-118 の他の 23 行 ok。**cleared**。D1〜D4 は案で実装、ユーザーの確認待ち（master）。
