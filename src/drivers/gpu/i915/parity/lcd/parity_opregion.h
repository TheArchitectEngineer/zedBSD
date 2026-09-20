/*
 * WS031 Linux-parity -- the OpRegion service instance as seen from outside the extracted text.  zedBSD project code.
 * Backend SHADOW only (driver-owned RAM in the OpRegion format); production runs VBT_ONLY and never joins the
 * firmware's runtime protocol.
 */
#ifndef PARITY_OPREGION_H
#define PARITY_OPREGION_H

#include <stdint.h>

struct parity_kworkqueue;

/* mapping table + intel_opregion_setup() on it (ASLS reads asls_token) */
int parity_opregion_shadow_map(uint64_t phys, void *ptr, uint32_t size);
int parity_opregion_shadow_setup(uint32_t asls_token);
const char *parity_opregion_mailbox_backend(void);
unsigned parity_opregion_service_epoch(void);
/* the reference lifecycle */
void parity_opregion_register(void);
void parity_opregion_unregister(void);
int parity_opregion_cleanup(void);        /* -EBUSY: still registered, or the ASLE work not shown idle */
int parity_opregion_notify_adapter(int pci_state);
/* the sanitized encoder of the readout (N1): port = enum port, output_type = enum intel_output_type */
#define PARITY_OUTPUT_ANALOG  1
#define PARITY_OUTPUT_DP      7
#define PARITY_OUTPUT_EDP     8
#define PARITY_OUTPUT_DSI     9
#define PARITY_OUTPUT_DDI     10
#define PARITY_OUTPUT_HDMI    6
#define PARITY_OUTPUT_DP_MST  11
int parity_opregion_notify_encoder(int port, int output_type, int enable);
const void *parity_opregion_vbt(uint32_t *size);
int parity_opregion_notifier_registered(void);
void parity_opregion_counters(unsigned *unported, unsigned *boundaries, unsigned *unmaps);

/* the worker queue, the backlight policy (acpi_video_get_backlight_type), the connectors */
#define PARITY_OPREGION_POLICY_VIDEO  1     /* acpi_backlight_video: BCLP requests are served */
#define PARITY_OPREGION_POLICY_VENDOR 2
#define PARITY_OPREGION_POLICY_NATIVE 3     /* acpi_backlight_native: the reference ignores ASLE backlight requests */
int parity_opregion_service_start(struct parity_kworkqueue *wq, int policy);
void parity_opregion_set_policy(int policy);
int parity_opregion_add_connector(int drm_connector_type, void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx);
int parity_opregion_add_backlight(void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx);
void parity_opregion_gse_entry(void);
int parity_opregion_asle_flush(uint64_t deadline);
void parity_opregion_worker_stats_get(unsigned *started, unsigned *finished, unsigned *queued_new, unsigned *queued_pending);

void parity_opregion_gate_counters(unsigned *dropped, unsigned *cleanup_refused);
/* E-123: the FIRMWARE backend (the real OpRegion, written) and mailbox access for the records / the synthetic firmware role */
int parity_opregion_firmware_setup(uint32_t asls);
uint32_t parity_opregion_mbox_read(unsigned off);
void parity_opregion_mbox_write(unsigned off, uint32_t v);

#endif /* PARITY_OPREGION_H */
