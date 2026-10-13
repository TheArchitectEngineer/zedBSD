# Kei/zedBSD 1.0.0 Beta 2: known issues

Status: reference; the problems known in the 1.0.0 Beta 2 release.

<!-- Draft of 2026-10-09 (ws129-p005, P1), for review. Taken from the open bug tickets and the beta 2 triage
     ("write as known issues"). The "review:" comments name rows that depend on fixes or tests still running; settle
     each one at the release candidate (10/13) and delete the comments. -->

These are the problems known when Beta 2 was released, with what you can do
about them. The numbers are the project's bug numbers. Read also the
[security notes](zedbsd-1.0.0-beta2-guide.md#security-notes) in the user
guide.

## Computers

| Problem | What to do | Bug |
| --- | --- | --- |
| Beta 2 is tested on the Dell Latitude 5330 only. Other computers, the Latitude 5320 among them, may start but are not tested. | Use a Latitude 5330. | — |
| On battery, drawing slows down to a few frames a second, and the computer stops without a warning when the battery runs out. | Keep the computer on AC power. | BUG-159 |
| **Shut Down** may leave the computer powered on. | When the screen stays on after Shut Down, hold the power button until the computer turns off. | BUG-119 |
| With an HDMI display connected, the HDMI display stays dark while the lid is closed. | Keep the lid open. To use only the external display, turn the built-in one off in Settings → Display. | BUG-255 |
| The Gen9 graphics driver (6th to 10th generation Core, on by default) does not drive the display yet: the screen stays at the resolution the firmware chose, its brightness and external displays cannot be changed, and Vulkan Video is not offered. While it drives the GPU, sleep is refused, and the GPU does not enter its power-saving state (RC6), so the battery runs down sooner. | Keep the computer on AC power, or turn the driver off with `i915.gen9=off` (see [Older Intel graphics](zedbsd-1.0.0-beta2-guide.md#older-intel-graphics)). | — (the display driver is planned for Beta 3) |
| SATA drives work without NCQ (many requests at once are slower), TRIM (an SSD is not told which blocks are free) and hot plugging, and optical drives (ATAPI) are not seen. Link power saving (ALPM, DevSleep) is left off on purpose, because many drives and controllers misbehave with it, so a SATA drive uses a little more power. An NVMe SSD that Intel RST hides behind a controller in RAID mode is not seen. | Connect a SATA drive before you start the computer. For an NVMe SSD behind RST, set the firmware's storage mode to AHCI. | — |

## Touch screen, keyboard and mouse

| Problem | What to do | Bug |
| --- | --- | --- |
| In the Browser, tapping a button on a web page does not press it. | Use the touchpad or a mouse in the Browser. | BUG-182 |
| Some USB mice with a Logi Bolt receiver do not work. | Use another USB mouse or the touchpad. | BUG-105 |
| A pen that is already near the screen or tablet when the desktop starts, when the tablet is connected, or when you switch back to the desktop does nothing. | Lift the pen away and bring it back. | — |

## Network

| Problem | What to do | Bug |
| --- | --- | --- |
| Some 5 GHz Wi-Fi networks connect but give no address, and the connection drops. | Use the 2.4 GHz network of the same router. | BUG-145 |
| Copying files over a USB Ethernet adapter is slow (about 1 MB/s). | — | BUG-222 |
| In a virtual machine (QEMU), an SSH session to Kei may stop for one or two minutes and then go on, and a command sent over SSH soon after the start may not run. The cause is not known yet. | Wait, or connect again. | BUG-281 |

## Desktop and applications

| Problem | What to do | Bug |
| --- | --- | --- |
| Dragging the volume slider can freeze the desktop for a while. | Tap a point on the slider instead of dragging it. | BUG-170 |
| With about 30 windows open, new windows cannot be drawn. | Close windows you do not use. | BUG-120 |
| In the shell, a long Japanese line in the history can break the display and hide the prompt. | Press Ctrl+C for a new prompt. | BUG-173 |
| In `vi`, the cursor can stand off by a column in a line with combining characters or characters of ambiguous width (such as some symbols), because `vi` and the Terminal count their widths differently. ASCII and ordinary Japanese text are not affected. | — | — |
| Bold text looks different from regular text (its edges are smoothed differently). | — | BUG-205 |
| Opening and closing App Home is less smooth than on Linux: the animation runs at a lower frame rate. | — | BUG-280 |
| The Browser does not respond while a page loads, and editing the address can put `file://` in front of `https://`. | Wait for the page; type the whole address again. | BUG-207, BUG-206 |
| Japanese input does not work in Phone. | — | BUG-203 |
| Phone calls through a paired phone (Bluetooth HFP) are not supported in Beta 2. The call controls behind **Also use calls** in Settings are untested, and there is no call audio. | Answer and make calls on the phone. Messages and contacts work. | — (planned for Beta 3) |

<!-- review (2026-10-14): the code freeze of 2026-10-14 18:00 took Gen9 (WS229) and AHCI (WS231). The SSH row is a
     suspected problem seen only in the project's own QEMU tests, intermittently. -->

<!-- review: the rows below are the bugs the beta 2 triage plans to fix before the RC, and the ones whose fixes wait for
     T1. Move a row into the table above only if it is still open at the RC.
| The key repeat settings in Settings do not change the repeat. | — | BUG-191 |
| Emacs does not take -nw, and the output of M-x shell is laid out wrongly. | — | BUG-238, BUG-242 |
-->

<!-- review: Bluetooth (WS143) is in the release on the condition that keyboards and mice work on the 5330 by the
     freeze (2026-10-09 user: "in, and a known issue if it does not work"). If they do not, add under "Computers":
| Bluetooth keyboards and mice do not work yet. The Bluetooth page in Settings is there, but pairing does not complete. | Use USB keyboards and mice. | — |
-->
