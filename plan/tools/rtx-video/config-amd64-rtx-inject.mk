# The RTX 2070 machine's test kernel with the AAT's input injection (q1692-i01, Notes typed into on the RTX): the
# product config with CONFIG_INPUT_TEST_INJECT and the aat-input program (copied to the machine, not in an image).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += aat-input
