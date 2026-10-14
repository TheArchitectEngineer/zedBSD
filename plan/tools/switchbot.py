#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Turns a SwitchBot plug on or off through the SwitchBot cloud API (v1.1),
# so Q1 can power-cycle a test machine that stopped answering (2026-10-16
# user: "これを使って、電源のON/OFFができます。…あなたにON/OFFを委ねることで、
# 自動再起動が可能になります。").  Only Q1 runs it; agents ask Q1.
#
# The token and secret are read from ~/.config/switchbot/credentials
# (SWITCHBOT_TOKEN=..., SWITCHBOT_SECRET=..., mode 600), never from the tree.
#
#   switchbot.py list                   the devices: id, type, name
#   switchbot.py status DEVICE_ID       the plug's state
#   switchbot.py on|off DEVICE_ID       one command
#   switchbot.py cycle DEVICE_ID [SEC]  off, wait SEC (default 10), on

import base64
import hashlib
import hmac
import json
import pathlib
import sys
import time
import urllib.request
import uuid

API = 'https://api.switch-bot.com/v1.1'


def credentials():
    values = {}
    path = pathlib.Path.home() / '.config' / 'switchbot' / 'credentials'
    for line in path.read_text().splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            values[key.strip()] = value.strip()
    return values['SWITCHBOT_TOKEN'], values['SWITCHBOT_SECRET']


def request(method, path, body=None):
    token, secret = credentials()
    stamp = str(int(time.time() * 1000))
    nonce = str(uuid.uuid4())
    digest = hmac.new(secret.encode(), (token + stamp + nonce).encode(), hashlib.sha256).digest()
    headers = {
        'Authorization': token,
        'sign': base64.b64encode(digest).decode(),
        't': stamp,
        'nonce': nonce,
        'Content-Type': 'application/json; charset=utf8',
    }
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=20) as reply:
        answer = json.load(reply)
    if answer.get('statusCode') != 100:
        sys.exit('switchbot: %s %s: %s' % (method, path, answer))
    return answer.get('body')


def command(device, name):
    request('POST', '/devices/%s/commands' % device,
            {'command': name, 'parameter': 'default', 'commandType': 'command'})
    print('switchbot: %s %s' % (device, name))


def main(argv):
    if len(argv) >= 2 and argv[1] == 'list':
        body = request('GET', '/devices')
        for device in body.get('deviceList', []):
            print('%s\t%s\t%s' % (device.get('deviceId'), device.get('deviceType'), device.get('deviceName')))
        return 0
    if len(argv) == 3 and argv[1] == 'status':
        print(json.dumps(request('GET', '/devices/%s/status' % argv[2]), ensure_ascii=False))
        return 0
    if len(argv) == 3 and argv[1] in ('on', 'off'):
        command(argv[2], 'turnOn' if argv[1] == 'on' else 'turnOff')
        return 0
    if len(argv) in (3, 4) and argv[1] == 'cycle':
        command(argv[2], 'turnOff')
        time.sleep(int(argv[3]) if len(argv) == 4 else 10)
        command(argv[2], 'turnOn')
        return 0
    sys.exit('usage: switchbot.py list | status ID | on ID | off ID | cycle ID [SEC]')


if __name__ == '__main__':
    sys.exit(main(sys.argv))
