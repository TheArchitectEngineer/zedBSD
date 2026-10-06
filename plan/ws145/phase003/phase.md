<!-- awesome-plan project=zedbsd record=ws145-p003 -->

# ws145-p003: backend の print・compositor の printers・libkeiland の口・printtest

Status: in-progress（2026-10-07 q831 P2）
Disposition: normal
Parent: [WS145](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)

## 実装（正常系）

- backend（`userland/desktop/libkeiland-backend/print/print.c`、OS に依らない、zedBSD・Linux・FreeBSD の backend の source の一覧に追加）: `~/.config/keiland/printers.conf`（`next-id`・`printer <id> <ipp|lpd> <host> <port> <path|queue> <name>`・`default`、変更は `.lock` の flock の下で読み直して当て、一時 file から rename、他の session の変更は mtime で読み直す、id は使い直さない、最初の printer が既定、既定を消すと最小の id）、keiland-printd を posix_spawn（socketpair の子の端を fd 3、XDG_RUNTIME_DIR を環境に、signal を既定に・mask 無し）、行の約束（JOB+fd・CANCEL・NAME・BYE を送る、送れない行は queue、SPOOL・ACCEPTED・REJECTED・STATE・PATH・NAMED・IDLE を受ける）、job の状態（QUEUED→SENDING→WAITING→DONE・FAILED・CANCELLED、fd は ACCEPTED・REJECTED まで持つ）、printd の EOF で終わっていない job を FAILED daemon にし spool を消す、IDLE n で BYE n。API は `keiland-backend.h` の `kl_backend_print_*`（設計の §4 に path を足した add）。
- protocol（`kl-system-protocol.h`）: `kl_system_manager_v1` version 17、request 12 `get_printers`、capability `KL_SYSTEM_CAPABILITY_PRINTERS` 0x2000（printd が実行できる時）、`kl_system_printers_v1`（add(request, protocol, host, port, path)・remove・set_default・print(request, printer, title, fd)・cancel、event printer・job・done・queued・result）。
- compositor（`userland/desktop/wayland/printers-shell.c`、`system.c` の capability・get_printers・tick、`kwl.h`、3 つの Makefile）: 最初の printers の object で backend を開く、print は fd を最初に取る（無ければ EAGAIN）、title の検め（制御文字・C1・127 byte）、backend の request の番号と client の番号の待ちの表（16）、state を全ての printers の object に、queued を result の前に、saved が 0 なら NOT_SAVED。
- libkeiland（KL_VERSION 56 の案、`keiland.h`・`exports.map`・`system.c`・`system-view.c`・`system-private.h`・`system-protocol.c/h`）: `kl_system_printers_get`・`kl_system_print_jobs_get`・`_add`・`_remove`・`_set_default`・`_print`（file を開いて regular・大きさ・先頭 1024 byte の `%PDF-` を検め、title は不正な UTF-8 を EINVAL、制御文字を空白、127 byte で文字の境界で切る、fd を送って自分の fd を閉じる）・`kl_system_print_job_of`（queued の ring）・`kl_system_print_cancel`、`KL_SYSTEM_HAS_PRINTERS` 0x4000・`KL_SYSTEM_CHANGED_PRINTERS` 0x2000。
- `userland/tests/printtest/`（package `printtest`）: list・add・default・remove・cancel・print（job を終わりまで追う）。`PRINTTEST …` の行。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws145/tests/run-host-print-backend.sh` → PASS 17（backend＋host の printd＋mock: IPP の追加と NAMED の名前、LPD（queue 指定）の追加、同じ printer の拒否、既定（IPP）へ印刷して done、LPD へ印刷して done、知らない printer の拒否、既定の移動・削除を 2 つ目の backend が読み直す、id を使い直さない、両方の文書が元と一致）。
- host: `sh plan/ws131/tests/host-system.sh` PASS（printers-shell.c と print.c を足して link。host に printd が無いので capability は出ない）。
- zedBSD: `make ZEDBSD_CONFIG=plan/ws145/tests/config-amd64-print.mk BUILD=build/ws120-zed …/bin/wayland …/dynamic/libkeiland.so …/bin/printtest …/bin/keiland-printd` warning 0。style-check 0（新しい file と変えた所）。
- 未実施: compositor と libkeiland を通した通しの試験（host の protocol の試験は作っていない → backlog）。QEMU は T1。
