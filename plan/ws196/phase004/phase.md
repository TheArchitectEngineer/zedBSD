<!-- awesome-plan project=zedbsd record=ws196-p004 -->
# ws196-p004: T1 — account-admin の root の形と useradd・usermod・userdel を guest で

Status: cleared（2026-10-14 Q1: T1-603 で admin-p026・user-commands-guest とも status=0、ssh-retries 0・ssh-lost 0。T1-600 の空の答えは SSH の経路（BUG-281 に記録））
Disposition: normal
Parent: [WS196](../ws.md)
Queue: —（Q1 が T1 に渡す）
依存: [p002](../phase002/phase.md)（merge 済み 7f2c850a7）、[p003](../phase003/phase.md)（merge 待ち、Q1 が `config/ci/*.mk` に 3 つの命令を足す）

## 目的

p002（account-admin の欠陥の直しと root の形、flock の lock、sessiond の autologin）と p003（root の前段の 3 つの命令）を
zedBSD の guest で 1 回の QEMU で確かめる。p002 と p003 の依頼を 1 つにまとめた（Q1 の指示）。結果で Q1 が p002 と p003 を判定する。

## T1 への依頼の文

- **tree**: p003 を merge した main の SHA（結果に書く）。
- **image**: `plan/tools/guest/test-image.sh plan/ws196/tests/config-amd64-users.mk BUILD`（CI の image `config/ci/config-amd64.mk` に
  account-admin・Settings・sessiond・useradd・usermod・userdel を足した物。graphical login 付き）。1 つの image で全部を流す。
- **起動**: `plan/tools/guest/guest.py start BUILD/hdd-image.img`、`plan/tools/guest/guest.py wait`。
- **流す試験と合否**（この順）:
  1. `plan/ws089/tests/admin-p026-guest.sh OUT1` — PASS は最後の行 `admin-p026: status=0`（Settings の形の回帰。p002 で root の場合の期待を `error bad-request` に直してある）。
  2. `plan/ws196/tests/user-commands-guest.sh OUT2` — PASS は最後の行 `user-commands-guest: status=0`。中身: 3 つの命令の mode（0755 root、setuid 無し）と root でない呼び手の拒否（1）、account-admin の root の形（user-add の uid と `/etc/account-admin.last-id`、home 0700、`!`、`no-password`、kei の lock の `last-administrator`、`home remove`、消した後の追加の uid が大きい）、2 つの同時の user-add が両方 `ok` で `/etc/shadow.lock` が残り `passwd -s` が通る（zedBSD の kernel の flock）、useradd・usermod・userdel の終わりの状態（0・1・2・6・8・9）と files、`-f` の削除、`-r` の home の削除、`sudo -S useradd` の syslog の `user-add by root (sudo kei) for sam: ok`。files は最後に guest の中で元に戻る。
  3. **autologin の拒否**（graphical、p002 D8）: root で `cp -p /etc/keiland/autologin /tmp/p004.autologin`（無ければ無いことを記録）、`printf 'user-add\nname tom\n' | /usr/libexec/account-admin`（`ok`、password は lock）、`echo tom > /etc/keiland/autologin`、guest を再起動 → 画面が greeter（login の画面）で tom の session ではないこと（PNG）、`/var/log/sessiond.log` に `SESSIOND AUTOLOGIN refused account=tom locked`。片付け: `/etc/keiland/autologin` を保存した物に戻し（無かったなら消す）、`/sbin/userdel -r tom`。PNG は依頼の規則どおり（`plan/tools/boot-test.sh` の PNG か、guest.py の QMP の screendump）。
  4. **Settings の Users の頁**（graphical）: root で `/sbin/useradd -M -c 'Uma U' uma`、kei で login した desktop の Settings > Users を開き、一覧に「Uma U」が見えること（PNG）。片付けに `/sbin/userdel uma`。AAT の道具（`plan/tools/aat/`）で操作してよい。
- **長さ**: 1〜2 は数分、3 は再起動 1 回、4 は画面の操作 1 回。長すぎれば 3・4 を別の組にしてよい（Q1 に相談）。
- **返す物**: 各試験の PASS/FAIL、OUT1・OUT2 の中身（`modes.txt`・`tom.txt`・`lock.txt`・`messages.txt` など）、3・4 の PNG と `sessiond.log` の該当の行、image の tree の SHA。結果は Q1 へ（P2 へは割り込まない）。FAIL の解析はしない。

## 結果

### T1-597（2026-10-14、QEMU、config-amd64-users.mk、tree 24208099f）— uncleared

- 1 admin-p026: 起動の直後の guest で status=1（2 回）、起こし直した guest で `admin-p026: status=0`。
- 2 user-commands-guest: `status=1`。FAILED は lock-passwd・tom-password・sudo-add の 3 つで、残りの 50 は ok。
- 3 autologin の拒否: PASS（再起動の後 greeter、kei・tom の一覧。sessiond.log に `SESSIOND AUTOLOGIN refused account=tom locked`）。
- 4 Settings の Users: PASS（`uma  Uma U · Standard`）。
- 証拠: main の checkout の `build/review/t1-597/`（autologin2.png・users3.png・uc-fresh.txt）、T1 の worktree の
  `build/review/t1-597/`（admin-run2.txt・uc-run2.txt・admin-fresh.txt・out1〜out2c）。

### FAIL の原因（2026-10-14 P2、T1-597 の出力から。QEMU は流していない）

- **2 の 3 つは試験の期待の誤り**（製品は正しい）。`passwd -s` と `sudo -S` の prompt（`New password: `・
  `[sudo] password for kei: `）は行の終わりが無いので、続く `echo status=$?` が同じ行に付き、`^status=0$` に合わなかった。
  status は 0 で、続く mod-lock・mod-locked（`tom:!$6$`）・su-tom・log-sudo が ok なので、password の設定と sudo の
  useradd は通っている。直し: `run()` が status を自分の行に出す（`s=$?; echo; echo status=$s`）。3 つを `run()` に変えた。
  他の段で同じ形の物は無い（not-root-status は useradd の message が行で終わる、admin-p026 の passwd-short は行頭に
  合わせない `status=4`）。
- **1 の起動の直後の FAIL は SSH の接続の問題で、account-admin の欠陥ではない**。起動の直後の guest の出力
  （admin-run2.txt・uc-run2.txt、out1・out1b の file が全て空）では、ほぼ全ての `guest.py run` が空の答えを数 ms で返し、
  admin-p026 の 1 回が約 6 s で終わった（bad-password の 2 s の待ちを含むはず）。guest の `/var/log/messages`
  （out2b/messages.txt）には最初の run の bad-password と bad-request の 2 行と、uc-run2 の後ろの段（add-jay から）の行だけ
  があり、間の request は account-admin に届いていない。つまり `guest.py wait` が通った後、約 1 分の間 SSH の接続が
  命令を走らせずに閉じられ（ssh の LogLevel=ERROR では何も出ない）、その後に戻った。sessiond・syslogd・/etc の初期化を
  待つ不足ではない（syslogd は file を自分で作り、届いた行はすぐ書く。`/etc` は image のまま）。起動の直後の何が SSH を
  止めたかは未解明（[BUG-281](../../bugs/BUG-281.md) の「SSH の session が約 1〜2 分止まる」と近いが、こちらは止まらずに
  すぐ閉じる）。Q1 に報告した（ticket にするかは Q1）。
- 試験の側の対策（842c58a82）: 2 つの script の `guest()` が ssh の status 255（ssh 自身の失敗）を 3 s ごとに最大 90 s
  取り直し、取り直しを `OUT/ssh-retries.txt` に書き、最後の行の前に `ssh-retries: N` を出す。接続が命令の途中で切れた
  なら命令が 2 回走りうるので、N が 0 でなければ結果と一緒に報告する。
- host の確認: `guest()` と `run()` を python3 の代役（2 回 255、その後 sh で実行）で流し、2 回の取り直しの記録、
  prompt の後の `status=0` が自分の行に出ること、status 4 が出ることを確かめた。

## T1 への再試験の依頼（T1-597 の後）

- **tree**: 842c58a82 を merge した main（image の中身は T1-597 と同じでよい。直したのは試験の script だけなので、
  T1-597 の image `config-amd64-users.mk` を作り直さずに使ってよい）。
- **起動**: `plan/tools/guest/guest.py start BUILD/hdd-image.img`、`plan/tools/guest/guest.py wait` の**直後に**（待ちを足さずに）流す。
- **流す試験と合否**（この順、同じ guest で）:
  1. `plan/ws089/tests/admin-p026-guest.sh OUT1` — PASS は最後の行 `admin-p026: status=0`。
  2. `plan/ws196/tests/user-commands-guest.sh OUT2` — PASS は最後の行 `user-commands-guest: status=0`。
- **返す物**: 2 つの出力の全文（`ssh-retries: N` の行を含む）、`OUT1/ssh-retries.txt`・`OUT2/ssh-retries.txt`、
  `OUT2/messages.txt`、guest の起動から 1 の開始までの秒数。N が 0 でなければ、その間の guest の `/var/log/messages` の
  末尾と `/var/log/sessiond.log` の末尾（解析は要らない、P2 が読む）。

### T1-600（2026-10-14、QEMU、image build/t1-597、tree 79562b496、起動から 29 s、ssh-retries 0）— uncleared

- 2 user-commands-guest: PASS。
- 1 admin-p026: `status=1`。`busy: ok` の直後の `remove-alice: FAILED` の答えが**空**（`status=` の行も無い）、続く
  alice-gone・alice-gone-group で passwd・group・shadow に alice が残り、home-exists は `error name-taken`。後の add-bob・
  remove-bob・bob-home-gone は ok。証拠: T1 の worktree の `build/review/t1-600/`（admin.txt・out1・out2）。

### FAIL の原因（2026-10-14 P2、T1-600 の出力と code から。QEMU は流していない）

- **account-admin の欠陥ではない。remove-alice の request は guest で走った跡が無く、SSH の経路で答えが 120 s 来なかった**
  （[BUG-281](../../bugs/BUG-281.md) の「答えが空のまま timeout」と同じ形）。根拠:
  - guest の log（out1/messages.txt）で busy（17:36:58、pid 149）の次の account-admin の行は 17:38:59 の home-exists の
    add（pid 170）。remove-alice の行が無い。account-admin は request を読んだ後のどの終わり方でも 1 行を書く（拒否・
    失敗・結果。main と admin_check_caller）。
  - 答えには `su` の shell の `echo status=$?` の行も無い。account-admin が落ちた（signal・crash）なら `status=139` などが出る。
    答えが全く無いのは、command 全体が host の `timeout 120` の間に何も返さなかったこと（`timeout` の 124 は 255 でないので
    取り直されず、ssh-retries 0）。
  - 17:38:59 の add は lock を取れて `name-taken` を返した。account-admin が lock の中（admin_remove の admin_busy・passkey・
    commit）で止まっていたなら、その add は lock を取れずに `error failed`（`the account files could not be locked`）に
    なる（zedBSD の flock は open file description の排他、src/kern/record-lock.c）。lock の前（request の読み・
    admin_check_caller）に待ちは無い（stdin は printf の pipe、password の照合は crypt、間違いの時だけ 2 s）。
  - 同じ手順（busy → kill → remove-alice keep-home → home-exists）は T1-597 の起こし直した guest で PASS した
    （admin-fresh.txt、busy の 1 s 後に remove が ok）。T1-597 の tree 24208099f と T1-600 の 79562b496 の差は
    libbrowser の 5 file だけで、account-admin・common・login・su・sh・kernel は同じ。
  - 「alice の process を止める前に送った」可能性は無い。busy-end.txt は `waited=0` で alice の process は残っていない。
    仮に残っていても account-admin はすぐ `error busy` を返し、答えは空にならない。
- 試験の直し（e317a9b1d、admin-p026-guest.sh と user-commands-guest.sh の `guest()`）:
  - 各 command は最初に自分の mark（run の tag と番号）を guest の `/tmp/guest-calls` に書く。host の待ちは 60 s。
  - ssh の 255 か 60 s の無答のとき、guest に mark があるかを訊く。無ければ command は始まっていないので 3 s ごとに
    最大 90 s 取り直す（`OUT/ssh-retries.txt`）。あれば（または訊けなければ）取り直さず（2 回走らせない）、その失敗と
    guest の `ps -A -o user,state,pid,ppid,comm` を `OUT/ssh-lost.txt` に書く。最後の行の前に `ssh-retries: N` と
    `ssh-lost: M` を出す。account-admin が本当に止まったなら、ssh-lost.txt の ps にその process と state が残る。
  - 残る危険: SSH が止まった後で guest が遅れて command を受け取って走らせると、取り直しと 2 回走りうる。その時は
    guest の log に同じ request の行が 2 つ出る（返された messages.txt で分かる）。
- host の確認:
  - `plan/ws196/tests/account-admin-root-host-test.sh` に Settings の形を足した（kei、uid 1000、wheel、password
    secret123 で set-user-ID の写し、private mount namespace で host の /etc/passwd・group・shadow を試験の物に bind、
    /run を tmpfs にして utmp の login で busy）: 間違いの password、add alice（home 0700）、name-taken、busy、login を
    消して remove keep-home（passwd・shadow・group から消え home が残る）、home-exists、self。各答えは 20 s 以内
    （`timeout 20`）。結果 `87 checks passed`、`account-admin-root-host-test: PASS`（Settings の 14 個を含む、ASan・UBSan）。
    guest の失敗は host では再現しない（再現するのは SSH の経路で、host の試験の外）。host の試験が示すのは、同じ request
    の並びで account-admin が毎回答えを返すこと。
  - `guest()` を python3・timeout の代役（timeout を 2 s に縮め、呼び出しごとに ok・255・無答で走らない・走ってから無答を
    選ぶ）で両方の script について流した: 255 → 取り直して hello、無答で走らない → 取り直して hello、走ってから無答 →
    取り直さず ssh-lost 1（count 1 と ps）、両方無答 → 取り直さず ssh-lost 1（unknown）、組み合わせも期待どおり。
- BUG-281 への追記（T1-600 の 1 件: SSH の command の答えが 120 s 空、guest で走った跡なし）は Q1 に頼む。

## T1 への再試験の依頼（T1-600 の後）

- **tree**: e317a9b1d を merge した main。
- **image**: 作り直す（build/t1-597 は消した）: `plan/tools/guest/test-image.sh plan/ws196/tests/config-amd64-users.mk BUILD`。
- **起動**: `plan/tools/guest/guest.py start BUILD/hdd-image.img`、`plan/tools/guest/guest.py wait` の直後に（待ちを足さずに）流す。
- **流す試験と合否**（この順、同じ guest で）:
  1. `plan/ws089/tests/admin-p026-guest.sh OUT1` — PASS は最後の行 `admin-p026: status=0`。
  2. `plan/ws196/tests/user-commands-guest.sh OUT2` — PASS は最後の行 `user-commands-guest: status=0`。
- **返す物**: 2 つの出力の全文（`ssh-retries: N` と `ssh-lost: M` の行を含む）、`OUT1`・`OUT2` の `ssh-retries.txt`・
  `ssh-lost.txt`・`messages.txt`、guest の起動から 1 の開始までの秒数。M が 0 でなければ、その時の guest の
  `/var/log/messages` の末尾も（解析は要らない、P2 が読む）。
- **長さ**: 2 つで数分（SSH が止まれば 1 回につき最大 60 s の待ちと 90 s の取り直しが増える）。
