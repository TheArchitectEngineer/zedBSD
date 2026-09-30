# F-065: Keiland を Linux・FreeBSD で動かす構成

Future Work の詳細。実行の許可ではない。着手するときは新しい WS を立てる。

## 動機（2026-09-30 ユーザー）

- 組み込み Linux で Keiland を使いたい。
- FreeBSD で、GPL の code なしに Keiland を使いたい。

## 決めたこと（2026-09-30 ユーザー）

1. **libwayland-client には core の protocol だけを入れる。**
2. **xdg-shell をはじめ core 以外の protocol は全て libkeiland（旧 libzdesktop）に入れる。** app は protocol を直接話さず、libkeiland の抽象を使う。
3. **`zed-*-client-protocol.h` は内部に置いてよいが、app には見せない。** 見るのは libkeiland だけ。
   例外として、GPU の buffer と fence の stub（`zed-gpu-buffer-v1-client-protocol.h`）は libvulkan の WSI だけが使うので、libvulkan の内部に置く（Q1 の補い）。
4. **Linux・FreeBSD でも、我々の Wayland compositor・libwayland-client・libkeiland を使う。**
   - `userland/desktop/` 以下の全てを `/opt/keiland/` に install する。compat の libpng なども `/opt/keiland/` に入れる。
   - 既存のディストリビューションの規則は無視し、バニラの Linux の上に構築できる形にする。
   - Linux の標準がどうなっているかは、ほぼ無視してよい。
5. **Linux・FreeBSD の compositor は OS の機能を使ってよい。**
   - dma-buf は使う。epoll・kqueue などは後で考える。
   - 対応は、macro の block か、OS ごとの source の module の入れ替えで行う。
6. **互換の Qt6（WS096）・GTK4（WS097）を新規に書き、Kei・Linux・FreeBSD の全てで使う。**
   - XDG などは気にしなくてよい。libkeiland が抽象化する。
   - 既存の仕組みは気にせず、全て再実装する。
7. **Vulkan は、Linux・FreeBSD では system の libvulkan を後段に使う。**
   - Mesa などの driver はディストリビューションが `/usr` に入れる。
   - app は、我々の独自の WSI を持つ libvulkan（`/opt/keiland/`）を使い、そこから後段の `/usr/lib/libvulkan.so`（とその ICD）へ chain する。
   - 後段の WSI は使わない。
   - 理由: 組み込みで一般的な、Mesa でない libvulkan も使いたい。ベンダーの Vulkan（Mali・PowerVR・Adreno など）は、
     Khronos の loader と ICD の形ではなく、単体の `libvulkan.so` のことが多い。`/usr/lib/libvulkan.so` を入口にすれば、
     中身が Khronos の loader でもベンダーの単体の libvulkan でも同じに扱える。

## 7 から出ること（2026-09-30 Q1）

- 後段は Wayland を話さない（WSI を使わないため）。ただし後段の library（ディストリビューションの Mesa の ICD、ベンダーの blob）は
  libwayland-client などに link していることがあり、同じ process の中で我々の library と名前がぶつかる（未決 1）。
  衝突を名前の分離で避ければ、我々の libwayland-client は upstream と ABI 互換である必要がない（1 の「core だけ」が通る）。
- client と compositor の間は、全 OS で我々の独自の protocol 1 本にでき、linux-dmabuf・syncobj は要らない。運ぶ中身だけが OS で変わる。

  | OS | buffer | fence |
  | --- | --- | --- |
  | zedBSD | kernel handle の fd と記述（kernel と照合できる） | 世代付きの fence の fd |
  | Linux・FreeBSD | dma-buf の fd と fourcc・modifier・plane の offset と stride | sync_file |

- 我々の EGL・GLES（WS068）は我々の libvulkan の上に載るので、後段の GL は要らない。
- zedBSD の libvulkan も「前段（WSI、OS 共通）」と「後段（zedBSD は Venus と i915、Linux・FreeBSD は system の libvulkan）」に分ける形になる。

## 未決（着手のときに決める）

1. **process の中の library の名前の衝突。**
   - 同じ process に、我々の `/opt/keiland/lib` の libvulkan・libwayland-client・compat の library と、後段の `/usr/lib/libvulkan.so` とその依存
     （system の libwayland-client・zlib・expat など）が載る。ELF の symbol は process の中で共通の名前空間にあるので、後段の `wl_*`・`vk*`・`inflate` などの
     参照が我々の library に結び付きうる（load の順で先にあるため）。多くのディストリビューションは `-z now` なので、足りない symbol は load の失敗になる。
   - symbol の version を付けるだけでは避けられない見込み（version 無しの参照は既定の version の定義に結び付く）。
   - Q1 の推奨: 我々の library の公開の symbol の名前を変える（header で標準の名前を我々の名前へ写す。例 `#define wl_display_connect keiland_wl_display_connect`）。
     `/opt/keiland/` の software は全て我々が build するので source は標準の名前のまま書け、OS と libc によらず効く。
   - 補助: glibc の `dlmopen`・`RTLD_DEEPBIND`（musl では使えず、FreeBSD は未確認）。
   - 前段が後段を dlopen し、`vkGetInstanceProcAddr` から後段の関数を得る形の細部（instance・device の dispatch の包み方）。
2. **compositor の画面の出力（VK_KHR_display）も WSI の一部である。** 次のどちらにするか。
   - (a) 我々の libvulkan が KMS を直接使って実装する（「後段の WSI は使わない」に一貫する）。
   - (b) Mesa の VK_KHR_display と `VK_EXT_acquire_drm_display` を例外として通す。

   どちらでも、DRM master を得る仕組み（logind の無いバニラ Linux なら seatd か自前）と、greeter と session の間の画面の受け渡しが要る。
3. **後段に要る export の拡張。** `VK_EXT_external_memory_dma_buf`、`VK_EXT_image_drm_format_modifier`、SYNC_FD の export
   （`VK_KHR_external_semaphore_fd`・`VK_KHR_external_fence_fd`）。Mesa の anv・radv は持っているはずだが未確認。ベンダーの libvulkan は
   揃わないことがあり（modifier が無いなど）、足りないときの予備の道（linear だけ、copy など）が要るかもしれない。
4. **libkeiland の OS の backend。** `audio.c`・`network.c`・`network-link.c` は zedBSD の audiod・networkd と話していると思われる（未確認）。
   Linux・FreeBSD の backend をどうするか。
5. **app と libkeiland の移行。** 今の Keiland の app が xdg-shell などを直接話している所を、libkeiland の抽象へ移す作業の範囲。
6. **`wl_proxy_add_dispatcher` など、我々の libwayland の独自の関数の扱い。** core だけにするとき、libkeiland が使う公開の契約として残すかを決める。

## 関係

- [WS103](../ws103/ws.md): compositor の GPU の直の ioctl を無くし、buffer・fence の受け側を backend の境界の後ろに置く。その境界がこの構成の Linux・FreeBSD の backend の入口になる。
- 2026-09-30 の対話（Q1 の説明）: libwayland-client に Keiland の protocol の stub がある事、OPAQUE_FD と dma-buf の違い、`set_acquire_fence` が libvulkan の WSI の中で送られる事。
