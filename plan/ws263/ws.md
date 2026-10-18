<!-- awesome-plan project=zedbsd record=ws263 -->

# WS263 riscv32 の virt（QEMU）への移植（framebuffer の console・VFS の段まで）

- ID: ws263、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19: p001・p002 cleared、到達点は T1-711 で確かめた。残りは p003 の規約の見直し）
- 起点（2026-10-18 ユーザー）:「さらに、virt riscv32, virt ppc32, virt ppc64, virt m68k (68030)も追加してほしいです。それぞれWSを作ってほしいです。フレームバッファでコンソールが出てVFSがマウントできないところまででいいです。」
- 前提（Q1 の調べ）: QEMU の `qemu-system-riscv32 -M virt` がある。共有の LLVM は RISCV の target（riscv32 を含む）を持つ（WS256 p002・p010）。WS256 の riscv64 の HAL（`src/hal/riscv64/`、bsp-virt の ramfb の console、fw_cfg）と QEMU の道具（boot-test.sh の riscv64-virt の mode）を手本・共有の候補にする。Sv32 の MMU、32 bit の SBI の呼び方、`include/hal/arch/riscv32.h` の新規が要る見込み。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる（WS256 の riscv64 の p003 と同じ到達点）。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ、`bootloader/` の header を見てよいのは HAL だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則（手順は参考、code・comment・構造は写さない、block ごとの出典）。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計（[design.md](design.md)）: QEMU の machine と画面の device、firmware・起動の道、toolchain、HAL の構成、HAL の API の差分、試験の道具、Phase の表 | cleared（2026-10-19 Q1） |
| [p002](phase002/phase.md) | HAL の芯と vmunix（riscv32 の HAL・platform・build・boot-test の mode）、T1 の画面で到達点 | cleared（2026-10-19 Q1、T1-711） |
| [p003](phase003/phase.md) | 規約の全文の見直し（書き方だけ）、kernel の platform の binding を `hal-console.c` に | in-progress |
- 決定（2026-10-18 ユーザー、クリック）: HAL の API は形を限った事前の承認（Guardrail の表）。
