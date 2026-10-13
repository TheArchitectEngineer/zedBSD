<!-- awesome-plan project=zedbsd record=ws206-p003 -->

# ws206-p003: BCM43455 の WiFi の firmware の package（bcm43455-firmware）と license の監査

Parent: [WS206](../ws.md)
Status: cleared（2026-10-13 Q1: T1-564 PASS（rpi4 の image に 7 file、sha256 一致、inventory の open 0）。E4 の読み（GPL-2 を package と同じ root の UFS に置く）は、ユーザーの「ではあとは推奨でお願いします」で推奨のまま。旧: 2026-10-13 P3: package・host 試験・license の監査まで済み。Pi 4 の image の build で 7 つの file が rootfs に載る確かめは Q1・T1 の rpi4 の image の build で見る。Q1 の判定まで cleared にしない）
Phase disposition: normal
Queue: P3 の lane（2026-10-13 Q1 の割当。承認: 2026-10-13 朝 ユーザー E4「FATにGPLのライセンスファイルを入れれば問題ないように思います。」、E6「世界共通でお願いします。」）

## 目的・範囲

設計 [p001](../phase001/phase.md) の W9・W10・E4・E6。Pi 4 の内蔵の BCM43455 の WiFi に driver（p004）が読ませる 3 つの file（firmware・CLM・NVRAM）を、既存の firmware の package の形（[ws215-p004](../../ws215/phase004/phase.md) の `brcmbt-firmware` と同じ: tree に取り込まず、immutable な上流から取得、大きさと sha256 で検証、license・manifest と一緒に入れる）で包む。driver の読み込みは p004。E2（`.wifi`）はこの段では使わない。

受け入れ条件:

1. `userland/firmware/bcm43455/`（package `bcm43455-firmware`、既定 off）。RPi-Distro/firmware-nonfree と linux-firmware の commit と path、大きさ、sha256 を固定し、取得は build の規則で行い、検証してから cache を公開する。command line から取得元・byte・検証・入れ先を差し替えられない（fixture の goal だけが例外）。
2. 3 つの file が `/lib/firmware/` の上流の名前（W9）に入る。Cypress の license の平文、GPL-2 の全文（E4）、`debian/copyright` の全文、出所の manifest が一緒に入る。
3. Cypress の license の本文: linux-firmware の `LICENCE.cypress` と firmware-nonfree の `debian/copyright` の `binary-redist-Cypress` の段落の一致を確かめる。3 つの file に効く `Files` の段（DEP-5 の最後の一致）を確かめる。
4. 国の設定は世界共通（E6）: NVRAM に `ccode` が無いこと、CLM に world の locale `XX` があることを確かめ、driver が `XX` を選ぶことを p004 への引き継ぎにする。
5. Pi 4 の image の config（`config/ci/config-rpi4.mk`）だけで選ぶ。他の checked-in の config は選ばない。
6. README・`tools/release/license-components.json` を更新し、license の目録（`license-inventory.py`）に open の項目が無い。
7. build（`make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk bcm43455-firmware`）が warning 0。brcmbt と同じ形の host 試験が PASS。

## 出所（2026-10-13 P3 が取得して確かめた、p001 の W9 の値と全て一致）

| 物 | 取得元 | 大きさ | sha256 | git blob |
| --- | --- | --- | --- | --- |
| `cyfmac43455-sdio-minimal.bin`（firmware 7.45.241、2021-11-01、feature に `idsup`・`idauth`、`clm_min`） | https://github.com/RPi-Distro/firmware-nonfree commit `3bab0f823f5b53150b76aab77093adef6655b920`（branch trixie の先頭、2026-07-10、「Update changelog for 1:20260519-1~bpo13+1+rpt1 release」）の `debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin`。取得は `https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/<commit>/<path>` | 548728 | `3075cb0bdc4b28ed4f08e01b1a216d0ebc70f4022d9d3272a4a43b3c90456e60` | `cc9768bd…` |
| `cyfmac43455-sdio.clm_blob`（"CLM DATA RPi.4 1.29.4"、2021-11-15） | 同じ commit の `debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob` | 2676 | `9823842cae9fb9a5dd1e5fb31f595516ec7deee341354bef30bb3026eee29cc1` | `c905a503…` |
| `brcmfmac43455-sdio.txt`（Pi 4 の NVRAM。`…raspberrypi,4-model-b.txt` はこれへの symlink） | 同じ commit の `debian/added-firmware/brcm/brcmfmac43455-sdio.txt` | 2074 | `ca709be81a78bdb6932936374f39943acbd7af07fae6151011127599a3ce9e3d` | `9a20cb7e…` |
| `debian/copyright`（`firmware-nonfree.copyright` として入れる、8505 行） | 同じ commit | 430523 | `07082fd0bb65e32c73af39a2292dd6c83f169be7427d338ba1fe34f9b86c0e30` | `5b58d968…` |
| `LICENCE.cypress`（平文） | linux-firmware の tag `20260410`（tag object `4585dd5a…`）の commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`（brcmbt・intelbt・intelax211・i915 と同じ）、`…/plain/LICENCE.cypress?id=<commit>` | 8914 | `ae0db6cc4db33941148df0f67de53e76a77b1b5a46b3165edb7040aa2750015f` | `070ef66f…` |
| `GPL-2`（E4） | 同じ linux-firmware の commit の `GPL-2`（linux-firmware が GPL の NVRAM に付ける本文） | 18092 | `8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643` | `d159169d…` |
| （比べただけ、入れない）linux-firmware の `brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt` | 同じ linux-firmware の commit | 1883 | `edb6f4e4fb19e18940004124feb4ffe160d72fc607243a07a4480338a28b2748` | `951dd87d…` |

firmware の名前（依頼の「brcmfmac43455-sdio.bin・.clm_blob」）: 設計 W9 の通り上流の名前のまま入れる。Raspberry Pi の配布では `brcm/brcmfmac43455-sdio.bin` は update-alternatives で standard か minimal を指す link だが、zedBSD の driver は minimal の path を直に開く（standard は `idsup` を持たず E1 の firmware の supplicant に合わない）。

## image での置き場所（p004 の driver が開く path）

rpi4 の config で `ZEDBSD_USERLAND_DATA_FILES` を印字して確かめた（root の UFS に入る。rtl8822b・brcmbt と同じ `--file` の経路）:

| path | 中身 | 大きさ |
| --- | --- | --- |
| `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin` | firmware | 548728 |
| `/lib/firmware/cypress/cyfmac43455-sdio.clm_blob` | CLM | 2676 |
| `/lib/firmware/brcm/brcmfmac43455-sdio.txt` | NVRAM | 2074 |
| `/usr/share/licenses/bcm43455-firmware/LICENCE.cypress` | Cypress の license | |
| `/usr/share/licenses/bcm43455-firmware/GPL-2` | GPL-2 の全文 | |
| `/usr/share/licenses/bcm43455-firmware/firmware-nonfree.copyright` | `debian/copyright` の全文 | |
| `/usr/share/zedbsd/packages/bcm43455-firmware.manifest` | 出所・版・大きさ・sha256・条件 | |

p004 は W8 の「file の確かめ」で上の大きさ（manifest の `installed-*` の `size=`）と、firmware の `43455c0-roml/` の版の文字列、NVRAM が text、CLM の先頭が `BLOB` を見る。

## license の監査

**効く `Files` の段（W9 の確かめの再現）**: `debian/copyright` を DEP-5 の規則（`*` は `/` も含む、同じ file に当たる段は最後が効く）で 3 つの path に当てた。3 つとも `Files: debian/*`（GPL-2+、Debian の packaging）と `Files: debian/added-firmware/*/*43455*`（`Copyright: Cypress Semiconductor Corporation`、`License: binary-redist-Cypress`）の 2 つに当たり、後の Cypress の段が効く。GPL-2 の段（`brcm/brcmfmac43455-sdio.*.txt`、Linaro・Raspberry Pi）は repo の root の `brcm/` の名前付きの file だけで、入れる file には当たらない。host 試験が verified cache のある時に毎回この判定を行う。

**本文の一致**: `debian/copyright` の単独の `License: binary-redist-Cypress` の段落（2139 行目〜、DEP-5 の字下げ 1 字を除き、` .` を空行に戻し、最後の区切りの空行を除く）は、linux-firmware の `LICENCE.cypress` と、平文の先頭の 2 行の見出しの印 `### ` を除いて byte まで同じ（138 行）。よって規範の平文として linux-firmware の `LICENCE.cypress` を入れる（brcmbt と同じ blob `070ef66f…`）。

**NVRAM の 2 つの license（E4）**: 入れる NVRAM は firmware-nonfree の Cypress の file。linux-firmware は同じ値（行の順・comment の見出しと、`NVRAMRev` の行と `btc_mode=1` の重複の 2 行だけ違う、`macaddr` も同じ）を `SPDX-License-Identifier: GPL-2.0+`、(C) 2018 Raspberry Pi (Trading) Ltd.、WHENCE「Licence: GPLv2」で公開している。どちらが元かは公開の資料からは決められない（p001 の W9）。ユーザーの E4 に従い、Cypress の license と GPL-2 の全文の両方を同じ package で入れる。GPL-2 で受けた場合の条件も満たす: NVRAM の text はそのまま改変の好ましい形（source）であり、file 自体が入っている。zedBSD の code はどの file とも link しない（driver は file を読んで chip に書くだけ）。

**E4 の「FAT」の解釈**（Q1 へ、判断の記録）: Pi 4 の image の FAT（boot 区画、実行中は `/boot/boot0`）には VideoCore の firmware と `LICENCE.broadcom` が並ぶが、設計 W9 は WiFi の firmware を root の UFS の `/lib/firmware` に入れる（package の機構は root にだけ入れ、driver は root の mount の後に読む）。ユーザーの答えの要点を「NVRAM と同じ所に GPL の license の file を一緒に入れる」と読み、GPL-2 を NVRAM と同じ root の UFS の package の license の directory に入れた。FAT に置くこと自体を求める意味なら、image の道具（`platform/arm64/tools/make-rpi4-hdd-image.py`、WS206 の範囲の外）の変更と driver の path の変更が要るので、Q1・ユーザーの判断を待つ（下の「判断」）。

**条件と zedBSD の扱い**（`LICENCE.cypress` の本文から、brcmbt と同じ license）:

| 条項 | 内容 | zedBSD の扱い |
| --- | --- | --- |
| Software Provided in Binary Code Form | object code のまま複製・配布してよい、ただし Cypress の IC の製品と使う時に限る（Purpose） | 既定 off。checked-in の config では Pi 4 の image（BCM43455 = Cypress CYW43455）だけが選ぶ（host 試験が確かめる）。byte は上流と同じ（sha256） |
| Proprietary Rights | 改変・派生物・（許された以外の）複製・逆解析・人の読める形への変換・Purpose 以外の使用の禁止 | file を改変しない。driver（W4・W8）は firmware の版の文字列を探し、NVRAM の text を chip の `key=value\0` の形に memory の上で直し、CLM を `clmload` で 1400 byte ずつ送るだけ。code の解析はしない。host 試験の確かめも版の文字列・`BLOB` の見出し・NVRAM の key の有無だけ |
| Free and Open Source Software | FOSS の部分は各 license | NVRAM の値は GPL-2.0+ でも公開されている → GPL-2 の全文を同梱（上） |
| Term and Termination | 違反で終わる | 上の条件を守る |
| Compliance With Law | 米国などの輸出管理 | manifest に記す。release の配布の判断は Q1・ユーザー（他の firmware と同じ） |
| copyright の表示 | `debian/copyright`: `Files: debian/added-firmware/*/*43455*` / `Copyright: Cypress Semiconductor Corporation` | `debian/copyright` の全文を `/usr/share/licenses/bcm43455-firmware/firmware-nonfree.copyright` に入れ、manifest に該当の段を記す |

license の目録の component は `LicenseRef-cypress-eula (redistribution allowed); the NVRAM values are also published under GPL-2.0-or-later`。目録は GPL の名の付く component に決定の出典を求めるので `status: decided`、`decision: 2026-10-13 user (WS206 E4)` とした。

## 国の設定（E6「世界共通でお願いします。」）

- NVRAM（`brcmfmac43455-sdio.txt`）に `ccode`・`regrev` の行は無い（host 試験が毎回確かめる。上流の更新で国が入ったら FAIL する）。
- CLM（"RPi.4"）の国の表に world の locale `XX` がある（`JP` などもある）。
- file は改変しない（Cypress の license）。世界共通は **driver が CLM を読ませた後に `country` の iovar で `XX` を選ぶ**形にする。→ **p004 への引き継ぎ**: W4 の bring-up の CLM の段（10）の後に `country`（abbrev `XX`、ccode `XX`、rev は CLM の既定、推測で 0 か -1）を設定し、読み返した値を log に出す。p001 の E6 の推し（firmware の既定のまま）から変わるので、p004 の設計の最初に W4 へ足す（Q1 へ）。world の locale では 5 GHz の一部の channel が受けるだけ（passive）になる見込み（推測、p006 で観察）。

## 変えた所

| file | 変更 |
| --- | --- |
| `userland/firmware/bcm43455/Makefile` | 新規。brcmbt と同じ形: 本番は `override` で取得元（firmware-nonfree の commit、linux-firmware の commit と `?id=`）・6 つの file の大きさと sha256・`curl`・`sha256sum`・入れ先を固定。`bcm43455-firmware-fixture-cache` の goal だけが差し替えを受ける（他の goal と一緒なら error）。cache は `build/sources/firmware/bcm43455/<nonfree の commit>-<linux の commit>/`。無ければ隣の一時の directory に取り、全部の型・大きさ・sha256 を確かめてから 1 回の rename で公開。lock、symlink・余分の file・壊れの検出。entry は `NAME\|SOURCE\|PATH\|SIZE\|SHA` で SOURCE は `nonfree`・`linux` |
| `userland/firmware/bcm43455/bcm43455-firmware.manifest` | 新規。取得元・commit・tag・sha256・git blob・版・効く段・本文の一致・NVRAM の 2 つの license・国（world）・再配布の条件 |
| `Makefile` | `ZEDBSD_CONFIG_OPTIONAL_GOALS` に `bcm43455-firmware-fixture-cache` |
| `config/ci/config-rpi4.mk` | `ZEDBSD_USER_PROGRAMS` に `bcm43455-firmware`（`brcmbt-firmware` の後） |
| `tools/release/license-components.json` | `bcm43455-firmware` の component（notices は `LICENCE.cypress`・`GPL-2`・`firmware-nonfree.copyright`、`status: decided`） |
| `userland/firmware/README.md` | `bcm43455/` の段落 |
| `plan/ws206/tests/bcm43455-firmware-package-test.sh` | 新規。brcmbt の試験と同じ形の host 試験（下） |

## 確かめ（2026-10-13 P3、host のみ、worktree agent/p3、base main 6ac1cc063）

| コマンド | 結果 |
| --- | --- |
| `make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk bcm43455-firmware` | rc 0、warning 0。network で 6 file を取得し、`build/sources/firmware/bcm43455/3bab0f82…-dc85cced…/` の sha256 が上の表と一致。2 回目は network 無しで検証だけ（rc 0） |
| `bash plan/ws206/tests/bcm43455-firmware-package-test.sh` | PASS（verified cache ありで、加えて「7.45.241」「licence and copyright record match」）。中身: firmware の byte が git に無い、Makefile と manifest の値の一致、本番の値が command line で差し替わらない、既定の config で選ばれず取得もしない、checked-in の config で選ぶのは `config/ci/config-rpi4.mk` だけ、fixture の goal は単独だけ、fixture の取得（6 回の fetch、2 つの上流の URL の形）、offline の再利用、cache 無し・壊れ（6 file それぞれ）・同じ大きさの別の byte・余分の file・symlink の file・symlink の cache・一部だけの取得（licence の側が無い）で失敗して何も公開しない、入れ先の 7 つの対応。cache ありでは DEP-5 の効く段、Cypress の本文の一致、GPL-2 の版、firmware の版の文字列（`idsup`）、CLM の `BLOB`・`RPi.4`・`XX`、NVRAM に `ccode` が無いこと |
| `bash plan/ws215/tests/brcmbt-firmware-package-test.sh` | PASS（config-rpi4.mk の変更の後も） |
| `python3 tools/release/license-inventory.py --config config/ci/config-rpi4.mk --index …` | rc 0、「22 components, 0 open items」、index に `bcm43455-firmware	1:20260519-1~bpo13+1+rpt1	LicenseRef-cypress-eula … GPL-2.0-or-later	…LICENCE.cypress …GPL-2 …firmware-nonfree.copyright` |
| `sh plan/ws129/tests/license-inventory-test.sh` | PASS |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk -n bcm43455-firmware` | warning 0（amd64 の image は選ばない） |
| rpi4 の config で `ZEDBSD_USERLAND_DATA_FILES` を印字 | 上の 7 つの path が cache と manifest に対応 |

未実施:

- rpi4 の image（rootfs）を最後まで build して、rootfs に 7 つの file が載ることの確かめ（担当は image を作らない。Q1・T1 の rpi4 の image の build で `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin` などを見る）。
- 実機で firmware を読ませる確かめは p004（chip id・版・MAC）。
- QEMU は不要（QEMU raspi4b に BCM43455 は無く、package は data だけ）。

## T1 への依頼（Q1 経由、次の rpi4 の image の build にまとめてよい）

- image: agent/p3 のこの Phase の commit を含む tree、`config/ci/config-rpi4.mk` の image（ws215-p004 の確かめと同じ回でよい）。
- 確かめ: 作った rootfs（または image の UFS）に次の 7 つがあり、大きさと sha256 が上の表と同じ: `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin`、`/lib/firmware/cypress/cyfmac43455-sdio.clm_blob`、`/lib/firmware/brcm/brcmfmac43455-sdio.txt`、`/usr/share/licenses/bcm43455-firmware/{LICENCE.cypress,GPL-2,firmware-nonfree.copyright}`、`/usr/share/zedbsd/packages/bcm43455-firmware.manifest`。`python3 tools/release/license-inventory.py --config config/ci/config-rpi4.mk --rootfs <rootfs>` が 0 open items。
- 合否: 7 つが揃い byte が一致、目録が 0 open items で PASS。QEMU の起動は要らない（rpi4 の起動の回帰は p002a・p002b の依頼と同じ回で既に予定）。

## 判断（Q1 へ）

- **E4 の「FAT」**: 上の解釈（GPL-2 を NVRAM と同じ root の UFS の package に同梱）でよいか。FAT（`/boot/boot0`）に GPL-2（と NVRAM）を置くことまで求める意味なら、`make-rpi4-hdd-image.py` と p004 の path の変更が要る（別の Phase）。
- **E6 の world**: driver が `country XX` を明示する形を p004 の W4 に足す（p001 の推しの「firmware の既定」からの変更）。

## 次

- Q1 の判定（rpi4 の image の build で 7 つの file が載ることを含む）で cleared。
- p004 は p002b と p003 の後。driver は上の 3 つの path を開く。
- この試験を回帰に残すなら master.md の試験の一覧へ（Q1）。
