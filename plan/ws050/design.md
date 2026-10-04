# WS050 設計: USB-C の UCSI driver

Status: 案（2026-10-04、ws050-p001、P1）。人間の判断が要る点は §10。

## 1. 目的と範囲

USB Type-C Connector System Software Interface（UCSI）で、USB-C の connector の状態（接続・向き・電源とデータの役割・partner の種類・
Alternate Mode・USB PD の contract）を読み、変化の通知を受け、Alternate Mode の状態を WS051（DP Alt Mode）へ、電源の状態を WS052・WS132 へ渡す。

範囲外: USB PD の message を OS が直接話すこと（TCPM。UCSI では PPM（PD controller と EC の firmware）が話す）、Thunderbolt・USB4 の
tunnel、role の切り替えの UI。

## 2. UCSI の要点

UCSI は OS（OPM: OS Policy Manager）と platform（PPM: Platform Policy Manager）の間の mailbox の規約である（Intel の
「USB Type-C Connector System Software Interface Specification」。1.0〜1.2、2.0〜2.1、3.0）。実装の前に仕様書の該当版で bit の配置を
確かめる（下の表は設計に要る範囲の要約。**実装の根拠は仕様書であり、Linux の driver（GPL）の code は写さない**）。

### 2.1 data structure（1.x の配置、合計 0x30 byte）

| offset | 名前 | 大きさ | 書く側 |
| --- | --- | --- | --- |
| 0x00 | VERSION（BCD、例 0x0120 は 1.2） | 2 | PPM |
| 0x02 | 予約 | 2 | — |
| 0x04 | CCI（Command Status and Connector Change Indication） | 4 | PPM |
| 0x08 | CONTROL（command の code と引数） | 8 | OPM |
| 0x10 | MESSAGE_IN（PPM → OPM の data） | 16 | PPM |
| 0x20 | MESSAGE_OUT（OPM → PPM の data） | 16 | OPM |

2.x 以降は MESSAGE_IN・MESSAGE_OUT が大きくなり配置が変わる版がある。driver は VERSION を読み、**知っている配置（1.x）以外は使わずに
止める**（log を出し、connector を公開しない）。5330 の region は 0x38 byte（§3）で 1.x の配置に合う。

### 2.2 CCI

command の完了（Command Completed）、busy、error、not supported、reset completed、cancel completed、acknowledge command（ACK の完了）、
MESSAGE_IN の data の長さ、変化した connector の番号（Connector Change Indicator、0 は無し）。

### 2.3 command（この WS で使うもの）

| code | command | 用途 |
| --- | --- | --- |
| 0x01 | PPM_RESET | 初期化（CCI の reset completed を待つ。CONTROL を書いた後は他の command と違い ACK しない） |
| 0x04 | ACK_CC_CI | command の完了・connector の変化の受領の通知 |
| 0x05 | SET_NOTIFICATION_ENABLE | 通知する変化の種類 |
| 0x06 | GET_CAPABILITY | connector の数、対応する機能、Alternate Mode の数、PD の版 |
| 0x07 | GET_CONNECTOR_CAPABILITY | connector ごとの能力（DFP/UFP/DRP、Alternate Mode、USB の世代） |
| 0x0C | GET_ALTERNATE_MODES | connector・partner（SOP）・cable（SOP'）の SVID と mode の VDO の一覧 |
| 0x0D | GET_CAM_SUPPORTED | connector が入れる Alternate Mode |
| 0x0E | GET_CURRENT_CAM | 今入っている Alternate Mode |
| 0x0F | SET_NEW_CAM | Alternate Mode に入る・出る（WS051 が要るときだけ。多くの platform は PPM が自分で入る） |
| 0x10 | GET_PDOS | source・sink の PDO（電源の能力） |
| 0x12 | GET_CONNECTOR_STATUS | 接続、向き、電源の方向、partner の種類と flag（USB・Alternate Mode）、電源の operation mode、RDO、変化の bit |
| 0x13 | GET_ERROR_STATUS | error の詳細（log 用） |

role の切り替え（SET_UOR・SET_PDR）・CONNECTOR_RESET・GET_CABLE_PROPERTY は p001 の時点では使わない（§10）。

### 2.4 手順

1. OPM が CONTROL（と要れば MESSAGE_OUT）を書き、PPM に知らせる（ACPI では `_DSM` の write の function）。
2. PPM が CCI と MESSAGE_IN を更新し、通知する（ACPI では `Notify (UCSI の device, 0x80)`）。OPM は `_DSM` の read の function で
   PPM の値を mailbox に写させてから、CCI と MESSAGE_IN を読む。
3. command completed なら MESSAGE_IN を受け、ACK_CC_CI（command completed の受領）を送る。
4. CCI に connector の番号があれば（command の完了と同時のこともある）GET_CONNECTOR_STATUS を送り、ACK_CC_CI（connector change の受領）を送る。
   変化の内容（GET_CONNECTOR_STATUS の変化の bit）に応じて GET_ALTERNATE_MODES・GET_CURRENT_CAM・GET_PDOS を読み直す。
5. 同時に出せる command は 1 つ。busy なら待つ。timeout（仕様の推奨と実機の様子から、通常の command 5 秒、PPM_RESET は 1 秒ごとに数回）で
   error として PPM_RESET からやり直す。

## 3. 対象機（Latitude 5330）の UCSI

[5330 の table](../ws049/tests/latitude5330/)（`ssdt8.dat`、OEM Table ID `UsbCTabl`）を `iasl -d` で読んだ結果:

- device `\_SB.UBTC`: `_HID` `USBC000`、`_CID` `PNP0CA0`、`_UID` 1、`_DDN` "USB Type-C"。
- `_STA`: GNVS の `USTC == 1` で、EC の 0x80・0x81（UCSI の VERSION）が 0 でなく、`OSYS >= 0x07DF`（`_OSI ("Windows 2015")` が真のとき
  DSDT が設定。zedBSD の `_OSI` は `Windows 2022` まで真なので満たす）なら 0x0F。
- `_CRS`: Memory32Fixed、基底は GNVS の `UBCB`、長さ 0x1000。mailbox は `OperationRegion (USBC, SystemMemory, UBCB, 0x38)` の
  `VER1`・`VER2`・`RSV1`・`RSV2`・`CCI0..3`・`CTL0..7`・`MGI0..F`・`MGO0..F`（§2.1 の 1.x の配置そのもの）。
- `_DSM`（UUID `6f8398c2-7ca4-11e4-ad36-631042b5008f`、UCSI の UUID）: function 0 は 0x1F（1〜4 が使える）、**1 = write**（MGO0..F を EC の
  0xA0..0xAF、CTL0..7 を 0x88..0x8F へ書き、EC の 0xB0 に 0xE0 を書いて PPM に知らせる）、**2 = read**（EC の 0x90..0x9F を MGI0..F、
  0x84..0x87 を CCI0..3 へ写す）、3 = `XDCE`、4 = `UDRS`（GNVS の値。用途は未確認）。EC の byte の読み書きは `\ECRB`・`\ECWB`
  （EC の region が繋がる前（`ECRD == 0`）は SMI の経路 `EISC`）。UUID `ce2ee385-…`（USB controller の `_DSM`）は function 5 だけ 1。
- 通知: EC の query `_Q79` が（`ECRD == 1` のとき）0x90..0x9F・0x84..0x87 を mailbox に写してから `Notify (\_SB.UBTC, 0x80)`。
- connector: `\_SB.UBTC.CR01`〜`CR0A` が GNVS（`TTUP`・`TPxU`・`TPxD`）の条件で作られ、`_PLD`・`_UPC` を持つ（`_ADR` 0）。
  `_PLD` の group position（GNVS の `TPxP`・`TPxT`）は xHCI の root hub の port の `_PLD` と対応付けられる（connector と USB の port の対応）。
- mailbox の memory は GNVS（ACPI NVS の RAM）。AML は SystemMemory の handler（kernel は `hal_space_map_device()`、uncached）で読み書きする。
  driver も同じ写像で読み書きし、AML と driver で属性の違う写像を作らない。

依存: EC の query の配送（ws049-p007、実機）、DSDT・SSDT の読み込み（ws049-p008、BUG-165）、EC の region（`_REG`）が繋がっていること。

## 4. 構成

```
src/drivers/typec/
  ucsi.c         UCSI の核: command の実行、CCI の処理、connector の状態の更新（transport に依らない）
  ucsi.h         核と transport の間の口（kernel 内）
  ucsi-acpi.c    ACPI の transport: USBC000/PNP0CA0 の device、_CRS の写像、_DSM、Notify
  typec.c        Type-C の connector の層: 状態の保持、利用者（WS051・WS052・WS132）への通知、診断の出力
include/drivers/typec/typec.h  kernel 内の公開の口（connector の状態の struct、listener の登録）
```

- transport の口: `read(offset, buffer, length)`・`write(offset, buffer, length)`（mailbox の写像）、`sync_write()`（`_DSM` 1）、
  `sync_read()`（`_DSM` 2）、通知の callback。host の試験は疑似の transport を差し込む。
- 核は 1 つの kernel thread（`ucsi`）で全ての command を順に実行する。ACPI の Notify の handler（event thread の中）は CCI を読まず、
  thread を起こすだけ（AML を event thread の中で長く走らせない。`_DSM` 2 は thread が呼ぶ）。
- 状態: connector ごとに、接続、向き（CC1/CC2）、電源の方向（source/sink）、電源の operation mode（USB 既定・1.5A・3A・PD）、
  partner の種類（DFP・UFP・powered cable・debug・audio accessory）、partner の flag（USB・Alternate Mode）、Alternate Mode の一覧
  （SVID・VDO）と今入っている mode、PDO と RDO（PD の contract）、`_PLD` の位置。版の番号（generation）を変化ごとに増やす。
- 利用者への口（kernel 内）: `typec_listener_register(callback)`。callback は変化した connector と generation を受け、状態を
  `typec_connector_get(index, &state)` で写して読む（lock の中で callback を呼ばない）。WS051 は「DP の SVID 0xFF01 の mode に入った・出た、
  pin の割り当て（DP の VDO）」を、WS052・WS132 は「電源の方向と contract」を見る。
- 診断の口: `/dev/typec`（text、read で全 connector の状態、`/dev/acpi` と同じ形）。UAPI を足さない（§10）。

## 5. 初期化

1. ACPI の walk で `_HID USBC000` か `_CID PNP0CA0` の device を探し、`_STA` が present なら attach（無ければ何もしない）。
2. `_CRS` から Memory32Fixed の基底と長さを読み、0x30 byte 以上あることを確かめて写像する。
3. `_DSM` function 0 で function 1・2 があることを確かめる。
4. `drv_acpi_notify_install(device, …)` で 0x80 を受ける。
5. thread を起こし: PPM_RESET → reset completed を待つ → SET_NOTIFICATION_ENABLE（connector の変化の全種類と command の完了）→
   GET_CAPABILITY → connector ごとに GET_CONNECTOR_CAPABILITY・GET_CONNECTOR_STATUS（接続していれば GET_ALTERNATE_MODES（SOP）・
   GET_CURRENT_CAM・GET_PDOS）。
6. VERSION が 1.x でなければ log を出して止める（§2.1）。

## 6. 誤りと競合

- command の timeout、PPM の error（GET_ERROR_STATUS を読んで log）、not supported（その機能を使わない）、busy（待つ）。
  続けて失敗したら PPM_RESET からやり直し、それも失敗したら driver を止める（kernel は止めない）。
- 通知の取りこぼし: 通知が来なくても、command の完了は `_DSM` 2 と CCI の poll（間隔 10ms、timeout まで）で拾える形にする
  （通知の経路（EC の query）が実機で働かない場合の保険。poll で動いたことは log に残す）。
- lock: 状態の struct は 1 つの mutex。thread だけが書き、利用者は写して読む。AML の評価（`_DSM`）は thread から、interpreter の lock は
  ACPI の側が取る。Notify の handler は spinlock で「仕事あり」を立てて thread を起こすだけ。

## 7. 試験

- host: `ucsi.c` を host で compile し、疑似の PPM（C の model: connector の接続・partner・Alternate Mode・PD の筋書き、CCI の完了と変化の
  通知）につないで、初期化、抜き差し、Alternate Mode の変化、busy・error・timeout・通知の取りこぼし（poll）を確かめる。ASan・UBSan。
- host（AML）: ws049 の harness に、`_DSM` 1・2 と mailbox の region を持つ ASL の UCSI device を足し、`ucsi-acpi.c` の手順（`_DSM` を呼ぶ
  順と mailbox の写し）を確かめる。5330 の `ssdt8.dat` を読み込み、`\_SB.UBTC._DSM` の function 0 が 0x1F、`_CRS` が Memory32Fixed で
  あることを確かめる（EC の模擬で `_DSM` 2 が 0x90..0x9F を写すことも）。
- QEMU: UCSI の device は無い（QEMU は USB-C を模さない）。boot test で driver が「device 無し」で何もしないことだけ。
- 実機（UAT）: `/dev/typec` で各 port の抜き差し・向き・電源の方向・partner の Alternate Mode（USB-C の monitor、hub、充電器）、
  dmesg の通知の経路（Notify か poll か）。

## 8. 他の WS との境界

- WS049: Notify の配送、`_DSM` の評価（buffer・package の引数）、SystemMemory の写像。足りない口があれば WS049 に足す（例: driver が
  device の `_CRS` を解く共通の関数。今は EC の driver が自分で解いている）。
- WS051: DP Alt Mode。Alder Lake の platform では Alternate Mode に入るのは PPM（EC と PD controller の firmware）と PMC の mux で、OS は
  状態を読む。Linux の i915 は DP-alt を UCSI を使わずに扱う（`intel_tc.c`: TCSS の register の live status で TC・DP-alt・TBT-alt を見分け、
  FIA の lane の割り当てを読み、PHY の ownership を取る。HPD は i915 の Type-C の hotplug の割り込み）。zedBSD の i915 は `intel_tc.c` を
  まだ移していない（`hotplug.c` の「Type-C connected check is not ported」）。**したがって WS051 の画面の出力は UCSI に必須の依存を持たない
  見込み**で、UCSI は補助の情報（partner の Alternate Mode の一覧、pin の割り当て、cable）と診断を渡す。WS051 の p001 でこの依存を確かめ、
  ws.md の依存（「WS050 が前提」）を直す提案をする。
- WS052・WS132: 電源の方向と contract、充電器の有無を `/dev/system` の電源の状態へ。

## 9. Phase の案

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | WS049 の namespace | この文書、§10 の判断 |
| p002 | UCSI の核と疑似の PPM での host の試験（`ucsi.c`・`ucsi.h`、host の harness） | p001、§10 の 1・2 | host の試験の筋書きが全て通る、ASan/UBSan、規約、kernel の flag で warning 0 |
| p003 | ACPI の transport と kernel への組み込み（`ucsi-acpi.c`、attach、Notify、thread、`/dev/typec`） | p002、ws049-p007・p008 | AML の host の試験、kernel の build、QEMU の boot test（device 無しで何もしない）、実機の UAT の手順 |
| p004 | Type-C の connector の層と利用者への口（`typec.c`、listener、Alternate Mode・PDO の読み直し） | p003 | host の試験、WS051・WS052 が使う口の文書 |
| p005 | 実機の確認と規約の全文の確認 | p003・p004、実機 | 実機で抜き差し・向き・電源・Alternate Mode が読め通知が届く、規約、build、boot test |

## 10. 人間の判断が要る点

1. **公開の形**: 診断の text の `/dev/typec`（UAPI を足さない、`/dev/acpi` と同じ）で始め、利用者向けの正式な口は WS132 の `/dev/system`
   に任せる案。別の形（専用の ioctl の device）にするか。
2. **role の切り替え**（SET_UOR・SET_PDR: data・power の role swap）と CONNECTOR_RESET をこの WS の範囲に入れるか（案: 入れない。状態の読みと
   通知、Alternate Mode の状態に絞る）。
3. **UCSI 2.x 以降の配置**への対応（案: 1.x だけ。5330 は 1.x の配置。2.x の機種が対象に入ったら足す）。
