#!/usr/bin/env python3
"""E-124 fixup: the plane walk lost its line continuation, and the power-domain macros already exist."""
import sys
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/lcd/n1_compat.h"
NL = chr(10)
TAB = chr(9)
BS = chr(92)
s = open(p).read()
bad = ("#define drm_for_each_plane_mask(plane, dev, plane_mask) " + NL +
       TAB + "for ((plane) = parity_n1_primary_plane(); (plane) != NULL; (plane) = NULL)")
good = ("#define drm_for_each_plane_mask(plane, dev, plane_mask) " + BS + NL +
        TAB + "for ((plane) = parity_n1_primary_plane(); (plane) != NULL; (plane) = NULL)")
if bad in s:
    s = s.replace(bad, good)
for dead in ["#define POWER_DOMAIN_TRANSCODER(tran) ((enum intel_display_power_domain)(POWER_DOMAIN_TRANSCODER_A + (tran)))" + NL,
             "#define POWER_DOMAIN_PIPE(pipe) ((enum intel_display_power_domain)(POWER_DOMAIN_PIPE_A + (pipe)))" + NL,
             "/* intel_display_power.h: the per-pipe / per-transcoder domains as the readout names them */" + NL]:
    s = s.replace(dead, "")
s = s.replace("#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) "
              "((enum intel_display_power_domain)(POWER_DOMAIN_PIPE_PANEL_FITTER_A + (pipe)))" + NL,
              "#ifndef POWER_DOMAIN_PIPE_PANEL_FITTER" + NL +
              "#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) "
              "((enum intel_display_power_domain)(POWER_DOMAIN_PIPE_PANEL_FITTER_A + (pipe)))" + NL + "#endif" + NL)
open(p, "w").write(s)
print("fixed")
