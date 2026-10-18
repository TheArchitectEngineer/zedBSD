<!-- awesome-plan project=zedbsd record=ws265 -->

# WS265 ppc64 の QEMU の machine（「virt」相当）への移植（framebuffer の console・VFS の段まで）

- ID: ws265、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: planning（2026-10-18 Q1 作成。担当は未定）
- 起点（2026-10-18 ユーザー）:「さらに、virt riscv32, virt ppc32, virt ppc64, virt m68k (68030)も追加してほしいです。それぞれWSを作ってほしいです。フレームバッファでコンソールが出てVFSがマウントできないところまででいいです。」
- 前提（Q1 の調べ）: **QEMU の PowerPC に「virt」という machine は無い**。64 bit の候補は `pseries`（sPAPR の paravirt、SLOF、virtio・VGA）と `powernv`（POWER10 の実機相当）。paravirt の `pseries` が「virt」に近い。endian（big か little か）、firmware（SLOF・OPAL）、画面の device は p001 でユーザーに案を出して決める。共有の LLVM は PowerPC の target を持つ。tree に PowerPC の HAL は無い。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる（WS256 の riscv64 の p003 と同じ到達点）。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ、`bootloader/` の header を見てよいのは HAL だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則（手順は参考、code・comment・構造は写さない、block ごとの出典）。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: QEMU の machine と画面の device、firmware・起動の道、toolchain、HAL の構成（既存の HAL との共有）、HAL の API の差分の案、試験の道具、Phase の表。code は書かない | planning |
- 決定（2026-10-18 ユーザー、クリック）: machine と endian は「pseries、big endian」。HAL の API は形を限った事前の承認（Guardrail の表）。
