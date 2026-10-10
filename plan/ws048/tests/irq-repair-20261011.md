# WS048 p013 IRQ repair evidence — 2026-10-11

Scope: current user report `enable slot doorbell complete` freeze; continued functional repair on private branch `codex/rpi4-xhci-irq`, base main `4b70292ef`. User supplied `DMA PCI 400000000 CPU 0 size c0000000 window 100000000`: PCI alias 16 GiB, CPU base zero, usable range 3 GiB, rounded aperture 4 GiB. No DMA mapping change in this attempt.

## Changes and source review

- Acknowledge USBSTS.EINT alone, flush IMAN.IP/IE writes, and clear/read back ERDP.EHB at batch completion even for an empty event ring.
- Preserve EHB while consuming a batch; polling clears it only when no IRQ consumer owns the batch. Read volatile event cycle first and apply the DMA read barrier before copying payload words.
- Bound each IRQ event batch to one ring (256 descriptors). Preserve command-pointer matching/handoff and command-recursive callback deferral.
- Reject fatal/HCE/inaccessible status before IRQ event consumption. Publish a sticky fault under the event lock, block new controller operations, request stop without waiting in IRQ, disable this PCI function's INTx, and retain DMA. The command owner returns EIO on the published fault; this does not claim DMA quiescence or automatic hardware recovery.
- Add RPi4 Enable Slot first-wait/scheduler-return logs. Hardware MMIO stalls or a scheduler that never returns remain outside a software loop deadline.

Protocol comparison: [Linux xhci-ring.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/host/xhci-ring.c), `xhci_clear_interrupt_pending`, `xhci_update_erst_dequeue`, `xhci_irq`. Reference used for hardware sequencing, implementation original. These source deficiencies do not establish which one caused the physical freeze.

Final review covered changed production code and host fixtures against AGENTS, Guardrail and full coding-style.md: event/active lock ordering, IRQ restoration, command ownership, fault admission, retained DMA, declarations, comments, split calls and function order. clang-format 19.1.7 was applied during implementation; canonical function/call layout and unrelated formatting were restored manually. No HAL API/UAPI/toolchain changes. Existing unmodified driver style was not mass-reformatted.

## Verification

| Command | Result |
| --- | --- |
| `python3 plan/ws048/tests/command-wait-host.py` | PASS, 11 scenarios using extracted production command function; includes IRQ fault during wait and fault before admission |
| `python3 plan/ws048/tests/irq-host.py` | PASS, 8 scenarios using extracted production IRQ/event functions: empty acknowledgement, high-address command handoff, continuous producer bound, HCE/fatal/inaccessible status, shared IRQ, suspended controller |
| `make -j16 build/arm64/vmunix` | exit 0, warning 0, error 0; ELF and raw-image checks PASS, entry 0xffff000000080000 |
| `cmp config.mk /home/awe/zedBSD-claude1/config.mk` | identical current RPi4 configuration |
| `git diff --check` | PASS |

Tools: Debian cc 14.2.0, clang-format 19.1.7, repository clang 23.1.0 (d7f1bbaca898fb5f4cc373b082e915ec1a07310f). Build log is private disposable `build/rpi4-irq-repair-build.log`. Fixtures are referenced by this incomplete WS for the current unresolved hardware investigation.

Final artifact SHA256:

```
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
607d2fbe8c9e0c3b34f1d38d67f0f22429370d7a583c029b8282ee2a47197f68  src/drivers/pci/pci-xhci.c
1ae21a7dbb3f3e1b3d182eabce89f96faaba944e57ab0fc42a9ed19b9e2a2ab7  build/arm64/kernel.elf
4a401f96c2b1bfd744f71cb24c968d09691c46818d6061fc3002492d7f00758d  build/arm64/vmunix
```

## Limits and resume

Source/build criteria cleared. Physical boot, USB input, real posted PCI acknowledgements and DMA are **unverified**; host models do not prove these. No SSH attempt because the user reports boot freezes before sshd. No QEMU, full SD image build, remote deployment/reboot, main integration or push in this attempt. WS048 remains incomplete; p009/p006 hardware acceptance remains unmet.

Next physical evidence should distinguish `enable slot completion wait irq=...`, `enable slot completion poll resumed`, controller IRQ fault status/mask result, and command timeout. A missing stage is evidence, not a successful recovery. Fatal containment deliberately leaves the USB controller offline and DMA retained until a supported teardown/restart or reboot.

Concrete commit integration is pending user/Q1 approval. Shared Master/Queue/history/cache/GitHub projections are Q1 pending; boot0 and CI patches remain separate.
