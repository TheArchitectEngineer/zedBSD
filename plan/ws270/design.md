# WS270 設計: VisionFive 2 で Keiland の desktop を動かす

- 版: 1（2026-10-19 P1、q1335、ws270-p001）。design-reviewer の前。
- 前提: [WS269 design.md](../ws269/design.md)（改訂 3.1、§2・§5・§6・§7）、[WS269 ws.md](../ws269/ws.md) の D5 (a)（2026-10-19 22:04 ユーザー「(a) 画面なしで先行」）、
  [WS256 ws.md](../ws256/ws.md)（p007 の riscv64 の userland、p015 の rootfs、p016・p020 の USB、p018 の LAN、p019 の sound）、
  WS269 p007 の display の node（`src/drivers/gpu/powervr/display.c`: gpu0、display だけの node、render の node は gpu1、import は p006 の後）。
- コードは書かない。下の「確かめた事」は自分の worktree の build（`build/ws270-p001/`、QEMU は起動していない）で見た物。「推測」は確かめていない。

## 1. 今の Keiland と依存、riscv64 での build（受け入れ 1）

### 1.1 試した build

`config/ci/config-visionfive2.mk` の `ZEDBSD_USER_PROGRAMS` を `config-rpi4.mk` の desktop の組（bcm・intel・rtl の firmware を除く）に替えた config で
`make -k -j16 ZEDBSD_CONFIG=build/ws270-p001/config.mk BUILD=build/ws270-p001/out rootfs`（2026-10-19 23:00 頃、rc=2）。

| 見た失敗 | 数 | 原因 |
| --- | --- | --- |
| `powervr-firmware: the firmware license was not accepted` | 1 | 試しの config に license の受け入れが無いだけ（WS269 p003 の手順）。2 回目は外した |
| `No rule to make target …/dynamic/libkeiland.so` ほか libvulkan・libwayland-client・libtruetype・libbrowser・libmedia・libpdf・libEGL・libGL・libGLESv2・libGLX・libwayland-egl・libpng/jpeg/gif/z-compat の `.so`、`bin/keiland-preview` | 19 | **`platform/riscv64/userland.mk` が `platform/common/userland-dynamic.mk` を include していない**。arm64 は `platform/arm64/vmunix.mk:583-587` で `DYNAMIC_SYSROOT`・`DYNAMIC_LINK_CFLAGS`・`DYNAMIC_LINK_LDFLAGS`・`DYNAMIC_ELF_MACHINE` を置いて include する。共有の library の規則が無いので、desktop の program 17 個（calendar・files・…・xserver）が `undefined symbol: kl_*`・`vk*`・`wl_*`（326 行）で link に失敗する |
| `userland/desktop/browser/shell/*.c: 'shell/internal.h' file not found` | 7 | 同じ原因（browser の `-I` は `userland-dynamic.mk` の target ごとの flag）（推測、include の後に消えるかを p002 で確かめる） |
| `userland/desktop/wayland/main.c:1313: invalid output constraint '=a'` | 1 | `kwl_cycles()` が `__aarch64__` 以外を x86 の `rdtsc` とみなす |
| `error: unknown target triple 'unknown'`（openssl・cmake の package） | 約 2,000 | `userland/packages/external.mk:47-49` と `userland/packages/security/openssl/Makefile:29-31` の arch の表に riscv64 が無い。zlib の configure も同じ |
| `userland/base/libpasskey/crypto-openssl.c: 'openssl/core_names.h' not found` | 1 | openssl が出来ないため |
| `No rule to make target …/lib/libcurses.a`・`…/bin/noct` | 2 | curses の static library の規則と Noct の riscv64 の規則が無い（Noct は WS256 p007 の [proposed/noct-riscv64.diff](../ws256/proposed/noct-riscv64.diff)、toolchain の扱い） |

警告は host の perl の locale の物だけ（既存）。

### 1.2 arch の分岐（`userland/desktop` の C、grep `__aarch64__|__x86_64__|__riscv`）

| file | 今 | riscv64 で要る事 |
| --- | --- | --- |
| `wayland/main.c:1300` `kwl_cycles` | aarch64 は `cntvct_el0`、他は `rdtsc` | riscv64 の枝。`rdtime`/`rdcycle` は U-mode では `scounteren` 次第で、`src/hal` に `scounteren` を書く所は無い（grep）。**`clock_gettime(CLOCK_MONOTONIC)` に寄せる**（HAL の変更を避ける）。用途が統計だけかを p002 で確かめる |
| `libvulkan/wsi-swapchain.c:844` `vulkan_wsi_cursor_clock` | aarch64 は counter、他は `swapchain_now`（monotonic） | 変更なしで build できる（他の枝が汎用） |
| `libbrowser/bind/environment.c:36` | `"unknown"` に落ちる | `"riscv64"` を足す（navigator の値だけ） |
| `libkeiland-backend/machine/machine.c:39` | x86 だけ CPUID の brand | 他は汎用の枝。riscv64 の CPU 名（`/dev/system` か FDT の compatible）は後回しでよい |
| `libmedia/vkvideo.c:24`・`videoplayer/videoplayer.h:55` | aarch64 だけ「失敗の skip」「GPU present」 | 意味は「Vulkan video が無い GPU」の扱い。arch でなく device の能力で分けるのが正しいが、範囲外。riscv64 は aarch64 と同じ側（BXE に Vulkan video は無い。推測）に置く案を p002 で決める |

### 1.3 依存の一覧

| 依存 | riscv64 の今 | 要る事 |
| --- | --- | --- |
| 共有の library の graph（`userland-dynamic.mk`） | 無い | p002 で include。`DYNAMIC_LINK_CFLAGS := $(RISCV64_USER_ARCH_FLAGS)`（`-march=rv64gc -mabi=lp64d -mno-relax`、TLS は global-dynamic・trad）、`DYNAMIC_LINK_LDFLAGS := -m elf64lriscv -z max-page-size=4096`、`DYNAMIC_ELF_MACHINE := riscv64`（`check-dynamic-elf.py` の名前は p002 で確かめる） |
| libc・rtld・TLS | WS256 p007 で済み（dynamic、TLS は全部 dynamic） | なし |
| Wayland（`libwayland`・`wayland` の compositor） | 上の graph 待ち | `kwl_cycles` |
| libvulkan（Venus の client） | 上の graph 待ち | arch に依らない（推測）。device は `/dev/gpu*` の render の node（WS269 p006 以降の gpu1） |
| font（`fonts`・`noto-color-emoji`・`hand-hershey`） | data だけ | rootfs の大きさ（§1.4） |
| input（`/dev/input`、usb-hid） | WS256 p016（virt、cleared）・p020（実機、待ち） | なし（kernel 側） |
| seat・session（`sessiond`、`/dev/system`） | 上の graph 待ち | なし（推測） |
| audio（`audiod`） | WS256 p019（PWMDAC、実機待ち） | なし |
| openssl・zlib・curl・openssh（外部 package） | triple が無い | `external.mk` と openssl の表に `riscv64-unknown-zedbsd`・`zedbsd-riscv64`（openssl の `Configurations` の追加は package の patch。toolchain ではない） |
| Noct（`bin/noct`、`make-*.noct` の道具と userland の noct） | 案のまま | **toolchain**（AGENTS.md: `userland/base/noct/` の patch と build の規則）。Q1 の許可で proposed の diff を適用（D4） |
| libcurses | static の規則が無い | p002 で riscv64 の規則（他の arch の写し） |

### 1.4 他の port との比べ

| | amd64 | rpi4（arm64） | VF2（riscv64、今） |
| --- | --- | --- | --- |
| config | `config-amd64.mk` | `config-rpi4.mk`: `ZEDBSD_GRAPHICAL_LOGIN := y`・`ZEDBSD_NOCT_ACCEL := y`、desktop の program 全部 | `config-visionfive2.mk`: login に要る 21 個と `powervr-firmware`、graphical login は n |
| 共有の library | `platform/amd64/vmunix.mk` が include | `platform/arm64/vmunix.mk` が include | 無い |
| GPU の描画 | Venus（virtio）・i915・nvrtx・amdrdna | bcm2711（Venus の decode） | WS269 p010 まで無い |
| display | 各 driver | bcm2711 の HVS | powervr の display の node（gpu0、boot framebuffer、copy の道） |
| rootfs | 大きな root | SD の root | p007 の `root.img` は 64 MiB の UFS、native は `RISCV64_NATIVE_ROOT_MIB` |

desktop の rootfs は 64 MiB に入らない（rpi4 の rootfs の大きさは p002 で `du` で測る）。`RISCV64_NATIVE_ROOT_MIB` の値と SD・NVMe の root（WS256 p015・p024）で足りるかを p002 で確かめる。

## 2. 画面なしで先行する範囲と、描画を始める条件（受け入れ 2）

前提: Keiland は Vulkan でしか描かない（WS269 design §2・§5 C）。powervr の render の node と Venus の decode は WS269 p006・p009・p010。D5 (a) により、WS269 p010 までは描画を含まない。

### 2.1 WS269 p010 までに進める範囲

| 項目 | 中身 | 依存 | 確かめ（画面の 1 行か試験の要約の 1 行。serial・console の log では判定しない） |
| --- | --- | --- | --- |
| A. userland の build | §1 の穴を全部埋め、desktop の program と library を riscv64 で build、image に入れる | WS256 p007、D4（Noct） | build warning 0、`check-dynamic-elf.py` が全部通る |
| B. service の起動 | `sessiond`・`audiod`・`volumed`・`dhcpc` などの service が riscv64 で起きる | A | 画面の text login の後、`service status` の要約の行（T1、QEMU riscv64-virt） |
| C. seat・input | `/dev/input` の keyboard・mouse の event を desktop の側の library（libkeiland-backend-zedbsd の `input-zedbsd.c`）で読める | A、WS256 p016（virt）・p020（実機） | 小さな試験の program（`userland/tests/` に置く、input の event を 1 件ごとに画面に 1 行）。QEMU は `-device usb-kbd -device usb-mouse`、実機はユーザーの目視 |
| D. display の claim | `scanout-zedbsd.c` の open（`/dev/gpu0`、`GPU_DISPLAY_*`）が powervr の display の node を claim・release できる。copy の道（`output_constraints` が copy だけ）で色の resource を present | A、WS269 p007（前半は済み） | 試験の program が claim → 色 → release → console に戻る、をユーザーが目で見る（実機だけ。WS269 p007 の確かめと同じ物は重ねない） |
| E. graphical login の道 | `ZEDBSD_GRAPHICAL_LOGIN := y` で、render の device が無い時に compositor が短い 1 行の理由を出して text login に戻る（落ちて黙らない） | A・B・D | 画面に理由の 1 行と text login（QEMU と実機） |

E が要るのは、p010 の前に graphical login を有効にした image を作ると、Vulkan の device が無いので compositor が起きないため。今の compositor が render の device の無い時にどう振る舞うかは未確認（p003 で QEMU で見る）。

### 2.2 描画を始める条件

1. WS269 p010 が cleared（`vkdemo`・`venus-frame` の画が実機で出る）。
2. WS269 p007 の import の部分（render の buffer の scanout、p006 の後）が cleared。それまでの present は copy の道で、compositor が copy の道を受け付けるかを p005 で確かめる（`scanout-zedbsd.c` は `GPU_DISPLAY_BLOB` の共有の image を要る。display.c は「copied route only」を返す。**compositor が copy の道で動かない時は import を待つ**）。
3. 本 WS の p002〜p004 が cleared。

### 2.3 QEMU で描画を先に確かめる道（D2）

riscv64-virt で `CONFIG_DRIVER_PCI_VENUS` を有効にし、host の Venus の renderer（amd64 の試験で使っている物）で Keiland を描けば、PowerVR を待たずに riscv64 の desktop の userland 全体（Vulkan の client・compositor・app）を確かめられる。pci-venus の driver が riscv64 で build・動作するか（DMA の coherency、BAR の写像、PCI ECAM の INTx は WS256 p016・p021 で済み）は未確認。GPU を使う確かめは T1 の QEMU で、回帰は GPU なし（AGENTS.md）。

## 3. 実装の Phase の分割（受け入れ 3）

LW は粗い（8 LW ≈ 実時間 2〜3 h）。

| Phase | 中身 | 依存 | LW | 確かめ |
| --- | --- | --- | --- | --- |
| p002 | riscv64 の desktop の build（§2.1 A）: `platform/riscv64/userland.mk` に `userland-dynamic.mk`、libcurses、`kwl_cycles` などの arch の分岐（§1.2）、外部 package の triple（external.mk・openssl・zlib）、desktop の config（D1）、rootfs の大きさ | WS256 p007、D1、D4（Noct は Q1 の適用） | 8〜14 | build warning 0、`check-dynamic-elf.py`、amd64・rpi4 の build が変わらない事（build だけ） |
| p003 | QEMU riscv64-virt の画面なしの確かめ（§2.1 B・C・E）: service、input の試験の program、graphical login の失敗の道 | p002 | 4〜8 | T1（QEMU riscv64-virt、`boot-test.sh` の PNG で text login と試験の 1 行） |
| p004 | VF2 の実機の画面なしの確かめ（§2.1 C・D・E）: desktop の image を SD（か NVMe）に、USB keyboard・mouse の event、display の claim、graphical login の失敗の道 | p003、WS256 p015・p020、WS269 p007（前半） | 3〜6 ＋ 実機の debug 2〜6 | 実機（ユーザーの目視、Q1 の「ユーザーが選べる試験の一覧」） |
| p005 | （D2 (a) の時）QEMU riscv64-virt の Venus で Keiland を描く: pci-venus の riscv64 の build と動作、Keiland の起動 | p003、D2 | 8〜16 | T1（QEMU riscv64-virt ＋ host の Venus、desktop の PNG） |
| p006 | VF2 で Keiland を描く: compositor の起動、scanout（copy の道か import）、性能の目安（解像度・fps）、WS269 p011 と合わせる | WS269 p010・p007 の import、p004 | 8〜16 ＋ 実機の debug 4〜8 | 実機（ユーザーが desktop を目で見る） |
| p007 | desktop の受け入れ: USB keyboard・mouse、LAN（WS256 p018）、sound（WS256 p019）、app の起動の一覧（D3）、起動の繰り返し | p006、WS256 p018・p019 | 4〜8 | 実機（ユーザーの目視・耳）。QEMU の回帰（GPU なし、riscv64-virt の text login） |
| p008 | 規約の全文の見直し | p002〜p007 | 2〜4 | build warning 0、host 試験 |

合計 37〜72 LW ＋ 実機の debug 6〜14。p002・p003 は今から進められる（板も GPU も要らない）。p004 は WS256 の実機（p015・p020）の後。p006 以降は WS269 p010 の後。

## 4. ユーザーの判断（D1〜D5）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | desktop の image の config | (a) `config-visionfive2.mk` を desktop の組に広げる（graphical login は p006 まで n）、(b) 別の `config-visionfive2-desktop.mk` を足し、今の最小の config を残す | (b)。最小の config は WS256・WS269 の実機の debug に要り、desktop の build の失敗に巻き込まれない |
| D2 | QEMU riscv64-virt の Venus で先に描くか（p005） | (a) 行う、(b) 行わず PowerVR を待つ | (a)。PowerVR（WS269 p010、60〜100 LW）と独立に riscv64 の desktop の userland の不具合を先に出せる。ただし pci-venus の riscv64 は未確認で、動かなければ p005 を uncleared で止める |
| D3 | VF2 で入れる app の範囲 | (a) rpi4 と同じ全部（browser・videoplayer を含む）、(b) videoplayer など Vulkan video が要る物を外す | (a) で build し、Vulkan video の無い時の skip（`VKVIDEO_SKIP_FAILED` の側）を使う。受け入れの app の一覧は p007 で決める |
| D4 | Noct の riscv64（WS256 p007 の proposed/noct-riscv64.diff）の適用 | (a) Q1 が toolchain の手順（`toolchain-lock.sh`）で適用、(b) Noct を VF2 の image から外す | (a)。desktop の image は Noct（`ZEDBSD_NOCT_ACCEL` と道具）を前提にしている（rpi4 は y）。static な Noct の PT_TLS の危険（WS256 p007 の注記）は p002 で確かめる |
| D5 | desktop の root を置く所 | (a) SD の root を大きくする、(b) NVMe の root（WS256 p024） | (a) を既定、(b) は p024 の後の選択。sizes は p002 で測る |

HAL の API の変更は要らない見込み（`kwl_cycles` は `clock_gettime` に寄せる）。要ると分かった時は proposed/ に差分を置くだけにする。

## 5. 危険と未確認

- `userland-dynamic.mk` の include の後にどれだけの link の失敗が残るか（riscv64 の relocation の未対応、`-mno-relax` と共有の library、`Bsymbolic-functions`）は未確認。p002 の LW の上側はこれ。
- compositor の render の device の無い時の振る舞い（§2.1 E）、copy の道の scanout を compositor が受け付けるか（§2.2）は未確認。
- BXE-4-32 の性能で Keiland が実用の fps を出すかは WS269 p010 の後まで分からない。
- 外部 package の openssl の riscv64 の `Configurations` の行（`no-asm` で足りるか）は未確認。
