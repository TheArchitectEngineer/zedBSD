<!-- awesome-plan project=zedbsd record=ws105-p003 -->

# ws105-p003: libvulkan-compat (1): 後段への chain（WSI 無し）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p002
実行者: phase-runner（high）。**始める前に [design.md](../design.md) の §4 を全部読む。**

## 目的

`userland/desktop/libvulkan-compat/` を作り、Vulkan の全ての関数を system の libvulkan（後段）に渡す library（`libvulkan.so.1`）にする（決定 D4・D5・D10）。
WSI（surface・swapchain・display）は p004・p005 で足す。この Phase の終わりには、WSI を使わない Vulkan の program が、我々の library を通して後段の lavapipe で動く。

**一番大事な考え方**（design §4.4）: dispatchable な handle（`VkInstance`・`VkDevice`・`VkCommandBuffer` など）を包まない。我々の export した `vkCmdDraw` は、
後段が export している `vkCmdDraw` を `dlsym` で得た pointer で呼ぶだけ。後段の export の関数が handle から正しい driver を選ぶ。

## 作る file（`userland/desktop/libvulkan-compat/`）

| file | 中身（design の節） |
| --- | --- |
| `README.md` | design §4.1・§4.2 の要約と図。「zedBSD の `libvulkan/` とは別の物」と冒頭に書く |
| `Makefile.linux` | `KEILAND_LINUX_LIBRARY,vulkan,libvulkan.so.1,...`、system の library `-ldl -lpthread`。build の時に `gen-forward.sh` で `$(KEILAND_LINUX_BUILD)/gen/libvulkan-compat/forward.inc` を作り、その directory を `-I` に足す。install で `lib/libvulkan.so.1` と、build の directory にだけ link `lib/libvulkan.so` → `libvulkan.so.1`（app の `-lvulkan` のため。install の先には置かない） |
| `functions.tsv` | 1 行 1 関数: `名前<TAB>種類（F・I・O）<TAB>export（y・n）<TAB>注釈`。F は Vulkan 1.0〜1.3 の core の全ての関数（host の `/usr/include/vulkan/vulkan_core.h` の `VK_VERSION_1_0`〜`VK_VERSION_1_3` の block の `vkXxx` の prototype から作る）。I は design §4.4 の表の物。O（WSI）の行は p004・p005 で足す（この Phase では書かない） |
| `gen-forward.sh` | `functions.tsv` と host の `vulkan_core.h` から `forward.inc` を作る POSIX の sh（`awk` 可）。F の関数ごとに: `static PFN_vkXxx next_vkXxx;`、export の定義 `VKAPI_ATTR <戻り値> VKAPI_CALL vkXxx(<引数>) { compat_enter(...); if (next_vkXxx == NULL) compat_missing("vkXxx"); [return] next_vkXxx(<引数の名前>); }`、表の初期化の行 `next_vkXxx = (PFN_vkXxx)dlsym(handle, "vkXxx");`。prototype は header から正規表現で取る（`VKAPI_ATTR ... VKAPI_CALL vkXxx(` から `);` まで）。取れない関数があれば script が失敗する |
| `compat.h` | 内部の struct（instance・device・queue の記録、design §4.5）と関数の宣言 |
| `backend.c` | design §4.3: 後段を探して開く（`pthread_once`）、自分を開いていないかの確かめ（`dladdr` で自分の path、`realpath` の比較、`dlsym` の address の比較）、`forward.inc` の表の初期化 |
| `dispatch.c` | `vkGetInstanceProcAddr`・`vkGetDeviceProcAddr`（design §4.7）、再入の検出（`compat_enter`・`compat_leave`、thread-local の深さ、同じ関数の再入で message と `abort()`、design §4.11）、`compat_missing` |
| `instance.c` | `vkCreateInstance`・`vkDestroyInstance`・`vkEnumerateInstanceExtensionProperties`・`vkEnumerateInstanceLayerProperties`（後段にそのまま）・`vkEnumerateInstanceVersion`、instance の記録（design §4.6） |
| `device.c` | `vkCreateDevice`・`vkDestroyDevice`・`vkEnumerateDeviceExtensionProperties`・`vkGetDeviceQueue`・`vkGetDeviceQueue2`、device と queue の記録（design §4.6） |
| `exports.map` | `{ global: vk*; local: *; };` |

この Phase の拡張の扱い: 後段の WSI の拡張を一覧から**消す**ことは今行う（design §4.6 の「消す」）。我々の WSI の拡張を**足す**のは p004・p005。
つまり p003 の終わりの library は、WSI の拡張を何も名乗らない。

`KEILAND_LINUX_PACKAGES` に `userland/desktop/libvulkan-compat/Makefile.linux` を足す（libwayland の後。p004 で libwayland-client に依存する）。

## 試験の道具（`plan/tools/keiland-linux/`、main が merge）

| file | 中身 |
| --- | --- |
| `vk-chain-test.c` | (1) `dlopen(NULL)` と `dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")` が我々の file の物であること（`dladdr` の path が `.../opt/keiland/lib/libvulkan.so.1` で終わる）。(2) instance を作り、physical device の名前と driver を出す。(3) device を作り、compute の queue で 1 MiB の buffer を `vkCmdFillBuffer` で 0x5a5a5a5a に埋め、host-visible の buffer に `vkCmdCopyBuffer` し、読んで確かめる。(4) 拡張の一覧に `VK_KHR_surface`・`VK_KHR_xcb_surface`・`VK_KHR_wayland_surface`・`VK_KHR_swapchain` が**無い**こと（p003 の時点）。(5) `vkGetDeviceProcAddr(device, "vkCmdFillBuffer")` が NULL でない。最後に `vk-chain-test: PASS` |
| `interpose-check.sh` | design §7.3: `LD_DEBUG=bindings LD_DEBUG_OUTPUT=$OUT/ld` で vk-chain-test を走らせ、`binding file <後段の path> ... to <stage の我々の file> ... symbol \`vk` の行を数える。0 なら PASS。行があれば出して FAIL |

build と実行（host、design §7.3）:

```
make keiland-linux && make keiland-linux-install DESTDIR=$PWD/build/keiland-linux/stage
L=build/keiland-linux/stage/opt/keiland/lib
mkdir -p build/keiland-linux/test
cc -o build/keiland-linux/test/vk-chain-test plan/tools/keiland-linux/vk-chain-test.c -L build/keiland-linux/lib -lvulkan -ldl -Wl,-rpath,/opt/keiland/lib
LD_LIBRARY_PATH=$L build/keiland-linux/test/vk-chain-test
LD_LIBRARY_PATH=$L vulkaninfo --summary
sh plan/tools/keiland-linux/interpose-check.sh
```

## 確かめ（完了の条件）

1. build（gcc と clang）warning 0、`elf-check.sh` PASS（`libvulkan.so.1` の SONAME は `libvulkan.so.1`）。
2. `vk-chain-test: PASS`（host の lavapipe）。
3. `LD_LIBRARY_PATH=$L vulkaninfo --summary` が走り、`llvmpipe` を出す（vulkaninfo は WSI を使わない summary の mode で、system の app を我々の library で走らせる確かめ。design §4.11 の「Keiland の外の app」は WSI の話で、ここは対象）。
4. `interpose-check.sh` PASS（design §10 の V1）。FAIL なら Phase を uncleared で止め、出た行を記録して main に報告（`RTLD_DEEPBIND` を既定にするかの判断、D10）。
5. 自分を開く誤りの確かめ: `KEILAND_VULKAN_BACKEND=$PWD/$L/libvulkan.so.1 LD_LIBRARY_PATH=$L build/keiland-linux/test/vk-chain-test` が
   `libvulkan-compat: no backend libvulkan` の message で失敗し（固まらない）、exit の code が 0 でない。
6. 後段が無い場合: `KEILAND_VULKAN_BACKEND=/nonexistent` で同じく message と失敗。
7. 再入の検出の確かめ: 後段の代わりに、自分の `vkCreateInstance` を名前で呼び返す偽の後段（`plan/tools/keiland-linux/fake-backend.c`、`vkGetInstanceProcAddr` と
   `vkCreateInstance` だけを持ち、`vkCreateInstance` の中で `vkCreateInstance` を PLT で呼ぶ。`-Wl,-Bsymbolic` を付けずに build）を
   `KEILAND_VULKAN_BACKEND` に指定して `vk-chain-test` を走らせ、design §4.11 の message で `abort` する（固まらない。timeout 10 秒で確かめる）。
8. zedBSD の build に影響が無い（`make -j16 BUILD=build/amd64 disk-image`。libvulkan-compat は zedBSD の `Makefile` を持たないので build されない、D4）。

## 結果

（実行の後に書く。特に V1・V2 の結果を design §10 の表にも追記する）
