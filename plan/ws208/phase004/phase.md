<!-- awesome-plan project=zedbsd record=ws208-p004 -->

# ws208-p004: 負荷と driver の競合（QEMU）

Phase ID: `ws208-p004`
Parent: [WS208](../ws.md)
Status: in-progress（source の洗い出しと 1 件の直し。QEMU の負荷は T1-537 の結果の後）
Phase disposition: normal
Queue: q939（P2、2026-10-11）
依存: [p003](../phase003/phase.md)

## source の洗い出し（2026-10-11 P2）

Pi 4 で使う driver のうち、「割り込みを止めれば排他」（UP の前提）で共有の状態を守る所を探した。

| 所 | 結果 |
| --- | --- |
| `src/drivers/platform/rpi4/rpi4-firmware.c`（mailbox） | **直した**（c34497c3e）。要求の buffer の `busy` を irq を止めて読み書きしていたので、別の CPU の 2 つの要求が同じ buffer に書けた。atomic の test-and-set にし、取られていれば polling の間隔で待つ（上限は mailbox の待ちの 2 倍、超えたら EBUSY と log） |
| `rpi4-sdhci.c` | mutex（SMP で可） |
| `rpi4-ethernet.c`（GENET）・`bcm54213pe.c` | `device->lock` の spin lock（可） |
| `pci-brcmstb.c`（PCIe の config） | `config_lock` の spin lock（可） |
| `pci.c`・`usb.c`・`pci-xhci.c` | irq を止める所は全て atomic・lock と組み（amd64 で SMP 済み） |
| `rpi4-console.c`、HAL の `bsp-rpi4/cons.c`、`kern/klog.c` | console の出力は lock 済み |
| `src/drivers/gpu/bcm2711/`（WS141） | irq を止めて排他する所は無い。SMP の試験で競合が出たら D5 で WS141 の session へ |

## 確かめ

- rpi4 の vmunix の named build rc 0・warning 0（c34497c3e）。
- QEMU の負荷: 未実施（T1-537 の (b) の結果を見て、足りなければ追加を依頼）。
