<!-- awesome-plan project=zedbsd record=ws073-p029 -->

# ws073-p029: ping を setuid root にし、利用者が ping できるようにする（BUG-102）

Status: in-progress（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-102](../../bugs/BUG-102.md)
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29）。Queue の ID は main が記録する

## 範囲と受け入れ

ユーザーの決定（2026-09-29、原文）「pingをsetuidにします。これはBSD系OSだからです。」

- ping の package の install の mode を 4755 にする（`userland/base/ping/Makefile` の 11 番目の引数。newgrp と同じ仕組み）。
- `socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)` の直後に `setuid(getuid())` で権限を落とす（BSD の ping と同じ順）。
  引数・名前の解決（getaddrinfo）は権限を落とした後に行う。
- 受け入れ: QEMU の amd64 guest で利用者 kei（uid 1000）から ping が通る。image の `/bin/ping` が `-rwsr-xr-x root`。
  kernel は変えない（exec の setuid は `src/kern/exec.c` の `exec_credential_prepare`、setuid(2) は superuser なら real・effective・saved の全てを置き換える）。

## 設計

`userland/base/ping/main.c`: main の最初（引数の検査の前）に raw socket を開いて `errno` を保存し、`getuid()` の値で `setuid()` する。
setuid が失敗したら権限を持ったまま走らず終了 1。socket の失敗は従来と同じ位置（`PING ...` の行の後）で保存した errno で報告する。
kernel の raw socket の検査（`src/kern/syscall.c` の socket 生成、`cred_is_superuser`）は生成の時だけで、sendto・recvfrom は検査しない。

## 手順と結果

（記入中）

## Resume point

amd64 の noclang の image（`sh plan/ws073/tests/build-image-noclang.sh build/amd64`、log `build/ws073-p029/build.log`）を build し、
boot test、guest で root から kei の authorized_keys を置いて `ssh kei@` で `id`・`ls -l /bin/ping`・`ping -c 3 127.0.0.1`・`ping -c 3 10.0.2.2` を確かめる。
