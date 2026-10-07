<!-- awesome-plan project=zedbsd record=ws050-p004 -->

# ws050-p004: 操作の command と操作の口

Phase ID: `ws050-p004`
Parent: [WS050](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: host 75/75、kernel の build warning 0。実機の確認は 5330 が戻ってから（p006））（旧: in-progress（2026-10-07 q834 P2: 正常系の実装、host 75/75（核 47 + ACPI 28）、kernel の build warning 0。QEMU は不要（UCSI の device が無い）、実機は対象外））
Phase disposition: normal
Queue: q834（P2）

## 範囲（ws.md の表、design.md §4・§9）

CONNECTOR_RESET・SET_UOR・SET_PDR・SET_NEW_CAM・GET_CABLE_PROPERTY と、kernel 内の操作の口（`typec_connector_set_data_role` ほか）。

## 注（2026-10-07、ws050-p002 の仕様との照合から）

- **command の番号は design.md §2.3 の表でなく仕様の値を使う**: SET_UOR は 0x09（0x08 は 1.2 の SET_UOM、3.1 の SET_CCOM）、SET_PDM は 0x0A（obsolete）、
  SET_PDR 0x0B、CONNECTOR_RESET 0x03、SET_NEW_CAM 0x0F、GET_CABLE_PROPERTY 0x11（UCSI 1.2 と 3.1 の Table A-1、[p002](../phase002/phase.md) の照合の表）。
- 各 command の field（SET_UOR・SET_PDR の bit、SET_NEW_CAM の connector 16〜22・EnterOrExit 23・New CAM 24〜31・AMSpecific 32〜63、1.2 Table 4-32）は
  実装の前に仕様の表で確かめる（文・表は写さず値だけ）。

## 範囲（Q1 の ACK 2026-10-07）

操作の口（`typec.h`）: `drv_typec_connector_set_data_role`・`set_power_role`・`reset`・`enter_mode`・`exit_mode`。呼び手を block せず層の要求の列（8 件）に積んで
serial を返し、結果は record の `request_serial`・`request_error` と listener の通知で返す。UCSI の CONNECTOR_RESET・SET_UOR・SET_PDR・SET_NEW_CAM・
GET_CABLE_PROPERTY。thread は Notify と要求の kick を分ける。誤り・取り消し・同時の多数の要求・SET_CAM_PRIORITY・SET_CCOM は backlog。

## 実装（2026-10-07 P2）

- `typec.c`: 要求の ring（`DRV_TYPEC_REQUEST_MAX` 8、層の lock の下）、serial（1 から増えるだけ）、`drv_typec_operator_set`（connector driver の kick）、
  `drv_typec_request_take`、`drv_typec_request_finish`（record に serial と errno を入れ、generation は変えない）。5 つの操作は ENOENT（無い connector）・
  EINVAL（role・kind・mode の番号の外れ）・EBUSY（列が満杯）。`/dev/typec` の行に ` cable=passive speed=… current=…mA` と ` request=N error=E`。
- `ucsi.c`: `drv_ucsi_request`（CONTROL を作って `ucsi_command`、結果を finish、connector を読み直して publish、保留の変化を処理。読めない時は今の record を publish）。
  CONTROL の値（3.1 Table 6-5・6-20・6-22・6-33 と照合、文と表は写していない）: CONNECTOR_RESET 0x03（bit 23: 2.0 以上は 1 = Data Reset・0 = Hard Reset、
  1.0 だけ 1 = Hard Reset、1.1〜1.x は 0。1.x の bit は Linux の定義から推定、unconfirmed。1.x の Data Reset は ENOTSUP で PPM に送らない）、SET_UOR 0x09
  （bit 23 DFP へ、24 UFP へ、25 相手の swap を受ける、policy 26〜27 は 0 = 全部受ける）、SET_PDR 0x0B（23 Source へ、24 Sink へ、25 受ける）、
  SET_NEW_CAM 0x0F（23 EnterOrExit、24〜31 New CAM = connector の mode の番号、32〜63 AMSpecific。bmOptionalFeatures の bit 3（Alternate mode override）が
  無ければ ENOTSUP、connector の mode の数を超える番号は EINVAL）。GET_CABLE_PROPERTY 0x11（bit 5 の Cable details がある時、接続の読みで。速度 = 仮数 2〜15 ×
  1000^指数 0〜1 bps、電流 16〜23 × 50 mA、VBUS 24、active 25、方向 26、端 27〜28、mode 29。Table 6-38〜6-40）。
- `ucsi-acpi.c`: Notify の handler は `notified` を atomic に立てて signal、要求の kick は signal だけ。step は wait の後、`notified` の時だけ
  `drv_ucsi_service`、続けて列の要求を全部 `drv_ucsi_request`。command の中の wait（transport の wait）は `notified` を下ろす（その後の CCI の読みが変化を
  latch し、操作の終わりに処理する）。

## 確かめ（2026-10-07）

- host: `make -C plan/ws050/tests` → 75/75（ASan・UBSan）。核（`ucsi-host.c`）の疑似の PPM に 5 つの command を足し、1.2 で Source への swap（CONTROL の値、
  role、serial と error、listener）、cable（10 Gb/s・5 A・Type-C・VBUS）、UFP への swap、Hard Reset（bit 23 が 0）、1.2 の Data Reset は ENOTSUP で PPM に
  送らない、DP の mode に configuration 0x406 で入る・出る、list の外の mode は EINVAL、text、無い connector は ENOENT、9 件目は EBUSY、ACK の規則の違反 0。
  2.x で Data Reset（bit 23 が 1）・Hard Reset（0）。ACPI（`ucsi-acpi-host.c`、5330 の table）で、要求が thread を起こし、`_DSM` 1 で CONNECTOR_RESET が EC に
  届き、結果が record に入る。`idle`（何も来ない step は 0）も通る。
- kernel: config/ci の vmunix の build warning 0、`kernel-check` warning 0（stack の最大 600 byte）。style-check 0、`git diff --check` 0。
- QEMU: 不要（UCSI の device が無く、p003 の起動の試験 T1-330 に含まれる）。実機: 対象外。

## 積み残し（backlog-p2.md）

PPM の誤りの後の回復と GET_ERROR_STATUS、要求の取り消し、同時の多数の要求の公平さ、SET_CAM_PRIORITY（3.x）、SET_CCOM、data role の現在の値の公開
（UCSI 1.2 の status に無い）、1.x の CONNECTOR_RESET の bit の文書での確認。
