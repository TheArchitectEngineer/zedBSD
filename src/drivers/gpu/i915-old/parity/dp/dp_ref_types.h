/*
 * zedBSD WS031: `struct intel_pps` extracted textually from the Linux v6.8.12 i915 reference
 * display/intel_display_types.h (MIT permission notice, Copyright Intel Corporation; the full
 * notice is kept in intel_pps_port.c's source header) by tools/port_dp_aux_pps.py.
 * Do not edit by hand.
 */
#ifndef PARITY_DP_REF_TYPES_H
#define PARITY_DP_REF_TYPES_H

/* from intel_display_types.h */
struct intel_pps {
	int panel_power_up_delay;
	int panel_power_down_delay;
	int panel_power_cycle_delay;
	int backlight_on_delay;
	int backlight_off_delay;
	struct delayed_work panel_vdd_work;
	bool want_panel_vdd;
	bool initializing;
	unsigned long last_power_on;
	unsigned long last_backlight_off;
	ktime_t panel_power_off_time;
	intel_wakeref_t vdd_wakeref;

	union {
		/*
		 * Pipe whose power sequencer is currently locked into
		 * this port. Only relevant on VLV/CHV.
		 */
		enum pipe pps_pipe;

		/*
		 * Power sequencer index. Only relevant on BXT+.
		 */
		int pps_idx;
	};

	/*
	 * Pipe currently driving the port. Used for preventing
	 * the use of the PPS for any pipe currentrly driving
	 * external DP as that will mess things up on VLV.
	 */
	enum pipe active_pipe;
	/*
	 * Set if the sequencer may be reset due to a power transition,
	 * requiring a reinitialization. Only relevant on BXT+.
	 */
	bool pps_reset;
	struct edp_power_seq pps_delays;
	struct edp_power_seq bios_pps_delays;
};

#endif /* PARITY_DP_REF_TYPES_H */
