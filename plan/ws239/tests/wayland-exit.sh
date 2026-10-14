#!/bin/sh
# ws239-p002 case B: a compositor that fails at once (copied over /bin/wayland in the test image only): it says
# nothing to sessiond and exits 1, as a compositor whose GPU or display cannot be opened does.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
echo "KWL EXIT frames=0 error=19 (ws239 fake compositor)"
exit 1
