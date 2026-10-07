<!-- awesome-plan project=zedbsd record=ws186 -->

# WS186: Realtek RTL8822CE（PCIe の WiFi、Latitude 5320）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG003
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Target: **ベータ3**（2026-10-07 ユーザー「RTL8822Cは、WSを立てて、ベータ3にしておきます。けど、ベータ2が期日前に完成したら、やるかもしれません。」）
Resume point: p001 の調査と設計から。
<!-- awesome-plan-current:end -->

## 目標

Latitude 5320 の内蔵の PCIe の WiFi（Realtek RTL8822CE）を zedBSD で使えるようにする。

## 既知の事実（2026-10-07 Q1、source と Linux の rtw88 からの見立て、未確認）

- RTL8822B と同じ系統（Linux では rtw88 の `rtw8822b.c` と `rtw8822c.c`）。今の zedBSD の driver（`src/drivers/wifi/rtl8822b/`、rtw88 を参考）と USB の transport（`src/drivers/usb/usb-rtl8822bu.c`、Archer T3U）がある。
- 使い回せそうな所: firmware の読み込み・H2C/C2H の枠、efuse の枠、受信の解釈、CCMP、上の WiFi の層（scan・接続・WPA）。
- 作り足す所: PCIe の transport（DMA の送受信の ring・割り込み・PCIe の電源、rtw88 の `pci.c` に当たる）、8822C の chip 固有（PHY・RF の表、IQK・DPK・DACK の校正、電源の順、firmware `rtw8822c_fw.bin`）。
- license: rtw88 は GPL-2.0 か BSD-3-Clause。表（`.inc`）は RTL8822B と同じく独立の file に分ける（AGENTS.md の規則）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| p001 | 調査と設計: 5320 の PCI ID・subsystem、表の量、firmware の入手と license、PCIe の transport の設計、8822B の driver との共通化の線 | planned | — |
