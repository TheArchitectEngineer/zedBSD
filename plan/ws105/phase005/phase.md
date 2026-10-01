<!-- awesome-plan project=zedbsd record=ws105-p005 -->

# ws105-p005: libvulkan-compat (3): 画面の WSI（KMS、VK_KHR_display、VK_EXT_acquire_drm_display）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p004、p001（guest）
実行者: phase-runner（high）。**始める前に [design.md](../design.md) の §4.9 を読む。**

## 目的

compositor が画面に出すための VK_KHR_display を、libvulkan-compat が Linux の KMS を直接使って実装する（決定 D8・D9・D20）。DRM の master の fd は compositor の
seat が得て `vkAcquireDrmDisplayEXT` で渡す（p006）。この Phase では、試験の program が自分で開いた fd を渡す形と、root の app（vkdemo）が acquire せずに使う形を確かめる。

## 作る・変える file（`userland/desktop/libvulkan-compat/`）

| file | 中身 |
| --- | --- |
| `functions.tsv` | O の行を足す: VK_KHR_display の全て（`vkGetPhysicalDeviceDisplayPropertiesKHR`・`vkGetPhysicalDeviceDisplayPlanePropertiesKHR`・`vkGetDisplayPlaneSupportedDisplaysKHR`・`vkGetDisplayModePropertiesKHR`・`vkCreateDisplayModeKHR`・`vkGetDisplayPlaneCapabilitiesKHR`・`vkCreateDisplayPlaneSurfaceKHR`）、`vkReleaseDisplayEXT`（VK_EXT_direct_mode_display）、`vkAcquireDrmDisplayEXT`・`vkGetDrmDisplayEXT`（VK_EXT_acquire_drm_display） |
| `instance.c` | instance の拡張に `VK_KHR_display`・`VK_EXT_direct_mode_display`・`VK_EXT_acquire_drm_display` を足す |
| `kms.c` | design §4.9 の KMS の ioctl（`<drm/drm.h>`・`<drm/drm_mode.h>`、libdrm を使わない）: device の open（問い合わせの fd）、connector・encoder・CRTC・mode の列挙、master（`SET_MASTER`・`DROP_MASTER`）、dumb buffer の作成・map・破棄、`ADDFB2`（`DRM_FORMAT_XRGB8888`）・`RMFB`、`SETCRTC`、`PAGE_FLIP` と event の読み取り |
| `wsi-display.c` | VK_KHR_display の Vulkan の側: `VkDisplayKHR`（connector ごと、connected の物だけ）・`VkDisplayModeKHR`（mode ごと）の記録、plane は 1 つ（primary）、`vkCreateDisplayPlaneSurfaceKHR`、acquire・release |
| `wsi-swapchain.c` | display の surface の swapchain（design §4.9 の複写の道）: image は後段の `OPTIMAL`（usage に `TRANSFER_SRC` を足す）、present で `vkCmdCopyImageToBuffer` → fence を待つ → dumb buffer へ行ごとに memcpy → `PAGE_FLIP`（最初の 1 回は `SETCRTC`）。FIFO（flip の event を待ってから次）。`VK_ERROR_OUT_OF_DATE_KHR`（master を失った）。swapchain の破棄で fb と dumb buffer を消し、CRTC を元に戻す（acquire の時に `GETCRTC` で覚えた物） |
| `Makefile.linux` | 変更があれば |

細部:

- surface の format: `VK_FORMAT_B8G8R8A8_UNORM`・`_SRGB`（dumb buffer は `XRGB8888`、memory の並びは B8G8R8A8 と同じ）。present mode は `FIFO` だけ。
- `vkGetPhysicalDeviceDisplayPropertiesKHR` の `physicalResolution` は mode の preferred の大きさ（`DRM_MODE_TYPE_PREFERRED`、無ければ最初の mode）。
  `displayName` は connector の種類と番号（例 `Virtual-1`）。zedBSD の compositor の `compose_display` が `physicalResolution` を使うので値を正しく入れる。
- `vkGetDisplayModePropertiesKHR` の `refreshRate` は mHz（`vrefresh × 1000`、clock から計算できればその値）。
- 問い合わせの fd の device: `KEILAND_DRM_DEVICE`、無ければ `/dev/dri/card0`〜`card15` で connector を持つ最初の物（design §4.9）。

## 試験の道具（`plan/tools/keiland-linux/`）

`display-probe.c`: 我々の libvulkan-compat で VK_KHR_display を使い、(1) display と mode を出し、(2) `--acquire` なら `/dev/dri/card0` を自分で開いて
`vkAcquireDrmDisplayEXT` に渡し（compositor の形）、(3) 全画面を 赤 → 緑 → 青 に 1 秒ずつ clear して present し、各色の間に stdout に `DISPLAY color=RRGGBB` を出して
5 秒待つ（screenshot の時間）、(4) release して終わる。

guest での手順:

```
make keiland-linux && make keiland-linux-install DESTDIR=$PWD/build/keiland-linux/stage
cc -o build/keiland-linux/stage/opt/keiland/bin/display-probe plan/tools/keiland-linux/display-probe.c -Lbuild/keiland-linux/lib -lvulkan -Wl,-rpath,/opt/keiland/lib
plan/tools/keiland-linux/guest.sh start
plan/tools/keiland-linux/install-guest.sh
plan/tools/keiland-linux/guest.sh ssh 'openvt -c 7 -s -- /opt/keiland/bin/display-probe --acquire > /tmp/display.out 2>&1'
# DISPLAY color=ff0000 が出た頃に（/tmp/display.out を ssh で見て待つ）
plan/tools/keiland-linux/guest.sh screenshot build/keiland-linux/p005-red.png
python3 plan/tools/keiland-linux/png-probe.py build/keiland-linux/p005-red.png 100 100   # #ff0000
```

（`openvt -s` は VT 7 に切り替えて起動する。QMP の `screendump` は今の VT の画面を撮る。終わった後 `chvt 1` で戻す。）

vkdemo（`userland/desktop/vkdemo/`、VK_KHR_display の demo）の Linux の build もこの Phase で足す（`vkdemo/Makefile.linux`。依存は libvulkan-compat と
`userland/base/common/sha256.c`）。guest で root で `openvt -c 7 -s -- /opt/keiland/bin/vkdemo`（vkdemo の引数は zedBSD の試験の使い方を見る）を走らせ、screenshot に絵が出ること。

## 確かめ（完了の条件）

1. build（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS、host の p003・p004 の試験が今も PASS。
2. guest: `display-probe --acquire` の 3 色が screenshot の中央と隅（`png-probe.py` の 4 点）で期待どおり（design §10 の V4）。
3. guest: `display-probe`（`--acquire` 無し。libvulkan-compat が自分で master を取る道）でも 2 と同じ。
4. guest: vkdemo の screenshot に描画が出る（wallpaper の色でない画素が中央にある、を `png-probe.py` で確かめ、PNG をユーザーに見せる）。
5. 終わった後に console に戻る（`chvt 1` の後の screenshot に console の文字）。CRTC を元に戻せていること。
6. master を失う確かめ: `display-probe` の途中で `chvt 1`（KMS の master は VT の切り替えでは失われないことがある。失われた場合に `VK_ERROR_OUT_OF_DATE_KHR` で終わり、固まらないこと。
   失われない場合はそう記録する）。

## 結果

（実行の後に書く）
