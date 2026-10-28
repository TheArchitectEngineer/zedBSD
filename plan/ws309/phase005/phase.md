<!-- awesome-plan project=zedbsd record=ws309-p005 -->

# ws309-p005 io-file: 設定の file の読み書きを thread へ

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §2 T7・§4.11・§7 #4・#13・#19。Queue: q1662-i02（P1）。
- 状態: in-progress（2026-10-29 02:4x P1: displays.conf・desktop.conf の Log Out・printers.conf は済み。言語の catalog・apps.conf の読み、account・鍵の保存は未着手）。
- 出典: ユーザーの決定 3「I/O系はまとめていいです。」と ws.md の構成の表（I/O-2 file）。
- 依存: p002（lane）。

## 設計（実装した形）

- `wayland/io.c`: io-file の lane（1 本、FIFO）を `kwl_io_start` で常に起動（io-device は今のまま zedBSD だけ）。`kwl_io_file_submit`（thread が無ければ ENOTSUP、呼ぶ側が自分で行う）、
  `kwl_io_take` で io-device と io-file の両方を取る。終了は main.c の event loop の後で `kwl_io_file_finish`（全ての job を走らせて取る → stop）を settings の close の前に。
  key は `KWL_IO_KEY_DISPLAYS`（kwl.h、未着手の古い書き込みを置き換える）。
- displays.conf（heads.c）: 専用の writer thread と同期の `heads_save`（main で fsync）を消し、全ての書き込み（apply・shown・回転・move の失敗の戻し）を io-file の job に。
  書き込みごとに ticket（`heads_saving.given`）、job の done で `written` と error を記録し `kwl_displays_written` を呼ぶ。置き換えられた job の ticket は後の job が覆う。
  `kwl_heads_config_ticket`・`kwl_heads_config_settled`（compose.h）。`kwl_heads_config_close`（compose_close の中の drain）は削除。
- `saved` の答え（displays-shell.c）: 1 つの `displays_pending` を list（`struct displays_held`）にし、result は「choice を含む ticket の書き込みが終わる」と「output の move の最初の frame」の
  両方を待って答える（move は今どおり最新の 1 つ、3 s で applied）。apply・set_brightness・set_shown・set_rotation の 4 つ。object が消えれば忘れる。memory が無い時はすぐ答える（log）。
  答えの意味は今と同じ（written = file が choice を持つ）、答えが書き終わりまで遅れるだけ（design §7 #4）。
- desktop.conf の Log Out（settings.c・settings-store.c）: store に `kwl_settings_store_save_give`（main で変更を写す）・`_save_run`（thread で merge）を足し、Log Out は io-file の job で merge。
  `kwl_settings_store_finish` は given の writer が走った（done）なら書けた分を saved にし、残りを書く（終了の後なので待ってよい）。thread が無い時は今の `save_later`（store の thread）。
  `save_later` は plan/tools/settings/host-store.sh が使うので残す（中身は give の上に作り直し）。
- printers.conf（libkeiland-backend/print/print.c・keiland-backend.h・printers-shell.c）: backend に host の口 `kl_backend_print_host`・`_write_wanted`・`_write`・`_check_begin/_run/_end` を足した。
  hosted の backend は自分の writer thread を作らず、update は file を stat しない。compositor は最初の printers object の時に hosted にし、tick ごとに変更が待てば write の job（同時に 1 つ）、
  1 s ごとに check の job（同時に 1 つ）を io-file に出す。check は begin の時の「取り込んだ変更の数」（`made_generation`）を持ち、その後に変更が取り込まれていれば end で当てない（古い表で新しい表を上書きしない）。
  close は待っている変更を書く。答え（saved）は今どおり書いた後。

## 受け入れ

- `build/amd64/bin/wayland` warning 0。print.c が host の cc で `-Wall -Wextra -Werror` を通る。
- host: `sh plan/tools/settings/host-store.sh`（既存、46 件）、`sh plan/ws309/tests/io-file/run.sh`（fsync を 300 ms に wrap、give・submit が 5 ms 未満、merge は lane の thread、finish が後の変更を書く、
  走らなかった give は finish が全部書く、1 session に 1 つ）、`sh plan/ws309/tests/print-file/run.sh`（hosted は thread も stat も無し、別 thread の write の後に update が答える、check が他の session の変更を読む、
  変更の後の古い check は当てない、close が残りを書く）。いずれも ASan+UBSan と TSan。
- QEMU Venus: `plan/ws309/tests/io-file-guest.sh`（KWL THREADS に io-file、display-mode と display-rotate の result が held → `saved by=io-file ticket=n` → error=0 の順、displays.conf に mode=extended）。
- 残り（この Phase の範囲）: 言語の catalog の読み直し（settings.c の `ui.language` → `kwl_language_set` → libkeiland の `kl_tr_open`、thread で読み main で差し替える口が libkeiland に要る）、
  apps.conf の読み（home.c `home_read_apps`、初めて Home を開く時の fopen）と `home_prepare` の `access()`、account の helper（system.c の job の thread）と network の鍵の保存を io-file へ、
  printers の open の初回の `print_load` と `kl_backend_print_can` の `access()`（今は main、小さな file）。

## 記録

- 2026-10-29 02:3x P1（d5fb0bd08）: io-file の lane・displays.conf・`saved` の保留・desktop.conf の Log Out。`make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/wayland` exit 0・warning 0。
  host-store 46 passed、io-file の host 試験は 2 つの sanitizer とも `host-io-file: PASS`。
  QEMU Venus（image は前の worktree `p1-ws309` の build/ws309/img/hdd-image.img を guest.py が写して使用、/bin/wayland と /bin/keiland-system を差し替え）:
  `sh plan/ws309/tests/io-file-guest.sh build/ws309/io-file2 <image> build/ws309/bins/wayland-p005a build/ws309/bins/keiland-system` → `io-file-guest: PASS`
  （compositor 自身の出力: `KWL THREADS count=2 names=io-file,io-device`、ticket 1〜5 の各 result が held の後に `saved by=io-file ticket=n errno=0`、probe の 5 件とも error=0）。
  1 回目（io-file1）は試験の script が display の key（空白を含む）を切り詰めて回転が ENXIO、script を直して 2 回目。guest の HOME は /tmp で fsync は 0〜1 ms（遅い fsync は host 試験だけで見た）。
- 2026-10-29 02:4x P1（c3b274d51）: printers.conf の host の口と io-file の job。wayland の build exit 0・warning 0、print.c の host の syntax OK、print-file の host 試験は 2 つとも `host-print-file: PASS`。
  printers の QEMU は未実施（printd が image に無い、keiland-system の printers の command も無い）。
- 未実施: 実機、riscv64・arm64 の build、Linux・FreeBSD の build（print.c・printers-shell.c・io.c は共通）。
