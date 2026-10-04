<!-- awesome-plan project=zedbsd record=ws050p001 -->

# ws050-p001: 調査と設計（UCSI）

Phase ID: `ws050-p001`
Parent: [WS050](../ws.md)
Status: in-progress（2026-10-04。設計の草案 [design.md](../design.md) を書いた。design-reviewer のレビューの結果待ち、§10 の人間の判断待ち）
Phase disposition: normal
Queue: q679 / q679-i01（P1）

## 経過（2026-10-04、P1 generation15）

- 5330 の `ssdt8.dat`（`UsbCTabl`）と DSDT を `iasl -d` で読み、UCSI の device `\_SB.UBTC`（USBC000/PNP0CA0）の `_STA`・`_CRS`（GNVS の `UBCB`）、
  mailbox の region（0x38 byte、UCSI 1.x の配置）、`_DSM`（UUID `6f8398c2-…`、1 = write、2 = read）、EC の `_Q79` → `Notify (UBTC, 0x80)`、
  connector `CR01`〜`CR0A` を確かめ、[design.md](../design.md) を書いた（構成、初期化、誤りと競合、試験、他の WS との境界、Phase の案、判断の点）。
- design-reviewer（読みのみ）を起動した。結果は generation15 の終わりまでに届かなかった（届けば Q1 か generation16 が design.md に反映する）。
- 発見: Linux の i915 は DP-alt を UCSI を使わず TCSS の register で扱う → WS051 の画面の出力は WS050 に必須の依存を持たない見込み（design §8、
  [ws051-p001](../../ws051/phase001/phase.md)）。

## 再開点

- design-reviewer の指摘を反映し、§10 の判断（公開の形 `/dev/typec`、role の切り替えを範囲外、UCSI 1.x だけ）を Q1 経由でユーザーに確かめる。
- design.md の §2 の bit の配置は UCSI の仕様書で確かめてから p002 の実装に入る（Linux の driver（GPL）は写さない）。
