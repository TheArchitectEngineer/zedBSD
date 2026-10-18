<!-- awesome-plan project=zedbsd record=ws232-p001 -->
# ws232-p001: i386 の CPU と board、Retro の menu（と pc98 の build・起動）

Status: cleared（2026-10-18 Q1: ws.md の表に記録済みの判定に合わせた。表: cleared（2026-10-14））（旧: in-progress（2026-10-14 P1、q982））
Disposition: normal
Parent: [WS232](../ws.md)
Queue: q982
依存: 無し

範囲と確かめは [ws.md](../ws.md) の目標 1〜5（4・5 は 2026-10-14 ユーザーの追加、Q1 経由）。

## 結果（目標 1〜3: menuconfig）

- `tools/menuconfig.py`
  - `MENU_CPUS` に i386 を足した。i386 は board が 2 つ（PC-9800 → platform `pc98`、PC/AT → platform `i386`）。
    CPU の menu は CPU を 1 回ずつ（x86_64・arm64・i386）出し、CPU を選ぶとその最初の board（i386 は ユーザーの順の PC-9800）。
    同じ CPU を選び直しても board は変えない。board が 2 つ以上の CPU では「Board」で選べ、1 つの CPU は従来どおり
    「The board follows the CPU.」。`PLATFORMS`・`BOARD_VARIANTS` の i386・pc98 の定義（variant `default`）はそのまま使う。
  - 主の menu の一番上に「Retro」（全 CPU）。`RETRO_PROGRAMS = (Xzed, zwm, zshell, zterm)` をこの順に名前で出す
    （label は命令の名前のまま）。Firmware と同じ 1 つずつの切り替え（`edit_programs`、`edit_program_group` はその wrapper）。
  - `in_menu` は program も受け、Retro の 4 つを menu に出る物として扱う（外す時の依存の扱い）。zgears・glxtest と Tests は
    従来どおり config.mk の直接記述だけで、load→save で保たれる。
- 試験 `plan/tools/menuconfig-target-host-test.py`（`make menuconfig-host-test`）に `check_i386_and_retro` を足した:
  CPU の menu が `x86_64, arm64, i386`、i386 → pc98、Board で PC/AT ↔ PC-9800、x86_64 の Board は選択でない、
  amd64・rpi4・i386・pc98 で Retro が主の menu の一番上で 4 つを選び外せる、zgears は menu に出ず config.mk で保たれる、
  menu が書いた i386・pc98 の config.mk（Retro 入り、Desktop 無し）が `make -n` で解決する。
- 結果: `make menuconfig-host-test` → `MAC-T001 menuconfig round-trip: PASS (... i386 PC/AT and PC-9800 with Retro)`（約 19 秒）。
- commit 966a95a0c。

### 分かった事（判断が要る）

- menuconfig の既定（amd64 の既定と同じ選択）のまま CPU を i386 にすると、Desktop の既定の program（files・settings・wayland など）が
  書かれ、i386 の `make -n` は `No rule to make target 'build/pcat/bin/keiland-preview'`（ほか libvulkan・libkeiland・libEGL など）で
  止まる。Desktop の program は Makefile が全 CPU を「*」にしている（2026-10-10 ユーザー）が、i386 の build の規則は無い。
  試験は `config/ci/config-pc98.mk` と同じく Desktop を外して書いた。i386 を選んだ時に Desktop を外すかどうかは未決（Q1・ユーザー）→ 下の決定で解決。

### 決定と対応: i386 では Desktop を自動で外す（2026-10-14 ユーザー、クリック「i386 では Desktop を自動で外す」）

- `tools/menuconfig.py`
  - `NO_DESKTOP_ARCHITECTURES = ("i386",)` と `desktop_builds(platform)`。i386（PC-9800・PC/AT）では主の menu に
    Desktop の項目を出さない（Retro・CPU / Board・Boot Option・Drivers・Development・Base・Packages・Firmware）。
  - `builds_for(platform)`: その platform で build する program の集合。従来の platform の指定（`applies`）に加え、
    Desktop を build しない CPU では Desktop の group の program と、Desktop の package を直接・間接に要る program
    （今の表では `packages/desktop` の gtk4 が `desktop/libwayland` を要る）を除く。`save`（書き出し）・`group_rows`
    （Base・Packages・Firmware の menu）・`retro_rows` がこれを使う。結果として i386 の config.mk は
    `config/ci/config-pc98.mk` と同じ形（Retro・Base・Packages・Firmware、Desktop 無し）になる。
- amd64・arm64 に戻した時の既定の扱い: 既存の CPU の切り替えの規則（「build しない選択は menu の状態に残し、書き出さない。
  target を戻すと保たれる」、`save` の注記）に合わせた。i386 にしている間も Desktop の選択は menu の状態に残るので、
  同じ session で x86_64・arm64 に戻すと Desktop の選択（既定なら既定の program）がそのまま書かれる。
  i386 で保存して menu を閉じ、その config.mk を開き直してから戻した場合は、file に Desktop が書かれていないので
  Desktop は空のまま（amd64 だけの試験の program など、他の platform の限られた選択と同じ扱い）。必要なら Desktop の All で戻す。
- 試験 `check_i386_and_retro` に足した事:
  - 主の menu に Desktop が出る（amd64・rpi4）・出ない（i386・pc98）。
  - 既定（`menu.defaults()`）から CPU の menu で i386 だけを選び（PC-9800）、既定のまま保存 → Desktop の program と
    Desktop を要る program が書かれず、`make -n ZEDBSD_CONFIG=<その file>` が解決する。
  - 続けて CPU を x86_64 に戻して保存 → Desktop の既定の program（既定 y の全部）が書かれる。
  - Retro を足した i386 PC/AT・pc98 の config.mk（以前は試験の中で Desktop を手で外していた。今は外さない）に Desktop が無く、
    `make -n` が解決する。
- 結果: `make menuconfig-host-test` → `MAC-T001 menuconfig round-trip: PASS (6 targets, 3 amd64 Variants, obsolete capacity
  removed, package requirements resolved, the WS193 menu, i386 PC/AT and PC-9800 with Retro and without the Desktop)`（約 18 秒）。
  既定のまま i386 にした config.mk の `ZEDBSD_USER_PROGRAMS` は 186 語、x86_64 に戻した物は 232 語。
- commit 7401f3fb8（P1 の worktree、branch agent/p1）。

## 目標 4: config/ci/config-pc98.mk の build

- 1 回目: `make -k -j32 ZEDBSD_CONFIG=config/ci/config-pc98.mk disk-image`（P1 の worktree、log
  `/home/awe/zedBSD-worktrees/p1/build/ws232/pc98-build-1.log`）→ 失敗は 1 つだけ:
  `src/rtld/rtld.c:3278:1: error: unused function 'tlsdesc_slot' [-Werror,-Wunused-function]`。
  `tlsdesc_slot`（WS140 U3 の TLSDESC の引数の置き場）は amd64・arm64 の TLSDESC の処理（`#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64)`）
  からだけ呼ばれ、i386 では使われない。
- 直した所: 宣言と定義を同じ `#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64)` で囲んだ（amd64・arm64 の code は変わらない）。commit c826dc5da。
- 2 回目: 同じ command（log `/home/awe/zedBSD-worktrees/p1/build/ws232/pc98-build-2.log`）→ rc=0、compiler の warning 0
  （log の warning は gmake の jobserver の 1 行だけ）。image `/home/awe/zedBSD-worktrees/p1/build/pc98/hdd-image.img`
  （185597952 byte、sha256 `969de239325a8c974b5aa965bc4cb56d93053f82f76a20d0bc0825c3cf0f638f`）。rootfs に Retro の
  `/bin/Xzed`・`/bin/zwm`・`/bin/zshell`・`/bin/zterm` が入っている。WS227 の library の移動による失敗は出なかった
  （config-pc98.mk は Desktop の program を持たない）。

## 目標 5: PC-98 の QEMU で login prompt まで（T1 への依頼）

P1 は QEMU を起動しない。次を T1 に流してもらう（Q1 経由）。image は上の目標 4 の物（P1 は T1 の結果まで `build/pc98/` を作り直さない）。

- 道具: 登録済みの [pc98-boot.py](../../tools/pc98-boot.py)（`boot-test.sh` に PC-98 の mode が無いため。Master の Tools 節）。
  image を一時の directory に複写して起動し、text VRAM（`pmemsave 0xa0000 0x2000`）で `login: ` を待ち、root/root で login して
  `uname -a` を打ち、画面を text と PNG で残す。
- command（main の checkout か P1 の worktree のどちらからでも同じ）:

      timeout 900 python3 plan/tools/pc98-boot.py \
          /home/awe/qemu-pc98/build/qemu-system-i386 \
          /home/awe/zedBSD-worktrees/p1/build/pc98/hdd-image.img \
          <OUTPUT>

  中で起動される QEMU（`plan/tools/clock/pc98-sleep.py` の `Guest`）:
  `qemu-system-i386 -M pc9821,pegc=off,coregraph=on -cpu 486 -smp 1 -m 64M -display none -serial none -no-reboot
  -drive if=ide,format=raw,file=<複写した image> -monitor unix:<dir>/monitor.sock,server=on,wait=off`
  （machine は PC-9821、disk は IDE の raw の 1 台）。
- 判定: 終了 status 0、`<OUTPUT>/login.png`（と `login.txt`）に `login:` の prompt、`<OUTPUT>/command.png` に `uname -a` の出力
  （zedBSD、i386）。PNG は text VRAM から描いた物なので、実際の画面も要るなら、`login:` が出た所で monitor に
  `screendump -f png <OUTPUT>/login-screendump.png` を送って撮る（QEMU 11.0.93、`-display none` でも撮れる）。
  console・serial の log では判定しない。
- 上限: 起動の待ちは pc98-boot.py の 300 秒、全体は 900 秒。変更なしの再試行は 3 回まで。

（結果は T1 の後に Q1 が記入・判定する。それまで Phase は cleared にしない。）
