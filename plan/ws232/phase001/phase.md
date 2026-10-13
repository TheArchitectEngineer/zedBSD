<!-- awesome-plan project=zedbsd record=ws232-p001 -->
# ws232-p001: i386 の CPU と board、Retro の menu（と pc98 の build・起動）

Status: in-progress（2026-10-14 P1、q982）
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
  試験は `config/ci/config-pc98.mk` と同じく Desktop を外して書いた。i386 を選んだ時に Desktop を外すかどうかは未決（Q1・ユーザー）。

## 目標 4: config/ci/config-pc98.mk の build

（進行中）

## 目標 5: PC-98 の QEMU で login prompt まで

（進行中）
