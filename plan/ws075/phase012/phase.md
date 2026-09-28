<!-- awesome-plan project=zedbsd record=ws075p012 -->

# ws075-p012: HDMI の主出力（H2）

Phase ID: `ws075-p012`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28。`display=hdmi` で Keiland が HDMI（1920x1280）の全画面、`display=auto` は従来どおり eDP。実機の scanout の buffer の画面で確認。LCD の目視と、HDMI の無い boot での fallback の実機確認は未実施）
Phase disposition: normal
承認: 2026-09-28 main の依頼（[hdmi-main-output.md](../hdmi-main-output.md) の H2、ユーザーの回答 1〜5）。

## 範囲

- kernel の parameter `display=hdmi|auto` と `display.mode=WxH[@R]`（`kmsg=`・`login=` の隣、`src/kern/boot.c`）。
- resident の display: `display=hdmi` で boot 時に HDMI が connected なら、HDMI（port B・pipe B・DVI mode）を唯一の出力にし、
  eDP は点けない。connected でなければ（または mode を駆動できなければ）eDP に戻り、理由を log に残す。
- display の query・mode・名前・大きさが HDMI の mode を返し、compositor がその大きさで全画面を作る。
- 範囲外: hotplug による切替、HDMI mode + infoframe（音声）、USB-C（H5）。HAL と UAPI は変えない。

## 設計と実装

| 所 | 変更 |
| --- | --- |
| `include/kern/boot.h`、`src/kern/boot.c` | key `display`・`display.mode` を追加。`display=` は `auto`・`hdmi` だけ（`kmsg=` と同じく、他の語は parse の EINVAL で boot は idle になる）。`display.mode=` は `kern_boot_display_mode_parse()`（公開）で `WxH` か `WxH@R`、W・H は 1〜16384、R は 1〜1000、それ以外は EINVAL |
| `src/drivers/gpu/i915/display/output.c`・`output.h`（新規） | `drv_i915_display_output_select()`: device の start で resident の依存を作った後に 1 回。`display=hdmi` なら hotplug の path で HDMI の connector を 1 回 detect（EDID も読む）、mode を選び（下）、340 MHz 以下を確かめ、WRPLL を計算する（`drv_i915_lcd_compute_hdmi`、参照 clock は CDCLK の ref、無ければ 38400 kHz）。結果は `display->output`（`struct i915_display_output`、`internal.h`）。mode の選び方: `display.mode=` があれば EDID の detailed timing（base の 4 つと CEA 拡張）の同じ大きさ（R があれば ±1 Hz）→ CEA 1・4・16 → CVT reduced blanking v1（R 無しは 60 Hz）。無ければ EDID の DTD1、EDID が無ければ CEA 4。物理寸法は DTD、無ければ EDID の byte 21・22（cm） |
| `display/display.c` | `drv_i915_display_panel()` を出力の mode（`drv_i915_display_output_mode()`）に。query の大きさ・名前（"HDMI" / "eDP panel"）を出力から。`drv_i915_display_resident_deps()` の最後で出力を選ぶ（capture の build は選ばない） |
| `display/modeset.c` | `drv_i915_lcd_kernel_resident_run()`: HDMI のとき run の parameter（`k->p`: output_hdmi、port 1、pipe 1、transcoder 1、DPLL0、単独なので PLL pool と DBUF を空から）、cfg を HDMI の encoder に（`i915_resident_hdmi_cfg()`: saved port bits、backlight なし、VBT の level shift）、`env->lcd` と `env->pipe` を HDMI に、buffer を出力の大きさで作る。判定は HDMI では link training の cr/eq を見ない。buffer の kernel の address を log に出す（QEMU monitor の memsave で画面を読むため） |
| `platform/amd64/vmunix.mk` | `output.c` を i915 の source に。`ZEDBSD_BOOT_EXTRA_LINES`（native UEFI の zedbsd.cfg に行を足す、値は cfg の名前にも入る） |
| `plan/ws031/tests/vkloop-hw.sh` | `KEILAND_ZDESKTOP_SH` で compositor の script を置き換える |

eDP は触らない: HDMI のとき resident run は panel の power sequence・backlight を呼ばない（実機の log で `PP_STATUS=0`）。
firmware の eDP の表示（bare metal の GOP）を止めるのは従来どおり N0/N1 の takeover（VFIO の試験では GOP は std VGA で eDP は消灯のまま）。

## 試験

### host（`plan/ws075/tests/hdmi/host-output-test.sh`、新規）

`src/kern/boot.c` と `output.c` を host の clang（ASan・UBSan、`--gc-sections`）で link。**80 checks, 0 failures**:
parameter（hdmi/auto、無し、他の語・壊れた mode・重複の拒否）、`WxH[@R]` の読み取り（境界、壊れた形 7 つ）、H1 の LCD の EDID
（[lcd-edid.hex](../phase011/lcd-edid.hex)）の DTD1（1920x1280、164.36 MHz、+h −v、259x173 mm）と CEA の DTD（1920x1080）、
CVT-RB（1920x1280@60 は LCD の timing と clock の刻み以外一致、VESA DMT の 1920x1200RB 154 MHz 2080x1235・1280x800RB 71 MHz
1440x823）、mode の選択 7 通り。

### 実機（5330、VFIO passthrough の QEMU の guest。2026-09-28）

`plan/ws075/tests/hdmi-h2-hw.sh OUTDIR "BOOT LINES" [秒...]`（新規）: zdesktop の image（`KEILAND_APP=home`、compositor は
`--width/--height` なし = display の preferred の大きさ、`hdmi/run-zdesktop-full.sh`）を `ZEDBSD_BOOT_EXTRA_LINES` 付きで build し、
lock の下で QMP 付きで起動、resident の 2 つの buffer を monitor の `memsave`（kernel の連続した仮想 address）で保存して PNG にする
（`hdmi/shot.py`・`hdmi/raw2png.py`）。同時に 5330 の USB を記録（`hdmi/usb-poll.sh`）。

| run | 証拠 | 結果 |
| --- | --- | --- |
| `display=hdmi`（1 回目） | — | **不具合**: WRPLL が EINVAL → eDP に fallback（fallback の経路は動いた）。原因: `drv_i915_lcd_compute_hdmi()` が out を消してから mode を写すので、out の中の mode を渡すと 0 になる。local に写して修正 |
| `display=hdmi` | `build/ws075-h2/hdmi/`（worktree） | **PASS**。`display output: HDMI on DDI B, pipe B, DVI mode: 1920x1280@60 Hz 164360 kHz (mode from EDID) 259x173 mm`。compositor `ZWL OUTPUT open width=1920 height=1280`、`first frame 1920x1280 ... shown on the 1920x1280 panel`。画面（scanout の buffer）: Keiland の system bar、壁紙が 1280 行の全体、wl_shm の窓 2 つ。t=40・120 s。eDP の `PP_STATUS=0x00000000`（panel の電源は入っていない） |
| `display=auto`（回帰） | `build/ws075-h2/auto/` | **PASS**。`display output: eDP panel (display=auto)`、1920x1080 の eDP で Keiland |
| `display=hdmi display.mode=1920x1080@60` | `build/ws075-h2/hdmi-1080/` | **PASS**。`mode from display.mode, EDID timing`（CEA 拡張の 148.5 MHz）、1920x1080 の Keiland |

画面: `/home/awe/zedBSD-rpi4/build/ws075-shots/hdmi-h2-*.png`（`display-hdmi-1920x1280-t040/t120`、`display-hdmi-sheet`、
`display-auto-edp-1920x1080-t040`、`display-auto-sheet`、`display-hdmi-mode1080-t040`）。**これは pipe が読む buffer の中身であり、
LCD の発光の写真ではない。LCD の目視は未実施。**

USB: 3 回とも HDMI の点灯中（各 200 秒余り）に 5330 の USB に新しい device は無い（H1 と同じ。touch の USB は未列挙）。

### 未実施

- LCD の目視・写真（ユーザー）。DVI mode を LCD が受けるかの目視も同じ。
- HDMI を抜いた boot での eDP への fallback（遠隔で抜けない）。connector が disconnected の分岐は実機で未確認。WRPLL の拒否での
  fallback は 1 回目の run で実機で通った。
- 蓋を閉じた boot、長時間の表示、shutdown での pipe B の停止（H4）。
- QEMU（Venus）の回帰: i915 の変更は Venus に届かないので未実施。

### boot test（QEMU）

boot の parameter の parser を変えたので、lean な amd64 image（`ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-userland.mk`、
`BUILD=build/h2-boot`、display= なし）で `plan/tools/boot-test.sh` → **PASS**（login prompt、
`/home/awe/zedBSD-rpi4/build/ws075-shots/hdmi-h2-boot-test-login.png`）。build は compiler の warning 0（kernel は zdesktop の
config でも warning 0）。

## 規約

新しいコード（`output.c`・`output.h`・boot.c の追加・modeset.c と display.c の変更・host の試験）は `plan/coding-style.md` の全文で
書き、`plan/tools/style-check.py` で新しい file の指摘 0、変えた行の指摘 0。`git diff --check` 清浄。CEA の表は i915 の既存の
code と同じく designated initializer（C99）を使う。
