<!-- awesome-plan project=zedbsd record=ws264 -->

# WS264 ppc32 の QEMU の machine（「virt」相当）への移植（framebuffer の console・VFS の段まで）

- ID: ws264、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-18 Q1 作成。2026-10-19 P1 が q1257 で p001・p002 を実施中）
- 起点（2026-10-18 ユーザー）:「さらに、virt riscv32, virt ppc32, virt ppc64, virt m68k (68030)も追加してほしいです。それぞれWSを作ってほしいです。フレームバッファでコンソールが出てVFSがマウントできないところまででいいです。」
- 前提（Q1 の調べ）: **QEMU の PowerPC に「virt」という machine は無い**。32 bit の候補は `ppce500`（generic paravirt e500 platform、virtio・PCI）、`mac99`・`g3beige`（PowerMac、Open Firmware）、`bamboo`・`sam460ex` など。どれを「virt」とするか（画面の device を含めて）は p001 でユーザーに案を出して決める。共有の LLVM は PowerPC の target を持つ。tree に PowerPC の HAL・`include/hal/arch` は無い（WS027 PowerPC 移植は canceled、Future Work）。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる（WS256 の riscv64 の p003 と同じ到達点）。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ、`bootloader/` の header を見てよいのは HAL だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則（手順は参考、code・comment・構造は写さない、block ごとの出典）。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計（[design.md](design.md)）: mac99、OpenBIOS から flat image、画面は firmware の display | in-progress |
| [p002](phase002/phase.md) | HAL・mac99 の BSP・vmunix・boot-test の mode、T1 の画面で到達点 | in-progress |
| p003 | 規約の全文の見直し（書き方だけ） | planned |
- 決定（2026-10-18 ユーザー）: machine は「mac99にします。」（QEMU の PowerMac G4 相当、OpenBIOS）。HAL の API は形を限った事前の承認（Guardrail の表）。
