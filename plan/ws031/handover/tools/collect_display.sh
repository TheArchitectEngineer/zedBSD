#!/bin/bash
# WS031: one-shot capture of the display reference state from a Linux i915 boot (run as root in the guest).
# Read-only with respect to the hardware: sysfs/debugfs reads, AUX DPCD reads, register reads.
set -u
O=/root/dispref; rm -rf $O; mkdir -p $O; cd $O
D=/sys/kernel/debug/dri/0
[ -d $D ] || D=$(ls -d /sys/kernel/debug/dri/* 2>/dev/null | head -1)
echo "debugfs=$D" > 00-summary.txt
uname -r >> 00-summary.txt
cat /sys/module/i915/parameters/enable_guc >> 00-summary.txt 2>&1
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends intel-gpu-tools edid-decode pciutils > apt.log 2>&1
echo "== connectors" >> 00-summary.txt
for c in /sys/class/drm/card*-*; do
  n=$(basename $c)
  echo "$n status=$(cat $c/status 2>/dev/null) enabled=$(cat $c/enabled 2>/dev/null) dpms=$(cat $c/dpms 2>/dev/null) edid_bytes=$(stat -c %s $c/edid 2>/dev/null)" >> 00-summary.txt
  cat $c/modes 2>/dev/null | head -3 | sed "s/^/    mode /" >> 00-summary.txt
  if [ -s $c/edid ]; then cat $c/edid > edid-$n.bin; edid-decode edid-$n.bin > edid-$n.txt 2>&1; fi
done
echo "== backlight" >> 00-summary.txt
for b in /sys/class/backlight/*; do
  [ -d "$b" ] && echo "$(basename $b) type=$(cat $b/type) max=$(cat $b/max_brightness) actual=$(cat $b/actual_brightness) brightness=$(cat $b/brightness)" >> 00-summary.txt
done
ls /sys/class/backlight >> 00-summary.txt 2>&1
echo "== fb / vbt / opregion" >> 00-summary.txt
cat /proc/fb >> 00-summary.txt 2>&1
for f in i915_vbt i915_opregion; do
  if [ -e $D/$f ]; then cat $D/$f > $f.bin 2>/dev/null; echo "$f bytes=$(stat -c %s $f.bin)" >> 00-summary.txt; fi
done
echo "ASLS(0xfc)=$(setpci -s 00:02.0 fc.l 2>&1)" >> 00-summary.txt
for f in i915_display_info i915_shared_dplls_info i915_ddb_info i915_power_domain_info i915_dmc_info \
         i915_display_capabilities i915_edp_psr_status i915_frontbuffer_tracking i915_hpd_storm_ctl i915_hpd_short_storm_ctl; do
  [ -e $D/$f ] && cat $D/$f > dbg-$f.txt 2>&1
done
for c in $D/eDP-1 $D/DP-1 $D/HDMI-A-1; do
  [ -d $c ] && for f in $c/*; do [ -f $f ] && { echo "### $f"; cat $f 2>&1; } ; done > dbg-$(basename $c).txt
done
[ -d $D/crtc-0 ] && for f in $D/crtc-0/*; do echo "### $f"; cat $f 2>&1; done > dbg-crtc-0.txt
# DPCD over the AUX character device (read-only)
for a in /dev/drm_dp_aux*; do
  [ -e $a ] || continue
  n=$(basename $a)
  dd if=$a of=dpcd-$n-000.bin bs=1 count=256 2>/dev/null
  dd if=$a of=dpcd-$n-200.bin bs=1 skip=$((0x200)) count=256 2>/dev/null
  dd if=$a of=dpcd-$n-700.bin bs=1 skip=$((0x700)) count=256 2>/dev/null
  echo "$n name=$(cat /sys/class/drm_dp_aux_dev/$n/name 2>/dev/null) dpcd000=$(stat -c %s dpcd-$n-000.bin)" >> 00-summary.txt
done
# registers (names resolved by intel_reg where known; raw offsets otherwise)
R="0xC7200 0xC7204 0xC7208 0xC720C 0xC7210 0xC8250 0xC8254 0xC8258 0x48250 0x48254 \
0x60000 0x60004 0x60008 0x6000C 0x60010 0x60014 0x6001C 0x60028 0x60030 0x60034 0x60040 0x60044 0x60400 0x60404 0x70008 0x46140 \
0x6F000 0x6F004 0x6F008 0x6F00C 0x6F010 0x6F014 0x6F01C 0x6F030 0x6F034 0x6F040 0x6F044 0x6F400 0x6F404 0x7F008 \
0x46010 0x46014 0x164280 0x164284 0x164288 0x16428C 0x164290 0x164294 0x64000 0x64040 0x64044 0x64100 \
0x70180 0x70184 0x70188 0x7018C 0x70190 0x7019C 0x701CC 0x70240 0x7027C 0x45008 0x44FE8 0x4438C 0x46000 0x45504 0x45400 0x45404 \
0xC4030 0xC4034 0xC4038 0xC4004 0xC4008 0xC400C 0x44470 0x44474 0x44478 0x4447C 0x162000 0x162100 0x162104"
for r in $R; do intel_reg read $r 2>&1 | tail -1; done > regs-selected.txt
intel_reg dump > regs-dump.txt 2>&1
dmesg > dmesg.txt
dmesg | grep -aiE "i915|drm|eDP|backlight|vbt|opregion|panel|pps|link train|DPLL|hotplug|fbcon|fb0" > dmesg-display.txt
lspci -nnvv -s 00:02.0 > lspci.txt 2>&1
cd /root && tar czf dispref.tgz dispref && ls -la dispref.tgz && cat dispref/00-summary.txt
