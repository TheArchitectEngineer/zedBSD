<!-- awesome-plan project=zedbsd record=ws266 -->

# WS266 m68k（68030）の virt（QEMU）への移植（framebuffer の console・VFS の段まで）

- ID: ws266、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: planning（2026-10-18 Q1 作成。担当は未定）
- 起点（2026-10-18 ユーザー）:「さらに、virt riscv32, virt ppc32, virt ppc64, virt m68k (68030)も追加してほしいです。それぞれWSを作ってほしいです。フレームバッファでコンソールが出てVFSがマウントできないところまででいいです。」
- 前提（Q1 の調べ）: QEMU の `qemu-system-m68k -M virt` がある（Goldfish の TTY・RTC・PIC、virtio-mmio）。virt の既定の CPU は 68040 で、**QEMU の MMU の emulation が 68030 の PMMU をどこまで支えるかは未確認**（p001 で QEMU の source で確かめ、68030 で起動できない場合の案をユーザーに出す）。画面の device（virt に ramfb・virtio-gpu を付けられるか）も p001 で確かめる。既存の m68k の HAL は X68000（`src/hal/m68k/`、`bsp-x68k`、`include/hal/arch/m68030.h`、`mmu030`・`cache030` など）で、toolchain は GCC（`m68k-linux-gnu-gcc`、platform/x68k）。BSP は virt の 1 つ、CPU は 68030 の部品を共有する（2026-10-18 Q1 の説明）。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる（WS256 の riscv64 の p003 と同じ到達点）。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ、`bootloader/` の header を見てよいのは HAL だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則（手順は参考、code・comment・構造は写さない、block ごとの出典）。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: QEMU の machine と画面の device、firmware・起動の道、toolchain、HAL の構成（既存の HAL との共有）、HAL の API の差分の案、試験の道具、Phase の表。code は書かない | planning |
- 既知の欠け（2026-10-18 P2 の報告、Q1 の確かめ）: `bootloader/x68k/stage2.c` と `src/hal/m68k/bsp-x68k/boot.c` が使う `struct x68k_boot_handoff` の定義が、2026-10-03 のリポジトリの作り直しの時から tree のどこにも無く、x68k は build できない。p001 で扱いを決める（boot loader → HAL の構造体として HAL か bootloader/ に定義し直すか）。
