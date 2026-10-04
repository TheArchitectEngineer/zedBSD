<!-- awesome-plan project=zedbsd record=ws052p004 -->

# ws052-p004: device の suspend・resume の口と、必須の i915・NVMe・xHCI

Phase ID: `ws052-p004`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-05 P1 generation17。PCI の口・NVMe・xHCI は実装済み（vmunix の link、host の試験）。i915 は未着手。QEMU は未実施）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（p003 の次。HAL に依らない範囲、1 つでも失敗したら中止し原因の device を返す）

## 範囲

- device の suspend・resume の口: PCI の driver の `suspend`・`resume`（`struct drv_pci_driver` に既にあった欄を使う）、子から親への suspend・
  親から子への resume、失敗したら suspend 済みの device を resume して中止し、原因の device を返す。
- 必須の 3 つ: i915（scanout の停止、DC6/DC9、RC6、D3hot、resume は suspend の前の出力先へ）、NVMe（flush、shutdown、D3hot）、xHCI（controller の
  停止、port の状態の保存、PME）。
- HAL に依らない。S0i3 の入口・出口そのもの（user の停止、CPU の idle、`/dev/system`）は p006。HDA・Wi-Fi などの「止めて入る」経路は p005。

## 実装（2026-10-05、途中）

- `src/drivers/pci/pci-power.c`（新規、amd64 の vmunix に追加）:
  - `drv_pci_suspend_all(&failed)`: driver の付いた全 function を bus の深い順（bridge の先が先）に: driver の `suspend` → 構成の保存 → driver が
    `drv_pci_device_set_wake()` で求めたら PME_En と platform の wake → PM capability で D3hot → platform（ACPI）の D3hot。どこかで失敗したら
    その function の済んだ段を戻し、それまでの function を逆順に resume し、`failed` に原因の function を入れて失敗を返す。`suspend` の無い driver は
    ENOTSUP で中止（p005 の「止めて入る」経路ができるまで、HDA・Wi-Fi・LPSS-I2C が付いた 5330 では中止になる。§10-5 のとおり）。
  - `drv_pci_resume_all()`: 浅い順に platform の D0 → D0（10 ms）→ 構成の書き戻し → wake の解除 → driver の `resume`。
  - `drv_pci_device_save_state()`/`restore_state()`: 先頭 64 byte、MSI、PCI Express の制御（DevCtl・LnkCtl・DevCtl2・LnkCtl2）、LTR、
    L1 PM Substates、MSI-X の制御と table 全体（No_Soft_Reset の無い function は D3hot→D0 で失うため）。
  - `drv_pci_device_set_power_state()`（PMCSR、D0/D3hot、10 ms）、`drv_pci_platform_power_set()`（platform の口）。
- `src/drivers/pci/pci.c`: `drv_pci_device_map_msix_table()`（driver の claim を問わず MSI-X の table を map）。
- `src/drivers/acpi/acpi-pci-power.c`（新規）: platform の口の ACPI 版。PCI の function の namespace の device を `_ADR`・`_BBN`・`_SEG`・bridge から
  探し（PCI-to-PCI bridge でない function の下には降りない: USB の port・GPU の出力の `_ADR` が別の function に見えるため）、cache し、
  `drv_acpi_device_power_set()`・`drv_acpi_device_wake_enable()`（p003）を呼ぶ。`acpi-kern.c` が boot で登録する。
- `src/drivers/acpi/aml-field.c`: region の PCI の解決を `drv_acpi_pci_location()` と `drv_acpi_pci_below_root()` に切り出した（`aml-internal.h`）。
- `src/drivers/pci/pci-nvme.c`: `nvme_suspend`: `suspended` で新しい block request を待たせ（失敗にしない）、受け付け済みの request の終わりを待ち、
  割り込みを mask、normal shutdown（volatile cache の書き出し）、disable、bus master を外し、割り込みの drain、I/O の lifecycle を quiesce。
  `nvme_resume`: disable → queue の memory の reset → enable → 割り込みを元に → I/O queue の作成 → 待たせた request を通す。起きない controller は
  quarantine。suspend の途中の失敗は controller を起こし直して失敗を返す。

- `src/drivers/pci/pci-xhci.c`（xHCI 1.2 §4.23.2 の Save/Restore State）: `xhci_suspend`: gate を閉じる（operation は待ち、submission は TRB を
  ring に置くが doorbell を鳴らさない）、interrupt の poll 以外の転送の終わりを待つ（2 秒で EBUSY）、port の worker を止め、動いている endpoint を
  Stop Endpoint（SP）で止める（その Stopped の転送の事象は request を ring に残す）、接続のある root port を U3、RS を下げて HCH、interrupter を
  止め、Restore に要る register（DNCTRL・CONFIG・DCBAAP・IMOD・ERSTSZ・ERSTBA）を保って CSS。`xhci_resume`: register を戻し、ERDP を driver の
  event ring の位置に、CRCR を command ring の位置にして CRS、RS、port を U0（USB 2 は Resume を 20 ms）、全 endpoint の doorbell、gate を開け
  worker を再開。SRE（QEMU は Restore を実装せず必ず立つ）・HCE なら `ESTALE` を返し、PCI の口がその function を detach・attach し直す（USB の
  device は列挙し直し。mount 中の USB storage は外れる）。USB の wake（remote wakeup、port の wake の bit）はまだ有効にしない。
- `pci-power.c`: driver の resume が `ESTALE` なら `drv_pci_device_reprobe()`（host の試験に追加）。

## 確認（ここまで）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| PCI の口の host の試験（ASan/UBSan） | `make -C plan/ws052/tests pci-power && build/ws052/host/pci-power` | PASS。模擬の 5 function（bridge の先 1 つ）で順（D → A → C、resume は逆）、C の wake（PME_En と platform）、D3hot→D0 の reset で失う command・BAR・LnkCtl・LTR・MSI-X の table が戻ること、driver の失敗・platform の失敗・suspend の無い driver で中止して前の function を戻し原因を返すこと、二重の suspend は EBUSY、suspend の無い resume は EINVAL |
| p003 の host の試験と WS049 の回帰 | `run-host-sleep.sh`、`check-latitude5330.sh`、`run-asl.py absentpci pcibridge` | PASS |
| vmunix の link | `make vmunix` | PASS、warning 0 |
| QEMU・実機 | — | 未実施（呼び出し元の `/dev/system` の ioctl は p006。QEMU で NVMe の suspend・resume の往復と disk の I/O の継続を見るには、p006 の ioctl に「device だけ」の試験の口を足すか、試験用の口が要る） |

## 残り

- i915: scanout の停止、DC6/DC9、GT の RC6、D3hot。resume は suspend の前の出力先（Keiland の指示の分を含む）。
- 5330 の LPSS-I2C（touchpad）は p004 の必須に入っていないが driver が付くので、p005 までは中止の原因になる。**p005（止めて入る）で扱う**（2026-10-05 Q1）。
- QEMU の試験の口（2026-10-05 Q1 の指示）: p006 の `/dev/system` の ioctl の「devices だけ」の mode（root だけ、HAL に依らない: device の
  suspend→resume の往復だけ、CPU の深い idle・tick・割り込みの mask には触れない）を先に作り、T1 に NVMe・xHCI の往復と、suspend の無い driver で
  中止して理由が返ることを依頼する。
