# WS105 設計: Keiland を Linux で動かす（`/opt/keiland`）

2026-10-01 に Q1 が、ユーザーとの検討（[ws.md](ws.md) の「決定と理由」D1〜D23）と code の調査から書いた。**決定の理由は ws.md に、仕組みはこの文書にある。**
code の行番号は 2026-10-01 の main（`cad93f04` の後）の物で、WS104 の後は変わっている。WS104 が作る境界（`zwl-gpu.h`・`zwl-input.h`・`zwl-os.h`・
`libkeiland/zedbsd/`・`userland/desktop/paths.h`・`userland/desktop/keiland/`）を前提にする。

## 0. 読み方と用語

- **この WS の agent は、Phase を始める前に ws.md の「決定と理由」とこの文書の §1・§2 を必ず読む。** libvulkan-compat を触る Phase（p003〜p005）は §4 の全部を読む。
- 「zedBSD の」と「Linux の」を必ず区別する。同じ名前の物が二つある:

| 名前 | zedBSD | Linux |
| --- | --- | --- |
| libvulkan | `userland/desktop/libvulkan/`: Venus と i915 の **driver そのもの** | `userland/desktop/libvulkan-compat/`: **WSI だけを持ち、他は system の libvulkan に渡す** library（§4）。install の名前は `libvulkan.so.1` |
| GPU の buffer の protocol | `keiland_gpu_buffer_v1`（kernel の image の capability と記述） | `zwp_linux_dmabuf_v1`（Linux の標準。dma-buf と modifier） |
| fence | `keiland_gpu_buffer_v1.set_acquire_fence`（世代つきの fence の fd） | implicit sync（dma-buf に付いた fence。sync_file の ioctl で出し入れ） |
| 画面 | zedBSD の libvulkan の VK_KHR_display（kernel の display の ioctl） | libvulkan-compat の VK_KHR_display（Linux の KMS の ioctl、複写の道） |
| 入力 | `/dev/input/eventN`（evdev と同じ API）、sessiond が持ち主を変える | `/dev/input/eventN`（evdev）、root か logind の `TakeDevice` |
| session | sessiond（greeter・login・Log Out・Shut Down） | gdm（または text console から root で起動） |
| network・音 | networkd・audiod（libkeiland の `zedbsd/`） | wpa_supplicant・ALSA（libkeiland の `wpa/`・`linux/`） |

用語:

- **後段（backend）**: libvulkan-compat が chain する先の system の `libvulkan.so.1`（Debian では `/usr/lib/x86_64-linux-gnu/libvulkan.so.1`、Khronos の loader）。
- **ICD**: Khronos の loader が読む driver（Mesa の lavapipe・anv・radv・venus など）。
- **WSI**: Vulkan の画面に出す部分（`VK_KHR_surface`・`VK_KHR_swapchain`・`VK_KHR_wayland_surface`・`VK_KHR_display`）。
- **dma-buf**: Linux の kernel の、device の間・process の間で共有できる buffer の fd。
- **modifier**: dma-buf の画素の並べ方（tiling）を表す 64 bit の値。`DRM_FORMAT_MOD_LINEAR`（0）は行の順の素直な並び。
- **sync_file**: Linux の kernel の fence の fd。`poll` で readable になったら済み。
- **implicit sync**: fence を protocol で送らず、dma-buf 自体に付けておく方法。`DMA_BUF_IOCTL_IMPORT_SYNC_FILE` で付け、`DMA_BUF_IOCTL_EXPORT_SYNC_FILE` で取り出す（Linux 6.0 から）。
- **KMS**: Linux の kernel の画面の設定と表示の ioctl（DRM の一部）。**DRM master** は KMS の操作をしてよい 1 つの fd。
- **logind**: systemd の session の管理。gdm の下の session の process に、DRM と入力の device の fd を `TakeDevice` で渡す（D-Bus）。
- **seat の backend**: compositor の中で、DRM の master の fd と入力の device の fd を得る部分（`seat-direct`: root で直接 open、`seat-logind`: logind から）。

## 1. 全体の形

```
                       zedBSD（今、WS104 の後）                         Linux（WS105 の後）
 app        /bin/terminal など                                  /opt/keiland/bin/terminal など（同じ source、Makefile.linux）
 library    /lib/libkeiland.so（zedbsd/ の backend）            /opt/keiland/lib/libkeiland.so（wpa/・linux/ の backend）
            /lib/libwayland-client.so（我々の）                 /opt/keiland/lib/libwayland-client.so（同じ source）
            /lib/libvulkan.so（Venus・i915 の driver）          /opt/keiland/lib/libvulkan.so.1（libvulkan-compat）→ /usr/lib/.../libvulkan.so.1（system）→ Mesa
 compositor /bin/wayland（zedbsd/ の module）                   /opt/keiland/bin/wayland（linux/ の module）
 起動       sessiond → greeter → session                         gdm → keiland.desktop → /opt/keiland/bin/wayland（または text console から root）
```

- **同じ source を zedBSD と Linux で build する。** OS で違う所は `<package>/zedbsd/`・`<package>/linux/`・`<package>/wpa/` の file の入れ替えで、
  macro の block は `wayland/zwl-evdev.h` の 1 つ（と install の path の `-D`）だけ。
- Linux の build は zedBSD の build（cross の toolchain・sysroot・image）と完全に別で（D2）、host の compiler（既定 `cc`）と host の header（glibc・
  linux-libc-dev・`/usr/include/vulkan`）を使う。
- install は全て `/opt/keiland/` の下（D1）。例外は gdm に見せる `/usr/share/wayland-sessions/keiland.desktop` の 1 file（D11、p009、`make keiland-linux-install-session`）。

## 2. install の配置（`/opt/keiland`、`KEILAND_PREFIX` で変えられる）

| directory | 中身 |
| --- | --- |
| `bin/` | `wayland`（compositor）、`terminal`・`files`・`settings`・`notes`・`textedit`・`imageview`・`pdfviewer`・`kuidemo`・`mview`・`vkdemo`・`wltest`・`wlshm` |
| `libexec/` | `keiland-ime` |
| `lib/` | `libwayland-client.so`・`libkeiland.so`・`libkeiui.so`・`libtruetype.so`・`libpdf.so`・`libz-compat.so`・`libpng-compat.so`・`libjpeg-compat.so`・`libgif-compat.so`・`libvulkan.so.1`（と build の時だけ使う link `libvulkan.so`） |
| `share/fonts/` | `keiland.ttf`・`keiland-mono.ttf`・`keiland-fallback.ttf`・`keiland-emoji.ttf`（zedBSD の `KEILAND_FONT_DATA` と同じ対応） |
| `share/licenses/keiland-fonts/` | font の license |
| `share/keiland/` | `wallpaper.ppm`・`wallpapers/`（`userland/desktop/wallpapers/generate.py` で作る）、IME の辞書など zedBSD の `/usr/share/keiland` にある物 |
| `etc/keiland/` | `apps.conf`・`desktop`・`open-with` など（zedBSD の `/etc/keiland` と同じ物があれば） |
| `etc/vulkan-backend` | 後段の path を変えたいときだけ（§4.3。既定では作らない） |

- SONAME は zedBSD と同じ版の番号の無い名前（`libkeiland.so` など）。system の library（`libz.so.1` など）と名前が重ならない。**例外は `libvulkan.so.1`**
  （普通の Vulkan の app と同じ名前で見つかるように。§4）。
- 全ての program と library に `RUNPATH=/opt/keiland/lib`（`-Wl,-rpath,$(KEILAND_PREFIX)/lib -Wl,--enable-new-dtags`）。

## 3. build（`make keiland-linux`、D1・D2）

### 3.1 入口（top-level の `Makefile`）

- `keiland-linux`・`keiland-linux-install`・`keiland-linux-install-session`・`keiland-linux-clean` を足す。中身は
  `$(MAKE) -f userland/desktop/keiland-linux.mk <goal>` を呼ぶだけ。
- これらを `ZEDBSD_CONFIG_OPTIONAL_GOALS`（`Makefile:44-53`）に足す（`config.mk` 無しで走れるように）。`help` にも 1 行ずつ足す。
- top-level の `Makefile` は `userland/*/*/Makefile` を全て include する（`Makefile:260-264`）が、`Makefile.linux` という名前は include しない。**Linux の build の file を
  `Makefile` という名前にしてはいけない。**

### 3.2 `userland/desktop/keiland-linux.mk`（Linux の build の本体）

zedBSD の `ZEDBSD_USERLAND_PACKAGE` を使わない、独立した GNU make の file。

| 変数 | 既定 | 意味 |
| --- | --- | --- |
| `KEILAND_LINUX_BUILD` | `build/keiland-linux` | 出力の directory（`obj/`・`lib/`・`bin/`・`libexec/`・`include/`・`share/`・`gen/`） |
| `KEILAND_PREFIX` | `/opt/keiland` | install の先 |
| `DESTDIR` | 空 | install の時の前置き（試験では `build/keiland-linux/stage`） |
| `CC` | `cc` | compiler（`CC=clang` でも通すこと） |
| `KEILAND_LINUX_OPT` | `-O2 -g` | 最適化 |
| `KEILAND_LINUX_MULTIARCH` | `$(shell $(CC) -print-multiarch)` | 後段の既定の path（§4.3） |

compile の flag（全ての source）:

```
$(CC) $(KEILAND_LINUX_OPT) -std=gnu17 -Wall -Wextra -Werror -fPIC -D_GNU_SOURCE \
  -DKEILAND_BINDIR='"$(KEILAND_PREFIX)/bin"' -DKEILAND_LIBEXECDIR='"$(KEILAND_PREFIX)/libexec"' \
  -DKEILAND_DATADIR='"$(KEILAND_PREFIX)/share"' -DKEILAND_SYSCONFDIR='"$(KEILAND_PREFIX)/etc"' \
  -I. -Iuserland/desktop/keiland -I$(KEILAND_LINUX_BUILD)/include \
  -MMD -MP -c SRC -o $(KEILAND_LINUX_BUILD)/obj/SRC.o
```

- `-I.`（repo の root）: tree の慣習の `#include "userland/..."` のため。
- `-Iuserland/desktop/keiland`: desktop の公開の header（WS104 p001 で移した物）。
- `-I$(KEILAND_LINUX_BUILD)/include`: zedBSD の libc の側にあって、Linux の build にも要る header の**複写**（build の時に写す）:
  `include/libc/compat/`（`compat/zlib/zlib.h` など）、`include/libc/pdf.h`、`include/libc/sha2.h`・`include/libc/md5.h`（glibc に無い OpenBSD の digest の API）。
  **`include/libc` を `-I` に入れてはいけない**（glibc と衝突する）。
- Vulkan・DRM・dma-buf・evdev・ALSA の header は host の物（`/usr/include/vulkan`、linux-libc-dev の `/usr/include/drm`・`linux/`・`sound/`）。

link:

- library: `$(CC) -shared -Wl,-soname,<SONAME> -Wl,-z,defs -Wl,--version-script=<package>/exports.map -Wl,-rpath,$(KEILAND_PREFIX)/lib -Wl,--enable-new-dtags OBJS -L$(KEILAND_LINUX_BUILD)/lib -l:<依存の SONAME> ... <system の library（-lm -lpthread -ldl など）>`
- program: `$(CC) -pie OBJS -Wl,-rpath,$(KEILAND_PREFIX)/lib -Wl,--enable-new-dtags -L$(KEILAND_LINUX_BUILD)/lib -l:<依存の SONAME> ... <system の library>`
- 依存の我々の library は `-l:libkeiland.so` の形で名前を固定する（system の同名の物を拾わないように）。

macro（`keiland-linux.mk` が定義し、各 `Makefile.linux` が呼ぶ）:

```make
# $(1) name, $(2) SONAME, $(3) sources, $(4) our libraries it needs (SONAMEs), $(5) system libraries (-lm ...), $(6) exports.map or empty
$(eval $(call KEILAND_LINUX_LIBRARY,keiland,libkeiland.so,$(LIBKEILAND_LINUX_SOURCES),libwayland-client.so libtruetype.so,-lm,userland/desktop/libkeiland/exports.map))
# $(1) name, $(2) bin or libexec, $(3) sources, $(4) our libraries, $(5) system libraries
$(eval $(call KEILAND_LINUX_PROGRAM,terminal,bin,$(TERMINAL_LINUX_SOURCES),libkeiui.so libkeiland.so ...,-lm))
# $(1) path under the prefix, $(2) source file
$(eval $(call KEILAND_LINUX_DATA,share/fonts/keiland.ttf,userland/desktop/fonts/Inter.ttf))
```

- `keiland-linux.mk` は `KEILAND_LINUX_PACKAGES`（`Makefile.linux` の path の並び。依存の順）を include する。新しい package は Phase ごとにここへ足す。
- `install`: `lib/`・`bin/`・`libexec/`・data を `$(DESTDIR)$(KEILAND_PREFIX)/` の下に `install -m` で写す（library は 0755、data は 0644）。
- emoji の font: `build/distfiles/NotoColorEmoji-2.047.ttf`（zedBSD の package の取得物）があれば使い、無ければ `userland/packages/fonts/noto-color-emoji/Makefile` の
  URL から取って、同じ file の SHA-256 の値で確かめる（`curl` と `sha256sum`）。
- wallpaper: `python3 userland/desktop/wallpapers/generate.py $(KEILAND_LINUX_BUILD)/share/keiland/wallpapers`（zedBSD の image の作り方と同じ引数。WS089 の build の script を見て合わせる）。

### 3.3 `Makefile.linux`（package ごと）

```make
# userland/desktop/libkeiland/Makefile.linux -- the Linux build of libkeiland (WS105).  The zedBSD build is Makefile.
# keiland-linux-sync: skip userland/desktop/libkeiland/zedbsd/*  (the zedBSD backend)
LIBKEILAND_LINUX_SOURCES := userland/desktop/libkeiland/version.c ... userland/desktop/libkeiland/wpa/network-wpa.c ...
$(eval $(call KEILAND_LINUX_LIBRARY,keiland,libkeiland.so,$(LIBKEILAND_LINUX_SOURCES),libwayland-client.so libtruetype.so,-lm,userland/desktop/libkeiland/exports.map))
```

- source の一覧は `Makefile` と別に持つ（D2）。ずれは `plan/tools/keiland-linux/makefile-sync.sh` が見つける: `Makefile` の `userland/...*.c` のうち、`zedbsd/` の下でなく、
  `Makefile.linux` に無く、`# keiland-linux-sync: skip <path or glob> <理由>` の行にも無い物があれば FAIL。逆向き（`Makefile.linux` だけにある物）は `linux/`・`wpa/` の下か、
  `# keiland-linux-sync: only <path> <理由>` の行があること。

### 3.4 Linux だけの小さな部品

- `userland/desktop/linux-compat/`（Linux の build だけ）: glibc に無い OpenBSD の digest（`sha2.h`・`md5.h`）を、zedBSD の libc の source
  （`src/libc/openbsd-sha2.c`・`openbsd-digest.c`）を compile して static な library `libkeiland-compat.a` にする。`Makefile.linux` だけを持つ。
  使う物: libpdf・files・notes。source がそのまま glibc で compile できない所があれば、`linux-compat/` に最小の差し替えを置く（src/libc は変えない）。

## 4. libvulkan-compat（Linux の app が使う libvulkan）

**この節が WS105 で一番わかりにくい仕組みである。** 実装する前に全部を読むこと。迷ったら §4.2 の図に戻る。

### 4.1 一言で

`/opt/keiland/lib/libvulkan.so.1` は、**WSI（画面に出す部分）だけを自分で実装し、それ以外の Vulkan の全ての関数を、system の
`libvulkan.so.1`（後段、backend）にそのまま渡す library** である。後段は Khronos の loader（Mesa の ICD を読む）でも、組み込みの
ベンダーの単体の libvulkan（Mali など）でもよい。描画・compute・memory は全て後段の driver が行う。

zedBSD の `userland/desktop/libvulkan/` は Venus・i915 の driver そのもの（Vulkan の全てを自分で実装する）なので、**全く別の物**である。
source は共有しない（D7）。名前の似た `libvulkan` と `libvulkan-compat` を取り違えないこと。

### 4.2 図

```
 app（Terminal・vkdemo・compositor など、/opt/keiland/bin/*）
   │  DT_NEEDED libvulkan.so.1、RUNPATH /opt/keiland/lib
   ▼
 /opt/keiland/lib/libvulkan.so.1  ＝ libvulkan-compat（我々）
   │   ├ WSI: VK_KHR_surface・VK_KHR_wayland_surface・VK_KHR_swapchain・VK_KHR_display・VK_EXT_acquire_drm_display を自分で実装
   │   │        → Wayland（我々の libwayland-client）で zwp_linux_dmabuf_v1 を話す / KMS の ioctl で画面に出す
   │   └ それ以外: 後段の関数の pointer を呼ぶだけ
   │  dlopen("/usr/lib/x86_64-linux-gnu/libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)（絶対 path）
   ▼
 後段 /usr/lib/<triplet>/libvulkan.so.1（Khronos の loader、またはベンダーの libvulkan）
   │
   ▼
 ICD（Mesa の lavapipe・anv・radv・venus、ベンダーの driver）→ GPU
```

- app は普通の Vulkan の app と同じく `libvulkan.so.1` に link する。`RUNPATH` が `/opt/keiland/lib` なので、動的 linker は我々の物を先に見つける。
- 我々の物は後段を**絶対 path で** `dlopen` する。名前（`libvulkan.so.1`）で開くと自分自身が返るので、絶対に名前で開かない。
- 後段の WSI（後段の `VK_KHR_wayland_surface` など）は**使わない**。理由: ベンダーの libvulkan は Wayland の WSI を持たないことや、
  別の版の libwayland を前提にすることがある。WSI を我々が持てば、後段は「描画できる Vulkan」でさえあればよい（D5）。

### 4.3 後段の見つけ方（`backend.c`）

順に試し、最初に開けた物を使う。

1. 環境変数 `KEILAND_VULKAN_BACKEND`（絶対 path）。
2. file `/opt/keiland/etc/vulkan-backend` の 1 行目（絶対 path）。prefix は build の `KEILAND_PREFIX`（§3）。
3. build の時に決めた既定の一覧: `/usr/lib/<multiarch>/libvulkan.so.1`（`<multiarch>` は `$(CC) -print-multiarch` の値。空なら省く）、
   `/usr/lib64/libvulkan.so.1`、`/usr/lib/libvulkan.so.1`。

開いた後に必ず確かめる:

- `dlsym(handle, "vkGetInstanceProcAddr")` が、我々自身の `vkGetInstanceProcAddr` の address と**違う**こと（`realpath` が自分の file と同じ path も断る）。
  同じなら自分を開いている。error を返す（`VK_ERROR_INITIALIZATION_FAILED`）。
- 見つからない・確かめに失敗したときは、`vkCreateInstance`・`vkEnumerate*` が `VK_ERROR_INCOMPATIBLE_DRIVER` を返し、stderr に
  `libvulkan-compat: no backend libvulkan (KEILAND_VULKAN_BACKEND, ...)` を 1 回出す。

後段は process の中で 1 回だけ開き（`pthread_once`）、閉じない。

### 4.4 関数の 3 つの種類

Vulkan の関数は全て、次のどれかに入る。**どれに入るかの一覧は `functions.tsv` の 1 か所で持ち、道具で `forward.inc`（§4.10）を作る。**

| 種類 | 何をするか | 例 |
| --- | --- | --- |
| **F: 素通し（forward）** | 我々の export した関数は、後段の同じ名前の関数の pointer を呼ぶだけ。引数も戻り値も触らない | `vkCmdDraw`・`vkQueueSubmit`・`vkCreateImage`・`vkAllocateMemory` などほぼ全部 |
| **I: 横取り（intercept）** | 後段に渡す前後で手を入れる | `vkCreateInstance`・`vkDestroyInstance`・`vkCreateDevice`・`vkDestroyDevice`・`vkEnumerateInstanceExtensionProperties`・`vkEnumerateDeviceExtensionProperties`・`vkGetInstanceProcAddr`・`vkGetDeviceProcAddr`・`vkGetDeviceQueue`・`vkGetDeviceQueue2` |
| **O: 自前（own）** | 後段に渡さず、我々が全部実装する（WSI） | `vkCreateWaylandSurfaceKHR`・`vkDestroySurfaceKHR`・`vkGetPhysicalDeviceSurface*`・`vkGetPhysicalDeviceWaylandPresentationSupportKHR`・`vkCreateSwapchainKHR`・`vkDestroySwapchainKHR`・`vkGetSwapchainImagesKHR`・`vkAcquireNextImageKHR`・`vkAcquireNextImage2KHR`・`vkQueuePresentKHR`・VK_KHR_display の全部・`vkGetDrmDisplayEXT`・`vkAcquireDrmDisplayEXT`・`vkReleaseDisplayEXT` |

**F の素通しの仕組み**（ここが「包まない」の意味）:

- Vulkan の dispatchable な handle（`VkInstance`・`VkPhysicalDevice`・`VkDevice`・`VkQueue`・`VkCommandBuffer`）は、**後段が返した値をそのまま app に返す**。
  我々の struct で包まない（wrap しない）。
- 我々が export する F の関数（例 `vkCmdDraw`）は、**後段が export している同じ名前の関数**（`dlsym(backend, "vkCmdDraw")`）を呼ぶ。
  後段の export の関数は、後段自身の trampoline であり、handle から正しい driver の関数を自分で選ぶ。我々は選ばなくてよい。
- だから F の関数には instance・device ごとの表が要らない。process に 1 つの表（`static PFN_vkCmdDraw next_vkCmdDraw` の集まり）を、
  後段を開いたときに `dlsym` で埋める。後段が export していない名前（拡張の関数）は F の export を持たない（app は `vkGet*ProcAddr` で得る）。
- 結果として、**WSI 以外の Vulkan の動きは後段と全く同じ**になる。app のバグや driver の差を我々が吸収することは無い。
- 後段が export していない core の関数（例: Vulkan 1.1 の後段に 1.3 の関数）を app が我々の export から呼んだときは、stderr に
  `libvulkan-compat: the backend has no vkXxx` を出して `abort()` する（版を確かめずに呼ぶ app の誤り。void の関数は error を返せないため）。

**I の横取りの内容**（§4.6・§4.7 で詳しく）:

- 拡張の一覧から後段の WSI の拡張を消し、我々の WSI の拡張を足す。
- instance・device の作成で、app が求めた我々の WSI の拡張を後段に渡さず（後段は知らない）、我々の WSI が内部で要る拡張を後段に足す。
- `vkGet*ProcAddr` で、O の関数の名前には我々の関数を、それ以外には後段の答えを返す。
- `vkGetDeviceQueue*` で、`VkQueue` → `VkDevice` の対応を覚える（`vkQueuePresentKHR` は queue しか受け取らないため）。

### 4.5 我々が持つ記録（handle を包まない代わり）

| 記録 | key | 中身 | いつ作り、いつ消すか |
| --- | --- | --- | --- |
| instance の記録 | 後段の `VkInstance` の値 | 後段の `vkGetInstanceProcAddr` で得た instance の関数の表（WSI が内部で使う物: `vkGetPhysicalDeviceFormatProperties2`・`vkGetPhysicalDeviceImageFormatProperties2`・`vkGetPhysicalDeviceMemoryProperties` など）、app が有効にした我々の WSI の拡張 | `vkCreateInstance` の成功で作り、`vkDestroyInstance` で消す |
| device の記録 | 後段の `VkDevice` の値 | 後段の `vkGetDeviceProcAddr` で得た device の関数の表（WSI が使う: `vkCreateImage`・`vkAllocateMemory`・`vkGetMemoryFdKHR`・`vkGetImageDrmFormatModifierPropertiesEXT`・`vkQueueSubmit`・`vkCreateSemaphore`・`vkGetSemaphoreFdKHR`・`vkImportSemaphoreFdKHR`・`vkCreateFence`・`vkWaitForFences` など）、physical device、我々が足した拡張が有効か | `vkCreateDevice` の成功で作り、`vkDestroyDevice` で消す |
| queue の記録 | 後段の `VkQueue` の値 | 属する device の記録、queue family の番号 | `vkGetDeviceQueue*` で作る。device とともに消す |
| surface | 我々の `VkSurfaceKHR`（我々の struct の pointer を `uint64_t` にした値） | `wl_display *`・`wl_surface *`、または display の plane と mode | `vkCreate*SurfaceKHR`〜`vkDestroySurfaceKHR` |
| swapchain | 我々の `VkSwapchainKHR` | §4.8・§4.9 | `vkCreateSwapchainKHR`〜`vkDestroySwapchainKHR` |
| display・mode | 我々の `VkDisplayKHR`・`VkDisplayModeKHR` | DRM の connector・CRTC・mode | §4.9 |

- 記録は小さな配列か連結 list に持ち、`pthread_mutex` で守る。数は少ない（instance・device は普通 1 つ）。
- `VkSurfaceKHR`・`VkSwapchainKHR`・`VkDisplayKHR`・`VkDisplayModeKHR` は non-dispatchable な handle なので、我々の値を返してよい。
  **後段にこれらの handle を渡してはいけない**（後段は知らない）。app が pNext で渡すことがある構造体（`VkImageSwapchainCreateInfoKHR`・
  `VkBindImageMemorySwapchainInfoKHR`）は、それを要する拡張（device group の swapchain）を我々が名乗らないので来ない前提にする。
  来たら（pNext の sType で見つけたら）`vkCreateImage`・`vkBindImageMemory2` を横取りする必要があるが、WS105 では名乗らない（§4.11）。

### 4.6 instance と device の作成（I の横取りの細部）

**我々の WSI の拡張**（我々が名乗る物）:

- instance: `VK_KHR_surface`、`VK_KHR_wayland_surface`、`VK_KHR_display`、`VK_EXT_direct_mode_display`、`VK_EXT_acquire_drm_display`
  （後の 2 つは compositor が DRM の fd を渡すため。`VK_EXT_acquire_drm_display` は `VK_EXT_direct_mode_display` を前提にする）。
  `VK_KHR_get_surface_capabilities2`・`VK_EXT_surface_maintenance1` は名乗らない（WS105 の範囲の外）。
- device: `VK_KHR_swapchain`。

**`vkEnumerateInstanceExtensionProperties(pLayerName = NULL)`**: 後段の一覧から、次を**消し**、我々の instance の WSI の拡張を**足す**。

- 消す: 名前が `VK_KHR_surface`・`VK_KHR_*_surface`・`VK_EXT_*_surface`・`VK_KHR_display`・`VK_KHR_get_display_properties2`・
  `VK_EXT_acquire_*_display`・`VK_EXT_direct_mode_display`・`VK_EXT_display_surface_counter`・`VK_KHR_get_surface_capabilities2`・
  `VK_EXT_swapchain_colorspace`・`VK_EXT_surface_maintenance1`・`VK_KHR_surface_protected_capabilities`・`VK_GOOGLE_surfaceless_query` の物。
- 足す: 上の我々の instance の拡張（重複させない）。
- `pLayerName` が NULL でなければ後段にそのまま渡す（layer は後段の物）。

**`vkEnumerateDeviceExtensionProperties`**: 後段の一覧から `VK_KHR_swapchain`・`VK_KHR_swapchain_mutable_format`・`VK_EXT_swapchain_maintenance1`・
`VK_KHR_present_id`・`VK_KHR_present_wait`・`VK_KHR_incremental_present`・`VK_EXT_display_control`・`VK_KHR_display_swapchain`・
`VK_EXT_full_screen_exclusive`・`VK_KHR_shared_presentable_image`・`VK_GOOGLE_display_timing`・`VK_EXT_hdr_metadata` を消し、
`VK_KHR_swapchain` を足す。**ただし** 我々の WSI が要る device の拡張（下）を後段が持たない physical device では `VK_KHR_swapchain` を足さない。

**`vkCreateInstance`**:

1. 後段を開く（§4.3）。
2. app の `ppEnabledExtensionNames` から我々の WSI の instance の拡張を**取り除いた**一覧を作る。
3. 内部で要る instance の拡張を足す: apiVersion が 1.0 のとき `VK_KHR_get_physical_device_properties2`・`VK_KHR_external_memory_capabilities`・
   `VK_KHR_external_semaphore_capabilities`（後段が持つときだけ。1.1 以上では core）。
4. 後段の `vkCreateInstance` を呼ぶ。`VkInstanceCreateInfo` は複写して拡張の一覧だけ差し替える（app の構造体を書き換えない）。
5. 成功したら instance の記録を作る（§4.5）。

**`vkCreateDevice`**:

1. app の一覧から `VK_KHR_swapchain` を取り除く。
2. app が `VK_KHR_swapchain` を求めたときだけ、WSI が要る device の拡張を足す（後段の 1.x の core に入っている物は足さない）:
   `VK_KHR_external_memory`・`VK_KHR_external_memory_fd`・`VK_EXT_external_memory_dma_buf`・`VK_EXT_image_drm_format_modifier`
   （とその前提の `VK_KHR_image_format_list`・`VK_KHR_bind_memory2`・`VK_KHR_sampler_ycbcr_conversion`・`VK_KHR_maintenance1`、1.1・1.2 の core なら不要）、
   `VK_KHR_external_semaphore`・`VK_KHR_external_semaphore_fd`、`VK_KHR_dedicated_allocation`・`VK_KHR_get_memory_requirements2`。
   後段が持たない物があれば: `VK_EXT_image_drm_format_modifier` が無い → LINEAR の image を `VK_IMAGE_TILING_LINEAR` で作る道（§4.8）、
   `VK_EXT_external_memory_dma_buf` が無い → wl_shm の複写の道（§4.8 の予備）。どちらの道かを device の記録に持つ。
3. 後段の `vkCreateDevice` を呼び、device の記録を作る。

### 4.7 `vkGetInstanceProcAddr`・`vkGetDeviceProcAddr`

- `vkGetInstanceProcAddr(instance, name)`:
  - `name` が O の関数か I の関数 → 我々の関数を返す（`instance` が NULL のときは global な関数だけ: `vkCreateInstance`・
    `vkEnumerateInstance*`・`vkGetInstanceProcAddr`）。
  - O の関数のうち、その拡張を app が有効にしていないもの → NULL（規格どおり）。
  - それ以外 → 後段の `vkGetInstanceProcAddr(instance, name)` の答えをそのまま返す。
- `vkGetDeviceProcAddr(device, name)`:
  - `name` が device の O・I の関数（`vkCreateSwapchainKHR`・`vkQueuePresentKHR`・`vkGetDeviceProcAddr`・`vkDestroyDevice`・`vkGetDeviceQueue*` など）→ 我々の物。
  - それ以外 → 後段の `vkGetDeviceProcAddr(device, name)`。これは driver の関数を直接指すので、app の呼び出しは我々を通らず速い。
- 我々の export の一覧（`exports.map`）は、Vulkan 1.0〜1.3 の core の関数と、Linux の Vulkan の loader が export する WSI の関数
  （`vkCreateWaylandSurfaceKHR`・`vkCreateSwapchainKHR` など）と同じにする。理由: 普通の Linux の app（`vulkaninfo` など）が我々の library で動くように。

### 4.8 Wayland の WSI（`wsi-wayland.c`・`wsi-swapchain.c`）

**protocol**: core の `wl_surface`・`wl_buffer`・`wl_callback` と、`zwp_linux_dmabuf_v1`（version 3 以上。4 なら feedback も読める）。
`zwp_linux_dmabuf_v1` は core ではないので、その client の stub（`linux-dmabuf-v1-client-protocol.h` と `wl_interface` の表）は
libvulkan-compat の**内部**に置く（F-065 の決定 3 と同じ扱い。libwayland-client にも libkeiland にも入れない）。stub は wayland-scanner の
出力ではなく、我々の libwayland の手書きの形（`userland/desktop/libwayland/*-protocol.c`）に合わせて書く。

**event queue**: app の event の処理を邪魔しないため、WSI は surface ごとに自分の `wl_event_queue` を作り（`wl_display_create_queue`）、
`wl_display` と `wl_surface` の wrapper（`wl_proxy_create_wrapper` + `wl_proxy_set_queue`）を通して request を送る。zedBSD の
`userland/desktop/libvulkan/wsi-wayland.c` が同じ形なので、queue・frame callback・release の扱いはそれを手本に読む（code の複写はしない、D7）。

**surface の作成**（`vkCreateWaylandSurfaceKHR`）: `wl_display *` と `wl_surface *` を覚えるだけ。protocol の bind は swapchain の作成で行う。

**surface の問い合わせ**:

- `vkGetPhysicalDeviceSurfaceSupportKHR`: graphics の queue family なら真。
- `vkGetPhysicalDeviceWaylandPresentationSupportKHR`: 真（graphics の queue family）。
- `vkGetPhysicalDeviceSurfaceFormatsKHR`: `VK_FORMAT_B8G8R8A8_UNORM`・`VK_FORMAT_B8G8R8A8_SRGB`（`DRM_FORMAT_ARGB8888`）と
  `VK_FORMAT_R8G8B8A8_UNORM`・`_SRGB`（`DRM_FORMAT_ABGR8888`）のうち、compositor が告げた format（dmabuf の `format`/`modifier` の event）と
  後段が作れる物の共通部分。color space は `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` だけ。
- `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`: `currentExtent` は `0xFFFFFFFF`（Wayland では client が大きさを決める）、
  `minImageCount` 2、`maxImageCount` 8、usage は `COLOR_ATTACHMENT`・`TRANSFER_SRC`・`TRANSFER_DST`・`SAMPLED`、composite alpha は
  `OPAQUE`・`PRE_MULTIPLIED`・`INHERIT`。
- `vkGetPhysicalDeviceSurfacePresentModesKHR`: `FIFO` と `MAILBOX`。

**swapchain の image の作り方**（`vkCreateSwapchainKHR`、image ごと）:

1. format の modifier を選ぶ: compositor が告げた modifier の集合 ∩ 後段の `VkDrmFormatModifierPropertiesListEXT`（`vkGetPhysicalDeviceFormatProperties2`）で
   `COLOR_ATTACHMENT` と usage を満たし、plane が 1 つの物。無ければ `DRM_FORMAT_MOD_LINEAR`。
2. `vkCreateImage`: `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT` + `VkImageDrmFormatModifierListCreateInfoEXT`（選んだ modifier）+
   `VkExternalMemoryImageCreateInfo{DMA_BUF_BIT_EXT}`。modifier の拡張が無い device では `VK_IMAGE_TILING_LINEAR`（modifier は LINEAR として送る）。
3. memory: `vkGetImageMemoryRequirements2` + dedicated（`VkMemoryDedicatedAllocateInfo`）+ `VkExportMemoryAllocateInfo{DMA_BUF_BIT_EXT}` で `vkAllocateMemory`、`vkBindImageMemory`。
4. `vkGetMemoryFdKHR(DMA_BUF)` で dma-buf の fd を得る。modifier と plane 0 の offset・stride を
   `vkGetImageDrmFormatModifierPropertiesEXT` と `vkGetImageSubresourceLayout(MEMORY_PLANE_0)` で得る（LINEAR tiling なら COLOR の aspect）。
5. `zwp_linux_dmabuf_v1.create_params` → `add(fd, 0, offset, stride, modifier_hi, modifier_lo)` → `create_immed(width, height, fourcc, 0)` で `wl_buffer` を作る。
   fd は送った後に閉じてよいが、**implicit sync の ioctl に使うので image ごとに 1 本残す**（dup する）。

**present**（`vkQueuePresentKHR`、image ごと）— **implicit sync**（D6）:

1. app の wait semaphore を待って描画の完了を表す semaphore を作る: `vkQueueSubmit`（command buffer 0 個、`pWaitSemaphores` = app の物、
   `pSignalSemaphores` = 我々の export できる semaphore（`VkExportSemaphoreCreateInfo{SYNC_FD}`））。
2. `vkGetSemaphoreFdKHR(SYNC_FD)` で sync_file の fd を得る（-1 なら既に signal 済み）。
3. その sync_file を dma-buf に付ける: `ioctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, {flags = DMA_BUF_SYNC_WRITE, fd})`。
   これで「この buffer への書き込みは、この fence が済むまで終わっていない」と kernel が知る。compositor はそれを待てる（§5.2）。
   sync_file を閉じる。
4. `wl_surface_attach` → `wl_surface_damage_buffer`（全体）→ FIFO なら `wl_surface_frame` → `wl_surface_commit` → `wl_display_flush`。
5. ioctl が `ENOTTY`・`EINVAL`（Linux 6.0 より前の kernel）のときの予備: `vkWaitForFences` で描画の完了を CPU で待ってから commit する（遅いが正しい）。
   一度失敗したら、その swapchain では以後ずっと予備の道を使う。

**acquire**（`vkAcquireNextImageKHR`）:

- `wl_buffer.release` を受けた image だけが空き。空きが無ければ、WSI の queue を `wl_display_dispatch_queue` で待つ（timeout に従う。0 なら待たない → `VK_NOT_READY`、
  時間切れ → `VK_TIMEOUT`）。
- 返す image の、compositor の読み終わりを待つ: `ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, {flags = DMA_BUF_SYNC_WRITE})` で
  「この buffer を読む・書く全ての fence」を sync_file にし、`vkImportSemaphoreFdKHR(SYNC_FD, TEMPORARY)` で app の semaphore に入れる
  （app が fence を渡したら、その fence にも同じ fd の dup を `vkImportFenceFdKHR` で入れる）。予備の道では、sync_file の代わりに
  -1（済み）を入れる（release の時点で compositor が読み終えている、§5.2 の約束）。
- FIFO: 前の present の frame callback が来るまで次の commit を送らない（`vkQueuePresentKHR` の中で待つ）。MAILBOX: 待たない。

**大きさの変化**: Wayland では client が大きさを決めるので、`VK_ERROR_OUT_OF_DATE_KHR` は返さない（`VK_SUBOPTIMAL_KHR` も返さない）。app が
新しい大きさで swapchain を作り直す。古い swapchain（`oldSwapchain`）の image は、release を受けたものから消す。

**dma-buf が使えないときの予備（wl_shm の複写）**: 後段に `VK_EXT_external_memory_dma_buf` が無い場合（ベンダーの libvulkan で起こりうる）。
image は普通の `OPTIMAL` で作り、present で `vkCmdCopyImageToBuffer` で host-visible の buffer に写し、`wl_shm` の pool（memfd）に
memcpy して `wl_buffer` にする。WS105 では作らない（範囲の外、§8）。ただし device の記録に「どの道か」を持つ形にしておき、後で足せるようにする。

### 4.9 画面の WSI（VK_KHR_display と VK_EXT_acquire_drm_display、`wsi-display.c`・`kms.c`）

compositor（`/opt/keiland/bin/wayland`）が画面に出すための物。後段の VK_KHR_display は使わない（D8）。

**二つの fd**:

| fd | 誰が開くか | 何に使うか |
| --- | --- | --- |
| 問い合わせの fd | libvulkan-compat 自身が、最初の display の問い合わせ（`vkGetPhysicalDeviceDisplayPropertiesKHR` など）で開く。`KEILAND_DRM_DEVICE`（環境変数、例 `/dev/dri/card0`）、無ければ `/dev/dri/card0`〜`card15` の中で connector を持つ最初の物。`O_RDWR | O_CLOEXEC`（DRM の問い合わせの ioctl は master でなくても通る） | connector・mode・CRTC の列挙（`VkDisplayKHR`・`VkDisplayModeKHR` を作る） |
| master の fd | **compositor の seat の backend** が得る（root なら直接 open、gdm の下なら logind の `TakeDevice`）。`vkAcquireDrmDisplayEXT(physicalDevice, fd, display)` で渡される | mode の設定・dumb buffer・page flip（master が要る操作） |

- compositor の手順（WS104 p006 の `zwl_os_display_acquire` の Linux の実装）:
  ```
  vkGetPhysicalDeviceDisplayPropertiesKHR → display を選ぶ（今の compose.c・vkdemo/display.c と同じ）
  vkAcquireDrmDisplayEXT(physicalDevice, seat の master の fd, display)   ← zwl_os_display_acquire（Linux）
  vkCreateDisplayPlaneSurfaceKHR → vkCreateSwapchainKHR → vkQueuePresentKHR …
  vkReleaseDisplayEXT(physicalDevice, display)                            ← 終わるとき
  ```
  compositor は fd を渡すだけで、DRM の ioctl は呼ばない（WS103 の「compositor は GPU を Vulkan だけで扱う」を Linux でも保つ）。
- acquire されていない display に swapchain を作ったとき（vkdemo など root で直接走る app）: libvulkan-compat が問い合わせの fd で master を取ろうとする
  （`DRM_IOCTL_SET_MASTER`）。取れなければ `VK_ERROR_INITIALIZATION_FAILED`。
- `vkGetDrmDisplayEXT(physicalDevice, drmFd, connectorId, &display)` も実装する（規格の関数。connector の番号から display を返す）。compositor は使わない。
- display は全ての physical device に同じ物を見せる（lavapipe のように DRM の device と関係の無い後段があるため。複写の道なのでどの device でも出せる）。
- KMS の操作（`kms.c`）は Linux の kernel の DRM の ioctl を直接使う（`<drm/drm.h>`・`<drm/drm_mode.h>`、linux-libc-dev の header、MIT）。
  **libdrm には link しない**（D9）。使う ioctl: `DRM_IOCTL_SET_MASTER`・`DROP_MASTER`・`MODE_GETRESOURCES`・`GETCONNECTOR`・`GETENCODER`・`GETCRTC`・
  `CREATE_DUMB`・`MAP_DUMB`・`DESTROY_DUMB`・`ADDFB2`・`RMFB`・`SETCRTC`・`PAGE_FLIP`（`DRM_MODE_PAGE_FLIP_EVENT`）。flip の完了の event は fd の `read`。
- **image の出し方は「複写の道」（v1、全ての後段で動く、D20）**: swapchain の image は後段の普通の image（`OPTIMAL`、usage に `TRANSFER_SRC` を足す）。
  present のたびに `vkCmdCopyImageToBuffer` で host-visible・host-coherent の buffer に写し、fence を待ち、KMS の dumb buffer（`CREATE_DUMB`・`MAP_DUMB` + mmap）に
  行ごとに memcpy し（stride が違う）、`PAGE_FLIP` する。dumb buffer は 2 つ（前と後）。flip の完了の event を読んでから次の flip を出す（FIFO）。
  1920×1080 で 1 frame 約 8 MB の memcpy。QEMU で 60 Hz を目標にしない（受け入れは「表示が正しい」こと）。
- **複写しない道**（dumb buffer の dma-buf を後段に import して直接描く、または後段の image の dma-buf を `ADDFB2` する）は WS105 では作らない（§8、Future Work）。
- VT の切り替え・logind の `PauseDevice` で DRM master を失ったとき: `PAGE_FLIP`・`SETCRTC` が `EACCES`・`EPERM` になる。
  `vkQueuePresentKHR` は `VK_ERROR_OUT_OF_DATE_KHR` を返す。compositor は pause の間は present を止め、resume で swapchain を作り直す（§5.5）。

### 4.10 file の構成（`userland/desktop/libvulkan-compat/`）

| file | 役割 |
| --- | --- |
| `Makefile.linux` | Linux の build だけ（zedBSD の `Makefile` は**作らない**。zedBSD では build しない、D4） |
| `README.md` | この節の要約と、§4.2 の図 |
| `functions.tsv` | 全ての関数の名前と種類（F・I・O）、export するか。Vulkan の header（host の `/usr/include/vulkan/vulkan_core.h`）の版に合わせる |
| `gen-forward.sh` | `functions.tsv` から `forward.inc`（F の関数の定義と dlsym の表）を作る sh の script。build の時に走らせる（生成物は `build/` に置き、source に入れない） |
| `backend.c` | §4.3（後段を開く、確かめ） |
| `dispatch.c` | F の表の初期化、`vkGetInstanceProcAddr`・`vkGetDeviceProcAddr`（§4.7） |
| `instance.c` | `vkCreateInstance`・`vkDestroyInstance`・instance の拡張の一覧（§4.6）、instance の記録 |
| `device.c` | `vkCreateDevice`・`vkDestroyDevice`・device の拡張の一覧・`vkGetDeviceQueue*`、device と queue の記録 |
| `wsi-wayland.c` | Wayland の surface、`zwp_linux_dmabuf_v1` の bind と format・modifier の受け取り、event queue |
| `wsi-swapchain.c` | swapchain（Wayland と display の共通の部分: image の配列、acquire・present の入口） |
| `wsi-display.c` | VK_KHR_display・VK_EXT_acquire_drm_display の Vulkan の側 |
| `kms.c` | KMS の ioctl（§4.9） |
| `linux-dmabuf-v1-protocol.c`・`linux-dmabuf-v1-client-protocol.h` | protocol の stub（内部） |
| `compat.h` | 内部の struct と関数の宣言 |
| `exports.map` | export する名前（§4.7） |

### 4.11 名前の衝突（symbol interposition）と確かめ

我々の library と後段は、どちらも `vkCreateInstance` などの同じ名前を export する。

- **我々 → 後段**: 我々は後段の関数を `dlsym(backend_handle, name)` の pointer で呼ぶので、名前の解決に頼らない。安全。
- **後段 → 我々（危険）**: 後段の中の code が自分の export の関数を**名前で**呼ぶ（PLT を通る）と、process の global な scope で先に
  載った我々の関数に結び付くことがある（ELF の既定。F-065 の未決 1、2026-09-30 の host の試験で gcc の既定の build では起きた）。
  そうなると 我々 → 後段 → 我々 → … と循環する。
- 対策（D10）:
  1. 後段を `RTLD_LOCAL` で開く（後段の symbol を global に出さない。我々の symbol が後段に見えるのは防げない）。
  2. 我々の F・I の入口に、thread-local の再入の数を持ち、**同じ関数への再入を見つけたら** stderr に
     `libvulkan-compat: backend called back into vkXxx (symbol interposition); set KEILAND_VULKAN_DEEPBIND=1` を出して `abort()` する（無限の循環で固まるより良い）。
  3. glibc では、環境変数 `KEILAND_VULKAN_DEEPBIND=1` のときだけ後段を `RTLD_DEEPBIND` を足して開く（後段が自分の symbol を先に見る）。
     既定にしない理由: `RTLD_DEEPBIND` は後段が使う libc の関数の解決にも効き、malloc などを差し替える程度の問題が知られている。musl には無い。
  4. 確かめ（p004 の受け入れ）: Debian の Khronos の loader（`/usr/lib/x86_64-linux-gnu/libvulkan.so.1`、1.4.309）を後段にして、
     `LD_DEBUG=bindings` の出力で、後段（`/usr/lib/.../libvulkan.so.1`）の中の参照が我々の file（`/opt/keiland/lib/libvulkan.so.1`）の `vk*` に
     結び付いていないことを確かめる（§7.3 に command）。
- **我々の libwayland-client と system の libwayland-client**: 後段の ICD（Mesa の lavapipe など）は `libwayland-client.so.0`（system の物）に依存している。
  我々の libwayland-client の SONAME は `libwayland-client.so`（版の番号が無い。zedBSD と同じ）で名前が違うので、ICD を載せると system の物も同じ process に載る。
  ICD の `wl_*` の参照は global な scope で先に載った**我々の物**に結び付くが、ICD の WSI は使わない（D5）ので実行されない。2026-10-01 に、Mesa 25.0.7 の
  lavapipe・anv・radv・venus の ICD が参照する `wl_*` の symbol（`wl_display_create_queue_with_name`・`wl_shm_pool_interface` などを含む）が全て我々の
  libwayland-client にあることを `nm` で確かめた。我々に無い symbol を参照する ICD が将来出ても、data の参照は system の物で解決され、load は失敗しない（D19）。
- **system の libwayland を使う app（Keiland の外の app）は対象の外**: そういう app の `wl_display *` は system の libwayland の物で、我々の WSI が我々の
  libwayland の関数で扱うと壊れる。libvulkan-compat は `/opt/keiland/bin` の Keiland の app（我々の libwayland-client に link した物）のためだけの物である。
  `LD_LIBRARY_PATH` で `vkcube` などを我々の library で走らせることは試験に使わない（`vulkaninfo --summary` は WSI を使わないので試験に使ってよい）。
- 同じ SONAME（`libvulkan.so.1`）の 2 つの file が 1 つの process に載ることも確かめる: glibc は絶対 path の `dlopen` では、既に載った
  物と device・inode で比べるので、別の file なら別に載る（p004 で `dlopen` の handle と `dlsym` の address が我々と違うことを試験する）。

## 5. compositor の Linux の module（`userland/desktop/wayland/linux/`）

WS104 の後、compositor の OS の部分は `wayland/zedbsd/` の file と、共通の code が呼ぶ 3 つの header（`zwl-gpu.h`・`zwl-input.h`・`zwl-os.h`）と
`zwl_handoff_*` の関数になっている。Linux では同じ関数を `wayland/linux/` の file で実装する。共通の code の変更は、下の §5.6 に挙げた物だけにする。

| file | 実装する物 | Phase |
| --- | --- | --- |
| `linux/os-linux.c` | `zwl-os.h`（seat の選択と open・close、VT、既定の socket、`zwl_os_display_acquire`、logind の poll） | p006（direct）、p009（logind） |
| `linux/seat-direct-linux.c` | root で DRM と入力の device を直接 open する seat | p006 |
| `linux/seat-logind-linux.c`・`linux/dbus-linux.c` | logind の seat と最小の D-Bus | p009 |
| `linux/input-linux.c` | `zwl-input.h`（`/dev/input/eventN`、device の fd は seat から） | p006 |
| `linux/handoff-linux.c` | `zwl_handoff_*`（sessiond が無い） | p006 |
| `linux/gpu-linux.c` | `zwl-gpu.h`（`zwp_linux_dmabuf_v1` の server、dma-buf の import、implicit sync） | p006（global 無しの空の形）、p007（全部） |
| `linux/seat-linux.h` | linux の file の間の内部の header | p006 |

### 5.1 seat（`os-linux.c`・`seat-direct-linux.c`）

- seat の選び方: 環境変数 `KEILAND_SEAT=direct|logind`。無ければ、`XDG_SESSION_ID` があり `/run/systemd/seats` があれば `logind`、それ以外は `direct`。
  p006 では `direct` だけ（`logind` を選んだら ENOTSUP で起動の失敗、message に「seat logind is not built yet」）。
- `direct`（root で走る。組み込みと開発用）:
  - DRM: `open(KEILAND_DRM_DEVICE or "/dev/dri/card0", O_RDWR | O_CLOEXEC)`。最初に開いた process は自動で master になる。`zwl_os_display_acquire` がこの fd を
    `vkAcquireDrmDisplayEXT` に渡す（関数は `vkGetInstanceProcAddr(instance, "vkAcquireDrmDisplayEXT")` で得る）。**compositor は DRM の ioctl を呼ばない。**
  - 入力: `input-linux.c` が `open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)`。
  - VT: 標準入力が VT（`ioctl(0, KDGETMODE, ...)` が成功）なら、`KDSETMODE KD_GRAPHICS`（console の文字を描かせない）と `KDSKBMODE K_OFF`（key を tty に流さない）にし、
    元の値を覚えて `zwl_os_close` で戻す。VT でなければ何もしない（SSH から直接起動した場合。そのときは key が console にも流れる）。
    試験では `openvt -s -w -- /opt/keiland/bin/wayland ...` で新しい VT で起動する（§7）。
  - 終わり方: SIGINT・SIGTERM → 今の `stop_service` → `service_cleanup` → `zwl_os_close`（VT を戻す、fd を閉じる）。
- 既定の socket: `--socket` が無ければ `$XDG_RUNTIME_DIR/wayland-keiland`、`XDG_RUNTIME_DIR` が無ければ `/tmp/wayland-keiland`（`zwl_os_open` で決める。
  共通の `main.c` に「`--socket` が与えられたか」の flag が無ければ足す、§5.6）。

### 5.2 GPU の buffer（`gpu-linux.c`、p007）

**global**: `zwl_gpu_global_interface()` → `"zwp_linux_dmabuf_v1"`、`zwl_gpu_global_version()` → 3。

**protocol**（`/usr/share/wayland-protocols/stable/linux-dmabuf/linux-dmabuf-v1.xml` が正。version 3 の範囲）:

| object | request（opcode） | event（opcode） |
| --- | --- | --- |
| `zwp_linux_dmabuf_v1`（kind `ZWL_FACTORY`） | 0 `destroy`、1 `create_params(new_id params)` | 0 `format(uint format)`、1 `modifier(uint format, uint hi, uint lo)` |
| `zwp_linux_buffer_params_v1`（新しい kind `ZWL_GPU_OBJECT`、§5.6） | 0 `destroy`、1 `add(fd, uint plane_idx, uint offset, uint stride, uint modifier_hi, uint modifier_lo)`、2 `create(int width, int height, uint format, uint flags)`、3 `create_immed(new_id wl_buffer, int width, int height, uint format, uint flags)` | 0 `created(new_id wl_buffer)`、1 `failed` |

- error の値（params）: `already_used` 0、`plane_idx` 1、`plane_set` 2、`incomplete` 3、`invalid_format` 4、`invalid_dimensions` 5、`out_of_bounds` 6、`invalid_wl_buffer` 7。
- bind の時: 受け付ける (format, modifier) の組ごとに、`format` の event を format ごとに 1 回、`modifier` の event を組ごとに 1 回送る。
- 受け付ける format: `DRM_FORMAT_ARGB8888`（`0x34325241`）→ `VK_FORMAT_B8G8R8A8_UNORM`・合成は alpha、`DRM_FORMAT_XRGB8888`（`0x34325258`）→ 同じ VkFormat・合成は opaque。
- 受け付ける modifier: 後段の `vkGetPhysicalDeviceFormatProperties2(B8G8R8A8_UNORM)` + `VkDrmFormatModifierPropertiesListEXT` のうち、plane が 1 つで
  `drmFormatModifierTilingFeatures` に `SAMPLED_IMAGE` がある物、かつ `vkGetPhysicalDeviceImageFormatProperties2`（`VkPhysicalDeviceImageDrmFormatModifierInfoEXT` と
  `VkPhysicalDeviceExternalImageFormatInfo{DMA_BUF}`）で import できる物。起動の時に 1 回求めて覚える。
- 複数の plane の buffer（YUV など）は WS105 では受けない（`create` は `failed`、`create_immed` は `invalid_format`）。
- import（`create`・`create_immed`）:
  1. 検査: plane 0 が add 済み、plane 1 以上が無い、format と modifier が受け付ける組、`0 < width, height ≤ server->gpu_limits.max_dimension`、
     `offset + stride × height ≤ lseek(fd, 0, SEEK_END)`（dma-buf の大きさ）、`stride ≥ width × 4`。違反は上の error（`out_of_bounds` など）。
  2. `vkCreateImage`: 2D、mip 1、layer 1、sample 1、`tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`、usage `SAMPLED`、pNext に
     `VkImageDrmFormatModifierExplicitCreateInfoEXT{modifier, 1, &VkSubresourceLayout{offset, 0, stride, 0, 0}}` と `VkExternalMemoryImageCreateInfo{DMA_BUF_BIT_EXT}`。
  3. `vkGetMemoryFdPropertiesKHR(DMA_BUF, fd)` の `memoryTypeBits` と `vkGetImageMemoryRequirements` の bits の共通の最も低い bit を memory type にする（無ければ失敗）。
  4. `vkAllocateMemory`: `VkImportMemoryFdInfoKHR{DMA_BUF, dup(fd)}` + `VkMemoryDedicatedAllocateInfo{image}`、大きさは requirements。成功すると Vulkan が dup の fd を持つ。失敗なら dup を閉じる。
  5. `vkBindImageMemory`、`zwl_import_adopt(buffer, image, memory, width, height, format)`、ARGB なら alpha の合成（`zwl_import_set_alpha` の意味で）。
  6. plane の fd の 1 本（dup）を buffer の OS の記録に残す（implicit sync の ioctl に使う）。buffer が消えるときに閉じる（§5.6 の OS の記録の後始末）。
  7. log: `ZWL IMPORT client=%llu buffer=%u width=%u height=%u bytes=%llu`（zedBSD と同じ形。bytes は dma-buf の大きさ）。
- **implicit sync**（acquire）: `zwl_gpu_commit(surface, buffer)` で、残した fd に `ioctl(DMA_BUF_IOCTL_EXPORT_SYNC_FILE, {flags = DMA_BUF_SYNC_READ})`
  （読む前に待つべき fence = client の描画）。得た sync_file を surface の `acquire[]` に入れる（generation は 1）。後は今の共通の code（`commit_fence`・`zwl_fence_ready` の
  poll）がそのまま待つ。ioctl が `ENOTTY` なら（古い kernel）fence 無し（client が CPU で待ってから commit する予備の道、§4.8）。
- release: 共通の code は、合成の frame の完了（`zwl_compose_complete`）の後に `wl_buffer.release` を送る。つまり release の時点で compositor は読み終えている。
  client（libvulkan-compat）は release の後に `EXPORT_SYNC_FILE(WRITE)` で念のため待つ（§4.8）。compositor が読みの fence を dma-buf に付ける必要は無い。
- Vulkan の拡張（`zwl_gpu_instance_extensions`・`zwl_gpu_device_extensions`）: instance `VK_EXT_direct_mode_display`・`VK_EXT_acquire_drm_display`、
  device `VK_EXT_external_memory_dma_buf`・`VK_EXT_image_drm_format_modifier`（後段が 1.2 未満なら前提の `VK_KHR_image_format_list`・`VK_KHR_bind_memory2`・
  `VK_KHR_sampler_ycbcr_conversion`・`VK_KHR_maintenance1` も）。
- frame の fence: `zwl_gpu_frame_fence_type()` → 0（fd にしない。D22。SYNC_FD の export は fence を reset する規格の副作用があり、今の
  `zwl_compose_complete` の `vkWaitForFences` と合わない。main loop は今の `vkGetFenceStatus` の poll の道を使う）。

### 5.3 入力（`input-linux.c`）

- `zwl_input_scan`: `/dev/input` の `eventN` を列挙し、まだ開いていない物を seat の関数で開き（direct は `open`、logind は `TakeDevice`）、`EVIOCGBIT` で bit を読んで
  `zwl_input_probe` に渡す。zedBSD の `input-zedbsd.c` と同じ形で、header は `zwl-evdev.h`（Linux では `<linux/input.h>`）。hotplug は zedBSD と同じ 2 秒ごとの再走査
  （inotify は使わない）。
- `zwl_input_device_absinfo`・`_name`・`_id`・`_read`・`_close`: `EVIOCGABS`・`EVIOCGNAME`・`EVIOCGID`・`read`・seat の close。
- `EVIOCGRAB` はしない（direct は `K_OFF`、logind は logind が VT を管理する）。

### 5.4 session（`handoff-linux.c`）

| 関数 | Linux の動き |
| --- | --- |
| `zwl_handoff_wait` | 何もしない（すぐ画面を取る） |
| `zwl_handoff_release` | `zwl_compose_output_close(server)`（zedBSD の release から sessiond への書き込みを除いた物） |
| `zwl_handoff_logout` | 0 を返す（呼ぶ側の `home.c` が `zwl_request_stop()` で compositor を終える → gdm が greeter に戻す） |
| `zwl_handoff_tick` | 何もしない |

- lock 画面（`zwl_lock`）は `control_fd < 0` で何もしない（zedBSD の sessiond 無しと同じ）。App Home に Lock Screen は出ない。
- session の中の Shut Down は無い（zedBSD の session にも無く、greeter だけが持つ）。Linux では gdm の greeter が持つ（D11 の補い）。

### 5.5 logind と pause・resume（p009）

- `dbus-linux.c`: system bus（`/run/dbus/system_bus_socket`）への unix の socket、認証 `AUTH EXTERNAL <uid の 16 進の ASCII>` → `BEGIN`、`Hello`、
  method の call と return の message の marshal・unmarshal（型は `s`・`o`・`u`・`b`・`h`（fd、SCM_RIGHTS）だけ）、signal の受け取り、`AddMatch`。
  D-Bus の仕様: https://dbus.freedesktop.org/doc/dbus-specification.html （little endian、header の field、8 byte の整列）。libdbus・libsystemd は使わない（D12）。
- `seat-logind-linux.c`:
  1. `org.freedesktop.login1.Manager.GetSession(s $XDG_SESSION_ID)` → session の object path。
  2. `org.freedesktop.login1.Session.TakeControl(b false)`。
  3. DRM: `TakeDevice(u major, u minor)` → `(h fd, b inactive)`（major・minor は `stat("/dev/dri/card0")` の `st_rdev`）。入力: 同じく device ごと。close は `ReleaseDevice`。
  4. `AddMatch` で `PauseDevice(u major, u minor, s type)`・`ResumeDevice(u major, u minor, h fd)` の signal を受ける。
  5. `PauseDevice` の type が `pause` なら、その device の使用を止めてから `PauseDeviceComplete(u, u)` を送る。`force`・`gone` は返事しない。
- pause・resume の共通の扱い（§5.6）: DRM の pause で `server->os_paused = 1`。paused の間は合成と present をしない（`zwl_schedule` が描かない）。
  DRM の resume で新しい fd を受け、`zwl_compose_output_close` → swapchain を作り直す（`enter_window_mode` と同じ道）。入力の pause では device を閉じ、resume で再走査に任せる。
- `keiland.desktop`（`/usr/share/wayland-sessions/`、`make keiland-linux-install-session`）:
  ```
  [Desktop Entry]
  Name=Keiland
  Comment=The Keiland desktop
  Exec=/opt/keiland/bin/wayland --session --glass
  Type=Application
  DesktopNames=Keiland
  ```
  （`--wallpaper` などの値は `KEILAND_DATADIR` の既定から compositor が決める。zedBSD の `session.sh` の引数を見て、Linux で要る物を Exec に足す。）

### 5.6 共通の code の変更（WS105 で許す物だけ）

| 変更 | 理由 | Phase |
| --- | --- | --- |
| `protocol.c`: `zwl_gpu_global_interface()` が NULL なら GPU の global を出さない | p006 の Linux の module はまだ global を持たない | p006 |
| `zwl.h`・`protocol.c`: object の kind `ZWL_GPU_OBJECT`（OS の GPU の protocol の factory 以外の object）を足し、`zwl_dispatch` で `zwl_gpu_request` に送る | linux-dmabuf の params の object | p007 |
| `struct zwl_object` に OS の module の記録の pointer（`void *gpu_private`）と、object が消えるときに OS の module を呼ぶ `zwl_gpu_object_free(object)`（zedBSD の module は何もしない） | dma-buf の fd を buffer に残すため | p007 |
| `main.c`: `--socket` が与えられたかの flag（無ければ） | 既定の socket を OS が決める | p006 |
| `struct zwl_server` の `os_paused` と、`zwl_schedule` の paused の扱い、resume の swapchain の作り直し | logind の pause | p009 |

**それ以外の共通の file を変える必要が出たら、Phase を止めて main に報告する**（WS104 の境界の欠けなので、WS104 の考え方に沿って直すかを main が決める）。
zedBSD の側の module（`zedbsd/`）に要る小さな追加（`zwl_gpu_object_free` の空の実装など）は、その Phase で一緒に行い、zedBSD の回帰（§9.2）を流す。

## 6. libkeiland の Linux の backend

WS104 の p003 で、libkeiland の OS の部分は `libkeiland/zedbsd/` に移してある（`network-zedbsd.c`・`network-link-zedbsd.c`・`audio-zedbsd.c`）。Linux では同じ関数（`keiland.h` の `keiland_network_*`・`keiland_audio_*`）を
別の source で実装する。**`keiland.h` の公開の API と意味は変えない。** app（zdesktop の system bar・Settings）は何も変えずに Linux で動く。

### 6.1 WiFi（`libkeiland/wpa/network-wpa.c`、FreeBSD と共有できる形）

- 相手: wpa_supplicant の制御の socket（`/run/wpa_supplicant/<ifname>`、`ctrl_interface=DIR=/run/wpa_supplicant GROUP=netdev`）。
  protocol は 1 行の text の command と応答（`PING`→`PONG`、`STATUS`、`SCAN`、`SCAN_RESULTS`、`LIST_NETWORKS`、`ADD_NETWORK`、`SET_NETWORK`、
  `SELECT_NETWORK`、`DISCONNECT`、`RECONNECT`、`SAVE_CONFIG`）。`ATTACH` した socket に `<3>CTRL-EVENT-CONNECTED` などの event が来る。
  client の側の socket は `/tmp` などに自分で `bind` した unix の datagram の socket（wpa_supplicant の `wpa_ctrl.c` と同じ形。この file は BSD license だが、
  **複写せずに自分で書く**）。
- `keiland_network_open`: `/run/wpa_supplicant/` の中の最初の socket（`p2p-` で始まる物を除く）に接続し、`ATTACH` する。無ければ
  `reachable = 0`（今の zedBSD の「daemon が居ない」と同じ）で、update が 1 秒ごとに試す。
- 状態の対応:

  | `STATUS` の `wpa_state` | `KEILAND_WIFI_*` |
  | --- | --- |
  | `INTERFACE_DISABLED` | `OFF` |
  | `SCANNING` | `SEARCHING` |
  | `AUTHENTICATING`・`ASSOCIATING`・`ASSOCIATED`・`4WAY_HANDSHAKE`・`GROUP_HANDSHAKE` | `CONNECTING` |
  | `COMPLETED` | `CONNECTED` |
  | `DISCONNECTED`・`INACTIVE` | `DISCONNECTED` |
  | socket が無い | `ABSENT` |

- request の対応: `SCAN` → `SCAN`（結果は `CTRL-EVENT-SCAN-RESULTS` の後に `SCAN_RESULTS`）、`JOIN ssid` → `LIST_NETWORKS` で ssid の番号を探して
  `SELECT_NETWORK n`（無ければ ENOENT。鍵の保存は §6.2 で `ADD_NETWORK` 済みの前提）、`DISCONNECT` → `DISCONNECT`、`WIFI_OFF` → `DISCONNECT` の後
  interface を down（`SIOCSIFFLAGS`、権限が無ければ EPERM）、`WIFI_ON` → interface を up して `RECONNECT`、`PROFILES` → 何もしない（成功）。
- `SCAN_RESULTS` の行（`bssid freq signal flags ssid`）: `signal` を `rssi`（dBm）、`flags` に `WPA` か `RSN` か `WEP` があれば `secured = 1`。同じ ssid は強い方だけ。
  ssid の escape（`\xNN`）を戻す。
- `connected`・`kind`・`interface`・`wired` は §6.2 の interface の情報から作る。

### 6.2 interface・DNS・鍵（`libkeiland/linux/network-link-linux.c`）

- `keiland_network_get_links`: `getifaddrs`（address、MAC は `AF_PACKET` の `sockaddr_ll`）と `/sys/class/net/<if>/statistics/{rx,tx}_bytes`。
  `/sys/class/net/<if>/wireless` か `phy80211` があれば WiFi。
- `keiland_network_get_dns`: `/etc/resolv.conf` の `nameserver`（zedBSD と同じ。zedBSD の source の読み方の部分は同じでよいが file を分ける）。
- `keiland_network_save_key(ssid, key)`: wpa_supplicant に `ADD_NETWORK` → `SET_NETWORK n ssid "<ssid>"` → `SET_NETWORK n psk "<key>"` →
  `ENABLE_NETWORK n`（接続はしない）→ `SAVE_CONFIG`（`update_config=1` のときだけ効く。失敗しても保存は wpa_supplicant の memory に残る）。
  既に同じ ssid があれば `SET_NETWORK` で鍵だけ変える。
- `keiland_network_get_saved`: `LIST_NETWORKS` の ssid。

### 6.3 音（`libkeiland/linux/audio-linux.c`）

- 相手: ALSA の kernel の control の interface `/dev/snd/controlC<N>`（`<sound/asound.h>`、linux-libc-dev の header）。**alsa-lib に link しない**（D15）。
- 使う ioctl: `SNDRV_CTL_IOCTL_CARD_INFO`、`SNDRV_CTL_IOCTL_ELEM_LIST`、`SNDRV_CTL_IOCTL_ELEM_INFO`、`SNDRV_CTL_IOCTL_ELEM_READ`、`SNDRV_CTL_IOCTL_ELEM_WRITE`、
  `SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS`（他の process の変更を `read` の event で知る）。
- 音量の element: 名前が `Master Playback Volume`（無ければ `PCM Playback Volume`、`Speaker Playback Volume`）の INTEGER。mute は `Master Playback Switch`（BOOLEAN、1 が音あり）。
  左右 2 channel。`keiland_audio_state` の音量（0〜100 の割合、zedBSD の audiod と同じ範囲か WS104 の時点の `keiland.h` で確かめる）に線形で写す。
- `keiland_audio_fd`: control の fd（event が来ると readable）。
- `keiland_audio_feedback`（確かめの音）: PCM の再生が要る（`/dev/snd/pcmC<N>D<M>p`、`SNDRV_PCM_IOCTL_*`）。**WS105 では作らず、0 を返して鳴らさない**
  （`keiland.h` の約束「an audiod without it stays silent」の範囲。§8）。
- `keiland_audio_available`（WS104 p002 で足した関数）: `/dev/snd/controlC*` のどれかが開けて、音量の element があれば 1。
- `reachable` は control の device が開けている間 1、`device` は音量の element があれば 1、`rate`・`channels` は 0（不明）と 2。

## 7. 試験の環境

### 7.1 二つの場所

| 場所 | 何を試すか | 理由 |
| --- | --- | --- |
| **host**（この開発機。Debian 13、kernel 6.12、Mesa 25.0.7 の lavapipe、Khronos の loader 1.4.309） | build、libvulkan-compat の chain と Wayland の WSI（試験用の Wayland server `dmabuf-probe` を相手に）、host の単体の試験 | 速い。host の GPU は Matrox（3D 無し）なので Vulkan は lavapipe（software）だけ。lavapipe は `VK_EXT_external_memory_dma_buf`・`VK_EXT_image_drm_format_modifier`・`VK_KHR_external_semaphore_fd` を持つ（2026-10-01 `vulkaninfo` で確かめた）。dma-buf は `/dev/udmabuf`（group kvm、利用者 awe は kvm に入っている） |
| **Linux の guest**（QEMU の中の Debian 13。`plan/tools/keiland-linux/` で作る、p001） | compositor（KMS・evdev・seat）、app、gdm、WiFi（`mac80211_hwsim`）、音（QEMU の `intel-hda`） | host の画面と入力を触らない。root で DRM master を取ってよい。gdm を入れてよい |

- **host の画面（`/dev/dri/card0`、Matrox の console）を Keiland の試験に使わない。** host の DRM・入力の device を開く試験は禁止。
- zedBSD の試験の規則（`plan/tools/boot-test.sh` だけで起動を確かめる、serial の log で判定しない）は zedBSD の image の物。Linux の guest の判定は
  QMP の `screendump` の PNG と、SSH の command の結果で行う。guest の serial の console は使わない（揃えるため）。

### 7.2 Linux の guest（p001 で作る）

- image: `mmdebstrap`（host にある）で Debian 13（trixie）の root file system を作り、ext4 の disk image にする。kernel は Debian の `linux-image-amd64`。
  boot は QEMU の `-kernel`・`-initrd`（guest の `/boot` から取り出す）と `root=/dev/vda`。
- 入れる package（最小）: `systemd-sysv`・`udev`・`openssh-server`・`mesa-vulkan-drivers`（lavapipe と venus）・`libvulkan1`（Khronos の loader、後段）・
  `vulkan-tools`（`vulkaninfo`）・`weston`（libvulkan-compat の比べの相手）・`wpasupplicant`・`hostapd`・`iw`・`alsa-utils`（試験の確かめだけ、我々は link しない）・
  `kmod`・`sudo`。gdm は p009 で別の image（`-gdm`）に足す（重い）。
- 利用者: `root`（password 無し、SSH の key だけ）と `kei`（uid 1000、group `video`・`input`・`audio`）。host の SSH の key を `authorized_keys` に入れる。
- QEMU の device:
  - 画面: `-device virtio-vga`（guest の KMS は `virtio_gpu`、`/dev/dri/card0`）。Venus を試すときだけ `-device virtio-vga-gl,hostmem=4G,blob=true,venus=true` と
    `-display egl-headless`（host の Vulkan は lavapipe）。既定は venus 無し（guest の Vulkan は lavapipe）。
  - 入力: `-device virtio-keyboard-pci -device virtio-tablet-pci -device virtio-mouse-pci`（guest の `/dev/input/event*`）。
  - 音: `-audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0`（guest の `/dev/snd/controlC0`）。
  - network: `-netdev user,id=n0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22 -device virtio-net-pci,netdev=n0`。
  - QMP: `-qmp unix:${OUT}/qmp.sock,server,nowait`（`plan/tools/qmp.py` を使う）。
  - `-display none`、`-m 4G -smp 4 -enable-kvm`。
- 道具（p001 で作る、`plan/tools/keiland-linux/`）:
  - `build-guest.sh [OUT]`: image を作る（既定 `build/keiland-linux/guest.img`）。sudo を使ってよい（AGENTS.md）。
  - `guest.sh start|stop|ssh CMD...|scp-in SRC DST|screenshot PNG|key KEYS|click X Y|status`: guest の起動・停止・操作。SSH の port は既定 2225。
    `screenshot` は QMP の `screendump`（PNG）。`key`・`click` は QMP の `input-send-event`。
  - `install-guest.sh`: host で `make keiland-linux-install DESTDIR=build/keiland-linux/stage` した tree を guest の `/opt/keiland` に写す（`rsync` か `tar | ssh`）。
  - `png-probe.py PNG X Y`: PNG の画素の色を出す（判定に使う）。
- **QEMU の port と directory は試験ごとに分ける**（他の agent と同時に走れるように、`OUT` と `SSH_PORT` を環境変数で変えられる形）。

### 7.3 host の試験の道具（`plan/tools/keiland-linux/`、p003・p004 で作る）

- `vk-chain-test.c`: libvulkan-compat を通して instance・device を作り、compute か transfer で buffer を埋め、結果を読む。後段の名前を出す。
  build: `cc -o build/keiland-linux/test/vk-chain-test plan/tools/keiland-linux/vk-chain-test.c -L build/keiland-linux/stage/opt/keiland/lib -lvulkan -Wl,-rpath,/opt/keiland/lib`。
  実行は DESTDIR の tree で: `LD_LIBRARY_PATH=build/keiland-linux/stage/opt/keiland/lib build/keiland-linux/test/vk-chain-test`。
- `dmabuf-probe`（試験用の Wayland server、**host の libwayland-server と wayland-scanner を使う。試験の道具だけで、出荷しない**）:
  - global: `wl_compositor`（v4）、`wl_shm`（無くてよい）、`zwp_linux_dmabuf_v1`（v3。`format` と `modifier` の event で `ARGB8888`・`XRGB8888` の `LINEAR` を告げる）。
  - commit された dma-buf の buffer ごとに: plane の fd に `DMA_BUF_IOCTL_EXPORT_SYNC_FILE(READ)` をかけ、得た sync_file を `poll` で待ち（implicit sync の確かめ）、
    `mmap`（LINEAR だけ）して中央の画素を読み、`PROBE frame=N pixel=0xAARRGGBB waited_ms=M` を stdout に出し、`wl_buffer.release` を送る。
  - `--frames N` で N frame 受けたら終わる。
- `wsi-probe-client.c`: libvulkan-compat の Wayland の WSI で、frame ごとに決まった色（frame 番号で赤・緑・青を巡る）で clear して present する client。
  我々の libwayland-client（`/opt/keiland/lib`）に link する。xdg-shell は使わない（`dmabuf-probe` は role を求めない）。
- `interpose-check.sh`: `LD_DEBUG=bindings` で `vk-chain-test` を走らせ、後段の file の中の参照が我々の file の `vk*` に結び付いた行が 0 であることを確かめる。
  ```
  LD_DEBUG=bindings LD_DEBUG_OUTPUT=/tmp/... ./vk-chain-test
  grep "binding file /usr/lib/x86_64-linux-gnu/libvulkan.so.1 .* to .*/opt/keiland/lib/libvulkan.so.1 .*symbol \`vk" → 0 行
  ```

## 8. 範囲の外（WS105 では作らない。Future Work か後の WS）

- FreeBSD（F-065 に残す）。`wpa/` の module は FreeBSD でも使える形にしておくだけ。
- browser（libbrowser）・xserver・EGL と GLES（libegl・libglesv2・egltest・glescompute・gpudemo）の Linux の build。
- libvulkan-compat の、複写しない画面の出力（dumb buffer の dma-buf の import、image の dma-buf の `ADDFB2`）と、dma-buf の無い後段のための wl_shm の予備の道（§4.8）。
- explicit sync（`linux-drm-syncobj-v1`）と、sync_file の ioctl の無い Linux 6.0 より前の kernel での試験（予備の道の code は作るが、試験は 6.12 だけ）。
- 確かめの音（`keiland_audio_feedback` の PCM の再生）。Linux では 0 を返して鳴らさない（`keiland.h` の約束「an audiod without it stays silent」の範囲）。
- amd64 の外の Linux（compositor の `zwl_cycles` が `rdtsc` を使う。組み込みの ARM へは後の WS）。
- 実機の Linux（WS105 の証拠は host と QEMU の guest だけ）。
- Keiland の外の app（system の libwayland を使う app）の libvulkan-compat での動作（§4.11）。
- sessiond・greeter・lock 画面の Linux の版、session の中の Shut Down。
- 互換の Qt6・GTK4（WS096・WS097）、XDG の規則、Linux の distribution の package（`.deb` など）。

## 9. 確かめの約束

### 9.1 Linux の側

- build: `make keiland-linux`（既定の `cc` = Debian の gcc 14）と `make keiland-linux CC=clang KEILAND_LINUX_BUILD=build/keiland-linux-clang` の両方で warning 0（`-Werror`）。
- install: `make keiland-linux-install DESTDIR=$PWD/build/keiland-linux/stage`。`readelf -d` で全ての ELF に `RUNPATH [/opt/keiland/lib]`、SONAME が §2 のとおり、
  `NEEDED` に我々の library が SONAME で並ぶこと（`plan/tools/keiland-linux/elf-check.sh`、p002 で作る）。
- host の試験と guest の試験は §7。**判定は数（PROBE の行、終了の code）と PNG（`png-probe.py` の画素）で行い、目で見ただけで PASS にしない。**
  PNG はユーザーに見せる（AGENTS.md の「撮れた PNG はユーザーに見せる」に合わせる）。

### 9.2 zedBSD の側（共通の file を変えた Phase で必ず）

WS105 は共通の source（compositor の `protocol.c`・`main.c`・`display.c`、libkeiland の共通の file など）を少し変える（§5.6）。変えた Phase では zedBSD の回帰を流す:

- `make -j16 BUILD=build/amd64 disk-image`、warning 0。
- `plan/tools/gpu-boundary/v1-check.sh`、`criteria.sh ... C1 C2 C9`（compositor を変えたとき）。
- `plan/tools/keiland-os-boundary/check.sh`（WS104 p008 の物）。
- boot test。

## 10. 作業の中で確かめる点（未確認の前提）

| # | 前提 | どこで確かめるか | 外れたら |
| --- | --- | --- | --- |
| V1 | Debian の Khronos の loader（1.4.309）を後段にして、後段の中の参照が我々の `vk*` に結び付かない | p003 の `interpose-check.sh` | `KEILAND_VULKAN_DEEPBIND=1` を既定にするかを main に報告（D10） |
| V2 | 同じ SONAME `libvulkan.so.1` の我々の物と後段が 1 つの process に別々に載る | p003 の `vk-chain-test`（`dlsym` の address が違う） | 後段を名前の違う複写で開くなどを main に報告 |
| V3 | host と guest の lavapipe が dma-buf の image を modifier（少なくとも LINEAR）で export・import でき、SYNC_FD の semaphore を export できる | p004 | modifier の拡張が無ければ LINEAR tiling の道（§4.8）。dma-buf が無ければ範囲の外の wl_shm の道が要るので main に報告 |
| V4 | guest の virtio-gpu の KMS で dumb buffer の page flip が動き、QMP の `screendump` に出る | p005 | `SETCRTC` だけで出す道（flip の event 無し）に落とす |
| V5 | lavapipe が別の process の lavapipe が export した dma-buf（udmabuf）を import できる | p007 | main に報告（compositor の import の道が要る） |
| V6 | gdm（Debian 13、gdm3 48）が `/usr/share/wayland-sessions/keiland.desktop` の session を logind の session として起動し、`TakeControl` が通る | p009 | gdm の版の差を調べて main に報告 |
| V7 | `mac80211_hwsim` と hostapd で guest の中に WiFi の AP と client ができ、wpa_supplicant の制御 socket で scan・接続できる | p010 | network の試験を「wpa_supplicant の制御 socket の偽物の server」で行う形に落とす |
