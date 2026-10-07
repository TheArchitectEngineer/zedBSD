<!-- awesome-plan project=zedbsd record=ws052 -->

# WS052: 電源管理（S0i3、modern standby）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-05: HAL の差分の案は専門家のレビューを受けて第 2 版 H1v2・H3v2・H4v2・H5（[proposed/README-v2.md](proposed/README-v2.md)）、承認待ち。p003・p004 cleared、p005・p009 は実機の UAT 待ち）
Primary Milestone: MG003
Related Milestones: MG004, MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p006（2026-10-05 P1 が HAL・kernel・UAPI を実装（910bc04c・ae2c61bf・58554e8b）。T1 の QEMU（S0IDLE の拒否・DEVICES の往復・拒否の試験）と 5330 の UAT（SLP_S0 の residency、電源ボタン・蓋での起床、counter と RTC）待ち）
<!-- awesome-plan-current:end -->

## 目標

ACPI の S0i3（modern standby、S0 low power idle）に入り、wake の event で戻る。操作は `/dev/system` から行う。

## きっかけ

2026-09-24 ユーザー指示: 「電源管理の実装。ACPIのS0i3に対応します。/dev/systemで制御。モダンスタンバイのみで、S3,S4は対応不要。
S0i1, S0i2は必要に応じてサポートを検討するが、基本的にi3のみでよいと思います。」

## 範囲（ユーザーの指示）

- **S0i3 だけ**。S3（suspend to RAM）と S4（hibernate）は対応しない。S0i1・S0i2 は必要になったら検討する。
- 制御は **`/dev/system`**（既にある system の制御 device、`src/drivers/generic/system-device.c`）の ioctl を足す。

## 要るもの（p001 で確かめる）

- ACPI: FADT の `LOW_POWER_S0_IDLE_CAPABLE`、LPS0 の device（`_DSM`、Intel の UUID と Microsoft の UUID: display off/on、entry/exit の通知）、
  各 device の `_PS0`・`_PS3`・`_PRx`・`_DSW`、wake の GPE → **WS049 が前提**。
- Intel の PMC（Alder Lake）: SLP_S0 の residency の確認、PMC の LTR の無視の設定、IP の電源の状態の確認（S0i3 に入れない原因の調べ方）。
- device ごとの suspend/resume: xHCI（USB）、NVMe（APST・D3）、i915（display の電源、DC6/DC9）、HDA、Wi-Fi（RTL8822B など）、有線 LAN、SATA/AHCI の LPM。
- CPU: 全 core の idle を深い C-state（MWAIT の C10 など）にし、timer と割り込みを止める（HAL の idle と timer の口が要るなら承認が要る）。
- wake の源: 電源 button、lid、キーボード（EC の `_Qxx` / GPE）、USB。
- 利用者の操作: `/dev/system` の ioctl（入る、状態、wake の理由）と、それを呼ぶ command。

## 受け入れ

- 対象機で S0i3 に入り（PMC の SLP_S0 の residency が増えることを確かめる）、電源 button か lid で戻り、login した session と network が続く。
- 規約の全文、build、boot test。実機の証拠が中心（QEMU の S0ix は無い）。

## Phase 一覧

| Phase | 内容 | Status | 依存 | 対象 |
| --- | --- | --- | --- | --- |
| [ws052-p001](phase001/phase.md) | 調査と設計: 対象機の FADT・LPS0 の `_DSM`・device の電源の method、PMC の register、driver ごとの suspend/resume の要否と順序、CPU の idle と timer（HAL の口の要否）、`/dev/system` の ioctl の案 | in-progress（2026-10-04。[design.md](design.md) 第 1 版、レビュー待ち） | WS049 の p001 | 設計文書 |
| [ws052-p002](phase002/phase.md) | 詳細の調査と HAL の差分の案（第 1 版 H1〜H4 → 専門家のレビュー → 第 2 版 H1v2 suspend idle＋probe、H3v2 notify の契約、H4v2 wake の arm と IRQ の suspend、H5 counter の契約）、FACP・LPIT、wake の GPE、`_CST`、PMC | in-progress（2026-10-05: 第 2 版 [README-v2](proposed/README-v2.md) を置いた、承認待ち） | p001 | `proposed/` |
| [ws052-p003](phase003/phase.md) | ACPI の側（LPS0 の `_DSM`、wake の GPE、`_PSx`・`_PRx`・`_DSW`）、`_OSI` と電源ボタンの経路（HIDD は不要） | cleared（2026-10-05 Q1、T1-153） | p001、ws049-p007 | `src/drivers/acpi/acpi-sleep.c`、`acpi-event.c` |
| [ws052-p004](phase004/phase.md) | device の suspend・resume の口、必須の NVMe・xHCI（失敗で中止と理由）、`KERN_SYSTEM_SLEEP` の devices だけの mode。i915 は p009 へ | cleared（2026-10-05 Q1） | p003 | `src/drivers/pci/pci-power.c`、`pci-nvme.c`、`pci-xhci.c` |
| [ws052-p005](phase005/phase.md) | HDA・Wi-Fi・LPSS-I2C の「止めて入る」経路 | in-progress（2026-10-05: 3 つの driver に suspend・resume、T1 の QEMU 待ち） | p004 | `pci-hda.c`、`intel-ax211.c`、`lpss-i2c.c` |
| [ws052-p006](phase006/phase.md) | CPU の idle・tick・割り込み（承認された H1v2・H3v2・H4v2・H5 の amd64 の実装と他 arch の stub）、S0i3 の入口・出口（[README-v2 §7](proposed/README-v2.md) の流れ、spurious wake の再突入、CPU 0 の時刻の補正）、user の停止、`/dev/system` の ioctl と事象 | in-progress（2026-10-05 P1 q742: HAL の実装 910bc04c、kernel の流れ ae2c61bf、UAPI 58554e8b。vmunix の link・sleepctl の build（warning 0）まで。T1 の QEMU と 5330 の UAT 待ち） | p002 の承認（済）、p004、p005、p009、WS132 | |
| [ws052-p007](phase007/phase.md) | Keiland の契機（蓋・電源ボタンの短押し・無操作の時間）と中止の理由の表示、networkd が sleep の前に radio を切る流れ | planning（2026-10-05: 設計の第 1 版、Q1 のレビュー待ち。code は p006 の口の後） | p006、WS132 p008、WS089 | |
| ws052-p008 | 実機の確認と規約の全文 | planned | p007 | |
| [ws052-p009](phase009/phase.md) | i915 の suspend・resume（display・DC9・GT の RC6・GGTT・display の core の再初期化・出力の設定の再適用）。設計から、検証は 5330 の UAT | in-progress（2026-10-05: 段 (a)(b)(c) を実装、5330 の UAT 待ち） | p004 | `src/drivers/gpu/i915/park.c`、`worker.c` |
| [ws052-p010](phase010/phase.md) | networkd の SLEEP_PREPARE（opcode 80）・SLEEP_END（81）（p007 の設計 第 4 版 §10） | test-wait（2026-10-07 P2: 実装、build warning 0、host 20/0。QEMU・実機は p011 の後） | p007 の設計 | |
| ws052-p011 | sessiond の sleep の子 process と backend の `POWER suspend`、kernel の CAN_SLEEP の bit（N1 = A1） | test-wait（2026-10-07 P2: kernel の CAN_SLEEP の flag・sessiond の sleep.c・backend、host 13/0・21/0、build warning 0。QEMU・実機は p012 の後） | p010、N1〜N3 | |
| ws052-p012 | compositor の sleep.c（契機・lock の 2 frame・中止の理由の表示） | planned | p011、N5〜N9 | |
| ws052-p013 | Settings の Power の頁（sleep・画面の時間） | planned | p012、N4 | |

## 人間の判断が要る点

- HAL（CPU の idle、timer の停止、割り込みの wake）の差分の承認（p001 が案を作る）。

## 2026-10-04 予定（Q1）

ユーザー「次の新規実装項目は、USB-C の DisplayPort Alternate Modeの実現を目標にします。その次が電源管理です。これらは併走できると思います。共通のpredecessorがAMLですね。」→ [queue.md](../queue.md) の q679（WS050 p001）・q680（WS051 p001）・q681（WS052 p001）。設計は WS049 の q677（BUG-165、DSDT）と並走、実装は q677・q678（ws049-p007）の後。

- 2026-10-05 ユーザー「/dev/system から S0 idle に入るための UAPIの変更を承認します。docs/に電源管理のドキュメントを作成して、記録しておいてください。あとでレビュー対象にできるようにです。」→ P1 の案（`KERN_SYSTEM_SLEEP_S0IDLE`、`wake` の欄、`KERN_SYSTEM_WAKE_*`）を承認。Q1 が `docs/architecture/power-management.md` に設計と UAPI を書いた（ユーザーの review 待ち）。p006 の実装はこの文書に従う。
