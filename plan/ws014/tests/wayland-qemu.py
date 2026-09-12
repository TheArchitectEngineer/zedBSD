#!/usr/bin/env python3
"""Verify real shared-GPU Wayland images in a finite QEMU/Venus session.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import time

from venus_rfb import capture as capture_rfb
import wayland_oracle as oracle

_spec = importlib.util.spec_from_file_location('venus_qemu', Path(__file__).with_name('venus-qemu.py'))
common = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(common)


def save(output, report):
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')


def exercise(args, qmp, output, debug, vnc_path, process, report):
    deadline = time.monotonic() + args.timeout
    baseline = debug.stat().st_size
    report.update(token=args.token, oracle_sha256=common.digest(Path(oracle.__file__)),
                  samples=[], commands=[], guest_completed=False)

    expected_failure = None
    seen = set()

    def console():
        if process.poll() is not None:
            raise RuntimeError('QEMU exited during Wayland testing')
        value = common.guest_text(debug, baseline) + common.console_text(qmp, output, args)
        fresh = [line for line in value.splitlines() if line and line not in seen]
        seen.update(fresh)
        with (output / 'wayland-observed.log').open('a') as stream:
            stream.write('\n'.join(fresh) + '\n')
        failures = re.findall(r'WLTEST FAILED run=([^ ]+)', value)
        if (re.search(r'kernel panic|amd64 fault v=|ZWL FAILED|ZWL IMPORT_ERROR', value) or
                any(token != expected_failure for token in failures)):
            raise RuntimeError('guest Wayland/GPU failure; inspect console and renderer logs')
        return value

    def command(value):
        report['commands'].append(value)
        qmp.text(value + '\n')

    def wait(pattern, description):
        matcher = re.compile(pattern)
        while time.monotonic() < deadline:
            found = matcher.search(console())
            if found:
                return found
            time.sleep(0.05)
        raise TimeoutError(description)

    command('/bin/gpu-share-test')
    wait(r'GPU SHARE PASS producer-exit SCM_RIGHTS independent-context import GPU-copy pixels=1024',
         'independent renderer import after producer exit')
    report['independent_renderer_import'] = True
    def start_compositor():
        old = {int(value) for value in re.findall(r'ZWL READY[^\r\n]*pid=(\d+)', console())}
        command('/bin/zwl --socket=/tmp/wayland-0 --timeout=150 &')
        while time.monotonic() < deadline:
            for marker in re.finditer(r'ZWL READY[^\r\n]*pid=(\d+)', console()):
                pid = int(marker.group(1))
                if pid > 1 and pid not in old:
                    report.setdefault('compositors', []).append({'pid': pid, 'ready': marker.group(0)})
                    return pid
            time.sleep(0.05)
        raise TimeoutError('fresh compositor startup')

    def stop_compositor(pid):
        command(f'kill {pid}')
        wait(r'ZWL EXIT frames=\d+ error=0 cleanup_failed=0 pid=' + str(pid) + r'\b',
             'compositor lease-safe shutdown')

    def ordinary(suffix):
        token = args.token + suffix
        command(f'/bin/wltest --display=/tmp/wayland-0 --frames=6 --token={token}')
        wait(r'WLTEST DONE run=' + re.escape(token) + r' frames=6', 'ordinary restarted client')
        return token

    compositor_pid = start_compositor()
    for mode in ('fifo', 'mailbox'):
        token = args.token + '-' + mode
        command(f'/bin/wltest --display=/tmp/wayland-0 --verify-session --mode={mode} --recreate-at=4 --token={token}')
        for frame in range(1, 7):
            marker = wait(r'WLTEST FRAME run=' + re.escape(token) + r' frame=' + str(frame) +
                          r' width=(\d+) height=(\d+) mode=' + mode,
                          f'{mode} frame {frame}')
            width, height = map(int, marker.groups())
            number = len(report['samples']) + 1
            shot = output / f'frame-{number}.ppm'
            capture_deadline = min(deadline, time.monotonic() + 15)
            mismatch = None
            while time.monotonic() < capture_deadline:
                info = capture_rfb(vnc_path, shot, min(5, capture_deadline - time.monotonic()))
                try:
                    check = oracle.verify(shot, frame, width, height)
                    mismatch = check
                    if check['passed']:
                        report['samples'].append({'mode': mode, 'frame': frame, 'width': width,
                                                  'height': height, 'oracle': check, 'capture': info,
                                                  'frame_sha256': common.digest(shot)})
                        (output / f'oracle-{number}.json').write_text(json.dumps(check, indent=2) + '\n')
                        save(output, report)
                        break
                except ValueError as error:
                    mismatch = str(error)
                time.sleep(0.05)
            else:
                raise TimeoutError(f'{mode} frame {frame} capture: {mismatch}')
            qmp.text('next\n')
        wait(r'WLTEST DONE run=' + re.escape(token) + r' frames=6', f'{mode} ordinary cleanup')
        report[mode + '_completed'] = True
        wait(r'WLTEST RECREATE run=' + re.escape(token) + r' before=4', f'{mode} oldSwapchain replacement')
    report['guest_completed'] = True
    # Correlate every observed compositor presentation with its imported K resource.
    text = '\n'.join(sorted(seen))
    imports = {(int(client), int(buffer), int(resource)) for client, buffer, resource in
               re.findall(r'ZWL IMPORT client=(\d+) buffer=(\d+) gpu_fd=\d+ resource=(\d+)', text)}
    presents = list(re.finditer(r'ZWL PRESENT client=(\d+) surface=\d+ buffer=(\d+) resource=(\d+) '
                               r'frame=(\d+) sequence=(\d+) width=(\d+) height=(\d+) flags=(\d+) refresh=(\d+)', text))
    report['shared_resources'] = [dict(zip(('client', 'buffer', 'resource', 'frame', 'sequence',
                                          'width', 'height', 'flags', 'refresh'), map(int, item.groups())))
                                  for item in presents]
    report['shared_gpu_path'] = (len(presents) == 12 and
        all(tuple(map(int, item.groups()[:3])) in imports and int(item.group(8)) & 2
            for item in presents))
    if not report['shared_gpu_path']:
        raise RuntimeError('compositor did not prove matching imported resources and BLOB presentation')

    if args.lifecycle:
        lifecycle = {'client_aborted': False, 'client_reopened': False,
                     'compositor_reopened': False, 'compositor_killed': False,
                     'surface_loss': False, 'console_restored': False}
        report['lifecycle'] = lifecycle
        token = args.token + '-abort'
        command(f'/bin/wltest --display=/tmp/wayland-0 --verify-session --token={token}')
        wait(r'WLTEST FRAME run=' + re.escape(token) + r' frame=1 ', 'held client frame before SIGINT')
        qmp.call('human-monitor-command', {'command-line': 'sendkey ctrl-c 1'})
        wait(r'WLTEST FRAME run=' + re.escape(token) + r' frame=1 [\s\S]*root@[^\r\n]*\$ ',
             'SIGINT returns the foreground terminal')
        if 'WLTEST DONE run=' + token in console():
            raise RuntimeError('interrupted client unexpectedly completed normally')
        lifecycle['client_aborted'] = True
        lifecycle['client_reopen_token'] = ordinary('-after-abort')
        lifecycle['client_reopened'] = True
        stop_compositor(compositor_pid)
        compositor_pid = start_compositor()
        lifecycle['compositor_reopened'] = True

        # SIGKILL exercises K fd teardown without compositor user-space cleanup.
        token = args.token + '-lost'
        expected_failure = token
        command(f'/bin/wltest --display=/tmp/wayland-0 --frames=600 --delay-ms=100 --token={token} &')
        wait(r'WLTEST FRAME run=' + re.escape(token) + r' frame=1 ', 'client before compositor SIGKILL')
        command(f'kill -9 {compositor_pid}')
        failure = wait(r'WLTEST FAILED run=' + re.escape(token) + r'[^\r\n]+',
                       'client observes compositor disconnection')
        details = re.search(r'api=(\S+) result=(-?\d+) cleanup=(-?\d+) errno=(\d+) frames=(\d+)',
                            failure.group(0))
        if details is None or int(details.group(3)) != 0 or int(details.group(5)) < 1:
            raise RuntimeError('disconnected client did not release its own GPU resources')
        native_loss = details.group(1) == 'wltest_window_dispatch' and int(details.group(2)) == 0
        vulkan_loss = details.group(1).startswith('vk') and int(details.group(2)) == -1000000000
        if not native_loss and not vulkan_loss:
            raise RuntimeError('client failure does not prove native disconnection or Vulkan surface loss')
        lifecycle['compositor_killed'] = True
        lifecycle['surface_loss'] = True
        lifecycle['surface_loss_marker'] = failure.group(0)
        killed = output / 'console-killed.ppm'
        capture_rfb(vnc_path, killed, 5)
        if oracle.read_ppm(killed)[:2] != (640, 480):
            raise RuntimeError('SIGKILL did not restore the native console scanout')
        lifecycle['killed_console_sha256'] = common.digest(killed)
        lifecycle['killed_console_restored'] = True
        # The disposable guest and this process identity prove ownership of this stale endpoint.
        command('/bin/rm /tmp/wayland-0')
        wait(r'/bin/rm /tmp/wayland-0[\s\S]*root@[^\r\n]*\$ ',
             'stale endpoint removal returns the shell prompt')
        compositor_pid = start_compositor()
        lifecycle['after_kill_token'] = ordinary('-after-kill')
        lifecycle['after_kill_reopened'] = True

    stop_compositor(compositor_pid)
    report['compositor_stopped'] = True
    if args.lifecycle:
        before, after = output / 'console-return.ppm', output / 'console-write.ppm'
        capture_rfb(vnc_path, before, 5)
        command('echo wayland-console-restoration')
        time.sleep(0.30)
        capture_rfb(vnc_path, after, 5)
        width, height, pixels = oracle.read_ppm(before)
        final_width, final_height, final_pixels = oracle.read_ppm(after)
        lifecycle['console_before_sha256'] = common.digest(before)
        lifecycle['console_after_sha256'] = common.digest(after)
        lifecycle['console_restored'] = ((width, height) == (640, 480) and
            (final_width, final_height) == (640, 480) and pixels != final_pixels)
        if not lifecycle['console_restored']:
            raise RuntimeError('console was not visibly restored and updated after compositor shutdown')
    report['status'] = 'pass'
    save(output, report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render-server', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--token', required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--lifecycle', action='store_true')
    parser.add_argument('--console-address', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--console-size', type=int, default=32768)
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--icd', default='/usr/share/vulkan/icd.d/intel_icd.json')
    parser.add_argument('--firmware', default='/usr/share/OVMF/OVMF_CODE_4M.fd')
    parser.add_argument('--variables', default='/usr/share/OVMF/OVMF_VARS_4M.fd')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]{1,43}', args.token):
        parser.error('token must be a short lowercase identifier')
    if not 1 <= args.timeout <= 600 or not 1 <= args.console_size <= 1048576:
        parser.error('bounded timeout or console size is invalid')
    if not 0 <= args.console_address < 1024 ** 3 - args.console_size:
        parser.error('console capture lies outside guest memory')
    args.phase, args.frame, args.boot_only = 'wayland', 0, False
    return common.run(args, exercise=exercise, harness_path=__file__)


if __name__ == '__main__':
    sys.exit(main())
