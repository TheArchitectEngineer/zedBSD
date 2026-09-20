/*
 * WS031 Linux-parity -- N1: the display the FIRMWARE left running (E-124).
 *
 * Two steps, each the reference's own text (intel_modeset_setup.c):
 *   readout   intel_modeset_setup_hw_state(): what the hardware says, and the sanitize the reference runs on it.
 *             Nothing of the firmware's picture changes.
 *   takeover  intel_crtc_disable_noatomic() for every crtc the readout found active: the firmware's picture
 *             stops and the pipe is the driver's.
 * After the takeover the caller lights the panel again through the ordinary modeset path; pointing that
 * plane at the firmware's own framebuffer keeps the console readable on a machine whose only console is
 * the screen.
 */
#ifndef PARITY_N1_H
#define PARITY_N1_H

#include <stdint.h>

struct parity_lcd_modeset_cfg;
struct parity_lcd_emit;

struct parity_n1_report {
	int live;                       /* the registry is built */
	unsigned active_pipes;          /* BIT(pipe) for every pipe the readout found active */
	int pipe;                       /* the first active pipe, or -1 */
	int cpu_transcoder;             /* of that pipe */
	int dpll_id;                    /* the shared DPLL that pipe uses, or -1 */
	int port;                       /* the port of the bound encoder */
	unsigned mode_h, mode_v, clock_khz, port_clock_khz;
	int pipe_bpp;
	unsigned output_types;
	unsigned plane_visible;         /* the primary plane of that pipe scans out */
	unsigned active_planes;
	int encoder_on_crtc;            /* the readout linked the encoder to a crtc */
	int connector_dpms;             /* 0 = on, 3 = off */
	unsigned takeovers;             /* crtcs stopped by the takeover */
	unsigned still_active;          /* BIT(pipe) of what the takeover did NOT stop (0 = all stopped) */
};

/* 0, or a negative errno.  -EBUSY: a readout is already live; -ENXIO: no screen is bound. */
int parity_n1_readout(const struct parity_lcd_modeset_cfg *cfg, struct parity_lcd_emit *ops,
	struct parity_n1_report *out);
/* 0 when every active crtc was stopped, -EBUSY when one is still active (out->still_active says which). */
int parity_n1_takeover(struct parity_n1_report *out);
void parity_n1_release(void);

#endif /* PARITY_N1_H */
