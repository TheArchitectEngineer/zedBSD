/*
 * WS031 Linux-parity — recorder over a parity_lcd_ops backend (see parity_lcd_trace.h).  zedBSD project code.
 */
#include "parity_lcd_trace.h"
#include <string.h>

static struct parity_lcd_trace_entry *add(struct parity_lcd_trace *t, int kind)
{
	struct parity_lcd_trace_entry *e;

	if (t->n >= PARITY_LCD_TRACE_MAX) {
		t->dropped++;
		return 0;
	}
	e = &t->e[t->n++];
	memset(e, 0, sizeof(*e));
	e->kind = (uint8_t)kind;
	e->n = 1u;
	return e;
}

static void t_write32(void *ctx, uint32_t reg, uint32_t value)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_WRITE);

	t->writes++;
	if (e) { e->a = reg; e->b = value; }
	t->backend->write32(t->backend->ctx, reg, value);
}

static uint32_t t_rmw32(void *ctx, uint32_t reg, uint32_t clear, uint32_t set)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_RMW);
	uint32_t old = t->backend->rmw32(t->backend->ctx, reg, clear, set);

	t->rmws++;
	if (e) { e->a = reg; e->b = set; e->c = clear; e->d = old; }
	return old;
}

static void t_posting_read(void *ctx, uint32_t reg)
{
	struct parity_lcd_trace *t = ctx;

	if (t->backend->posting_read != 0)
		t->backend->posting_read(t->backend->ctx, reg);
}

static uint32_t t_read32(void *ctx, uint32_t reg)
{
	struct parity_lcd_trace *t = ctx;
	uint32_t v = t->backend->read32(t->backend->ctx, reg);
	struct parity_lcd_trace_entry *e;

	/* polling loops: one entry per run of identical reads */
	if (t->n != 0u && t->e[t->n - 1u].kind == PARITY_LCD_T_READ && t->e[t->n - 1u].a == reg && t->e[t->n - 1u].b == v &&
	    t->e[t->n - 1u].n < 0xffffu) {
		t->e[t->n - 1u].n++;
		return v;
	}
	e = add(t, PARITY_LCD_T_READ);
	if (e) { e->a = reg; e->b = v; }
	return v;
}

static int t_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value, unsigned timeout_ms)
{
	struct parity_lcd_trace *t = ctx;
	int rc = t->backend->wait_reg(t->backend->ctx, reg, mask, value, timeout_ms);
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_WAIT);

	t->waits++;
	if (rc != 0)
		t->wait_timeouts++;
	if (e) { e->a = reg; e->b = value; e->c = mask; e->d = timeout_ms; e->rc = rc; }
	return rc;
}

static void t_usleep(void *ctx, unsigned us)
{
	struct parity_lcd_trace *t = ctx;

	t->sleeps++;
	t->slept_us += us;
	t->backend->usleep(t->backend->ctx, us);
}

static void t_udelay(void *ctx, unsigned us)
{
	struct parity_lcd_trace *t = ctx;

	t->slept_us += us;
	t->backend->udelay(t->backend->ctx, us);
}

static uint32_t first_bytes(const uint8_t *buf, size_t size)
{
	uint32_t v = 0u;
	size_t i;

	for (i = 0; i < size && i < 4u; i++)
		v |= (uint32_t)buf[i] << (8u * i);
	return v;
}

static long t_dpcd_read(void *ctx, unsigned offset, uint8_t *buf, size_t size)
{
	struct parity_lcd_trace *t = ctx;
	long rc = t->backend->dpcd_read(t->backend->ctx, offset, buf, size);
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_DPCD_READ);

	if (e) { e->a = offset; e->b = rc > 0 ? first_bytes(buf, (size_t)rc) : 0u; e->c = (uint32_t)size; e->rc = (int32_t)rc; }
	return rc;
}

static long t_dpcd_write(void *ctx, unsigned offset, const uint8_t *buf, size_t size)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_DPCD_WRITE);
	long rc = t->backend->dpcd_write(t->backend->ctx, offset, buf, size);

	if (e) { e->a = offset; e->b = first_bytes(buf, size); e->c = (uint32_t)size; e->rc = (int32_t)rc; }
	return rc;
}

static int t_read_dpcd_caps(void *ctx, uint8_t dpcd[15])
{
	struct parity_lcd_trace *t = ctx;
	int rc = t->backend->read_dpcd_caps(t->backend->ctx, dpcd);
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_DPCD_READ);

	if (e) { e->a = 0u; e->b = rc == 0 ? first_bytes(dpcd, 4u) : 0u; e->c = 15u; e->rc = rc; e->name = "drm_dp_read_dpcd_caps"; }
	return rc;
}

static int t_panel(void *ctx, int op)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_PANEL);
	int rc = t->backend->panel(t->backend->ctx, op);

	if (e) { e->a = (uint32_t)op; e->rc = rc; }
	return rc;
}

static int t_power_get(void *ctx, int domain)
{
	struct parity_lcd_trace *t = ctx;
	int rc = t->backend->power_get(t->backend->ctx, domain);
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_POWER_GET);

	if (e) { e->a = (uint32_t)domain; e->rc = rc; }
	return rc;
}

static void t_power_put(void *ctx, int domain, int wakeref)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_POWER_PUT);

	if (e) { e->a = (uint32_t)domain; e->b = (uint32_t)wakeref; }
	t->backend->power_put(t->backend->ctx, domain, wakeref);
}

static void t_lock(void *ctx, int which, int take)
{
	struct parity_lcd_trace *t = ctx;

	t->backend->lock(t->backend->ctx, which, take);
}

static void named(struct parity_lcd_trace *t, int kind, const char *name)
{
	struct parity_lcd_trace_entry *e = add(t, kind);

	if (e)
		e->name = name;
}

static void t_step(void *ctx, const char *name)
{
	struct parity_lcd_trace *t = ctx;

	if (strncmp(name, "(decided) ", 10) == 0) {
		t->decided++;
		named(t, PARITY_LCD_T_DECIDED, name);
	} else {
		t->steps++;
		named(t, PARITY_LCD_T_STEP, name);
	}
	if (t->backend->step != 0)
		t->backend->step(t->backend->ctx, name);
}

static void t_error(void *ctx, const char *what)
{
	struct parity_lcd_trace *t = ctx;

	if (t->errors++ == 0u)
		t->first_error_at = (int)t->n;
	named(t, PARITY_LCD_T_ERROR, what);
	if (t->backend->error != 0)
		t->backend->error(t->backend->ctx, what);
}

static void t_debug(void *ctx, const char *what)
{
	struct parity_lcd_trace *t = ctx;

	if (t->backend->debug != 0)
		t->backend->debug(t->backend->ctx, what);
}

void parity_lcd_trace_init(struct parity_lcd_trace *t, struct parity_lcd_emit *backend)
{
	memset(t, 0, sizeof(*t));
	t->backend = backend;
	t->first_error_at = -1;
	t->ops.ctx = t;
	t->ops.write32 = t_write32;
	t->ops.rmw32 = t_rmw32;
	t->ops.posting_read = t_posting_read;
	t->ops.step = t_step;
	t->ops.read32 = t_read32;
	t->ops.wait_reg = t_wait_reg;
	t->ops.usleep = t_usleep;
	t->ops.udelay = t_udelay;
	t->ops.dpcd_read = t_dpcd_read;
	t->ops.dpcd_write = t_dpcd_write;
	t->ops.read_dpcd_caps = t_read_dpcd_caps;
	t->ops.panel = t_panel;
	t->ops.power_get = t_power_get;
	t->ops.power_put = t_power_put;
	t->ops.lock = t_lock;
	t->ops.error = t_error;
	t->ops.debug = t_debug;
}

void parity_lcd_trace_phase(struct parity_lcd_trace *t, const char *name)
{
	named(t, PARITY_LCD_T_PHASE, name);
}

int parity_lcd_trace_find(const struct parity_lcd_trace *t, int kind, uint32_t a, const char *name, unsigned from)
{
	unsigned i;

	for (i = from; i < t->n; i++) {
		if (t->e[i].kind != kind)
			continue;
		if (kind == PARITY_LCD_T_STEP || kind == PARITY_LCD_T_ERROR || kind == PARITY_LCD_T_PHASE || kind == PARITY_LCD_T_DECIDED) {
			if (name != 0 && t->e[i].name != 0 && strstr(t->e[i].name, name) != 0)
				return (int)i;
		} else if (t->e[i].a == a) {
			return (int)i;
		}
	}
	return -1;
}
