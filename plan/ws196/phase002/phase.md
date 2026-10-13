<!-- awesome-plan project=zedbsd record=ws196-p002 -->
# ws196-p002: account-admin の欠陥の直しと、root の前段の request の口

Status: in-progress（2026-10-14 P2 q969: 詳細設計、実装中）
Disposition: normal
Parent: [WS196](../ws.md)
Queue: q969
依存: [p001](../phase001/phase.md)（2026-10-14 の決定と [review-1](../phase001/review-1.md)）

## 目的

useradd・usermod・userdel（p003）が呼ぶ account-admin を、端末の root からも安全に使える形にし、Settings からも同じに効く既存の欠陥を直す。
構成は p001 の決定 ①: 編集・保護・lock は account-admin だけが持つ。common/ への切り出し（7d632a555）は使わない。

## 範囲（p001 の決定と review-1 の番号）

1. 呼び手が root（実の uid 0）の時は wheel と password の確かめを飛ばす口。前段が渡す request の行の形を決める（usermod の操作: shell・comment・lock・unlock・primary group・groups の置き換えと追加、§2 から `-d`・`-m` を除く）。syslog には SUDO_USER も残す（L3）。
2. S1・M8: 最後の管理者は使える管理者だけ数える（hash が使える・期限が切れていない・shell が login できる。`machine/users.c` の判定と揃える）。
3. S2: 最後の使える管理者を無くす全ての変更（削除・lock・nologin の shell・primary group・`-a` 無しの groups）を拒む。
4. S3: uid は使い回さない（既に使われた最大より上。/home の持ち主と cron・at の spool も見る）。userdel は cron・at の spool と autologin の設定を掃除する。
5. S4: account の file の lock を process の死で解ける形にし（fcntl/flock か持ち主の確かめ）、home の削除と skel の複写は commit と unlock の後。common/account.c の lock を変えるので passwd・su・sudo・passkey・passkey-fido2 にも効く。Linux・FreeBSD の passkey の build を確かめる。
6. S5: 3 つの file の途中の失敗は lock の下で元の文に戻す。userdel は passwd の行が無くても名前の group・shadow・passkey の残りを掃除できる。
7. S6: tree の削除は st_dev の違う entry に降りない（log に残す）。
8. M7: 他の人の primary group の group は消さない。
9. ⑥（M2）: sessiond の autologin は shadow を見て、lock・期限切れの人は autologin せず greeter を出す。
10. F2 の guard（home が `/home/NAME` の時だけ消す）は保つ。⑦ root の `passwd NAME` は passkey の行を消さない（変えない）、`passwd NAME` が暗に lock を解くことを docs に書く。
11. `docs/architecture/security.md` の「Account administration」を直す（L4）。

## 確かめ

- 実装を自分で review、amd64 の build（warning 0）、`plan/ws089/tests/run-host-account-admin.sh`・`plan/ws158/tests/run-host-admin-language.sh` に新しい場合（数え方・uid・M7・戻し）を足して PASS。Linux・FreeBSD の passkey の build。
- QEMU は p004 で T1（`plan/ws089/tests/admin-p026-guest.sh` を含む）。

## 詳細設計（2026-10-14 P2、q969）

構成は p001 の決定 ①: 編集・保護・lock は account-admin だけ。common/ への切り出し（7d632a555）は使わず、branch は main（d7e38eeae）から始め直した。
純粋な部分は `edit.c`・`edit.h`（host 試験）、file と方針は `main.c`、lock は `common/account.c`、autologin は `sessiond/main.c`。

### D1. 呼び手と request の行の形（範囲 1）

- 呼び手は実の uid で分ける。**0 以外**（Settings の backend）は今までの形（1 行目が呼び手の password、2 行目が操作、引数が 1 行ずつ）と今までの 6 つの操作だけ。wheel と password の確かめも今まで通り。
- **実の uid 0**（root の前段: p003 の useradd・usermod・userdel、sudo 経由を含む）は password の行が無く、wheel と password の確かめを飛ばす。root の形では root の 3 つの操作だけを受ける（Settings の 6 つの操作は受けない。root でない呼び手は root の操作を受けない）。
- root の形: 1 行目が操作、続く行は `KEY VALUE`（最初の空白 1 つで分け、値は行の終わりまで、空も可）。同じ key の 2 回目、知らない key、空白の無い行は `bad-request`。値の規則に反すれば `bad-value`。

| 操作 | key（* は必須） | 既定・意味（p003 の option） |
| --- | --- | --- |
| `user-add` | `name`*、`comment`、`shell`、`uid`、`group`、`groups`、`home` | useradd。`home make`（既定、`-m`）か `home none`（`-M`）。password は lock（shadow の hash は `!`、E5）。home は常に `/home/NAME`（⑤） |
| `user-modify` | `name`*、`comment`、`shell`、`group`、`groups`（置き換え、`-G`）か `groups-add`（`-a -G`）、`password lock`（`-L`）か `password unlock`（`-U`） | usermod。変更の key が 1 つも無ければ `bad-request`。`groups` と `groups-add` の両方も `bad-request` |
| `user-remove` | `name`*、`home`（`keep` 既定・`remove`（`-r`））、`force yes`（`-f`） | userdel。`home remove` と `force yes` の同時は `bad-request`（S6） |

- 値の規則（S8・④）: `comment` は今の表示名の規則（印字できる、`:` 無し、64 byte まで）。`shell` は絶対 path、`:`・制御文字無し、255 byte まで、存在して実行できる通常の file（main.c で確かめる）。`uid` は 1000〜59999 の 10 進で、passwd の uid と group の gid のどちらにも使われていない値（使われていれば `id-taken`）。`group`（`-g`）は `wheel`・`network`・その人の名前（private group）だけ。`groups`・`groups-add` は `,` で区切った `wheel`・`network` だけ（`groups` の空は全ての group から外す）。
- `groups` の置き換えは shadow-utils と同じく、全ての group の member の list から名前を外してから、挙げた group に入れる。
- private group は `group` の指定にかかわらず常に作る（gid = uid）。後で `usermod -g NAME` で戻せるように。
- 新しい答えの語（`edit_reasons` の末尾に足し、今の番号は変えない）: `bad-value`、`id-taken`、`no-password`（F1）、`home-failed`（account は作った、home を作れなかった。shadow-utils の 12 に当たる）。Settings の page は知らない語を一般の失敗として示す（page-users-admin.c の表は変えない）。
- syslog（auth）: root の行は `OPERATION by root (sudo USER) for NAME: REASON`。`SUDO_USER` は実の uid が 0 の時だけ、clearenv の前に読み、名前の規則（印字できる 32 byte まで、`:` 無し）に合わない時は `?`（偽れるので記録だけ、M4: self の確かめには使わない）。root の形でも umask 022、標準の descriptor の確かめ、止める signal の保留は今まで通り（L3）。
- M4: root の操作には self の確かめが無い。守りは busy と最後の使える管理者だけ。Settings の操作の self の確かめは保つ。

### D2. 使える管理者の数え方と、最後の管理者の守り（範囲 2・3、S1・S2・M8・②）

- 使える管理者 = passwd の行で uid が 1000〜59999、wheel に居る（primary group か member）、shell が login できる（空、または `nologin`・`/false` を含まない。`machine/users.c` の users_person と同じ）、shadow の行があり hash が空でも `!`・`*` で始まってもいない、期限（shadow の 8 番目の field）が空か 0 か今日より後（`passkey/main.c` の passkey_usable と同じ）。期限の field が数でなければ使えないと数える（少なく数える側に倒す）。
- 守り方: 全ての操作は 3 つの file の新しい文を memory で全部作ってから、**前の文で 1 人以上・新しい文で 0 人**なら `last-administrator` で断る。削除・lock・nologin の shell・primary group・`-a` 無しの groups・Settings の wheel 外しの全部を同じ 1 つの確かめで覆う。前から 0 人の機械では、増やす操作も含め断らない（悪くならない）。
- この確かめは副作用（`/etc/passkey` の掃除、uid の記録、file の書き込み）より前に行う。

### D3. uid の選び方と掃除（範囲 4、S3・③）

- 新しい uid = 次の全部の最大 + 1（59999 を超えたら `failed`）: 999、passwd の uid と group の gid（1000〜59999 の物）、記録 `/etc/account-admin.last-id`（使った最大の uid、1 行の 10 進、mode 0644、lock の下で atomic に書く）、`/home` の直下の entry の持ち主（link は辿らない）、`/var/spool/cron` の数の名前、`/var/spool/at` の file の持ち主。
- 記録は account の file より先に書く（途中で落ちても同じ uid を二度出さない）。`uid` を指定した追加も、記録より大きければ記録を上げる。
- 削除（Settings・root とも）は commit と unlock の後に: `/var/spool/cron/UID` を消す、`/var/spool/at` の中でその uid が持つ通常の file を消す（link は辿らない）、`/etc/keiland/autologin` の 1 行目がその名前ならその file を消す。

### D4. lock（範囲 5、S4）

- `account_files_lock` は `ACCOUNT_SHADOW_LOCK` を `O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC`（0600）で開き、`flock(LOCK_EX|LOCK_NB)` を 100 ms おきに 50 回（5 秒）試す。持っている間 descriptor を file-scope の変数に持ち、`account_files_unlock` は close するだけ（lock の file は消さない。消すと、待っていた人と新しく作った人が別の inode で同時に持てる）。process が死ねば kernel が解く（zedBSD の `record_lock_release_file`、Linux・FreeBSD も同じ）。mtime による横取りは無くす。
- 共有者: passwd・su・sudo（`account_shadow_set`）、account-admin、passkey、passkey-fido2、sessiond（account.c を link する）。呼び方は変わらない（関数の形は同じ）。
- account-admin は lock の下で検べ・編集・`/etc/passkey`・記録・3 つの file の書き込みだけを行い、home の作成と skel の複写、home の削除、spool と autologin の掃除は unlock の後（D3・D6）。

### D5. 途中の失敗の戻し（範囲 6、S5）

- 読んだ元の文を memory に保ち、新しい文は別の buffer で作る。変わった file だけを順に書く（追加: group → passwd → shadow、削除と変更: shadow → passwd → group）。書けなかったら、既に書いた file を逆順に元の文で書き戻し（失敗は log）、`failed`。SIGKILL・電源断は戻せない（残りは下の掃除で取る）。
- 削除で passwd の行が無い時（残り）: 名前が新しい名前の規則に合えば、shadow の行、名前の group（下の M7 の条件を満たす物）、全ての member の list の名前、`/etc/passkey` の行を掃除して `ok`。どれも無ければ `no-such-user`。

### D6. home の削除（範囲 7・10、S6・F2）

- 消すのは passwd の home が `/home/NAME` ちょうどの時だけ（F2 の guard、保つ）。加えて、その directory の持ち主がその人の uid でなければ消さずに log（`home none` で作った人の、前からある directory を守る）。
- tree の削除は home の `st_dev` と違う entry（mount の境）に降りず、残して log に書く（その上の directory は空にならないので消えない）。link は今まで通り辿らない。
- `-r` と `-f` の同時は D1 で断る。

### D7. 他の人の primary group（範囲 8、M7）

- 名前の group を消すのは、gid が 1000〜59999、gid がその人の uid か primary の gid（残りの掃除では問わない）、削除の後の passwd でその gid を primary にする人が居ない、member の list が空かその名前だけ、の全部の時だけ。それ以外は残して log。

### D8. autologin（範囲 9、M2・⑥）

- sessiond の `main_autologin` は passwd の後に `getspnam_r` で shadow を見て、行が無い・hash が `!`・`*` で始まる・期限が切れている（`sp_expire > 0` かつ今日 ≥ `sp_expire`）なら autologin せず greeter を出す（`SESSIOND AUTOLOGIN refused account=NAME locked|expired|no-shadow` を log）。空の hash は lock ではないので今まで通り。

### D9. passwd と docs（範囲 10・11、⑦・L4・L7）

- root の `passwd NAME` は変えない（`/etc/passkey` の行を消さない、hash の field を丸ごと置き換えるので lock も解く）。これを docs に仕様として書く。
- `docs/architecture/security.md` の「Account administration」に root の形・新しい語・数え方・uid・lock・戻し・home・autologin を書き、「Shared account core」の lock の記述を flock に直す。

### 人の判断が要る点

- 無し（全て p001 の決定と review の修正案の範囲の技術判断）。次の 3 つは技術判断として選んだので Q1 に報告する: private group を `-g` にかかわらず常に作る（D1）、uid の記録 file `/etc/account-admin.last-id`（D3）、home の削除に持ち主の確かめを足した（D6）。
