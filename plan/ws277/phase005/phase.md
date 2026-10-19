<!-- awesome-plan project=zedbsd record=ws277-p005 -->

# ws277-p005 自前の mode（HDMI の PHY・PLL、DC8200 の panel・plane）

- 親: [ws277](../ws.md)、Queue: q1353 / q1353-i01（P1、2026-10-20）
- Status: in-progress（追補 版 3.2・実装・build（VF2・riscv64-virt・amd64、warning 0）・host 試験 PASS。実機 U7 待ち、GOP の無い時は R1b 待ち）
- 設計: [design.md](../design.md) 版 3.2 §15（追補）、§2.2・§2.3、§4.4・§4.7。D3・D6・D7 は (a)。

## 道の優先（§15.1、2026-10-20 Q1 の事実による）

配布の U-Boot は GOP（1920x1080、0xfe000000）を渡すので、**主の道は引き継ぎ（p002〜p004）**。p005 は (a) GOP が無い時、(b) boot の行に `display.mode=` がある時（U7 の試験の手段）だけ動く。

## 実装（2026-10-20 P1）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/dc8200-mode.c`・`.h` | 純粋な値: mode の表 5 つ（640x480・800x600・1024x768・1280x720・1920x1080、各 60 Hz）、既定 1080p60、boot の行の `display.mode=WxH[@R]` の読み、HDMI の PLL の表（pre・post）、DC の timing の 4 語、HDMI の PLL・timing・drive の書き込みの列 |
| `src/drivers/gpu/powervr/dc8200-hdmi.c`・`.h` | `powervr_dc_hdmi_start()`: bias → PLL → lock（各 20 ms 上限、越えたら ETIMEDOUT を返して続ける）→ LDO・serializer・drive → PHY down で timing → PHY up・TMDS・sync |
| `src/drivers/gpu/powervr/dc8200.c` | `drv_powervr_dc_own_wanted()`（GOP 無し、または行に `display.mode=`）、`drv_powervr_dc_own_mode()`: mode を選ぶ、framebuffer（bootfb が収めればそれ、無ければ `kern_pmem_alloc_limited` ≤ 0xffffffff・uncached の別名に色の帯）、窓の写像、model の確認、DSS の 2 bit（自前で電源を入れた時だけ）、HDMI → panel（DP RGB888・DPI・dither 無し・timing・panel の信号と running・start）→ plane（XRGB・swizzle・SIZE・位置・blend 無効・FB_EN・panel 0）を commit。`board_adoption` を go・plane 0・`own` にして、p003 の commit・fatal の戻しの道がそのまま使う（§15.3） |
| `src/drivers/gpu/powervr/powervr.h` | `struct drv_powervr_dc_own_place`・上の 2 関数 |
| `src/kern/platform/visionfive2.c` | `probe_display_controller` が own を選んだら `light_display`: GOP がある時は clock の確認だけ、無い時は `power_display`（PD_VOUT on、SYS 58・60〜63 と VOUT 4〜7・9・15〜17 の gate、LCD の親を pix0、reset SYS 43・26・VOUT 0・1・2・9）。HDMI 0x29590000・DSS 0x295B0000 の組み込みの記述 |
| `platform/riscv64/vmunix.mk` | 2 file を足した |
| `plan/ws277/tests/p005/` | host 試験（`run.sh OUTDIR`）。p003 の `fatal-host-test.c`・`run.sh` に新しい symbol の stub と source を足した |

出典は block ごとに `Values referenced:`／`Procedure referenced:`（vendor 6.12 `inno_hdmi.c/.h`・`vs_dc.c`・`vs_dc_hw.c`・`jh7110.dtsi`、mainline `vs_crtc.c`・`vs_crtc_regs.h`・`vs_bridge.c`・`vs_bridge_regs.h`・`vs_primary_plane_regs.h`、`drm_edid.c` の CEA・DMT の timing、dt-bindings）。vendor の code・名前は写していない。HAL・toolchain・WS256 の source（vf2-crg・vf2-pmu）は変えていない。

### 画面の 1 行

`dc8200: own mode <W>x<H>@<R> clock <kHz> kHz model … rev … cid … fb <phys>/<pitch> format <f>`。失敗は `dc8200: own mode: …`（no framebuffer・map failed・unknown model・HDMI not set・HDMI PLL not locked・power domain・clock・reset）と `visionfive2: display not lit (<errno>)`。

## 確認（2026-10-20 P1）

- build（warning 0）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws277-p005-vf2 -j16 vmunix` → 0、`warning:` 0（`dc8200-hdmi.o`・`dc8200-mode.o` を compile）。`config-riscv64-virt.mk BUILD=build/ws277-p005-riscv64-virt`・`config-amd64.mk BUILD=build/ws277-p005-amd64` → 0、warning 0。
- host 試験: `sh plan/ws277/tests/p005/run.sh build/ws277-p005-host` → `PASS mode-host-test (100 passes, 0 failures)`（行の読み 11、5 mode の PLL の行・timing の順・60 Hz・語の field・極性、PLL の書き込みの順と値（1080p・480p の fraction）、HDMI の timing・drive）、`PASS own-host-test (23 passes …)`（GOP あり・`display.mode=1280x720@60`: bootfb を使う・pitch 7680・SIZE 1280x720・timing・ABGR・74.25 MHz・DSS に触らない）、`PASS own-host-test (20 passes …)`（GOP 無し: 既定 1080p・確保した fb・色の帯・ARGB・DSS の bit）。
- p002・p003 の host 試験を流し直した: `PASS adopt-host-test (16 …)`、`PASS fatal-host-test (36 …)`・flip 24・boot-zerocopy 7、failure 0。
- QEMU: 未実施（DC8200 は QEMU に無い。依頼に従い起動していない）。実機: 未実施。

## 実機の試験（U7、今は頼まない。T1 またはユーザーの試験の一覧へ）

- image: VF2 の通常の build（`config/ci/config-visionfive2.mk`）。配布の U-Boot（GOP を出す物）。HDMI に monitor。
- 手順 1（GOP あり、主）: boot の行に `display.mode=1280x720@60` を足して起動する。
  - 合格: monitor が 1280x720 を受け（monitor の情報表示で解像度を見る）、画面の左上に HAL の文字（boot の log）が出て、`dc8200: own mode 1280x720@60 clock 74250 kHz …` の行が画面に見える。画面を撮る。serial・console の log では判定しない。
  - 記録: `HDMI PLL not locked` の行の有無、monitor の表示の解像度、色（赤と青が入れ替わっていないか）。
- 手順 2: `display.mode=1920x1080@60`・`640x480@60` で同じ。
- 手順 3（GOP 無し、R1b の後）: GOP を出さない firmware（または loader の GOP の受け渡しを切った試験の image）で、HDMI に赤・緑・青・白の縦の帯が出る事。
- 失敗の見方: 画が出ない時は monitor の「信号なし」か「範囲外」かを記録する。起動が止まる時は gdbstub は使えない（実機）ので、画面の最後の行を撮る。

## 残り・未確認

- **R1b（新、WS256）**: HDMI の panel の pixel clock（VOUT 7）の親に外の入力 `hdmitx0_pixelclk` を選ぶ口が `vf2-crg` に無い（`drv_vf2_clock_set_parent` は CRG の clock の親だけ）。GOP がある時は U-Boot が選び済みと推測して触らない。GOP が無い時の画は R1b まで未確認。
- HDMI の regulator（AXP15060 の ALDO3・ALDO5）は firmware が on にしている事に頼る（PMIC の driver は無い）。
- DSS の syscon の 2 bit の意味（TRM 未照合）。
- GOP があり bootfb を使う時、display.c の COPY の道の screen は bootfb の 1920x1080 のままなので、copy の画は左上の W×H だけが見える（試験の手段としては許す）。GOP が無い時は display の node が出ない（display.c の screen を driver の framebuffer にするのは p003 の側、§15.3）。
- 確保した framebuffer に HAL の console の文字を出すには HAL の API の変更が要る（[proposed/hal-riscv64-late-framebuffer.md](../proposed/hal-riscv64-late-framebuffer.md)、今は承認を求めない推奨）。
- 失敗の途中で戻る時、写した窓は外さない（boot に 1 回なので leak は小さい）。
- p005 の mode は boot の時の 1 回だけで、UAPI の mode の設定・EDID は p006。
