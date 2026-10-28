<!-- awesome-plan project=zedbsd record=ws309-p005 -->

# ws309-p005 io-file: 設定の file の読み書きを thread へ

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §2 T7・§4.11・§7 #4・#13・#19。Queue: q1662-i02・q1662-i03（P1）。
- 状態: cleared（2026-10-29 03:1x P1。account の変更の QEMU は未実施、実機は未実施）。
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

- 言語の catalog（libkeiland translate.c・keiland.h、wayland/language.c）: libkeiland に `kl_tr_read(domain, language, &catalogs)`（任意の thread で別の record に読む、使っている text に触れない）・
  `kl_tr_use`（text を使う thread で入れ替えて record を解放）・`kl_tr_discard` を足した（KL_VERSION 96、exports.map）。`kl_tr_open_directory` は同じ内部の読み（`tr_state_read`・`tr_state_use`）の上に作り直し、意味は同じ。
  compositor: 最初の `ui.language`（起動、READY の前）は今どおり同期で `kl_tr_open`、以後の変更は io-file の job（key `KWL_IO_KEY_LANGUAGE`、未着手の古い読みを置き換える）。
  `server->language_asked`（最後に頼んだ言語の写し、失敗の後は使っている言語）と違う読みは当てずに捨てる（読み中に別の言語が頼まれた時）。log は `KWL LANGUAGE language=.. from=.. error=.. by=io-file|event-loop`。
- apps.conf（home.c）: 一覧を `struct home_list` に読む `home_list_read`（file・既定の一覧・各 command の path の `access()`、一覧だけに触れる）を io-file の job で走らせ、done（main）で `home_list_take`
  （login の session の Lock Screen・Power Off を足し、`home_apps` へ写す、`KWL HOME read apps=n by=io-file`）。読み中は Home に icon が無い（今の「読む前」と同じ）、`kwl_home_open_app`（時計の Calendar）は EAGAIN で答える。
  thread が無い・memory が無い時は今どおり main で読む。`home_prepare` は一覧が来た後の tick で今どおり。
- account（system.c）: 専用の pthread と lock の `system_account_job` をやめ、change ごとに確保した job を io-file に出す（`system_account_busy` で 1 つずつ）。password と operation は run の後と drop で wipe。
  答え（refused・result）は done で今と同じ。thread が無い時は main で行う（今は専用の thread で行っていた。io-file はいつも起動するので実際には通らない）。
- network の鍵の保存（system.c）: `SYSTEM_JOB_SAVE_KEY` を network の thread から外し、`system_key_save` が鍵の写しを持つ job を io-file に出す（run が保存の後に wipe、drop も wipe）。
  details の読み（network の thread、p004 で io-device へ）と同時に走らない順を保つ: details の読み中は鍵が待ち（QUEUED）、鍵の保存中（SAVING）は details を始めない。done の後は今どおり PROFILES → join。
- printers（printers-shell.c・print.c）: `kwl_printers_prepare`（main.c、io の起動の直後、READY の前、greeter でない時）で backend を open（初回の `print_load` は起動の例外の中）して hosted にする。
  `kl_backend_print_can` は hosted なら最後の check（io-file の 1 s の job、`kl_backend_print_check_run` が program の `access()` も見る）の値、hosted でなければ今どおりその場で見る（keiland-backend.h の引数の const を外した）。
  log `KWL PRINTERS prepared open=.. hosted=.. can=..`。

## 受け入れ

- `build/amd64/bin/wayland` warning 0。print.c が host の cc で `-Wall -Wextra -Werror` を通る。
- host: `sh plan/tools/settings/host-store.sh`（既存、46 件）、`sh plan/ws309/tests/io-file/run.sh`（fsync を 300 ms に wrap、give・submit が 5 ms 未満、merge は lane の thread、finish が後の変更を書く、
  走らなかった give は finish が全部書く、1 session に 1 つ）、`sh plan/ws309/tests/print-file/run.sh`（hosted は thread も stat も無し、別 thread の write の後に update が答える、check が他の session の変更を読む、
  変更の後の古い check は当てない、close が残りを書く）。いずれも ASan+UBSan と TSan。
- QEMU Venus: `plan/ws309/tests/io-file-guest.sh`（KWL THREADS に io-file、display-mode と display-rotate の result が held → `saved by=io-file ticket=n` → error=0 の順、displays.conf に mode=extended）。
- 残りの 4 件（q1662-i03）: build の warning 0（amd64、arm64・riscv64 も）。host: `sh plan/ws309/tests/tr-read/run.sh`（thread の `kl_tr_read` の間も main の text は変わらない、use で入れ替わる、
  拒んだ名前と discard は何も変えない、`kl_tr_open` は今どおり）、print-file に「hosted の can は最後の check の値」を追加。いずれも ASan+UBSan と TSan。
  QEMU Venus: `plan/ws309/tests/io-read-guest.sh`（KWL THREADS に io-file、起動の後の言語の変更が by=io-file・error=0 で ja → en、最後は en、Home の一覧が by=io-file で来て prepared はその後、
  printers が READY の前に open・hosted、鍵の保存が by=io-file・error=0 で compositor の log に鍵が出ない）。

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
- 2026-10-29 02:5x〜03:1x P1（q1662-i03、branch `agent/p1-ws309c`、基点 9402d4c74、38fbef081 と次の commit）: 言語の catalog・apps.conf と各 program の access・account・鍵の保存・printers の open と can（上の設計）。
  `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/wayland build/amd64/bin/keiland-settings build/amd64/bin/keiland-system` exit 0・warning 0、
  `config-rpi4.mk BUILD=build/arm64` と `config-visionfive2-keiland.mk BUILD=build/riscv64` の `bin/wayland` も exit 0・warning 0。
  host: tr-read・print-file・io-file は 2 つの sanitizer とも PASS、host-store 46 passed。
  QEMU Venus（image は前の worktree `p1-ws309` の build/ws309/img/hdd-image.img、/bin/wayland・/lib/libkeiland.so（KL_VERSION 96 の新しい export が要る）・/bin/keiland-settings・/bin/keiland-system を差し替え）:
  1 回目（io-read1）は Home の一覧が読まれず FAIL（`--testing` だけでは glass の tick が走らない。試験の script に `--glass` を足した）、2 回目（io-read2）PASS、
  鍵の保存を足した 3 回目 `sh plan/ws309/tests/io-read-guest.sh build/ws309/io-read3 <image> build/ws309/bins/wayland-p005b build/ws309/bins/libkeiland-p005b.so build/ws309/bins/keiland-settings build/ws309/bins/keiland-system` → `io-read-guest: PASS`
  （compositor の出力: `KWL PRINTERS prepared open=1 hosted=1 can=0` が READY の前、`KWL HOME read apps=9 by=io-file` の後に `KWL HOME prepared names=9`、`KWL LANGUAGE language=ja … by=io-file`・en・en、
  `KWL SYSTEM network key saved ssid=p1-ws309-test error=0 by=io-file` の後に PROFILES と join の step）。
- 未実施: account の変更（set_password・administer）の実行（試験の probe に命令が無い。build と読みだけ）、実機、Linux・FreeBSD の build（language.c・home.c・system.c・printers-shell.c・print.c・translate.c は共通）。
