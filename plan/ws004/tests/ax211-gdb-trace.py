# gdb Python tracer for the AX211 passthrough analysis (BUG-158, q684):
# connects to a QEMU gdbstub before the guest runs, breaks on the AX211
# driver's lifecycle functions and the panic entry points, prints the time,
# the arguments and a backtrace at each hit, traces every CSR set/clear/poll
# while drv_intel_ax211_mmio_stop runs, samples every vCPU's pc periodically,
# and continues.  The output is a stream: when the host dies, the last lines
# are the last known state of the guest.  Symbols come from the unstripped
# vmunix (no DWARF needed; arguments are read from the SysV registers).
#
#   gdb -batch -ex 'set pagination off' -ex 'target remote 127.0.0.1:PORT' \
#       -x ax211-gdb-trace.py vmunix
#
# Environment: AX211_TRACE_SAMPLE_SECONDS (default 2) for the pc sampling,
# AX211_TRACE_KLOG_BYTES (default 6000) for the ring tail printed on panic.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import os
import re
import threading
import time

import gdb

SAMPLE_SECONDS = float(os.environ.get('AX211_TRACE_SAMPLE_SECONDS', '2'))
KLOG_BYTES = int(os.environ.get('AX211_TRACE_KLOG_BYTES', '6000'))

# Always-on breakpoints: the panic entry points and the AX211 lifecycle.
EVENT_FUNCTIONS = [
    'hal_fatal', 'kern_fatal',
    'drv_intel_ax211_mmio_stop', 'drv_intel_ax211_mmio_sw_reset',
    'drv_intel_ax211_mmio_prepare_card_hw', 'drv_intel_ax211_mmio_apm_init',
    'ax211_pci_session_stop', 'ax211_runtime_start_stop_and_release',
    'ax211_pci_interrupt_drain', 'ax211_pci_recovery_latch_locked',
    'ax211_pci_recovery_run_locked', 'ax211_pci_close_locked',
    'ax211_radio_stop_retry', 'ax211_net_open', 'ax211_net_close',
    'drv_intel_ax211_command_timeout_oldest', 'drv_intel_ax211_command_cancel',
    'ax211_pci_scan_report_error', 'drv_intel_ax211_transport_quiesce',
    'drv_intel_ax211_mmio_prph_write32',
]
# Enabled only between entry to and return from the outer function: every
# device access the stop sequence and the interrupt drain make, in order.
INNER_FUNCTIONS = {
    'drv_intel_ax211_mmio_stop': [
        'ax211_csr_set_bits', 'ax211_csr_clear_bits', 'ax211_poll_csr'],
    'ax211_pci_interrupt_drain': [
        'ax211_mask_all', 'ax211_backend_csr_write32',
        'ax211_backend_csr_write32.4299', 'ax211_backend_csr_read32',
        'ax211_backend_csr_read32.4298',
        'drv_pci_device_disestablish_irq_checked', 'kern_irq_unregister_msi',
        'irq_set_handler', 'cfg_write', 'pcat_config_write', 'pcat_free_irqs',
        'pcat_unmap_bar', 'command_set', 'pci_command_quiesce'],
}
INNER_NAMES = set(name for names in INNER_FUNCTIONS.values() for name in names)
PANIC_FUNCTIONS = {'hal_fatal', 'kern_fatal'}

start = time.time()


def stamp():
    return '%8.3f' % (time.time() - start)


def reg(name):
    return int(gdb.parse_and_eval('$' + name)) & 0xffffffffffffffff


def read_string(address, limit=200):
    try:
        return gdb.selected_inferior().read_memory(address, limit).tobytes().split(b'\0')[0].decode(errors='replace')
    except gdb.error as error:
        return '<unreadable: %s>' % error


def run(command):
    try:
        return gdb.execute(command, to_string=True)
    except gdb.error as error:
        return '<%s failed: %s>' % (command, error)


def symbol_address(name):
    text = run('info address %s' % name)
    match = re.search(r'at (0x[0-9a-f]+)', text)
    if match is None:
        raise gdb.error('no address for %s: %s' % (name, text.strip()))
    return int(match.group(1), 16)


def read_word(address):
    return int.from_bytes(gdb.selected_inferior().read_memory(address, 8).tobytes(), 'little')


def klog_tail():
    try:
        capacity = int(gdb.parse_and_eval('sizeof(klog_buffer)'))
    except gdb.error:
        capacity = 512 * 1024
    try:
        base = symbol_address('klog_buffer')
        oldest = read_word(symbol_address('klog_oldest'))
        used = read_word(symbol_address('klog_used'))
    except gdb.error as error:
        return '<klog unreadable: %s>' % error
    take = min(used, KLOG_BYTES)
    first = (oldest + used - take) % capacity
    inferior = gdb.selected_inferior()
    data = b''
    while take > 0:
        chunk = min(take, capacity - first)
        data += inferior.read_memory(base + first, chunk).tobytes()
        first = (first + chunk) % capacity
        take -= chunk
    return 'klog oldest=%d used=%d tail:\n%s' % (oldest, used, data.decode(errors='replace'))


class Tracer:
    def __init__(self):
        self.inner = {}
        self.events = []
        self.return_bp = None
        self.stop_depth = 0
        self.halted = False
        self.armed = False
        self.klog_end = None
        # The kernel's text is not mapped while the firmware and the boot
        # loader run, so software breakpoints cannot be inserted yet; they
        # start disabled and are armed at the first sample that finds the
        # kernel mapped (arm_if_mapped).
        for name in EVENT_FUNCTIONS:
            try:
                bp = gdb.Breakpoint(name)
                bp.silent = True
                bp.enabled = False
                self.events.append(bp)
            except gdb.error as error:
                print('%s no breakpoint for %s: %s' % (stamp(), name, error))
        for outer, names in INNER_FUNCTIONS.items():
            self.inner[outer] = []
            for name in names:
                try:
                    bp = gdb.Breakpoint(name)
                    bp.silent = True
                    bp.enabled = False
                    self.inner[outer].append(bp)
                except gdb.error as error:
                    print('%s no breakpoint for %s: %s' % (stamp(), name, error))
        self.inner_outer = None
        gdb.events.stop.connect(self.on_stop)

    def set_inner(self, outer, enabled):
        for bp in self.inner.get(outer, []):
            bp.enabled = enabled
        self.inner_outer = outer if enabled else None

    def all_inner(self):
        return [bp for bps in self.inner.values() for bp in bps]

    def arm_if_mapped(self):
        if self.armed:
            return
        try:
            probe = int(gdb.parse_and_eval('(unsigned long)&hal_fatal'))
            gdb.selected_inferior().read_memory(probe, 1)
        except gdb.error:
            return
        for bp in self.events:
            bp.enabled = True
        self.armed = True
        print('%s ARMED %d event breakpoints (kernel text is mapped)' % (stamp(), len(self.events)))

    def klog_delta(self):
        # Prints what the kernel appended to its log ring since the last
        # sample (read from guest memory, not from any console).
        try:
            capacity = int(gdb.parse_and_eval('sizeof(klog_buffer)'))
        except gdb.error:
            capacity = 512 * 1024  # KLOG_CAPACITY, src/kern/klog.c (no DWARF)
        try:
            base = symbol_address('klog_buffer')
            oldest = read_word(symbol_address('klog_oldest'))
            used = read_word(symbol_address('klog_used'))
        except gdb.error as error:
            print('  klog unreadable: %s' % error)
            return
        end = (oldest + used) % capacity
        if self.klog_end is None:
            self.klog_end = end
            return
        fresh = (end - self.klog_end) % capacity
        if fresh == 0 or fresh > used:
            self.klog_end = end
            return
        first = self.klog_end
        inferior = gdb.selected_inferior()
        data = b''
        while fresh > 0:
            chunk = min(fresh, capacity - first)
            data += inferior.read_memory(base + first, chunk).tobytes()
            first = (first + chunk) % capacity
            fresh -= chunk
        self.klog_end = end
        print('  klog+ ' + data.decode(errors='replace').rstrip().replace('\n', '\n  klog+ '))

    def sample(self):
        self.arm_if_mapped()
        print('%s SAMPLE' % stamp())
        if self.armed:
            self.klog_delta()
        print(run('thread apply all x/i $pc'))
        print(run('thread apply all bt 6'))

    def event(self, name):
        thread = gdb.selected_thread()
        cpu = thread.num if thread is not None else -1
        args = ' '.join('%s=%#x' % (r, reg(r)) for r in ['rdi', 'rsi', 'rdx', 'rcx'])
        print('%s EVENT cpu%d %s %s' % (stamp(), cpu, name, args))
        if name in PANIC_FUNCTIONS:
            if name != '__libc_panic':
                print('  fatal %s:%d: %s' % (read_string(reg('rdi')), reg('rsi') & 0xffffffff, read_string(reg('rdx'))))
            else:
                print('  panic: %s' % read_string(reg('rdi')))
            print(run('bt 20'))
            print(run('thread apply all bt 12'))
            print(klog_tail())
            self.halted = True
            return
        if name in INNER_NAMES:
            print('  ' + run('bt 4').strip().replace('\n', '\n  '))
            return
        print('  ' + run('bt 12').strip().replace('\n', '\n  '))
        if name in INNER_FUNCTIONS and self.inner_outer is None:
            self.set_inner(name, True)
            ret = int.from_bytes(gdb.selected_inferior().read_memory(reg('rsp'), 8).tobytes(), 'little')
            self.return_bp = gdb.Breakpoint('*%#x' % ret, temporary=True)
            self.return_bp.silent = True
            print('  %s: tracing device accesses until return to %#x' % (name, ret))

    def on_stop(self, ev):
        if isinstance(ev, gdb.BreakpointEvent):
            for bp in ev.breakpoints:
                if self.return_bp is not None and bp.number == self.return_bp.number:
                    print('%s EVENT %s returned eax=%#x' % (stamp(), self.inner_outer, reg('rax') & 0xffffffff))
                    self.return_bp = None
                    self.set_inner(self.inner_outer, False)
                    continue
                self.event(bp.location)
        elif isinstance(ev, gdb.SignalEvent):
            self.sample()
        else:
            print('%s STOP %s' % (stamp(), ev))
            self.sample()


def ticker():
    while True:
        time.sleep(SAMPLE_SECONDS)
        try:
            gdb.interrupt()
        except Exception as error:  # noqa: BLE001 - keep sampling
            print('interrupt failed: %s' % error)


tracer = Tracer()
threading.Thread(target=ticker, daemon=True).start()
print('%s TRACE START' % stamp())
while not tracer.halted:
    try:
        gdb.execute('continue')
    except gdb.error as error:
        # A breakpoint that cannot be inserted (text not mapped) aborts the
        # continue; disarm, continue, and arm again at the next sample.
        print('%s continue failed: %s' % (stamp(), error))
        if 'not being run' in str(error):
            break
        for bp in tracer.events + tracer.all_inner():
            bp.enabled = False
        tracer.armed = False
        time.sleep(0.5)
    except KeyboardInterrupt:
        # The ticker's gdb.interrupt() landed while Python, not the guest,
        # was running (seen in q684 run 6); nothing stopped, so go on.
        pass
print('%s TRACE END (guest halted; gdb stays attached)' % stamp())
