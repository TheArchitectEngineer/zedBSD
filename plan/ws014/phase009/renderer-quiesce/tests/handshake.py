#!/usr/bin/env python3
"""Probe only a private renderer INIT socket; create no GPU contexts."""

import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys

prefix = Path(sys.argv[1])
assert prefix in [Path('/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce/install'), Path('/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce-delay/install')]
server = next(prefix.rglob('virgl_render_server'))
library = next(prefix.rglob('libvirglrenderer.so.1.9.0'))
environment = dict(os.environ, LD_LIBRARY_PATH=str(library.parent))
cases = [('valid_quiesce', struct.pack('<IIII', 1, 0, 0x5a424453, 7), struct.pack('<II', 0x5a424453, 7)),
         ('wrong_magic', struct.pack('<IIII', 1, 0, 0x12345678, 3), b''),
         ('old_opaque_only', struct.pack('<IIII', 1, 0, 0x5a424453, 1), b''),
         ('old_strict_only', struct.pack('<IIII', 1, 0, 0x5a424453, 3), b''),
         ('unknown_flags', struct.pack('<IIII', 1, 0, 0x5a424453, 15), b''),
         ('stock_request_size', struct.pack('<II', 1, 0), b'')]
results = []
for name, request, expected in cases:
    local, remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    local.settimeout(2)
    process = subprocess.Popen([str(server), '--socket-fd', str(remote.fileno())],
                               pass_fds=(remote.fileno(),), env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    remote.close()
    try:
        local.sendall(request)
        actual = local.recv(64)
        passed = actual == expected
        result = {'case': name, 'pass': passed, 'reply_hex': actual.hex()}
    except socket.timeout:
        result = {'case': name, 'pass': False, 'timeout_seconds': 2}
    finally:
        local.close()
        try:
            output, errors = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            output, errors = process.communicate(timeout=3)
    result['server_exit'] = process.returncode
    result['server_stderr'] = errors.decode(errors='replace')
    results.append(result)
print(json.dumps({'prefix': str(prefix), 'protocol_only': True, 'contexts_created': 0, 'results': results}, indent=2))
sys.exit(0 if all(result['pass'] for result in results) else 1)
