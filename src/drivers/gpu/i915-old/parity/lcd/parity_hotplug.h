/*
 * WS031 Linux-parity -- the HDMI hotplug receive path (E-123), the zedBSD side of the generated reference text.
 * zedBSD project code.  No Linux types here: probe.c, irq.c and the tests use this interface.
 *
 *   start   intel_hpd_init_early() + the connectors intel_setup_outputs() would have made (one per encoder: eDP-1,
 *           HDMI-A-1, DP-1, DP-2), each encoder's ->hotplug = intel_ddi_hotplug and ->connected =
 *           lpt_digital_port_connected / TC (the latter recorded as unported), then the IRQ entry is opened
 *   irq     from gen8_de_irq_handler(): the acked SDEIIR value -> icp_irq_handler() (IRQ context)
 *   stop    the IRQ entry is closed, in-flight entries drained, then intel_hpd_cancel_work()
 */
#ifndef PARITY_HOTPLUG_H
#define PARITY_HOTPLUG_H

#include <stdint.h>

struct osdep_mmio;
struct parity_hotplug;
struct parity_display_nogem;
struct parity_power_domains;
struct parity_pw_ctx;

#define PARITY_HPD_MAX_CONNECTORS 8u
#define PARITY_HPD_MAX_IRQ_RECORDS 64u
#define PARITY_HPD_MAX_HOTPLUG_RECORDS 64u

/* register access of the model tests (NULL = the MMIO BAR) */
struct parity_hpd_fake_regs {
	uint32_t sdeisr, shotplug_ddi, shotplug_tc;
	unsigned rmw_writes;
	uint32_t last_rmw_write;
	/* the DDC bus behind GMBUS: a sink answering at 0x50 with `ddc_edid`, or nobody (NAK) */
	int ddc_present;
	const uint8_t *ddc_edid;
	unsigned ddc_edid_len;
	uint32_t gmbus[6];
	unsigned gm_ptr, gm_left, gm_reads, gm_naks;
	int gm_nak, gm_active;
};

/* the last EDID read over DDC (drm_edid_read_ddc) */
struct parity_hpd_edid_info {
	int rc;                         /* blocks read (>= 1) or a negative errno */
	unsigned blocks, extensions, product, serial, version, revision, digital, checksum;
	unsigned pixel_clock_khz, hactive, vactive;     /* the first detailed timing descriptor */
	unsigned reads, fails;
	char mfg[4];
};

struct parity_hpd_irq_record {
	uint64_t tick;
	uint32_t sde_iir;               /* the acked SDEIIR value handed to icp_irq_handler() */
	uint32_t shotplug_ddi;          /* what its rmw of SHOTPLUG_CTL_DDI read (0 when the DDI trigger was clear) */
	uint32_t event_bits_after;      /* display.hotplug.event_bits when the handler returned */
};

/* one encoder->hotplug() call of the hotplug work (the reference's "Connector %s (pin %i) received hotplug event") */
struct parity_hpd_hotplug_record {
	uint64_t tick;
	unsigned connector;
	int pin, retries;
	int old_status, new_status;     /* enum drm_connector_status (1 connected, 2 disconnected, 3 unknown) */
	int state;                      /* enum intel_hotplug_state (0 UNCHANGED, 1 CHANGED, 2 RETRY) */
	int live;                       /* SDEISR & pch_hpd[pin] at the detection */
	int edid_rc;                    /* the EDID read of this detection: blocks read, or a negative errno */
	unsigned edid_blocks;
};

struct parity_hpd_summary {
	int started, live;
	unsigned num_connectors;
	unsigned irq_entries, irq_dropped, ddi_triggers, gmbus_irqs;
	unsigned hotplug_works, digport_works, reenable_works, retries_armed;
	unsigned hpd_pulse_steps, hotplug_events, irq_setups, storms, warnings;
	unsigned irq_records, hotplug_records;
	unsigned to_connected, to_disconnected;     /* HDMI-A connector status transitions */
	int hdmi_connector;                         /* index, -1 = none */
	int hdmi_status;                            /* enum drm_connector_status, now */
	unsigned long long hdmi_epoch;
};

int  parity_hpd_start(struct parity_hotplug *hp, struct osdep_mmio *m, const struct parity_display_nogem *nogem,
	struct parity_power_domains *pd, struct parity_pw_ctx *c, int pch_type, int intel_irqs_enabled,
	struct parity_hpd_fake_regs *fake);
void parity_hpd_pch_irq(uint32_t sde_iir);      /* IRQ context; ignored unless started */
void parity_hpd_model_irq(uint32_t sde_iir);    /* the model tests' interrupt (interrupts disabled around it) */
/* 0 once a real instance has run in this boot: the model tests must not take over the device's hotplug state */
int  parity_hpd_model_allowed(void);
void parity_hpd_stop(void);
/* the initial detection (what the first probe of the connector does), under mode_config.mutex; returns the status */
int  parity_hpd_probe_connector(unsigned idx);
const char *parity_hpd_connector_name(unsigned idx);
void parity_hpd_summary(struct parity_hpd_summary *s);
const struct parity_hpd_irq_record *parity_hpd_irq_record(unsigned i);
const struct parity_hpd_hotplug_record *parity_hpd_hotplug_record(unsigned i);
/* model-test views of the reference state */
uint32_t parity_hpd_event_bits(void);
uint32_t parity_hpd_retry_bits(void);
int  parity_hpd_pin_state(int pin);             /* display.hotplug.stats[pin].state */
int  parity_hpd_pin_count(int pin);
int  parity_hpd_connector_polled(unsigned idx);
int  parity_hpd_connector_status(unsigned idx);
int  parity_hpd_flush_reenable(void);
void parity_hpd_edid_info(struct parity_hpd_edid_info *out);
const uint8_t *parity_hpd_edid_bytes(unsigned idx, unsigned *size);
void parity_hpd_edid_forget(void);
void parity_hpd_gmbus_forget(void);             /* 1 = the armed re-enable work was run now */

/* the HPD-TEST window (-DPARITY_HDMI_HPD_TEST=1): probe.c after intel_display_driver_probe */
int  parity_hpd_test_run(struct osdep_mmio *m, unsigned window_s);
/* HDMI-EDID (-DPARITY_HDMI_EDID_TEST=1): the first detection of the (connected) HDMI sink, its EDID over GMBUS */
int  parity_hdmi_edid_test_run(struct osdep_mmio *m);
/* the model tests (GPU-free) */
typedef void (*parity_hpd_ktest_check)(int ok, const char *msg);
void parity_hpd_ktest(parity_hpd_ktest_check check);

#endif /* PARITY_HOTPLUG_H */
