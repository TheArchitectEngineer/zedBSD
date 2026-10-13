<!-- awesome-plan project=zedbsd record=ws227-p001 -->
# ws227-p001: menuconfig の表示名、library の Desktop への移動、Linux・FreeBSD の独自の命令

Status: in-progress（2026-10-14 P1: 実装と host・build の確かめ済み、Q1 の判定待ち。※要確認の 3 件は下の「Linux・FreeBSD」）
Disposition: normal
Parent: [WS227](../ws.md)
Queue: q970
依存: 無し

## 範囲

[ws.md](../ws.md) の目標 1〜4 の全部。

- 表示名は `ZEDBSD_USERLAND_PACKAGE` の 2 番目の引数（`make list-user-programs` の 2 列目）。Base は目標 1、Desktop は目標 2 の規則で全部の行を揃える。表示の前後の比較の表を phase.md に残す。
- 移動は `git mv`（rm は使わない）。`userland/base/lib{png,gif,jpeg}-compat`・`userland/base/libpdf` → `userland/desktop/`。参照（Makefile の第 9・10 引数の path、`platform/*/vmunix.mk`、`userland/desktop/keiland-{linux,freebsd}.mk`、各 Makefile.linux/freebsd、`plan/tools` と master.md の Tools、docs）を grep で全部追う。menu の group を desktop に。libtruetype の menu も desktop。
- pcat・pc98 の `config/ci/*.mk` は Desktop の app が無いのにこれらを入れている。使う物が無ければ外す（結果を phase.md に）。
- Linux・FreeBSD: zedBSD 独自の base の命令を調べて一覧にする（passkey は既に入っている。他の候補とそれぞれ入れる・入れないの理由）。host にある命令は入れない。入れる物は `KEILAND_LINUX_PROGRAM` などで `bin`・`sbin` に。一覧は Q1 に報告（ユーザーの確認が要る物があれば印を）。

## 確かめ

- `python3 tools/menuconfig.py` の host 試験（`make menuconfig-host-test`）、`make list-user-programs` の前後の差。
- amd64 の build（config-amd64.mk 相当）で warning 0、image の中の library の path と名前が前と同じ（.so の install 先は変えない）。
- `make keiland-linux` と FreeBSD の build（できる範囲、できなければ未実施と書く）。
- QEMU の確かめは不要（path の移動だけ）。要ると判断したら T1 への依頼の文を書く。

## 結果（2026-10-14 P1、q970-i01）

base は main 993ff61c3。commit（branch agent/p1）: 5370b7df8（表示名・移動・参照・pcat/pc98 の config・plan/tools・tests/scenarios）、
e86755cf9（他の WS の試験の path だけ、Q1 の (a) の指示）、21ace71c5（Linux の fidoctl と deb の source の snapshot）、ab604cdb0（LINUX.md）。

### 表示名（`make list-user-programs` の 2 列目と 5 列目の前後、変わった 49 行）

Base の他の 176 行は前から命令の名前だけで変えていない。Desktop の「命令の名前＋説明」は `名前 - 説明` の形にした（man の NAME 節の形）。
library の表示は install される .so の名前（`libgl` → `libGL`、`libegl` → `libEGL`、`libglesv2` → `libGLESv2`）。package の名前（1 列目、config.mk の
`ZEDBSD_USER_PROGRAMS` の値）は変えていない。`noct` の表示名は toolchain の file（`userland/base/noct/Makefile`）なので **Q1 の許可（2026-10-14）で表示名だけ変えた**（build の規則・patch は不変、noct の build・install はしていない）。

| package | menu 前 | 表示 前 | menu 後 | 表示 後 |
| --- | --- | --- | --- | --- |
| `audiod` | Base | sound server | Base | audiod |
| `bluetoothd` | Base | Bluetooth daemon | Base | bluetoothd |
| `bt` | Base | Bluetooth command | Base | bt |
| `curses` | Base | curses terminal library | Base | libcurses |
| `emacs` | Base | Emacs editor | Base | emacs |
| `fidoctl` | Base | Security key tool | Base | fidoctl |
| `libz-compat` | Base | zlib interface for base programs | Base | libz-compat |
| `noct` | Base | Noct language | Base | noct |
| `terminfo` | Base | major terminal descriptions | Base | libcurses major terminal definitions |
| `terminfo-extra` | Base | extra terminal descriptions | Base | libcurses extra terminal definitions |
| `volumed` | Base | removable media daemon | Base | volumed |
| `browser` | Desktop | Web browser | Desktop | browser - Web browser |
| `calendar` | Desktop | Calendar | Desktop | calendar - Calendar |
| `files` | Desktop | File manager | Desktop | files - File manager |
| `imageview` | Desktop | Image Viewer | Desktop | imageview - Image Viewer |
| `ime-dict-ja` | Desktop | Japanese dictionaries of the input method | Desktop | ime-dict-ja - Japanese dictionaries of the input method |
| `ime-dict-skk` | Desktop | Dictionaries of the SKK input method | Desktop | ime-dict-skk - Dictionaries of the SKK input method |
| `keiland-ime` | Desktop | Input method | Desktop | keiland-ime - Input method |
| `keiland-preview` | Desktop | Preview maker | Desktop | keiland-preview - Preview maker |
| `keiland-printd` | Desktop | Printer daemon | Desktop | keiland-printd - Printer daemon |
| `libbrowser` | Desktop | Web browser engine library | Desktop | libbrowser |
| `libegl` | Desktop | EGL | Desktop | libEGL |
| `libgif-compat` | Base | GIF interface for base programs | Desktop | libgif-compat |
| `libgl` | Desktop | OpenGL | Desktop | libGL |
| `libglesv2` | Desktop | OpenGL ES | Desktop | libGLESv2 |
| `libjpeg-compat` | Base | JPEG interface for base programs | Desktop | libjpeg-compat |
| `libkeiland` | Desktop | Desktop system library | Desktop | libkeiland |
| `libmedia` | Desktop | Media playing library | Desktop | libmedia |
| `libpdf` | Base | PDF library for base programs | Desktop | libpdf |
| `libpng-compat` | Base | PNG interface for base programs | Desktop | libpng-compat |
| `libtruetype` | Base | TrueType glyphs | Desktop | libtruetype |
| `libvulkan` | Desktop | Vulkan 1.0 | Desktop | libvulkan |
| `libwayland-client` | Desktop | Wayland client | Desktop | libwayland-client |
| `libwayland-egl` | Desktop | Wayland EGL window | Desktop | libwayland-egl |
| `mailer` | Desktop | Mail | Desktop | mailer - Mail |
| `mediastorage` | Desktop | Media library CLI | Desktop | mediastorage - Media library CLI |
| `monitor` | Desktop | System monitor | Desktop | monitor - System monitor |
| `music` | Desktop | Music | Desktop | music - Music |
| `notes` | Desktop | Handwritten notebook | Desktop | notes - Handwritten notebook |
| `pdfviewer` | Desktop | PDF Viewer | Desktop | pdfviewer - PDF Viewer |
| `phone` | Desktop | Phone | Desktop | phone - Phone |
| `photos` | Desktop | Photos | Desktop | photos - Photos |
| `sessiond` | Desktop | graphical login session manager | Desktop | sessiond - Graphical login session manager |
| `settings` | Desktop | Settings | Desktop | settings - Settings |
| `terminal` | Desktop | Wayland terminal | Desktop | terminal - Wayland terminal |
| `textedit` | Desktop | Text Editor | Desktop | textedit - Text Editor |
| `videoplayer` | Desktop | Video Player | Desktop | videoplayer - Video Player |
| `wayland` | Desktop | Wayland compositor | Desktop | wayland - Wayland compositor |
| `xserver` | Desktop | X11 server for the compositor | Desktop | xserver - X11 server for the compositor |

### 移動

- `git mv`: `userland/base/{libpng-compat,libgif-compat,libjpeg-compat,libpdf}` → `userland/desktop/`。package の path（第 9 引数）と依存（第 10 引数）を
  `desktop/lib*` に、menu を desktop に。libtruetype（source は既に desktop）の menu も desktop に。libz-compat は base のまま（表示名だけ `libz-compat`）。
- 参照の追従: `platform/common/userland-dynamic.mk`・`platform/pcat/vmunix.mk`・`platform/pc98/vmunix.mk`（exports.map の path と注記）、各 Makefile と
  Makefile.linux・Makefile.freebsd、`userland/desktop/keiland-{linux,freebsd}.mk`、desktop の app の Makefile の依存、`tests/scenarios/apps/{notes,pdfviewer}/*.md` の paths、
  `tools/release/keiland-linux-deb/run.py`（`userland/desktop` に含まれるので個別の 4 行を外した）、`plan/tools` の 6 file。
- 他の WS の試験（Q1 の指示 (a)）: 34 file の path だけを置き換え（ws079・081・089・090・102・155・157・169・170・175・177・190・210・216）。
  除いた物: `plan/ws074/` の 3 file（Codex の別のセッションが作業中、Q1 が merge の時に扱う）: `plan/ws074/tests/host-build.sh`・`run-gif-tests.py`・`run-jpeg-tests.py`。
  完了済みの WS の試験（master.md の Tools・試験の一覧から参照なし→削除の候補、削除は Q1）: `plan/ws127/tests/host-pdf-thumb.sh`・`plan/ws128/tests/run-host-page-text.sh`・`plan/ws128/tests/run-host-pdfviewer-find.sh`。
- 完了した Phase の phase.md・`plan/history/` の古い path は履歴なので直していない。`plan/master.md` 1093 行の追記は Q1。
- `include/libc/compat/`・`include/libc/pdf.h` の header は置き場（2026-09-28 のユーザーの決定）を変えていない。
- pcat・pc98 の `config/ci/config-{pcat,pc98}.mk`: Desktop の app も X11 の program（Xzed・zshell・zterm・zwm）もこれらを使わないので、
  libgif-compat・libjpeg-compat・libpdf・libpng-compat を外した（libz-compat は残す）。`config/ci/config-intelmac.mk` も同じ状態（使う物が無いのに 4 つを入れている）が範囲外なので残した。
  `config/ci/config-amd64-server.mk` はユーザーの file なので触っていない（package の名前は変わらないので影響なし）。

### Linux・FreeBSD（/opt/keiland/bin・sbin に入れる zedBSD 独自の base の命令）

| 命令 | Linux | FreeBSD | 理由 |
| --- | --- | --- | --- |
| `passkey`（sbin） | 入れる（既存、ws172-p011） | 入れる（既存） | zedBSD 独自。sessiond が使う |
| `fidoctl`（bin） | **入れた**（今回、`userland/base/fidoctl/Makefile.linux`） ※要確認 | 入れない ※要確認 | zedBSD 独自の security key の道具。libpasskey に Linux の層（`os-linux.c`、hidraw）が既にあり、そのまま build できた。**確認: Linux の deb の依存に host の libcrypto（libssl3）が増える**（dpkg-shlibdeps が拾う）。FreeBSD は libpasskey に FreeBSD の層が無く、新しい移植（hidraw の層）が要る |
| `passkey-fido2`（libexec） | 入れない ※要確認 | 入れない ※要確認 | ws172-p011 で「Linux・FreeBSD の passkey は security key の backend を持たない（OpenSSL 不要）」と決めている。入れるなら passkey の設計の変更 |
| `noct` | 入れない ※要確認 | 入れない ※要確認 | zedBSD の言語で host に無いが、build の規則が toolchain（AGENTS.md）で subagent は変えられない。入れるなら Q1・ユーザーの判断 |
| `emacs` | 入れない | 入れない | host に同じ名前の emacs がある（衝突）、noct も要る |
| `account-admin` | 入れない | 入れない | zedBSD の `uapi/system.h` と account の file に依存。host には useradd・pw がある |
| `audiod`・`bluetoothd`・`bt`・`volumed`・`diskpart`・`wifi`・`networkd`・`dhcpc`・`bootlog`・`service`・`shutdown-control`・`swap-control` ほか init・getty | 入れない | 入れない | zedBSD の kernel の uapi（audio・block・blkid・mountinfo・wlan・sysctl）か zedBSD の init・service に依存。host には pipewire・bluez・udisks・fdisk・iw・systemd/rc がある。`bt` は bluetoothd の socket の client |
| SCCS（`admin`・`delta`・`get`・`prs`・`rmdel`・`sact`・`sccs`・`unget`・`val`・`what`）・`cflow`・`cxref`・`fetch` | 入れない | 入れない | POSIX・BSD の標準の命令で zedBSD 独自ではない（ユーザー「zedBSD 独自のコマンドだけ」）。Debian の既定には無いが CSSC などの package がある |
| その他の POSIX の命令（ls・cat など） | 入れない | 入れない | host にある |

deb: `tools/release/keiland-linux-deb/run.py` の source の snapshot に `userland/base/passkey`・`login/verify.{c,h}`・`fidoctl`・`libpasskey` を足した
（passkey の Makefile.linux は ws172-p011 から keiland-linux.mk に入っているのに snapshot に無く、deb の build はその source を欠いていた）。

### 確かめ（2026-10-14、host）

- `make list-user-programs`: 前後の差は上の表の 49 行だけ（328 行、package の名前・platform・既定・依存の解決は不変）。
- `make menuconfig-host-test`: `MAC-T001 menuconfig round-trip: PASS`。
- image の中の path と名前: HEAD（993ff61c3）の `git archive` と作業の tree で `make ZEDBSD_CONFIG=config/ci/config-amd64.mk` の
  `ZEDBSD_USERLAND_DATA_FILES` の宛先（116 個、/lib/lib*.so を含む）が一致、`ZEDBSD_DEFAULT_USER_PROGRAMS` も一致（md5 同じ）。
- amd64 の build: `make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk world`（worktree の build/amd64）rc=0。移した library は
  `build/amd64/dynamic/obj/userland/desktop/lib*/` で compile され、`build/amd64/rootfs/lib/lib{pdf,png-compat,gif-compat,jpeg-compat,truetype}.so` がある。
  zedBSD の tree の warning は 0（log の warning は外部の package の FFmpeg・OpenSSH・perl の locale だけ）。log: worktree の `build/ws227/amd64-world.log`。
- Linux: `make -j8 -f userland/desktop/keiland-linux.mk all KEILAND_LINUX_BUILD=build/ws227/keiland-linux` rc=0、warning 0（gcc、Debian 13 の host）。
  fidoctl: build warning 0、host で `fidoctl` が usage、`fidoctl list` が `devices 0`・`cards 0`（host に key なし）、ldd に libcrypto.so.3。
- FreeBSD: `make -n -f userland/desktop/keiland-freebsd.mk all` で新しい path が全部解決（No rule なし）。**FreeBSD での実際の build は未実施**（host に FreeBSD が無く、QEMU の guest は T1 の担当）。
- QEMU: 不要と判断（image の宛先と既定の選択が前と同じ、変わったのは source の path と menu の表示）。未実施。

### 残件

- ※要確認の 3 件（Linux の fidoctl の libcrypto の依存、passkey-fido2、noct）はユーザーの判断。fidoctl を入れないなら commit 21ace71c5 の
  `userland/base/fidoctl/Makefile.linux` と keiland-linux.mk の include を戻す（run.py の passkey の snapshot は残す）。
- Q1: master.md 1093 行の追記、ws074 の 3 file の path、ws127・ws128 の試験 3 file の削除、config-intelmac.mk の 4 つの library（範囲外）。
- FreeBSD の実際の build（keiland-freebsd）は FreeBSD の環境で未実施。
