# N1: taking over the firmware-lit display — dependency map (E-121, code reading only)

Reference = Linux v6.8.12 i915 (`ms` intel_modeset_setup.c, `drv` intel_display_driver.c, `pi` intel_plane_initial.c,
`dpll` intel_dpll_mgr.c). Port = `src/drivers/gpu/i915/parity/` (`dn` display_nogem.c).

Native input (E-120 first native frame): pipe A active (TRANSCONF 0xc0000000), transcoder A → DDI A, DP SST 6 bpc
2 lanes (TRANS_DDI_FUNC_CTL 0x8a210102; bit 8 VC_PAYLOAD_ALLOC set by firmware, MST-only in the reference), **DPLL1**
enabled+locked (Linux's own modeset uses DPLL0), plane 1 PLANE_CTL 0x94000008, SURF 0, stride 2560 B, 640×480 (GOP mode
set by the boot loader), panel power on, backlight PWM on. Pipes B/C powered + inactive, D off.

## 1. Reference order
probe_noirq (power_domains_init_hw, dmc, cdclk, dbuf, bw) → irq_install → **probe_nogem** (crtc/dpll init, init_hw,
vga_disable, setup_outputs, **intel_modeset_setup_hw_state** `ms`:934, initial_plane_config `pi`:305) → gem_init
(init_ggtt clears only the holes, so a vma pinned earlier survives).

intel_modeset_setup_hw_state: INIT wakeref → readout (crtc `hsw_get_pipe_config`, planes, encoder get_hw_state +
**get_config → shared_dpll**, **DPLL readout: pipe_mask / active_mask + PLL wakeref**, connectors, inherited=true, bw /
cdclk state) → encoder power domains → per crtc: vblank reset, active → PIPEDMC + vblank_on → fbc / plane-mapping /
encoder sanitize → connector atomic state → sanitize_crtc (disable_noatomic only without encoders or on link reset) →
**DPLL sanitize (off only if on && !active_mask)** → wm readout, crtc power domains → release INIT, power-domain
sanitize → initial plane: fb from stolen at the plane's GGTT offset, pinned PIN_OFFSET_FIXED, held by plane_state.

For the observed state the reference **keeps pipe A running** through setup; it is stopped by the first full modeset
of the inherited state (hsw_crtc_disable, DDI post-disable, DPLL1 off), then the initial fb is released.

## 2. Port status
| item | port | status |
|---|---|---|
| crtc readout | dn:1104, 1189 | partial (no DDI config, port_clock, shared_dpll, inherited) |
| plane readout | dn:1151 | enable bit only |
| encoder get_config / sync_state | — | missing → shared_dpll never set |
| DPLL pipe_mask / active_mask | dn:1255–1272 | **missing (always 0), no PLL wakeref** |
| connector readout | — | missing |
| encoder / crtc power domains | — | missing |
| vblank_on for active crtcs | dn:1500–1510 | partial (PIPEDMC write real) |
| sanitize_crtc | dn:1370 | partial |
| crtc_disable_noatomic | dn:1392 | flag only (bodies exist in lcd/: hsw_crtc_disable, intel_ddi_post_disable, intel_disable_shared_dpll) |
| DPLL sanitize | dn:1421 | **hazard: turns DPLL1 off under the live pipe** (N0 stops before it) |
| wm readout | dn:1520 | flag only |
| power-well sanitize | dn:1452 | safe only while INIT is held; needs the encoder / crtc refs first |
| initial plane / fb reserve | — | missing; no stolen-memory region |

## 3. Buckets
- **A (no display change):** hooks icl_ddi_combo_get_config (DPCLKA_CFGCR0 → DPLL id), ddi_get_power_domains,
  connector get_hw_state, full skl_get_initial_plane_config; crtc_state with inherited; wiring crtc_disable_noatomic to
  the lcd/ bodies; GGTT reservation record; N0 record kept.
- **B (old state kept):** the readout (power-gated reads), DPLL masks + PLL wakeref, encoder / crtc power refs, real
  vblank_on + PIPEDMC for pipe A, reserve + pin the initial fb GGTT range and backing.
- **C (after readout):** fifo / fbc / encoder sanitize, sanitize_crtc, DPLL sanitize (keeps DPLL1 with correct masks),
  wm readout, power-well sanitize after the step-9 refs.
- **D (after the stop is confirmed):** first full modeset of the inherited state (stop pipe A / DDI A / DPLL1 /
  backlight / PPS by the reference path), then release the firmware fb vma, then new GGTT use at offset 0, zedBSD
  modeset (DPLL0), drawing.

## 4. Where in the port order
Reference nogem ≈ port **P5** (P5a front, P5b setup_outputs, **P5c readout**, **P5d sanitize**, new **P5e** initial
plane before P6 / GEM). N0 must let ACTIVE_PIPE through only when this chain exists. Steps before readout that must
take the reference's "keep the active display" branch (checked, not assumed): P3.6 display core init (combo PHY A
verify_state skip), cdclk sanitize (must accept the GOP's CDCLK_CTL), DBUF / buddy (no reset of slice S1), DMC load
(async), P4 IRQ reset (IER / IMR only), P5a vga_disable (must be "already off"), P5b eDP prepare (PPS / VDD / backlight
read-only or panel-on aware), P5d DPLL sanitize (**hazard**), power-well sanitize (refs first).

## 5. Firmware framebuffer
Reference (iGPU): GGTT offset = stolen offset; object from stolen with I915_BO_PREALLOC, pinned at the fixed offset
before init_ggtt, released only when the first commit replaces the fb. Port: reserve GGTT [0, round_up(stride ×
aligned height)) = 300 pages (via the skl_get_initial_plane_config size rules), check PTE[0..299] point into stolen
(BDSM), keep every allocator off those pages, free only after D (pipe / plane off and the flip completed).
