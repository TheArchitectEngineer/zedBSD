<!-- awesome-plan project=zedbsd record=ws160-p002 -->

# ws160-p002: Settings の Users の頁の password の変更（GUI）

Status: in-progress（2026-10-05 P1 generation17。実装・build・host の試験まで。QEMU は Q1 経由で T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS160](../ws.md)
Queue: Q1 の指示（q722 の後、2026-10-05）

## 経路（Q1 の承認、2026-10-05）

Keiland の OS の境界の規則（Settings は libkeiland-backend を使わない B3、/bin の literal を持たない C4、OS の ifdef を持たない L1）のため、Settings は passwd を直接起動しない:

Settings（Users の頁）→ libkeiland の `kl_system_account_set_password`（新しい `kl_system_account_v1`、`kl_system_manager_v1` の version 4 の `get_account`）→ compositor の system extension（専用の thread）→ libkeiland-backend の `kl_backend_account_set_password` → zedBSD は `ACCOUNT_PASSWD_PATH`（base の `account.h`、`/bin/passwd`）の `passwd -s` に pipe で 2 行、Linux・FreeBSD は ENOTSUP（ベータ1）。

- protocol の version 4: WS113 の設計が get_displays に予約していたが未実装なので、こちらが version 4 を使い、WS113 は version 5 に改める（Q1 が記録）。
- password は log に出さない。compositor は受けた request の bytes・写し・job の buffer を、渡し終えたら消す。Settings は聞いた瞬間に field を消す。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `keiland/kl-system-protocol.h` | manager version 4、`get_account`（7）、capability ACCOUNT（0x40）、`kl_system_account_v1`（destroy、set_password(uint request, string current, string new)、event result）、`KL_SYSTEM_PASSWORD_MAX` 256 |
| `keiland/keiland.h`・`libkeiland/system/system.c`・`system-protocol.c/.h`・`exports.map` | `kl_system_account_set_password(system, current, fresh, &request)`、`KL_SYSTEM_HAS_ACCOUNT`、`KL_VERSION` 27。結果は `kl_system_take_result` の errno（0、EPERM 今の password が違う、EINVAL 規則、ENOTSUP、EBUSY、EIO） |
| `wayland/system.c`・`zwl.h`・`protocol.c` | `ZWL_SYSTEM_ACCOUNT`。version 4 の manager に capability を出し、`get_account` で object を作る。set_password は request の bytes を消し、写しを job に移して消し、1 つずつ thread で `kl_backend_account_set_password`、`zwl_system_tick` で結果を問い合わせた object へ（log `ZWL SYSTEM account set-password/result …`、password 無し）。終わりに job を待って消す |
| `libkeiland-backend/keiland-backend.h`・`libkeiland-backend-zedbsd/account-zedbsd.c`・`unsupported/account-unsupported.c` | zedBSD: pipe と fork（子は async-signal-safe だけ）で `passwd -s`、2 行を書いて消し、終了 status を errno に（0→0、3→EACCES、4・5→EINVAL、他→EIO）。SIGPIPE はこの thread で保留して受け取る。Linux・FreeBSD は ENOTSUP |
| `userland/base/common/account.h` | `ACCOUNT_PASSWD_PATH` と passwd -s の終了 status（host の試験のために path は上書きできる） |
| `userland/base/passwd/main.c` | `-s` で自分の password の時は root でも今の password の行を読む（root の分は確かめない）。他の利用者を root が設定する時は新しい password の 1 行だけ。compositor が root で動く試験の guest でも行がずれない |
| `settings/page-users.c`（新）・`settings.h`・`pages.c`・`system.c`・Makefile 3 つ | Users の頁（準備済みに）。「Your account」（名前・full name・home、passwd の database）と「Password」（今・新・もう一度の 3 つの field、点で表示、Show・Hide、Tab・click で移る、Enter と Change Password、新しい 2 つが違えば押せず理由を出す）。聞いたら field を消し、答えを下に（変わった・今の password が違う・規則・できない・使用中）。desktop が account を出さなければ「This desktop cannot change the password here.」 |
| `plan/ws089/tests/host-kl-system.c`・`host-render.c` | host の stand-in に account（`HOST_ACCOUNT_RESULT` で提供し、その errno で答える、password は長さだけ印字） |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor・Settings・libkeiland・passwd（CI の config） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| `python3 userland/desktop/libkeiland/exports.py --check` | OK |
| `sh plan/tools/keiland-os-boundary/check.sh` | PASS |
| `sh plan/ws160/tests/run-host-users.sh`（Settings の host の描画） | 10 checks passed（account 無し、聞く・stand-in は長さだけ・出力に password 無し・0、EPERM・EINVAL・ENOTSUP が頁に届く、違う 2 つでは聞かない）。PNG は `build/ws160-p002/users-*.png` |
| `sh plan/ws160/tests/run-host-account-backend.sh`（偽の passwd） | 12 checks passed（`passwd -s` が 2 行を読む、status の対応、空・改行・長すぎは passwd を起動せずに拒む） |
| `sh plan/ws160/tests/run-host-account.sh` | 32 checks passed（p001 の核、変更なし） |
| `sh plan/ws131/tests/host-system.sh`（compositor の system.c と libkeiland を socket で繋ぐ） | PASS。capability に ACCOUNT、set_password が 0・EPERM・EINVAL で答え、空の password は library が EINVAL。この試験は ws099-p032（df68df7）の `zwl_network_details` で link できなくなっていたので、stub を足して直した |
| style-check（新しい file、変えた hunk） | 指摘 0 |
| QEMU（`plan/ws160/tests/p002-guest.sh BUILD`、Settings の guest） | **未実施**。T1 に依頼 |

## QEMU の試験（T1 への依頼）

- image: `SETTINGS_CONFIG=plan/ws160/tests/config-amd64-settings-users.mk plan/ws089/tests/build-settings-image.sh BUILD`（Settings の guest＋passwd・su・sudo）。起動 `plan/ws089/tests/settings-guest.sh start`。
- 試験: `plan/ws160/tests/p002-guest.sh BUILD [OUTDIR]`（BUILD の wayland・settings・libkeiland.so を写す。guest の desktop は root なので root の password を変え、最後に /etc/shadow を戻す）。
- 合格: 全行 ok（passwd-setuid、users-page、change-asked、compositor-asked、compositor-result、settings-result、shadow-changed、no-password-in-logs、no-error）。users.png・users-changed.png をユーザーに見せる。

## 残り

- QEMU の結果の判定。kei の desktop での確認（今の password を確かめる経路）は実機の UAT（release の image）。
- Linux・FreeBSD の backend（logind・PAM・chpasswd など）はベータ1 の後。
