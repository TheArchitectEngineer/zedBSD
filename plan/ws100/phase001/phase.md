<!-- awesome-plan project=zedbsd record=ws100p001 -->

# ws100-p001: 設計（system bar の音量・audiod・確かめの音・保存・QEMU の試験・5330 の HDA）

Phase ID: `ws100-p001`
Parent: [WS100](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `ws100-volume`（branch `wt/ws100`）。設計だけ、source の変更なし）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「ws100-p001（設計）。source は変えない。修正してよい範囲は plan/ws100/ だけ。試験機 solaris10-man は読むだけ」）

## 成果

- 設計: [design.md](../design.md)（今あるものの調べ、5330 の HDA の調べ、audiod・libkeiland・zdesktop・保存・QEMU の試験の設計、Phase の案、見直し）。

## 要点

- 5330（試験機 `solaris10-man` の Linux 6.19 を読むだけ）: HDA は `8086:51c8`、class `0x040380`（prog-if 0x80）で Kei の pci-hda の
  match（`0x0403xx`）に合う。Linux は自動の選択で **SOF でなく legacy の `snd_hda_intel`** を選んだ。ACPI の NHLT の endpoint は SSP（Bluetooth の経路）
  2 つだけで **DMIC が無い**ことと合う。ただし probe は i915 の blacklist（VFIO の試験の設定）で「audio component」を待って保留され、codec の列挙まで
  進んでいない。**analog codec の speaker・headphone の widget は未確認**（p006 で Kei を直に起動して読む）。
- 既存の audiod は `DEVICE_VOLUME`・`SUBSCRIBE`・`VOLUME_CHANGED` を既に持つ。足すのは `AUDIOD_FEEDBACK`（約 100 ms の内蔵の音、重ならない）と、
  codec に amplifier の無いときの software の音量（今は黙って効かない）。
- zdesktop は network の icon と menu（`network.c`）と同じ形で `volume.c` を足し、libkeiland の `keiland_audio_*`（WS089 の案 + feedback）を通す。
- 保存は利用者の `desktop.conf` の `sound.volume`・`sound.muted`（audiod は root の常駐で利用者を知らないので持たない）。
- Phase の案: p002 audiod、p003 libkeiland、p004 zdesktop（A1〜A6）、p005 Settings の Sound の頁（基準の外、ユーザーの判断）、p006 5330（A7、実機）、
  p007 規約と全体の確かめ。

## 見直し

design.md の §5（誤り 2・欠落 4・矛盾 2 を直し、危険 3 と基準の外 1 を残した）。

## 確かめたこと（読むだけ）

- 試験機: `lspci -nn`・`lspci -vvnn -s 00:1f.3`、`/sys/bus/hdaudio/devices/`（空）、`/proc/asound/cards`（no soundcards）、`lsmod`、
  `/etc/modprobe.d/`、`/sys/module/snd_intel_dspcfg/parameters/dsp_driver`（0）、`sudo -n dmesg`（audio の行）、`/sys/firmware/acpi/tables/NHLT`
  （`sudo -n cat` で読み、host で decode）。試験機の設定は変えていない。
- Kei: `src/drivers/pci/pci-hda.c`、`src/drivers/audio/audio.c`、`userland/base/audiod/`、`userland/desktop/wayland/{network,shell,seat,touch,preferences}.c`、
  `userland/desktop/settings/page-input.c`、`include/libc/keiland.h`、`plan/ws089/proposed/libkeiland-audio.md`、`plan/ws035/tests/run-hda-qemu.sh`。
- 5330 で Kei の HDA が attach した記録は repo と共有の build に無い（これまでの実機の試験は IGD だけの passthrough）。

## Resume point

2026-09-30: cleared。次は p002（audiod）から。p004 の前に `seat.c` の wheel の hook を main と確認（IME の作業中）。p005 を入れるかはユーザーの判断。
