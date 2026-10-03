# HDMI hotplug (DDI B) — port plan (E-123, code reading only)

Reference: Linux v6.8.12 i915 (`display/intel_hotplug_irq.c`, `intel_hotplug.c`, `intel_hdmi.c`, `intel_ddi.c`, `intel_gmbus.c`,
`intel_dp.c`, `i915_reg.h`). Port: `src/drivers/gpu/i915/parity/`.

Target: Latitude 5330, ADL-P, PCH ADP (ICP-class). VBT child for port B: type 0x60d2 "DVI-D", HDMI output, no DP bit,
DDC pin 2 → init_hdmi=1, init_dp=0 → **no hpd_pulse**: port B's HPD goes event_bits → hotplug_work → encoder->hotplug.
Linux register dump: SDEISR = 0x00010000 (DDI A live, DDI B idle).

## Reference chain (ADL-P)
1. gen8 DE: `master_ctl & GEN8_DE_PCH_IRQ` → read + ack SDEIIR → `icp_irq_handler(iir)` (intel_hotplug_irq.c:551)
2. icp_irq_handler: `ddi_trigger = iir & SDE_DDI_HOTPLUG_MASK_ICP`; under irq_lock `intel_uncore_rmw(SHOTPLUG_CTL_DDI, 0, 0)`
   (write-back clears sticky status); `intel_get_hpd_pins(... pch_hpd, icp_ddi_port_hotplug_long_detect)`; `intel_hpd_irq_handler`;
   `SDE_GMBUS_ICP` → gmbus wake
3. intel_get_hpd_pins (:341): pin_mask / long_mask; icp long detect (:242): `val & SHOTPLUG_CTL_DDI_HPD_LONG_DETECT(pin)`
4. intel_hpd_irq_handler (intel_hotplug.c:495, under irq_lock): encoders without hpd_pulse skipped; HPD_ENABLED pins →
   event_bits; storm detect (+10 long / +1 short, threshold 50, 1000 ms) → HPD_MARK_DISABLED + intel_hpd_irq_setup;
   then `queue_delayed_work(unordered_wq, hotplug_work, 0)`
5. i915_hotplug_work_func (:378): mode_config.mutex; swap event_bits / retry_bits under irq_lock; per connector on the pin:
   "Connector %s (pin %i) received hotplug event. (retry %d)" → encoder->hotplug; CHANGED → uevent (N/A); RETRY →
   re-queue at HPD_RETRY_DELAY 1000 ms
6. intel_ddi_hotplug (intel_ddi.c:4538): intel_encoder_hotplug → intel_hotplug_detect_connector (drm_helper_probe_detect,
   epoch_counter compare, "status updated from %s to %s"); intel_hdmi_reset_link (0 without crtc); UNCHANGED and
   hotplug_retries < 1 (non-TC) → RETRY (the second detect pass: DDC outlives HPD on unplug)
7. intel_hdmi_detect (intel_hdmi.c:2491): POWER_DOMAIN_GMBUS; display ≥ 11: `!intel_digital_port_connected` → disconnected;
   else unset_edid / set_edid. dig_port->connected for a combo PHY = lpt_digital_port_connected: `SDEISR & pch_hpd[pin]`
8. intel_hdmi_set_edid (:2451): drm_edid_read_ddc → i2c_transfer → gmbus_xfer (intel_gmbus.c:754) → do_gmbus_xfer (:623):
   GMBUS0 = pin | rate, index xfer, read chunks, gmbus_wait (GMBUS2 poll); failure → bit-bang force_bit (GPIO 2)
9. DDC pin: VBT pin 2 → adlp_ddc_pin_map → ICL_DDC_BUS_DDI_B = GMBUS pin 2

## Port status
Have: HPD pin tables + gen11 / icp hpd_irq_setup (driver_probe.c), SDEIIR ack (irq.c), encoders (init_hdmi / init_dp),
GMBUS pin table without adapters, drm_do_probe_ddc_edid + EDID reader (dp/), IRQ-safe kworkqueue, delayed work (backend_delayed).
Missing: SDE decode, hotplug state (event_bits, retry_bits, storm stats, works, irq_lock), connector objects (status,
epoch_counter, hotplug_retries, ddc, detect_edid), HDMI connector init, dig_port->connected, GMBUS transfer, i2c retries.
drm_probe_helper.c (drm_helper_probe_detect) is not in the saved reference set: fetch it first.

## Slices
(a) IRQ decode + work + live-status detect (set_edid recorded as a step; "connected" from live status, an adaptation).
(b) EDID over GMBUS: port intel_gmbus.c:367-776 through the generator with a compat i2c_adapter on pin 2.

## Registers
HPD pin B = 5 (HPD_PORT_A = 4) → mask 0x20. SDEISR 0xc4000, SDEIMR 0xc4004, SDEIIR 0xc4008, SDEIER 0xc400c.
SDE_DDI_HOTPLUG_ICP(B) = bit 17 (0x20000); SDE_GMBUS_ICP bit 23. SHOTPLUG_CTL_DDI 0xc4030, nibble per pin:
ENABLE 0x8, OUTPUT_DATA 0x4, STATUS long 0x2 / short 0x1 → for B: enable 0x80, long 0x20, short 0x10.
GMBUS0 0xc5100 (pin | rate), GMBUS1 0xc5104, GMBUS2 0xc5108, GMBUS3 0xc510c, GMBUS4 0xc5110, GMBUS5 0xc5120; GPIO(2) 0xc5018.

## Tests
Model: decode (iir, SHOTPLUG) pairs; storm (6 long pulses / 1000 ms → disabled, SDEIMR re-masks bit 17); work state
machine with a fake detect (UNCHANGED → RETRY once); GMBUS fake replaying an EDID fixture.
Real (VM, iGPU passthrough, -DPARITY_HDMI_HPD_TEST=1): log SDEIMR / SDEIER / SHOTPLUG baseline; a ~60 s window; the user
plugs / unplugs an HDMI monitor; log per IRQ (SDEIIR, dig, pins, long), work, live status, (b) EDID header / checksum /
manufacturer, "status updated from … to …"; pass = plug → connected (EDID valid), unplug → disconnected, no storm.
