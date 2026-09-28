<!-- awesome-plan project=zedbsd record=ws075p013 -->

# ws075-p013: デモの形での確認（H4）

Phase ID: `ws075-p013`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28。デモの image で splash → greeter（HDMI 1920x1280）→ login → session（HDMI）→ 30 分の連続表示 → Log Out →
greeter の Shut Down を実機の passthrough で通した。途中で見つけた 2 つの不具合（H2 の pipe B の frame counter、i915 の GPU node の前に
sessiond が諦める）は直すか回避した。eDP への fallback は試験の switch で確認。最終の image（Notes・PDF Viewer 入り）で Notes の起動が
kernel の fatal を起こした（不具合 3、未修正、要 Bug ticket）。LCD の目視・bare metal の起動は未実施）
Phase disposition: normal
承認: 2026-09-28 main の依頼（[hdmi-main-output.md](../hdmi-main-output.md) の H4、ユーザーの回答 1〜5、デモは 2026-10-17）。

## 範囲

- デモの image の config（amd64 の graphical boot + `display=hdmi` + デモの application）と build の script。
- 実機（5330）で graphical boot の全体: loader の splash、HDMI の greeter、login、HDMI の session。firmware の splash の出る先、
  takeover の前後の eDP、greeter の大きさ、scanout の画面。
- 30 分の連続表示（session、QMP pointer で数秒ごとに窓を動かす）: underrun・停止・BUG-085 形の freeze が無いこと、session からの
  Shut Down と電源断の確認。
- HDMI を抜かずにできるなら eDP への fallback の確認。
- 範囲外（提案だけ）: splash を HDMI に出す方法、HAL・UAPI の変更。

## 作ったもの

| 所 | 内容 |
| --- | --- |
| `plan/ws075/demo/config-demo-hdmi.mk` | `plan/ws031/tests/config-zdesktop-hw.mk`（i915、Vulkan・EGL/GLES・Wayland、compositor、X server、App Home の application）+ Notes・PDF Viewer（`libjpeg-compat libpdf notes pdfviewer`、main の merge の後に追加）+ `ZEDBSD_GRAPHICAL_BOOT := y`（logo・`kmsg=quiet`・`login=graphical`）+ `ZEDBSD_BOOT_EXTRA_LINES ?= display=hdmi` |
| `plan/ws075/demo/build-demo-image.sh [BUILD] [passthrough] [make の引数...]` | 上の config で `disk-image`。App Home は `plan/ws035/demo/apps.conf`（Files・Notes・Terminal・PDF Viewer・Browser・Model viewer・Gears・X terminal）、font と壁紙（git の外、`build/ws035-fonts/`・`build/ws035-wallpaper/wallpaper-1080.ppm`）。`passthrough` は 5330 の QEMU passthrough 用（guest に OpRegion が無いので `I915_TEST_VBT=y`）。bare metal では付けない。zedbsd.cfg: `logo=logo.ppm kmsg=quiet login=graphical display=hdmi` |
| `plan/ws075/demo/rc.conf`・`greeter_gpu`・`greeter-gpu.sh` | 下の不具合 2 の回避: service `greeter` の代わりに `greeter_gpu`（`/dev/gpu0` を最大 120 秒待ってから `exec /sbin/sessiond`、待った秒数は `/var/log/greeter-gpu.log`）。rc.conf は base の rc.conf に greeter を無効、greeter_gpu を有効にしたもの |
| `src/drivers/gpu/i915/display/vblank.c` | 不具合 1 の修正（下） |
| `src/drivers/gpu/i915/display/output.c` | 試験の switch `-DI915_TEST_HDMI_ABSENT=1`: HDMI の probe の答えを disconnected とみなす（抜いた cable と同じ分岐）。既定の build には入らない |
| `plan/ws075/tests/hdmi-h4-hw.sh start IMAGE OUTDIR \| ctl ARGS... \| fetch OUTDIR \| stop OUTDIR` | 実機を段ごとに動かす: `start` は lock（`hdmi/h4-lock.sh` が `/tmp/i915-hw.lock` を stop まで持つ）、image と helper を `~/bigbang/h4/` へ、QEMU を起動（`hdmi/h4-qemu.sh`）。`ctl` は `hdmi/h4-ctl.py`（`shot NAME`: std VGA の screendump と resident の 2 つの buffer の memsave、`pointer`（move・down・up・sleep・drag）、`keys`、`hmp`、`load 秒 周期 X0 Y0 X1 Y1`（2 つ目の QMP socket で往復の drag）、`quit`）。`fetch` は log と PNG（`hdmi/h4-png.py`）、`stop` は QEMU の終了、guest の disk の log、lock の返却 |
| `plan/ws075/tests/hdmi/h4-qemu.sh` | 5330 の上で QEMU（run-parity-vk.sh の基準の条件 + std VGA + QMP 2 つ + USB tablet・keyboard、`H4_MINUTES` 分で打ち切り）。起動直後から std VGA を 250 ms ごとに撮る（resident の buffer ができるまで、`shots/splash-*.ppm`） |

## 見つけた不具合と対処

1. **HDMI（pipe B）の resident display が最初の lease の終わりで失敗し、login の後の session が画面に出ない**（H2 の不具合、修正済み）。
   run2（`build/ws075-h4/run2/`）: greeter の lease の終わりの flip の settle が `flip to 0xbc0c0000 did not complete: event_rc=-110 live
   0xbc0c0000`（surface は latch 済み）で失敗 → `resident display: ended FAIL (show rc=5 ...)` → `presentation fails from now on`。session の
   compositor は 0 frame で終わり、sessiond は greeter を出し直すが画面は出ない（`hdmi-h4-before-fix-stuck-at-login.png`）。原因:
   run の vblank 待ちの frame の hook（`vblank.c` の `i915_lcd_kernel_frame()`）が観察表の `PIPE_FRMCOUNT_G4X`（pipe A、0x70040）を
   読んでいた（XXX の注記どおり）。HDMI の run は pipe B なので counter は 0 のまま、flip の event は全て時間切れ（上限 100 ms。run2 の
   perf は present あたり flip 50.46 ms、修正後の run3 は 0.05〜7.45 ms）。修正: run の parameter の pipe（`k->p->pipe`、無ければ pipe A）の `PIPE_FRMCOUNT_G4X(pipe)` を読む。修正の後の
   run3 は lease 1（greeter）・2（session、3214 flip）・3（greeter）の全てが `ended PASS ... first anomaly: none`。eDP（pipe A）は run4 で
   lease 1・2 とも PASS（回帰なし）。
2. **i915 の GPU node の前に sessiond が起動し、console の login に戻る**（回避、本来の修正は WS035 へ提案）。run1（`build/ws075-h4/run1/`）:
   `SESSIOND CONSOLE reason=no-display errno=6`（ENOENT、`/dev/gpu0` がまだ無い）→ getty（`hdmi-h4-before-wait-console-login.png`）。
   Venus は起動が速いので出なかった。i915 は GT・firmware の初期化で約 1 秒遅れて node を出す（run3 の `greeter-gpu.log`: `/dev/gpu0
   after 1 s`）。デモの image は `greeter_gpu`（上）で回避した。提案: sessiond の `main_ready()` が、GPU の driver が attach 中の間は
   `/dev/gpu0` を上限付きで待つ（例: PCI に display class の device がある間だけ待つ）か、init に device の準備の依存を持たせる。
   GPU の無い機械で console の login を遅らせない形が要る（ENOENT は GPU の無い機械でも同じ）。
3. **Notes の起動で kernel が止まる**（未修正、Bug ticket が要る。H4 の範囲外の kernel・i915 の不具合）。run5-apps
   （`build/ws075-h4/run5-apps/`、最終の image）: App Home の Notes を押した約 2 秒後、Notes の最初の draw（`vk: first draw ... target
   1024x768 ... depth yes`）の直後に `amd64 fault v=6 rip=FFFFFFFF:8031201D` → `fatal: src/hal/amd64/int.c:395: unhandled amd64 fault`
   （`hdmi-h4-notes-launch-kernel-fault.png` は止まった画面）。gdbstub（QMP の `gdbserver`、5330 の gdb と stripped でない vmunix）で
   解析: CPU1 の trap frame は `spin_unlock+0x35`（`ud2` = 持ち主の違う lock の解放の trap）、呼び出しは `sched_sleep_locked+0x103` ←
   `waitq_sleep+0x18e` ← `i915_timer_thread+0x83`（`src/drivers/gpu/i915/workqueue.c`）。解放しようとした lock（rdi = 0xffff800100470688、
   `timers->lock`）は `held=1 owner_cpu=0 owner_valid=1` で、解放したのは CPU1。仮説（未確認）: timer thread は `timers->lock` を
   割込み許可のまま持って loop を回る（`spin_lock` は preemption を止めない）ので、lock を持ったまま preempt されて別の CPU へ移り、
   次の `waitq_sleep` の解放が持ち主の検査に掛かった。Notes は timer の仕事を増やしたきっかけと見られる（run3 の 30 分では出ていない）。
   BUG-085（i915 の compositor の停止）と同じ系統かは未確認。1 回の観察（再現の率は未測定）。デモの Notes に直接効くので優先が高い。

## 実機の結果（5330、VFIO passthrough の QEMU の guest。2026-09-28。QEMU の emulation の証拠ではない）

証拠は worktree の `build/ws075-h4/run{1,2,3,4-absent}/`（`kernel.log` = debugcon の kernel の全 record、`serial.log`、`splash.log`、
`load.log`、`guest-logs.txt` = guest の disk の sessiond・greeter の log、`shots/`）。画面は worktree の `build/ws075-shots/hdmi-h4-*.png`。

| run | image | 結果 |
| --- | --- | --- |
| run1 | 修正前 | 不具合 2: console の login |
| run2 | greeter_gpu の回避あり | greeter は HDMI に出た。login で不具合 1 |
| run3 | 両方の修正 | **PASS**（下の全て） |
| run4-absent | run3 + `-DI915_TEST_HDMI_ABSENT=1` | **PASS**: eDP への fallback |
| run5-apps | 最終の image（main の merge の後、Notes・PDF Viewer を加えた） | greeter・login・App Home（Notes・PDF Viewer が出る）は PASS。**Notes の起動の約 2 秒後に kernel の fatal**（下の不具合 3） |

### run3: boot から shutdown まで

- **splash**: firmware の画面は passthrough では QEMU の std VGA（OVMF の GOP。iGPU は `rombar=0` で GOP を出さない）。std VGA を 250 ms
  ごとに撮った 36 枚: 0〜1.0 s は OVMF（TianoCore）、1.27 s から Kei の splash と kernel の quiet の spinner（回る）、8.9 s に resident の
  buffer ができた（greeter の lease）。splash の画面は shutdown まで std VGA に残る（spinner は止まる、`hdmi-h4-halted-firmware-display.png`）。
  **実機の LCD と eDP に splash が出るかはこの試験では分からない**（下の「未実施」）。
- **eDP**: passthrough の guest では firmware が pipe A を点けない（`N0 pipe A: POWER_OFF`、`edp pps before: PP_STATUS=0x00000000`）。
  `display=hdmi` では panel の power sequence・backlight を呼ばない（PP_STATUS は 0 のまま、DPCD を読む間だけ VDD を強制し 3 秒後に
  切る予約: `edp late: ... KEPT: vdd_hw=1 ... off_reserved=1 (in 3000 ms)`）。**eDP は boot から shutdown まで暗い**（register の証拠。
  目視は未実施）。
- **greeter**: `display output: HDMI on DDI B, pipe B, DVI mode: 1920x1280@60 Hz 164360 kHz (mode from EDID) 259x173 mm`、greeter は
  1920x1280 の全画面（`hdmi-h4-greeter-hdmi-1920x1280.png`、scanout の buffer）。
- **login → session**: Enter（root、空の password）→ `SESSIOND HANDOFF` → lease 1 が `ended PASS`、lease 2 が HDMI を点け直して session の
  desktop（`hdmi-h4-session-hdmi-1920x1280.png`）。lease の替わり目で pipe は止めて modeset し直す（HDMI は一瞬暗くなるはず、長さは未計測）。
  App Home（`hdmi-h4-session-app-home.png`）から Terminal（`hdmi-h4-session-terminal.png`）。
- **30 分の連続表示**: Terminal の title bar を 4 秒ごとに (1100,338)⇔(1400,538) へ drag（`load.log`: 14:35:32〜15:05:33 に 450 回）。
  5 分ごとの画面（`hdmi-h4-long-run-sheet.png`、system bar の時計が 05:40→06:05 と進み、窓の位置が変わる）。kernel の perf の行（約 8 秒
  ごと）は session の間の全 225 区間で present が 8〜15（0 の区間なし、停止・freeze なし）。session の lease 2 の終わり: `ended PASS
  (show rc=5; flips 3214, stop confirmed, buffers released=1, power refs held 0, first anomaly: none)`。underrun は steady の間の
  `ICL_PIPESTATUS` の sticky bit を stop で読む検査（`first anomaly` に出る）で **なし**。show rc=5 は run log の容量超過（2048 entry を
  保持、104086 を捨てた。判定は counter で行う設計）で、判定は PASS。hang・fault・device lost の報告なし。session は 05:33:32〜06:06:11
  （約 33 分）。
- **shutdown**: session には Shut Down が無い（App Home は Lock Screen と Log Out まで）。Log Out → greeter（`hdmi-h4-greeter-after-logout.png`、
  lease 3、HDMI を点け直す）→ greeter の Shut Down（1840,1238）。`SESSIOND POWER poweroff`、`sessiond: poweroff from the graphical login`、
  `SESSIOND STOP`、lease 3 の `ended PASS ... stop confirmed`（pipe B は `TRANSCONF` 0、`PLANE_SURF` 0）。QMP: `VM status: running`、4 つの
  CPU が全て `HLT=1`、`RFL=00000002`（割込み禁止）、RIP は `amd64_lapic_panic_all`（CPU0）と `hal_cpu_park`（CPU1〜3）= init の
  `KERN_SYSTEM_HALT` → `kern_platform_halt()` の停止。**amd64 の「電源を切る」は halt であり、ACPI の S5 の電源断ではない**（ACPI 未実装）。
  実機では画面が消えて CPU が止まるが、電源は入ったまま（電源 button の長押しが要る）。

### run4-absent: eDP への fallback

`display output: I915_TEST_HDMI_ABSENT takes the HDMI sink as absent (probe said 1)` → `display output: eDP panel (display=hdmi, but no
HDMI sink is connected at boot: rc=13)`。greeter と session が eDP の 1920x1080（`hdmi-h4-fallback-edp-greeter-1920x1080.png`・
`hdmi-h4-fallback-edp-session-1920x1080.png`）、lease 1 は `ended PASS`。これは connector が disconnected の分岐の確認で、cable を
抜いた実物の状態（SDEISR の live status が 0、EDID の読めない状態）ではない。shutdown はこの run では行わず QMP で QEMU を止めた。

## 試験と build

- build: `plan/ws075/demo/build-demo-image.sh build/h4-demo passthrough`（worktree）、`... build/h4-absent passthrough
  ZEDBSD_TEST_CPPFLAGS=-DI915_TEST_HDMI_ABSENT=1`。compiler の warning は外部の Noct の既存の 1 件だけ（kernel・driver は 0）。
  共有の `build/llvm` は触っていない（BUG-089 の回避: `build/llvm-source`・`toolchain/llvm/distfiles` を main への symlink、patch の mtime を
  main に合わせた）。
- host: `plan/ws075/tests/hdmi/host-output-test.sh` → 80 checks, 0 failures（output.c の既定の build）。vblank.c の host 試験は無い
  （実機の run3・run4 が証拠）。
- 規約: `plan/tools/style-check.py` で変えた行（`vblank.c` の `i915_lcd_kernel_frame()`、`output.c` の追加）の指摘 0（vblank.c の他の行の
  既存の指摘は別）。`git diff --check` 清浄。
- QEMU の boot test（Venus）: 未実施（変えたのは i915 の driver と試験の switch と plan の script だけで、QEMU の guest の経路は変わらない）。

## 未実施・ユーザーが要るもの

- **LCD と eDP の目視**。passthrough では firmware の画面は QEMU の std VGA であり、5330 の Dell の firmware の GOP がどの出力（eDP・HDMI・
  両方）に loader の splash を出すかは分からない。bare metal の起動（USB の stick から）はこの Phase では行っていない（遠隔で起動すると
  ssh で戻れない）。ユーザーに依頼: デモの image を USB に書き、蓋を開けた 5330 に LCD を HDMI で繋いで起動し、(a) splash がどちらに
  出るか、(b) i915 の takeover の時に eDP がどうなるか（N0/N1 が firmware の pipe A を止めると暗くなるはず）、(c) greeter が LCD の全画面か、
  を見る。bare metal では `passthrough` を付けずに build する（VBT は OpRegion から）。
- 実物の cable を抜いた boot での fallback（上の run4 は switch による分岐の確認）。
- 蓋を閉じた boot（ユーザーの回答 1 により不要）。
- touch の USB（H1 のとおり 5330 に列挙されない、cable の確認待ち）。

## 提案（実装しない）

1. **splash を HDMI にも出す**（bare metal で firmware が eDP だけに出す場合）:
   (a) kernel の quiet の spinner を takeover の後も HDMI で続ける: i915 の start で pipe B を点け（resident の buffer を greeter より前に
   作る）、GOP の framebuffer の splash を写して spinner の描画先を i915 の buffer に替える。greeter の lease はその pipe を引き継ぐ。
   (b) loader が GOP の handle を全て調べ、EDID（`EFI_EDID_DISCOVERED_PROTOCOL`）が外の monitor のものに `SetMode` する（Intel の GOP が
   出力ごとに child handle を出す firmware の場合）。(c) Dell の BIOS の設定（外部の画面を主にする、等）をユーザーが確かめる。
   まず bare metal で firmware の出し先を見てから選ぶ。
2. **lease の替わり目で pipe を止めない**: 今は lease ごとに modeset と stop をする（login・logout で HDMI が一瞬暗くなる、LCD によっては
   信号の再検出で数秒）。pipe と buffer を boot 中は持ち続け、lease は present の権利だけを移す。
3. **sessiond の GPU の待ち**（不具合 2 の本来の修正、WS035）。
4. **電源断**: halt ではなく電源を切るには ACPI の S5（`\_S5` と PM1a/PM1b の SLP_TYP・SLP_EN。WS049 の AML）。デモでは Shut Down の後に
   電源 button の長押しが要る。
5. session の App Home に Shut Down（今は Log Out → greeter の Shut Down の 2 段）。WS035 の判断。

## 残り

- 不具合 3（Notes の起動で kernel の fatal）の Bug ticket と修正（i915 の timer thread の lock、または kernel の spinlock と preemption の
  規則）。デモの前に要る。root が ticket を作り担当を決める。
- 上の目視（ユーザー）と、その結果による提案 1 の選択。
- 提案 2〜5 は root が計画に入れるか判断する。
