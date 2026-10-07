<!-- awesome-plan project=zedbsd record=ws050-p004 -->

# ws050-p004: 操作の command と操作の口

Phase ID: `ws050-p004`
Parent: [WS050](../ws.md)
Status: planned
Phase disposition: normal
Queue: なし

## 範囲（ws.md の表、design.md §4・§9）

CONNECTOR_RESET・SET_UOR・SET_PDR・SET_NEW_CAM・GET_CABLE_PROPERTY と、kernel 内の操作の口（`typec_connector_set_data_role` ほか）。

## 注（2026-10-07、ws050-p002 の仕様との照合から）

- **command の番号は design.md §2.3 の表でなく仕様の値を使う**: SET_UOR は 0x09（0x08 は 1.2 の SET_UOM、3.1 の SET_CCOM）、SET_PDM は 0x0A（obsolete）、
  SET_PDR 0x0B、CONNECTOR_RESET 0x03、SET_NEW_CAM 0x0F、GET_CABLE_PROPERTY 0x11（UCSI 1.2 と 3.1 の Table A-1、[p002](../phase002/phase.md) の照合の表）。
- 各 command の field（SET_UOR・SET_PDR の bit、SET_NEW_CAM の connector 16〜22・EnterOrExit 23・New CAM 24〜31・AMSpecific 32〜63、1.2 Table 4-32）は
  実装の前に仕様の表で確かめる（文・表は写さず値だけ）。
