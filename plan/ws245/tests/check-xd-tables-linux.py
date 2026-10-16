# ws245-p002: checks the data tables of src/drivers/gpu/i915/intel/display-tables-xd.h against the Linux v6.8.12 files
# they were taken from: the CD clock steps, the combo DPLL DisplayPort words, the TBT PLL words, the compensation
# references, the bandwidth buddy masks, the steppings, the PCI ids, the swing tables, the display buffer rules and
# (p004) the VBT numbering tables.
#   python3 -I plan/ws245/tests/check-xd-tables-linux.py [LINUX_TREE]   (default build/ws229-p001/linux/linux-6.8.12)
# The Linux files are only read.  Prints one line per table and FAILED with the differences when a table differs.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else 'build/ws229-p001/linux/linux-6.8.12'
i915 = root + '/drivers/gpu/drm/i915/'
header = open('src/drivers/gpu/i915/intel/display-tables-xd.h').read()
failed = []


def block(text, start):
	"""The text from a declaration's opening brace to its closing '};'."""
	i = text.index(start)
	j = text.index('\n};', i)
	return text[text.index('{', i) + 1:j]


def nums(text):
	"""Every number of a text, in order (hex or decimal)."""
	return [int(x, 0) for x in re.findall(r'\b0[xX][0-9a-fA-F]+\b|\b\d+\b', text)]


def compare(name, ours, theirs):
	"""Reports one table."""
	if ours == theirs:
		print('check_xd_tables: %s: %d values equal' % (name, len(ours)))
	else:
		failed.append(name)
		print('check_xd_tables: %s DIFFERS\n  ours   %s\n  linux  %s' % (name, ours, theirs))


# The CD clock steps: Linux rows are (refclk, cdclk, divider, ratio); ours are (ref, cdclk, ratio, divider).
cd = open(i915 + 'display/intel_cdclk.c').read()
for ours_name, linux_name in (('i915_xd_cdclk_tgl', 'icl_cdclk_table'), ('i915_xd_cdclk_rkl', 'rkl_cdclk_table'),
			      ('i915_xd_cdclk_adlp', 'adlp_cdclk_table'), ('i915_xd_cdclk_rplu', 'rplu_cdclk_table')):
	rows = re.findall(r'\.refclk = (\d+), \.cdclk = (\d+), \.divider = (\d+), \.ratio = +(\d+)', block(cd, 'struct intel_cdclk_vals %s[]' % linux_name))
	theirs = [v for r in rows for v in (int(r[0]), int(r[1]), int(r[3]), int(r[2]))]
	compare(ours_name, nums(block(header, ours_name + '[]')), theirs)
for ours_name, linux_name in (('i915_xd_voltage_tgl', 'tgl_voltage_level_max_cdclk'), ('i915_xd_voltage_rplu', 'rplu_voltage_level_max_cdclk'),
			      ('i915_xd_voltage_icl', 'icl_voltage_level_max_cdclk')):
	text = cd[cd.index(linux_name + '[]'):]
	theirs = [int(x) for x in re.findall(r'\] = (\d+)', text[:text.index('};')])]
	ours = nums(header[header.index(ours_name + '[]'):header.index(';', header.index(ours_name + '[]'))].split('=')[1])
	compare(ours_name, ours, theirs)

# The combo DPLL DisplayPort words: Linux (clock, dco_integer, dco_fraction, pdiv, kdiv, qdiv_mode, qdiv_ratio).
dp = open(i915 + 'display/intel_dpll_mgr.c').read()
for ours_name, linux_name in (('i915_xd_dpll_dp_19p2', 'icl_dp_combo_pll_19_2MHz_values'), ('i915_xd_dpll_dp_24', 'icl_dp_combo_pll_24MHz_values')):
	text = re.sub(r'/\*.*?\*/', '', block(dp, linux_name + '[]'), flags=re.S)
	rows = re.findall(r'\{ (\d+),\s*\{ \.dco_integer = (\w+), \.dco_fraction = (\w+),\s*\.pdiv = (\w+) *, \.kdiv = (\d+), \.qdiv_mode = (\d+), \.qdiv_ratio = (\d+)', text)
	theirs = [v for r in rows for v in (int(r[0]), int(r[1], 0), int(r[2], 0), int(r[6]), int(r[5]), int(r[4]), int(r[3], 0))]
	compare(ours_name, nums(block(header, ours_name + '[]')), theirs)
for ours_name, linux_name in (('i915_xd_tbt_pll_19p2', 'tgl_tbt_pll_19_2MHz_values'), ('i915_xd_tbt_pll_24', 'tgl_tbt_pll_24MHz_values')):
	m = re.search(r'%s = \{\s*\.dco_integer = (\w+), \.dco_fraction = (\w+)' % linux_name, dp)
	ours = nums(header[header.index(ours_name + ' ='):header.index(';', header.index(ours_name + ' ='))].split('=')[1])
	compare(ours_name, ours, [int(m.group(1), 0), int(m.group(2), 0)])
# p045: Ice Lake's Thunderbolt PLL words with their dividers; ours (link 0, dco_integer, dco_fraction, qdiv_ratio, qdiv_mode,
# kdiv, pdiv).
for ours_name, linux_name in (('i915_xd_tbt_pll_icl_19p2', 'icl_tbt_pll_19_2MHz_values'), ('i915_xd_tbt_pll_icl_24', 'icl_tbt_pll_24MHz_values')):
	m = re.search(r'%s = \{\s*\.dco_integer = (\w+), \.dco_fraction = (\w+),\s*\.pdiv = (\w+) /\* \d+ \*/, \.kdiv = (\d+), \.qdiv_mode = (\d+), \.qdiv_ratio = (\d+)' % linux_name, dp)
	ours = nums(header[header.index(ours_name + ' ='):header.index(';', header.index(ours_name + ' ='))].split('=')[1])
	compare(ours_name, ours, [0, int(m.group(1), 0), int(m.group(2), 0), int(m.group(6)), int(m.group(5)), int(m.group(4)), int(m.group(3), 0)])

# The compensation references: Linux (dw1, dw9, dw10) in the enum order; ours (voltage_process, dw1, dw9, dw10).
cp = open(i915 + 'display/intel_combo_phy.c').read()
theirs = [int(x, 0) for x in re.findall(r'\.dw1 = (\w+), \.dw9 = (\w+), \.dw10 = (\w+)', block(cp, 'icl_procmon_values[]')) for x in x]
ours = nums(block(header, 'i915_xd_procmon[]'))
compare('i915_xd_procmon', [v for i, v in enumerate(ours) if i % 4 != 0], theirs)

# The bandwidth buddy masks: Linux (num_channels, type, page_mask); ours (type, channels, mask).
pw = open(i915 + 'display/intel_display_power.c').read()
types = {'INTEL_DRAM_DDR4': 'DDR4', 'INTEL_DRAM_DDR5': 'DDR5', 'INTEL_DRAM_LPDDR4': 'LPDDR4', 'INTEL_DRAM_LPDDR5': 'LPDDR5'}
for ours_name, linux_name in (('i915_xd_buddy_masks', 'tgl_buddy_page_masks'), ('i915_xd_buddy_masks_wa', 'wa_1409767108_buddy_page_masks')):
	theirs = [(types[t], int(c), int(m, 0)) for c, t, m in re.findall(r'\.num_channels = (\d+), \.type = (\w+),\s+\.page_mask = (\w+)', block(pw, linux_name + '[]'))]
	ours = [(t, int(c), int(m, 0)) for t, c, m in re.findall(r'I915_XD_DRAM_(\w+), (\d+), (\w+)', block(header, ours_name + '[]'))]
	compare(ours_name, ours, theirs)

# The steppings: Linux [revid] = { ..., .display_step = STEP_x } or COMMON_STEP(x).
st = open(i915 + 'intel_step.c').read()
for ours_name, linux_name in (('icl', 'icl_revids'), ('tgl', 'tgl_revids'), ('tgl_uy', 'tgl_uy_revids'), ('rkl', 'rkl_revids'), ('adls', 'adls_revids'),
			      ('rpls', 'adls_rpls_revids'), ('adlp', 'adlp_revids'), ('adln', 'adlp_n_revids'), ('rplp', 'adlp_rplp_revids')):
	theirs = []
	for revid, rest in re.findall(r'\[(\w+)\] = \{([^}]*\})', block(st, 'intel_step_info %s[]' % linux_name)):
		m = re.search(r'display_step = STEP_(\w+)', rest) or re.search(r'COMMON_STEP\((\w+)\)', rest)
		theirs.append((int(revid, 0), m.group(1)))
	ours = [(int(r, 0), s) for r, s in re.findall(r'\{ (\w+), I915_XD_STEP_(\w+) \}', block(header, 'i915_xd_steps_%s[]' % ours_name))]
	compare('i915_xd_steps_' + ours_name, ours, theirs)

# The PCI ids.
ids = open(root + '/include/drm/i915_pciids.h').read()


def pci(macro):
	"""The ids of a Linux id macro, expanding the macros it names."""
	text = ids[ids.index('#define %s(info)' % macro):]
	text = text[:re.search(r'[^\\]\n', text).end()]
	out = [int(x, 16) for x in re.findall(r'INTEL_VGA_DEVICE\(0x(\w+)', text)]
	for inner in re.findall(r'(INTEL_\w+_IDS)\(info\)', text.split('\n', 1)[1] if '\n' in text else ''):
		out = pci(inner) + out
	return out


for ours_name, linux in (('icl', pci('INTEL_ICL_PORT_F_IDS')), ('icl_no_f', [x for x in pci('INTEL_ICL_11_IDS') if x not in pci('INTEL_ICL_PORT_F_IDS')]),
			 ('tgl', pci('INTEL_TGL_12_GT1_IDS')), ('tgl_uy', pci('INTEL_TGL_12_GT2_IDS')), ('rkl', pci('INTEL_RKL_IDS')),
			 ('adls', pci('INTEL_ADLS_IDS')), ('rpls', pci('INTEL_RPLS_IDS')), ('adlp', pci('INTEL_ADLP_IDS')),
			 ('adln', pci('INTEL_ADLN_IDS') + [0x46d3, 0x46d4]), ('rplp', [x for x in pci('INTEL_RPLP_IDS') if x not in pci('INTEL_RPLU_IDS')]),
			 ('rplu', pci('INTEL_RPLU_IDS'))):
	compare('i915_xd_ids_' + ours_name, nums(block(header, 'i915_xd_ids_%s[]' % ours_name)), linux)

# The swing tables: Linux .icl = { swing, nscalar, cursor, post2, post1 } and .dkl = { vswing, preshoot, deemph }.
bt = open(i915 + 'display/intel_ddi_buf_trans.c').read()
pairs = [('combo_swing_edp_hbr3_common', 'icl_combo_phy_trans_dp_hbr2_edp_hbr3'), ('combo_swing_edp_low_common', 'icl_combo_phy_trans_edp_hbr2'),
	 ('combo_swing_hdmi', 'icl_combo_phy_trans_hdmi'), ('combo_swing_edp_hobl', 'tgl_combo_phy_trans_edp_hbr2_hobl'),
	 ('combo_swing_dp_low_tgl', 'tgl_combo_phy_trans_dp_hbr'), ('combo_swing_dp_high_tgl', 'tgl_combo_phy_trans_dp_hbr2'),
	 ('combo_swing_dp_high_tgl_uy', 'tgl_uy_combo_phy_trans_dp_hbr2'), ('combo_swing_dp_low_rkl', 'rkl_combo_phy_trans_dp_hbr'),
	 ('combo_swing_dp_high_rkl', 'rkl_combo_phy_trans_dp_hbr2_hbr3'), ('combo_swing_dp_high_adls', 'adls_combo_phy_trans_dp_hbr2_hbr3'),
	 ('combo_swing_edp_low_adls', 'adls_combo_phy_trans_edp_hbr2'), ('combo_swing_edp_hbr3_adls', 'adls_combo_phy_trans_edp_hbr3'),
	 ('combo_swing_dp_low_adlp', 'adlp_combo_phy_trans_dp_hbr'), ('combo_swing_dp_high_adlp', 'adlp_combo_phy_trans_dp_hbr2_hbr3'),
	 ('combo_swing_edp_low_adlp', 'adlp_combo_phy_trans_edp_hbr2'), ('combo_swing_edp_hbr3_adlp', 'adlp_combo_phy_trans_dp_hbr2_edp_hbr3'),
	 ('dkl_swing_dp_low_tgl', 'tgl_dkl_phy_trans_dp_hbr'), ('dkl_swing_dp_high_tgl', 'tgl_dkl_phy_trans_dp_hbr2'),
	 ('dkl_swing_hdmi', 'tgl_dkl_phy_trans_hdmi'), ('dkl_swing_dp_low_adlp', 'adlp_dkl_phy_trans_dp_hbr'),
	 ('dkl_swing_dp_high_adlp', 'adlp_dkl_phy_trans_dp_hbr2_hbr3'),
	 ('mg_swing_dp_low_icl', 'icl_mg_phy_trans_rbr_hbr'), ('mg_swing_dp_high_icl', 'icl_mg_phy_trans_hbr2_hbr3'),
	 ('mg_swing_hdmi_icl', 'icl_mg_phy_trans_hdmi')]
for ours_name, linux_name in pairs:
	text = re.sub(r'/\*.*?\*/', '', block(bt, 'intel_ddi_buf_trans_entry _%s[]' % linux_name), flags=re.S)
	rows = [[int(x, 0) for x in r.split(',')] for r in re.findall(r'\{ \.\w+ = \{([^}]*)\}', text)]
	if ours_name.startswith('combo'):
		theirs = [v for r in rows for v in (r[0], r[1], r[2], r[4], r[3])]
	elif ours_name.startswith('mg'):
		# .mg = { override 11..6, override 5..0, override 17..12 }; ours (17..12, 11..6, 5..0).
		theirs = [v for r in rows for v in (r[2], r[0], r[1])]
	else:
		theirs = [v for r in rows for v in (r[0], r[2], r[1])]
	compare('i915_xd_' + ours_name, nums(block(header, 'i915_xd_%s_rows[]' % ours_name)), theirs)

# The display buffer rules: Linux active_pipes, dbuf_mask per pipe and join_mbus.
wm = open(i915 + 'display/skl_watermark.c').read()
for ours_name, linux_name in (('i915_xd_dbuf_tgl', 'tgl_allowed_dbufs'), ('i915_xd_dbuf_adlp', 'adlp_allowed_dbufs'),
			      ('i915_xd_dbuf_icl', 'icl_allowed_dbufs')):
	text = wm[wm.index('dbuf_slice_conf_entry %s[]' % linux_name):]
	text = text[:text.index('\n};')]
	theirs = []
	for entry in re.split(r'\n\t\},\n', text):
		if 'active_pipes' not in entry:
			continue
		pipes = sum(1 << 'ABCD'.index(p) for p in re.findall(r'PIPE_([A-D])', re.search(r'\.active_pipes = ([^,]*),', entry).group(1)))
		slices = [0, 0, 0, 0]
		for p, v in re.findall(r'\[PIPE_([A-D])\] = ([^,\n]*)', entry):
			slices['ABCD'.index(p)] = sum(1 << (int(s) - 1) for s in re.findall(r'DBUF_S(\d)', v))
		theirs += [pipes, 1 if 'join_mbus = true' in entry else 0] + slices
	compare(ours_name, nums(block(header, ours_name + '[]')), theirs)

# The VBT numbering tables (p004): the DVO port codes of each port, the DDC pin code of each GMBUS pin and the AUX code
# of each AUX channel, with the Linux enum values worked out (implicit increments and aliases) from the Linux headers.
def defines(text, prefix):
	"""The values of the #defines of a text whose names start with prefix."""
	return {n: int(v, 0) for n, v in re.findall(r'#define (%s\w*)\s+(0x[0-9a-fA-F]+|\d+)' % prefix, text)}


def enum(text, start):
	"""The values of an enum (implicit increments and aliases) from the text after start."""
	body = text[text.index(start):]
	body = re.sub(r'/\*.*?\*/', '', body[body.index('{') + 1:body.index('};')], flags=re.S)
	values = {}
	nxt = 0
	for item in [x.strip() for x in body.split(',') if x.strip()]:
		if '=' in item:
			name, value = [x.strip() for x in item.split('=')]
			nxt = values[value] if value in values else int(value, 0)
		else:
			name = item
		values[name] = nxt
		nxt += 1
	return values


vd = open(i915 + 'display/intel_vbt_defs.h').read()
dvo = defines(vd, 'DVO_PORT_')
dvo['-1'] = None
aux_codes = defines(vd, 'DP_AUX_')
gmbus_ddi = enum(vd, 'enum vbt_gmbus_ddi')
ports = enum(open(i915 + 'display/intel_display_limits.h').read(), 'enum port {')
aux_ch = enum(open(i915 + 'display/intel_display.h').read(), 'enum aux_ch {')
gmbus_pins = defines(open(i915 + 'display/intel_gmbus.h').read(), 'GMBUS_PIN_')
xl = open('src/drivers/gpu/i915/intel/xd-vbt-layout.h').read()
xd_names = defines(xl, 'I915_XD_DVO_')
xd_names.update(defines(xl, 'I915_XD_VBT_AUX_'))
bios = open(i915 + 'display/intel_bios.c').read()
for ours_name, linux_name in (('tgl', 'port_mapping'), ('rkl', 'rkl_port_mapping'), ('adls', 'adls_port_mapping'), ('adlp', 'xelpd_port_mapping')):
	text = bios[bios.index('static const int %s[][3] = {' % linux_name):]
	text = text[:text.index('};')]
	theirs = sorted((ports[p], dvo[c]) for p, codes in re.findall(r'\[(PORT_\w+)\] = \{([^}]*)\}', text)
			for c in [x.strip() for x in codes.split(',')] if dvo[c] is not None)
	ours = sorted((int(d.rstrip('U')), xd_names[c]) for d, codes in re.findall(r'\{ (\d+U), \{([^}]*)\} \}', block(header, 'i915_xd_vbt_dvo_%s[]' % ours_name))
		      for c in [x.strip() for x in codes.split(',')] if c != 'I915_XD_DVO_NONE')
	compare('i915_xd_vbt_dvo_' + ours_name, ours, theirs)
for ours_name, linux_name in (('tgl', 'icp_ddc_pin_map'), ('rkl', 'rkl_pch_tgp_ddc_pin_map'), ('adls', 'adls_ddc_pin_map'), ('adlp', 'adlp_ddc_pin_map')):
	text = block(bios, 'static const u8 %s[]' % linux_name)
	theirs = sorted((gmbus_pins[p], gmbus_ddi[c]) for p, c in re.findall(r'\[(GMBUS_PIN_\w+)\] = (\w+)', text))
	values = [int(x) for x in re.findall(r'(\d+)U', header[header.index('i915_xd_vbt_ddc_%s[] =' % ours_name):].split('};')[0].split('=', 1)[1])]
	compare('i915_xd_vbt_ddc_' + ours_name, sorted((i, v) for i, v in enumerate(values) if v != 0), theirs)
for ours_name, linux_name in (('tgl', 'direct_aux_ch_map'), ('rkl', 'rkl_aux_ch_map'), ('adls', 'adls_aux_ch_map'), ('adlp', 'adlp_aux_ch_map')):
	text = block(bios, 'static const u8 %s[]' % linux_name)
	theirs = sorted((aux_ch[a], aux_codes[c]) for a, c in re.findall(r'\[(AUX_CH_\w+)\] = (\w+)', text))
	values = [x.strip() for x in header[header.index('i915_xd_vbt_aux_%s[] =' % ours_name):].split('};')[0].split('{', 1)[1].split(',')]
	compare('i915_xd_vbt_aux_' + ours_name, sorted((i, xd_names[v]) for i, v in enumerate(values) if v != '0U'), theirs)

if failed:
	print('check_xd_tables: FAILED: ' + ', '.join(failed))
	sys.exit(1)
print('check_xd_tables: all tables equal')
