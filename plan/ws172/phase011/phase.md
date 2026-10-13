<!-- awesome-plan project=zedbsd record=ws172-p011 -->

# ws172-p011: passkey の command を Linux・FreeBSD の Keiland へ（FIDO2 の backend 無し）

Parent: [WS172](../ws.md)
Status: in-progress — test-wait（2026-10-13 P2 q961: 実装・Linux の build・install・host 試験済み。FreeBSD は FreeBSD 15 の header での構文の確かめまで、guest の build と Debian の guest は T1、Q1 経由）
Phase disposition: normal
Queue: q961（P2、2026-10-13）。承認: ユーザー 2026-10-13「あ、Linux/FreeBSDでは、FIDO2バックエンドがないだけで、passkeyコマンドは入れてください。」「/opt/keiland/etc/passkey に書き込める /opt/keiland/sbin/passkey コマンドがLinuxとFreeBSDに移植されればいいと思います。」（Q1 経由）。同じ日「Linux/FreeBSDではFIDO2はまだ入れなくていいです。…ベータ3にします。」
番号: p008〜p010 は [p004b](../phase004b/phase.md) の設計が実装の Phase の案に使ったので、この Phase は p011。

## 範囲

- `userland/base/passkey`（p002 の `/sbin/passkey`）を Linux・FreeBSD の Keiland の build（`keiland-linux.mk`・`keiland-freebsd.mk`）に入れる。install 先は `$(KEILAND_PREFIX)/sbin/passkey`（既定 `/opt/keiland/sbin/passkey`）、mode 0500（zedBSD と同じ、root だけ、set-user-ID ではない）。
- 設定の file は `KEILAND_SYSCONFDIR "/passkey"`（既定 `/opt/keiland/etc/passkey`、zedBSD の `/etc/passkey` に当たる）。書く時の lock は `KEILAND_SYSCONFDIR "/passkey.lock"`（zedBSD は account の file と共通の `/etc/shadow.lock`。Linux・FreeBSD では passkey は account の file を書かないので、passkey の file だけの lock にする）。path は build の時に macro で切り替える（`PASSKEY_FILE`・`ACCOUNT_SHADOW_LOCK`・`PASSKEY_FIDO2_PATH`、既に `#ifndef` の形か、その形にする）。
- password の確かめは各 OS の shadow（Linux は glibc の `getspnam_r` と libcrypt の `crypt`。FreeBSD は shadow.h が無いので、`getpwnam_r` が root に返す master.passwd の hash を `struct spwd` の形で渡す小さな橋（`freebsd-compat` の native bridge、既存の `pty.h`・`sys/xattr.h` と同じ形）と、`crypt.h` を `unistd.h` に向ける橋）。PIN の hash は今と同じ SHA-512 crypt（`$6$rounds=`、glibc の libxcrypt と FreeBSD の libcrypt の両方が持つ）。
- **FIDO2 の backend は入れない**（passkey-fido2・libpasskey・OpenSSL は build しない。Linux の build に libssl-dev を必須にしない）。

## backend が無い時の振る舞い（source の変更なし）

- FIDO2 の要求（`AUTH name fido2`・`ENROLL … fido2`・鍵の操作）は、今の source のとおり `PASSKEY_FIDO2_PATH`（Linux・FreeBSD では `KEILAND_LIBEXECDIR "/passkey-fido2"`、install しない）が実行できないので `fail no-key` を返す（終了の状態 1）。新しい理由の語は足さない（zedBSD で passkey-fido2 が無い時と同じ意味）。
- `styles`・`enrolled` は password と PIN だけを言う（fido2 の行は登録できないので無い）。
- 呼ぶ側（Linux・FreeBSD の Keiland の login・lock の画面、PAM など）は**範囲の外**: この Phase は command と file の移植だけ（ユーザーの補足どおり）。誰が呼ぶかは後の判断。

## 実装（2026-10-13 P2）

| file | 内容 |
| --- | --- |
| `userland/base/passkey/Makefile.linux`・`Makefile.freebsd`（新） | passkey（main・request・record と common/account.c・login/verify.c）を `sbin` に、`-lcrypt`。dir ごとの CPPFLAGS で `PASSKEY_FILE`＝`$(KEILAND_PREFIX)/etc/passkey`、`PASSKEY_FIDO2_PATH`＝`$(KEILAND_PREFIX)/libexec/passkey-fido2`（install しない）、`ACCOUNT_SHADOW_LOCK`＝`$(KEILAND_PREFIX)/etc/passkey.lock`。FreeBSD は `libkeiland-compat.a` も link |
| `userland/base/passkey/main.c` | `PASSKEY_FIDO2_PATH` を `#ifndef` の形に。`passkey_read` が始めに `*length = 0`（Linux の gcc の `-Werror=maybe-uninitialized`、`read` の失敗の errno が 0 の時の道。動作は同じ） |
| `userland/desktop/keiland-linux.mk`・`keiland-freebsd.mk` | passkey の規則を include、install の mode に `sbin/*` → 0500。FreeBSD の native bridge の header に `shadow.h`・`crypt.h` |
| `userland/desktop/freebsd-compat/freebsd/shadow-freebsd.h`・`shadow-freebsd.c`・`crypt-freebsd.h`（新）と `Makefile.freebsd` | FreeBSD の橋: `getspnam_r` は `getpwnam_r`（root には master.passwd の hash）を `struct spwd` の形に（名前・hash・期限（`pw_expire` の秒→日）、他は -1。FreeBSD の `*LOCKED*` は先頭の `*` で既存の判定のとおり鍵のかかった account）。`crypt.h` は `unistd.h` に向ける（FreeBSD 15 の `crypt()`・`clearenv()`・`getentropy()` は libc にある、src-15.0 の header で確認。libcrypt の `$6$rounds=` も `crypt-sha512.c` で確認） |
| `plan/ws172/tests/passkey-linux-host-test.sh`（新） | 下の host 試験 |

## 確かめ（2026-10-13 P2、host = Debian 13）

| コマンド | 結果 |
| --- | --- |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws145-p005-linux -j16 all` | rc 0、warning 0。`sbin/passkey` の中の path は `/opt/keiland/etc/passkey`・`/opt/keiland/etc/passkey.lock`・`/opt/keiland/libexec/passkey-fido2` |
| 同じ build の `install DESTDIR=build/ws172-p011-dest` | `/opt/keiland/sbin/passkey` が mode 500 |
| `sh plan/ws172/tests/passkey-linux-host-test.sh`（新。Linux の規則と同じ source と flag で、file と lock を新しい folder に向けて build し、sudo の `unshare -m` の中で試験の `/etc/passwd`・`/etc/shadow` を bind mount、host の file は触れない） | PASS: password の正・誤（`ok uid=1500`・`fail bad-secret`）、`enroll-pin` → `ok`、PIN の正・誤、`styles` に pin、file に `kei:1500:pin:$6$…` で mode 600、`auth … fido2`・`enroll-fido2` → `fail no-key`、無い account → `fail no-such-user`、root でない呼び手 → 終了 2 |
| `sh plan/ws172/tests/passkey-host-test.sh`（p002 の純粋な部分） | PASS |
| zedBSD: `make BUILD=build/ws157-p017 ZEDBSD_CONFIG=config/current-uat.mk build/ws157-p017/bin/passkey` | rc 0、我々の source の warning 0（同時に build された OpenSSL の package と perl の locale の warning は外部の物） |
| FreeBSD: `clang --target=x86_64-unknown-freebsd15 -std=gnu17 -Wall -Wextra -Werror -fsyntax-only`、FreeBSD 15.0 の source の header（`/home/awe/freebsd/port-analysis/src-15.0`）と橋の header で、passkey の 5 つの source と shadow-freebsd.c | 全て error・warning 0（link は未確かめ） |
| `python3 plan/tools/style-check.py userland/desktop/freebsd-compat/freebsd/shadow-freebsd.c` | 0（main.c の指摘は既存の行だけ） |

## 未実施（T1 へ、Q1 経由）

- FreeBSD 15 の guest（WS109）: `make -f userland/desktop/keiland-freebsd.mk all install` が通る（passkey の link と libkeiland-compat.a）、`/opt/keiland/sbin/passkey` が 0500、root で `printf 'styles\nkei\n' | /opt/keiland/sbin/passkey` が `ok uid=… styles=password`、guest の利用者の password で `auth` が ok、`enroll-pin` の後 `/opt/keiland/etc/passkey` に pin の行、`auth … fido2` が `fail no-key`。
- Debian 13 の guest: 同じ（host 試験と同じ内容を本当の `/etc/shadow` で）。
- 削除の依頼: host 試験が root で作った file（`build/tmp/ws172-passkey-linux.*` の中の `passkey`・`passkey.lock` は root の物）は Q1 の片付けで sudo が要る。
