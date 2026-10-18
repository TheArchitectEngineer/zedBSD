<!-- awesome-plan project=zedbsd record=ws251-p002 -->

# ws251-p002: GPU ドライバ（src/drivers/gpu/）の comment と log から開発の管理情報を消す

Status: in-progress（2026-10-17 q1240-i01、P1。i915 display の群まで済み、ラップアップで中断）
Disposition: normal
Parent: [WS251](../ws.md)

## 由来（2026-10-17 21:1x ユーザー、原文）

「そのほかに、コメントからWSとかBUGSとかSTAGEみたいな開発管理の情報を削除することが必要です。"The test build run under QEMU leaves the display hardware alone"みたいな話はplan/に書けばいいのであって、よそのプロジェクトでも流用されるソースコードから削除するべきと思います。このプロジェクトのGPUドライバは、Mesaを使わないという点では、現時点で世界で唯一のOSSのGPUドライバです。おそらく有効活用してくれる人がいるはずです。」

解釈（Q1）: WS251 の削除の対象に、番号の類だけでなく、このプロジェクトの開発・試験の進め方の話（どの試験の build・QEMU・実機の回でどう振る舞うか、どの段で何を入れたか、開発の段の符号）を加える。そうした説明は必要なら plan/ に移す。hardware と code の振る舞いの説明は残す。GPU ドライバは他のプロジェクトでも使われうるので先に行う。

## 範囲

- `src/drivers/gpu/` の全部（i915 → bcm2711 → nvrtx・venus・compiler・amdrdna の順）と、その driver の header（`include/` の GPU の物）。
- 消す物: WS・Phase・Queue・Bug・試験の回の番号、plan/ への参照、日付つきの決定、段の符号（N0・LCD-B・P5-0・F1 の類）、STAGE・stage の開発の段の話、試験の build・QEMU・試験機の運用の話（例「The test build run under QEMU leaves the display hardware alone」）。
- 試験の code（`tests/` の下の host 試験・ktest）の comment も同じ規則。ただし試験が何を確かめるかの説明は残す。
- log の文字列を変える時は照合する試験・道具（plan/tools・plan/ws*/tests・tests/ の scenario）を追従させる。
- 振る舞いは変えない（comment と log の文字列だけ）。HAL の API・toolchain は触らない。

## 完了の条件

- `git grep -nE '\b[Ww][Ss][0-9]{3}\b|\bq[0-9]{3,4}\b|BUG-[0-9]{3}|\bT[0-9]-[0-9]+|plan/'` が src/drivers/gpu/ で 0 件。
- 開発の段・試験の運用の話の目視の点検（file ごとの記録）。
- build 3 組（既定・vke2・execution）と Pi 4 の arm64 の build が warning 0。i915 の display の host 試験（run-xd-host-tests）と変えた log の照合先の試験が通る。
- 振る舞いを変えていない根拠: comment だけの file は kernel の binary の比べ（log の文字列を変えた file は除く）。

## 結果（q1240-i01、P1、2026-10-17、ラップアップで中断）

### 群 1: i915 display（`src/drivers/gpu/i915/display/`）— 済み

- 34 file の comment と 1 file の log の文字列。機械の検査: src/drivers/gpu 全体 602 → 565 件、display は 37 → 0 件。
- 消した物: WS229・WS237（stage 1〜3、p005）・ws113-p011/p012/p013・BUG-310 の参照、日付つきの決定（gen9-display.h の「2026-10-15 user」）、
  設計の ID（display.c の D-ID A2）、review の符号（present.c の F9）、段の符号（output.c の N0）、試験の運用（capture.c/h の「test build run under QEMU」・
  QMP pmemsave・host harness、internal.h の「test machine」、edp-device.c の「5330」、resident.c の「QEMU monitor」、survey.c・display.c の「test build」）。
  display.c の QEMU の emulated bridge の ID は hardware の事実なので残す。xd-edp・display-power・vbt-shell・vga の `*_STAGE` は code の段（trace・eDP の手順）なので残す。
- 変えた log の文字列（gen9-firmware.c の 7 行）: `"i915: N0-lite (gen9): ..."` → `"i915: gen9 firmware display: ..."`。照合する試験・道具は無い
  （plan/tools・plan/ws*/tests・tests/ を grep。`plan/ws245/tests/run-xd-host-tests.sh` の検査は gen9- の file を除外しており、新しい語は禁止の語に当たらない）。
- 確かめた事: build 3 組（`sh build/tmp/kbuild.sh ws251p002-g1`）default・vke2・execution とも rc=0 warning 0。基準（`ws251p002-base`、e9157f2c2）と
  `build/tmp/ws251-objcmp.sh ws251p002-base ws251p002-g1` で src/drivers/gpu の object（270/273/280）を比べ、違うのは log を変えた gen9-firmware.o だけ。
  `sh plan/ws245/tests/run-xd-host-tests.sh build/ws251p002-xd` all passed、`sh plan/ws075/tests/hdmi/host-output-test.sh build/ws251p002-h2` 80 checks, 0 failures。
- 未確認: WS237 の gen9 の host 試験（`plan/ws237/tests/run-gen9-*-host-test.sh`）と WS229 の `run-bootfb-host-test.sh`（ラップアップで未実施）。

### 群 2: i915 render（`src/drivers/gpu/i915/render/`）— 済み（2026-10-18 q1242、P1）

- 34 file の comment だけ。機械の検査の数は src/drivers/gpu で 565 → 412 件、render は 153 → 0 件。
- 消した物: ws229・ws101・ws075・ws031・ws083・ws035・WS068 の Phase の参照、BUG-117・BUG-244・BUG-260、T1-608、ws083 の R-S1〜3、設計の番号（design §N・D17・D18・D21〜D23・B2・N3・N4・items N）、試験機の名前（compute.c の「which ran on the Latitude 5330」）、codec.h と vulkan-codec.inc の生成の道具の `plan/ws031/handover/tools/` の path（道具の名 gen_vk_server_codec.py は残した）。
- 残した物: 「test build」は I915_TESTS の build にある weak の checkpoint と video の hang の試験の code の説明（code の振る舞い）なので残した。
- **Q1 への依頼**: vulkan-codec.inc は plan/ws031/handover/tools/gen_vk_server_codec.py の出力。道具の書く header の行を同じに直さないと、作り直した時に path が戻る（道具は範囲の外）。
- 確かめた事: 新しい基準 `build/ws251p002-base2`（tree 0a59b8a8a、前の基準 e9157f2c2 からは WS245 の merge で source が変わったので作り直した）。`sh build/tmp/kbuild.sh ws251p002-g2` default・vke2・execution とも rc=0 warning 0、`ws251-objcmp.sh ws251p002-base2 ws251p002-g2` で object 270/273/280 の差なし、vmunix 3 組とも同じ（comment だけ）。

### 群 3: i915 の top の file・intel・compiler と gpu.c — 済み（2026-10-18 q1242、P1）

- comment だけ。src/drivers/gpu の数は 412 → 269 件、i915 の top・intel・compiler・gpu.c は 0 件。
- 消した物: Phase の参照（ws035・ws083・ws101・ws075・ws031 ほか）、BUG-077 ほか、WS031 E-127、\`plan/\` の path（試験の script・設計・退役した生成の道具・Linux の reference の置き場）、日付つきの決定（i915.c の 2026-10-14 U1・U2、2026-10-15）、memory.h の経緯（「it was 32 MiB … before ws035-p066」）、試験機の名前（eu-encoding-gen12.h・genxml.h の「ran on the Latitude 5330」）、session.c の「ws083 §4.3」。engine-table.inc の出典は \`(drivers/gpu/drm/i915)\` に（Linux の tree の中の path。版と SHA-256 の行は残した）。
- 残した物: 「test build」は I915_TESTS の checkpoint の説明（code の振る舞い）。dma.h の「Under QEMU with VFIO」は DMA の address の hardware・仮想化の事実。
- 確かめた事: \`sh build/tmp/kbuild.sh ws251p002-g3\` 3 組 rc=0 warning 0。\`ws251-objcmp.sh ws251p002-base2 ws251p002-g3\` で違う object は memory.o・gpu.o（それぞれ数 byte、comment の行が減って行番号の情報がずれた分）だけで、vmunix は 3 組とも同じ。

### 群 4: bcm2711（`src/drivers/gpu/bcm2711/`）— comment は済み（2026-10-18 q1242、P1）

- comment だけ（37 file）。src/drivers/gpu の数は 269 → 190 件、bcm2711 は 79 → 0 件。消した物: ws141・ws208・ws211・ws213・ws236 の参照、BUG-314、T1-631・T1-637・T1-674、review-b2 H1・M2、「recorded by WS141」と「recorded with their sources in WS141 p013」。
- **残した物（Q1 の判断が要る）**: `bcm2711_stage_mark`・`bcm2711_stage_allowed`・`bcm2711_stage_pause` の段の名前（P0〜P5・N0・R0・V0〜V7）。これは boot の行の `rpi4gpu.stop=<stage>`・`v3d.stop=<stage>`（stage.c）が受ける名前で、log の文字列であると同時に boot parameter の値になっている。名前を変えると parameter の振る舞いが変わり、この Phase の「振る舞いは変えない」の外になる。案: (a) 段の名前は code の interface として残し、comment の中の「stage P0 found it」などだけ意味の言い方に直す、(b) 段の名前を意味の名前（discovery・readout・scanout…）に替え、parameter と照合の試験を一緒に直す（別の Phase）。
- 確かめた事: x86 の build 3 組は bcm2711 を含まないので、Pi 4 の arm64 の build（`config/ci/config-rpi4.mk`）を前後で作った: `build/ws251p002-arm-base`（tree 31d130233 相当、変更の前を stash で）と `build/ws251p002-arm-g4` とも rc=0 warning 0。gpu の object 88 個に差なし、vmunix も同じ。

### 群 5: i915 の tests（`src/drivers/gpu/i915/tests/`）— 済み（2026-10-18 q1242、P1）

- comment・README・生成の道具の comment と、その出力の header（compute-shaders の regenerate.py と compute-shaders-gen.inc、feature・generality の regenerate.py の comment は出力と同じ文に）。i915/tests は 167 → 0 件、src/drivers/gpu 全体は 23 件。
- 消した物: Phase・Queue・Bug・T1 の番号、WS031 E-118・E-127・T1 の生成の物の名、日付つきの経緯（q762）、render/README.md の `plan/ws031/phase014/phase.md` の「後回し」の段落。`plan/wsNNN/…/<file>` の path は file の名だけにした（試験の runner の `vkloop-hw.sh`・`run-*-host-test.sh`、gen9 の asm の入力、生成の道具）。それらの file は plan/ にあり、source の tree からは名前でしか指せない。
- 残した物: 古い試験の名前（execution/README.md の P6B-RC6・P7-IPC などの表、LCD-G）は、移植しなかった試験の名前の一覧として残した。
- 確かめた事: `sh build/tmp/kbuild.sh ws251p002-g5` 3 組 rc=0 warning 0、`ws251-objcmp.sh ws251p002-base2 ws251p002-g5` の差は群 3 と同じ memory.o・gpu.o（行番号）だけで vmunix は 3 組とも同じ。`run-xd-host-tests.sh build/ws251p002-g5-host` all passed。`git diff --check` 0。

### 残り・再開の手順

- 残り 23 件（群 5 の後。venus 6、compiler 15、amdrdna 2、`include/uapi/gpu-op.h` 3）。
  i915 の render の image.c「design N3」・video.c「(N4)」、tests/execution/README.md の LCD-G・P7-IPC の類も対象。
  bcm2711 は `bcm2711_stage_mark` の log が N0・P0・R0 の段の符号を持つ（readout.c ほか）。照合する試験の grep が要る。
- 手順: 群ごとに `git grep` で文脈を読み、comment を直す → `sh build/tmp/kbuild.sh ws251p002-gN` → `sh build/tmp/ws251-objcmp.sh ws251p002-base ws251p002-gN`
  （log を変えた file 以外の object が同じ）→ 群の host 試験 → WIP commit。基準の build `build/ws251p002-base2`（0a59b8a8a）を使う（古い `ws251p002-base` は消してよい）。
- 段の符号の検査（機械の検査の外）: `git grep -nE '\bN[0-9]\b|\bP[0-9]\b|\bR[0-9]\b|\bLCD-[A-Z]|\bF[0-9]+\b|[Ss]tage [0-9]|test build|QEMU|5330'` を群ごとに目視。
- 掃除（Q1 に依頼）: `build/ws251p002-base`（再開に要るので残すなら後で）・`build/ws251p002-g1`・`build/ws251p002-xd`・`build/ws251p002-h2` と各 .log。
