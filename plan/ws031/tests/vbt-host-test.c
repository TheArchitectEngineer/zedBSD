/*
 * WS031 host test: the generated VBT parser (the same intel_bios_port.c the kernel builds)
 * on the captured target VBT + EDID, checked against values from an INDEPENDENT decoder
 * (igt intel_vbt_decode: plan/ws031/display-ref/vbt-decode.txt), plus malformed inputs.
 *
 *   cc -DPARITY_VBT_HOST -I<vbt dir> -o vbt_host_test vbt_host_test.c <vbt dir>/intel_bios_port.c
 *   ./vbt_host_test i915_vbt.bin edid-eDP-1.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parity_vbt.h"

void parity_vbt_emit(const char *text) { fputs(text, stdout); }
int parity_vbt_fmtcheck(const char *fmt, ...) { (void)fmt; return 0; }

static unsigned checks, failures;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("FAIL: %s\n", msg); } } while (0)

static unsigned char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;

	if (!f) { perror(path); exit(2); }
	fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	b = malloc(*n + 16);
	if (fread(b, 1, *n, f) != *n) exit(2);
	fclose(f);
	return b;
}

int main(int argc, char **argv)
{
	static struct parity_vbt v;
	struct parity_vbt_panel p;
	const struct parity_vbt_encoder *e;
	unsigned char *vbt, *edid, *bad;
	size_t vn, en;
	unsigned i;
	int rc;

	if (argc < 3) { fprintf(stderr, "usage: %s vbt.bin edid.bin [-v]\n", argv[0]); return 2; }
	vbt = slurp(argv[1], &vn);
	edid = slurp(argv[2], &en);
	parity_vbt_set_log_level(argc > 3 ? 2 : 0);

	/* ---- 1. the real VBT ---- */
	CHECK(parity_vbt_validate(vbt, vn) == 1, "the captured VBT validates");
	rc = parity_vbt_init(&v, vbt, vn, PARITY_VBT_ORIGIN_EXPLICIT_BLOB);
	CHECK(rc == 0 && v.inited && v.origin == PARITY_VBT_ORIGIN_EXPLICIT_BLOB && !v.missing_defaults_used,
	      "init from the explicit blob; missing-defaults NOT used");
	printf("signature=\"%s\" bdb_version=%u blocks=%u encoders=%u arena_peak=%u alloc_fail=%u log_errors=%u\n",
	       v.signature, v.bdb_version, v.num_bdb_blocks, v.n_encoders, v.arena_peak, v.alloc_failures, v.log_errors);
	/* igt: signature "$VBT ALDERLAKE-P", BDB version 249 */
	CHECK(strncmp(v.signature, "$VBT ALDERLAKE-P", 16) == 0 && v.bdb_version == 249, "signature / BDB version 249 (igt)");
	CHECK(v.alloc_failures == 0 && v.log_errors == 0, "no allocation failure, no error-level message");
	for (i = 0; i < v.n_encoders; i++) {
		e = &v.enc[i];
		printf("  enc[%u] port=%c aux=%d type=0x%04x dvo=%u ddc_raw=%u ddc_pin=%d dp=%d edp=%d hdmi=%d dvi=%d "
		       "typec=%d tbt=%d lanes=%d rate=%d hpd_inv=%d lane_rev=%d\n", i,
		       e->port >= 0 ? 'A' + e->port : '-', e->aux_ch, e->device_type, e->dvo_port, e->ddc_pin_raw,
		       e->ddc_pin, e->supports_dp, e->supports_edp, e->supports_hdmi, e->supports_dvi,
		       e->supports_typec_usb, e->supports_tbt, e->dp_max_lane_count, e->dp_max_link_rate,
		       e->hpd_invert, e->lane_reversal);
	}
	/* igt child 1: handle LFP1(eDP), type 0x1806, DVO port DP-A (0x0a), AUX-A (0x40), internal connector */
	e = parity_vbt_encoder_for_port(&v, 0);
	CHECK(e != 0 && e->device_type == 0x1806 && e->dvo_port == 10 && e->aux_ch == 0 &&
	      e->supports_edp == 1 && e->supports_dp == 1 && e->supports_hdmi == 0,
	      "port A = the eDP child: type 0x1806, DVO DP-A, AUX A, eDP yes, HDMI no (igt)");
	/* igt child 2: type 0x60d2 (HDMI/DVI-D) on port B */
	e = parity_vbt_encoder_for_port(&v, 1);
	CHECK(e != 0 && e->device_type == 0x60d2 && e->supports_hdmi == 1 && e->supports_dp == 0,
	      "port B = HDMI child type 0x60d2 (igt)");

	/* ---- 2. panel data with the real EDID ---- */
	rc = parity_vbt_init_panel(&v, 0, edid, &p);
	printf("panel rc=%d type=%d bpp=%d rate=0x%02x lanes=%d pps t1_t3=%u t8=%u t9=%u t10=%u t11_t12=%u | "
	       "bl present=%d type=%d ctrl=%d active_low=%d freq=%u min=%u | lfp_mode=%d %ux%u clk=%d drrs=%d vrr=%d\n",
	       rc, p.panel_type, p.bpp, p.edp_rate, p.edp_lanes, p.t1_t3, p.t8, p.t9, p.t10, p.t11_t12,
	       p.bl_present, p.bl_type, p.bl_controller, p.bl_active_low_pwm, p.bl_pwm_freq_hz, p.bl_min_brightness,
	       p.has_lfp_mode, p.hdisplay, p.vdisplay, p.mode_clock_khz, p.drrs_type, p.vrr);
	/* igt: panel type 2; eDP block panel 2: T3 2000 T7 800 T9 2000 T10 1100 T12 5000, 18 bpp;
	 * backlight block panel 2: PWM (type 2), active low 0, 200 Hz, min 6, controller 0 */
	CHECK(rc == 0 && p.panel_type == 2, "panel type 2 (igt: LVDS options block)");
	CHECK(p.t1_t3 == 2000 && p.t8 == 800 && p.t9 == 2000 && p.t10 == 1100 && p.t11_t12 == 5000,
	      "eDP power sequence T3 2000 / T7(t8) 800 / T9 2000 / T10 1100 / T12 5000 x100us (igt)");
	CHECK(p.bpp == 18, "eDP colour depth 18 bpp (igt)");
	/* BDB >= 234: the reference takes brightness_min_level[] (igt "Brightness min level: 15"),
	 * not the older per-entry "Minimum brightness: 6". */
	CHECK(p.bl_present == 1 && p.bl_active_low_pwm == 0 && p.bl_pwm_freq_hz == 200 &&
	      p.bl_min_brightness == 15 && p.bl_controller == 0,
	      "backlight: PWM present, active high, 200 Hz, min level 15, controller 0 (igt)");
	/* igt child 3/4: type 0x68c6 DisplayPort on the Type-C ports */
	e = parity_vbt_encoder_for_port(&v, 3);
	CHECK(e != 0 && e->device_type == 0x68c6 && e->supports_typec_usb == 1 && parity_vbt_encoder_for_port(&v, 2) == 0,
	      "TC1 = DP Type-C child 0x68c6; no child on port C (igt)");
	parity_vbt_fini(&v);

	/* ---- 3. no VBT: the reference's missing defaults, clearly distinguished ---- */
	rc = parity_vbt_init(&v, 0, 0, PARITY_VBT_ORIGIN_NONE);
	CHECK(rc == 0 && v.missing_defaults_used == 1 && v.origin == PARITY_VBT_ORIGIN_NONE &&
	      v.bdb_version == 155 && v.n_encoders == 3,
	      "no VBT -> init_vbt_missing_defaults(): version 155, ports A/B/C (ADL-P)");
	parity_vbt_fini(&v);

	/* ---- 4. malformed inputs are refused by validation ---- */
	CHECK(parity_vbt_validate(vbt, 47) == 0, "truncated below the header is invalid");
	CHECK(parity_vbt_validate(vbt, vn / 2) == 0, "truncated in the middle is invalid (vbt_size > size)");
	bad = malloc(vn); memcpy(bad, vbt, vn); bad[0] = 'X';
	CHECK(parity_vbt_validate(bad, vn) == 0, "bad signature is invalid");
	memcpy(bad, vbt, vn); bad[0x1c] = 0xff; bad[0x1d] = 0xff; bad[0x1e] = 0xff; bad[0x1f] = 0x7f;
	CHECK(parity_vbt_validate(bad, vn) == 0, "BDB offset out of range is invalid");
	/* a block whose size runs past the BDB must not be followed */
	memcpy(bad, vbt, vn);
	{
		unsigned bdb = 0x30, hdr = bad[bdb + 0x12] | (bad[bdb + 0x13] << 8);

		bad[bdb + hdr + 1] = 0xff; bad[bdb + hdr + 2] = 0xff;      /* first block: size 0xffff */
		rc = parity_vbt_init(&v, bad, vn, PARITY_VBT_ORIGIN_EXPLICIT_BLOB);
		printf("oversized-first-block: rc=%d encoders=%u blocks=%u alloc_fail=%u\n", rc, v.n_encoders,
		       v.num_bdb_blocks, v.alloc_failures);
		CHECK(rc == 0 && v.alloc_failures == 0, "an oversized block does not overrun or exhaust the arena");
		parity_vbt_fini(&v);
	}
	/* second init while live is refused */
	rc = parity_vbt_init(&v, vbt, vn, PARITY_VBT_ORIGIN_EXPLICIT_BLOB);
	{
		static struct parity_vbt w;
		CHECK(rc == 0 && parity_vbt_init(&w, vbt, vn, PARITY_VBT_ORIGIN_EXPLICIT_BLOB) == -16,
		      "a second live VBT state is refused (-EBUSY)");
	}
	parity_vbt_fini(&v);
	rc = parity_vbt_init(&v, vbt, vn, PARITY_VBT_ORIGIN_EXPLICIT_BLOB);
	CHECK(rc == 0 && v.n_encoders >= 2, "init after fini works again (arena released)");
	parity_vbt_fini(&v);

	printf("vbt_host_test: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
