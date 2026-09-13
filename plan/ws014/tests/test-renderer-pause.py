#!/usr/bin/env python3
"""Bounded renderer identity/cleanup fixtures using owned children and fake I/O.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import copy
import importlib.util
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


harness = module('wayland_fault_fixture', 'wayland-qemu.py')
wrapper = module('venus_fault_wrapper', 'run-venus-remote.py')


class OwnedTree:
    def __enter__(self):
        code = ('import json,subprocess,sys,time; '
                'workers=[subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"]) '
                'for _ in range(3)]; '
                'print(json.dumps([worker.pid for worker in workers]),flush=True); time.sleep(60)')
        self.process = subprocess.Popen([sys.executable, '-c', code], stdout=subprocess.PIPE,
                                        text=True, start_new_session=True)
        ready, _, _ = select.select([self.process.stdout], [], [], 5)
        if not ready:
            self.__exit__(None, None, None)
            raise TimeoutError('owned fixture children did not start')
        self.pids = json.loads(self.process.stdout.readline())
        return self

    def __exit__(self, *unused):
        # This new session contains only the processes created by this fixture.
        for signum in (signal.SIGCONT, signal.SIGTERM):
            try:
                os.killpg(self.process.pid, signum)
            except ProcessLookupError:
                pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait(timeout=5)
        self.process.stdout.close()


def fake_pause():
    value = object.__new__(harness.RendererPause)
    value.qemu_pid = 10
    value.executable = Path('/owned/renderer')
    value.report = {}
    value.handles = []
    value.root_identity = (1, 1, '/owned/qemu', 1, 1)
    return value


class RendererPauseTests(unittest.TestCase):
    def test_real_owned_children_stop_and_resume(self):
        with OwnedTree() as tree:
            report = {}
            pause = harness.RendererPause(tree.process.pid, Path(sys.executable), report)
            try:
                pause.pause()
                self.assertEqual(set(report['renderer_pause']['pids']), set(tree.pids))
                self.assertNotIn(tree.process.pid, report['renderer_pause']['pids'])
                self.assertEqual(len(report['renderer_pause']['identities']), 3)
            finally:
                pause.resume()
            self.assertTrue(report['renderer_pause']['resumed'])
            self.assertEqual(pause.handles, [])

    def test_reused_pid_or_changed_ancestry_is_never_signaled(self):
        before = (2, 10, '/owned/renderer', 1, 7)
        after = (3, 99, '/owned/renderer', 1, 7)
        for changed, owned, alive in ((True, True, True), (False, False, True),
                                      (False, True, False)):
            with self.subTest(changed=changed, owned=owned, alive=alive):
                pause = fake_pause()
                snapshots = [before, after if changed else before]
                with mock.patch.object(pause, 'descendants', return_value=[11]), \
                     mock.patch.object(pause, 'identity', side_effect=snapshots), \
                     mock.patch.object(pause, 'owned', return_value=owned), \
                     mock.patch.object(pause, 'alive', return_value=alive), \
                     mock.patch.object(harness.os, 'pidfd_open', return_value=71), \
                     mock.patch.object(harness.os, 'close') as close, \
                     mock.patch.object(harness.signal, 'pidfd_send_signal') as send:
                    with self.assertRaises(RuntimeError):
                        pause.pause()
                    send.assert_not_called()
                    close.assert_called_once_with(71)
                self.assertEqual(pause.handles, [])

    def test_resume_attempts_every_handle_and_never_clears_a_failure(self):
        pause = fake_pause()
        pause.report['renderer_pause'] = {'stopped': True, 'resumed': False}
        pause.handles = [(11, 71, ()), (12, 72, ()), (13, 73, ())]
        with mock.patch.object(harness.signal, 'pidfd_send_signal',
                               side_effect=[PermissionError('fixture'), None, ProcessLookupError()]) as send, \
             mock.patch.object(harness.os, 'close') as close:
            with self.assertRaisesRegex(RuntimeError, 'renderer cleanup failed'):
                pause.resume()
            self.assertEqual(send.call_count, 3)
            self.assertEqual(close.call_count, 3)
        self.assertFalse(pause.report['renderer_pause']['resumed'])
        pause.resume()
        self.assertFalse(pause.report['renderer_pause']['resumed'])

    def test_term_unwinds_actual_common_run_and_resumes_our_children(self):
        with OwnedTree() as tree, tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image, firmware, variables = [root / name for name in ('image', 'firmware', 'variables')]
            for path in (image, firmware, variables):
                path.write_bytes(b'fixture')
            args = SimpleNamespace(output=root / 'capture', image=image, variables=variables,
                                   firmware=firmware, qemu='owned-fixture', icd='unused',
                                   render_server=Path(sys.executable), renderer_library_dir=None,
                                   phase='wayland', frame=0, console_address=0, console_size=1,
                                   timeout=5, boot_only=False)
            qmp = SimpleNamespace(call=lambda *unused: {}, close=lambda: None)
            completed = []

            def exercise(args, qmp, output, debug, vnc, process, report):
                pause = harness.RendererPause(process.pid, args.render_server, report)
                try:
                    pause.pause()
                    os.kill(os.getpid(), signal.SIGTERM)
                finally:
                    pause.resume()
                    completed.append(report['renderer_pause']['resumed'])

            previous = signal.getsignal(signal.SIGTERM)
            with mock.patch.object(harness.common, 'QMP', return_value=qmp), \
                 mock.patch.object(harness.common, 'guest_text', return_value='root@fixture$ '), \
                 mock.patch.object(harness.common.subprocess, 'Popen', return_value=tree.process), \
                 mock.patch.object(harness.common.subprocess, 'run',
                                   side_effect=lambda *a, **k: (args.output / 'guest.img').write_bytes(b'fixture')):
                result = harness.common.run(args, exercise=exercise)
            self.assertEqual(result, 1)
            self.assertEqual(completed, [True])
            self.assertEqual(signal.getsignal(signal.SIGTERM), previous)
            report = json.loads((args.output / 'result.json').read_text())
            self.assertEqual(report['status'], 'fail')
            self.assertIn('SIGTERM', report['error'])
            self.assertTrue(report['renderer_pause']['resumed'])


class FaultAcceptanceTests(unittest.TestCase):
    def test_fault_identity_and_failed_attempts_are_not_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence = root / 'attempt/evidence'
            evidence.mkdir(parents=True)
            marker = 'GPUFENCE PRODUCER_EXIT_ERROR PASS'
            (evidence / 'wayland-observed.log').write_text(marker + '\nroot@fixture$ ')
            sha = 'a' * 64
            args = SimpleNamespace(profile='wayland', phase='wayland', frame=0, boot_only=False,
                                   render_server='/server', renderer_library_dir=None,
                                   fault_test='producer-exit', token='fixture',
                                   output_root=root, attempt='attempt')
            local = {'remote_exit_code': 0, 'console': {'physical_address': 1, 'bytes': 32768},
                     'disposable_image': {'sha256': sha},
                     'artifacts': {key: {'sha256': sha}
                                   for key in ('harness', 'rfb_client', 'transport_harness')},
                     'fetched_evidence': {name: sha for name in wrapper.EVIDENCE_FILES}}
            remote = {'status': 'pass', 'qemu_exit_code': 0, 'phase': 'wayland', 'frame': 0,
                      'console_address': 1, 'console_size': 32768, 'image_sha256': sha,
                      'source_image_sha256': sha, 'environment': {'RENDER_SERVER_EXEC_PATH': '/server'},
                      'render_server_sha256': sha, 'harness_sha256': sha, 'rfb_client_sha256': sha,
                      'transport_harness_sha256': sha, 'boot_surface_available': False,
                      'guest_log_sha256': sha, 'renderer_log_sha256': sha,
                      'token': 'fixture', 'fault_test': 'producer-exit',
                      'guest_completed': True, 'fault_marker': marker}
            self.assertEqual(wrapper.verify_remote_result(args, local, remote), 'pass')
            for field, value in (('transport_harness_sha256', 'b' * 64), ('status', 'fail'),
                                 ('qemu_exit_code', 1), ('guest_completed', False),
                                 ('fault_test', 'recovery'), ('token', 'another')):
                with self.subTest(field=field), self.assertRaises(RuntimeError):
                    wrapper.verify_remote_result(args, local, dict(remote, **{field: value}))
            failed = copy.deepcopy(local)
            failed['remote_exit_code'] = 1
            with self.assertRaises(RuntimeError):
                wrapper.verify_remote_result(args, failed, remote)


if __name__ == '__main__':
    unittest.main(verbosity=2)
