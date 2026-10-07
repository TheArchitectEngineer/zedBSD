<!-- awesome-plan project=zedbsd record=ws050-p002 -->

# ws050-p002: UCSI の核と Type-C の層（正常系、host）

Phase ID: `ws050-p002`
Parent: [WS050](../ws.md)
Status: in-progress（2026-10-07 q834 P2: 実装と host の試験 31/31、kernel の flag で warning 0。kernel への組み込みは p003）
Phase disposition: normal
Queue: q834（P2）

## 範囲（Q1 の ACK 2026-10-07）

transport に依らない UCSI の核（`src/drivers/typec/ucsi.c`・`ucsi.h`）: §2.4 の command と ACK の規則、CCI の connector の latch、§5 の 7 の初期化、
1.x・2.x の配置の表。Type-C の層（`typec.c`・`include/drivers/typec/typec.h`）: connector ごとの状態、generation、listener、copy の読み。
疑似の PPM（1.x・2.x）の host の試験（ASan・UBSan、kernel の flag で warning 0）。busy 以外の誤り・timeout・CANCEL・PPM_RESET の回復・通知の取りこぼしの poll は backlog。
実機の mailbox の記録の再生は p006 と backlog（実機は対象外）。

## 仕様との照合（Q1 の許可 2026-10-07: 公開の仕様書を WebFetch で取得、値だけを照合、文・表は写さない）

出典: "USB Type-C Connector System Software Interface (UCSI) Requirements Specification" Intel, Revision 1.2（2020-01、文書 336205-002）と
"USB Type-C Connector System Software Interface (UCSI) Specification" USB Promoter Group, Revision 3.1（2026-06、USB-IF の文書庫の zip）。
2.0・2.1 の文書は手に入らなかった（3.1 の改訂履歴だけ）。code の comment に節・表の番号を書いた。

| 項目 | 照合の結果 |
| --- | --- |
| data structure（1.2 Table 3-1） | VERSION 0・CCI 4・CONTROL 8・MESSAGE IN 16（16 byte）・MESSAGE OUT 32（16 byte）。design §2.1 と一致 |
| 2.x 以降の配置（3.1 Table 4-1） | VERSION 0（24 bit）・CCI 4・CONTROL 8・MESSAGE IN 16（255 byte）・MESSAGE OUT 272（255 byte）、計 528 byte（0x210）。**2.0 で同じかは unconfirmed** |
| CCI（1.2 Table 3-2） | connector 1〜7・length 8〜15・not supported 25・cancel 26・reset 27・busy 28・ACK 29・error 30・completed 31。一致 |
| command の番号（1.2・3.1 Table A-1） | 一致: PPM_RESET 1、CANCEL 2、CONNECTOR_RESET 3、ACK_CC_CI 4、SET_NOTIFICATION_ENABLE 5、GET_CAPABILITY 6、GET_CONNECTOR_CAPABILITY 7、SET_PDR 0x0B、GET_ALTERNATE_MODES 0x0C〜GET_ERROR_STATUS 0x13。**違い: design の SET_UOR 0x08・SET_PDM 0x09 は誤りで、0x08 は SET_UOM（3.1 では SET_CCOM）、SET_UOR は 0x09、SET_PDM は 0x0A（obsolete）**。p004 で使う時に直す |
| ACK_CC_CI（1.2 Table 4-7） | bit 16 connector change、bit 17 command completed。一致 |
| 通知（1.2 Table 4-9） | 0 completed、1 external supply、2 power operation、5 provider capabilities、6 power level、7 PD reset、8 supported CAM、9 battery、11 partner、12 power direction、14 connect、15 error |
| GET_CAPABILITY（Table 4-13）・optional features（Table 4-54） | 接続の数 bit 32〜38、features 40〜63（2 alt mode details、4 PDO details、6 external supply、7 PD reset）、alt mode の数 64〜71 |
| GET_ALTERNATE_MODES（Table 4-24） | recipient 16〜18、connector 24〜30、offset 32〜39、数 40〜41（数 − 1、最大 1 = 1 回 2 つ）。**3.1 でも同じ 2 bit**: 2.x の大きな MESSAGE IN でも 1 回の件数は増えない（design §2.1 の「MESSAGE IN の大きさで件数を決める」は不要） |
| GET_PDOS（Table 4-34） | connector 16〜22、partner 23、offset 24〜31（0〜7）、数 32〜33（最大 4）、source 34、型 35〜36 |
| GET_CONNECTOR_STATUS（1.2 Table 4-42、3.1 Table 6-43） | change 0〜15、power operation 16〜18（3.1 で 6 = 5 A）、connect 19、direction 20、partner flags 21〜28（3.1 で bit 2 USB4）、partner type 29〜31、RDO 32〜63。1.2 は 9 byte、3.1 は 19 byte で **orientation が bit 86**（2.0 にあるかは unconfirmed） |
| DP の HPD・pin（design §13） | **3.1 の GET_CONNECTOR_STATUS に DP の HPD・pin の field は無い**。3.x には GET_ATTENTION_VDO（0x16）・GET_CAM_CS（0x18）がある（DP の Attention・Configuration の VDO と読める、2.x にあるかは unconfirmed）。p005 の「UCSI 2.0 以上の経路」はこの 2 つの command で設計し直すことになる（Q1 に報告） |

## 実装

- `ucsi.c`: `drv_ucsi_start`（PPM_RESET を poll → completed と error の通知 → GET_CAPABILITY → connector ごとに capability・connector の mode の一覧・状態 →
  変化の通知を有効に → 全部を読み直し → 保留の変化の ACK）、`drv_ucsi_service`（通知の後の CCI と保留の connector）、`ucsi_execute`（write → wait → read、
  通知が無ければ refresh で 20→100 ms の poll、busy は待つ、5 s で ETIMEDOUT）、`ucsi_command`（完了の ACK）、`ucsi_acknowledge`、
  GET_ALTERNATE_MODES（2 つずつ offset を進める、空の SVID か少ない返事で終わり）、GET_CAM_SUPPORTED・GET_CURRENT_CAM、GET_PDOS（4 つずつ、7 まで）。
  CCI の connector は全部の read で latch し、ACK の後に保留から外す（PPM は ACK まで同じ connector を CCI に出し続ける）。
- `typec.c`: 記録の表（16 connector）、generation（1 から増えるだけ）、listener（4 つ、lock の外で呼ぶ）、`drv_typec_connector_get`・`_publish`・`_reset`・`_count`。
- `typec-os.h`: lock・unlock・log（kernel の実装は p003）。

## 確かめ（2026-10-07）

- host: `make -C plan/ws050/tests` → 31/31（ASan・UBSan、-Werror -Wshadow -Wmissing-prototypes ほか）。筋書き: 1.2 の start（2 connector、DP の mode に入った
  PD の sink、partner の mode 3 つ = offset 2+1、PDO 5 つ = 4+1）、ACK の規則の違反 0、plug（3 A の source）・generation と listener、unplug（partner の物が消え
  connector の mode は残る）、2 つの変化を続けて、busy の PPM、2.x（0x0200、orientation flipped、USB4、5 A）、配置の選択（0x38 → 1.x、0x210 → 2.x、0x20 → 無し）。
- 試験の効き目: 一時の写しで完了の ACK を抜くと 5 件、保留の処理を 1 つで止めると 2 件 FAIL になることを確かめた（source は変えていない）。
- kernel: `make -C plan/ws050/tests kernel-check`（amd64 の kernel と同じ flag、-Werror）warning 0。stack の最大は `ucsi_pending_handle` の 312 byte。
- style-check 0（新しい 5 file と試験）。
- QEMU・実機: 対象外（kernel に組み込むのは p003）。

## 積み残し（backlog へ）

error・not supported・timeout の回復（CANCEL・PPM_RESET の再列挙、GET_ERROR_STATUS の log）、通知の取りこぼしの poll（Notify の無い時の周期の読み）、
実機の mailbox の記録の再生（p006）、2.0・2.1 の文書での配置と orientation の確認。
