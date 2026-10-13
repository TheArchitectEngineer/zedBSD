<!-- awesome-plan project=zedbsd record=ws229-p005 -->
# ws229-p005: Vulkan の通しと実機（Gen9）、5330 の回帰

Status: planned（2026-10-14 Q1、q980、P1。準備の部分を先に）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q980
依存: p002・p003（merge 済み）・p004

## 範囲

- 準備（p002・p004 の merge を待たずにできる物）: `tests/execution/eu-test.c` の Gen9 の kernel、Gen9 の実機の USB の image の config（`plan/ws229/tests/config-amd64-gen9.mk`、Gen9 の boot の指定 `i915.gen9=on` 相当（p002 が決める key）を含む）、実機の手順（6・7・8 世代の laptop を USB で直に起動、SSH で Vulkan の試験 eu-test → vkcs → vkx・vke1・vke2・vkc の読み戻し）、5330 の回帰の T1 の依頼の文（`plan/ws101/tests/hw/run-hw.sh`・`plan/ws075/tests/test-hw.sh`）。
- 統合: p002・p004 が main に入った後、全体の build と host の試験、image を作り、実機の手順をユーザーに渡す（Q1 経由）。
- ユーザーの決定（phase001 の末尾）: U1 既定 off（実機 PASS で on）、U2 Gen9 の間は suspend を断る、U3 5330 の回帰 PASS がベータ2 の条件。

## 確かめ

- 実機（ユーザー）で Vulkan の試験の読み戻しが PASS。5330（T1）で回帰が PASS。

## 準備の実行（2026-10-14 P1、q980）

base: main bb57b2397（worktree `agent/p1` に merge）。Gen9 の boot の key は `i915.gen9=on`（Q1 から、p002 で決定、既定 off）。

### 1. `tests/execution/eu-test.c` の Gen9（commit 9edb904cc）

- `drv_i915_test_eu_run()` に `graphics_ver` を足し、`drv_i915_test_execution_eu()` が `gt->graphics_ver` を渡す。9 なら Gen9 の fixture、それ以外は今の Gen12 のまま（draw の試験の `t->graphics_ver` は 0 で Gen12）。
- Gen9 の store kernel `drv_i915_test_eu_kernel_gen9`（36 dword、Gen12 と同じ長さ）: Mesa 25.0.7 の `brw_asm --gen=skl` で `plan/ws229/tests/eu-test-gen9.asm` を組んだ固定の表。mov 6 個（SWSB 無し）→ data port 1 の A64 untyped write（SIMD8、mlen 3: address g3-g4 と data g5 を 1 つの SEND、desc 0x06066efd）→ thread spawner の EOT（"do not dereference URB" の bit 4、0x82000010）→ NOP。zedBSD の encoder を通さない独立の表（GT の不具合と encoder の不具合を分けるため）。
- batch は `drv_i915_test_eu_build_batch_gen()`（旧名は Gen12 の wrapper、draw・ktest-gt の呼び手は不変）。Gen9 の差: STATE_BASE_ADDRESS 19 dword（bindless sampler 無し）、MOCS の uncached は index 0（p002 の `mocs-table-gen9.inc` の I915_MOCS_UNCACHED と一致）、write-back は 2、PIPE_CONTROL の dword 0 に HDC flush の bit を立てない、CS stall だけの PIPE_CONTROL に stall-at-scoreboard を足す（Gen8/9 の規則）、3D→GPGPU の前に 3DSTATE_CC_STATE_POINTERS（valid 0）・write cache の flush（RT・depth・DC）・read-only cache の invalidate（anv の `flush_pipeline_select` の Gen9 の形）、PIPELINE_SELECT は mask 0x3・DOP clock gate 無し（0x69040300/0x69040302）、MEDIA_VFE_STATE に Reset Gateway Timer、thread の上限は 56×subslice−1、IDD の dword 2 は 0（Thread Preemption Disable は Gen9 に無い）。`drv_i915_test_eu_check_pipeline_select_gen()`・`drv_i915_test_eu_fill_idd()` を足した。
- forcewake: Gen9 は RENDER と GT の 2 つだけ取る（Gen12 の media の domain は Gen9 に無い。p002 の MEDIA_GEN9 は試験が触らないので不要）。
- MCR の probe（Gen12 の selector と register）は Gen9 では飛ばし、`i915: MCR-PROBE skipped` を出す。hang の dump の ring fault は Gen9 では 0x4094。

### 2. 実機の image と手順（commit 8e019e481）

- `plan/ws229/tests/config-amd64-gen9.mk`: CI の amd64 の config に、graphical boot・login 無し、kernel の行を console に、`ZEDBSD_BOOT_EXTRA_LINES := i915.gen9=on`、`I915_TESTS := y`、`GEN9_SCENARIO`（eu・vkcs・vkx・vke1・vke2・vkc）から test set と `-DI915_TEST_SCENARIO` を決める。
- `plan/ws229/tests/gen9-images.sh`: 試験ごとの image を `test-image.sh --no-harness` で作る（`build/ws229-gen9/<scenario>/hdd-image.img`）。
- `plan/ws229/tests/gen9-collect.sh HOST SCENARIO LABEL`: SSH（kei/kei、sshpass）で `/var/log/kernel.log` を読み、判定の行と PASS/FAIL を出す。
- `plan/ws229/tests/gen9-hw.md`: ユーザーに渡す手順（Secure Boot off、USB の LAN の adapter、`eu` から順に、判定の行、network が無い時の写真）。

### 3. 確かめ（host と build だけ）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| Gen12 の batch が前後で同一 | main の eu-test.c（bb57b2397）と新しい物を host で compile して batch を `cmp`（作業は `build/ws229-p005/old/`） | 同一（322 dword、SHA-256 09786119…） |
| Gen9 の fixture | `GENXML_CHECK=/home/awe/zedBSD-worktrees/p3/plan/ws101/tests/host/genxml-check.py plan/ws229/tests/eu-test-gen9-host.sh`（新。p004 の `genxml-check.py` の gen90.xml の root を使う。p004 の merge 前なので P3 の worktree の写しを読み取りで使った） | PASS: Gen12 の batch の SHA-256 が一致、Gen9 の kernel の表＝`brw_asm --gen=skl eu-test-gen9.asm`、`brw_disasm`→`brw_asm` の往復が一致、Gen9 の batch 327 dword・descriptor を gen90.xml で decode（field の外の bit 無し）して 14 の期待、Gen12 を gen120.xml で 9 の期待。逆に Gen12 の batch を gen90.xml で decode すると HDC の bit で FAIL（判別できる） |
| test の object | kernel の flag（`-Werror`）で `tests/execution/` の eu-test・draw-test・ktest-gt・ktest・runner・fhd-render を compile | warning 0 |
| 試験の kernel 6 本 | `make ZEDBSD_CONFIG=plan/ws229/tests/config-amd64-gen9.mk BUILD=build/ws229-p005/k-<s> GEN9_SCENARIO=<s> build/ws229-p005/k-<s>/vmunix`（eu・vkcs・vkx・vke1・vke2・vkc） | 6 本とも rc 0、warning 0、`amd64 vmunix check: PASS`（3.30〜3.61 MB） |
| image 1 本 | `OUT=build/ws229-p005/img plan/ws229/tests/gen9-images.sh eu` | rc 0、`build/ws229-p005/img/eu/hdd-image.img`（2.2 GB）、zedbsd.cfg に `i915.gen9=on` |

未実施: 実機（p002・p004 の merge の後、ユーザー）。QEMU（Intel の GT が無いので意味が無い）。`eu-test-gen9-host.sh` の in-tree の `genxml-check.py` での gen90.xml の decode（p004 の merge の後に通る。今の main の版は第 3 引数を無視して gen120 で decode し、Reset Gateway Timer で FAIL する）。

### 4. 5330 の回帰の T1 の依頼の文（p002・p004 が main に入った後に Q1 が出す）

> **依頼主**: P1（ws229-p005）。**目的**: Gen9 の変更（p002・p003・p004）が Gen12 の経路を壊していないこと（ユーザーの決定 U3: ベータ2 に入れる条件）。
> **image**: main の HEAD（p002・p003・p004 を含む commit を書く）。5330 の passthrough の試験の build の標準（`plan/ws075/tests/config-test-hw.mk`、`plan/ws031/tests/vkloop-hw.sh test <scenario>` が scenario ごとの test set を選ぶ）。`i915.gen9=on` は付けない（5330 は Gen12）。
> **流し方**: `OUT=build/ws229-p005/hw-5330 plan/ws101/tests/hw/run-hw.sh ws229 ktest eu vkcs vkx vke1 vke2 vkc`（中で `plan/ws075/tests/test-hw.sh` が `/tmp/i915-hw.lock` を取って 1 つずつ流す。7 本、1 本 5〜10 分）。
> **合否**: 各 `build/ws229-p005/hw-5330/hw-<scenario>-ws229/run.log` の verdict の行で判定。`ktest`: `i915: test ktest: PASS`（ktest-gt の register model に Gen9 の表が足されている、review-1 M6）。`eu`: `i915: test eu: PASS` と `i915: EU-TEST PASS:`、加えて `EU-TEST fixture: batch_hash=` が p005 の前の 5330 の run と同じ値（Gen12 の batch は byte で不変）。`vkcs`・`vkx`・`vke1`・`vke2`・`vkc`: `i915: <name>: verdict PASS (n of n steps passed)` で、n が前回の T1 の PASS と同じ。どれか 1 つでも FAIL・HANG・行が無い、なら FAIL。
> **返し先**: Q1 と P1（または Q1 が p005 を割り当てた担当）。log の場所と verdict の行。FAIL の解析はしない。

## 2026-10-14 ユーザーの指示: 全部入りの試験

「ごめんなさい、テストが多すぎます。ws229は全部入りのテストを作ってください。」→ 実機の試験は 1 つの image・1 回の起動で eu → vkcs → vkx → vke1 → vke2 → vkc を順に流し、試験ごとの要約と全体の判定を出す形に変える（P3）。試験ごとの 6 つの image（Q1 が途中まで作った物）は消した。

## 2026-10-14: SV7（Gen9.5）の自動の試験（P1）

ユーザー:「ssh awe@10.0.10.27 で第8世代Core i5のLinuxに入れます。…i915その他をblacklistして再起動すれば、5330同様に自動でテストできます。」
「Core i5 8thのLet's Note SV7では、自動のテストなので、細かく分けたテストでOKです」→ SV7 では試験の場面を 1 つずつ、5330 の passthrough の道具で流す
（USB の全部入りの image は、手で起動する 6・7 世代の機械のための物として残る）。

### 道具（commit 3f1879119 ほか）

- `plan/ws031/tests/hw-machine.sh`（新）: `I915_MACHINE`（既定 `5330`、`sv7`）で host・iGPU の ID の確かめ・試験の VBT・boot の語を決める。
  sv7: `awe@10.0.10.27`、`8086:5917`、`I915_TEST_VBT=n`、`i915.gen9=on`。5330: 前と同じ（`solaris10-man`、VBT y、boot の語無し、ID の確かめ無し）。
- `plan/ws031/tests/vkloop-hw.sh`・`plan/ws075/tests/test-hw.sh`・`plan/ws101/tests/hw/run-hw.sh` がそれを読む。`I915_TEST_VBT` は環境の値を使う（既定 y）、
  `I915_BOOT_EXTRA` があれば `ZEDBSD_BOOT_EXTRA_LINES=` を make に足し flag の記録に入れる、`I915_IGPU` があれば host の `00:02.0` を読んで違えば止まる。
  `run-hw.sh` の `OUT` の既定は sv7 で `build/ws101-hw-sv7`。lock（centris の `/tmp/i915-hw.lock`）は 5330 と共有。
- `plan/tools/hwsv7/setup.sh`（新）: SV7 の `~/bigbang/` に `run-parity-vk.sh`・`igpu-mode.sh` を写し、QEMU を起動せずに機械を確かめる。
- `plan/tools/hwsv7/README.md`（新）: SV7 の手引き。`plan/tools/hw5330/README.md` に SV7 への行を足し、変わった script の行の番号を直した。

### 確かめ（host と build だけ、QEMU は起動していない）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| machine の選択 | `I915_MACHINE=`（空）・`5330`・`sv7`・`bogus` で `hw-machine.sh` を dash で読み、値と make の語の展開を出す | 空と 5330: host `solaris10-man`・VBT y・boot 無し・ID 無し、make の語は足されない。sv7: `awe@10.0.10.27`・`8086:5917`・n・`ZEDBSD_BOOT_EXTRA_LINES=i915.gen9=on`。bogus: rc 2。環境の `I915_TEST_VBT=y` は sv7 でも勝つ |
| script の構文 | `sh -n` を 4 本に | OK |
| SV7 の準備 | `plan/tools/hwsv7/setup.sh` | rc 0、13 の `ok`（写した 2 本が一致、`8086:5917` が vfio-pci で group に単独、`/dev/kvm`、OVMF、QEMU 10.0.13、`sudo -n`、39 bit、14 GiB、204 GiB、QEMU 無し）、`setup: awe@10.0.10.27 ready` |
| iGPU の ID の読み方 | `ssh awe@10.0.10.27 'cat …/vendor …/device' \| sed 's/^0x//' \| paste -sd:` | `8086:5917` |
| SV7 の image の build | `I915_MACHINE=sv7 BUILD=build/ws229-p005/sv7-ktest ZEDBSD_CONFIG=plan/ws075/tests/config-test-hw.mk VKLOOP_BUILD_ONLY=1 plan/ws031/tests/vkloop-hw.sh test ktest` | rc 0、`vkloop-hw: built build/ws229-p005/sv7-ktest/hdd-image.img`（2.2 GB）。boot の config は `zedbsd-native-uefi-graphical-n-login-n-kmsg-y-i915.gen9=on.cfg` で最後の行が `i915.gen9=on`、compile の行に `-DI915_TEST_VBT` が無い、`.vkloop-flags` は `…\|vbt=n\|capture=n\|boot=i915.gen9=on`。compiler の warning 0（log の `warning:` は gmake の jobserver の 1 行だけ） |

未実施: SV7 の上の QEMU の run（T1）。5330 の run（5330 の振る舞いは変えていない: make の行・host・VBT が前と同じ）。

### T1 への依頼の文（SV7、Q1 が出す）

> **依頼主**: P1（ws229-p005）。**目的**: Gen9 の GT・EU・Vulkan の経路（p002・p003・p004、main に merge 済み）が Gen9.5 の実機の iGPU
> （SV7 の UHD 620 `8086:5917`）で動くかを、QEMU の passthrough で場面ごとに確かめる。ユーザー（2026-10-14）「細かく分けたテストでOK」。
> **前提**: main の HEAD（commit を書く）。`plan/tools/hwsv7/setup.sh` が `setup: awe@10.0.10.27 ready` を出すこと（P1 が 2026-10-14 に確認済み）。
> 5330 と同じ lock を取るので、5330 の試験と同時に流さない。
> **流し方**: `I915_MACHINE=sv7 OUT=build/ws229-p005/hw-sv7 plan/ws101/tests/hw/run-hw.sh ws229 ktest eu vkcs vkx vke1 vke2 vkc`
> （image は lock の外で場面ごとに build、その後 `test-hw.sh` が lock の下で 1 つずつ。7 本、1 本 5〜10 分。`timeout` で括らず background で）。
> 途中で止まった場面があっても残りは流れる。1 本だけ流し直す時は場面の名前を 1 つにする。
> **合否**: 各 `build/ws229-p005/hw-sv7/hw-<scenario>-ws229/run.log` の verdict の行で判定。
> `ktest`: `i915: test ktest: PASS`。`eu`: `i915: test eu: PASS` と `i915: EU-TEST PASS:`（Gen9 の固定の store kernel、`i915: MCR-PROBE skipped` が出るのは正しい）。
> `vkcs`・`vkx`・`vke1`・`vke2`・`vkc`: `i915: <name>: verdict PASS (n of n steps passed)`。
> どれか FAIL・HANG・行が無い、なら FAIL。`run.log` に `the iGPU of awe@10.0.10.27 is …` が出たら、機械の取り違え（試験の FAIL ではない）。
> 最初の run で QEMU の IGD の quirk（OpRegion・stolen memory）が Gen9.5 で働くかは未確認なので、`i915:` の行が 1 つも無い場面は
> `run.log` の `--- vkdemo` の後の行（`i915: vk`・`panic`・`fault`）と、SV7 の `~/bigbang/run-parity-qemu.log` の VFIO の行を添えて返す。
> **返し先**: Q1 と P1（または Q1 が p005 を割り当てた担当）。場面ごとの verdict の行と log の場所。FAIL の解析はしない。

- 2026-10-14 ユーザー（全部入りの取りやめ）:「全部入りの Gen9 の imageはいらないです。あなたがテストに必要な単位で機能を分けたイメージでテストしてください」→ 全部入りの image は作らない。Gen9.5 の試験は SV7 の passthrough で試験ごと（T1-608）。USB の手順（gen9-hw.md）は使わない。

## 2026-10-14: T1-608（SV7）の FAIL の解析と直し（P1）

T1-608（SV7、UHD 620 `8086:5917`、main bfebc43b7 以降）: ktest・eu・vkcs は PASS、vkx 2/9・vke1 5/6・vke2 20/21・vkc 8/9 が FAIL（draw・r1 は解析の時点で実行中）。
log は T1 の worktree の `build/ws229-p005/hw-sv7/hw-<scenario>-ws229/run.log`（verdict と step の行だけを読んだ。serial.log は読んでいない）。

### 原因: Gen9 では頂点の段の push 定数が commit されない（p004、3D の state）

- 落ちた step は全部、頂点 shader が push の register を持つ pipeline だった。vkx は 6 つの pipeline が全部 `vs 624 bytes (… 1 push registers …)` で、
  描画の 7 step（INDEX16・INDEX32・TOPOLOGY・VIEWPORT・GRID・MIP-NEAREST・MIP-LINEAR）が全部 FAIL、描画の無い COPY・MIP-BLIT だけ PASS。
  vke1 は push の有る `vs 1328 bytes (… 3 push registers …)` の UBO だけ FAIL、vs の push が 0 の BLEND・TEX3・TEXOPS・TEXKINDS・LEVELDRAW は PASS。
  vke2 は `pipeline 0: vs 3248 bytes (4 push registers)` の MATRIX だけ FAIL（全部 0）、他の 20 は vs の push が 0 で PASS。
  vkc は `vs 1984 bytes (… 4 push registers …)` の mview だけ FAIL、他の 8 は vs の push が 0 で PASS。
  失敗の画素は消去の色のまま（例 vkx INDEX16 `0xff000000, expected 0xff0000ff`）で、頂点の位置・行列が 0 として読まれた形。
  compute の push（vkcs、CURBE）と、push の無い描画は通る。EU の符号化（p003）・GT（p002）の不具合なら push の有無で分かれない。
- Skylake 以降、`3DSTATE_CONSTANT_*` はその段の次の `3DSTATE_BINDING_TABLE_POINTERS_*` で初めて効く（Mesa 25.0.7 anv
  `genX_cmd_draw.c` の `cmd_buffer_emit_descriptor_pointers()`「on SKL this is what flushes push constants」、`cmd_buffer_flush_gfx_state()` の
  「After emitting push constants, on SKL+ we have to emit the corresponding 3DSTATE_BINDING_TABLE_POINTER_*」）。
  render の batch は文脈の setup（`drv_i915_gfx_emit_context_setup()`）で VS・HS・DS・GS の binding table の pointer を 0 にした**後**で
  `drv_i915_gfx_emit_constants()` を出し、その後は PS の pointer（`drv_i915_gfx_emit_draw()`）しか出さない。PS の push は commit され、VS・GS の push は commit されない。
  Gen12 の実機（5330）では今の順で PASS しているので、Gen12 の byte は変えない。

### 直し（commit dfd55f0fe）

- `src/drivers/gpu/i915/render/state.c` の `drv_i915_gfx_emit_constants()`: Gen9 のときだけ、5 つの `3DSTATE_CONSTANT_*` の後に
  `3DSTATE_BINDING_TABLE_POINTERS_VS` と `_GS`（値 0、文脈の setup と同じ）を出す（2 dword × 2）。draw と blit（矩形の fill・copy）の両方が通る。
- `plan/ws229/tests/host/gen9-check.py`: その 2 つの命令を Gen9 だけの命令（`GEN9_ONLY`）に理由とともに足した。

### 確かめ（host と build だけ。QEMU・実機は起動していない）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| 3D の state（Gen12 の byte・gen90.xml の decode） | `plan/ws229/tests/host/run.sh` | PASS: `Gen12: 17 files byte for byte as 03dee2287's`、`14 batches (890 instructions) … decode with gen120.xml and gen90.xml; every field agrees or differs as listed`。直しの前の check.py では Gen9 の 4 つの batch に `3DSTATE_BINDING_TABLE_POINTERS_VS/GS has no Gen12 counterpart` が出る（新しい命令が decode で見えていること） |
| EU（Gen9） | `plan/ws229/tests/eu-gen9-host.sh` | PASS（`ws229 Gen9 EU host test PASS`） |
| eu-test の fixture | `plan/ws229/tests/eu-test-gen9-host.sh` | PASS（Gen12 322 dword・Gen9 327 dword の decode と期待） |
| kernel | `make ZEDBSD_CONFIG=plan/ws229/tests/config-amd64-gen9.mk BUILD=build/ws229-p005/k-vkx GEN9_SCENARIO=vkx build/ws229-p005/k-vkx/vmunix` | rc 0、warning 0（log の `warning:` 0 行）、`amd64 vmunix check: PASS`、`render/state.o` を作り直した |

未実施: SV7 の再試験（T1、下の依頼）。5330 の回帰は要らない（Gen12 の batch は byte で不変、host の試験で確認）。
T1-608 の draw は後で着いて PASS（`i915: test draw: PASS`、1024/1024）。r1 は返却の時点で実行中（落ちていれば別に見る）。

### T1 への再試験の依頼の文（SV7、落ちた場面だけ、Q1 が出す）

> **依頼主**: P1（ws229-p005）。**目的**: T1-608 で SV7 の vkx・vke1・vke2・vkc が落ちた原因（Gen9 で頂点の段の push 定数が
> commit されない、p004 の 3D の state）の直し（commit dfd55f0fe）が効くかを確かめる。
> **前提**: main の HEAD（dfd55f0fe を含む commit を書く）。`plan/tools/hwsv7/setup.sh` が `setup: awe@10.0.10.27 ready`。5330 の試験と同時に流さない（lock を共有）。
> **流し方**: `I915_MACHINE=sv7 OUT=build/ws229-p005/hw-sv7-r2 plan/ws101/tests/hw/run-hw.sh ws229 vkx vke1 vke2 vkc`（4 本、1 本 5〜10 分、background で）。
> **合否**: 各 `build/ws229-p005/hw-sv7-r2/hw-<scenario>-ws229/run.log` の verdict の行。
> `i915: vkx: verdict PASS (9 of 9 steps passed)`、`i915: vke1: verdict PASS (6 of 6 steps passed)`、`i915: vke2: verdict PASS (21 of 21 steps passed)`、
> `i915: vkc: verdict PASS (9 of 9 steps passed)`。どれか FAIL・HANG・行が無い、なら FAIL。FAIL なら場面ごとの step の FAIL の行を添えて返す。
> **返し先**: Q1 と P1（または Q1 が p005 を割り当てた担当）。FAIL の解析はしない。
