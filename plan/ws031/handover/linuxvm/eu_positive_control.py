#!/usr/bin/env python3
"""One-work-item Intel ADL-P OpenCL positive control for a Linux VFIO guest.

Ubuntu example dependencies:
  sudo apt install --no-install-recommends intel-opencl-icd python3-pyopencl python3-numpy
Run in the Linux guest (not on the host):
  sudo timeout --kill-after=5s 60s /usr/bin/python3 -u eu_positive_control.py

The script requires exactly one Intel PCI display device, 8086:46a8, bound to
'i915', and exactly one Intel-vendor GPU on Intel's OpenCL platform. It never
selects a CPU device. A PCI-bus extension, when available, is checked as well.
It does NOT validate the custom driver's binary, A64 descriptor, VA, or LRC.
No resets or system configuration changes are performed by this script.
An external timeout bounds ordinary userspace waiting, not a wedged kernel.

API references:
  https://documen.tician.de/pyopencl/runtime_platform.html
  https://documen.tician.de/pyopencl/runtime_program.html
  https://documen.tician.de/pyopencl/runtime_memory.html
  https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.util
from pathlib import Path
import sys


def find_target(root: Path = Path('/sys/bus/pci/devices')) -> str:
    """Require an unambiguous physical Intel display controller."""
    gpus: list[Path] = []
    for path in sorted(root.iterdir()):
        try:
            vendor = int((path / 'vendor').read_text().strip(), 16)
            pci_class = int((path / 'class').read_text().strip(), 16)
        except (OSError, ValueError):
            continue
        if vendor == 0x8086 and (pci_class >> 16) == 0x03:
            gpus.append(path)
    if len(gpus) != 1:
        raise RuntimeError(f'Expected exactly one Intel PCI display device; found {len(gpus)}')
    path = gpus[0]
    device_id = int((path / 'device').read_text().strip(), 16)
    if device_id != 0x46A8:
        raise RuntimeError(f'Target is 8086:{device_id:04x}, not 8086:46a8')
    driver = (path / 'driver').resolve(strict=True).name
    if driver != 'i915':
        raise RuntimeError(f'{path.name} is bound to {driver}, not i915')
    return path.name


def pci_bdf_from_opencl(device) -> str | None:
    """Query cl_khr_pci_bus_info via the ICD loader, independent of wrapper version."""
    if 'cl_khr_pci_bus_info' not in device.extensions.split():
        return None

    class PciInfo(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint32)
                    for name in ('domain', 'bus', 'device', 'function')]

    library = ctypes.util.find_library('OpenCL')
    if library is None:
        raise RuntimeError('Cannot find OpenCL ICD loader for PCI identity query')
    loader = ctypes.CDLL(library)
    query = loader.clGetDeviceInfo
    query.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_size_t,
                      ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    query.restype = ctypes.c_int32
    info = PciInfo()
    returned = ctypes.c_size_t()
    status = query(ctypes.c_void_p(device.int_ptr), 0x410F, ctypes.sizeof(info),
                   ctypes.byref(info), ctypes.byref(returned))
    if status != 0 or returned.value != ctypes.sizeof(info):
        raise RuntimeError(f'PCI query failed: status={status}, bytes={returned.value}')
    return f'{info.domain:04x}:{info.bus:02x}:{info.device:02x}.{info.function:x}'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repeat', type=int, default=3,
                        help='Number of one-work-item runs (default: 3)')
    args = parser.parse_args()
    if not 1 <= args.repeat <= 1000:
        parser.error('--repeat must be between 1 and 1000')

    try:
        import numpy as np
        import pyopencl as cl
    except ImportError as exc:
        raise RuntimeError('Install python3-pyopencl and python3-numpy; use /usr/bin/python3') from exc

    target_bdf = find_target()
    print(f'PCI target: {target_bdf} 8086:46a8, driver=i915', flush=True)
    candidates = []
    for platform in cl.get_platforms():
        for device in platform.get_devices(device_type=cl.device_type.GPU):
            print(f'GPU candidate: platform={platform.name!r}, vendor={platform.vendor!r}, '
                  f'device={device.name!r}, vendor_id=0x{device.vendor_id:04x}', flush=True)
            if device.vendor_id == 0x8086 and 'intel' in platform.vendor.lower():
                candidates.append((platform, device))
    if len(candidates) != 1:
        raise RuntimeError(f'Expected exactly one Intel GPU on Intel OpenCL; found {len(candidates)}. '
                           'No CPU or automatic fallback is permitted.')
    platform, device = candidates[0]
    cl_bdf = pci_bdf_from_opencl(device)
    if cl_bdf is not None:
        if cl_bdf != target_bdf:
            raise RuntimeError(f'OpenCL PCI device {cl_bdf} does not match {target_bdf}')
        print(f'OpenCL PCI identity: {cl_bdf} (matches sysfs)', flush=True)
    else:
        print('NOTE: no cl_khr_pci_bus_info; identity relies on exactly one Intel PCI GPU '
              'and exactly one Intel OpenCL GPU.', flush=True)
    print(f'Platform version: {platform.version}', flush=True)
    print(f'Device: {device.name}; driver: {device.driver_version}; OpenCL: {device.version}', flush=True)

    context = cl.Context(devices=[device],
                         properties=[(cl.context_properties.PLATFORM, platform)])
    queue = cl.CommandQueue(context, device=device)
    source = '''
    __kernel void eu_marker(__global uint *out, uint tag) {
        out[0] = tag;
    }
    '''
    program = cl.Program(context, source)
    print('STAGE: build', flush=True)
    try:
        program.build(options=['-cl-std=CL1.2'])
    except Exception:
        try:
            print(program.get_build_info(device, cl.program_build_info.LOG), file=sys.stderr)
        except Exception:
            pass
        raise
    kernel = cl.Kernel(program, 'eu_marker')
    for iteration in range(args.repeat):
        tag = np.uint32(0xC0FFEE02 + iteration)
        initial = np.array([0xDEAD0000, 0x13579BDF], dtype=np.uint32)
        output = np.zeros(2, dtype=np.uint32)
        buffer = cl.Buffer(context, cl.mem_flags.READ_WRITE | cl.mem_flags.COPY_HOST_PTR,
                           hostbuf=initial)
        print(f'STAGE: enqueue run={iteration + 1}, global=1, local=1, tag=0x{int(tag):08x}',
              flush=True)
        event = kernel(queue, (1,), (1,), buffer, tag)
        print('STAGE: wait for kernel completion', flush=True)
        event.wait()
        queue.finish()
        print('STAGE: read back', flush=True)
        cl.enqueue_copy(queue, output, buffer, is_blocking=True)
        if int(output[0]) != int(tag) or int(output[1]) != 0x13579BDF:
            raise RuntimeError(f'Output mismatch: got {[hex(int(x)) for x in output]}, '
                               f'expected {[hex(int(tag)), "0x13579bdf"]}')
        print(f'PASS run={iteration + 1}: marker=0x{int(output[0]):08x}, canary intact', flush=True)
        buffer.release()
    print('PASS: Intel GPU kernel execution, completion, and post-completion readback.', flush=True)
    print('Check the guest kernel log separately for GPU reset/hang/recovery events.', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:
        print(f'FAIL: {type(exc).__name__}: {exc}', file=sys.stderr, flush=True)
        sys.exit(1)
