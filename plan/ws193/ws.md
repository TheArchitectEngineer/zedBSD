<!-- awesome-plan project=zedbsd record=ws193 -->

# WS193: make menuconfig のメニュー階層の作り直しと Build boot image（進捗表示）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p002・p004〜p011 cleared、p002 はユーザー「menuconfigはOK」（2026-10-10）、RPi4 の実機は 2026-10-13 のユーザーの UAT の一括「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。p003 の規約は WS の外の一括の見直しへ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

- `make menuconfig` の toplevel を下の階層にする（CPU / Board、Boot Option、Development、Base（All / Select）、Desktop（All / Select）、Packages（userland/packages の分類の階層）、空行、Build boot image、空行、Exit）。Board は CPU に従う（x86_64 → UEFI+ACPI、arm64 → RPi4）。
- Build boot image は進捗の bar と今 build している物の名前を出す。この menu に限り `-j$(nproc)`。
- 既存の config.mk の形と互換（既存の変数・既定値を壊さない）。

完了の条件: 新しい階層で保存した config.mk で image が build でき boot-test で login prompt、`make menuconfig-host-test` が PASS、進捗の画面をユーザーに見せる、変えた code の規約の全文の見直し。

## ユーザーの決定

- 2026-10-09（由来、原文）
  ```
  make menuconfigの実装です。メニュー階層を変更します。

  * toplevel
     * CPU / Board
        * CPU (x86_64, arm64)
        * Board (x86_64->UEFI+ACPI, arm64->RPi4)
     * Boot Option
        * Graphical boot (No kernel messages on boot console)
        * Graphical login (Automatically starts Keiland)
        * Mirror kernel messages to serial port
     * Development
        * Install development files (/usr/include, .so links, .pc, .a)
     * Base
        * All
        * Select
           * userland/baseの項目を選べる
     * Desktop
        * All
        * Select
           * userland/desktopの項目を選べる
     * Packages
        * userland/packages/**/*を階層的に
     * 一行空ける
     * Build boot image
     * 一行空ける
     * Exit

  Build boot imageはプログレスバーを表示して、何をビルド中なのかも表示する。このメニューに限り、nprocの数だけ-jしてOKです。
  ```
- 2026-10-09（クリック）「両方ベータ2」。原文に無い項目（Variant の disk の形・kernel option・driver の選択・試験の hook・Noct の GPU accel）は「menu から外す」（config.mk の直接の記述だけ、読んだ値は保つ）。
- 2026-10-09「Firmwareはトップレベルに階層を作る。X11とTestsはメニューから削除し、直接記述のみにする。」
- 2026-10-10「menuconfigはOK」（p002）。
- 2026-10-10（arm64 で FFmpeg が消える件への回答）「全項目を全CPUで選択可能にしたい」（p005）。
- 2026-10-10「Firmwareもアーキテクチャに関係なくすべて選べるようにしてください。」（p006）
- 2026-10-10〜11 Drivers を Boot Option と Development の間に、全 CPU で共通に選べるように、機能別の階層に（p007）。
- 2026-10-11「mainにマージしてください。」（`8c93ba8e0`）、「mainへ統合する」（`1b3d6cd03`）、「/boot/boot0がないようですが、これもマージしてもらえますか？」（`067ae96d8`、p011）。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- `tools/menuconfig.py` を新しい階層と Build boot image（進捗の bar）に作り直した（p001、T1-497 PASS: menu の画面・Build succeeded・boot-test）。
- CPU arm64 の platform 値の取り違えを直した（p004）。Base・Desktop・Packages・Firmware・Drivers を全 CPU の共通の registry で選び保存できるようにした（p005〜p007、全 6 platform の選択・保存・Make の実効値と RPi4 の PTY の操作 PASS）。
- RPi4 の選んだ config の rootfs・desktop の build の規則（p008、`make -j16` で SD image まで、check-disk-image PASS）、Graphical login を firmware の command line へ（p009）、userland の配置の共通化と OpenSSH の runtime の欠け（p010）、起動 FAT を `/boot/boot0` に自動で公開（p011）。
- 各段の Queue と結果は [codex-queue.md](codex-queue.md)・[codex-userland-queue.md](codex-userland-queue.md)・[codex-firmware-queue.md](codex-firmware-queue.md)・[codex-drivers-queue.md](codex-drivers-queue.md)・[codex-rpi4-build-queue.md](codex-rpi4-build-queue.md)・[codex-rpi4-sshd-queue.md](codex-rpi4-sshd-queue.md)・[codex-rpi4-boot0-queue.md](codex-rpi4-boot0-queue.md)、[history](history/menuconfig-arm64-20261010.md)。

## 制限・移管

- p003（規約の全文の見直し）は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」）。
- RPi4 の実機の確かめ（host key・SSH、graphical login、`/boot/boot0` の mount・書き込み）は個別の記録が無く、2026-10-13 のユーザーの UAT の一括で済みとした。全 optional package の全 CPU の実行、SPARC・X68k の bootstrap の disk format は主張しない。
- ECM は x86 で build の切り替えがあり、arm64 には source が繋がっていない（移植の成功とはしない）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws193-p001 | 今の menuconfig の調べ、新しい階層と Build boot image、host 試験 | cleared（2026-10-09、T1-497） |
| ws193-p002 | T1 の image の build と boot-test、ユーザーの確認 | cleared（2026-10-10、ユーザー「menuconfigはOK」） |
| ws193-p003 | 規約の全文の見直し | planning のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws193-p004 | CPU arm64 の platform 値の取り違えの修正 | cleared（2026-10-10、93914124c） |
| ws193-p005 | CPU 共通の userland の選択、FFmpeg の arm64 | cleared（2026-10-10、fe4300313） |
| ws193-p006 | Firmware を全 CPU で選択・保存・packaging | cleared（2026-10-10、c2b97e953） |
| ws193-p007 | Drivers を指定の位置へ、機能別の階層と全 CPU の共通の選択 | cleared（2026-10-11、a1c65db6a） |
| ws193-p008 | RPi4 の config の rootfs・desktop の build の規則 | cleared（2026-10-11、df1d2be26） |
| ws193-p009 | RPi4 の Graphical login を firmware の command line へ | cleared（2026-10-11、source・option・image。実機の desktop は未確認） |
| ws193-p010 | userland の配置の共通化・OpenSSH の runtime の欠け | cleared（2026-10-11、1b3d6cd03） |
| ws193-p011 | RPi4 の起動 FAT を `/boot/boot0` へ自動で公開 | cleared（2026-10-11、4cfddb3f9。実機は未確認） |

## 記録の所在

Phase の計画・結果・試験の証拠（`tests/`）は削除した。git の commit `fb1f6afee` の `plan/ws193/` にある。Queue の履歴は [plan/history](../history/index.md)。
