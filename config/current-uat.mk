# The image for the current UAT on the Latitude 5330: the beta 1 release's configuration
# (config/release/config-amd64-beta1.mk: root locked, kei the user with the password "kei", sshd on),
# built as a development build (uname's release carries the source's revision), with the programs
# the UAT also looks at.  Build it from the top of the tree whenever wanted:
#   make -j16 ZEDBSD_CONFIG=config/current-uat.mk BUILD=build/uat disk-image
# and write build/uat/hdd-image.img to the USB stick.  Holding Ctrl (kernel messages on the console)
# and Shift (the console login) while tapping Space at power-on gives the safe boot.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/release/config-amd64-beta1.mk
ZEDBSD_RELEASE_BUILD := n
ZEDBSD_USER_PROGRAMS += systemevents sleepctl phone calendar mailer account-admin keiland-settings
