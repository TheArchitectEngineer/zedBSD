<!-- awesome-plan project=zedbsd record=ws196-p004 -->
# ws196-p004: T1 — account-admin の root の形と useradd・usermod・userdel を guest で

Status: uncleared（T1-597 で 2 が FAIL。試験の直し 842c58a82、再試験の依頼は下の「T1 への再試験の依頼（T1-597 の後）」）
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
