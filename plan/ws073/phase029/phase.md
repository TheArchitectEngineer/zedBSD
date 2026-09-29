<!-- awesome-plan project=zedbsd record=ws073-p029 -->

# ws073-p029: ping を setuid root にし、利用者が ping できるようにする（BUG-102）

Status: cleared（2026-09-29）
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

変えた file: `userland/base/ping/main.c`（socket を main の最初に開き `setuid(getuid())`、socket の失敗は保存した errno で従来の位置で報告）、
`userland/base/ping/Makefile`（mode 4755）。試験: [tests/ping-user.sh](../tests/ping-user.sh)、[tests/setuid-drop.c](../tests/setuid-drop.c)。

- style: `python3 plan/ws073/tests/style-diff.py userland/base/ping/main.c` 変えた行の findings 0、`git diff --check` PASS。
- build: `sh plan/ws073/tests/build-image-noclang.sh build/amd64`（worktree の build/amd64、sysroot も worktree の中に作った）。
  1 回目は並列の build で sysroot の完成の前に `zedbsd-target-toolchain-ready` の検査が走って止まった（sysroot は同じ実行の中で完成）。
  2 回目で `check-amd64-native-image: build/amd64/hdd-image.img.unchecked: OK`。log `build/ws073-p029/build2.log`。warning は外部 package
  （openssh・openssl の deprecated・unused）だけで、zedBSD の source と ping に warning 0。
- boot test（QEMU）: `OUTPUT=build/ws073-p029/boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` PASS
  （`build/ws073-p029/boot-test/login.png`）。
- guest（QEMU、`tests/g.sh start build/amd64/hdd-image.img`、SSH）: `sh plan/ws073/tests/ping-user.sh` → `PING-USER:PASS`（出力 `build/ws073-p029/guest/`）。
  - kei で `id` = `uid=1000(kei) gid=1000(kei) groups=1000(kei),69(network)`、`/bin/ping` = `-rwsr-xr-x 1 root wheel`（image の install の mode）。
  - kei で `ping -c 3 127.0.0.1` 3/3 受信、`ping -c 3 10.0.2.2`（QEMU の gateway）3/3 受信。
  - 修正前の再現に当たる確認: kei が set-user-ID の無い複写を実行すると `ping: socket: Operation not permitted`、exit 1（BUG-102 の症状）。
  - 権限を落とす手順（setuid-drop.c を set-user-ID root にして kei で実行）: `before: uid=1000 euid=0` → `after: uid=1000 euid=1000`、
    その後の raw socket・`setuid(0)`・`seteuid(0)` は拒まれる。`SETUID-DROP:PASS`。
  - root の `ping -c 2 127.0.0.1` も 2/2。
- 未実施: 実機（ユーザーの報告の素の 5330・demo-lcd3）、graphical の session の terminal からの実行、i386・arm64 の image。

## 所見（範囲外、直していない）

- ping の統計の最小値: RTT が 0 の応答を「未設定」と扱う（`minimum == 0 || rtt < minimum`）ため、root の 2 回の実行で
  `min/avg/max = 1.000/0.500/1.000` と min が avg より大きく出た。QEMU では RTT が 0 ms・1 ms に丸まる（時計の分解能）。見た目だけの不具合。

## Resume point

完了。次は BUG-051（sshd-session の SIGSEGV）の Phase。
