/*
 * WS031 Linux-parity — a recorder stacked on any parity_lcd_ops backend (the register / sink model or the
 * real GPU): every operation is forwarded, and logged in order with what the backend answered.  It is
 * the run log of a modeset ("which phase, which write, what the wait returned, the first anomaly") and
 * the source of the sequence tables; it never replays anything.  zedBSD project code; plain C.
 */
#ifndef PARITY_LCD_TRACE_H
#define PARITY_LCD_TRACE_H

#include <stdint.h>
#include "parity_lcd_ops.h"

enum parity_lcd_trace_kind {
	PARITY_LCD_T_WRITE = 1,     /* a = reg, b = value */
	PARITY_LCD_T_RMW,           /* a = reg, b = set, c = clear, d = old value */
	PARITY_LCD_T_READ,          /* a = reg, b = value, n = how many identical reads in a row (polling) */
	PARITY_LCD_T_WAIT,          /* a = reg, b = value wanted, c = mask, rc */
	PARITY_LCD_T_DPCD_WRITE,    /* a = offset, b = first bytes (little end first), c = size, rc */
	PARITY_LCD_T_DPCD_READ,     /* a = offset, b = first bytes, c = size, rc */
	PARITY_LCD_T_PANEL,         /* a = op, rc */
	PARITY_LCD_T_POWER_GET,     /* a = domain, rc = wakeref */
	PARITY_LCD_T_POWER_PUT,     /* a = domain, b = wakeref; asynchronous: d = 1, c = delay in ms */
	PARITY_LCD_T_STEP,          /* name: a reference callee that is not ported */
	PARITY_LCD_T_ERROR,         /* name: a drm_err / WARN of the reference text */
	PARITY_LCD_T_PHASE,         /* name: a marker written by the caller */
	PARITY_LCD_T_DECIDED,       /* name: a reference callee deliberately not connected (reason in lcd_seq_compat.h) */
	PARITY_LCD_T_DBUF,          /* a = the DBUF slices requested (gen9_dbuf_slices_update) */
	PARITY_LCD_T_OBSERVE,       /* a = enum parity_lcd_observe: a point of the commit */
};

struct parity_lcd_trace_entry {
	uint8_t kind;
	uint16_t n;
	int32_t rc;
	uint32_t a, b, c, d;
	const char *name;
};

#define PARITY_LCD_TRACE_MAX 2048u
struct parity_lcd_trace {
	struct parity_lcd_emit *backend;
	struct parity_lcd_emit ops;             /* hand THIS to the modeset */
	unsigned n, dropped;
	unsigned writes, rmws, waits, wait_timeouts, steps, decided, errors, sleeps;
	uint64_t slept_us;
	int first_error_at;                     /* index of the first ERROR entry, or -1 */
	void (*tap)(void *ctx, int point);      /* optional: called at every observe() point, before the backend */
	void *tap_ctx;
	struct parity_lcd_trace_entry e[PARITY_LCD_TRACE_MAX];
};

void parity_lcd_trace_init(struct parity_lcd_trace *t, struct parity_lcd_emit *backend);
void parity_lcd_trace_phase(struct parity_lcd_trace *t, const char *name);
/* index of the first entry of `kind` whose a == `a` (kind STEP / ERROR / PHASE: whose name contains `name`) at or after `from`; -1 */
int parity_lcd_trace_find(const struct parity_lcd_trace *t, int kind, uint32_t a, const char *name, unsigned from);

#endif /* PARITY_LCD_TRACE_H */
