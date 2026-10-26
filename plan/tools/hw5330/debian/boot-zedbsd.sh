#!/bin/sh
# Start zedBSD from the NVMe once, on the Latitude 5330's Debian (q1614, 2026-10-27).  It sets the UEFI BootNext to
# the entry "zedBSD NVMe" (\EFI\zedbsd\BOOTX64.EFI on the Debian ESP, found by its label and checked by its path;
# the BootOrder is not changed, so the start after that is Debian again) and reboots.
#   ~/boot-zedbsd.sh              set BootNext and reboot now
#   ~/boot-zedbsd.sh --no-reboot  set BootNext only (the next reboot, whenever it comes, starts zedBSD once)
#   ~/boot-zedbsd.sh --cancel     clear BootNext again
# This is the UEFI path; the GRUB menu has its own entry "zedBSD (NVMe)" (/etc/grub.d/42_zedbsd) for choosing by
# hand.  Both start the same loader; this script does not touch GRUB.  Installed as ~/boot-zedbsd.sh; the source is
# plan/tools/hw5330/debian/boot-zedbsd.sh.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
LABEL='zedBSD NVMe'
LOADER='\EFI\zedbsd\BOOTX64.EFI'
ESP_PARTUUID=08ab575f-f718-4eff-a3c9-10d80b1217ea

case "${1:-}" in
'') reboot=1 ;;
--no-reboot) reboot=0 ;;
--cancel)
	sudo efibootmgr --delete-bootnext > /dev/null
	echo "boot-zedbsd: BootNext cleared"
	exit 0
	;;
*)
	echo "usage: $0 [--no-reboot | --cancel]" >&2
	exit 2
	;;
esac

# The entry: its label exactly, on the ESP, with the zedBSD loader (the line is "BootXXXX* LABEL<TAB>HD(...)/File(...)").
# awk -v would read the backslashes of the path as escapes, so the values go through the environment.
entry=$(sudo efibootmgr -v | LABEL=$LABEL LOADER=$LOADER UUID=$ESP_PARTUUID awk '
	BEGIN { label = ENVIRON["LABEL"]; loader = ENVIRON["LOADER"]; uuid = ENVIRON["UUID"] }
	/^Boot[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]\*? / {
		head = $0; sub(/\t.*/, "", head); name = head; sub(/^Boot[0-9A-Fa-f]+\*? /, "", name)
		if (name == label && index(tolower($0), uuid) && index(tolower($0), tolower("File(" loader ")")))
			print substr($0, 5, 4)
	}' | head -1)
if [ -z "$entry" ]; then
	echo "boot-zedbsd: no UEFI entry \"$LABEL\" for $LOADER (make it with 5330-run.sh --nvme-none, or" >&2
	echo "  sudo efibootmgr --create-only -d /dev/nvme0n1 -p 1 -L \"$LABEL\" -l '$LOADER')" >&2
	exit 1
fi
sudo test -f /boot/efi/EFI/zedbsd/BOOTX64.EFI || { echo "boot-zedbsd: the loader is not on the ESP" >&2; exit 1; }

sudo efibootmgr --bootnext "$entry" > /dev/null
next=$(sudo efibootmgr | sed -n 's/^BootNext: //p')
[ "$next" = "$entry" ] || { echo "boot-zedbsd: BootNext is \"$next\", not $entry" >&2; exit 1; }
echo "boot-zedbsd: BootNext $entry ($LABEL)"
if [ "$reboot" = 1 ]; then
	echo "boot-zedbsd: rebooting"
	sudo systemctl reboot
fi
