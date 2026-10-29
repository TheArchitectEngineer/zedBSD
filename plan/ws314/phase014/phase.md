<!-- awesome-plan project=zedbsd record=ws314-p014 -->

# ws314-p014 Preview の API から fd・pid を外す

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §4.5（review 14）。Queue: q1690 / q1690-i02（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。build・host・QEMU（zedBSD）で確かめた）
- Disposition: normal

## 目的・受け入れ

- Preview の呼び手の API（`preview/client.h`、Files・Phone・Photos・Settings が使う）から、app に見える fd と pid を外す:
  `preview_start(path, int output, ...)` の output の fd、`struct preview_job` の `pid_t pid`、`struct preview_pending` の `int output`。
  `client.c` の `waitpid`・`kill` を OS ごとの spawn の file へ移す。sandbox（資源の上限つきの起動）は Preview の物のまま（kl_process にしない）。
- build warning 0（zedBSD 3 arch・Linux）、host の試験、QEMU で thumbnail（design §12）。

## 実装

- `preview/client.h`: `preview_start(const char *input, const char *output, ...)`（output は path。client が 0600 で作り・空にして開く）。
  `struct preview_job` は `struct preview_child *child`（system の不透明な記録、終われば NULL）と `int apart`（子で作ったか）。
  `struct preview_pending` の output は `struct preview_output *`（client.c の不透明な記録）。`preview_read` は client.c の static
  （`client_read`）に、`preview_spawn` の宣言は新しい内部の header へ。`<sys/types.h>` を外した。
- `preview/spawn.h`（新、client.c と spawn.c だけ）: `preview_spawn(input, output, request, struct preview_child **, status)`、
  `preview_child_ended`（待たずに見る。終われば記録を放し status）、`preview_child_end`（SIGKILL・刈り取り・放す）。
- `preview/zedbsd/spawn.c`・`linux/spawn.c`: `struct preview_child { pid_t pid; }` と上の 2 つ（`waitpid(pid, WNOHANG)`、EINTR は
  走っているとみなす）。`freebsd/spawn.c` は同じ process で作るので子は無く、2 つは呼ばれない（終わったと返す）。
  設計の「POSIX 共通の `preview/posix/`」にはしなかった: 新しい file を足すと client.c を build する 11 の Makefile と plan/tools の
  host の script 2 本（files の host-build.sh、media の run-host-mediastorage.sh）の一覧も変わるため。重なりは 2 つの関数（約 50 行）。
- `preview_info_read(int input, ...)` は fd のまま: Photos は受け取った media（KL_VERSION 82 の呼び手の fd）も同じ道で取り込み、
  file の I/O は §9 D の境界の外（app の platform の部分）。
- Files（`thumb.c`）: 一時 file を path で渡す（自分で開かない）。log の `pid=` を外し、終わりの行に `child=1`（子で作った）／`child=0`
  （FreeBSD の同じ process）。
- log を読む試験（Q1 の指示で grep し、同じ形に直した。2026-10-30 Q1 が所有に足した）:
  - `plan/tools/aat/scenarios/helpers_preview.py`（59 行目の `not line.endswith(" pid=0")` → `line.endswith(" child=1")`、10 行目の説明）
  - `tests/scenarios/apps/files/thumbnails.md`（20 行目の正解の `pid=<0 でない>` → `child=1`）
  - 他に `ZFILES THUMB` を読む物（`plan/tools/files/files-p007.sh`・`files-p010.sh`、`plan/ws127/tests/files-p002.sh`）は pid を見ないので変えない。

## 確かめ（2026-10-30）

- Linux（all、`build/ws314-p014-linux`）: make 0・warning 0。
- host の試験 `plan/ws314/tests/preview-test.c`（新、`sh plan/ws314/tests/preview-test.sh build/ws314-p014-linux`、client.c と linux/spawn.c、
  Linux の keiland-preview（seccomp）、ASan・UBSan）: 絵を作って読む（待つ・待たない）、path の file へ書く、時間切れの子を終える、
  取りやめ、普通の file でない入力の EINVAL、--info、刈り残しが無い。PASS（10 件）。
- zedBSD（`build/ws314-p014-build-all.sh`）: amd64（desktop 19＋tests 9、mediastorage・phone・photos・settings・files を含む）・arm64・riscv64・
  installer の image の installer: make 0・warning 0。FreeBSD の `freebsd/spawn.c` は host で `-fsyntax-only`。files の host-model、phone の
  run-host-phone.sh（client.c と freebsd/spawn.c）も PASS。
- QEMU（2026-10-30、`sh plan/ws314/tests/guest.sh build/p2-q/run9 build/ws314-img`、image は
  `plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-guest.mk build/ws314-img`（files・textedit を足した）、compositor は
  `/bin/wayland --testing --glass` 1280x800、判定は SSH で取った各試験の結果の行と app の log（console は読まない））: `ws314-guest: PASS`。
  Files の folder の PNG の thumbnail が `ZFILES THUMB path=/tmp/ws314demo/picture.png error=0 width=256 height=78 cached=0 status=0 child=1`
  （zedBSD の sandbox_spawn の子で作り、`preview_child_ended` で刈った）。
