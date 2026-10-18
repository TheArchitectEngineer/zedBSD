<!-- awesome-plan project=zedbsd record=ws266-p001 -->

# ws266-p001 設計

Status: in-progress（2026-10-19 P1、q1257。cleared は Q1 の判定）

- 親: [WS266](../ws.md)。結果: [design.md](../design.md)。
- 68030 の確認（design §1）: QEMU の m68k は 68030 の `pmove` と 68030 の table の形を持たない。ユーザーの決定（2026-10-18）どおり 68040 で virt を動かす。
- 画面: virt に ramfb・PCI が無いので virtio-mmio の virtio-gpu（design §2）。
