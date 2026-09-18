import os
def rd(p): return open(os.path.expanduser(p)).read()
def wr(p, s): open(os.path.expanduser(p), "w").write(s)

# --- gt_engine.h: carry the engine's SSEU, as engine_setup_common() does ---
p = "~/zedBSD/src/drivers/gpu/i915/parity/gt_engine.h"; s = rd(p)
old = """	int setup_done;
	int resumed;"""
new = """	/*
	 * engine->sseu = intel_sseu_from_device_info(&engine->gt->info.sseu):
	 * "use the whole device by default".  Only the two fields gen12's
	 * intel_sseu_make_rpcs() reads are kept.
	 */
	uint8_t sseu_slice_mask;
	int sseu_has_slice_pg;

	int setup_done;
	int resumed;"""
assert old in s, "engine fields"
s = s.replace(old, new, 1)
old = """int parity_engine_setup_common(struct parity_gt_engine *ge,
	struct parity_engine *info, struct parity_gt_mem *gm);"""
new = """struct parity_sseu;
int parity_engine_setup_common(struct parity_gt_engine *ge,
	struct parity_engine *info, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu);"""
assert old in s, "setup_common decl"
s = s.replace(old, new, 1)
wr(p, s); print("gt_engine.h: sseu fields")

# --- gt_engine.c ---
p = "~/zedBSD/src/drivers/gpu/i915/parity/gt_engine.c"; s = rd(p)
old = """parity_engine_setup_common(struct parity_gt_engine *ge,
	struct parity_engine *info, struct parity_gt_mem *gm)
{
	unsigned i;
	int rc;

	if (ge == 0 || info == 0 || gm == 0)
		return -EINVAL;"""
new = """parity_engine_setup_common(struct parity_gt_engine *ge,
	struct parity_engine *info, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu)
{
	unsigned i;
	int rc;

	if (ge == 0 || info == 0 || gm == 0)
		return -EINVAL;"""
assert old in s, "setup_common def"
s = s.replace(old, new, 1)
old = """	/* intel_engine_init_execlists(): one port pair, nothing in flight. */
	ge->port_mask = 1u;"""
new = """	/* intel_engine_init_execlists(): one port pair, nothing in flight. */
	ge->port_mask = 1u;

	/* "Use the whole device by default." */
	if (sseu != 0) {
		ge->sseu_slice_mask = sseu->slice_mask;
		ge->sseu_has_slice_pg = sseu->has_slice_pg;
	}"""
assert old in s, "port_mask"
s = s.replace(old, new, 1)
wr(p, s); print("gt_engine.c: sseu copy")

# --- ktest.c: the two setup_common call sites ---
p = "~/zedBSD/src/drivers/gpu/i915/parity/ktest.c"; s = rd(p)
old = """			rc = parity_engine_setup_common(&ge, &einfo, &gm);"""
new = """			esseu.slice_mask = 0x1u;
			esseu.has_slice_pg = 1;
			rc = parity_engine_setup_common(&ge, &einfo, &gm, &esseu);"""
assert old in s, "test call 1"
s = s.replace(old, new, 1)
old = """				if (parity_engine_setup_common(&ge2, &einfo2, &gm) == 0) {"""
new = """				if (parity_engine_setup_common(&ge2, &einfo2, &gm, &esseu) == 0) {"""
assert old in s, "test call 2"
s = s.replace(old, new, 1)
old = """		static struct osdep_mmio em;
		static struct fake_mmio_state ef;"""
new = """		static struct osdep_mmio em;
		static struct fake_mmio_state ef;
		static struct parity_sseu esseu;"""
assert old in s, "test locals"
s = s.replace(old, new, 1)
wr(p, s); print("ktest.c: setup_common call sites updated")
