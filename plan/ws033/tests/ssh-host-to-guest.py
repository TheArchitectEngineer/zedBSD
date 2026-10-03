#!/usr/bin/env python3
"""Drives the guest console, then connects to it from the host.

The guest is set up over its serial console, because that is the only way
in before the network works.  What it is set up for is the network: an
address from QEMU's user-mode DHCP server, the host's public key in
root's authorized_keys, and the server listening.  Then the console is put
down and the rest is done the way anything else would do it, through ssh.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time

# The key names QEMU knows, for the characters these commands need.
KEYS = {
    ' ': 'spc', '-': 'minus', '.': 'dot', '/': 'slash', ',': 'comma',
    '=': 'equal', ';': 'semicolon', "'": 'apostrophe', '[': 'bracket_left',
    ']': 'bracket_right', '\\': 'backslash', '`': 'grave_accent',
    '\n': 'ret',
}
SHIFTED = {
    ':': 'semicolon', '_': 'minus', '"': 'apostrophe', '|': 'backslash',
    '<': 'comma', '>': 'dot', '?': 'slash', '+': 'equal', '~': 'grave_accent',
    '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7',
    '*': '8', '(': '9', ')': '0', '{': 'bracket_left', '}': 'bracket_right',
}


class TimeoutError_(Exception):
    pass


def key_sequence(character):
    """Returns the QEMU key names that produce one character."""
    if character.islower() or character.isdigit():
        return [character]
    if character.isupper():
        return ['shift', character.lower()]
    if character in KEYS:
        return [KEYS[character]]
    if character in SHIFTED:
        return ['shift', SHIFTED[character]]
    raise ValueError('no key for character %r' % character)


class Monitor:
    """A QMP connection to the running emulator."""

    def __init__(self, path, deadline):
        self.socket = None
        while time.time() < deadline:
            try:
                self.socket = socket.socket(socket.AF_UNIX)
                self.socket.connect(path)
                break
            except OSError:
                self.socket.close()
                self.socket = None
                time.sleep(0.2)
        if self.socket is None:
            raise TimeoutError_('the monitor never accepted a connection')
        self.stream = self.socket.makefile('rwb')
        self.stream.readline()
        self.command('qmp_capabilities')

    def command(self, name, **arguments):
        request = {'execute': name}
        if arguments:
            request['arguments'] = arguments
        self.stream.write((json.dumps(request) + '\n').encode())
        self.stream.flush()
        while True:
            line = self.stream.readline()
            if not line:
                raise TimeoutError_('the monitor closed')
            answer = json.loads(line)
            if 'return' in answer or 'error' in answer:
                return answer

    def type(self, text):
        """Types one line, a key at a time."""
        for character in text:
            names = key_sequence(character)
            self.command('send-key', keys=[
                {'type': 'qcode', 'data': name} for name in names])
            time.sleep(0.012)

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass


def read_console(path):
    try:
        with open(path, 'rb') as handle:
            return handle.read().decode('utf-8', 'replace')
    except FileNotFoundError:
        return ''


def wait_for(path, pattern, deadline, what):
    expression = re.compile(pattern)
    while time.time() < deadline:
        if expression.search(read_console(path)):
            return
        time.sleep(0.25)
    raise TimeoutError_('timed out waiting for %s' % what)


def count_prompts(path, pattern, deadline, wanted, what):
    while time.time() < deadline:
        if len(pattern.findall(read_console(path))) >= wanted:
            return wanted
        time.sleep(0.25)
    raise TimeoutError_('timed out waiting for %s' % what)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--monitor', required=True)
    parser.add_argument('--console', required=True)
    parser.add_argument('--key', required=True)
    parser.add_argument('--interface', default='ue0')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--timeout', type=float, default=300.0)
    parser.add_argument('--user', default='root')
    options = parser.parse_args(argv[1:])

    deadline = time.time() + options.timeout
    public = open(options.key + '.pub').read().strip()
    status = 1
    monitor = None
    try:
        monitor = Monitor(options.monitor, deadline)
        wait_for(options.console, r'login:', deadline, 'the login prompt')
        monitor.type(options.user + '\n')
        try:
            wait_for(options.console, r'[Pp]assword:',
                     min(deadline, time.time() + 20.0), 'the password prompt')
            monitor.type('\n')
        except TimeoutError_:
            pass

        prompt = re.compile(r'[^\r\n]*[#$] ')
        seen = count_prompts(options.console, prompt, deadline, 1,
                             'the shell prompt')

        # Everything the guest needs, over the console: an address, the key
        # the host will offer, and a server listening on that address.
        commands = [
            'net up ' + options.interface,
            'net dhcp ' + options.interface + ' --timeout=20',
            'net show ' + options.interface,
            'mkdir -p /root/.ssh',
            'chmod 700 /root/.ssh',
            "printf '%s\\n' '" + public + "' > /root/.ssh/authorized_keys",
            'chmod 600 /root/.ssh/authorized_keys',
            'wc -l /root/.ssh/authorized_keys',
            'service status sshd',
            'echo GUEST-READY',
        ]
        for command in commands:
            monitor.type(command + '\n')
            seen = count_prompts(options.console, prompt, deadline, seen + 1,
                                 'the end of %r' % command)
        wait_for(options.console, r'GUEST-READY', deadline,
                 'the guest to report itself ready')

        # The guest must have an address that is not a link-local one, or
        # there is nothing for the host to connect to.
        console = read_console(options.console)
        if re.search(r'1 /root/\.ssh/authorized_keys', console) is None:
            raise RuntimeError('the key did not arrive in the guest whole')
        match = re.search(options.interface +
                          r' +(unconfigured|static|dhcp)[^\r\n]*',
                          console)
        print('guest interface: %s' % (match.group(0) if match else '?'))
        if match is None:
            raise RuntimeError('the guest never reported %s at all'
                               % options.interface)
        if 'unconfigured' in match.group(0) or '169.254.' in match.group(0):
            raise RuntimeError('the guest was given no usable address: %s'
                               % match.group(0).strip())

        # From here it is an ordinary ssh session, from outside the guest.
        arguments = [
            'ssh', '-tt', '-p', str(options.port),
            '-i', options.key,
            '-o', 'StrictHostKeyChecking=no',
            '-o', 'UserKnownHostsFile=/dev/null',
            '-o', 'IdentitiesOnly=yes',
            '-o', 'ConnectTimeout=10',
            '-o', 'LogLevel=ERROR',
            '%s@127.0.0.1' % options.user, 'uname -a',
        ]
        print('host: %s' % ' '.join(arguments))
        attempt = 0
        result = None
        while time.time() < deadline:
            attempt += 1
            result = subprocess.run(arguments, capture_output=True, text=True,
                                    timeout=60)
            if result.returncode == 0:
                break
            time.sleep(3.0)
        if result is None or result.returncode != 0:
            print('ssh failed after %d attempts' % attempt, file=sys.stderr)
            if result is not None:
                print(result.stdout, file=sys.stderr)
                print(result.stderr, file=sys.stderr)
            raise RuntimeError('the host could not reach the guest')

        output = result.stdout.strip()
        print('guest said: %s' % output)

        # uname -a names the system, the host it ran on and the machine.
        if 'zedBSD' not in output:
            raise RuntimeError('uname did not name zedBSD: %r' % output)
        print('SSH-HOST-TO-GUEST-VERIFIED')
        status = 0
    except (TimeoutError_, RuntimeError, ValueError) as failure:
        print('ssh-host-to-guest: %s' % failure, file=sys.stderr)
    finally:
        if monitor is not None:
            monitor.close()

    sys.stdout.write(read_console(options.console))
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv))
