#!/usr/bin/env python3
# WS032: run commands on a booted zedBSD image and capture what it printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The console is a VGA text screen with a PS/2 keyboard, and the text console
# now mirrors everything it renders to the first serial port.  So output is
# read from the serial line, and input is delivered as key presses through
# QEMU's monitor -- the same path a person at the keyboard would use.
#
# usage: run-target-console.py --image <img> [--timeout N] <command>...
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

# The key names QEMU knows, for the characters a command line needs.
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
            raise RuntimeError('the monitor socket never appeared: %s' % path)
        self.stream = self.socket.makefile('rwb')
        self.stream.readline()          # greeting
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
                raise RuntimeError('the monitor closed the connection')
            message = json.loads(line)
            if 'return' in message or 'error' in message:
                if 'error' in message:
                    raise RuntimeError('monitor refused %s: %s'
                                       % (name, message['error']))
                return message['return']
            # Anything else is an asynchronous event; keep reading.

    def type(self, text):
        for character in text:
            keys = [{'type': 'qcode', 'data': name}
                    for name in key_sequence(character)]
            self.command('send-key', keys=keys)
            time.sleep(0.03)

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass


def wait_for(path, pattern, deadline, description):
    """Waits until the captured output matches, and returns all of it."""
    expression = re.compile(pattern)
    while time.time() < deadline:
        try:
            with open(path, 'rb') as handle:
                text = handle.read().decode('utf-8', 'replace')
        except FileNotFoundError:
            text = ''
        if expression.search(text):
            return text
        time.sleep(0.25)
    raise TimeoutError('%s did not appear within the time allowed' % description)


def count_prompts(path, expression, deadline, wanted, description):
    """Waits until the console has shown at least `wanted` prompts."""
    while time.time() < deadline:
        try:
            with open(path, 'rb') as handle:
                text = handle.read().decode('utf-8', 'replace')
        except FileNotFoundError:
            text = ''
        if len(expression.findall(text)) >= wanted:
            return len(expression.findall(text))
        time.sleep(0.25)
    raise TimeoutError('%s did not appear within the time allowed' % description)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    # The default processor model matters: zedBSD takes its entropy from
    # RDRAND, which the emulator's own default model does not provide.  "max"
    # has it without needing KVM; "host" also does, where KVM is available.
    parser.add_argument('--cpu', default='max',
                        help='processor model to emulate (default: max)')
    parser.add_argument('--timeout', type=float, default=180.0)
    parser.add_argument('--user', default='root')
    parser.add_argument('--output', help='where to keep the captured console')
    parser.add_argument('command', nargs='+')
    options = parser.parse_args(argv[1:])

    directory = tempfile.mkdtemp(prefix='zedbsd-console-')
    console = options.output or os.path.join(directory, 'console.log')
    monitor_path = os.path.join(directory, 'monitor.sock')
    deadline = time.time() + options.timeout

    emulator = subprocess.Popen(
        [options.qemu, '-machine', 'pc', '-m', '512', '-smp', '4']
        + (['-cpu', options.cpu] if options.cpu else [])
        + [
         '-drive', 'file=%s,format=raw,if=ide' % os.path.abspath(options.image),
         '-boot', 'c', '-display', 'none', '-no-reboot',
         '-serial', 'file:%s' % console,
         '-qmp', 'unix:%s,server,nowait' % monitor_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    status = 1
    monitor = None
    try:
        monitor = Monitor(monitor_path, deadline)
        wait_for(console, r'login:', deadline, 'the login prompt')
        monitor.type(options.user + '\n')

        # The account has no password, so the prompt is answered with an
        # empty line.  A configuration without one simply never asks.
        try:
            wait_for(console, r'[Pp]assword:',
                     min(deadline, time.time() + 20.0), 'the password prompt')
            monitor.type('\n')
        except TimeoutError:
            pass

        # The shell prints a prompt when it is ready for the next command.
        # Counting prompts is what separates one command from the next: a
        # marker typed straight after a command is echoed by the terminal
        # immediately, long before the command itself has finished.
        prompt = re.compile(r'[^\r\n]*[#$] ')
        seen = count_prompts(console, prompt, deadline, 1, 'the shell prompt')
        for position, command in enumerate(options.command):
            # A command answered by a ?pattern or @text that follows it is
            # still running when they are typed, so there is no prompt after
            # it yet to wait for.
            answered = (position + 1 < len(options.command) and
                        options.command[position + 1][:1] in '?@')
            # A command written as ?pattern waits for the console to show
            # it.  A program that asks a question of its own must be given
            # the chance to ask it: it flushes what was typed before the
            # question, so that a password typed ahead is never captured.
            if command.startswith('?'):
                wait_for(console, command[1:], deadline,
                         'the prompt %r' % command[1:])
                continue

            # A command written as @text is typed into whatever is already
            # running, for a program that asks a question of its own: its
            # prompt is not the shell's, so there is none to count.
            if command.startswith('@'):
                monitor.type(command[1:] + '\n')
                continue
            monitor.type(command + '\n')
            if answered:
                continue
            seen = count_prompts(console, prompt, deadline, seen + 1,
                                 'the end of %r' % command)
        status = 0
    except (TimeoutError, RuntimeError) as failure:
        print('run-target-console: %s' % failure, file=sys.stderr)
    finally:
        if monitor is not None:
            monitor.close()
        emulator.terminate()
        try:
            emulator.wait(timeout=10)
        except subprocess.TimeoutExpired:
            emulator.kill()

    try:
        with open(console, 'rb') as handle:
            sys.stdout.write(handle.read().decode('utf-8', 'replace'))
    except FileNotFoundError:
        print('run-target-console: nothing was captured', file=sys.stderr)
    print('\nrun-target-console: console kept at %s' % console, file=sys.stderr)
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv))
