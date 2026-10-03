<!-- awesome-plan project=zedbsd record=ws136-p003 -->
# ws136-p003: p001 の残り（既存の image を写す道具、/home/awe/zedBSD-rpi4 の既定、vkloop-hw.sh）

Status: in-progress（q663、P2 generation8、2026-10-04。書き換え済み。T1-073 で build・boot・clang は PASS、guest-expat.sh の失敗は道具の config.sub の抜けで直した。再実行は T に）
Disposition: normal
Parent: [WS136](../ws.md)
依存: p001（cleared）。範囲（Q1 2026-10-04）: 既存の image を写す道具（hybrid-image.sh・ws073 kernel-image.sh ほか）の既定の
`/home/awe/zedBSD-rpi4`、vkloop-hw.sh。ws101 の accelerator の noct（`g3-hw.sh`・`g3-venus.sh`）は toolchain の許可が要るので除く。

## 棚卸し

`/home/awe/zedBSD-rpi4` は今は無い（別の tree）。そこを既定にしていた script と、image を写して中身を差し替えていた道具を直した。

## 実装

- **full の guest image の標準の作り方**（新）: `plan/tools/guest/config-amd64-full.mk`（`config/ci/config-amd64.mk` に text の console の起動、
  clang・make・sshd・library の入った全体の userland）と `plan/tools/guest/build-full-image.sh BUILD`（`test-image.sh` で build し image の path を出す）。
  前の「full HAL guest」（`/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest`）の代わり。clang の package を build するので、image は試験の担当
  （T1・T2、自分の worktree の build/、2026-10-03 の main の許可）が作る。
- `plan/tools/guest/hybrid-image.sh BUILD OUTPUT_IMAGE`: 既存の image を写して vmunix・BOOTX64.EFI・libc・make・sh を入れていたのを、
  この tree の full の image を build（それらは全て tree の物）して写すだけに。3 つ目の引数（base の image）は受けず、理由を出して止まる。
- `plan/ws073/tests/kernel-image.sh BUILD OUTPUT_IMAGE`: 同じ（vmunix を差し替える代わりに tree の full の image）。呼ぶ側の `bug075.sh`・`bug082.sh`・
  `p015-native.sh` の最初の引数を VMUNIX から BUILD に。
- 既定を外して引数・環境で求めるように: `plan/tools/sh/build-guest-sh.sh IMAGE_BUILD`・`guest-expat.sh`（`IMAGE` 必須、expat の tar は package の
  検証済みの `build/distfiles/expat-2.8.5.tar.xz` から作る）・`guest-batches.sh`（`IMAGE` 必須）、`plan/ws045/tests/guest-batches.sh`（`IMAGE` 必須）・
  `build-guest-utils.sh IMAGE_BUILD`（sysroot はこの tree の `build/amd64/sysroot`）・`target-check.sh`（既定はこの tree の build）。
- 他の tree への `cd /home/awe/zedBSD-rpi4` を script の在る tree に: `plan/tools/latency/run-echo-qemu.sh`・`run-wakebench-qemu.sh`、
  `plan/ws035/tests/run-hda-qemu.sh`・`run-hda-mmap-qemu.sh`・`run-audiod-qemu.sh`・`run-networkd-replug.sh`・`boot-hda.sh`。
- その他の既定: `plan/ws074/tests/boot-check.sh` の写真の置き場（`build/ws074-shots`、`SHOTS` で変える）、`plan/ws073/tests/socket-desktop.sh` と
  ws081 の試験の使い方の comment の `VENUS_RENDERER`（この tree の `build/ws035-sq-venus/install`、`zdesktop-guest.sh` と同じ）、ws081 の
  pen の image の作り方の comment（`test-image.sh plan/ws079/tests/config-amd64-pen.mk`）、`plan/ws079/tests/notes-p014.sh` の comment、
  `plan/ws095/tests/host-engine.sh` の辞書の候補、`plan/ws001/tests/guest-run.sh` の noct（この tree の `build/NoctLang`）、
  `plan/tools/toolchain/zlib-shared-configure.sh` の distfile（この tree の `build/distfiles`。toolchain の source と規則は変えていない）。
- `plan/ws031/tests/vkloop-hw.sh`: 自分で `make … disk-image` を呼んでいたのを `test-image.sh --no-harness CONFIG BUILD --file… 変数… disk-image`
  に。mode の無い run の config が tree の `config.mk`（worktree には無い）だったので、`plan/ws031/tests/config-vkprobe-hw.mk`（新、
  `config-zdesktop-hw.mk` に vkdemo）を既定に。

## 範囲の外として残した物

- ws101 の accel の noct（toolchain の許可が要る）。
- 指定の image（BUILD の物、`build/amd64` の標準の image）を写して FAT の file を差し替える試験（`plan/ws073/tests/p015-hybrid.sh`、
  `plan/ws044/tests/build-ptrace-test.sh`、`plan/ws013/tests/run-*-zedbsd-config-*.sh`、`plan/ws035/tests/boot-shots.py`）: boot の設定・ESP の file
  そのものが試験の対象で、過去の build を読まない。
- `plan/ws031/handover/tools/make_reloc_image.sh`（他の機械への引き継ぎの道具、`~/zedBSD` と ssh）・`plan/ws048/proposed/` の scratch（提案の下書き）は
  履歴の資料として触らない。
- `plan/master.md` の Tools 節の `guest/hybrid-image.sh BUILD OUT [BASE]` の行は Q1 に更新を依頼。

## 確かめ

- 書き換えた全ての script の `sh -n`。
- `VKLOOP_BUILD_ONLY=1 BUILD=build/p2-vkloop plan/ws031/tests/vkloop-hw.sh`（i915 の実機の image を build だけ、`test-image.sh` 経由）: exit 0、
  `check-amd64-native-image: OK`、rootfs に `/bin/vkdemo` と `/etc/service.d/vkprobe1`・`vkwait1`。5330 での実行は未実施（実機）。
- full の image（clang を含む）の build と、それを使う道具の 1 つ（`kernel-image.sh` で作り boot-test、`guest-expat.sh`）: T に依頼。

## T1-073 の結果（2026-10-04、T1、b1bf23e）と直し

- `build-full-image.sh build/t1-full`: exit 0（780 s）。`kernel-image.sh` の native.img で boot-test PASS（T1 の
  `build/t1-073/boot/login.png`）、SSH で `clang version 23.1.0`・Target `x86_64-unknown-zedbsd`、`make` は bmake の usage の行（version の表示は無い）。
- `guest-expat.sh`: `configure status 1`・`make status 2`・`runtests status 127`。原因は試験の道具（p003 で変えた所）: T1 の guest の disk の写し
  （`build/t1-073-run/disk.img`）の `/root/configure.log` を host で読むと（`tools/build/check-ufs-image.py` の UFS の reader、読むだけ）
  `Invalid configuration 'x86_64-unknown-zedbsd': OS 'zedbsd' not recognized`。前の既定の tar（ws046 が作った物）は `conftools/config.sub` に
  `zedbsd*` を足してあったが、release の tarball から作る形にしたときにそれが抜けた。guest の不具合ではない。
- 直し: `guest-expat.sh` が既定の tar を作るとき、展開して `conftools/config.sub` の `netbsd*` の隣に `zedbsd*` を足し（openssh の package の patch
  と同じ形）、足せたことを確かめ、ustar で tar にし直す。host の確かめ: 作った tar の `conftools/config.sub x86_64-unknown-zedbsd` が
  `x86_64-unknown-zedbsd` を返す、100 文字を超える path は 0。guest での再実行は未実施（次の T の batch で `guest-expat.sh` を 1 回）。
