<!-- awesome-plan project=zedbsd record=ws256-p001 -->

# ws256-p001 設計: riscv64 の HAL と VisionFive 2 への移植

Status: in-progress（2026-10-18 q1243-i01、P2）

- 親: [WS256](../ws.md)。Queue: q1243-i01。
- 範囲: 設計の文書 `plan/ws256/design.md` を書く。code は書かない。HAL の API（include/hal/）の差分は案として plan に置くだけ。
- 完了の条件: design.md に (1) 調べた事実と出典、(2) HAL の riscv64 の構成と API の差分の案、(3) 起動の道（QEMU virt と VisionFive 2）、(4) build の構成、(5) 試験の方法、(6) Phase の表（LW の見積もり）、(7) ユーザーの判断が要る点と既定の案、がそろい、Q1 が読んで合格とする。
