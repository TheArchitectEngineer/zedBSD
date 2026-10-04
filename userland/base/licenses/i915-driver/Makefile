# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include $(dir $(lastword $(MAKEFILE_LIST)))../../package.mk

I915_DRIVER_LICENSE_DATA := \
	/usr/share/licenses/i915-driver/LICENSE=userland/base/licenses/i915-driver/LICENSE

# This package is selected automatically when the kernel i915 driver is
# enabled (ws129-p002).  It is deliberately hidden from menuconfig: the driver
# contains code derived from the Linux i915 and DRM display drivers, whose MIT
# notice must accompany every copy, binaries included.
$(eval $(call ZEDBSD_USERLAND_PACKAGE,i915-driver-license,Intel i915 driver license,\
	amd64,n,data,,n,base,base/licenses/i915-driver,,,data,\
	$(I915_DRIVER_LICENSE_DATA)))
