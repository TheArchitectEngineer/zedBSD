# ws196-p001 review-1（phase.md の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer。Q1 が報告をそのまま写した）

## ws196-p001（useradd・usermod・userdel）の敵対的 review

対象: /home/awe/zedBSD-claude1/plan/ws196/phase001/phase.md（以下 phase.md）。ソースと設計文書は変更していません。実行したのは読むこと（cat・grep・sed）だけで、build・試験・QEMU は動かしていません。

### 判定
- **p003（CLI の実装）は、まだ進めないでください。** 下の致命的 2 件と重大 S1〜S8 を設計に書き足すことが前提です。S1・S3・S8 は account-admin の振る舞いや方針にも関わるので、ユーザーの判断が要ります。
- **p002（共有の層の切り出し）は条件つきで可です。** 条件は次の 3 つです。
  1. M1 を直す（E4 の前提が無くなったことと、cancel 済みの依存）。
  2. E4 をユーザーに確かめ直す。
  3. 検証を M5 の形に広げる。
- S4（lock の作り直し）を共有の層に入れるなら、p002 の範囲が変わります。

---

### 致命的

**F1. `usermod -U` で password の無い account ができる**
- 根拠:
  - phase.md:25 と E5（phase.md:52）で、useradd は shadow を `!` だけで作る。
  - phase.md:26 は「`-U` は hash の前の `!` の付け外し」とする。
  - `userland/base/login/verify.c:90-96` は、空の hash に空の password での login を許す。
- 起こる状況: `useradd bob; usermod -U bob` で bob の hash が空になり、console の login と su が password なしで通る。passkey_usable（`passkey/main.c:361`）は空の hash を拒否するが、verify.c は拒否しない。
- 修正案: hash が `!` だけ（または `!` の後が空か `*`）のときは `-U` を拒否する（shadow-utils と同じ）。試験にもこの場合を入れる。

**F2. 任意の path の `-d` と `userdel -r` が組み合わさると、任意の tree を消せる**
- 根拠:
  - phase.md:25-27 は `useradd -d HOME`・`usermod -d HOME` と「`-r` で home を消す」を並べるだけで、消す範囲の制約を書いていない。
  - 今の account-admin は passwd の home が `/home/NAME` と一致する時だけ消す（`account-admin/main.c:668-682`）。この guard を引き継ぐことが設計に無い。
- 起こる状況: `usermod -d / x; userdel -r x` で、根から順に削除される。
- 修正案: 次を設計に明記する。
  - `-r` が消すのは `/home` の直下で、名前が NAME、持ち主が対象の uid の directory だけにする。
  - それ以外の home では警告して消さない。
  - `-d` は絶対 path で、`:` と改行を含まず、長さは ADMIN_PATH_MAX 未満とする（`main.c:475` は長い home を黙って空にする）。

### 重大

**S1. 「最後の管理者」の数え方が、login できない管理者も数える。E5 と組み合わさると管理できない機械になる**
- 根拠:
  - `edit.c:676-740`（admin_administrators）は、uid が 1000 以上で wheel に居る行を全部数える。shadow の `!`・`*`・空、shadow の行が無い、nologin の shell は区別しない。
  - root は lock されている（phase.md:19、U10）。
- 起こる状況（root は lock 済みなので、誰も直せない）:
  1. `sudo useradd -G wheel alice`（E5 で `!`）
  2. 続けて `sudo usermod -G network bob`、または bob の `userdel`
  3. alice を管理者に数えるので通り、password を持つ管理者が居なくなる。
- 修正案: 次の全部を満たす人だけを数える。
  - hash が使える（`!`・`*`・空でない）
  - 期限が切れていない
  - shell が login できる（`machine/users.c:248-275` と同じ判定）

  account-admin の数え方も変わるので、p002 の「振る舞いは変えない」とぶつかります。ユーザーの判断が要ります。

**S2. E1 の対象が `-L`・`-s`・`-g`・`-a` 無しの `-G` を含まない**
- 根拠: phase.md:48 の E1 は「消さない・wheel から外さない」だけ。phase.md:26 の usermod には次の経路があり、どれも最後の管理者を無力にする。
  - `-L`（lock）
  - `-s /sbin/nologin`
  - `-g`（wheel は gid 0。primary group で wheel に居る人を `-g` で外せる。`etc/group:1`、`account.c:368`）
  - `-a` 無しの `-G`（list を置き換えるので、wheel が暗に外れる）
- 修正案: E1 を「最後の使える管理者を無くす全ての変更を拒否する」と書き、上の 4 つを列挙して試験に入れる。

**S3. uid を一番小さい空きから使い回すので、消した人の物を新しい人が受け継ぐ**
- 根拠:
  - `edit.c:378-405`（admin_free_id）は一番小さい空きの uid を返す。
  - cron の spool は uid で引く（`cron/main.c:94-101`、`/var/spool/cron/<uid>`、at も同様）。
  - `userdel -f` は process を残す（phase.md:27）。
  - 削除で残した home（keep-home）を持つのも、その uid。
- 起こる状況: `userdel -f bob`（uid 1001）の後に `useradd carol` で carol が 1001 になり、次を受け継ぐ。
  - bob の crontab の job
  - bob の残った process（signal・ptrace ができる）
  - bob の残った home と /tmp の file

  今の account-admin にもある欠陥で、useradd がそれを引き継ぎ、`-f` が広げます。
- 修正案: 次のどちらかを採り、ユーザーに確かめる。
  - 最大の uid + 1 にする。
  - 使い回さない（`/home` の下の持ち主と、cron・at の spool を見る）。

  加えて、`userdel` で cron・at の spool を掃除し、`-f` は process を止めるか拒否する（E2 の見直し）。

**S4. lock が長い作業で横取りされ、更新が失われる。他の道具は 2 秒で EBUSY になる**
- 根拠:
  - lock は O_EXCL の lock file で、mtime が 60 秒を過ぎると他者が消して取る（`common/account.c:487-493`）。
  - unlock は持ち主を確かめずに unlink する（`account.c:505-508`）。
  - 待つのは 20×100ms だけ（`account.c:58-59`、`475-500`）。
  - account-admin は home の tree の削除を lock の中で行う（`main.c:337-372`、`668-682`）。
- 起こる状況:
  1. `userdel -r` で大きな home を 60 秒以上かけて消す。
  2. その間に Settings（account-admin）が lock を取り、group → passwd と順に書く。
  3. userdel の unlock が account-admin の lock を消す。
  4. 3 番目の書き手が途中の状態を読み、行が失われる。

  60 秒より短くても、passwd・passkey・passkey-fido2（`passkey/main.c:593,709`、`passkey-fido2/main.c:821,1464`）が 2 秒で EBUSY になる。
- 修正案:
  - home の削除と skel の複写は、file を commit して unlock した後に行う。
  - lock は process の死で解ける形（fcntl/flock）にするか、持ち主の確認と mtime の更新を入れる。

  phase.md:18 の共有者の列挙からは passkey と passkey-fido2 が抜けています。

**S5. 3 つの file の途中の失敗を戻さない。残った group は E3（groupdel 無し）では消せない**
- 根拠:
  - 追加は group → passwd → shadow → home、削除は shadow → passwd → group の順に、1 つずつ置き換える（`main.c:551-583`、`637-666`）。前の file は戻さない。
  - phase.md:15 の「取引」という表現は、原子性を過大に言っている。
- 起こる状況:
  - passwd の書き込みが失敗する（ENOSPC など）か、SIGKILL や電源断が起きると、private group と wheel の member の行だけが残る。
  - 同じ名前の useradd は NAME_TAKEN になり（`main.c:521-523`）、userdel は NO_SUCH_USER になるので、vi で直すしかない。
  - passwd まで書いて shadow が無い account は、S1 の数え方で管理者に数えられる。
- 修正案:
  - 失敗したら、lock の下で、memory に残っている元の文で戻す。
  - userdel は passwd の行が無くても、名前の group・shadow・passkey の残りを掃除できるようにする。
  - home を作れなかった時の終わりの状態（shadow-utils の 12 相当）を決める。

**S6. `userdel -r` が mount の境を越え、`-f` の時は走っている process と競る**
- 根拠: `main.c:1116-1184`（admin_remove_tree）は st_dev を見ない。
- 起こる状況:
  - home の下に mount した物（USB・bind）の中身まで消える。mount の実例は未確認で、推測です。
  - `-r -f` では、本人の process が削除の間も file を作り続ける。
- 修正案:
  - home の st_dev と違う entry は降りずに残し、log に書く。
  - `-r` と `-f` の同時指定は拒否する。

**S7. `usermod -d HOME -m`（home の移動）の意味が決まっていない**
- 根拠: phase.md:26。
- 欠けている点:
  - 別の file system への移動（EXDEV なら copy と delete になる）
  - 移動先が既にある時
  - 本人が login 中の時
  - 本人が持つ tree を root が辿る時の symlink の競合
- 修正案: rename だけにする（`/home` の fd に対する renameat、EXDEV は拒否、busy なら拒否）か、`-m` を範囲から外す（E2 の見直し）。

**S8. `-u`・`-g`・`-G` の値の範囲が書かれていない**
- 根拠: phase.md:25-26。E6 は `-r` を外すだけ。
- 起こる状況:
  - `useradd -u 0 x` で root と同じ権限の account ができる。
  - `-u 70000` は E1 の範囲（1000〜59999）の外になる。
  - `-u` が既にある gid と重なると private group と衝突する。
  - `-g wheel` で管理者になる。
  - `-G _passkey,_greeter` で system の group に入る。account-admin は wheel と network だけに限っている（`edit.c:184-190`）。
- 修正案:
  - `-u` は 1000〜59999 で、uid・gid のどちらとも重ならない値に限る。
  - `-g`・`-G` で許す group を決める。方針なのでユーザーの判断が要ります。
  - `-g` を付けた時に private group を作るかどうかも書く。

### 中

**M1. E4 の前提が無くなった。依存の記録と Target も古い**
- phase.md:51 の推しの理由「base の道具が Keiland の tree の source に依存しない（WS195 で account-admin が Keiland に移るため）」は、WS195 の cancel（`plan/ws195/ws.md` の「結果」）で成り立たなくなりました。account-admin は base に残るので、(b)（account-admin の source を compile する）にも境界の問題はありません。
- そのほか古い記述:
  - phase.md:8 と `ws.md:31` の依存「WS195 p001 の D7」は、cancel 済みの Phase への依存で、このままでは選べない。
  - phase.md:41-42 は Keiland の tree への移動を前提にしている。
  - phase.md:7・55 は「ベータ3 以降」と書くが、`ws.md:12` の Target はベータ2。
- (a) にも、common/ が base の共有の置き場であるという利点は残ります。ただし、ベータ2 の時期に setuid の 2300 行を切り出す危険と比べる必要があります。
- 対応: ユーザーは誤った前提で (a) を選んだので、E4 は確かめ直しが要ります。依存の行は消します。

**M2. autologin が lock を見ず、userdel でも消されない**
- 根拠: `sessiond/main.c:546-585` は uid 0 だけを拒否し、shadow を見ない。
- 起こる状況:
  - `usermod -L` しても autologin で入れる。
  - 消した人と同じ名前を作り直すと、autologin で入る。
- 対応: 他の WS の変更になるので、Q1 に報告して判断を仰ぐ。

**M3. E5 の流れで使う root の `passwd NAME` は、passkey の行を消さない**
- 根拠:
  - account-admin の reset は PIN と key を消す（`main.c:718-721`）。
  - root の passwd は消さず、password も 1 文字から許す（`account.c:91`）。
- 起こる状況: 端末で `usermod -L` してから `passwd NAME` で回復しても、古い PIN と key が使える。
- 対応: 揃えるかどうかは方針なので、ユーザーの判断が要ります。

**M4. root から使う時の「自分自身」が決まっていない**
- phase.md:16 は account-admin に self の拒否があると書くが、E1（phase.md:48）には無い。
- sudo の下では「自分」は SUDO_USER だけで分かり、これは偽装できる。
- 対応: self の確認は持たず、busy と最後の管理者だけで守る、と明記する。

**M5. p002 の検証が、移す code の大半を覆わない**
- `run-host-account-admin.sh` は edit.c だけを compile する（`plan/ws089/tests/run-host-account-admin.sh`）。
- main.c から移す lock・home・tree の削除・passkey・busy は、p004 まで試験されない。
- 対応:
  - p002 の後に、T1 で `plan/ws089/tests/admin-p026-guest.sh` を流す。
  - common/account.c に触れるなら、Linux と FreeBSD の passkey の build も確かめる（`passkey/Makefile.linux`・`Makefile.freebsd` が compile する）。phase.md の「影響は無いはず」は、まだ確かめられていません。

**M6. account-change.c の層と、host 試験ができるかどうか**
- account-change.c を common/ に置くと、次に依存する。
  - `passkey/record.c`（common が passkey に依存する逆向き）
  - `<uapi/system.h>` と `/dev/system` の ioctl、utmpx（`main.c:934-979`）
- その結果、p003 の「tmp の root での host 試験」は Linux の host で build できない。fchown（`main.c:1016`）も root でないと失敗する。
- 対応: busy の判定と chown を差し替えられる形にするか、host 試験の範囲を明記する。

**M7. userdel が、他の人の primary group に使われている group を消す**
- 根拠: `main.c:651-659`。
- 起こる状況: `usermod -g alice bob` の後に alice を消すと、bob の gid が宙に浮く。
- 対応: 拒否する。

**M8. Settings の一覧と E1 の数え方の定義が食い違う**
- Settings は nologin の人を一覧に出さない（`users.c:259-273`）が、管理者の数には入る。
- S1 と合わせて、定義を揃える。

### 軽微
- L1. 終わりの状態の番号を「p002 の詳細設計で」としている（phase.md:30）が、CLI の実装は p003。
- L2. skel は直下の通常の file だけを写す（`main.c:1031-1109`）。Linux の再帰の複写と違うので、明記する。
- L3. root が手で打つ道具なので、umask の固定と標準の descriptor の確認（`main.c:1224-1243` 相当）を引き継ぐ。syslog には SUDO_USER も残す。
- L4. `docs/architecture/security.md`（「Account administration」と「root administers with the base tools」）を直すことが、Phase の案に無い。
- L5. host 試験を「一緒に移す」（phase.md:37）なら、2026-10-06 の方針に従い master.md の Tools に登録する。WS の中に置くと、WS の完了で消えます。
- L6. admin_busy は process を 4096 個までしか見ない（`main.c:938`）。それより多いと、居るのに居ないと判定する。
- L7. phase.md §6 の未確認点への答え: passwd に `-l`・`-u` は無く、root が password を付けると hash の field を丸ごと置き換える（`account.c:253-275`）。つまり `passwd NAME` は暗に lock を解きます。仕様として明記してください。
- L8. HAL・UAPI・kernel の変更は要らない（既存の KERN_SYSTEM_GET_PROCESS を使う）。

### ユーザーの判断が要るのに、技術判断で済ませている点
1. E4 の確かめ直し（M1）。
2. 最後の管理者の数え方を変えるか。account-admin の振る舞いも変わる（S1・S2）。
3. uid の使い回しと `userdel -f` の扱い（S3）。
4. `-u`・`-g`・`-G` で許す範囲（S8）。
5. `usermod -m` を範囲に残すか（S7）。
6. autologin と cron・at の掃除の担当（M2・S3）。
7. root の passwd と passkey の整合（M3）。

### 未実施
- build・host 試験・QEMU は動かしていません。
- 次の 2 点は推測です。
  - S6 の mount の実例。home の下に automount が載るかどうか。
  - S4 の横取りが実際に起きる時間。
- sshd が `!` の lock を扱うかどうかは確かめていません。

関係する file:
- /home/awe/zedBSD-claude1/plan/ws196/phase001/phase.md
- /home/awe/zedBSD-claude1/plan/ws196/ws.md
- /home/awe/zedBSD-claude1/userland/base/account-admin/main.c
- /home/awe/zedBSD-claude1/userland/base/account-admin/edit.c
- /home/awe/zedBSD-claude1/userland/base/common/account.c
- /home/awe/zedBSD-claude1/userland/base/login/verify.c
- /home/awe/zedBSD-claude1/userland/base/passkey/main.c
- /home/awe/zedBSD-claude1/userland/base/cron/main.c
- /home/awe/zedBSD-claude1/userland/desktop/sessiond/main.c
- /home/awe/zedBSD-claude1/userland/desktop/libkeiland-backend/machine/users.c
- /home/awe/zedBSD-claude1/userland/base/etc/passwd
- /home/awe/zedBSD-claude1/userland/base/etc/group
- /home/awe/zedBSD-claude1/userland/base/etc/shadow
