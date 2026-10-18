<!-- awesome-plan project=zedbsd record=ws263-p001 -->

# ws263-p001 設計

Status: in-progress（2026-10-19 P1、q1257。設計は [design.md](../design.md)。cleared は Q1 の判定）

- 親: [WS263](../ws.md)。Queue: q1257（Q1 の指示、2026-10-18 ユーザー「…これらを完了させるまで自走してほしいです。」）。
- 結果: [design.md](../design.md)（machine・firmware・toolchain・HAL の構成・HAL の API の差分・試験の mode・Phase の表）。
- 未決: `include/uapi/stat.h` の riscv32 の枝（design §5）は HAL の外の path なので Q1 の確認を待つ。
