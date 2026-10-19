# WS270 設計: VisionFive 2 で Keiland の desktop を動かす

- 版: 2（2026-10-19 P1、q1338、ws270-p001）。[レビュー 1](design-review-1.md)（B1・M1〜M7・Minor 1〜12）への対応は末尾の表。版 1 は git の履歴（q1335）。
- 前提: [WS269 design.md](../ws269/design.md)（改訂 3.1、§2・§5・§6・§7）、[WS269 ws.md](../ws269/ws.md) の WS269-D5 (a)（2026-10-19 22:04 ユーザー「(a) 画面なしで先行」）、
  [WS256 ws.md](../ws256/ws.md)（p007 の riscv64 の userland、p015 の rootfs、p016・p020 の USB、p018 の LAN、p019 の sound）、
  WS269 p007 の display の node（`src/drivers/gpu/powervr/display.c`: gpu0、display だけの node、render の node は gpu1。p007 は in-progress、実機の目視は未実施、import は WS269 p006 の後）。
- 本 WS の判断は **WS270-D1〜WS270-D5**（WS269 の D と区別する前置き）。コードは書かない。「確かめた事」は自分の worktree の build（QEMU は起動していない）で見た物。「推測」は確かめていない。

## 1. 今の Keiland と依存、riscv64 での build（受け入れ 1）

### 1.1 build の状況（p002 の後）

ws270-p002（q1336 d9a4dc026、q1338 の直し）で、設計の判断に依らない build の不足は直った。試しの config は [tests/config-visionfive2-desktop.mk](tests/config-visionfive2-desktop.mk)（visionfive2 の config に rpi4 の desktop の program の組。firmware・bluetooth・wifi・powervr-firmware・noct・emacs を除く）。

| 版 1 で見た失敗 | 直した所（p002） |
| --- | --- |
| 共有の library の規則が無い（`.so` 19 個、desktop の program 17 個の link の失敗 326 行） | `platform/riscv64/userland.mk` が `DYNAMIC_SYSROOT`・`DYNAMIC_LINK_CFLAGS := $(RISCV64_USER_ARCH_FLAGS)`・`DYNAMIC_LINK_LDFLAGS := $(RISCV64_USER_LDFLAGS)`（`--no-relax` を含む）・`DYNAMIC_ELF_MACHINE := riscv64` を置いて `platform/common/userland-dynamic.mk` を include。libutil の規則は common 側だけにし、基本の program の規則から `DYNAMIC_PORTABLE_PROGRAMS` を除いた（arm64 `vmunix.mk:589` と同じ。除かないと規則が二重になり make が overriding を警告し、basic の規則が libc だけで link して undefined symbol になる） |
| browser の `shell/internal.h` | 上の include で解決（`-Iuserland/desktop/browser` は `userland-dynamic.mk` の target ごとの flag） |
| `kwl_cycles` の `rdtsc` | riscv64 は `clock_gettime(CLOCK_MONOTONIC)` の ns。用途は KWL PERF の統計だけ（`wayland/main.c` の自己較正、`compose.c` の perf）。分解能は tick（`HAL_TIMER_FREQUENCY 1000`）で統計には足りる。**閉じた** |
| 外部 package の `unknown target triple` | `external.mk` の triple `riscv64-unknown-zedbsd` と `ZEDBSD_EXTERNAL_LLVM_TARGET` の `RISCV`、`gen-cross-toolchain.sh` の riscv64 の abi_flags（`-DKERN_USER_ABI_LP64 -mno-relax`）、openssl の `zedbsd-riscv64`（asm なし） |
| `bin/keiland-preview` | class が static の program。arm64 の `ARM64_USER_STATIC_COMMAND` の形を riscv64 に写した（`RISCV64_USER_STATIC_COMMAND`。`--gc-sections` で PT_TLS を空にし、`check-user-elf.py --machine riscv64` で確かめる。空の PT_TLS は kernel が無いと扱う、`src/kern/elf.c` の「Linkers may emit an empty PT_TLS」）。kernel・HAL の変更は要らない（arm64 と同じ制約） |
| `lib/libcurses.a` | platform の規則（toolchain ではない）。arm64 `vmunix.mk:344` の形で riscv64 の static の object の規則と libcurses の規則を足した |
| `bin/noct` | WS270 の範囲の外（§1.3、M2）。試しの config から noct と、実行時に `/usr/bin/noct` を要る emacs（remacs）を外した |

結果（2026-10-19 q1338、P1 の worktree の `build/visionfive2/`）: 試しの config の `make -k -j16 rootfs` は rc=0（残りの失敗 0）。`bin` 215 個、`dynamic/*.so` 23 個。amd64・rpi4 の `rootfs` は rc=0、自前の code の warning 0（出たのは openssl・openssh の upstream の既存の warning と host の perl の locale だけ）。

残る注意（未確認）: `gen-cross-toolchain.sh` の wrapper の注釈「The loader places the startup set in one static area, so … may use any model」は riscv64 では成り立たない（riscv64 は全部 dynamic の TLS、`platform/riscv64/userland.mk:5-9`）。`-ftls-model=global-dynamic` は強制されるが、package の明示の `tls_model("initial-exec")` は防げない。openssl・curl・openssh の riscv64 での実行は未確認（p003 で起動を見る）。openssl の patch を変えた時に `ZEDBSD_EXT_openssl_PATCH_LEVEL` を上げる慣習があるかは未確認（patch は extract の前提なので再 extract はされる）。

### 1.2 arch の分岐（`userland/desktop` の C、grep `__aarch64__|__x86_64__|__riscv`）

| file | 今 | riscv64 |
| --- | --- | --- |
| `wayland/main.c` `kwl_cycles` | aarch64 は `cntvct_el0`、riscv64 は monotonic（p002）、他は `rdtsc` | 済み |
| `libvulkan/wsi-swapchain.c:844` `vulkan_wsi_cursor_clock` | aarch64 は counter、他は monotonic | 変更なし |
| `libbrowser/bind/environment.c` | riscv64 を足した（p002） | 済み |
| `libkeiland-backend/machine/machine.c:39` | x86 だけ CPUID の brand | 変更なし（CPU 名は後回し） |
| `libmedia/vkvideo.c:20-28` `VKVIDEO_SKIP_FAILED` | aarch64 だけ。Pi 4 の firmware の decoder の絵の失敗を飛ばす設定 | **変更なし（else の側）** |
| `videoplayer/videoplayer.h:47-58` `VP_GPU_PRESENT` | aarch64 だけ。Pi の decoder が持つ絵を GPU で出す | **変更なし（else の側、CPU で描く）** |
| `libmedia/vkvideo-runtime.c:38-47` `VKVIDEO_FLIGHTS` | aarch64 だけ 4 | **変更なし（else の側）** |

BXE に Vulkan video は無い（推測）ので、riscv64 は何もしないで else の側に落ちる。それが正しい側。

### 1.3 依存の一覧

| 依存 | riscv64 の今 | 要る事 |
| --- | --- | --- |
| 共有の library の graph | p002 で済み | なし |
| libc・rtld・TLS | WS256 p007 で済み（全部 dynamic の TLS） | なし。static の program は PT_TLS を持たない事を `check-user-elf.py` が確かめる |
| Wayland・libvulkan・sessiond・audiod | build は済み | 実行は p003 以降 |
| font | data だけ | rootfs の大きさ（§1.4） |
| input（`/dev/input`、usb-hid） | WS256 p016（virt、cleared）・p020（実機、待ち） | なし |
| audio | WS256 p019（実機待ち） | なし |
| openssl・zlib・curl・openssh | p002 で build 済み | 実行は未確認（§1.1） |
| Noct（`bin/noct`） | 案のまま（WS256 p007 の proposed/noct-riscv64.diff） | **WS270 の外**。`ZEDBSD_NOCT_ACCEL` は amd64 でしか効かない（`userland/base/noct/Makefile:88`）、desktop の実行時に `/bin/noct` を使う所は無い、`make-*.noct` の道具は host の noct（`build/NoctLang`）。VF2 の desktop の config から noct（と、noct を実行時に要る emacs）を外す。riscv64 の Noct の要否は Q1 が WS270 の外で判断する |
| libcurses | p002（q1338）で済み | なし |

### 1.4 他の port との比べ

| | amd64 | rpi4（arm64） | VF2（riscv64） |
| --- | --- | --- | --- |
| config | `config-amd64.mk` | `config-rpi4.mk`: graphical login y、desktop の program 全部 | 製品: `config-visionfive2.mk`（login に要る 21 個と `powervr-firmware`）。試し: `plan/ws270/tests/config-visionfive2-desktop.mk` |
| 共有の library | include | include | include（p002） |
| GPU の描画 | Venus・i915・nvrtx・amdrdna | bcm2711 | WS269 p010 まで無い |
| display | 各 driver | bcm2711 の HVS | powervr の display の node（gpu0、copy の道だけ。BLOB の present は ENOTSUP `display.c:741-742`、constraints は COPY だけ `:850`） |
| rootfs | 大きな root | SD の root | virt の `root.img` は 64 MiB 固定（`platform/riscv64/rootfs.mk:20-21`）、native は `RISCV64_NATIVE_ROOT_MIB` |

desktop の root は 64 MiB に入らない（推測、大きさは p003 の始めに `du` で測る）。QEMU は WS256 p015 の「走り D」（virt の NVMe に native.img、`RISCV64_NATIVE_ROOT_MIB`）を使う（BOOT_MODE は p015 の走り D と同じ）。`ZEDBSD_ROOTFS_DEVELOPMENT := n`（sysroot の headers と static lib を外す）でも減る（WS270-D5 の選択肢）。

## 2. 画面なしで先行する範囲と、描画を始める条件（受け入れ 2）

前提: Keiland は Vulkan でしか描かない（WS269 design §2・§5 C）。render の node は WS269 p006・p009・p010。WS269-D5 (a) により、WS269 p010 までは描画を含まない。

**描画の道（B1）**: compositor が普段 display を claim して present する道は libvulkan の WSI（`libvulkan/wsi-display.c:990` の `GPU_DISPLAY_CLAIM`）。この WSI には copy の道がすでにある: `display_prepare_copy`（`:1883-1889`）が `GPU_SCANOUT_COPY` を見て、`:1810-1852` が `GPU_RESOURCE_WRITE` で gpu0 の resource に書いてから `GPU_DISPLAY_PRESENT` を呼ぶ。`wsi-swapchain.c:1097-1118` は shared の trial が失敗すると copy に落ち、companion の無い display も隠さない（`wsi-display-nodes.c:426`）。`libkeiland-backend-zedbsd/scanout-zedbsd.c` は **game mode の direct scanout 専用**で、`GPU_DISPLAY_BLOB` が無いと ENOTSUP（`:327-329`）。VF2 では import が来るまで game mode の direct scanout は無く、`wayland/scanout.c:296-300` で kept（合成）に落ちる。

### 2.1 WS269 p010 までに進める範囲

| 項目 | 中身 | 依存 | 確かめ（画面の 1 行か試験の要約の 1 行。serial・console の log では判定しない） |
| --- | --- | --- | --- |
| A. userland の build | §1 | WS256 p007 | **p002 で済み**（rc=0、warning 0、`check-dynamic-elf.py`・`check-user-elf.py`） |
| B. service の起動 | `sessiond`・`audiod`・`volumed`・`dhcpc` などが riscv64 で起きる | A | T1、QEMU riscv64-virt: `boot-test.sh` の `BOOT_THEN` で text login の後に `type:` で `service status` を打ち、`expect:` で画面を読む。`service status` が要約の 1 行を出すかは未確認（出なければ各 service の行を `expect:`） |
| C. seat・input | `/dev/input` の keyboard・mouse の event を読める | A、WS256 p016（virt）・p020（実機） | 既存の `userland/tests/hidraw-probe`・`aat-input` を再利用（新しい program は作らない）。QEMU は PCI（xhci・usb-hid）の入った virt の config（`plan/ws256/tests/config-riscv64-virt-pci.mk` を元に desktop の組）。実機はユーザーの目視 |
| D. display の node が desktop の image で動く | 既存の `userland/tests/gpu-bars`・`display-inventory` が desktop の image で動く（gpu0 の列挙、claim → 色 → release） | A、WS269 p007 の前半（in-progress） | 実機（ユーザーの目視）。WS269 p007 の試験 1〜4 と同じ program なので、WS269 p007 の確かめと同じ回で 1 度に見る（重ねない） |
| E. graphical login の失敗の道 | 既存の ws239-p001 の fallback（`sessiond/fallback-rules.c`。greeter が READY を言わずに終わると 6 回連続か 90 s で text console の getty に戻る。READY の時間切れは 30 s、`greeter.c:66`）が riscv64・VF2 で働く事の**確かめ**。新しい実装はしない | A・B | QEMU（T1、`boot-test.sh` で text login に戻る PNG）と実機。理由の 1 行が画面に出るかは未確認（p003 で見る。出なければ表示の 1 行を足すかを p003 で決める） |

### 2.2 描画を始める条件

1. WS269 p010 が cleared（`vkdemo`・`venus-frame` の画が実機で出る）。gpu1（render の node）が無いと libvulkan の WSI の claim の道そのものが動かない。
2. 本 WS の p003・p004 が cleared。
3. **最初の描画は libvulkan の copy の道**（gpu1 の image → host-visible の staging → CPU の `GPU_RESOURCE_WRITE` で gpu0 の resource → `GPU_DISPLAY_PRESENT`）。import を待たない。WS269 p007 の import と damage は**性能**のため（§2.3 の段 2）。

### 2.3 性能の段（M4）

copy の道は毎 frame 全面を CPU で読む: GPU から host-visible の staging へ copy、CPU が `GPU_RESOURCE_WRITE` で gpu0 へ書き（`wsi-display.c:1810-1835`）、kernel が framebuffer へ copy して clean する（`display.c` の show_frame）。WS269 D7 (a) では HOST_COHERENT は uncached で HOST_CACHED は出さないので、staging の読みは uncached の読み。1080p で 1 frame 約 8 MB。U74 の uncached の読みの帯域は未計測で、frame あたり数十 ms、10 fps 台以下と推測する。damage の矩形だけの copy（WS269 §3.5）は import の道だけ。

- 段 1（p006）: copy の道で最初の画を出す。frame の時間（KWL PERF の統計）と compositor の起動の時間を測る。
- 段 2（WS269 p007 の後半の import・damage、WS269 p011）: 実用の fps。受け入れの目安は段 1 の測定の後に決める。
- 危険: 遅い起動が sessiond の READY の時間切れ（30 s）にかかり、ws239 の fallback で text に戻る（§6）。

## 3. 実装の Phase の分割（受け入れ 3）

LW は粗い（8 LW ≈ 実時間 2〜3 h）。

| Phase | 中身 | 依存 | LW | 確かめ |
| --- | --- | --- | --- | --- |
| p002 | riscv64 の desktop の build（§1.1）。**済み**（q1336・q1338） | WS256 p007 | （実績） | build rc=0・warning 0、amd64・rpi4 の rootfs rc=0 |
| p003 | QEMU riscv64-virt の画面なしの確かめ（§2.1 B・C・E）: desktop の image（NVMe の native.img、大きさを測る）、service、input の既存の試験、graphical login の失敗の道（ws239 の fallback） | p002、WS270-D5 | 4〜8 | T1（`boot-test.sh` の PNG と `BOOT_THEN` の `type:`・`expect:`） |
| p004 | VF2 の実機の画面なしの確かめ（§2.1 C・D・E）: desktop の image を SD（か NVMe）に、USB keyboard・mouse の event、`gpu-bars`・`display-inventory`、graphical login の失敗の道 | p003、WS256 p015・p020、WS269 p007 の前半 | 3〜6 ＋ 実機の debug 2〜6 | 実機（ユーザーの目視、Q1 の「ユーザーが選べる試験の一覧」） |
| p005 | （WS270-D2 (a) の時だけ）QEMU riscv64-virt の Venus で Keiland を描く。§4 の (a)〜(f): riscv64 の HAL の user の DEVICE の写像の補完、virt の Venus の vmunix.mk・`riscv-virt.c` への登録、hostmem の BAR の置き場所、virtio-gpu の head を撮る道具 | p003、WS270-D2 | 16〜28 | T1（QEMU riscv64-virt ＋ host の Venus、desktop の PNG） |
| p006 | **desktop の起動と目視**: compositor の起動、libvulkan の copy の道の present、frame の時間の測定（§2.3 段 1） | **WS269 p011**、p004 | 8〜16 ＋ 実機の debug 4〜8 | 実機（ユーザーが desktop を目で見る） |
| p007 | desktop の受け入れ: USB keyboard・mouse、LAN（WS256 p018）、sound（WS256 p019）、app の起動の一覧（WS270-D3、videoplayer は Vulkan video の queue が無い時の振る舞いを見る）、起動の繰り返し、製品の config（WS270-D1 (b)） | p006、WS256 p018・p019 | 4〜8 | 実機（ユーザーの目視・耳）。QEMU の回帰（GPU なし、riscv64-virt の text login） |
| p008 | 規約の全文の見直し | p002〜p007 | 2〜4 | build warning 0、host 試験 |

**WS269 との分担（M5、Q1 の判断 2026-10-19 q1338）**: WS269 p011 =「Keiland が使う Vulkan の機能の不足を driver 側で埋める（受け入れは機能の試験）」、WS270 p006 =「desktop の起動と目視」。矢印は **WS269 p011 → WS270 p006**。WS269 の design・ws.md の写しは Q1 が直す。

合計（p003〜p008）37〜70 LW ＋ 実機の debug 6〜14（p005 を行わなければ 21〜42）。p003 は今から進められる。p004 は WS256 の実機（p015・p020）と WS269 p007 の前半の後。p006 以降は WS269 p011 の後。

## 4. ユーザーの判断（WS270-D1〜WS270-D5）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| WS270-D1 | desktop の image の config | (a) `config-visionfive2.mk` を desktop の組に広げる（graphical login は p006 まで n）、(b) 製品の別の config（`config/ci/config-visionfive2-desktop.mk`）を足し、今の最小の config を残す | (b)。最小の config は WS256・WS269 の実機の debug に要る。**試験用の config は `plan/ws270/tests/`**（AGENTS.md の「試験の image の作り方」、p002 はそうしている）、**製品の config は `config/ci/`** と分ける。製品の config を足すのは p007（受け入れの app の一覧が決まった後） |
| WS270-D2 | QEMU riscv64-virt の Venus で先に描くか（p005） | (a) 行う、(b) 行わず PowerVR を待つ | **(b)、ただし p003 の後に見直す**。費用（下の (a)〜(f)、16〜28 LW）が版 1 の見積もりより大きい、HAL の実装の補完が要る、TCG の遅さで desktop の起動が READY の時間切れにかかる危険がある、VF2 の copy の道は試されない。p003 で TCG での desktop の userland の起動の時間が分かってから、(a) の価値を測り直す |
| WS270-D3 | VF2 で入れる app の範囲 | (a) rpi4 と同じ全部（browser・videoplayer を含む、noct・emacs を除く）、(b) videoplayer など Vulkan video が要る物を外す | (a)。riscv64 は arch の分岐の else の側（CPU で描く）に落ちる（§1.2）。Vulkan video の queue が無い時の videoplayer の振る舞いを p007 で見て、受け入れの app の一覧を決める |
| WS270-D5 | desktop の root を置く所 | (a) SD の root を大きくする、(b) NVMe の root（WS256 p024）。どちらも `ZEDBSD_ROOTFS_DEVELOPMENT := n` を足せる | (a) を既定、(b) は p024 の後の選択。大きさは p003 の始めに測る。製品の config（WS270-D1 (b)）では `ZEDBSD_ROOTFS_DEVELOPMENT := n` を勧める |

版 1 の D4（Noct）は外した（ID は欠番）: riscv64 の Noct は WS270 の外で Q1 が判断する（M2）。WS270 の desktop は Noct を前提にしない。

WS270-D2 (a) の費用（M3）:
- (a) riscv64 の HAL は user の device の写像を断る（`src/hal/riscv64/space.c:1897-1899`「device pages are not offered to users」で `HAL_SPACE_DEVICE` は UNSUPPORTED。`:1902-1907` の NOCACHE は board の uncached の別名が要り、virt では 0）。Venus の blob の user の写像は `DRV_GPU_MAPPING_DEVICE`（`venus/venus.c:1442`）→ `VM_DEVICE_MMIO`（`gpu.c:3267-3268`）→ `HAL_SPACE_DEVICE|HAL_SPACE_NOCACHE`（`src/kern/vm/device.c:163-164`）なので、HOST_VISIBLE の memory の map が失敗する。**riscv64 の HAL の実装の補完**（user の `HAL_SPACE_DEVICE` と virt の `DEVICE|NOCACHE` を、Svpbmt か PMA の IO の属性で許す）が要る。hal.h の API は変えない（hal.h:750-754「A port may report HAL_ERR_UNSUPPORTED」の範囲）。2〜4 LW（推測）。
- (b) ECAM の host は 32 bit の窓だけを使う（`pci-ecam-fdt.c:294-298`）。QEMU virt の 32 bit の PCI の MMIO の窓は 1 GiB（記憶、未確認）で、既存の Venus の試験は `hostmem=1G`（`plan/tools/guest/venus-hostmem.sh`）。hostmem を小さくする（256 MiB 等）か、64 bit の窓を使う ECAM の変更が要る。2〜6 LW。
- (c) Venus の source は amd64 の list（`platform/amd64/vmunix.mk:142-146`）と `src/kern/platform/pcat.c` の登録だけ。riscv64 の vmunix.mk と `riscv-virt.c` に足す。2〜4 LW。
- (d) virt の Venus の display は venus 自身の node なので、VF2 の copy の道は試されない（得られるのは riscv64 の userland の確かめだけ）。
- (e) riscv64 は TCG。sessiond の READY の時間切れ 30 s を越えると fallback で text に戻る（推測、起動時間は未計測）。
- (f) desktop の PNG を撮る道具: `boot-test.sh` の riscv64-virt は ramfb を撮るが、Keiland は virtio-gpu の head に出る。QMP の `screendump` の head の指定を足すか別の道具が要る。1〜2 LW。

## 5. HAL

hal.h の API の変更はどの Phase でも要らない見込み（kwl_cycles は clock_gettime、HOST_COHERENT は WS269 D7 (a) の HAL の実装、keiland-preview の static は kernel・HAL の変更なし）。**例外: p005（WS270-D2 (a) の時だけ）は riscv64 の HAL の実装の補完（`src/hal/riscv64/space.c` の user の DEVICE・NOCACHE）が要る**。hal.h の契約の範囲なので AGENTS.md では承認は要らないが、p005 を始める前に差分を示す。

## 6. 危険と未確認

- copy の道の性能（§2.3）: U74 の uncached の読みの帯域、1080p の frame の時間は未計測。遅い起動は sessiond の READY の時間切れ 30 s にかかり得る（ws239 の fallback で text に戻る。黙って落ちはしない）。
- desktop の rootfs の大きさは未測定（p003）。
- ws239 の fallback の理由の 1 行が画面に出るか未確認（p003）。
- `service status` の要約の 1 行の有無は未確認（p003）。
- 外部 package（openssl・curl・openssh）の riscv64 での実行と TLS の model は未確認（§1.1）。
- BXE-4-32 の性能で Keiland が実用の fps を出すかは WS269 p010・p011 の後まで分からない。
- 進め方の記録: p002（q1336）は design-reviewer と D の決定の前に main に merge された。範囲は設計の判断に依らない直しに絞った（Q1 の判断、q1338 の queue の行）。試しの config は版 1 では rpi4 の全 program（noct を含む）で D の判断を先取りしていたが、q1338 で noct・emacs を外した。

## レビュー 1 への対応

| 指摘 | 対応 |
| --- | --- |
| B1 描画の道の取り違い | §2 の冒頭に libvulkan の WSI の copy の道と scanout-zedbsd.c（game mode 専用、VF2 では kept に落ちる）を書いた。D を「既存の `gpu-bars`・`display-inventory` が desktop の image で動く事」に縮め、WS269 p007 の確かめと同じ回で見る。§2.2 を「最初の描画は copy の道、import と damage は性能」に書き直し、p006 の依存から WS269 p007 の import を外した |
| M1 §1 の誤りと抜け | §1.1 を p002 の結果に書き直した（二重定義、`--no-relax`、abi_flags、LLVM の target）。keiland-preview は arm64 の static の規則を写した（PT_TLS を持たない static の image、`check-user-elf.py`）。libcurses は platform の規則として足した。p002 の phase.md の「libcurses は toolchain」の誤記を直した |
| M2 D4（Noct） | D4 を外した。VF2 の desktop の config（試し）から noct と、実行時に noct を要る emacs を外した。riscv64 の Noct は WS270 の外で Q1 が判断する |
| M3 D2 の費用 | §4 に (a)〜(f) の費用、p005 の LW を 16〜28 に上げた。§5 に HAL の実装の補完を p005 の例外として書いた。推奨を (b)（p003 の後に見直す）に変えた |
| M4 copy の道の性能 | §2.3 を足した（段 1: 最初の画と測定、段 2: import・damage で実用の fps）。READY の時間切れを §6 |
| M5 WS269 p011 と p006 の持ち主 | Q1 の判断に従った（§3 の表の後）。WS269 の写しの文は Q1 への返却に書く |
| M6 D3 の仕組みと §1.2 | §1.2 を「変更なし（else の側）」に直し、`vkvideo-runtime.c` を足した。WS270-D3 の推奨から `VKVIDEO_SKIP_FAILED` を外した |
| M7 E が既存の fallback を見ていない | E を ws239 の fallback の確かめに直した。READY の時間切れを §2.3・§6 に |
| Minor 1 check-dynamic-elf.py の名前 | §1.3 から「確かめる」を外した |
| Minor 2 `shell/internal.h` | §1.1 で確定（推測を外した） |
| Minor 3 kwl_cycles | §1.1 で閉じた |
| Minor 4 「p007 の前半は済み」 | 前提と §2.1 D を「in-progress、実機の目視は未実施」に直した |
| Minor 5 試験の program | C・D は既存の `hidraw-probe`・`aat-input`・`gpu-bars`・`display-inventory` を再利用。QEMU は PCI の入った virt の config |
| Minor 6 root の大きさ | §1.4 に 64 MiB 固定、走り D（NVMe の native.img）。`ZEDBSD_ROOTFS_DEVELOPMENT := n` を WS270-D5 に |
| Minor 7 B の確かめ方 | §2.1 B に `BOOT_THEN` の `type:`・`expect:` |
| Minor 8 ID の衝突 | WS270-D1 のように前置きを付けた |
| Minor 9 config の置き場所 | WS270-D1 に試験用（`plan/ws270/tests/`）と製品（`config/ci/`）を書き分けた |
| Minor 10 gen-cross-toolchain.sh の注釈 | §1.1 の「残る注意」に記録した。wrapper の注釈を直すかは Q1 の判断に残す（本 Phase では直していない） |
| Minor 11 userland-dynamic.mk の注釈 | 直した（amd64, arm64 and riscv64） |
| Minor 12 進め方 | §6 に Q1 の判断として記録 |
