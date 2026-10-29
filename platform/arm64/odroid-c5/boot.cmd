# zedBSD on the ODROID-C5: the boot script Hardkernel's U-Boot runs from
# the SD card's FAT partition (its boot_attempt sets devtype, devnum and
# bootpart before it sources boot.scr).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

echo "zedBSD: boot.scr on ${devtype} ${devnum}:${bootpart}"

# The kernel is read high and moved down to its link address by booti;
# the device tree stays where it is read, below the first gigabyte that
# the kernel's boot tables map.  fdt_high keeps U-Boot from moving it.
setenv zedbsd_kernel_addr 0x20000000
setenv zedbsd_fdt_addr 0x10000000
setenv fdt_high 0xffffffffffffffff

# The console framebuffer: OSD1 scanning out 1920x1080 at 32 bits a pixel
# (ARGB, blue in the low byte) from the reserved logo memory the device
# tree's simple-framebuffer node describes.  U-Boot has brought HDMI up
# in its preboot; this points its OSD at that memory and turns it on.
setenv fb_addr 0x7f800000
setenv fb_width 1920
setenv fb_height 1080
setenv display_width 1920
setenv display_height 1080
setenv display_bpp 32
setenv display_color_index 32
osd open
osd clear
vout output ${outputmode}

# The kernel's command line.
setenv bootargs "login=console"

echo "zedBSD: loading odroid-c5.dtb and vmunix"
if load ${devtype} ${devnum}:${bootpart} ${zedbsd_fdt_addr} odroid-c5.dtb; then
	if load ${devtype} ${devnum}:${bootpart} ${zedbsd_kernel_addr} vmunix; then
		echo "zedBSD: booti ${zedbsd_kernel_addr} - ${zedbsd_fdt_addr}"
		booti ${zedbsd_kernel_addr} - ${zedbsd_fdt_addr}
	fi
fi
echo "zedBSD: boot failed; staying in U-Boot"
