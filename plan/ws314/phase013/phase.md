<!-- awesome-plan project=zedbsd record=ws314-p013 -->

# ws314-p013 `kl_launch` と Files・Settings・PDF Viewer・Terminal の新しい窓の移行

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §5。Queue: q1690 / q1690-i02（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。build・host・QEMU（zedBSD）で確かめた）
- Disposition: normal

## 目的・受け入れ

- design.md §5 の `kl_launch`（切り離した起動、KL_VERSION 100）。fd・pid を公開しない。Files の「開く」（`fm_apps_launch`・`fm_apps_spawn`）、
  Settings の Files の起動、PDF Viewer の Notes の起動（と `waitpid(-1)`）、Terminal の新しい窓を移す。
  終わったら `plan/tools/keiland-os-boundary/app-allow.tsv` の terminal・settings・pdfviewer・files の A4 の行を外す。
- build warning 0（zedBSD 3 arch・Linux）、host の試験、QEMU で Files の「開く」と Terminal の新しい窓。

## 実装

- 公開: `keiland_process.h` に `struct kl_launch_options`（argv か command のどちらか一つ、environment、directory、`from` の窓）と
  `kl_launch(app, options)`。`keiland_version.h` を 100。exports.map（`kl_launch`）。新しい header は作らない（「app が始める program」の component）。
- `libkeiland/launch.c`（OS に依らない）: 引数の検査、`from` があれば `kl_activation_token`（app の display と窓の surface、
  app_id は NULL）で token を取り `XDG_ACTIVATION_TOKEN=` を環境の変更の後ろに足す（compositor が出さなければ無し）。
- `platform/process-posix.c` の `keiui_launch_start`: path（PATH の検索）・argv・envp・報告の pipe（CLOEXEC）を親で作る。`fork` → 中間の子は
  `setsid`・`fork` して `_exit(0)`。孫は signal を既定へ・mask を空（`process_child_signals`、terminal の子と共有にした）、報告の端を fd 3 へ寄せ
  `closefrom(4)`、`/dev/null` を 0 へ、`chdir`、`execve`。失敗は errno を報告の pipe へ書いて `_exit(127)`。親は中間の子を `waitpid`（pid 指定）で
  刈り、pipe を読む（空で終われば exec した）。thread は 0 本。command は `/bin/sh -c`。
- Files（`apps.c`）: `fm_apps_attach(app, window)`（file の static に app と窓。Files の main と Image Viewer の share.c が呼ぶ。Image Viewer は
  `apps.c` を一緒に build する）。`fm_apps_launch` は `@terminal ` を argv（`/bin/terminal --command=...`）、他を command で `kl_launch`。
  `fm_apps_spawn` は argv。どちらも `from` は窓。`apps_run`・`apps_exec`（二重の fork と `/dev/null`・`APPS_DESCRIPTORS`）を外した。
  変更点（design §5）: 開いた program の stdout・stderr は `/dev/null` ではなく Files の物を受け継ぐ。
- Settings: Welcome の終わりの Files を `kl_launch`（`from` は NULL、今と同じく compositor が置く）。`<spawn.h>`・`environ` を外した。
- PDF Viewer: Notes を `kl_launch`（`from` は窓）。loop の `waitpid(-1, WNOHANG)` を外した。log の `pid=` を外した（読む試験は無い）。
- Terminal: 新しい窓を `kl_launch`（`from` は窓）。失敗は `ZTERM NEW-WINDOW failed run=... errno=...`。`<sys/wait.h>` を外した。
- `app-allow.tsv`: A4 の settings/main.c・pdfviewer/main.c・files/apps.c・terminal/main.c と A1 の files/apps.c（`/dev/null`）の 5 行を外した。

## 確かめ（2026-10-30）

- Linux（all、`build/ws314-p013-linux`）: make 0・warning 0。
- zedBSD（`build/ws314-p013-build-all.sh`）: amd64（desktop 19＋tests 9）・arm64（19）・riscv64（19）・installer の image の installer:
  make 0・warning 0。
- host の試験 `plan/ws314/tests/launch-test.c`（新、`sh plan/ws314/tests/host-test.sh launch build/ws314-p013-linux`）: command（環境の設定・
  削除と directory）、argv（PATH の検索、空白を含む引数、`$PPID` が app ではない）、stdin が空、descriptor を渡さない（pipe の書き端を
  受け継げば `sleep 3` の間 EOF にならない）、signal の既定（app が SIGINT を無視しても子は SIGINT で終わる）、誤り（ENOENT（PATH・path・
  directory は子から報告）、EINVAL（program 無し・空の command・両方・どちらも無し・app 無し））、刈り残しが無い（`waitpid(-1)` が ECHILD）。
  PASS（22 件）。post・process・instance も PASS。TSan の libkeiland（`build/ws314-p013-tsan`）でも 4 本 PASS・警告 0。
- `plan/tools/keiland-os-boundary/check.sh`: FAIL の集合は前（`build/ws314-boundary-after.txt`）と同じ（行の番号の移動だけ）。移した 4 file に
  FAIL は無い。
- QEMU（2026-10-30、`sh plan/ws314/tests/guest.sh build/p2-q/run9 build/ws314-img`、image は
  `plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-guest.mk build/ws314-img`（files・textedit を足した）、compositor は
  `/bin/wayland --testing --glass` 1280x800、判定は SSH で取った各試験の結果の行と app の log（console は読まない））: `ws314-guest: PASS`。
  launch-test 22 件 PASS（zedBSD の上）、Terminal t1 の窓を click して Ctrl+Shift+N で `ZTERM START run=t1-new`（新しい窓の log は
  kl_launch で t1 の出力を受け継ぐ）、Files の double click で `ZFILES LAUNCH name=Text Editor command=/bin/textedit '/tmp/ws314demo/notes.txt'` と
  textedit の process、compositor の log に Files の activation token（`reason=focus granted=1`）。post・process・instance も PASS。
- 途中の失敗と直し: launch-test が zedBSD で kl_app_open の ENOTSOCK（試験の誤り: zedBSD の getenv は同じ buffer を返し、WAYLAND_DISPLAY の
  pointer が後の getenv で上書きされた。strdup で保つ）。Ctrl+Shift+N が届かない（compositor の menu の shortcut は `--glass` の時だけ
  `seat.c` が見る。試験の compositor に `--glass` を足した。既存の挙動で Bug ではない）。
- host の Files の試験の追従（Q1 の承認）: `plan/tools/files/host-launch.c`（host の kl_launch の代役）と `host-build.sh`、files-model PASS。
  `plan/ws128/tests/host-share.*` は参照が無いので削除（ws128 phase005 の link を平文に）。
