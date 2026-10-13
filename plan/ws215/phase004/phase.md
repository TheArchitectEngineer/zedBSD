<!-- awesome-plan project=zedbsd record=ws215-p004 -->

# ws215-p004: BCM4345C0.hcd の firmware の package（brcmbt-firmware）と license の監査

Parent: [WS215](../ws.md)
Status: in-progress（2026-10-13 P3: package・host 試験・license の監査まで済み。Pi 4 の image の build に入って `/lib/firmware/brcm/BCM4345C0.hcd` が rootfs に載る確かめは、rpi4 の image の build（Q1・T1）で見る。Q1 の判定まで cleared にしない）
Phase disposition: normal
Queue: P3 の lane（2026-10-13 Q1 の割当。承認: 2026-10-13 朝 ユーザー D2「OKです」（BCM4345C0.hcd（Cypress の EULA）を Pi 4 の image に））

## 目的・範囲

設計 [p001](../phase001/phase.md) の B18・中-11・D2。Pi 4 の内蔵の BCM43455 の Bluetooth に bluetoothd が読ませる patchram の file（`BCM4345C0.hcd`）を、既存の firmware の package の形（tree に取り込まず、immutable な上流から取得、大きさと sha256 で検証、license・manifest と一緒に入れる）で包む。bluetoothd の読み込みの手順は p005。

受け入れ条件:

1. `userland/firmware/brcmbt/`（package `brcmbt-firmware`、既定 off）。RPi-Distro/bluez-firmware の commit と path、大きさ、sha256 を固定し、取得は build の規則で行い、検証してから cache を公開する。command line から取得元・byte・検証・入れ先を差し替えられない（fixture の goal だけが例外）。
2. `/lib/firmware/brcm/BCM4345C0.hcd` に入る。license の平文と出所の manifest が一緒に入る。
3. Cypress の license の本文: linux-firmware の `LICENCE.cypress` と bluez-firmware の HTML 版の本文の一致を確かめる（違えば規範の側から平文、または止めて Q1 へ）。
4. Pi 4 の image の config（`config/ci/config-rpi4.mk`）だけで選ぶ。他の checked-in の config は選ばない。
5. README・`tools/release/license-components.json` の一覧を更新し、license の目録（`license-inventory.py`）に open の項目が無い。
6. build（`make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk brcmbt-firmware`）が warning 0。既存の firmware の package の試験の形の host 試験が PASS。

## 出所（2026-10-13 P3 が取得して確かめた）

| 物 | 取得元 | 大きさ | sha256 |
| --- | --- | --- | --- |
| `BCM4345C0.hcd` | https://github.com/RPi-Distro/bluez-firmware commit `adb4d55c8f193ff16ece74dd72fd6e8801632103`（branch bookworm の先頭、2025-10-02、「Update changelog for 1.2-9+rpt4 release」）の `debian/firmware/broadcom/BCM4345C0.hcd`。取得は `https://raw.githubusercontent.com/RPi-Distro/bluez-firmware/<commit>/<path>`（rtl8822b と同じ GitHub の raw） | 63806 | `51c45e77ddad91a19e96dc8fb75295b2087c279940df2634b23baf71b6dea42c`（p001 の 2 節の値と一致） |
| `debian/copyright`（`bluez-firmware.copyright` として入れる） | 同じ commit | 23518 | `79576e5ea6740845bf71ce96ad4e31cd49797c8e1a259d64670f48554b8eb67c` |
| `LICENCE.cypress`（平文） | linux-firmware の tag `20260410`（tag object `4585dd5a…`）の commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`（intelbt・intelax211・i915 と同じ）、`…/plain/LICENCE.cypress?id=<commit>`。git blob `070ef66f43421ab8913299953926bfdd8c7b7a91` | 8914 | `ae0db6cc4db33941148df0f67de53e76a77b1b5a46b3165edb7040aa2750015f` |
| （比べただけ、入れない）bluez-firmware の `debian/firmware/broadcom/LICENSE.cypress` | 同じ commit | 22118 | `a92f031f19089fa63bd24ac4c5298ca1356670321b39c67024a961ee3285fff7` |

`debian/bluez-firmware.links` は `BCM4345C0.raspberrypi,4-model-b.hcd` などを `BCM4345C0.hcd` への link にしている。bluetoothd は表で `BCM4345C0` → `BCM4345C0.hcd` を引く（p001 の review-2 への対応の軽微「firmware の file の名前」）ので、入れるのは `BCM4345C0.hcd` の 1 つだけ（image の builder は file だけを入れるので、link の名前を足せば複写になる）。

## license の監査

**本文の一致（中-11）**: 一致した。

- bluez-firmware の `LICENSE.cypress` は kernel.org の cgit の HTML のページ（`<title>LICENCE.cypress - kernel/git/firmware/linux-firmware.git</title>`）で、ページに `blob: 070ef66f43421ab8913299953926bfdd8c7b7a91` とある。linux-firmware の `LICENCE.cypress` を `git hash-object` すると同じ `070ef66f…`。
- HTML の `<td class='lines'><pre><code>` の中から `<style>` と tag を除き、entity を戻した本文は、linux-firmware の `LICENCE.cypress` と、最後の空の 1 行（cgit の code の塊の終わり）を除いて byte まで同じ（138 行）。
- bluez-firmware の `debian/copyright` の `License: CYPRESS-WIRELESS-CONNECTIVITY-DEVICES-DRIVER-END-USER-LICENSE-AGREEMENT` の段落（DEP-5 の字下げ 1 字を除き、` .` を空行に戻す）も、最後の区切りの空行を除いて同じ。この比べは host 試験が verified cache のある時に毎回行う。
- よって規範の平文として linux-firmware の `LICENCE.cypress` を入れる（HTML は入れない）。

**条件と zedBSD の扱い**（`LICENCE.cypress` の本文から）:

| 条項 | 内容 | zedBSD の扱い |
| --- | --- | --- |
| Software Provided in Binary Code Form | object code のまま複製・配布してよい、ただし Cypress の IC の製品と使う時に限る（Purpose） | 既定 off。checked-in の config では Pi 4 の image（BCM43455 = Cypress CYW43455）だけが選ぶ（host 試験が確かめる）。byte は上流と同じ（sha256） |
| Proprietary Rights | 改変・派生物・（許された以外の）複製・逆解析・人の読める形への変換・Purpose 以外の使用の禁止 | 改変しない。bluetoothd は file の HCI の command の枠（opcode・長さ・引数）を読み、各 record をそのまま controller に送るだけ（intelbt の `.sfi` と同じ扱い）。中身の解析はしない。host 試験の record の数え（322 個の `0xFC4C` と 1 個の `0xFC4E`）も枠だけ |
| Free and Open Source Software | FOSS の部分は各 license | 該当なし（`.hcd` は object code のみ） |
| Term and Termination | 違反で終わる | 上の条件を守る |
| Compliance With Law | 米国などの輸出管理 | manifest に記す。release の配布の判断は Q1・ユーザー（他の firmware と同じ） |
| copyright の表示 | `debian/copyright`: `Files: debian/firmware/broadcom/*` / `Copyright: Copyright (c) 2000-2003 Broadcom Corporation` | `debian/copyright` の全文を `/usr/share/licenses/brcmbt-firmware/bluez-firmware.copyright` に入れ、manifest に該当の段落を記す |

GPL の物は入らない（`debian/copyright` の他の段落、GPL-2+ の BlueZ の部分や Synaptics の file は package に入れない。`debian/copyright` は記録の全文として入る）。license の目録は `LicenseRef-cypress-eula (redistribution allowed)`。

## 変えた所

| file | 変更 |
| --- | --- |
| `userland/firmware/brcmbt/Makefile` | 新規。intelbt と同じ形: 本番は `override` で取得元（bluez-firmware の commit、linux-firmware の commit と `?id=`）・3 つの file の大きさと sha256・`curl`・`sha256sum`・入れ先を固定。`brcmbt-firmware-fixture-cache` の goal だけが差し替えを受ける（他の goal と一緒なら error）。cache は `build/sources/firmware/brcmbt/<bluez の commit>-<linux の commit>/`。無ければ隣の一時の directory に取り、全部の型・大きさ・sha256 を確かめてから 1 回の rename で公開。lock、symlink・余分の file・壊れの検出。file の entry は `NAME\|SOURCE\|PATH\|SIZE\|SHA` で SOURCE は `bluez`・`linux` |
| `userland/firmware/brcmbt/brcmbt-firmware.manifest` | 新規。取得元・commit・tag・sha256・git blob・本文の一致の記録・再配布の条件 |
| `Makefile` | `ZEDBSD_CONFIG_OPTIONAL_GOALS` に `brcmbt-firmware-fixture-cache`（intelbt と同じ、config 無しで fixture を走らせるため） |
| `config/ci/config-rpi4.mk` | `ZEDBSD_USER_PROGRAMS` に `brcmbt-firmware` |
| `tools/release/license-components.json` | `brcmbt-firmware` の component（notices は `LICENCE.cypress` と `bluez-firmware.copyright`） |
| `userland/firmware/README.md` | `brcmbt/` の段落 |
| `plan/ws215/tests/brcmbt-firmware-package-test.sh` | 新規。intelbt の試験と同じ形の host 試験（下） |

入る物（rpi4 の config で `ZEDBSD_USERLAND_DATA_FILES` を印字して確かめた）:

- `/lib/firmware/brcm/BCM4345C0.hcd`
- `/usr/share/licenses/brcmbt-firmware/LICENCE.cypress`
- `/usr/share/licenses/brcmbt-firmware/bluez-firmware.copyright`
- `/usr/share/zedbsd/packages/brcmbt-firmware.manifest`

## 確かめ（2026-10-13 P3、host のみ）

| コマンド | 結果 |
| --- | --- |
| `make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk brcmbt-firmware` | rc 0、warning 0。network で 3 file を取得し、`build/sources/firmware/brcmbt/adb4d55c…-dc85cced…/` の sha256 が上の表と一致。2 回目は network 無しで検証だけ |
| `bash plan/ws215/tests/brcmbt-firmware-package-test.sh` | PASS（cache 無しでも PASS、cache ありでは加えて「322 Write_RAM records and Launch_RAM」「licence and copyright record match」）。中身: `.hcd` が git に無い、Makefile と manifest の値の一致、本番の値が command line で差し替わらない、既定の config で選ばれず取得もしない、checked-in の config で選ぶのは `config/ci/config-rpi4.mk` だけ、fixture の goal は単独だけ、fixture の取得（3 回の fetch、2 つの上流の URL の形）、offline の再利用、cache 無し・壊れ（3 file それぞれ）・同じ大きさの別の byte・余分の file・symlink の file・symlink の cache・一部だけの取得で失敗して何も公開しない、入れ先の 4 つの対応 |
| `python3 tools/release/license-inventory.py --config config/ci/config-rpi4.mk --index …` | rc 0、「21 components, 0 open items」、index に `brcmbt-firmware	1.2-9+rpt4	LicenseRef-cypress-eula …` |
| `sh plan/ws129/tests/license-inventory-test.sh` | PASS |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk -n brcmbt-firmware` | warning 0（amd64 の image は選ばない） |

未実施:

- rpi4 の image（rootfs）を最後まで build して、rootfs に 4 つの file が載ることの確かめ。この worktree の `build/arm64/bin` は 12 個だけで、全部の userland の build は重いので行わなかった。data の入れ方は既に rpi4 の image に入っている `rtl8822b-firmware` と同じ `--file` の経路。rpi4 の image の build（Q1・T1）で `/lib/firmware/brcm/BCM4345C0.hcd` を見る。
- 実機で firmware を読ませる確かめは p005（bluetoothd の Broadcom の始動）。

## 気付いた事（範囲の外、Q1 へ）

- `plan/ws143/tests/intelbt-firmware-package-test.sh` は main の今の tree で FAIL する（この Phase の変更の前の tree でも同じ rc 1）。`list-user-programs` が firmware の package の platform を `*` と出す（Makefile 274 行の「CPU に依らない catalog」）のに、試験は `|amd64|` を期待している。`'intelbt-firmware|Intel Bluetooth firmware|*|n|firmware|firmware/intelbt|'` に直せば通る見込み（WS143 の持ち物なので触っていない）。

## 次

- Q1 の判定（rpi4 の image の build で file が載ることを含む）で cleared。
- p005 は p003・p004 の後。bluetoothd は `/lib/firmware/brcm/BCM4345C0.hcd` を読む。
- この試験を回帰に残すなら master.md の試験の一覧へ（Q1）。
