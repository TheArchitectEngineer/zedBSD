<!-- awesome-plan project=zedbsd record=ws233-p001 -->

# ws233-p001: firmware の一覧と license の監査、zedBSD-firmware の中身、build の取り方の変更

Parent: [WS233](../ws.md)
Status: in-progress（2026-10-14 P1: 一覧・監査・repo の中身（local）・取り方と同意の実装・host の確かめを済ませた。残りは Q1 の repo の作成と push、placeholder の SHA、submodule の除去、T1 の image の試験）
Phase disposition: normal
Queue: P1 の lane（Q1 の割当、2026-10-14）

## 依頼（引用）

- ユーザー 2026-10-14「リポジトリ awemorris/zedBSD-firmware を作って、RPi4のコンパイル済みファームウェアを含め、再配布してよいバイナリであることを確認してそこにすべて入れて、GitHubから落とす形にできませんか？」
- ユーザー 2026-10-14（Q1 が引用）「それでよいです。ghコマンドで作ること、アップロードすることを許可します。ファームウェアのディレクトリごとにライセンスファイルもわかりやすく置くのがよいです。makeで個別ファームウェアをダウンロードしたとき、初回は ライセンス表示と Agree するかの yes/no があるのがいいと思います。CIでは yes | make でいいのでは。」
- ユーザー 2026-10-14（Q1 が引用）「はい、何のファームウェアか、著作権表示、ライセンス文、yes/noですね。」

## 1. 一覧（build が取る・使う firmware の全部）

調べた所: `config/ci/*.mk`・`config/release/*.mk`（どの firmware の package を選ぶか）、`userland/firmware/*/Makefile`、`platform/arm64/`（RPi4 の起動の firmware）、`vendor/`、`.github/workflows/`、`toolchain/`・`tools/`・`platform/` の download の規則（`curl`・`_FETCH`・URL）。firmware の download は下の 7 つだけ。他の download（LLVM の tarball・cache、Kei-nightly の zip、keiland-linux-deb の image）は firmware ではない。

| package（repo の directory） | 選ぶ config | 元（repository・commit） | image の中の宛先 |
| --- | --- | --- | --- |
| `i915` | amd64・pc98・pcat・intelmac | linux-firmware tag 20260410（tag object `4585dd5a…`）commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120` | `/lib/firmware/i915/`、license は `/usr/share/licenses/i915-firmware/` |
| `intelax211` | amd64・pc98・pcat・intelmac | 同上 | `/lib/firmware/intel/iwlwifi/`（manifest のとおり）、`/usr/share/licenses/intelax211-firmware/` |
| `intelbt` | release の amd64（beta2）・amd64 | 同上 | `/lib/firmware/intel/`（WHENCE の link の 8 名は同じ bytes の複写）、`/usr/share/licenses/intelbt-firmware/` |
| `rtl8822b` | 全部の config | endlessm/linux-firmware commit `2f56219d20e4becccd718963fc3bcc671c543ce5`（同じ bytes が linux-firmware `458e40fd…` にある） | `/lib/firmware/rtw88/rtw8822b_fw.bin`、`/usr/share/licenses/rtl8822b-firmware/` |
| `brcmbt` | rpi4 | RPi-Distro/bluez-firmware `adb4d55c8f193ff16ece74dd72fd6e8801632103`、`LICENCE.cypress` は linux-firmware `dc85cced…` | `/lib/firmware/brcm/BCM4345C0.hcd`、`/usr/share/licenses/brcmbt-firmware/` |
| `bcm43455` | rpi4 | RPi-Distro/firmware-nonfree `3bab0f823f5b53150b76aab77093adef6655b920`、`LICENCE.cypress`・`GPL-2` は linux-firmware `dc85cced…` | `/lib/firmware/cypress/`・`/lib/firmware/brcm/`、`/usr/share/licenses/bcm43455-firmware/` |
| `rpi4-boot`（新） | rpi4（platform arm64 は常に） | raspberrypi/firmware `3d301dd924bcd758a4c8cb19fe8531031f033f43` の `boot/`（今は submodule `vendor/raspberrypi-firmware`） | SD の FAT の partition: `/start4.elf`・`/fixup4.dat`・`/bcm2711-rpi-4-b.dtb`・`/overlays/disable-bt.dtbo`・`/LICENCE.broadcom` |

file ごと（34 file、9,344,925 byte。大きさ・sha256 は全部 upstream から取り直して Makefile の値と一致を確かめた、2026-10-14）: [firmware-inventory.txt](../tests/firmware-inventory.txt)（`PACKAGE|REPO-FILE|UPSTREAM-URL|SIZE|SHA-256`）。RPi4 の submodule の中で image に入るのは上の 5 file だけ（`platform/arm64/tools/make-rpi4-hdd-image.py` の `FIRMWARE_FILES` と overlay）。DTB は host の試験（`rpi4-fdt-host-test`）と `make run` も使う。repo には DTB の license の `COPYING.linux` も入れる（build は取らない）。

入れない・対象外:

- `vendor/intel-vbt/dell-latitude-5330-1028-0b02.inc`: Dell Latitude 5330 の VBT を OpRegion から取った物。download ではなく tree の中の source（`I915_TEST_VBT=y` の試験の build だけが使う）。**license が無い（再配布の許可が不明）ので repo に入れない。** 同じ物の raw（`plan/ws031/display-ref/i915_vbt.bin`・`i915_opregion.bin`）も tree にある。今の public の zedBSD の tree に入っていること自体が再配布に当たりうる（要判断、下の 2 節の「判断が要る点」）。
- 試験の data（`plan/ws031/handover/increment-results/*.bin` など）は GPU の command の記録で firmware ではない。

## 2. license の監査

読んだ文書: linux-firmware `dc85cced…` の `WHENCE`（各 file の `License:` の行）・`LICENSE.i915`・`LICENCE.iwlwifi_firmware`・`LICENCE.ibt_firmware`・`LICENCE.cypress`・`GPL-2`、endlessm の `LICENCE.rtlwifi_firmware.txt` と `WHENCE`、bluez-firmware と firmware-nonfree の `debian/copyright`、raspberrypi/firmware の `boot/LICENCE.broadcom`・`boot/COPYING.linux`・`extra/git_hash`、raspberrypi/linux `89586905b8603e545cce9089a81f5f35d65bc998` の dts の SPDX の行。

| repo の directory | license | 第三者の GitHub の repo に置いて配ることを許すか | 条件 | 入れる |
| --- | --- | --- | --- | --- |
| `i915` | `LICENSE.i915`（WHENCE「Redistributable」） | 許す: "Redistribution and use in binary form, without modification, are permitted" | 改変しない。著作権表示と免責を「documentation and/or other materials provided with the distribution」に再現（license の file を同じ directory に置く）。Intel の名を宣伝に使わない。reverse engineering・逆 compile・逆 assemble の禁止。特許の許諾は単体か OSI の license の OS との組だけ | 入れる |
| `intelax211` | `LICENCE.iwlwifi_firmware` | 許す（同じ文） | 同上 | 入れる |
| `intelbt` | `LICENCE.ibt_firmware` | 許す（同じ文） | 同上 | 入れる |
| `rtl8822b` | `LICENCE.rtlwifi_firmware.txt`（Realtek、WHENCE「Redistributable」） | 許す（同じ形の文） | 同上（Realtek） | 入れる |
| `brcmbt` | Cypress Wireless Connectivity Devices Driver EULA（`LICENCE.cypress`。bluez-firmware の `debian/copyright` は `debian/firmware/broadcom/*` をこの EULA とする） | 許す: "license … to reproduce and distribute the Software in object code form only, solely for use in connection with Cypress integrated circuit products" | object code のまま。用途は Cypress の IC と使う場合だけ（RPi4 の BCM43455 = CYW43455）。改変・reverse engineering の禁止（法が許す範囲を除く）。license は non-transferable | 入れる（判断が要る点 b） |
| `bcm43455` | firmware と CLM: Cypress EULA（firmware-nonfree の `debian/copyright` の `debian/added-firmware/*/*43455*`）。NVRAM の値は linux-firmware にも GPL-2.0+ で載る（ws206-p003、ユーザーの回答 E4 2026-10-13） | 許す（同上） | 同上。NVRAM は GPL-2 の文書も添える（`GPL-2`） | 入れる（判断が要る点 b） |
| `rpi4-boot` の `start4.elf`・`fixup4.dat` | `LICENCE.broadcom`（Broadcom・Raspberry Pi） | 許す: "Redistribution and use in binary form, without modification, are permitted" | 改変しない。表示と免責を再現。**使う目的は Raspberry Pi の device のための開発・実行・使用だけ**。Broadcom の名を宣伝に使わない | 入れる |
| `rpi4-boot` の `bcm2711-rpi-4-b.dtb`・`overlays/disable-bt.dtbo` | GPL-2.0（`bcm2711-rpi-4-b.dts` の `SPDX-License-Identifier: GPL-2.0`。overlay は同じ Linux の tree の dts） | 許す（GPL-2.0） | GPL-2.0 の文（`COPYING.linux`）を添え、対応する source を得られるようにする。source は raspberrypi/linux `89586905…`（firmware の commit の `extra/git_hash`）の `arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts`・`arch/arm/boot/dts/overlays/disable-bt-overlay.dts`（2026-10-14 に URL が 200 を返すことを確かめた）。README に書く | 入れる（判断が要る点 c） |
| `vendor/intel-vbt`（tree の中） | なし | 不明 | — | **入れない** |

`WHENCE` は linux-firmware の file と license の目録で、license の行を引く根拠として添える（file 自体に license の記載は無い。linux-firmware の全部の配布物と同じ扱い）。

判断が要る点（法的な判断。下は私の推測であり確認ではない）:

- a. Intel・Realtek・Broadcom の license は「binary の形の再配布」を条件付きで許す文で、配る人の種類を限らない。公開の GitHub の repo に置くのは再配布で、条件（無改変・表示の添付）は directory ごとに license の file を置けば満たすと読む。linux-firmware 自身が git.kernel.org と GitHub の mirror で同じ形で配っている。
- b. Cypress の EULA は「END USER LICENSE AGREEMENT」と題し、non-transferable で、用途を Cypress の IC に限る。public の repo は誰でも取れるので、用途の限定を配る側が担保できない。RPi-Distro（GitHub、public）・linux-firmware・Debian の non-free-firmware が同じ文で public に配っている事実はあるが、それが許諾の解釈として正しいかは法的な判断。zedBSD の今の RPi4 の image もこの file を入れて配っているので、repo に置くことで新しい種類の配布が増えるわけではない（場所が増えるだけ）。README に用途の限定を明記した。
- c. DTB は GPL-2.0 の object の形。§3 は source の添付か書面の申し出を求める。README の source の場所（commit と path）の記載で足りるとするのは推測（Raspberry Pi 自身の firmware の repo も DTB と `COPYING.linux`・`git_hash` だけで配る）。厳密にするなら dts と include する dtsi を repo にも置く。今の zedBSD の RPi4 の image は DTB を入れているが `COPYING.linux` を入れていない（image の側の不足。WS233 の範囲外、Q1 へ報告）。
- d. `vendor/intel-vbt` と `plan/ws031/display-ref/` の VBT・OpRegion の raw: Dell・Intel の platform の data で license が無い。今の public の tree に入っている。外すか残すかはユーザーの判断（WS233 では触らない）。

## 3. zedBSD-firmware の repo の中身（local、push しない）

場所: `/home/awe/zedBSD-worktrees/p1/build/ws233-firmware-repo/`（git init 済み、branch main、commit の message は `WIP`）。作り方は再現できる:

1. `sh plan/ws233/tests/fetch-upstream.sh plan/ws233/tests/firmware-inventory.txt OUT` — 全部の file を元の URL から取り、大きさと sha256 を確かめる。
2. `sh plan/ws233/tests/make-firmware-repo.sh plan/ws233/tests/firmware-inventory.txt OUT plan/ws233/tests/repo-README.head.md plan/ws233/tests/repo-directories.txt REPO` — directory ごとに置き、全体と directory ごとの README.md（directory ごとの物は notice の file（何の firmware か・著作権表示）、license の file への link、条件の要約、file の表）・`SHA256SUMS` を作り、`sha256sum --check` する。REPO が既存の git の checkout なら上書きする。

構成: `README.md`（全体: 目的、directory ごとの用途・出所・license・条件、全 file の表）、`SHA256SUMS`、directory ごとに firmware と元の license の文書と `README.md`（何の firmware か・対象の hardware・file・著作権表示・出所・license の file の名）。

## 4. build の取り方の設計

### 4a. 取る場所

- `userland/firmware/zedbsd-firmware.mk`（新）: `ZEDBSD_FIRMWARE_REPOSITORY_REVISION`（**placeholder、空**）と `ZEDBSD_FIRMWARE_REPOSITORY_BASE_URL := https://raw.githubusercontent.com/awemorris/zedBSD-firmware`。revision が空でなければ 40 桁の 16 進でなければ `$(error)`。`ZEDBSD_FIRMWARE_REPOSITORY_URL` = base/revision（空なら空）。全部 `override`（command line で変えられない。各 package の取得の同一性と同じ扱い）。
- 各 package の production の値: `<P>_FIRMWARE_ZEDBSD_URL := $(ZEDBSD_FIRMWARE_REPOSITORY_URL)/<directory>`（revision が空なら空）。fixture の goal だけ `?=`（既定は空）で変えられる。
- 取り方: file ごとに、`<P>_FIRMWARE_ZEDBSD_URL` が空でなければ先に `ZEDBSD_URL/<cache の名>` を取る。失敗したら message（`could not fetch NAME from the zedBSD firmware repository; fetching it from upstream`）を出して元の URL から取る。大きさと sha256 の検査（cache 全体、今のまま）は取った後に必ず行う。repo が違う bytes を返したら検査で失敗し、upstream へは行かない（改ざん・取り違えを隠さない）。
- **元の配布元を予備に残す理由**: repo が消える・GitHub が止まる・placeholder の commit の取り違えでも build が止まらない（今回の CI の失敗は git.kernel.org の 503 で、主を GitHub にし予備を元にすれば両方が止まらない限り通る）。bytes は sha256 で固定なので、どちらから来ても受け入れる物は同じ。予備を使ったことは message で分かる。
- **段階の切り替え**: placeholder が空の間は今と全く同じ URL から取る（repo が無くても main の build は壊れない）。Q1 が repo を push した後、`zedbsd-firmware.mk` の 1 行に commit の SHA を書けば切り替わる。
- installed の manifest（`*-firmware.manifest`）は変えない。manifest は bytes の出所（upstream の repository・commit・sha256）を書いた物で、repo から取っても bytes は同じ。

### 4b. RPi4 の起動の firmware（submodule をやめる）

- `platform/arm64/firmware.mk`（新）: firmware の package と同じ形（fixture の goal、cache、検査、repo → upstream の順）で `raspberrypi/firmware` `3d301dd9…` の `boot/` の 5 file を `build/sources/firmware/rpi4-boot/<commit>/` に取る（`overlays/disable-bt.dtbo` は `overlays/` の中）。upstream は `https://raw.githubusercontent.com/raspberrypi/firmware/<commit>/boot/...`。`platform/arm64/Makefile` で最初に include。
- `bootloader.mk`・`vmunix.mk`（hdd-image の依存と `--firmware-dir`、`rpi4-fdt-host-test` の DTB）・`run.mk` の DTB を cache に変える。DTB を image の横（`$(BUILD)/bcm2711-rpi-4-b.dtb`）にも複写する（`boot-test.sh` が kernel と同じく image の横から取れるように）。
- これで build は `vendor/raspberrypi-firmware` を読まない。CI の `git submodule update --init --depth 1 vendor/raspberrypi-firmware` は不要になる（ci.yml から外す差分を commit した）。
- **submodule の除去は Q1 の手順**（担当は削除の command を打たない）: `git rm vendor/raspberrypi-firmware`（`.gitmodules` の節も消える。`.gitmodules` が空になれば消す）と、DTB の既定の path を使う道具の更新（`plan/tools/boot-test.sh`・`plan/tools/guest/rpi4-serial.sh` の既定を image の横の `bcm2711-rpi-4-b.dtb` に、無ければ cache に）。他の WS の試験（`plan/ws048/tests/host-test.mk`・`plan/ws203/tests/host-test.mk`・`plan/ws206/tests/*.sh`）は DTB を引数で受けるので、既定の path の更新は各 WS の変更の時に（AGENTS.md の試験の整理の基準）。差分の案は [proposed/remove-submodule.md](proposed/remove-submodule.md)。

### 4c. license の同意（初回の表示と yes/no）

- 表示の順（ユーザー 2026-10-14）: (1) 何の firmware か（名前・対象の hardware・入る file）、(2) 著作権表示、(3) license の全文、(4) `yes/no` の質問。(1)(2) は tree の中の package ごとの notice の file（`userland/firmware/<pkg>/<pkg>-firmware.notice`、`platform/arm64/rpi4-boot-firmware.notice`）、(3) は package が取る license の file（repo → upstream の順で取り、大きさと sha256 を確かめた物）。repo の directory の README.md も同じ notice から作る。
- 共通の helper `userland/firmware/license-agree.sh PACKAGE RECORD NOTICE LICENSE-NAME SIZE SHA256 REPOSITORY-URL UPSTREAM-URL FETCH...`:
  1. 同意の印 `RECORD`（`<cache root>/license-accepted`。行は `<license の sha256>  <license の名>`）に license の sha256 があれば何もしない。
  2. 全体の lock（`<cache root の親>/.license-agree.lock` を `flock`）を取ってから再び見る（`make -j` で複数の package が同時に聞かない。lock の file は残す）。
  3. license の file を `<cache root>/licenses/<名>.<sha256>` に取る（無ければ。repo → upstream、大きさ・sha256 を確かめてから rename。表示した文の記録として残す）。
  4. stderr に (1)〜(3) を出し、(4) `Do you accept the license of PACKAGE? [yes/no]` を聞く。答えは stdin の 1 行。stdin が EOF なら `/dev/tty` を開ければそこから読む（対話の `make -j` で make が stdin を他の job に渡した時）。
  5. `yes`・`y`（大小文字を問わない。`yes` の command は `y` を出す）なら RECORD に追記して 0。他（`no`・空・EOF）は「license が受け入れられなかったので取らない」と次にすることを出して 1。
- 文書が変われば（新しい revision で license の sha256 が変われば）RECORD に無いので再び聞く。license が同じなら revision が変わっても聞かない。
- **いつ聞くか**: その package の cache が無い（初めて取る）時だけ。既に cache がある worktree では聞かない（取得は済んでいる）。
- **make -j と stdin**: GNU make は並列の job のうち最初に始めた 1 つにしか stdin を渡さない（他は即 EOF。make 4.4.1 で確かめた: `yes | make -j3` で 2 つ目の `read` は EOF）。recipe の中で聞くと `yes | make -j4` が偶然に頼る。そこで **parse の時に聞く**: make が makefile を読む時の `$(shell)` は make 自身の stdin を直列に読める（確かめた）。条件: top-level の build、`MAKELEVEL` 0、`-n` でない、goal が build の goal（goal 無し＝既定の disk-image、`all`・`disk-image`・`world`・`bootloader`・`rootfs`・`rootfs-bin`・`rootfs-usr`・`download`・`firmware-licenses`・`*-firmware`）、package が選ばれている（`ZEDBSD_USER_PROGRAMS`、rpi4-boot は arm64 なら常に）、cache が無い。同意しなければ `$(error)`。`release-info`・`list-*`・`kei-nightly-zip`・`toolchain` などは聞かない。
- recipe の中（cache を取る直前）でも同じ helper を呼ぶ（parse で聞かなかった goal で cache を作る時の守り。印があれば何もしない）。
- `firmware-licenses` の goal: 選ばれた package の同意だけを済ませる（parse で聞き、recipe は helper を呼ぶだけ）。CI: `yes | make firmware-licenses` の後に今の `make -j4`（`yes` を build 全体の stdin に流し続けると、stdin を EOF まで読む物があれば止まらないので、聞く所だけに流す）。GitHub Actions の既定の shell は `bash -e`（pipefail 無し）なので `yes` の SIGPIPE で step は落ちない。
- fixture の goal（package の hermetic な試験）では既定で聞かない（`<P>_FIRMWARE_LICENSE_PROMPT ?= n`、production は `override := y`）。既存の package の試験はそのまま通る。

### 4d. 変えた file

- 新: `userland/firmware/zedbsd-firmware.mk`（placeholder・取る場所・同意の macro・`firmware-licenses`）、`userland/firmware/license-agree.sh`（同意の helper）、`userland/firmware/*/<pkg>-firmware.notice` 6 個、`platform/arm64/firmware.mk`、`platform/arm64/rpi4-boot-firmware.notice`。
- 変更: `userland/firmware/{i915,intelax211,intelbt,rtl8822b,brcmbt,bcm43455}/Makefile`（repo → upstream の取り方、同意）、`userland/firmware/README.md`、`platform/arm64/{Makefile,bootloader.mk,vmunix.mk,run.mk}`、`.github/workflows/ci.yml`（submodule の行を外し、各 build の前に `yes | make firmware-licenses`）、`.github/workflows/release.yml`（同じ）。
- `plan/tools`（Q1 の指示 2026-10-14「image を build する plan/tools の script は build の前に `yes | make firmware-licenses`」）: `plan/tools/guest/test-image.sh`（試験の image の標準の道。`build-ssh-image.sh`・`build-full-image.sh` などはこれを通る）、`plan/tools/libm/browser-js.sh`、`plan/tools/libm/guest-test.sh`。DTB の既定（submodule を外せるように、image の横を先に、無ければ submodule）: `plan/tools/boot-test.sh`、`plan/tools/guest/rpi4-serial.sh`。
- 直していない: `plan/ws*/tests/` の中で make の disk-image を直に呼ぶ script（11 本、例 `plan/ws035/tests/build-login-image.sh`・`plan/ws001/tests/guest-run.sh`）。cache の無い worktree で firmware を選ぶ config を build すると、stdin に yes が無いので直ちに止まる（message が `yes | make firmware-licenses` を示す）。

## 5. 確かめ（host、2026-10-14、P1 の worktree）

QEMU・実機は使っていない（build の取り方だけの変更で、image の中の bytes は同じ sha256）。

| 確かめ | command | 結果 |
| --- | --- | --- |
| upstream から全 file を取り直し、Makefile の値と一致 | `sh plan/ws233/tests/fetch-upstream.sh plan/ws233/tests/firmware-inventory.txt build/ws233-upstream` | 34/34 OK（git.kernel.org・GitHub raw とも応答） |
| repo の中身を作る | `sh plan/ws233/tests/make-firmware-repo.sh … build/ws233-firmware-repo` | 43 file（firmware と license 34、README 8、SHA256SUMS）、`sha256sum --check` OK、commit `1e18621b65add96b19377e2ea0063762a618314f`（message `WIP`） |
| 変えた Makefile で repo（file://）から全部を取り、upstream は届かない URL | `sh plan/ws233/tests/check-repository-fetch.sh build/ws233-firmware-repo build/ws233-check4` | 7 package の 33 file（COPYING.linux は build が取らない）が全部 repo から、sha256 一致。repo を届かなくすると intelbt 8・rpi4-boot 5 が upstream へ切り替わり一致。cache の再利用は fetch 無し |
| production の値の固定 | command line で `ZEDBSD_FIRMWARE_REPOSITORY_REVISION`・`*_ZEDBSD_URL` を渡す | 全部空のまま（無視される） |
| placeholder を埋めた時の production の道（一時に local の commit の SHA を書き、戻した） | `make ZEDBSD_CONFIG=config/ci/config-amd64.mk I915_FIRMWARE_CACHE_ROOT=build/ws233-check3/i915 i915-firmware` | URL は `https://raw.githubusercontent.com/awemorris/zedBSD-firmware/<SHA>/i915/…`。repo が未公開なので 404 → 4 file とも upstream へ切り替わり、sha256 一致。40 桁でない値は `$(error)` |
| 同意（yes の pipe・no・EOF・無言の pipe・2 回目・make -j・他の goal・文書の変更・recipe の中） | `sh plan/ws233/tests/check-license-agree.sh build/ws233-firmware-repo build/ws233-license3` | 11/11 OK: `yes \| make firmware-licenses` は 4 回聞き 4 つ記録、表示の順は Firmware → Copyright → license の文 → 質問。2 回目は聞かない。`echo no` は失敗し記録無し、message に `yes \| make firmware-licenses`。`</dev/null` は 1 s で失敗、開いたままの無言の pipe は 3 s で失敗（待たない）。`yes \| make -j4 brcmbt-firmware bcm43455-firmware rpi4-boot-firmware` は 3 回聞いて 3 つの cache。`make list-targets` は聞かない。別の sha256 の記録しか無ければ再び聞く。fixture の recipe で聞く時、no は cache を作らず、yes は作る |
| make の warning | `make -n ZEDBSD_CONFIG=config/ci/config-<各>.mk bootloader`（rpi4・amd64・pc98・pcat・intelmac・amd64-server） | `warning:`・`***` 0 件 |
| script の文法 | `sh -n`／`bash -n`（license-agree.sh・test-image.sh・browser-js.sh・guest-test.sh・rpi4-serial.sh・boot-test.sh） | OK |

未実施:

- 既存の package の試験（`plan/ws031/tests/run-i915-firmware-package-test.sh`・`plan/ws004/tests/run-intelax211-firmware-package-test.sh`・`plan/ws143/tests/intelbt-firmware-package-test.sh`・`plan/ws215/tests/brcmbt-firmware-package-test.sh`・`plan/ws206/tests/bcm43455-firmware-package-test.sh`）: script の中に host の `rm -rf`（trap）があり、担当は流さない（2026-10-06・10-07 の規則）。読んだ範囲では、fixture は既定で聞かず（`*_LICENSE_PROMPT ?= n`）、`*_ZEDBSD_URL` は既定で空、production の値の grep の行は変えていないので通るはず（推測）。Q1 か T1 に流してもらう。
- 端末（tty）からの対話の確かめ（agent の shell に tty が無い）。
- image の build（disk-image）と RPi4 の image の中身の比較（`check-rpi4-hdd-image.py`）、QEMU の boot: T1 に依頼する（下の 6）。
- GitHub の repo からの実際の取得（repo が未公開）。

## 6. 残り・Q1 の手順

1. **GitHub の repo を作り push する**（ユーザーの許可 2026-10-14「ghコマンドで作ること、アップロードすることを許可します」、Q1 が行う）:
   ```sh
   cd /home/awe/zedBSD-worktrees/p1/build/ws233-firmware-repo
   git log --format=%B | grep -v '^$' | grep -vx WIP | wc -l   # 0
   gh repo create awemorris/zedBSD-firmware --public --description "Device firmware used by zedBSD, unmodified, with its licenses"
   git remote add origin https://github.com/awemorris/zedBSD-firmware.git
   git push -u origin main
   ```
2. **placeholder を埋める**: `userland/firmware/zedbsd-firmware.mk` の `override ZEDBSD_FIRMWARE_REPOSITORY_REVISION :=` に push した commit の SHA（今は `1e18621b65add96b19377e2ea0063762a618314f`）を書く。確かめ: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk I915_FIRMWARE_CACHE_ROOT=build/<新しい dir> i915-firmware` が `could not fetch` を出さずに通る（または `check-repository-fetch.sh` と同じ形で raw の URL を `curl -fsSI` で 34 本）。
3. **submodule を外す**: [proposed/remove-submodule.md](proposed/remove-submodule.md)。
4. **T1 への試験の依頼（案）**: image `config/ci/config-rpi4.mk`（`yes | make firmware-licenses` の後に `make -j16 disk-image`、BUILD は新しい dir）。合格: `make check-disk-image` が PASS、FAT の `start4.elf`・`fixup4.dat`・`bcm2711-rpi-4-b.dtb`・`overlays/disable-bt.dtbo`・`LICENCE.broadcom` が今の main の image と byte 一致（`plan/ws073/tests/fat-compare.py`）、`BOOT_MODE=raspi4b plan/tools/boot-test.sh IMAGE` で login prompt（DTB は image の横）。amd64 の CI の config でも `make disk-image` が通り `/lib/firmware` の file が今と同じ。上の 5 本の package の試験も流す。
5. **新しい worktree で image を build する時**（T1・T2・Q1）: `test-image.sh` は自分で `yes | make firmware-licenses` を流す。直に make する時は先に `yes | make ZEDBSD_CONFIG=… firmware-licenses`。
6. 監査の「判断が要る点」b・c・d をユーザーに示す。image に `COPYING.linux`（DTB の GPL-2.0）が入っていない件は別の Phase に。
