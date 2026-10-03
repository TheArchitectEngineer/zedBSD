<!-- awesome-plan project=zedbsd record=ws127-p003 -->

# ws127-p003: 名前の衝突と Trash の残り（F-050・F-041 の一部）

Status: cleared（q666、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS127](../ws.md)
Queue: q666 / q666-i01（Q1 の dispatch、2026-10-04。user「任せます」→ Q1 の採否）
依存: p001 でユーザーが採用（2026-10-04 Q1 が委任で採用）
目安: 3h（1 Queue）。実行者の目安: phase-runner-mid
所有 path: `files/actions.c`・`task.c`・`trash.c`・`undo.c`・`clip.c`・`ui-overlay.c` の該当、`plan/ws127/tests/`

## 範囲

(1) Replace で置き換えた item を消さずに Trash へ（undo で戻せる）。(2) cut の paste を Esc で止めたとき clipboard を戻す。(3) folder の merge（中身を合わせ、子の衝突は同じ dialog）。(4) home の外の volume の `$topdir/.Trash-$uid`（freedesktop.org Trash）。

## 受け入れ

(1)〜(4) の host の model の試験と guest の手順が PASS、既存の衝突の dialog（ws035-p106）の試験 PASS。
変えた source の style-check の違反 0、build の warning 0、`files-regress.sh` の 14 本と boot test PASS。

## 範囲の照合（2026-10-04 P2、Q1 了承）

(1)〜(3) は既に実装・試験済みだった（F-050 は promoted 済み）: (1) Replace で置き換えた item は Trash へ・undo で戻る、(2) cut の paste の
衝突の問いを Esc で止めると clipboard を保つ は [ws035-p110](../../ws035/phase110/phase.md)（2026-09-28 cleared、QEMU の zdesktop-p110）、
(3) folder の merge は [ws035-p115](../../ws035/phase115/phase.md)。この Phase で実装したのは (4) だけ。

## 未決の判断

なし（採否は 2026-10-04 Q1 の委任の決定。folder の merge は ws035-p115 で入っている）。

## 実装（2026-10-04、q666-i01）

- `files/trash.c`・`ops.h`: freedesktop.org Trash の volume の trash。`fm_trash_for(item)` は item の file system が home の trash と違えば、
  item の volume の top（同じ st_dev を上へたどった最上の folder）の trash を選ぶ: sticky で link でない `$topdir/.Trash` があればその中の
  `$uid`（0700、本人の物）、無ければ `$topdir/.Trash-$uid`（作る、本人の物で link でないこと）。どちらも使えなければ home の trash（従来の copy）。
  `fm_trash_of(item)` は trash の files の中の item から trash を求める（home の trash か、名前が `.Trash-$uid`／`.Trash/$uid` の物だけ）。
  `fm_trash_list()` は home の trash と、mount の表（`mounts.h`）の各 volume の top にある trash を並べる（作らない）。volume の trash の
  記録の `Path=` は top からの相対で書き（volume を別の場所に mount しても戻せる）、読む時は相対なら top を前に付ける。
- `files/task.c`: Trash へ移す task は source ごとに `fm_trash_for`、Put Back は `fm_trash_of` で trash を求める（無ければ従来どおり
  destination）。Replace で置き換えた item も、その item の volume の trash へ。
- `files/dir.c`・`files.h`: `fm_dir_read_trash(listing)` は全ての trash の item を一つの一覧に（読めない trash は飛ばす）。`dir_read_into`
  を分けた。`files/ui.c`・`actions.c`: Trash の表示・Empty Trash は全ての trash、Trash の中の Delete は item ごとに `fm_trash_of` で記録を消す。
- 試験: `plan/tools/files/host-model.c` に 8b（volume の trash: 入る・相対の Path・読み戻し・一覧・`fm_trash_of`・Put Back）、2e（別の
  file system の Replace）の期待を「置き換えた item は自分の volume の trash」に直し、trash に入れた copy を片付ける。`host-model.sh` は
  /dev/shm が別の file system なら 2 つ目の folder として渡し、試験が作った volume の trash が空なら消す。guest の手順
  [files-p003.sh](../tests/files-p003.sh)（tmpfs を /tmp/vol に mount）。

## 確認（host。QEMU は T1 待ち、実機は未実施）

- `sh plan/tools/files/host-model.sh` → `files-model: PASS`（119 件 ok、8b の 7 件と 2e の 5 件を含む。/dev/shm の tmpfs と /tmp の tmpfs で）。
  `host-p009.sh`・`host-p010.sh`・`host-p013.sh`・`host-p014.sh`・`host-default.sh`・`host-png.sh` rc=0。
- zedBSD amd64 の build: `make -j16 ZEDBSD_CONFIG=plan/tools/files/config-amd64-files.mk BUILD=build/p2-files build/p2-files/bin/files` rc=0、
  warning 0（`-Werror`）。Linux・FreeBSD の build は未実施（trash.c は POSIX と `mounts.h` だけ、両 Makefile に mounts の実装がある）。
- `plan/tools/style-check.py` の違反: trash.c・task.c・dir.c・actions.c・ui.c とも 0。`git diff --check` 0。
- Settings と共有の `canvas.c`・`text.c`・`icons.c` は変えていない（Settings の host 試験は不要）。
- 未実施（T1 へ）: guest の `files-p003.sh`、回帰 `files-regress.sh`（p004 の Trash を含む）、boot test。

## 残り・制限

- `/tmp` のような揮発の volume でもその top の trash を使う（GIO は system の内部の mount を除く）。zedBSD の volume の一覧に system の mount を
  除く規則は places.c にあるが、trash の選択には使っていない。
- F-041 の行（plan/future-work.md）の `$topdir/.Trash-$uid` の disposition の更新は Q1 へ。

## 検証の方法と範囲

QEMU の Venus（`plan/tools/files/files-guest.sh`、`files-regress.sh`）と host の試験。console・serial の log では判定しない。各 Phase の最後に `files-regress.sh` の 14 本と boot test。
Settings と共有の `canvas.c`・`text.c`・`icons.c` を変えたら `sh plan/ws089/tests/host-build.sh` と Settings の host 試験も流す。

## Event

2026-10-02 / ws127-beta1-plan: fg019 の計画で新設（planning）。p001 の結果とユーザーの選択で範囲を確定し planned にする。

## 判定（Q1、2026-10-04）

cleared。T1-077：files-p003・files-regress 14 本・boot-test PASS（QEMU）。実機は未実施
