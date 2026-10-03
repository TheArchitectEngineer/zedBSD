# WS031 E-122 報告 — OpRegion をデータ取得だけに使う（VBT_ONLY）＋受信側の本体（shadow と合成通知）＋合成 ASLE による実 LCD の輝度変更

日付: 2026-09-20　HAL・既存 UAPI の変更はありません。commit／push もしていません。

## 0. 要約

| 項目 | 結果 |
|---|---|
| native 第二の採取（E-121 イメージ、利用者が実行） | N0 の VBT: OPREGION(RVDA) を観測、parser が採用したのは EXPLICIT_BLOB、**same bytes=1**。DPCLKA_CFGCR0=0x01e07801（PHY A → **DPLL1**）。pipe A は READABLE_ACTIVE、B・C は READABLE_INACTIVE、D は POWER_OFF。条件は ACTIVE_PIPE と OPREGION の 2 つ。VT-d 由来の条件はなし（IRES=0）。TRANS_DDI_FUNC_CTL=0x8a210102 の bit8（VC_PAYLOAD_ALLOC）は firmware の差（正本では MST の経路だけで使う） |
| VBT_ONLY（前回の判断） | P2 で読み取り専用 map → コピー → VBT 取得 → parser（明示 blob → OpRegion → ROM）。runtime=DISABLED。`intel_opregion_register` での BLOCKED は撤回。native 用の `zedbsd-native-e122.img` は明示 blob を無効にして、OpRegion から採用させる |
| OpRegion の受信側（今回の方針） | 正本の setup から cleanup までを生成し、**shadow**（driver 所有の RAM）と**合成イベント**で動かした。通知の登録・配送（zedBSD 側のコード）、共有 worker、受付の関門、境界（_DSM、DMI、SWSCI）を記録 |
| GPU なし試験 | **ktest 582/0**（+43: OP-SETUP／REGISTER／SWSCI／NOTIFY／ASLE／LIFECYCLE） |
| **実 LCD の輝度（LCD-O）** | **PASS**: 合成 ASLE の BCLP 10／64／160／255 → PWM duty 5647／24094／60235／96000 が、正本の clamp_user_to_hw と一致。ASLC 0、CBLV は期待どおり。元の輝度に戻し、service を解除した。写真 6 枚。firmware の OpRegion への書込みは 0 |
| 回帰（最終 source） | **14/14 PASS**（`sweep_e122.sh`: 既存の 13 モード＋LCD-O、各 ktest 582/0、runner はすべて COMPLETE、HW 由来の TLB timeout は 0 件） |

## 1. 通知の向きと用語（利用者の問いへの回答、専門家の整理）
- `register_acpi_notifier` は、受信 callback の登録です。i915 は受信側です。
- 通知は、ACPI 側（将来は AML の Notify → ACPI video）から `acpi_notifier_call_chain` を通って i915 の callback に届きます。
- `drdy=1`（受付準備の公開）、`csts=0`（処理状態の更新）、`ardy`（ASLE の受付準備）は、共有メモリの欄への書込みです。通知を送る操作ではありません。
- OpRegion は、firmware と driver が情報を交換する共有メモリです。VBT の取得（データ）と、実行時の連携（通知と応答）は別の機能として扱います。

## 2. 実装
- **通知の登録と配送**（`parity/opregion_service.c`）: 契約は公開 v6.8.12 の `drivers/acpi/event.c` と `kernel/notifier.c` で確認しました（-EEXIST、-ENOENT、優先度順、STOP_MASK で停止、NOTIFY_BAD → -EINVAL、blocking）。これらは GPL-2.0 なので、原文は写していません。配送と登録・解除は 1 本の mutex で直列化します（正本の rwsem との差は適応として記録）。
- **正本からの生成**（`lcd/intel_opregion_port.c`、`intel_acpi_port.c`、`opreg_struct.h`、`opreg_pci_config.h`）: mailbox の構造体、callback、ASLE の処理（全要求）、worker、setup から cleanup まで、DIDL／CADL、SWSCI の不在分岐。check_generated は一致。
- **service**（`opregion_compat.h`、`parity_opregion_glue.inc`）:
  - memremap の対応表（ASLS の値 → shadow、ASLS+RVDA → shadow VBT）、connector の表、共有 kworkqueue。
  - backlight の policy は構成の入力（video／vendor なら処理、native なら正本どおり処理しない）。
  - **受付の関門**: GSE の受信入口は、受付中だけ queue します。unregister は、関門を閉じてから正本の停止処理（ARDY NOT_READY → cancel_work_sync → DRDY 0 → notifier 解除）を行います。cleanup は、work が idle であることを確かめてから解放します（確かめられなければ解放しません）。
- **実 LCD**: 正本の `intel_backlight_set_acpi` を backlight の生成ファイルに追加しました（そのために互換定義 `drm_connector_state` に `crtc` を足し、modeset が設定します）。LCD-O モードで、カーネルが持つ shadow の上の service から、既存の実 backlight へつなぎます。

## 3. 試験の結果（抜粋）
- OP-REGISTER: DIDL／CADL = 400、300、301、0（eDP、DP、HDMI。ACPI 5.0 B.3.2 から別に計算した値と一致）。CSTS 0、DRDY 1、TCHE 2、ARDY 1（shadow の中）。_DSM は境界として記録。SWSCI は -ENODEV で、PCI アクセスは 0。
- OP-NOTIFY: 正本の判定表どおり（非 video → DONE で CSTS を書かない、0x80 で bit0 → OK、bit0 なし → BAD（dispatch -EINVAL）でも CSTS=0、0x81 → OK）。解除後は配送されない。
- OP-ASLE: 有効値 128 → set_acpi(128,255)、CBLV 0x80000033、ASLC 0。valid なし／範囲外 → BACKLIGHT_FAILED。native → 何もせず成功。混在 → 0x4400。
- OP-LIFECYCLE: callback 実行中の解除は、callback の終了後に戻る。work 実行中の停止は、work の終了（応答の書込み）を待つ。停止後の要求は関門で破棄。保留中の要求は取り消されて実行されない。setup の失敗、再初期化も確認。
- LCD-O（実 LCD）:
  - 最初の実行: duty は 24094／60235 で、ハードウェアは正本の値でした。ただ、**試験側の期待値の式が誤っていた**（`[min, max]` へ拡大縮小すると置いていた）ため FAIL になりました。正本の clamp_user_to_hw（`[0, max]` へ拡大縮小してから `[min, max]` に制限）に直し、下限の例（10/255 → 5647）を加えて再実行し、PASS しました。最初の実行の log も保存しています。

## 4. 残り
- 実イベント源への接続（AML の Notify、GSE の IRQ decode、実共有 mailbox、READY の公開）は、`notes/opregion-register-scope.md` §4 の手順で進めます。
- native 第三の採取（E-122 イメージ）: firmware 自身の DRDY／ARDY などの値と、OpRegion から採用した VBT を確認します。
- N1（firmware の表示の引継ぎ）の本体、同じ context を再利用する renderer。
- 外部ディスプレイ: HPD の割込みの設定（gen11／icp の hpd_irq_setup）は移植済みで、割込みは受け取って ack している（DE HPD、DE PORT、SDE）。decode（intel_hpd_irq_handler → hotplug worker → detect）と外部 connector はまだない。表示切替そのものは OpRegion の機能ではなく、ACPI video の通知を受けたユーザー空間の modeset。実装の段階で、利用者が外部ディスプレイを接続してカメラで確認する予定。
- GSE の IRQ は、正本の CONFIG_ACPI=n と同じく、有効化して ack するだけで、ASLE の処理は呼びません。専門家の「IRQ source も有効化しない」との差は、確認をお願いします。

## 5. 成果物
- 報告: `expert-reports/report-e122-opregion-service.md`、notes: `opregion-register-scope.md`（今回の実装に合わせて更新）
- 作業 script: `handover/tools/lcd-e122/`（round65〜72、op/）、`sweep_e122.sh`
- log／写真: `increment-results/e122-run-parity-hw-*.log`、`e122-photos/lcdo2-*.jpg`（一覧 `lcdo2-sheet.jpg`）
- USB: `C:\Work\qemu-work\usb\zedbsd-native-e122.img`、`README-native-e122.md`
