#!/usr/bin/env python3
"""WS031 E-111a: ktest checks + the real-hardware diagnostic for the cpu-transcoder / DDI words.  usage: <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)
T = lambda n: TAB * n

sh = load("plan/ws031/tests/run-lcd-host-test.sh")
sh = rep(sh, "-Wno-missing-field-initializers -fsanitize=address,undefined -fno-sanitize=alignment",
         "-Wno-missing-field-initializers -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize=shift-base")
sh = rep(sh, "cc -std=gnu11", "# shift-base is off: the reference text has `(1 << 31)` register bits (the kernel is built with wrapping semantics)" + NL + "cc -std=gnu11")
save("plan/ws031/tests/run-lcd-host-test.sh", sh)

# ---- ktest
e = load(P + "dp/edp_ktest.c")
e = rep(e, T(2) + "{" + NL + T(3) + "uint8_t one_lane[16];" + NL,
    T(2) + "{" + NL +
    T(3) + "static struct parity_lcd_words cw, dw;" + NL +
    T(3) + "uint32_t buf = 0u, func = 0u;" + NL + NL +
    T(3) + "rc = parity_lcd_emit_cpu_transcoder(&lcd, 0, 0, &cw);" + NL +
    T(3) + "check(rc == 0 && cw.n == 17u && cw.w[3].reg == 0x60044u && cw.w[11].reg == 0x60014u && cw.w[12].rmw == 1u &&" + NL +
    T(4) + "cw.w[12].reg == 0x420c0u && cw.w[12].value == 0x80000000u && cw.w[13].reg == 0x60420u && cw.w[14].reg == 0x6002cu &&" + NL +
    T(4) + "cw.w[15].rmw == 1u && cw.w[15].clear == 0x18000000u && cw.w[16].reg == 0x70008u && cw.w[16].value == 0u &&" + NL +
    T(4) + "parity_lcd_words_find(&cw, 0x6001cu, 0) == 0u," + NL +
    T(4) + '"lcd: LCD-A-CPU-TRANSCODER the reference\'s hsw_configure_cpu_transcoder orders M/N, timings, VRR, MULT, frame start delay, TRANSCONF (no enable bit)");' + NL +
    T(3) + "rc = parity_lcd_emit_ddi(&lcd, 0, 0, 0, 0u, &dw, &buf);" + NL +
    T(3) + "check(rc == 0 && dw.n == 3u && dw.w[0].reg == 0x60410u && dw.w[0].value == 1u && dw.w[1].reg == 0x60404u &&" + NL +
    T(4) + "parity_lcd_words_find(&dw, 0x60400u, &func) == 1u && func == 0x8a210002u && buf == 0x00000002u," + NL +
    T(4) + '"lcd: LCD-A-DDI-WORDS TRANS_DDI_FUNC_CTL equals Linux\'s dump (0x8a210002), DDI_BUF_CTL value + enable = 0x80000002, MSA 6 bpc");' + NL +
    T(2) + "}" + NL +
    T(2) + "{" + NL + T(3) + "uint8_t one_lane[16];" + NL)
save(P + "dp/edp_ktest.c", e)

# ---- resident device state + diagnostic
h = load(P + "dp/parity_dp_kernel.h")
h = rep(h, T(1) + "int lcd_words_rc;" + NL,
    T(1) + "int lcd_words_rc;" + NL +
    T(1) + "struct parity_lcd_words lcd_cpu_words;  /* hsw_configure_cpu_transcoder's operations (computed, not written) */" + NL +
    T(1) + "struct parity_lcd_words lcd_ddi_words;  /* TRANS_MSA_MISC, TRANS_DDI_FUNC_CTL2, TRANS_DDI_FUNC_CTL (computed, not written) */" + NL +
    T(1) + "uint32_t lcd_ddi_buf_ctl;               /* intel_dp->DP as intel_ddi_init_dp_buf_reg leaves it */" + NL +
    T(1) + "uint32_t ddi_buf_ctl_readout;           /* DDI_BUF_CTL of the eDP port when the connector was initialised */" + NL +
    T(1) + "int lcd_cpu_words_rc, lcd_ddi_words_rc;" + NL)
save(P + "dp/parity_dp_kernel.h", h)

k = load(P + "dp/parity_dp_kernel.c")
k = rep(k, T(2) + "if (dev->lcd_words_rc == 0) {" + NL,
    T(2) + "/*" + NL +
    T(2) + " * saved_port_bits as intel_ddi_init() forms it: the port's DDI_BUF_CTL readout masked with DDI_BUF_PORT_REVERSAL" + NL +
    T(2) + " * (bit 16).  A READ of the port register only; nothing is written.  eDP here = port A (0x64000)." + NL +
    T(2) + " */" + NL +
    T(2) + "dev->ddi_buf_ctl_readout = dev->env.read32(dev->env.ctx, 0x64000u);" + NL +
    T(2) + "dev->lcd_cpu_words_rc = dev->lcd_rc == 0 ? parity_lcd_emit_cpu_transcoder(&dev->lcd, 0, 0, &dev->lcd_cpu_words) : -1;" + NL +
    T(2) + "dev->lcd_ddi_words_rc = dev->lcd_rc == 0 ? parity_lcd_emit_ddi(&dev->lcd, 0, 0, 0, dev->ddi_buf_ctl_readout & 0x00010000u," + NL +
    T(3) + "&dev->lcd_ddi_words, &dev->lcd_ddi_buf_ctl) : -1;" + NL +
    T(2) + "if (dev->lcd_cpu_words_rc == 0 && dev->lcd_ddi_words_rc == 0) {" + NL +
    T(3) + "unsigned wi;" + NL + NL +
    T(3) + "for (wi = 12u; wi < dev->lcd_cpu_words.n; wi++)" + NL +
    T(4) + 'kern_logf("i915: parity LCD-A cpu-transcoder[%2u] %s 0x%05x = 0x%08x clear=0x%08x (computed; NOT written)\\n", wi,' + NL +
    T(5) + 'dev->lcd_cpu_words.w[wi].rmw ? "rmw  " : "write", dev->lcd_cpu_words.w[wi].reg, dev->lcd_cpu_words.w[wi].value,' + NL +
    T(5) + "dev->lcd_cpu_words.w[wi].clear);" + NL +
    T(3) + "for (wi = 0u; wi < dev->lcd_ddi_words.n; wi++)" + NL +
    T(4) + 'kern_logf("i915: parity LCD-A ddi[%u] write 0x%05x = 0x%08x (computed; NOT written)\\n", wi,' + NL +
    T(5) + "dev->lcd_ddi_words.w[wi].reg, dev->lcd_ddi_words.w[wi].value);" + NL +
    T(3) + 'kern_logf("i915: parity LCD-A ddi: DDI_BUF_CTL value=0x%08x (enable bit comes with link training) | DDI_BUF_CTL_A readout now=0x%08x\\n",' + NL +
    T(4) + "dev->lcd_ddi_buf_ctl, dev->ddi_buf_ctl_readout);" + NL +
    T(2) + "}" + NL +
    T(2) + "if (dev->lcd_words_rc == 0) {" + NL)
k = rep(k, T(2) + "if (dev->lcd_words_rc != 0 || dev->lcd_words.n != 13u)" + NL + T(3) + "lcd_match = 0;" + NL,
    T(2) + "if (dev->lcd_words_rc != 0 || dev->lcd_words.n != 13u)" + NL + T(3) + "lcd_match = 0;" + NL +
    T(2) + "{" + NL +
    T(3) + "/* Linux's dump: PIPE_DDI_FUNC_CTL_A 0x8a210002, DDI_BUF_CTL_A 0x80000002 (= the value + the enable bit) */" + NL +
    T(3) + "uint32_t func = 0u;" + NL + NL +
    T(3) + "if (dev->lcd_cpu_words_rc != 0 || dev->lcd_cpu_words.n != 17u || dev->lcd_ddi_words_rc != 0 ||" + NL +
    T(3) + "    parity_lcd_words_find(&dev->lcd_ddi_words, 0x60400u, &func) != 1u || func != 0x8a210002u ||" + NL +
    T(3) + "    (dev->lcd_ddi_buf_ctl | 0x80000000u) != 0x80000002u)" + NL +
    T(4) + "lcd_match = 0;" + NL +
    T(2) + "}" + NL)
save(P + "dp/parity_dp_kernel.c", k)
print("done")
