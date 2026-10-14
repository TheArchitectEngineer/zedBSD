#!/bin/sh
# ws239-p002 case C: a compositor that hangs in its start (copied over /bin/wayland in the test image only): it never
# says READY, so sessiond ends it after 60 seconds and counts it as a failure to start.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
echo "ws239 fake compositor: hanging"
exec sleep 3600
