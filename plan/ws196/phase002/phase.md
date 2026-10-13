<!-- awesome-plan project=zedbsd record=ws196-p002 -->
# ws196-p002: account-admin の欠陥の直しと、root の前段の request の口

Status: planned（2026-10-14 Q1 が投入、q969、P2）
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
