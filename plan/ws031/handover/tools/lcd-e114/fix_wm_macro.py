import os
L = os.path.expanduser("~/zedBSD/src/drivers/gpu/i915/parity/lcd/")
w = open(L + "lcd_wm_compat.h").read()
a = w.index("#define for_each_new_intel_plane_in_state(")
b = w.index("#define for_each_plane_id_on_crtc(")
new = ("#define for_each_new_intel_plane_in_state(state, plane, new_plane_state, i) " + chr(92) + chr(10) + chr(9) +
       "for ((i) = 0, (plane) = parity_lcd_wm->crtc_state->only_plane, " +
       "(new_plane_state) = (struct intel_plane_state *)parity_lcd_wm->crtc_state->only_plane_state; (i) < 1 && (plane) != 0; (i)++)" + chr(10))
w = w[:a] + new + w[b:]
open(L + "lcd_wm_compat.h", "w").write(w)
print("fixed")
