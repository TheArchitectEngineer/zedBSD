<!-- awesome-plan project=zedbsd record=ws035p112 -->

# ws035-p112: GOP の 1920x1080 と起動画面の黒い帯

Phase ID: `ws035-p112`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て）

## 範囲

ユーザーの決定（2026-09-28、master の「retro・GOP・Kei の印」）:「GOPフレームバッファは1920x1080を要求して上下左右の不足部分を
黒い帯にすればいいかなと思います。」[p107](../phase107/phase.md) の `fit=cover`（画面を覆い、はみ出しを切る）を置き換える。

## 実装（2026-09-28）

- **絵**（`tools/build/make-boot-splash.py`）: 既定を 1920x1080 にし、header の comment を `# fit=contain` にした。
- **UEFI loader**（`bootloader/uefi/video.c`・`video.h`・`bootx64.c`）: `zbl_uefi_video_select` に望む mode（幅・高さ、0 で無し）を
  足した。`video=` が無く `logo=` がある起動では GOP の 1920x1080（RGBX/BGRX）を探して SetMode する。無い・失敗したときは今の mode
  のまま（起動は止めない）。`video=` があればそれが優先（従来どおり、無ければ失敗）。mode を探す loop は `video_set` に分けた。
- **UEFI の描き方**（`bootloader/uefi/logo.c`）: `fit=contain` の絵は、画面が絵を収めるなら原寸で中央、収めないなら比率を保って
  bilinear で縮めて中央。残りは黒（上下左右の帯）。拡大はしない。`fit=cover` は無くした（comment の無い PPM は従来どおり）。
- **BIOS の描き方**（`bootloader/bios/logo.c`・`logo.h`）: 同じ `fit=contain`。最初の画素で画面を黒で塗り、原寸か最近傍の縮小で
  中央に。struct の末尾は cut_x・cut_y を除いて 40→32 byte（bootzbsd.S の知る offset は不変、`_Static_assert`）。
- **spinner**（`src/drivers/platform/pcat/graphics/splash.c`）: 絵の高さを「原寸 1080、画面が小さいときは縮めた高さ」で出す（loader
  と同じ式）。spinner の中心は画面の中央から絵の高さの 27.2 % 下（帯を含めた画面で loader の置き方に一致）。
- 試験の道具（`plan/ws035/tests/p107/logo-host.c`・`bios-logo-host.c`）の表示を contain に合わせた。

## 検証（2026-09-28）

- host（`cc -std=c11 -Wall -Wextra` で warning 0）: logo-host・bios-logo-host・spinner-host を 1920x1080・2560x1440・2560x1080・
  1280x1024・1280x800・1024x768・640x480 で。帯（左・右・上・下）と絵の大きさ: 1920x1080 は帯無し、2560x1440 は 320/320/180/180 で
  原寸 1920x1080、2560x1080 は左右 320、1280x1024 は上下 152 で 1280x720、640x480 は上下 60 で 640x360。UEFI と BIOS で同じ。BIOS の
  sector の最大書き込み 171/172（原寸）。spinner-host（3 step）の描いた範囲の中心は絵の spinner の場所（中央、絵の高さの 77.2 %）と
  1.3 px 以内で一致（全ての mode）。
- 画面（host）: `build/ws035-shots/p112-20260928-host-uefi-2560x1440.png`・`-host-uefi-1280x1024.png`・`-host-uefi-1920x1080.png`・
  `-host-bios-1024x768.png`。
- **graphical の image の cfg**（`platform/amd64/vmunix.mk`）: `zedbsd-native-uefi.cfg` の `video=640x480` が要求を打ち消すので、
  `ZEDBSD_GRAPHICAL_BOOT=y` の cfg は `video=` の行を落とす（graphical でない image は 640x480 のまま）。
- **boot test の読み取り**（`plan/tools/boot-test.py`）: 1920x1080 の console は行の余り 8 px を上下に分けて 4 px 下から描くので、
  その原点も試し、読みの選び方を「未知の cell が最少」から「認識した文字 − 未知の cell が最大」にした（中央の空白の格子が
  選ばれて何も読めなかった）。
- style-check: `bootloader/uefi/logo.c`・`bootloader/bios/logo.c`・`splash.c` は 0。`video.c`（`video_mode_matches`、不変）と
  `bootx64.c`（変更から遠い 1669〜1692 行）の指摘は既存。
- QEMU（amd64、`build/ws035-p112` の graphical-network の image、GPU 無し、cfg は `logo=logo.ppm kmsg=quiet login=graphical`）:
  - UEFI（OVMF、`video=` 無し）: GOP が 1920x1080 になり、絵が帯無しで全面、spinner が絵の場所で回る → login。
  - UEFI `--cfg video=1280x1024`: 上下に 152 px の黒い帯、絵は 1280x720、spinner は絵の場所。
  - UEFI `video=640x480`（cfg を落とす前の image）: 上下 60 px の帯。
  - BIOS（SeaBIOS、VBE 1024x768、`bios-hdd-image.img`）: 上下 96 px の帯、spinner は絵の場所。
  - boot test（`plan/tools/boot-test.sh build/ws035-p112/hdd-image.img`）PASS（1920x1080 の console の `login:`）。
- 画面: `build/ws035-shots/p112-20260928-uefi-1920x1080-spinner.png`・`-uefi-1280x1024-bars.png`・`-uefi-640x480-bars.png`・
  `-bios-1024x768-bars.png`・`-boot-test-login.png`。
- 未実施: 実機。GPU（Venus・i915）の guest で greeter が 1920x1080 の firmware の framebuffer を引き継ぐ場合の画面。

## 残り

- 無し（実機の確認は WS035 の実機の Phase で）。
