# Kei/zedBSD 1.0.0 Beta 2: known issues

Status: reference; the problems known in the 1.0.0 Beta 2 release.

These are the problems known when Beta 2 was released, with what you can do
about them. The numbers are the project's bug numbers. Read also the
[security notes](zedbsd-1.0.0-beta2-guide.md#security-notes) in the user
guide.

## Computers

| Problem | What to do | Bug |
| --- | --- | --- |
| Beta 2 is tested on the Dell Latitude 5330 only. On the Latitude 5320 the built-in display is not controlled correctly. | Use a Latitude 5330, or try the Windows virtual machine. | — |
| The firmware's ACPI table (DSDT) of the Latitude 5330 is not read completely. Battery, lid and power button events can be missing or wrong. | Keep the computer on AC power. | BUG-165 |
| On battery, drawing slows down to a few frames a second, and the computer stops without a warning when the battery runs out. | Keep the computer on AC power. | BUG-159 |
| **Shut Down** may leave the computer powered on. | When the screen stays on after Shut Down, hold the power button until the computer turns off. | BUG-119, BUG-095 |

## Touchpad, touch screen, keyboard and mouse

| Problem | What to do | Bug |
| --- | --- | --- |
| Two-finger scrolling on the touchpad does not scroll. | Use the scroll bars, the keyboard or a USB mouse wheel. | BUG-156 |
| Pressing the touchpad down (the physical click) is not recognized. | Tap the touchpad instead. | BUG-167 |
| Dragging a window's title bar with the touchpad does not move the window. | Use a USB mouse, or drag with a finger on the touch screen. | BUG-166 |
| On the touch screen, a control reacts when the finger lifts, not when it touches; the volume slider in the system bar moves only with a double tap and slide. | Tap and lift; set the volume in Settings → Sound by tapping a point on the slider. | BUG-190 |
| In the Browser, tapping a button on a web page does not press it. | Use the touchpad or a mouse in the Browser. | BUG-182 |
| Key repeat is uneven, and the key repeat settings in Settings do not change it. | — | BUG-172, BUG-191 |
| Some USB mice with a Logi Bolt receiver do not work. | Use another USB mouse or the touchpad. | BUG-105 |

## Network

| Problem | What to do | Bug |
| --- | --- | --- |
| Some 5 GHz Wi-Fi networks connect but give no address, and the connection drops. | Use the 2.4 GHz network of the same router. | BUG-145 |
| A wrong Wi-Fi password is reported as "Could not join (Network is down)". | Check the password and try again. | BUG-157 |
| A USB Ethernet adapter plugged in after start does not come up. | Plug the adapter in before you turn the computer on. | BUG-168 |
| For about ten seconds after start, the system bar says the network service is not running. | Wait; it goes away by itself. | BUG-176 |
| SSH to the computer's Wi-Fi address does not connect. | Use a wired USB Ethernet adapter for SSH. | BUG-174 |

## Desktop and applications

| Problem | What to do | Bug |
| --- | --- | --- |
| Dragging the volume slider freezes the desktop for a while. | Click a point on the slider instead of dragging it. | BUG-170 |
| Setting the window opacity to opaque in Settings leaves windows slightly see-through. | — | BUG-171 |
| With about 30 windows open, new windows cannot be drawn; many Terminal windows at once can fail to open. | Close windows you do not use. | BUG-120, BUG-175 |
| Japanese input does not work in the search field of Files. | Search with Latin letters, or browse the folders. | BUG-177 |
| In the shell, a long Japanese line in the history can break the display and hide the prompt. | Press Ctrl+C for a new prompt. | BUG-173 |
