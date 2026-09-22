# ws032-p007 結果: OpenSSH（2026-09-22、cleared）

q315-i07。OpenSSH portable 10.5p1 を `userland/packages/network/openssh/` としてクロス
ビルドし、**実機で公開鍵ログインができるところまで**確認した。

## 1. 成果物

`/usr/bin/ssh`、`/usr/bin/ssh-keygen`、`/usr/bin/ssh-agent`、`/usr/bin/ssh-add`、
`/usr/bin/scp`、`/usr/bin/sftp`、`/usr/sbin/sshd`、`/usr/libexec/sftp-server`、
`/usr/libexec/ssh-sk-helper`、`/etc/ssh/` の設定、`/usr/share/licenses/openssh/LICENCE`。

libcrypto に依存するため、menuconfig の登録は `security/openssl` を要求として持つ。
OpenSSH だけを選ぶと OpenSSL が付いてこない不具合は
[user-requested-fixes.md](../user-requested-fixes.md) の「menuconfig: OpenSSL と OpenSSH」で直した。

## 2. 当てているパッチ（2 件）

| パッチ | 何のため |
| --- | --- |
| `0001-recognise-the-zedbsd-target.patch` | `configure` の `case $host_os` に zedbsd を足す。知らない OS には既定値を当てないのが OpenSSH の作りで、当てなければ configure が止まる |
| `0002-include-stdio-where-a-stream-is-named.patch` | `FILE *` を使う場所で `<stdio.h>` を include していない箇所。上流が別経路で間接 include していたものが、こちらのヘッダでは来ない |

## 3. privilege separation

`sshd` は権限分離のために `chroot` 先と専用ユーザを要る。`/var/empty`（root 所有・
0755・空）と `sshd` ユーザを用意し、`chroot(2)` を libc に足した（[p004](../phase004/results.md)）。

## 4. サービスとしての起動

`/etc/service.d/sshd` をパッケージが同梱し、既定 `rc.conf` は `sshd: enabled: true,
optional: true` を持つ。パッケージを選ばなければ定義自体が入らず、init は何も言わない。
起動スクリプト `/usr/libexec/sshd-start` は、設定の有無・`/var/empty`・host key を
確かめてから `sshd` を起こす。詳細は
[user-requested-fixes.md](../user-requested-fixes.md) の「OpenSSH の起動スクリプト」。

`sshd` は自分を再実行するので、init が渡す `argv[0]` は絶対パスでなければならない
（`sshd requires execution with an absolute path`）。これも同じ節に記録がある。

## 5. 実機で確認したこと

`plan/ws032/tests/openssh-target.sh`: 版表示、host key 生成、鍵の作成、
`authorized_keys` 配置、`sshd` 起動、**公開鍵での `ssh root@127.0.0.1 id`**、
疑似端末（`ssh -t … tty`）、`scp` の往復、`sftp` の `put`/`get`。

`plan/ws032/tests/sshd-service-target.sh`: **手で起動せず**、ログイン前に init が
起動した `sshd` へ ssh して `SERVICE-LOGIN-OK`。パッケージ非選択時に init が
何も言わないことも見る。

どちらも WS 終了時点の回帰で exit 0。

## 6. 限界

ホストからゲストへの ssh は `plan/ws033/tests/` の hostfwd 経由で別に確認している。
鍵交換アルゴリズムの網羅、`sshd_config` の全項目、PAM・Kerberos・GSSAPI は範囲外。
