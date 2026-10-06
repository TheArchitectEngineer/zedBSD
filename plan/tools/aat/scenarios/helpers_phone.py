#!/usr/bin/env python3
"""The automatic helpers of tests/scenarios/apps/phone/ beyond open-from-home (WS170 p002–p004).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    helpers_phone.py --outdir OUTDIR [--only REGEX] -- TARGET-OPTIONS     (--list: the ids)

Phone keeps its contacts and timelines under ~/Documents/Phone (one file each); the helpers write two contacts and
their items there as kei before Phone starts (Ben, the latest, then Aiko), and take the folder away after.  The
phone's backend is the desktop's setting phone.backend, set with /bin/keiland-settings (the test image has it): 1 the
loopback (a message comes back as "Echo: ..."), 0 none.  The places in the window are view.c's layout: the contacts
a third of the window (260 to 340), the timeline right of them, the field at the bottom, the call button at the
header's right.
"""
import sys
import time

import aatlib
import common

run = aatlib.Run.from_command_line("helpers_phone")

FOLDER = "/home/kei/Documents/Phone"
COMPOSER = 60
SHARE = 0.34


def prepare_store(item) -> None:
	"""Two contacts with a few items in kei's Documents/Phone (Ben's latest, so he is first)."""
	now = int(time.time())
	script = f"""set -e
rm -rf {FOLDER}
mkdir -p {FOLDER}/contacts {FOLDER}/messages/aat-ben {FOLDER}/messages/aat-aiko
printf 'BEGIN:VCARD\\r\\nVERSION:3.0\\r\\nFN:Ben Carter\\r\\nTEL:+1 415 555 0142\\r\\nEND:VCARD\\r\\n' > {FOLDER}/contacts/aat-ben.vcf
printf 'BEGIN:VCARD\\r\\nVERSION:3.0\\r\\nFN:Aiko Tanaka\\r\\nTEL:+81 90 1234 5678\\r\\nEND:VCARD\\r\\n' > {FOLDER}/contacts/aat-aiko.vcf
printf 'Kind: text\\nChannel: sms\\nDirection: in\\nDate: {now - 60}\\nState: unread\\n\\nCall me when you can.\\n' > {FOLDER}/messages/aat-ben/1-1-in.txt
printf 'Kind: text\\nChannel: rcs\\nDirection: in\\nDate: {now - 7200}\\nState: read\\n\\nGood morning!\\n' > {FOLDER}/messages/aat-aiko/1-2-in.txt
printf 'Kind: call\\nChannel: line\\nDirection: out\\nDate: {now - 3600}\\nState: answered\\nDetail: 2:10\\n\\n\\n' > {FOLDER}/messages/aat-aiko/1-3-out.txt
chown -R kei {FOLDER}
"""
	status, output = run.sh(script)
	item.step("two contacts in ~/Documents/Phone", output.strip()[-200:])
	item.check(status == 0, "the store could not be written")


def forget_store() -> None:
	"""Takes the helpers' store away and the backend back to none."""
	run.sh(f"rm -rf {FOLDER}")
	run.as_user("/bin/keiland-settings set phone.backend 0", wait=True)


def conversation_middle(window) -> int:
	"""The middle of the timeline's column, across the window."""
	share = min(max(int(window.width * SHARE), 260), 340)
	return window.x + (share + 8 + window.width) // 2


@run.define("apps.phone.browse")
def browse(item):
	prepare_store(item)
	try:
		window = run.launch(item, "Phone")
		run.click(window.x + window.width // 6, window.y + window.height // 3)
		time.sleep(0.5)
		mark = run.mark()
		run.key("down")
		first = run.wait(r"PHONE SELECT contact=\d+", mark, 10)
		item.step("Down", first)
		run.shot(item, "selected")
		item.check(first, "no PHONE SELECT")
		mark = run.mark()
		run.key("up")
		back = run.wait(r"PHONE SELECT contact=0", mark, 10)
		item.step("Up", back)
		item.check(back, "Up did not go back to the first contact")
		mark = run.mark()
		run.key("ctrl+q")
		done = run.wait(r"PHONE DONE reason=close", mark, 10)
		item.step("Ctrl+Q", done)
		item.check(done, "Phone did not end on Ctrl+Q")
		item.person("Ben and Aiko in the list, Aiko's call card and message in the screenshot")
	finally:
		forget_store()


@run.define("apps.phone.message")
def message(item):
	prepare_store(item)
	try:
		run.as_user("/bin/keiland-settings set phone.backend 1", wait=True)
		window = run.launch(item, "Phone")
		mark = run.mark()
		run.click(window.x + window.width // 6, window.y + 96 + 34)
		chosen = run.wait(r"PHONE SELECT contact=0", mark, 10)
		item.check(chosen, "Ben was not chosen")
		# A message: sent through the loopback, delivered, and back as an echo.
		run.click(conversation_middle(window), window.y + window.height - COMPOSER // 2)
		time.sleep(0.3)
		mark = run.mark()
		run.type("Hello from AAT")
		run.key("enter")
		sent = run.wait(r"PHONE SEND contact=0 channel=0 length=14 error=0", mark, 10)
		delivered = run.wait(r"PHONE STATUS request=\d+ state=2 failed=0", mark, 10)
		echo = run.wait(r"PHONE RECEIVED contact=0 channel=0 length=20 error=0", mark, 10)
		item.step("typed Hello from AAT, Enter", f"{sent}; {delivered}; {echo}")
		time.sleep(0.5)
		run.shot(item, "echo")
		item.check(sent and delivered and echo, "the message did not go and come back")
		status, files = run.sh(f"grep -l 'Echo: Hello from AAT' {FOLDER}/messages/aat-ben/*.txt | wc -l")
		item.check(files.strip() == "1", f"the echo is not kept in a file ({files.strip()})")
		# A call: not answered.
		mark = run.mark()
		run.click(window.x + window.width - 38, window.y + 32)
		called = run.wait(r"PHONE CALL contact=0 error=0", mark, 10)
		ended = run.wait(r"PHONE STATUS request=\d+ state=5 failed=0", mark, 10)
		item.step("the call button", f"{called}; {ended}")
		item.check(called and ended, "the call was not made and ended")
		# No backend: refused.
		run.as_user("/bin/keiland-settings set phone.backend 0", wait=True)
		time.sleep(0.5)
		run.click(conversation_middle(window), window.y + window.height - COMPOSER // 2)
		mark = run.mark()
		run.type("Again")
		run.key("enter")
		# The error is ENODEV, whose number differs between the systems: any nonzero one, with the message failed.
		refused = run.wait(r"PHONE RESULT request=\d+ error=[1-9]\d*", mark, 10)
		failed = run.wait(r"PHONE STATUS request=\d+ state=0 failed=1", mark, 10)
		item.step("Again with no backend", f"{refused}; {failed}")
		run.shot(item, "no-backend")
		item.check(refused and failed, "the message was not refused without a backend")
		code_lines = [line for line in run.lines(r"(PHONE|KWL PHONE)", mark) if "Again" in line or "555" in line]
		item.check(not code_lines, "a number or the words are in the log")
		item.person("the echo under the message, the call card 'Call not answered', the failed message and the notice")
	finally:
		forget_store()


sys.exit(run.go(before=common.before(run), after=common.after(run)))
