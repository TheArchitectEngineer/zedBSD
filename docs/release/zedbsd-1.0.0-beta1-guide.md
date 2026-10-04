# Kei/zedBSD 1.0.0 Beta 1: user guide

Status: howto; for the 1.0.0 Beta 1 release.

Kei/zedBSD 1.0.0 Beta 1 is a preview. It runs from a USB drive and does not
install itself on the computer's internal disk. Expect rough edges: read the
[known issues](zedbsd-1.0.0-beta1-known-issues.md) and the
[security notes](#security-notes) before you start.

## What you need

- A **Dell Latitude 5330**. This is the computer Beta 1 is tested on. Other
  UEFI PCs with an Intel CPU and Intel graphics may start, but they are not
  tested.
- A USB drive of **4 GB or more**. Writing the image erases everything on it.
- From the release page on GitHub:
  - `zedbsd-1.0.0-beta1-amd64.img.gz`: the USB image, compressed with gzip.
  - `SHA256SUMS`: the checksums of the files.
  - `LICENSES.md`: the components in the image and their licenses.

If the release also has `zedbsd-1.0.0-beta1-windows.zip`, that file runs Kei
in a virtual machine on Windows instead (see [Running on Windows](#running-on-windows)).

## 1. Check the download

The checksum tells you whether the file arrived intact. It does not prove who
made the file.

On Linux or macOS, in the folder that holds both files:

```sh
sha256sum -c SHA256SUMS --ignore-missing     # Linux
shasum -a 256 -c SHA256SUMS --ignore-missing # macOS
```

The image's line must say `OK`.

On Windows, in PowerShell:

```powershell
Get-FileHash .\zedbsd-1.0.0-beta1-amd64.img.gz -Algorithm SHA256
```

Compare the hash with the image's line in `SHA256SUMS`.

## 2. Write the image to the USB drive

Writing erases the whole USB drive. Check twice that you picked the USB drive
and not a disk you need.

### Linux

Find the USB drive's device (for example `/dev/sdb`) with `lsblk`, unmount
its partitions if your desktop mounted them, then write:

```sh
gunzip -c zedbsd-1.0.0-beta1-amd64.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

Replace `/dev/sdX` with the USB drive's device, the whole drive and not a
partition (`/dev/sdb`, not `/dev/sdb1`).

### macOS

Find the USB drive (for example `/dev/disk4`) with `diskutil list`, then:

```sh
diskutil unmountDisk /dev/diskN
gunzip -c zedbsd-1.0.0-beta1-amd64.img.gz | sudo dd of=/dev/rdiskN bs=4m
diskutil eject /dev/diskN
```

Replace `N` with the USB drive's number. `rdiskN` writes faster than
`diskN`.

### Windows

Use a tool that writes disk images, such as
[balenaEtcher](https://etcher.balena.io/) or [Rufus](https://rufus.ie/).
balenaEtcher reads the `.img.gz` file directly. For Rufus, first extract the
`.img` file with a tool that opens gzip files (for example 7-Zip), then choose
the `.img` file in Rufus and write it in DD image mode.

## 3. Set up the firmware

Kei/zedBSD starts in UEFI mode and its boot loader is not signed for Secure
Boot. On the Latitude 5330, press **F2** while the Dell logo is shown to open
the BIOS setup, then:

- Under **Boot Configuration**, turn **Secure Boot** off.
- Keep the boot mode UEFI (Legacy/CSM boot is not used).
- Save and exit.

The other settings can stay as they are.

## 4. Start from the USB drive

Plug in the USB drive, turn the computer on and press **F12** while the Dell
logo is shown. Choose the USB drive under the UEFI boot entries.

The Kei logo appears, and after a short while the desktop. The image signs in
the user **kei** by itself at the first start. After you log out, the login
screen asks for a user and password:

| User | Password |
| --- | --- |
| `kei` | `kei` |

The `root` user cannot log in on this release.

To stop the computer, use **Shut Down** in the system menu. Then remove the
USB drive to start the computer's own system again.

## 5. Connect to a network

### Wi-Fi

The Latitude 5330's built-in Wi-Fi (Intel Wi-Fi 6E AX211) works. Open
**Settings** → **Wi-Fi**, turn Wi-Fi on, choose your network and type its
password. WPA2-Personal networks are supported. Some 5 GHz networks may not
give an address; if that happens, use the 2.4 GHz network of the same router
(see the [known issues](zedbsd-1.0.0-beta1-known-issues.md)).

USB Wi-Fi adapters with the Realtek RTL8822BU chip also work.

### Wired network

USB Ethernet adapters that follow the USB CDC-NCM or CDC-ECM standard work
(many USB-C adapters and docks do). Plug the adapter in **before** you start
the computer; an adapter plugged in later may not come up in Beta 1.

## Security notes

Read these before you connect Beta 1 to a network.

- **The password is public.** Everyone who reads this guide knows the
  password of `kei`, and Beta 1 has no way to change it.
- **The SSH server is on.** Beta 1 accepts SSH logins with that password
  from the network, and the user `kei` cannot turn the server off.
- Together, this means that anyone on the same network can log in to the
  computer as `kei` and read or change that user's files. Use Beta 1 only on
  networks you trust, such as your home network, and do not keep anything
  private in it.
- The image keeps what you save on the USB drive. Anyone who has the drive
  can read it.

## Running on Windows

When the release has `zedbsd-1.0.0-beta1-windows.zip`, you can run Kei in a
virtual machine on a Windows PC instead of starting from USB. Extract the zip
to a folder and run `boot.bat` in it. It starts QEMU with the same image and
3D graphics passed to the PC's GPU (Venus). The same login, password and
security notes apply.

## Licenses

`LICENSES.md` on the release page lists every component in the image and its
license. Inside the running system, the license texts are under
`/usr/share/licenses/`, and `/usr/share/licenses/INDEX` lists them.
