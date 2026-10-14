# Optional firmware packages

Copyright (C) 2026 Awe Morris. SPDX-License-Identifier: Zlib

Device firmware is not part of the zedBSD base system. Each supported firmware
family has one independently selectable directory below this one, downloads
only from a reviewed immutable upstream or acquisition-mirror revision when
explicitly selected, verifies all declared bytes before publishing its cache,
and installs the firmware below `/lib/firmware` together with its applicable
license, WHENCE record when required, and provenance manifest.

Every Firmware menu entry is selectable on every CPU architecture. The build
registry supplies the same catalog to the menu, configuration saving and image
packaging; selection follows the attached device rather than the CPU. Selecting
a firmware package installs its data without enabling a hardware driver. The
other way round, menuconfig selects the firmware a driver needs when the
driver is turned on, and drops it when no driver that is on needs it any more
(`config/drivers/firmware.list`); the licenses are still accepted when the
image is built.

The entries are `rtl8822b/`, `intelax211/`, `intelbt/`, `i915/`, `brcmbt/`,
`bcm43455/`, and `nvidia-gsp/`; future
RTL8822C support owns a separate `rtl8822c/` entry. RTL8822B uses its frozen GitHub
acquisition mirror. AX211 uses the official `linux-firmware` tag `20260410`
dereferenced commit and installs the exact `-89.ucode`, PNVM, complete Intel
license, WHENCE, and manifest. `i915/` (package `i915-firmware`) uses the same
official commit and installs the Intel display DMC firmware
`i915/adlp_dmc.bin` (ADL-P, v2.20) and `i915/tgl_dmc_ver2_12.bin` (TGL, v2.12)
below `/lib/firmware/i915/`, byte-identical to the blobs the i915 driver was
brought up with, together with the complete `LICENSE.i915`, WHENCE, and
manifest. `LICENSE.i915` permits redistribution of the unmodified binaries
only, with its notice, so these bytes belong to this package and not to the
kernel. Its focused check is
`plan/ws031/tests/run-i915-firmware-package-test.sh`. `intelbt/` (package
`intelbt-firmware`) uses the same official commit and installs the whole
Solar block of WHENCE (`BT_Solar_REL82122_23.50.26053.82122`): the three
`intel/ibt-*-0041.{sfi,ddc}` pairs that bluetoothd loads into an AX211's
Bluetooth controller, and, as copies of the same verified bytes, the eight
names WHENCE links to them, because the image builder installs files only.
`LICENCE.ibt_firmware` permits redistribution of the unmodified binaries
with its notice and forbids reverse engineering; bluetoothd reads only the
HCI command frames a `.sfi` file is made of. Its focused check is
`plan/ws143/tests/intelbt-firmware-package-test.sh`. `brcmbt/` (package
`brcmbt-firmware`) installs `brcm/BCM4345C0.hcd`, the patch file bluetoothd
loads into the Bluetooth part of the Raspberry Pi 4's BCM43455 over its
UART, from Raspberry Pi's `RPi-Distro/bluez-firmware` commit
`adb4d55c8f193ff16ece74dd72fd6e8801632103` (1.2-9+rpt4). That tree carries
the file under the Cypress Wireless Connectivity Devices EULA but keeps the
licence only as a saved HTML page, so the package installs the plain
`LICENCE.cypress` of the official linux-firmware commit above (the blob the
saved page shows), the tree's `debian/copyright`, and the manifest. The EULA permits
redistribution of the object code only, solely for use with Cypress
integrated circuits, and forbids modification and reverse engineering;
the Raspberry Pi 4 image configuration is the only one that selects it.
Its focused check is `plan/ws215/tests/brcmbt-firmware-package-test.sh`.
`bcm43455/` (package `bcm43455-firmware`) installs the three files the
Raspberry Pi 4's on-board WiFi driver writes into the BCM43455:
`cypress/cyfmac43455-sdio-minimal.bin` (firmware 7.45.241, the variant
with the firmware supplicant), `cypress/cyfmac43455-sdio.clm_blob`
(Raspberry Pi's regulatory data, used with the world locale), and
`brcm/brcmfmac43455-sdio.txt` (the Pi 4 NVRAM), all from Raspberry Pi's
`RPi-Distro/firmware-nonfree` commit
`3bab0f823f5b53150b76aab77093adef6655b920` (1:20260519-1~bpo13+1+rpt1),
whose `debian/copyright` places them under the Cypress licence. The
same NVRAM values are published in linux-firmware under GPL-2.0+, so the
package installs the plain `LICENCE.cypress` and `GPL-2` of the official
linux-firmware commit above together with the full `debian/copyright` and
the manifest. Only the Raspberry Pi 4 image configuration selects it.
Its focused check is `plan/ws206/tests/bcm43455-firmware-package-test.sh`.
`nvidia-gsp/` (package `nvidia-gsp-firmware`) uses the same official
linux-firmware commit and installs NVIDIA's GSP-RM firmware 570.144 for
Turing, which the nvrtx driver boots (plan/ws037/nvrtx-design.md section 9):
`nvidia/tu102/gsp/gsp-570.144.bin`, `bootloader-570.144.bin`,
`booter_load-570.144.bin`, `booter_unload-570.144.bin`, and
`nvidia/tu102/acr/bl.bin`, together with the complete `LICENCE.nvidia`,
WHENCE, and manifest.  WHENCE links the tu104 and tu106 names to tu102; the
files are installed once below tu102 and the driver reads that directory for
every TU10x chip.  `LICENCE.nvidia` permits redistribution of the unmodified
binaries solely for use on an operating system under an OSI-approved open
source license, with a copy of the license, and forbids reverse engineering
and translation; nvrtx reads only the container headers needed to place the
images.
Every entry is default-off, and ordinary builds perform no firmware fetch.

Every file a package fetches is also kept, byte for byte and with its license
texts, in the GitHub repository `awemorris/zedBSD-firmware`, one directory per
package (`rpi4-boot/` holds the Raspberry Pi 4 boot firmware of
`platform/arm64/firmware.mk`). `zedbsd-firmware.mk` names the repository's
commit; while that is set, a package fetches each file from the repository
first and from its upstream only when that fails, and checks the same size and
SHA-256 either way. While it is empty, the packages fetch from their upstream
alone.

The first time a package's firmware is fetched, `license-agree.sh` shows what
the firmware is and its copyright (the package's `*.notice` file), the whole
license text, and asks `yes/no`. An accepted text is recorded beside the cache
(`build/sources/firmware/PACKAGE/license-accepted`) and is not asked about
again; a changed text is. Any other answer, or none, stops the build without
fetching. A build goal asks while make reads the makefiles, so `make -j` is
safe; an unattended build answers first with `yes | make firmware-licenses`,
as CI does. The build menu (`make menuconfig`, Build boot image) asks in a
license screen of its own before it builds: `make firmware-licenses-pending`
lists what would be asked without asking, and `license-agree.sh` in its
accept mode writes the record.
This hierarchy is package organization, not a common hardware-driver layer.
