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

### 残り・再開の手順

- 残り 565 件（i915 render 153・tests 167・compiler 40・intel 25・top の GT の file 群、bcm2711 79、venus 6、compiler 15、amdrdna 2、`include/uapi/gpu-op.h` 3）。
  i915 の render の image.c「design N3」・video.c「(N4)」、tests/execution/README.md の LCD-G・P7-IPC の類も対象。
  bcm2711 は `bcm2711_stage_mark` の log が N0・P0・R0 の段の符号を持つ（readout.c ほか）。照合する試験の grep が要る。
- 手順: 群ごとに `git grep` で文脈を読み、comment を直す → `sh build/tmp/kbuild.sh ws251p002-gN` → `sh build/tmp/ws251-objcmp.sh ws251p002-base ws251p002-gN`
  （log を変えた file 以外の object が同じ）→ 群の host 試験 → WIP commit。基準の build `build/ws251p002-base` は残してある。
- 段の符号の検査（機械の検査の外）: `git grep -nE '\bN[0-9]\b|\bP[0-9]\b|\bR[0-9]\b|\bLCD-[A-Z]|\bF[0-9]+\b|[Ss]tage [0-9]|test build|QEMU|5330'` を群ごとに目視。
- 掃除（Q1 に依頼）: `build/ws251p002-base`（再開に要るので残すなら後で）・`build/ws251p002-g1`・`build/ws251p002-xd`・`build/ws251p002-h2` と各 .log。
