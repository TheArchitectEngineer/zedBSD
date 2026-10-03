# What runs before N0, and what the N0-STOP exit does (E-121, code reading; probe.c line numbers at E-121)

Summary: nothing before N0 or on the N0-STOP teardown writes a display register, a GGTT PTE or a D-state. The writes
that do happen are GT-side and PCI-config-side. "Before any display write" is accurate; "nothing touched" is not.

## Before N0
- PCI config: probe runtime-PM get (usage count only; no PMCSR write); pci_enable_device → set_power_state(D0) RMW of
  PMCSR (already D0; may clear PME_Status), COMMAND saved then MEM/IO decode OR-ed in; pci_set_master (BME on); MSI
  vector programmed + MSI enable with no handler (kept); ROM BAR (0x30) touched only when no explicit VBT is used.
- MMIO (GT only): forcewake RENDER / GT; MCR select 0x0fdc; L3BANK steering; error-register clears (PGTBL_ER, IPEIR,
  EIR, EMR, GEN2_IIR, RING_FAULT); **GT reset** GDRST full (reference sanitize_gpu; ADL-P gpu_reset_clobbers_display is
  false); **fence clears** FENCE_REG_GEN6_LO/HI(0..31) = 0 (the reference does the same in i915_ggtt_init_hw →
  intel_ggtt_init_fences; small order / posting-read differences recorded; fences affect CPU tiled access through
  GMADR only, not scanout); PCODE read mailbox commands (dram / QGV / PSF).
- GGTT / DMA / mappings: UC CPU mapping of the GSM (reads only; no PTE written before P6 → GGTT pages 0..300 untouched);
  scratch page allocated and zeroed in RAM (PTE only encoded); WC CPU view of the whole aperture; read-only OpRegion /
  VT-d mappings in N0.
- P3.1–P3.5: drm_dev_init / vblank_init / bios_init / vga_register / power_domains_init / pmdemand_init_early —
  software (and option-ROM / config reads).

## Teardown after N0 STOP
Only the guards set so far run: power_domains_cleanup (no MMIO), bios remove (frees the parsed VBT), vga_unregister
(pointers only; no VGACNTRL / SR01 / GMCH write), drm_dev_fini (joins vblank workers), MSI disable + vector free, WC
unmap of the driver's own window (the console's boot-framebuffer mapping is separate), scratch / DMA / BAR unmaps,
**COMMAND restored to the firmware's value**, runtime-PM put (usage 0 with runtime PM not enabled during probe → no
D3hot). No GT stop / reset, no GGTT scratch fill, no display fini, no IRQ uninstall, no uninitialised display-stop code.

## Left behind / to watch
- GT state is not restored (fault / error registers, fences, the GT reset, MCR steering) — none of it is display.
- BME / decode: the restore writes back exactly what the firmware left (the value is now logged by N0: "PCI COMMAND
  firmware=… now=…").
- Cache-type alias: the WC aperture view overlaps the console's UC mapping of the GOP framebuffer during P2–P3 (Linux
  shares this with efifb + io_mapping_init_wc); consider mapping the aperture WC only after N0 proceeds.
- MSI enabled without a handler from P2 to teardown (harmless unless the firmware left an interrupt source enabled).
