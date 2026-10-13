<!-- awesome-plan project=zedbsd record=ws196-p004 -->
# ws196-p004: T1 — account-admin の root の形と useradd・usermod・userdel を guest で

Status: planned（2026-10-14 P2 が依頼の文を書いた。Q1 が T1 の台帳に積む）
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

- 未実施（T1 の結果待ち）。
