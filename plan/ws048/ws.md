<!-- awesome-plan project=zedbsd record=ws048 -->

# WS048: Raspberry Pi 4 の USB（PCIe・VL805 の xHCI・USB キーボード）

<!-- awesome-plan-current:start -->
Status: closed（2026-10-13 ユーザー「WS044、WS048、WS118はクローズします。」。旧: incomplete（p014 の後の実機の起動の成功はユーザー確認、USB の入力は未確認））
Primary Milestone: MG008
Related Milestones: MG003, MG006
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

実機の Raspberry Pi 4 で USB の Type-A の port が使え、USB キーボードで console に入力できる。きっかけは 2026-09-24 ユーザー「USBが使えないみたいです。」（ws044-p009 で実機が login prompt まで起動した後）。要る物: BCM2711 の PCIe の root complex（brcmstb）、VL805 の firmware の mailbox の通知、非 coherent な DMA、xHCI の rpi4 での動作、hub・HID。

## 結果（2026-10-13 close）

- 設計（p001）: [design.md](design.md)（brcmstb の初期化、VL805、DMA、割り込み）。`src/drivers/pci/pci-brcmstb.c` の注から参照されるので残す。
- FDT の reader、arm64 の device mapping の修正、brcmstb の host bridge と PCI の backend（p002）、firmware の mailbox と VL805 の通知（p003）、非 coherent な DMA（p004、承認済みの hal.h の差分 `hal_pmem_map_uncached` を適用）。
- 2026-10-11 の実機の不具合の修正（p008・p010〜p015、ユーザーの写真と log で）: PCIe の最初の MMIO の読み出しの例外、VL805 の DMA の size の契約と page の backing、PCI の DMA の alias と doorbell の flush、command の待ちの実時間の上限、IRQ の acknowledgement と fault の封じ込め、ARM64 の kernel worker の IRQ の有効化、正常な cancel の log の抑制。p014 の後に実機の起動の成功をユーザーが確認。最新の log で USB の hub（`2109:3431`、4 port）の登録まで。
- host 試験: `make -f plan/ws048/tests/host-test.mk run DTB=…`（FDT・brcmstb・mailbox・DMA、ASan・UBSan）。WS141・WS203・WS206 も使っている。

## 制限・残り

- USB キーボードの実機の入力は未確認のまま（WS の目的は未達で close）。xHCI の rpi4 での有効化（p005）、hub と HID で console 入力（p006）、規約の全文と回帰（p007）、USB の入力の活性化（p009 の実機）は canceled。
- QEMU の raspi4b は PCIe を持たないので、PCIe・VL805・USB の確認は実機だけ。
- Future Work の F-025〜F-029（spread spectrum、MSI、`hal_space_unmap_device()`、mailbox の FULL の register、GENET）はこの WS から移したもの（2026-09-27）。続けるなら rpi4 の USB の新しい WS の候補（勝手に移していない）。
- HAL の承認済みの差分 `proposed/hal-pmem-uncached.diff` は Guardrail の承認の表（`plan/guardrail.md` 85 行）から参照される。消すか残すかは Q1。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws048-p001 | 調査と設計 | cleared |
| ws048-p002 | FDT の reader、arm64 の device mapping、brcmstb の host bridge と PCI の backend | cleared（実機は未実施だった） |
| ws048-p003 | firmware の mailbox と VL805 の firmware の通知 | cleared（実機は未実施だった） |
| ws048-p004 | 非 coherent な DMA（`hal_pmem_map_uncached` と `dma.c`、承認済み） | cleared（2026-09-27） |
| ws048-p005 | xHCI を rpi4 で | canceled（2026-10-13、ユーザーの指示で WS を close。旧: uncleared） |
| ws048-p006 | USB の hub と HID キーボードで console に入力 | canceled（2026-10-13、同上。旧: planned） |
| ws048-p007 | 規約の全文の確認と回帰、実機の結果の取りまとめ | canceled（2026-10-13、同上。旧: planned） |
| ws048-p008 | PCIe の最初の MMIO の読み出しの例外の修正 | cleared（2026-10-11、ユーザーの実機で login） |
| ws048-p009 | USB 入力の activation と選んだ class の driver の接続 | canceled（2026-10-13、同上。旧: uncleared、source・build 済みで実機の入力待ち） |
| ws048-p010 | VL805 の起動の時の非 coherent な DMA の size の契約 | cleared（source・build、実機は未実施だった） |
| ws048-p011 | VL805 の command の完了・PCI の DMA の alias と doorbell の flush | cleared（source・build） |
| ws048-p012 | command の待ちの実時間の上限と IRQ・CPU の進行 | cleared（source・host・build） |
| ws048-p013 | IRQ の acknowledgement・EHB・fault の封じ込め | cleared（source・host・build） |
| ws048-p014 | ARM64 の kernel worker の IRQ の entry | cleared（source・build、後に実機の起動の成功をユーザー確認） |
| ws048-p015 | 正常な cancel の log の抑制 | cleared（source・build、実機の log は未確認だった） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。他の WS が使う host 試験（`tests/host-test.mk` と `*-host-test.c`）と `design.md` は残すか `plan/tools/rpi4/` へ移す（Q1）。
