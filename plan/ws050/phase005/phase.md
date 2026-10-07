<!-- awesome-plan project=zedbsd record=ws050-p005 -->

# ws050-p005: i915 との連携（HPD・pin・向きの二つの出所）

Phase ID: `ws050-p005`
Parent: [WS050](../ws.md)
Status: planned
Phase disposition: normal
Queue: なし

## 範囲（ws.md の表、design.md §13）

`typec_display_report`、HPD・pin・向きの UCSI と i915 の二つの出所の統合、TC の port と connector の対応付け。

## 注（2026-10-07、ws050-p002 の仕様との照合から、設計の見直しが要る）

- **UCSI の GET_CONNECTOR_STATUS に DP の HPD・pin の field は無い**（3.1 Table 6-43。1.2 にも無い）。design.md §13 の「UCSI 2.0 以上で PPM が返すなら」
  の経路は、3.x の **GET_ATTENTION_VDO（0x16、DP の Attention VDO = HPD の状態）** と **GET_CAM_CS（0x18、今の mode の Configuration/Status = pin の割り当て）**
  で設計し直す。この 2 つが 2.0・2.1 にあるかは unconfirmed（2.x の文書が手に入らなかった）。
- 向きは 3.1 の GET_CONNECTOR_STATUS の bit 86（p002 で実装済み、2.0 で同じかは unconfirmed）。
- GET_ALTERNATE_MODES は 3.1 でも 1 回 2 つまで（2.x の大きな MESSAGE IN でも同じ。p002 は 2 つずつ offset を進める）。
