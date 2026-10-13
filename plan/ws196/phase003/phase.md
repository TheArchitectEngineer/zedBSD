<!-- awesome-plan project=zedbsd record=ws196-p003 -->
# ws196-p003: useradd・usermod・userdel（account-admin を呼ぶ root の前段）

Status: cleared（2026-10-14 Q1: host 試験 148 項目 PASS、T1-603 の user-commands-guest PASS（QEMU）。実機は未実施）
Disposition: normal
Parent: [WS196](../ws.md)
Queue: q972
依存: [p002](../phase002/phase.md)（root の request の形 D1、merge 済み 7f2c850a7）

## 範囲

- `userland/base/useradd`・`usermod`・`userdel`（または 1 つの source の 3 つの名前）。root（実の uid 0）でなければ EPERM 相当で終わる。option を解析して p002 D1 の root の形の request を account-admin に渡し、答えの語を shadow-utils の終わりの状態と message に写す。
- option は [p001](../phase001/phase.md) §2 から `-d`・`-m`（usermod の home の移動）を除く（2026-10-14 ⑤）。useradd の `-m`・`-M` は home の作る・作らない。`-u`・`-g`・`-G` の範囲は p002 D1（④）。`usermod -U` の `!` だけの hash は `no-password`（F1）。
- install は `/sbin`（mode 0755、setuid にしない）、menu は Base、表示名は命令の名前。man（`docs` か package の man の置き場の慣習に合わせる）。
- `plan/coding-style.md` の全文を適用。

## 確かめ

- amd64 の build warning 0。host 試験: p002 の `plan/ws196/tests/account-admin-root-host-test.sh` の形で、前段の 3 つの命令の option の解析と終わりの状態（正常・使い方の誤り・last-administrator・id-taken・no-password・bad-value）。
- QEMU は p004 で T1（依頼の文を phase.md に。p002 の依頼と 1 つにまとめる）。

## 詳細設計（2026-10-14 P2、q972）

- 置き場: 3 つの命令は `userland/base/useradd`・`usermod`・`userdel`（それぞれ `main.c` と `Makefile`）、共有の前段は `userland/base/user-control/front.c`・`front.h`（Makefile の無い source の置き場、`shutdown-control` と同じ形）。各 main は option を読み、`front.c` が root の確かめ・request の組み立て・account-admin の実行・答えの写しをする。
- 呼び手: 実の uid が 0 でなければ `PROGRAM: Permission denied.` で 1（shadow-utils と同じ文）。option の解析より先。setuid にしない（mode 0755、`/sbin`）。
- request: p002 D1 の root の形。値は与えられたまま渡し、規則の判定は account-admin だけ（前段は値の規則を重ねない）。前段が断るのは形を壊す物だけ: 値の中の改行（別の key に読まれる）と、4096 byte（account-admin の上限）を超える request（どちらも 3）。同じ文字の option の 2 回目は前の値を置き換える（getopt の慣習）。
- option と key: useradd `-c`→`comment`、`-g`→`group`、`-G`→`groups`、`-s`→`shell`、`-u`→`uid`、`-m`/`-M`→`home make`/`home none`（常に送る、既定 make）。usermod `-c`・`-g`・`-s` は同じ、`-G`→`groups`（`-a` 付きは `groups-add`）、`-L`/`-U`→`password lock`/`unlock`。userdel `-r`→`home remove`、`-f`→`force yes`。
- 使い方の誤り（2）: 知らない option（`-d`・`-p`・`-u`（usermod）・`-l` など）、名前が 0 個・2 個以上・空、useradd の `-m` と `-M` の同時、usermod の `-a` だけ・`-L` と `-U` の同時・変更の option が 1 つも無い、userdel の `-r` と `-f` の同時（S6）。
- 実行: pipe 2 本と fork、子は標準入力と出力を pipe にして `execv(ACCOUNT_ADMIN_PATH)`（`common/account.h` の `/usr/libexec/account-admin`、host 試験は -D で差し替え）。環境はそのまま渡す（account-admin が `SUDO_USER` を log に書く）。SIGPIPE は前段では無視、子では既定に戻す。答えの 1 行を読み、子を待つ。
- 答えと終わりの状態（shadow-utils の番号）: ok 0、bad-request 2、bad-value・bad-name 3、id-taken 4、no-such-user 6、busy 8、name-taken 9、home-exists・home-failed 12、root・last-administrator・no-password・failed と知らない語 1、答えが無い（account-admin が動かない・止められた）1。成功は無言、拒否は `PROGRAM: NAME: 理由の文` を標準エラーに。
- man: zedBSD の base に man の仕組みが無いので、`docs/reference/user-administration.md`（3 つの命令の書式・option・保護・終わりの状態・例）を man に当たる参照にし、`docs/reference/README.md` の System administration と `docs/architecture/security.md` の Account administration から link。

## 実装（2026-10-14 P2）

| file | 内容 |
| --- | --- |
| `userland/base/user-control/front.h`・`front.c`（新） | `user_front_start`（標準の descriptor と root の確かめ）、`user_front_begin`・`user_front_key`（request の組み立て、改行と長さの拒否）、`user_front_send`（account-admin の実行と答えの写し、表 `front_reasons`） |
| `userland/base/useradd/main.c`・`Makefile`（新） | useradd（`c:g:G:mMs:u:`） |
| `userland/base/usermod/main.c`・`Makefile`（新） | usermod（`ac:g:G:s:LU`） |
| `userland/base/userdel/main.c`・`Makefile`（新） | userdel（`rf`） |
| `docs/reference/user-administration.md`（新）、`docs/reference/README.md`、`docs/architecture/security.md` | 命令の参照（man に当たる）と link |
| `plan/ws196/tests/user-commands-host-test.sh`・`user-commands-run.sh`（新） | host 試験（下） |
| `plan/ws196/tests/config-amd64-users.mk`・`user-commands-guest.sh`（新） | T1 の image と guest の試験（p004） |

Makefile は 3 つとも `ZEDBSD_USERLAND_PACKAGE,NAME,NAME,*,y,basic,…,y,base,,,0755,program,,,sbin`（menu Base、表示名は命令の名前、`/sbin`、0755）。`config/ci/*.mk` の `ZEDBSD_USER_PROGRAMS` に `useradd usermod userdel` を足すのは Q1 が merge の時に（担当の範囲の外）。

## 確かめ（2026-10-14 P2、host = Debian 13）

| コマンド | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws196-p003/zed "ZEDBSD_USER_PROGRAMS=useradd usermod userdel account-admin" build/ws196-p003/zed/bin/{useradd,usermod,userdel}` | rc 0、`-Wall -Wextra -Werror`、warning 0（log `build/ws196-p003/build-zed.log`） |
| `make -n ZEDBSD_CONFIG=plan/ws196/tests/config-amd64-users.mk …/bin/userdel` | 試験の config で userdel が選ばれる |
| `sh plan/ws196/tests/user-commands-host-test.sh` | PASS（148 checks、ASan・UBSan の報告 0）。代わりの account-admin（request を保存し決めた答えを返す shell）で: 各 option の組の request の文の完全一致、`SUDO_USER` の受け渡し、使い方の誤り 15 件（account-admin を呼ばない）、改行と長すぎる値（3、呼ばない）、全ての答えの語の終わりの状態と message と、知らない語・答えの無い時（1）。本物の account-admin（p002 の試験と同じ build、全ての path を新しい folder へ）で: useradd（行・wheel・`!`・skel）、name-taken 9、id-taken 4、uid 99・知らない group・他人の group・無い shell・`Bad`（3）、`-M -u -g -s`、home-exists 12 と `-M`、usermod の no-password 1・last-administrator（`-L`・`-s nologin`・`-G network`）1、`-c`・`-a -G`・`-G ''`・`-g`・`-L`/`-U`（hash が元に戻る）、無い人 6、system の account 1、userdel の無い人 6・system 1・last-administrator 1・`-r`（home が消える）・`-f`・home を残す既定、消した人の uid を使い回さない。root でない呼び手は `useradd: Permission denied.` で 1 |
| `python3 plan/tools/style-check.py`（4 つの C） | 0 件 |
| `git diff --check` | 0 件 |

## 未実施・残り

- QEMU（guest）は p004 で T1（[phase004](../phase004/phase.md) の依頼。p002 の依頼と 1 つにまとめた）。`busy`（8）は host では作れない（account-admin は `/dev/system` で process を見る）ので guest で確かめる。
- 実機は未実施。
- Q1 への依頼: `config/ci/*.mk` に `useradd usermod userdel` を足す。`plan/ws196/tests/user-commands-host-test.sh` を master.md の Tools・試験の一覧に登録する（account-admin の前段の回帰）。host 試験の `build/tmp/ws196-user-commands.*` は root の file を含むので片付けに sudo が要る。
