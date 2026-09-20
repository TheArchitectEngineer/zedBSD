# OpRegion — 範囲、現在の実装、試験、実イベント源への接続手順（E-122 時点）

正本: Linux v6.8.12 i915（`display/intel_opregion.c`、`display/intel_acpi.c`、`display/intel_backlight.c`、`intel_pci_config.h`、`display/intel_opregion.h`）。
ACPI 側の契約（`drivers/acpi/event.c`、`kernel/notifier.c`）は公開 v6.8.12 で確認しました。これらは GPL-2.0 なので、原文は写さずに契約だけを実装しています。
対象機: OpRegion 2.1、mboxes 0x1d（ACPI bit0、ASLE bit2、mailbox#4 VBT bit3、ASLE_EXT bit4。SWSCI bit1 と backlight bit5 はなし）。VBT は RVDA（相対 0x2000、8704 byte）。

## 1. 境界と状態（いま有効な範囲）

| 責務 | 通常動作（native／VM） | 試験（SHADOW） |
|---|---|---|
| VBT の取得 | **有効**: P2 で読み取り専用 map → コピー → 検証 → parser（明示 blob → OpRegion → ROM の順） | shadow の RVDA → 対応表 → shadow VBT |
| ACPI video 通知の受信 | 参加しない（runtime=DISABLED、ACPI_RUNTIME_UNAVAILABLE） | 合成イベント → 通知の登録・配送 → 正本の callback |
| ASLE 要求 | 参加しない（ARDY は書かない） | 合成 GSE 入口 → 関門 → 共有 worker → 正本の asle_work |
| SWSCI | 対象外（mailbox なし） | 不在分岐（-ENODEV）と PCI アクセス 0 件を確認 |
| 準備状態の公開（DRDY／ARDY／TCHE／CSTS／DIDL／CADL／CHPD） | 書かない（firmware の値は観測だけ） | shadow の中だけで、正本の setup／resume／suspend が書く |
| _DSM、DMI | 評価しない（AML なし、DMI データなし）。境界として記録 | 同じ |
| 実共有領域への書込み | 0（書き込める map が存在しない） | 0（同じ） |

## 2. 正本との対応（生成物と glue）

- `lcd/intel_opregion_port.c`（生成）:
  - 範囲: mailbox の定義と構造体、ACPI_EV_*、power_state_map、intel_no_opregion_vbt
  - 関数: asle_set_*、asle_work、intel_opregion_asle_intr、intel_opregion_video_event、check_swsci_function、swsci、intel_opregion_notify_adapter、set_did、intel_didl_outputs、intel_setup_cadls、intel_no_opregion_vbt_callback、intel_load_vbt_firmware、intel_opregion_setup、_register、_resume_display、_resume、_suspend_display、_suspend、_unregister、_cleanup
- `lcd/opreg_struct.h`（生成）: struct intel_opregion、OPREGION_SIZE
- `lcd/opreg_pci_config.h`（生成）: ASLE、ASLS、SWSCI、SWSCI_SCISEL、SWSCI_GSSCIE
- `lcd/intel_acpi_port.c`（生成）: _DOD の ID の定義、acpi_display_type、intel_acpi_device_id_update
- `lcd/intel_backlight_port.c`（生成）に intel_backlight_set_acpi を追加
- `parity/opregion_service.c`（zedBSD 側のコード）: ACPI notifier chain。-EEXIST、-ENOENT、優先度順、STOP_MASK で停止、NOTIFY_BAD → -EINVAL。blocking（1 本の mutex で配送と登録・解除を直列化する。rwsem を使う正本との差は適応として記録）
- `lcd/opregion_compat.h`、`lcd/parity_opregion_glue.inc`: service の instance、memremap の対応表、ASLS の値、connector の表、共有 kworkqueue（INIT_WORK／queue_work／cancel_work_sync）、backlight の policy（acpi_video_get_backlight_type は構成の入力）、境界の記録、**受付の関門**（GSE は受付中だけ queue する。unregister は関門を閉じてから正本の停止処理を行う。cleanup は work が idle であることを確かめてから）
- 適応の記録: DMI は何も一致しない、_DSM は評価しない、SWSCI の config アクセスは提供しない（到達したら誤りとして記録）、drm_dbg は出力しない、msleep／wait_for は SWSCI の経路でのみ使い、到達しない

## 3. 実行できる試験（GPU なし: `lcd/opregion_ktest.c`、ktest に登録済み）

| 群 | 入力 | 期待（正本から、試験とは別に導いたもの） |
|---|---|---|
| OP-SETUP | shadow（2.1、0x1d、RVDA → shadow VBT） | CHPD=1、ARDY=NOT_READY、RVDA の VBT が有効 |
| OP-REGISTER | connector: eDP（fake の backlight）、DP、HDMI | DIDL／CADL = 0x400、0x300、0x301、0。CSTS 0、DRDY 1、TCHE BLC_EN、ARDY READY。_DSM は境界に 1 回記録。2 回目の register は重複登録を拒否（-EEXIST）し、表の登録は 1 件のまま |
| OP-SWSCI | notify_adapter(D0) | -ENODEV、PCI アクセス 0 |
| OP-NOTIFY | 非 video／0x80 で CEVT bit0 あり／bit0 なし／0x81 | DONE・CSTS そのまま／OK・0／BAD（dispatch -EINVAL）・0／OK・0。STOP で配送停止、解除後は配送されない |
| OP-ASLE | BCLP の有効値／valid なし／範囲外／native policy／混在／要求なし | set_acpi(128,255)、CBLV 0x80000033、ASLC 0／BACKLIGHT_FAILED／同じ／backlight は変えず ASLC 0／0x4400（backlight は処理）／応答しない |
| OP-LIFECYCLE | setup 失敗、callback 実行中の解除、work 実行中の停止、停止後の要求、保留中の停止、再初期化 | 登録されない／callback の終了後に戻る／work の終了（応答の書込み）後に ARDY と DRDY が 0／関門で破棄／取り消されて実行されない／世代が進む |

実 LCD（`-DPARITY_LCDO_TEST=1`、run-parity-ref-240）: SHADOW の service と合成 GSE で、BCLP 10／64／160／255 → PWM duty 5647／24094／60235／96000。正本の clamp_user_to_hw（`scale(level, 0..255 → 0..max)` の後に `[min, max]` へ制限）と一致し、ASLC は 0、CBLV は期待どおり。通常経路で元の輝度に戻して、service を解除する。写真あり。

## 4. 将来、実イベント源へ接続する手順
1. **ACPI 側**: AML の Notify（通知対象の namespace object と値）→ ACPI video 側の処理 → 正規化した event（device_class、bus_id、type、data）→ `parity_acpi_notifier_call_chain()`（event_source="ACPI"）。受信本体は今回のものをそのまま使う。video 側の仕事（表示切替 policy）は別に実装する。
2. **GSE**: GU_MISC の GSE の decode → `parity_opregion_gse_entry()`（関門あり）。今は IRQ を ack するだけ（正本の CONFIG_ACPI=n と同じ）。IRQ の decode 部分は fake register で確認してから接続する。
3. **実共有 mailbox**: backend FIRMWARE を追加する。map の属性（WB）、アクセス幅、公開の順序、寿命（cleanup を IRQ uninstall の後にするなど）を確認する。対応表を差し替えるだけで完成とはしない。
4. **READY の公開**: 実際に受信と終了処理が成立した経路だけで、DRDY と ARDY を独立に有効にする（ASLE だけを先に有効にしてもよい）。
5. native の firmware が残した DRDY／ARDY などの値は、P2 のログ（「runtime mailboxes as found」）で観測できる（E-122 の native イメージで採取を依頼中）。

## 5. ログの項目
`mailbox_backend`（SHADOW／FIRMWARE）、`event_source`（SYNTHETIC／ACPI／GSE entry）、`display_backend`（MODEL／HARDWARE）、`service_epoch`、`callback_result`／`dispatch_result`、`request_before`／`response_after`、`worker started／finished、queued new／pending`、`real_opregion_write_count`、`unregister_synced`、`resources_released`（cleanup の結果）。
