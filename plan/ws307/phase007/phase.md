<!-- awesome-plan project=zedbsd record=ws307-p007 -->

# ws307-p007 UEFI の runtime services と Boot####

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §5.8（U5・U15）。Queue: q1665 / q1665-i01（担当 P4、2026-10-29）。
- Status: in-progress（最初の段の HAL の差分の案を置いた。ユーザーの承認待ち。承認までの code は無い）
- 出典: 2026-10-28 U5「…Boot####の作成もWSに入れる。」、U15「末尾に足します。また、KeiではなくzedBSDです。…」。

## 最初の段（2026-10-29、P4）

- HAL の差分の案: [proposed/hal-uefi-variables.md](../proposed/hal-uefi-variables.md)（loader の handoff v8、`hal_get_arch_handoff("uefi.runtime")`、`hal.h` に UEFI の変数の 4 関数、amd64 の実装の方針、判断 H1〜H3 と推し）。AGENTS.md §7 により承認まで実装しない。
- 承認までの間: installer（p009・p010）は loader を ESP に置き、S11 で「firmware の boot menu で選ぶ」を出すだけ（spec §5.8）。

## 改訂 2（2026-10-29、P4）

- ユーザーの回答（原文、Q1 の中継）:「UEFIに限らないファームウェア環境変数という概念で作り直してみてください。もう一度レビューして承認します。」
- 案を [proposed/hal-fw-variables.md](../proposed/hal-fw-variables.md)（firmware の環境変数: 名前空間（UEFI・Open Firmware・U-Boot・Pi の EEPROM）＋vendor＋UTF-8 の名前の key、`hal_fw_variable_space_info`・`get`・`set`・`next`）に作り直した。前の案 [proposed/hal-uefi-variables.md](../proposed/hal-uefi-variables.md) は置き換え（file の削除は Q1）。

## 残り

- ユーザーの承認（改訂 2 の F1〜F3、Q1 経由）。
- 承認の後: loader の v8、HAL の実装、`/dev/system` の UAPI（形・版・上限を code の前に決める）、`efiboot`、QEMU の OVMF で Boot#### を書いて読み戻す（案 §4）。
