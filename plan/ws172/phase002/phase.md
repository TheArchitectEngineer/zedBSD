<!-- awesome-plan project=zedbsd record=ws172-p002 -->
# ws172-p002: /sbin/passkey の password・PIN と sessiond の外部の認証

Status: uncleared（2026-10-05 夜 T1-203 FAIL: greeter の password の login が起きない（sessiond.log に `GREETER failed … ZWL EXIT frames=0 error=5` が続き CONSOLE へ）。試験の image の名前（hdd-image.img）と su の欠けも。証拠は T1 の台帳 T1-203。再開: 次の P1 が解析と修正、main には merge 済みなので graphical login の image への影響を先に確かめる）
WS: [ws172](../ws.md)
設計: [phase001](../phase001/phase.md) の §1〜§12（第 2 版）と判断 P1〜P10（ユーザー承認、2026-10-05）、docs/architecture/security.md の「Login authentication」、keiland.md の login の節

## 範囲

`/sbin/passkey`（password・PIN・styles・enrolled・enroll-pin・remove-pin）と `/etc/passkey`、sessiond（passkey の非同期の起動・CANCEL・busy・uid ごとの数・起動の後の PIN の規則・`ok uid` の照らし）、greeter・lock の PIN、Settings の PIN（set_pin を `ENROLL pin` に）、account-admin（削除・追加・reset）、WS163 の mock を外す。FIDO2 は p003。

## 実装（commit、agent/p1）

| 段 | commit | 中身 |
| --- | --- | --- |
| 1 | 16b4fd99 | `userland/base/passkey/`（main.c・request.c・record.c・passkey.h、sbin 0500）。host 試験 `tests/passkey-host-test.sh` |
| 2 | 9668d420 | sessiond: `auth.h`・`auth.c`（要求の読み、passkey を自分の process group で起動、出力を poll、`status touch` → `TOUCH`、期限 10 秒（鍵 35 秒）で TERM、2 秒後に KILL、`ok uid` の照らし、遅れた FAIL）、`auth-policy.c`（uid ごとの数、試行の前に数える、起動の後は password か鍵で入るまで PIN を出さない、5 回で PIN を止める、2〜16 秒の遅れ、理由の語の対応）。greeter.c・session.c の poll の loop に組み込み、`login_verify` を外した。host 試験 `tests/sessiond-auth-host-test.sh` |
| 3 | dd30409a | libkeiland-backend: `kl_backend_session_authenticate/unlock` に style、`kl_backend_session_styles(_get)`・`set_pin`・`enrolled(_get)`・`reason`、答えの分類（`STYLES`・`ENROLLED`・`OK`・`FAIL reason`・`ERROR busy`・`ERROR`、`TOUCH` は答えにしない）。compositor: greeter・lock の画面は `STYLES` で方式を知り、PIN があれば PIN を先に、下の link で切り替え（6 桁でなければ送らない、STYLES の答えの前の Enter は答えの後に送る）、FAIL の語（pin-off・locked・timeout）を表示。Settings の `set_pin` は `ENROLL pin`/`REMOVE pin` へ、`kl_system_account_v1` に `enrolled` event（manager v11、KL_VERSION 36、`kl_system_account_enrolled`、`KL_SYSTEM_CHANGED_ENROLLED`）、Settings の PIN の card はそれで PIN の有無を出す。WS163 の mock（pin-store.c/.h、-lcrypt）を削除し、`~/.config/keiland/pin` は session の始めに消す。sessiond の greeter の返事の buffer を 512 に（m1） |
| 4 | b92dc626 | account-admin: `/etc/passkey` のその名前の行を、削除では最初に、追加では残りを、reset-password でも消す（`passkey_record_replace`、後の版の file は変えずに失敗） |

## 確認（host、2026-10-05）

- `plan/ws172/tests/passkey-host-test.sh`: PASS
- `plan/ws172/tests/sessiond-auth-host-test.sh`: ok（数の規則、偽の passkey での遅れ・TOUCH・busy・CANCEL・期限の KILL・uid の食い違い・空白の label・greeter と session の要求の制限、ASan・UBSan）
- `plan/ws131/tests/host-session.sh`: 48/48（新しい要求と答え）
- `plan/ws131/tests/host-system.sh`: PASS（set_pin の答えと refused の語、enrolled event、古い PIN の file の削除）
- `plan/ws131/tests/host-power.sh`・`host-seat-linux.sh`・`host-seat-freebsd.sh`・`plan/ws132/tests/run-host-events.sh`・`plan/ws089/tests/host-build.sh`・`run-host-account-admin.sh`: 通過
- `plan/tools/keiland-linux/makefile-sync.sh`: PASS（WS173 の shot-none.c の sync の規則を足した）、`header-check.sh`: PASS
- zedBSD の build（-Werror）: sessiond・wayland・settings・account-admin・libkeiland、warning 0。style-check: 新しい・変えた file で新しい指摘 0

## 未実施

- QEMU（T1）: `plan/ws172/tests/build-passkey-image.sh BUILD` で image（`config-amd64-passkey.mk` = graphical login の image ＋ passkey・account-admin・settings）、`plan/ws172/tests/passkey-p002-guest.sh`（§ の 0〜8）。
- Settings の PIN の card の UI の操作は QEMU では流さない（host-system の試験で protocol を確かめた）。
- Linux・FreeBSD の compositor の build（guest の中の build）は未実施。session の口は unsupported（ENOTSUP）のまま。
- 実機は未実施。

## 残り

- `ENROLLED` の答えは `pin=0|1 fido2=N`。鍵の一覧（id・label）は p003。
- `TOUCH` の表示と CANCEL の UI は p003。
- WS163 の Phase の扱い（cancel か移管か）は Q1。
