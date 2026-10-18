<!-- awesome-plan project=zedbsd record=ws260-p002 -->

# ws260-p002 試験の土台（golden の書き込みの列と数の計算の表）

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.1。

## 作った物

- `plan/ws260/tests/run-golden.sh [--record] [OUT]`: 次の 4 つの記録を今の tree で作り、`plan/ws260/tests/golden/` と比べる（違いは diff の先頭 40 行）。
  - `modeset.txt`・`outputs.txt`: host-gen9-modeset-test・host-gen9-outputs-test の場面ごとの register の書き込みの列（`gen9-fake-hw.c` の reset のたびに 1 場面、番地・値を順に）。8 場面・2 場面。
  - `backlight.txt`: 新しい `host-gen9-golden-test.c`（MMIO BAR の stand-in を model に繋ぎ、SPT と CNP の backlight の start、get、set 0/50/100 %、suspend/resume、stop）。7 場面。
  - `table.txt.gz`: 新しい `host-gen9-mode-table.c` の数の表（95,619 行）: CDCLK、EDID からの mode、DP の link と M/N、DPCD の rate、訓練の間隔、DPLL の語（DP、HDMI の WRPLL 20〜650 MHz、ref 24/19.2 MHz）、sku・buf trans・signal level・voltage max・HDMI の level、訓練の判定、WM・DDB（2000 組）、WM・DDB の語、PCODE の latency、PWM の decode と対応、firmware の readout（SV7 と変形 2000）、flip の窓・port clock・pixel clock。入力は格子と固定の擬似乱数。
- `gen9-fake-hw.c/h`: golden の記録（`i915_gen9_fake_golden_open/close`）、log の溢れの数、xd の register の窓口の hooks（`i915_gen9_fake_xd_hooks`、xd-io の backend として model に繋ぐ口）、MMIO の stand-in の hooks（`i915_gen9_fake_mmio`）。
- `host-kernel.c`・`host-test.h`: MMIO BAR の stand-in（raw_read32・raw_write32・read32・write32・posting_read32）が test の model の hooks（`i915_host_mmio`）に届く。無ければ今までどおり test を止める。`drv_pci_find_class` は model の時は bridge 無し。`kern_backlight_register/unregister` の stand-in（ops を test が呼ぶ）。
- `plan/ws260/tests/golden-stubs.c`（native-decide-stubs と同じ、boot line は空）、`plan/ws260/tests/kbuild.sh NAME`（build 3 組: amd64 既定、I915_TESTS=y の vke2 と execution。display の組は vmunix.mk が拒否するので使わない）。

## 確かめ（2026-10-18）

- `sh plan/ws260/tests/run-golden.sh build/tmp/ws260-g6`・`-g7`（記録の後に 2 回）: 4 つとも same（modeset 299 行、outputs 606、backlight 17、table 95,619）。2 回流して同じ。
- gen9 の host 試験 5 本: flip 48・backlight 21・mode 71・modeset 80・outputs 29、全部 0 failures。
- `plan/ws245/tests/run-xd-host-tests.sh`（host-kernel.c を変えたため）: all passed。
- `sh plan/ws260/tests/kbuild.sh p002`: 3 組とも rc 0、warning 0。

## 残り・注意

- flip の書き込みの列（latch・arm・restore・irq）と firmware の点検（読むだけ）は、GGTT・buffer・VT-d の stand-in が要るので、p013 で gen9-flip.c・gen9-firmware.c を変える前にその段の最初に今の code で取る（その段まで両 file は変えない）。
- 後の段は、自分の範囲の場面が golden に無ければ、変える前に今の code で足して記録してから置き換える。
