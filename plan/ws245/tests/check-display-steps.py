#!/usr/bin/env python3
# ws245: checks that the device start and stop (src/drivers/gpu/i915/device.c) call the display's steps in their
# order: the display's start before the interrupts, its interrupt half around the install, its outputs after the
# install, its serve after the GT; its serving stopped before the GT stops, its outputs released after, its stop after
# the interrupt uninstall, its destroy last; the suspend's and the resume's begin before their end.
#   python3 -I plan/ws245/tests/check-display-steps.py src/drivers/gpu/i915/device.c
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import sys

# Each function of device.c and the calls it must make, in this order.
ORDERS = {
    'drv_i915_device_start': ['i915_start_memory(', 'i915_start_display(', 'i915_start_interrupts(',
                              'i915_start_display_outputs(', 'i915_start_gt(', 'i915_start_display_serve('],
    'i915_start_display': ['drv_i915_display_start('],
    'i915_start_display_outputs': ['drv_i915_display_outputs('],
    'i915_start_display_serve': ['drv_i915_display_serve('],
    'i915_start_interrupts': ['drv_i915_display_irq_prepare(', 'drv_i915_irq_install(', 'drv_i915_display_irq_log('],
    'drv_i915_device_stop': ['i915_stop_gt(', 'drv_i915_display_stop_outputs(', 'i915_stop_hardware('],
    'i915_stop_gt': ['drv_i915_display_stop_serving('],
    'i915_stop_hardware': ['drv_i915_irq_uninstall(', 'drv_i915_display_stop(', 'drv_i915_display_destroy('],
    'drv_i915_device_suspend_hw': ['drv_i915_display_suspend_begin(', 'drv_i915_display_suspend_end('],
    'drv_i915_device_resume_hw': ['drv_i915_display_resume_begin(', 'drv_i915_display_resume_end('],
}


def body(text, name):
    """Returns the body of a function definition, from its opening brace to its closing one."""
    match = re.search(r'^' + re.escape(name) + r'\(\n(?:\t.*\n)*?\{\n', text, re.M)
    if match is None:
        return None
    end = text.find('\n}\n', match.end())
    return text[match.end():end]


def main():
    text = open(sys.argv[1]).read()
    failed = 0
    for name, calls in ORDERS.items():
        found = body(text, name)
        if found is None:
            print('check-display-steps: %s is not defined' % name)
            failed = 1
            continue
        last = -1
        for call in calls:
            where = found.find(call)
            if where < 0:
                print('check-display-steps: %s does not call %s' % (name, call))
                failed = 1
                break
            if where < last:
                print('check-display-steps: %s calls %s out of order' % (name, call))
                failed = 1
                break
            last = where
    if failed:
        return 1
    print('check-display-steps: the display steps are in order (%d functions)' % len(ORDERS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
