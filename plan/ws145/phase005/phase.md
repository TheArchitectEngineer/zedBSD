<!-- awesome-plan project=zedbsd record=ws145-p005 -->

# ws145-p005: 印刷の Linux・FreeBSD の build と install と guest の確かめ

Parent: [WS145](../ws.md)
Status: in-progress — test-wait（2026-10-13 P2 q953: Linux・FreeBSD の build の規則を足し、Linux の build・install と host の試験まで済み。Debian の QEMU+KVM と FreeBSD 15 の guest の確かめは T1、Q1 経由）
Phase disposition: normal
Queue: q953（P2、2026-10-13。承認: ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」と WS145 の design.md §8 の p005）

## 範囲（design.md §8・§7）

- Linux・FreeBSD の Keiland の build と install に `keiland-printd`（libexec）と `printtest`（bin）を入れる（Makefile.linux・Makefile.freebsd、`keiland-linux.mk`・`keiland-freebsd.mk`、`KEILAND_LIBEXECDIR` は `$(KEILAND_PREFIX)/libexec` = `/opt/keiland/libexec`）。
- compositor の printers（`wayland/printers-shell.c`）と backend の print（`libkeiland-backend/print/print.c`）は既に Linux・FreeBSD の規則に入っていて、`KEILAND_LIBEXECDIR "/keiland-printd"` を `posix_spawn` で起こす。足りなかったのは daemon と試験の client の build だけ。
- design.md の D5: Linux・FreeBSD も同じ keiland-printd、CUPS は使わない。source（printd・backend）の変更は無し。

## 実装（2026-10-13 P2）

| file | 内容 |
| --- | --- |
| `userland/desktop/printd/Makefile.linux`・`Makefile.freebsd`（新） | `keiland-printd` を libexec に（source は zedBSD の package と同じ 5 つ、`-pthread`） |
| `userland/tests/printtest/Makefile.linux`・`Makefile.freebsd`（新） | `printtest` を bin に（libkeiland.so・libwayland-client.so） |
| `userland/desktop/keiland-linux.mk`・`keiland-freebsd.mk` | 上の 4 つを include |

## 確かめ（2026-10-13 P2、host）

| コマンド | 結果 |
| --- | --- |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws145-p005-linux -j16 all` | rc 0、warning 0。`libexec/keiland-printd`・`bin/printtest` ができる |
| 同じ build の `install DESTDIR=build/ws145-p005-dest` | rc 0、`/opt/keiland/libexec/keiland-printd`・`/opt/keiland/bin/printtest` が入る |
| `sh plan/ws145/tests/run-host-printd.sh build/ws145-p005/keiland-printd`（Linux の host、ASan/UBSan） | printd-test: PASS |
| `sh plan/ws145/tests/run-host-print-backend.sh build/ws145-p005/host-print-backend` | host-print-backend: PASS |

## 未実施（T1 へ依頼、Q1 経由）

- Debian 13 の QEMU+KVM の guest（WS131 の Linux の確かめと同じ、SSH と QMP の PNG）: main の tree で `keiland-linux.mk` の build と install、Keiland の session で `mock-printers.py`（guest の中か host の 10.0.2.2）に `printtest add ipp …`・`printtest print FILE.pdf` → job が done、受けた文書の SHA-256 が元と同じ、`ps` に `/opt/keiland/libexec/keiland-printd`。LPD も同じ。Settings の Printers の頁の PNG。
- FreeBSD 15 の guest（WS109）: guest の中で `keiland-freebsd.mk` の build（FreeBSD の build はこの host で未実施）と同じ確かめ。
- 制限: FreeBSD の build は host に環境が無く未確認（Makefile.freebsd は Linux の物と同じ形）。
