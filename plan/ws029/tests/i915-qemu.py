#!/usr/bin/env python3
"""Boot one disposable zedBSD image under QEMU with the host IGD passed through.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Runs on the test host as root (sudo -n) because VFIO pins guest RAM and the
QMP socket and pmemsave files are created by QEMU. The output directory is
handed back to --owner at the end. QMP, guest-log and console helpers come
from the sibling venus-qemu.py so the boot protocol stays identical.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('venus_qemu', HERE / 'venus-qemu.py')
venus_qemu = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(venus_qemu)

PATTERN_BASE = 0x5a000000
FILL_VALUE = 0x3197a5e2
STORE_VALUE = 0xdeadbeef
TEST_BYTES = 65536
RESOURCE_LINE = re.compile(r'i915: resource session=(\d+) slot=(\d+) bytes=(\d+) phys=0x([0-9a-f]+) va=0x([0-9a-f]+) handle=(\d+)')
PASS_LINE = re.compile(r'GPUI915 PASS copy=(\d) fill=(\d) store=(\d) job=(\d) src_handle=(\d+) dst_handle=(\d+)')
FAIL_LINE = re.compile(r'GPUI915 FAIL stage=(\S+) errno=(\d+)')
HANG_LINE = re.compile(r'GPUI915 HANG wait_errno=(-?\d+) status=(\d+) peer=(\w+)')
PEER_OK_LINE = re.compile(r'GPUI915 ISOLATION_PEER_OK')


def digest(path):
    return venus_qemu.digest(path)


def driver_state(text):
    """Extracts the i915 attach evidence from the guest kernel log."""
    state = {'attach_stopped': None, 'selftest': None, 'registered': False, 'device_line': None}
    stopped = re.search(r'i915: attach stopped at (\S+): (-?\d+)', text)
    if stopped:
        state['attach_stopped'] = {'stage': stopped.group(1), 'error': int(stopped.group(2))}
    selftest = re.search(r'i915: selftest bcs0 store=(\w+) irq=(\d+) seqno=(\d+)/(\d+)', text)
    if selftest:
        state['selftest'] = {'store': selftest.group(1), 'irq': int(selftest.group(2)),
                             'completed_seqno': int(selftest.group(3)), 'request_seqno': int(selftest.group(4))}
    state['registered'] = 'i915: registered native GPU node' in text
    device = re.search(r'i915: device 8086:([0-9a-f]{4}) rev ([0-9a-f]{2})', text)
    if device:
        state['device_line'] = {'product': device.group(1), 'revision': device.group(2)}
    state['ggtt'] = re.search(r'i915: GGTT (\d+) entries', text) is not None
    state['msi'] = re.search(r'i915: MSI vector (\d+) enabled', text) is not None
    return state


def memory_check(qmp, output, text, pass_match):
    """Dumps the test resources through QMP and checks them independently of the guest."""
    resources = {int(m.group(6)): {'session': int(m.group(1)), 'slot': int(m.group(2)),
                                   'bytes': int(m.group(3)), 'phys': int(m.group(4), 16),
                                   'va': int(m.group(5), 16)}
                 for m in RESOURCE_LINE.finditer(text)}
    src_handle = int(pass_match.group(5))
    dst_handle = int(pass_match.group(6))
    result = {'resources_seen': len(resources), 'src_handle': src_handle, 'dst_handle': dst_handle}
    if src_handle not in resources or dst_handle not in resources:
        result['status'] = 'fail'
        result['error'] = 'guest handles not found in the kernel resource log'
        return result
    for name, handle in (('src', src_handle), ('dst', dst_handle)):
        entry = resources[handle]
        if entry['bytes'] != TEST_BYTES:
            result['status'] = 'fail'
            result['error'] = f'{name} resource is {entry["bytes"]} bytes, expected {TEST_BYTES}'
            return result
        dump = output / f'{name}.bin'
        qmp.call('pmemsave', {'val': entry['phys'], 'size': TEST_BYTES, 'filename': str(dump)})
        words = struct.unpack(f'<{TEST_BYTES // 4}I', dump.read_bytes())
        result[name] = {'phys': entry['phys'], 'va': entry['va'], 'slot': entry['slot'],
                        'sha256': digest(dump), 'first_words': [f'0x{w:08x}' for w in words[:4]]}
        if name == 'src':
            mismatches = sum(1 for i, w in enumerate(words) if w != PATTERN_BASE + i)
            result['src']['pattern_mismatches'] = mismatches
        else:
            mismatches = sum(1 for w in words[1:] if w != FILL_VALUE)
            result['dst']['fill_mismatches'] = mismatches
            result['dst']['store_word_ok'] = words[0] == STORE_VALUE
    ok = (result['src']['pattern_mismatches'] == 0 and result['dst']['fill_mismatches'] == 0
          and result['dst']['store_word_ok'])
    result['status'] = 'pass' if ok else 'fail'
    if not ok:
        result['error'] = 'guest memory differs from the independent expectation'
    return result


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    image = args.image.resolve()
    run_image = output / 'guest.img'
    subprocess.run(['cp', '--reflink=auto', '--sparse=always', str(image), str(run_image)], check=True)
    variables = output / 'OVMF_VARS.fd'
    shutil.copyfile(args.variables, variables)
    debug = output / 'guest.log'
    qemu_log = output / 'qemu.log'
    qmp_path = output / 'qmp.sock'
    command = [args.qemu, '-machine', 'pc,accel=kvm,memory-backend=memory',
               '-cpu', 'host', '-m', '1024', '-smp', '2',
               '-object', 'memory-backend-memfd,id=memory,size=1G,share=on',
               '-drive', f'if=pflash,format=raw,readonly=on,file={args.firmware}',
               '-drive', f'if=pflash,format=raw,file={variables}',
               '-drive', f'file={run_image},format=raw,if=ide,index=0',
               '-vga', 'std', '-display', 'none',
               '-device', f'vfio-pci,host={args.host_device}',
               '-qmp', f'unix:{qmp_path},server=on,wait=off',
               '-monitor', 'none', '-serial', 'none', '-nic', 'none',
               '-debugcon', f'file:{debug}', '-no-reboot']
    report = {'started': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'image_sha256': digest(run_image), 'source_image_sha256': digest(image), 'argv': command,
              'mode': 'hang' if args.hang else ('test' if args.test else 'boot-only'), 'status': 'running',
              'harness_sha256': digest(__file__), 'transport_harness_sha256': digest(HERE / 'venus-qemu.py'),
              'firmware_sha256': digest(args.firmware), 'host_device': args.host_device,
              'console_address': args.console_address, 'console_size': args.console_size,
              'capture_method': 'debugcon guest.log + QMP pmemsave of vt_history and test resources',
              'boot_surface_available': False,
              'boot_surface_note': 'standard VGA only supplies the UEFI GOP the loader requires; the IGD is UPT passthrough with no scanout'}
    report_path = output / 'result.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    if report['image_sha256'] != report['source_image_sha256']:
        raise RuntimeError('source image changed while copying')
    started = time.monotonic()
    qmp = None
    process = None
    previous_term = signal.signal(signal.SIGTERM, venus_qemu.interrupt_termination)
    try:
        with qemu_log.open('wb') as log, (output / 'qmp.jsonl').open('w') as qlog:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            qmp = venus_qemu.QMP(qmp_path, qlog)
            report['pci'] = qmp.call('query-pci')
            report['qemu_version'] = qmp.call('query-version')
            deadline = time.monotonic() + args.timeout
            text = ''
            while time.monotonic() < deadline:
                text = venus_qemu.guest_text(debug)
                if 'boot: starting init' in text:
                    text += venus_qemu.console_text(qmp, output, args)
                if re.search(r'root@[^\r\n]*\$ ', text):
                    break
                if process.poll() is not None:
                    raise RuntimeError('QEMU exited during boot')
                if re.search(r'kernel panic|amd64 fault v=|VFS initialization failed|Open configured kernel:', text):
                    raise RuntimeError('guest boot failure (see guest.log)')
                time.sleep(0.1)
            else:
                report['i915'] = driver_state(venus_qemu.guest_text(debug))
                raise TimeoutError('guest shell prompt')
            report['i915'] = driver_state(venus_qemu.guest_text(debug))
            if report['i915']['attach_stopped'] is not None:
                raise RuntimeError(f'i915 attach stopped at {report["i915"]["attach_stopped"]["stage"]}')
            if report['i915']['selftest'] is None or report['i915']['selftest']['store'] != 'ok':
                raise RuntimeError('i915 selftest did not report store=ok')
            if not report['i915']['registered']:
                raise RuntimeError('i915 did not register the GPU node')
            if args.hang:
                baseline = debug.stat().st_size
                guest_command = '/bin/gpu-i915-test --hang\n'
                report['guest_command'] = guest_command.rstrip()
                qmp.text(guest_command)
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    text = venus_qemu.guest_text(debug, baseline) + venus_qemu.console_text(qmp, output, args)
                    hang = HANG_LINE.search(text)
                    if hang:
                        report['hang_result'] = {'line': hang.group(0), 'wait_errno': int(hang.group(1)),
                                                'wait_status': int(hang.group(2)), 'peer': hang.group(3),
                                                'isolation_peer_ok': PEER_OK_LINE.search(text) is not None}
                        break
                    if process.poll() is not None:
                        raise RuntimeError('QEMU exited during the hang test')
                    time.sleep(0.1)
                else:
                    raise TimeoutError('guest hang test result line')
                report['i915_final'] = driver_state(venus_qemu.guest_text(debug))
                if not report['hang_result']['isolation_peer_ok']:
                    raise RuntimeError('peer session did not continue after isolation')
                report['status'] = 'pass'
            elif not args.test:
                report['status'] = 'boot-pass'
            else:
                baseline = debug.stat().st_size
                guest_command = '/bin/gpu-i915-test\n'
                report['guest_command'] = guest_command.rstrip()
                qmp.text(guest_command)
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    text = venus_qemu.guest_text(debug, baseline) + venus_qemu.console_text(qmp, output, args)
                    passed = PASS_LINE.search(text)
                    failed = FAIL_LINE.search(text)
                    if passed:
                        report['guest_result'] = {'line': passed.group(0), 'copy': int(passed.group(1)),
                                                  'fill': int(passed.group(2)), 'store': int(passed.group(3)),
                                                  'job': int(passed.group(4))}
                        break
                    if failed:
                        report['guest_result'] = {'line': failed.group(0), 'stage': failed.group(1),
                                                  'errno': int(failed.group(2))}
                        raise RuntimeError(f'guest test failed at {failed.group(1)}')
                    if process.poll() is not None:
                        raise RuntimeError('QEMU exited during the guest test')
                    time.sleep(0.1)
                else:
                    raise TimeoutError('guest test result line')
                report['memory_check'] = memory_check(qmp, output, venus_qemu.guest_text(debug), passed)
                report['i915_final'] = driver_state(venus_qemu.guest_text(debug))
                if report['memory_check']['status'] != 'pass':
                    raise RuntimeError(report['memory_check'].get('error', 'memory check failed'))
                report['status'] = 'pass'
            qmp.call('quit')
            qmp.close()
            qmp = None
            report['qemu_exit_code'] = process.wait(timeout=10)
            if report['qemu_exit_code'] != 0:
                raise RuntimeError('QEMU failed during shutdown')
    except (Exception, KeyboardInterrupt) as error:
        report.update(status='fail', error=str(error))
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if qmp is not None:
            try:
                qmp.close()
            except OSError:
                pass
        if process is not None:
            try:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                report['qemu_exit_code'] = process.returncode
            except Exception as cleanup_error:
                report['cleanup_error'] = str(cleanup_error)
                report['status'] = 'fail'
        if 'i915' not in report and debug.exists():
            report['i915'] = driver_state(venus_qemu.guest_text(debug))
        report['elapsed_seconds'] = round(time.monotonic() - started, 3)
        report['guest_log_sha256'] = digest(debug) if debug.exists() else None
        report_path.write_text(json.dumps(report, indent=2) + '\n')
        signal.signal(signal.SIGTERM, previous_term)
        if args.owner:
            uid, gid = (int(part) for part in args.owner.split(':'))
            for path in [output, *output.rglob('*')]:
                try:
                    os.chown(path, uid, gid)
                except OSError:
                    pass
    print(json.dumps({k: report.get(k) for k in ['status', 'mode', 'elapsed_seconds', 'error', 'i915']}, indent=2))
    return 0 if report['status'] in ('pass', 'boot-pass') else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--boot-only', action='store_true')
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--hang', action='store_true')
    parser.add_argument('--console-address', type=lambda text: int(text, 0))
    parser.add_argument('--console-size', type=int, default=32768)
    parser.add_argument('--host-device', default='0000:00:02.0')
    parser.add_argument('--owner', help='uid:gid that receives the output directory')
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--firmware', default='/usr/share/OVMF/OVMF_CODE_4M.fd')
    parser.add_argument('--variables', default='/usr/share/OVMF/OVMF_VARS_4M.fd')
    args = parser.parse_args()
    if sum((args.boot_only, args.test, args.hang)) != 1:
        parser.error('choose exactly one of --boot-only, --test and --hang')
    if not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]', args.host_device):
        parser.error('host-device must be a PCI address like 0000:00:02.0')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
