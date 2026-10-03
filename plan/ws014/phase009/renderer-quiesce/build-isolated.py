#!/usr/bin/env python3
"""Build only two fresh private renderer prefixes from existing public source."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

DEPENDENCIES = Path('/home/awe/zedbsd-q306-venus/dependencies')
BASE = DEPENDENCIES / 'q311-strict/source'
INPUT = DEPENDENCIES / 'q312-quiesce-input'
MANIFEST = json.loads((INPUT / 'proposal.json').read_text())
PATCH_SHA = '04f75fad2ee92c1ba0b78bf8751efd08ed6d3503774cc1d0e0e0d0ebb1ed8cf9'
DELAY_SHA = '333295a1c9e5408cf4e00d274c9651d8a4a86b51d6091f46e08efdc4b94b51c5'
DELAY_SOURCE_SHA = '72b4c7d8d994017bbea550dc27d350cbc1e7692e302e8b9bfa5943f7b7de2368'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(argv, log, environment, cwd=None):
    started = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(argv, cwd=cwd, env=environment, stdout=output,
                                stderr=subprocess.STDOUT, timeout=600)
    print(json.dumps({'command': argv, 'exit': result.returncode,
                      'seconds': time.monotonic() - started, 'log': str(log)}), flush=True)
    if result.returncode:
        raise RuntimeError('isolated build step failed: ' + str(log))


def main():
    assert digest(INPUT / 'host-quiesce-proposal.patch') == PATCH_SHA
    assert digest(INPUT / 'strict-native-completion-delay.patch') == DELAY_SHA
    for name, expected in MANIFEST['host_base_sha256'].items():
        assert digest(BASE / name) == expected, name
    assert 'MIT license' in (BASE / 'COPYING').read_text()
    for name in ('q312-quiesce', 'q312-quiesce-delay'):
        assert not (DEPENDENCIES / name).exists(), 'refuse any existing prefix: ' + name
    environment = dict(os.environ)
    environment['PYTHONPATH'] = str(DEPENDENCIES / 'q310-opaque/python')
    for name, delayed in (('q312-quiesce', False), ('q312-quiesce-delay', True)):
        prefix = DEPENDENCIES / name
        source, build, install = prefix / 'source', prefix / 'build', prefix / 'install'
        prefix.mkdir()
        shutil.copytree(BASE, source, symlinks=True)
        execute(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(INPUT / 'host-quiesce-proposal.patch')],
                prefix / 'patch.log', environment, source)
        for relative, expected in MANIFEST['host_proposed_sha256'].items():
            assert digest(source / relative) == expected, relative
        if delayed:
            execute(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(INPUT / 'strict-native-completion-delay.patch')],
                    prefix / 'delay-patch.log', environment, source)
            assert digest(source / 'src/venus/vkr_queue.c') == DELAY_SOURCE_SHA
        execute(['python3', '-m', 'mesonbuild.mesonmain', 'setup', str(build), str(source),
                 '--prefix=' + str(install), '-Dvenus=true', '-Dplatforms=egl', '-Dtests=false',
                 '-Dvideo=false', '-Drender-server-worker=process', '-Dminigbm_allocation=false',
                 '-Dbuildtype=release'], prefix / 'setup.log', environment)
        execute(['ninja', '-C', str(build), '-j2'], prefix / 'build.log', environment)
        execute(['python3', '-m', 'mesonbuild.mesonmain', 'install', '-C', str(build), '--no-rebuild'],
                prefix / 'install.log', environment)
        options = json.loads((build / 'meson-info/intro-buildoptions.json').read_text())
        values = {item['name']: item['value'] for item in options}
        assert values['render-server-worker'] == 'process'
        assert values['venus'] is True
        library = install / 'lib/x86_64-linux-gnu/libvirglrenderer.so.1'
        server = install / 'libexec/virgl_render_server'
        report = {'prefix': str(prefix), 'flags': 7, 'worker': 'process', 'delay_fixture': delayed,
                  'source_sha256': {relative: digest(source / relative)
                                    for relative in MANIFEST['host_proposed_sha256']},
                  'queue_source_sha256': digest(source / 'src/venus/vkr_queue.c'),
                  'library': str(library), 'library_sha256': digest(library),
                  'server': str(server), 'server_sha256': digest(server),
                  'license_sha256': digest(source / 'COPYING'), 'pass': True,
                  'system_packages_display_vfio_physical_reset_modified': False}
        (prefix / 'provenance.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
