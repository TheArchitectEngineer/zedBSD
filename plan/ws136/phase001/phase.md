<!-- awesome-plan project=zedbsd record=ws136-p001 -->
# ws136-p001: 試験の image の script の棚卸しと移行（config.mk ＋ tree の file の複写）

Status: in-progress（q655、P1 generation12、2026-10-04）
Disposition: normal
Parent: [WS136](../ws.md)
Queue: q655（2026-10-04 user「試験のイメージが特殊なビルドになっているのがよくない気がします。試験ビルドの標準的な方法は、config.mkでのビルド＋個別ファイルコピー、程度にして、過去のbuild/を参照するのはやめましょう。config.mkと個別ファイルをwsのtests/に入れればいいだけです。」）

## 棚卸し（2026-10-04）

image を作る script は 33 本（`find plan -name 'build-*image*.sh'` の 32 本と `plan/ws081/tests/build-demo-win.sh`）。過去の build を image の入力に
読んでいた所:

| 入力 | 置き換え |
| --- | --- |
| `build/ws035-fonts`・`build/ws071-fonts`（Inter・JetBrains Mono・Droid Sans Fallback と license） | `userland/desktop/fonts/`（sha256 が同一を確認）。image には wayland の package が既に入れる（`KEILAND_FONT_DATA`）ので、script の重複の `--file` を外した |
| `build/ws035-wallpaper/wallpaper(-1080).ppm` | `userland/desktop/keiland/wallpapers/Birch-Lake.ppm`（同一を確認） |
| `userland/desktop/wallpapers/generate.py` で script ごとに `$build/wallpapers` に描いていた Settings の 5 枚 | config.mk の `ZEDBSD_KEILAND_WALLPAPERS := y` で build の規則（新 `userland/desktop/wallpapers/Makefile`）が描いて入れる（Q1 の判断「案 A」2026-10-04、既定 n で製品の image は不変）。criteria・settings・demo-hdmi・demo-win の config に足した（forge・volume・ax211・remote-log は include で継ぐ） |
| `plan/ws035/demo/demo-accounts.sh`（base の passwd・group・shadow を build/ に写して上書き） | 2026-09-29 から base の account が demo のものなので不要。script を削除し、参照の文書を直した |
| `build/ws100-tests/audiod-feedback`・`build/ws081-tests/touchlog`（共有の固定 path） | 各 script が tree の source から `BUILD/tests/` に compile し、config は `$(BUILD)/tests/…` を読む |
| `build/ws074-images`（make-test-images.py の出力） | script が tree から `BUILD/browser-images` に描く。font の元を `userland/desktop/fonts` に |
| `build/ws079-p006-host/notes.pdf`・`ops.pdf` | libpdf の host 試験（tree の source）が書く物。無ければ script が `run-pdf-render.sh` を流して作る |
| bug027 の libLLVM（main の `build/packages/clang/stage`） | 80 MiB の data として script が決定的な乱数で書く（`LLVM_LIBRARY` を与えればそれ） |
| ws101 s13 の noct（`/home/awe/zedBSD-rpi4/build/demo-lcd9/bin/noct`）、wallpaper（同 tree） | wallpaper は tree の物に。noct は既定を外し `NOCT` を必須に（下の残り） |

## 実装

- 共通の helper [plan/tools/guest/test-image.sh](../../tools/guest/test-image.sh): `test-image.sh [--no-harness] CONFIG BUILD [--file D=S | --mode D=M | NAME=VALUE | TARGET]...`。
  guest の harness の file（鍵・net.conf）を足して `make ZEDBSD_CONFIG BUILD ZEDBSD_TEST_EXTRA_FILES … disk-image` を呼ぶだけ。
- 33 本の script をこの helper で書き直した（config と `--file` の一覧と make の変数だけ）。host の試験・guest の試験の script の
  `build/ws035-fonts`・`build/ws071-fonts` を `userland/desktop/fonts` に、壁紙の参照を tree の物に（files-p015・vkloop-hw ほか）。

## 検証

- 代表の build（2026-10-04、1 つの BUILD `build/p1-ws136/img` で順に、`flock /tmp/zedbsd-image-build.lock`、runner `build/p1-ws136/run.sh`）:
  files・login（graphical）・settings・criteria・ime の 5 つとも exit 0、image を `build/p1-ws136/images/<名前>.img` に写した。warning は外部の package
  （openssh・openssl の source と perl の locale）だけで、tree の C の warning は 0（-Werror）。
- 中身の確かめ（rootfs）: `/usr/share/fonts/keiland{,-mono,-fallback,-emoji}.ttf`（package から）、`/usr/share/keiland/wallpaper.ppm`（Birch-Lake）、
  `/usr/share/files-tests/make-home.sh`、`/root/.ssh/authorized_keys` と `/etc/ssh/ssh_host_*`（harness）。criteria では
  `/usr/share/keiland/wallpapers/{Aurora,Dawn,Lagoon,Meadow,Twilight}.ppm`（`ZEDBSD_KEILAND_WALLPAPERS`、build が描いた）。
- `make-test-images.py` を新しい出力先で流して画像と web font が出ること、全ての書き直した script の `sh -n`。
- demo（`plan/ws075/demo/build-demo-image.sh`）は clang の package を含む構成（`config/ci/config-amd64.mk`）で、subagent は toolchain の package を
  build しないので T1 に依頼。
- QEMU の boot-test: T1 に依頼（未実施）。

## 残り（この Phase で移さなかった物）

- 既存の image を写して ESP や file を差し替える道具（`plan/tools/guest/hybrid-image.sh`、`plan/ws073/tests/kernel-image.sh`・`p015-hybrid.sh`、
  `plan/tools/sh/build-guest-sh.sh`・`guest-expat.sh`・`guest-batches.sh`、`plan/ws045/tests/guest-batches.sh`・`build-guest-utils.sh`、
  `plan/ws044/tests/build-ptrace-test.sh`、`plan/ws014/tests/run-venus-remote.py`、`plan/ws035/tests/boot-shots.py`、`plan/ws013/tests/run-*-zedbsd-config-*.sh`、
  `plan/ws031/handover/tools/make_reloc_image.sh`）: 多くは既定が今は無い `/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest` の image。置き換えは
  clang の入る `config/ci/config-amd64.mk` での test-image.sh の build。使う時に移す。
- `plan/ws031/tests/vkloop-hw.sh`（i915 の実機の試験が自分で make を呼ぶ）: font・壁紙は tree の物にしたが helper には移していない。
- ws101 の accelerator 付きの noct（`build-s13-image.sh`・`g3-hw.sh`・`g3-venus.sh`）: `ZEDBSD_NOCT_ACCEL := y` の build は toolchain の規則で main の許可が要る。
