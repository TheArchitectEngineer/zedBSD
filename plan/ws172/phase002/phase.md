<!-- awesome-plan project=zedbsd record=ws172-p002 -->
# ws172-p002: /sbin/passkey の password・PIN と sessiond の外部の認証

Status: cleared（2026-10-05 夜 Q1: T1-210 (2) で passkey-p002-guest status=0（PIN の login・lock・5 回・password で戻る・sessiond の再起動・pk2・log に secret 無し）、T1-212 (1) で passkey の config の無い graphical login の image の password の login と zdesktop-p102 が PASS。T1-203・T1-209・T1-210 (1) の FAIL は 2 つの直し（0356c470 STYLES の答えを handoff の前に捨てない、64f20ef2 passkey を sessiond の依存に）で解消。QEMU の証拠、実機は未）
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

## T1-203・T1-209 の FAIL の解析と修正（2026-10-05 夜、P1）

- 症状: greeter に password を打っても AUTH が送られない（`ZWL GREETER styles=`・`ZWL GREETER auth`・`SESSIOND AUTH` が無い）。WS172 の image（T1-203）と ws035 の素の graphical login の image（T1-209、main 9006fc3a）の両方。
- 原因（dd30409a の後退）: greeter は最初の `zwl_schedule` の pass で `zwl_glass_tick` → `zwl_greeter_tick` から `STYLES kei` を送る。同じ pass の後の `enter_window_mode` で output の準備（約 160 ms）の後に `zwl_handoff_wait` が READY を言い GO を待つ。sessiond はその間に passkey の答え `STYLES password` を返し、`kl_backend_session_ready` は GO 以外の行を捨てていたので答えが失われ、`session_request` は STYLES のまま、`greeter_styles_asked` は 1 のまま、Enter は `greeter_submit_pending` で永久に保留された。session の側も sessiond は READY の前の行を読み捨てる（session.c）ので、READY の前に ENROLLED を送ると同じく答えが来ない。
- 修正: (1) `kl_backend_session_ready`（session-zedbsd.c）は GO の前の行を `session_line` に残し、`kl_backend_session_tick` は新しく読めなくても残った行を処理する。(2) greeter の STYLES と system.c の ENROLLED は `server->handed_over` の後にだけ尋ねる。
- `ZWL EXIT … error=5` が続いた 2 回（t1-203、t1-203-run1）は、boot の sessiond・respawn・試験の sessiond が同時に 2〜3 個動き greeter が display を取り合ったもので、試験が boot の落ち着く前に始まったため（試験の側）。待った回（p002-waited）は sessiond が 1 つで、上の原因だけが出た。
- 試験の側: `build-passkey-image.sh` が作るのは `BUILD/hdd-image.img`（試験の注記を直した）。`/bin/su: not found` は image の config に su が無いため（`config-amd64-passkey.mk` に su を足した）。
- host: `plan/ws131/tests/host-session.sh` 51/51（GO の前の答えを tick が渡す試験を追加）、`host-system.sh` PASS（試験の server を hand-over 済みに）、`sessiond-auth-host-test.sh` ok、`passkey-host-test.sh` PASS、ws089 `host-build.sh`・`run-host-account-admin.sh`、keiland-linux `header-check.sh`・`makefile-sync.sh` PASS。zedBSD の clang（-Wall -Wextra -Werror）で greeter.c・system.c・handoff.c・session-zedbsd.c・backend.c の compile は warning 0。style-check の指摘 0。
- QEMU の再試験は T1 に依頼（Q1 経由）。

## T1-210 (1) の FAIL の解析と修正（2026-10-05 夜、P1 の新しい generation）

- 症状: ws035 の素の graphical login の image（T1-209 の build、main ad9a1d7b）で AUTH が送られるようになったが、sessiond が毎回 `SESSIOND AUTH fail … reason=timeout`（GO の 2 秒後）を返し、greeter は「That took too long.」。同じ main の passkey の image（`config-amd64-passkey.mk`、T1-210 (2)）では `SESSIOND AUTH ok user=kei` で通る。
- 原因: `/sbin/passkey` が image に無い。passkey は `userland/base/passkey` の program だが、config.mk・`config/ci`・ws035・ws159 の UAT などの config は `ZEDBSD_USER_PROGRAMS` を名前で並べ、passkey を含めていない（`config-amd64-passkey.mk` だけが足していた）。T1-209 の build の rootfs/sbin に passkey は無く、`/etc/passkey` も無い（無いのは正しい: 空として扱う）。sessiond は exec の失敗（子の `_exit(2)`）で答えの無い終わりを `timeout` と読み、試行に数えて 2 秒の遅れの後に `FAIL timeout` を返していた。期限・pipe・poll の誤りではない（期限は起動の時刻から ms、poll は passkey の出力の fd、host 試験で確認済み）。
- 修正: (1) passkey を package `base/passkey` として登録し、sessiond の package が `base/passkey` を require する（Makefile の依存の展開で、sessiond を持つ全ての image に `/sbin/passkey` 0500 が入る。config.mk・`config/ci/config-amd64.mk`・ws035 の graphical/login・ws159 の UAT で展開を確認、修正の前は sessiond だけ）。(2) sessiond: `/sbin/passkey` が実行できなければ AUTH・UNLOCK・ENROLL・REMOVE は即座に `FAIL internal`（数えない、`SESSIOND passkey missing` を log）、STYLES・ENROLLED は passkey が答えなかった時と同じ。答えずに終わった passkey は `internal`（`SESSIOND passkey gave no answer exit=N|signal=N` を log）、`timeout` は sessiond が止めた試行（期限・CANCEL）だけ。exec の失敗は exit 127。(3) greeter: `internal` は「The login failed.」（既存の文）。docs/architecture/security.md に 1 段落。
- host: `sessiond-auth-host-test.sh` ok（答えの無い passkey → `FAIL internal`、実行できない passkey → 1 秒以内に `FAIL internal`・`STYLES password` を追加）、`passkey-host-test.sh` PASS、ws131 `host-session.sh` 51/51、`host-system.sh` PASS。zedBSD の build（ws035 graphical の config、-Werror）で sessiond・passkey・wayland を link まで、warning 0。style-check の指摘 0。
- 既存の不具合（範囲外、未修正）: `make menuconfig-host-test`・`make list-user-programs` は修正の前から `/bin/sh: Syntax error: "(" unexpected` で失敗する。

## 未実施

- QEMU（T1）: `plan/ws172/tests/build-passkey-image.sh BUILD` で image（`config-amd64-passkey.mk` = graphical login の image ＋ passkey・account-admin・settings）、`plan/ws172/tests/passkey-p002-guest.sh`（§ の 0〜8）。
- Settings の PIN の card の UI の操作は QEMU では流さない（host-system の試験で protocol を確かめた）。
- Linux・FreeBSD の compositor の build（guest の中の build）は未実施。session の口は unsupported（ENOTSUP）のまま。
- 実機は未実施。

## 残り

- `ENROLLED` の答えは `pin=0|1 fido2=N`。鍵の一覧（id・label）は p003。
- `TOUCH` の表示と CANCEL の UI は p003。
- WS163 の Phase の扱い（cancel か移管か）は Q1。
