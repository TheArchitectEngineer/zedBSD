<!-- awesome-plan project=zedbsd record=ws039-p012 -->

# ws039-p012: 段 A4（Tier F）: SMU の fan・thermal の制御

Status: in-progress（2026-10-16 P4: code と host 試験は済、**実機前**。実機の確かめは P3 の p011 の後に Q1 が段取りする）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1217 / q1217-i01
実行者: P4（worktree `/home/awe/zedBSD-worktrees/p4`、branch `agent/p4-ws039`、base main `98761dba8`）。実機には触らない（10.0.10.3 は P3 の p011 が使う）

## 範囲（[design.md](../design.md) §16 の p012 の行）

SMU の Tier F: NumOfDisplays、pptable（VBIOS ＋ smc_dpm_info、soft pptable の選択）、driver の表、RunBtc、allowed mask と EnableAllSmuFeatures（per-feature の第 2 の試し）、THM の割り込みと閾値、metrics の温度と fan の読み（THM の値との比較）、host 試験 (l)。**実機: A4 の `mode=F`、features の 3 bit、温度の 2 つの源の一致**。順は design-review-2 の M7（Linux の `smu_smc_hw_setup` の順: NumOfDisplays → 表 → pptable → RunBtc → allowed → EnableAll）。§15 の回答（[ws.md](../ws.md) の表: #3 fan の制御も最初から、#4 CTF は GPU だけ fault、#5 Tier S の fan は 50%）。

承認: 2026-10-16 ユーザー「p012以降もコードだけ別なサブエージェントで作成してOKです」（Q1 の依頼文の引用）。実機の確かめは Q1 の段取り。

## 実装（2026-10-16 P4、実機前）

driver（`src/drivers/gpu/amdrdna/`）:
- `thermal-format.c`（host でも動く純粋な file、build の行は変えない）に足した:
  - `amdrdna_power_table_build`: VBIOS の powerplay の表（1674 byte）の offset 482 から 1192 byte（PPTable_t）を写し、offset 964 からの 196 byte（I2cControllers から BoardReserved **まで**）を smc_dpm_info（format 4・content 5、200 byte）の offset 4 からの 196 byte で置き換える。edge・hotspot・memory の limit（PPTable_t の 52・54・56）、shutdown 温度（powerplay の 32）、thermal controller の種類（21）、FeaturesToRun（4・8）を返す。短い表・dpm_info の無い・format 4 でない・content 5 でない（Navi12 の 7 を含む）・200 byte 未満は EINVAL。
  - `amdrdna_power_table_in_firmware`: smc.bin の header 2.0（表 1 つ、ID を問わない）と 2.1（{id, offset, bytes} の list から ID の物）。2 以外の major は ENOENT、2.2 以上は EINVAL、list・表が file の外は EINVAL。
  - `amdrdna_power_readings_read`: metrics の TemperatureEdge（34）・TemperatureHotspot（36）・TemperatureMem（38）・CurrFanSpeed（62）。SmuMetrics_t と legacy で同じ位置なので、SMU の版（Linux は Navi10 で 0x002A3B00 を境に切り替える。実機は 42.65.0 = 0x002A4100 で新しい方）で分けない。
  - `amdrdna_power_features_on`: bit 31（FW_CTF）・32（FAN_CONTROL）・33（THERMAL）が全部立つか。
- `amdrdna-hw.h`: 上の宣言、`struct amdrdna_power_limits`・`struct amdrdna_power_readings`、`AMDRDNA_SMU_FEATURES_LOW/HIGH`（0x80000000 / 0x3）、表の大きさ（1192・104）。
- `amdrdna-private.h`: `struct amdrdna_thermal` に table page（VRAM、`table_taken`・`table_vram`）、`table_built`・`limits`、`features_on`・`features_low/high`、THM と metrics を並べる読み（`sensor`・`hotspot`・`memory_temperature`・`tachometer_speed`）、`readings`（数）。
- `smu.c`（段 A4）:
  1. 既存: FIRMWARE_FLAGS → GetDriverIfVersion・GetSmuVersion → **THM の static duty（50%）を先に**（Tier F の途中で SMU が止まっても fan が回る網。Linux の pwm1_enable 1→2 と同じく、FAN_CONTROL を有効にすると PMFW が fan を取る）。
  2. pptable を組む（VBIOS の pplib_pptable_id が 0 なら VBIOS の powerplay、0 でなければ `<chip>_smc.bin` の soft pptable）。limit は Tier S でも使う。
  3. Tier F（`amdrdna.smu.features=0` で省く）: NumOfDisplays(0) → driver の表（VRAM の CPU pool から 4 KiB を 1 回取り、SetDriverDramAddrHigh/Low に MC address）→ 表を page に写し HDP flush → TransferTableDram2Smu（引数 0）→ RunBtc → SetAllowedFeaturesMaskHigh(0x3)・Low(0x80000000) → EnableAllSmuFeatures → GetEnabledSmuFeaturesHigh・Low。3 bit が立たない・拒まれた時は第 2 の試し EnableSmuFeaturesLow(0x80000000)・High(0x3) → 読み戻し。それも駄目なら DisableAllSmuFeatures と static duty の掛け直しで Tier S。SMU が hung になったら Tier S に落として GPU を fault（11.5）。
  4. 既存: THM の alert（high = 表の shutdown 温度 118 ℃、無ければ VBIOS の値）と MP1 の sw 割り込み、IH の確認。監視の閾値は `amdrdna.temp.max=`、無ければ **pptable の TedgeLimit（実機の VBIOS で 100 ℃）**、表が無ければ 95 ℃。
  5. 読み: THM（CG_MULT_THERMAL_STATUS・tach・FDO の duty）は常に読み、Tier F では毎秒 TransferTableSmu2Dram（引数 6）→ HDP の invalidate → 104 byte を読み、edge を監視の温度、CurrFanSpeed を fan とする。metrics が取れなければ fault。10 回目の読み（A4 の約 10 秒後）で 2 つの源を 1 行に並べる（`thermal reading 10: edge … fan …; thm … tach …`）。
  6. Tier F では fan の回転待ち（5 s）を省く（PMFW は冷えている間 fan を止める。Linux の dmesg の時の hwmon も fan1_input 0）。
  7. 停止: Tier F なら DisableAllSmuFeatures → static duty を掛け直す（次の post まで fan が回る）→ PrepareMp1ForUnload。
  8. mailbox: `smu_message` を mutex を取る外側と、取った中で送る `smu_message_held` に分け、表の page の写しと Transfer の message を 1 つの mutex の区間にした（新しい lock を足さない。mutex の初期化は `amdrdna.c` のまま）。
- A4 の印: `A4 power table from the vbios id=0: edge … hotspot … mem … shutdown … controller … features …`、`A4 features all on: 0x…, fan mode word 0x…`（第 2 の試しなら `by word`）、`A4 ok … mode=F features=0x… duty=0% fan=…rpm tach=…rpm driven=…% temp=…C hotspot=…C mem=…C thm=…C max=…C limit=…C alert=…C irq=thm`、停止で `smu: all features off, answered 0`。

設計（design.md 4.4）との違い・決めたこと:
- **smc_dpm_info の写す範囲**: design 4.4 の 3 は「`I2cControllers` から `BoardReserved` の手前まで」だが、Linux の `smu_memcpy_trailing` は `offsetofend(BoardReserved)` まで（BoardReserved を含む 196 byte = dpm_info の 200 − header 4 で、Linux は大きさの一致を BUILD_BUG_ON で確かめる）。Linux に合わせた。design の文の直しは Q1 へ（下の残り）。
- Linux は GFXOFF が allowed の時に `DebugOverrides |= DPM_OVERRIDE_DISABLE_DFLL_PLL_SHUTDOWN` を足すが、amdrdna は GFXOFF を allowed にしないので足さない。
- boot の parameter `amdrdna.smu.features=0`（Tier F を省き p004 の Tier S のまま）を足した: 実機で Tier F が害をなす時の退路と、Tier S の回帰の比較用。design 12 の parameter の一覧への追記は Q1 へ。
- UAPI（`include/uapi/amdrdna.h`）と `control.c` は変えていない（P3 の p011 と重なるため）。`amdrdnactl status` の `fan_mode` は Tier F で既存の `AMDRDNA_FAN_FIRMWARE`（2）、`temperature` は metrics の edge、`temperature_highest` は THM の bit 8:0 のまま、`fan_rpm` は metrics の CurrFanSpeed、`fan_duty` は 0。hotspot・mem・THM の bit 17:9・tach は log の行だけ。
- source の名前は design 13 の規則で独自（`SMU_MESSAGE_PERMIT_HIGH` など）。監査（audit-symbols）で一般語 5 つ（board・controller・edge・memory・power）を allow list に足した。

## 確かめ（2026-10-16 P4、host、実機の証拠ではない）

- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix` exit 0、warning 0、`amd64 vmunix check: PASS`。`make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk vmunix` exit 0、warning 0。`git diff --check` OK。
- host 試験 (l): `plan/ws039/tests/amdrdna-power-test.c`（新）と `amdrdna-power-linux.c`（新、Linux v6.19 の struct で表を作る test だけの道具。Linux の header は include path で読むだけ）。`amdrdna-host-test.sh` に足した。
  - 合成: 表の組み立て（firmware の部分・board の部分・その後ろ・limit 6 つ）、拒否 5 つ、smc の header 2.1・2.0・1・2.2・list と表の範囲外・短い file、metrics の 4 欄、features の判定 5 つ。
  - **Linux の struct との byte 一致**: `smu_11_0_powerplay_table`・`atom_smc_dpm_info_v4_5`・`PPTable_t`（Linux の写し方で作った期待値）・`SmuMetrics_t`・`SmuMetrics_legacy_t` の大きさと、組み立てた表の 1192 byte が一致、metrics の両方の版の 4 欄。
  - **実機の VBIOS**（P3 の `plan/ws039/temp/dump/cold/rom.bin`、2026-10-16 の dump）: pplib_pptable_id 0、dpm_info 4.5（200 byte）、powerplay 1674 byte、組み立てた表の limit は edge 100・hotspot 106・mem 105（Linux の hwmon の temp1_crit 100000・temp2_crit 106000 と一致）、shutdown 118（A1v の値と一致）、controller 27、FeaturesToRun 0x00000623a3ddaffb。
  - **実 file の navi10_smc.bin**（linux-firmware 20260410）: header 2.1、soft pptable 3 つ（2187・2207・2204、各 1674 byte）がどれも組める（edge 110/110/110、shutdown 118/118/113）。この board は ID 0 なので実機では通らない道。
  - 2026-10-16 13:3x `AMDRDNA_LINUX=…/p3/plan/ws039/temp/linux AMDRDNA_FIRMWARE=…/p3/plan/ws039/temp/linux-firmware/amdgpu sh plan/ws039/tests/amdrdna-host-test.sh …/p3/plan/ws039/temp/dump/cold` exit 0、`PASS amdrdna-host-test`（PASS の行 366、FAIL・SKIP 0、audit-symbols PASS（38 file））。power の部分だけ: PASS 62 case（FAIL 0）。

## 実機で確かめる事（Q1 の段取り、P3 の p011 の後。未実施）

手順（p004 と同じ道。この機械は warm reboot で PSP・SMU が reset されるので cold power cycle は要らない、p004 の 2 回目の観測）:
1. この branch の vmunix（`make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk vmunix`、または p011 と合わせた kernel）を Debian 側で USB の sda2 の `vmunix` と差し替える（p004 の手順）。
2. Debian で比較の値を取る: amdgpu が載っていれば `/sys/class/drm/card0/device/hwmon/hwmon*/temp{1,2,3}_input`・`fan1_input`（p002 の dump では edge 43 ℃・junction 43 ℃・mem 44 ℃・fan 0 rpm）。
3. `plan/ws039/tests/hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 plan/ws039/tests/hw-run.sh collect` → zedBSD で `amdrdnactl advance A4`（A5 以降を含む kernel なら `advance A5` でもよい）→ 約 20 秒後に `amdrdnactl status` と dmesg の `A4`・`thermal reading 10`・`smu:` の行。
4. 合格の見方:
   - `A4 power table from the vbios id=0: edge 100C hotspot 106C mem 105C shutdown 118C controller 27 features 0x00000623a3ddaffb`
   - `A4 features all on: …`（`by word` なら第 2 の試しで通った。どちらも合格、design 14 節の 5 の観測として記録）と `A4 ok … mode=F`、features の 3 bit（high の bit 0・1、low の bit 31）が立つ。`fan mode word` が static（bit 13:11 = 1）から変わるか（PMFW が FDO を取ったか）。
   - **温度の 2 つの源の一致**: `thermal reading 10:` の edge と thm（CG_MULT_THERMAL_STATUS bit 17:9）が数 ℃ 以内（cold の dump で THM 45 ℃・Linux の edge 43 ℃）。hotspot・mem が 0 でなく edge 以上の程度。metrics の fan と tach の rpm が近い（冷えている間は両方 0 rpm がありうる。0 なら負荷か温度が上がった時の回転は未確認として残す）。
   - `amdrdnactl status` の temperature が毎秒更新、`limit` 100、fault の flag が立たない。
   - 退路: `amdrdna.smu.features=0` を boot の行に足した起動で p004 と同じ `A4 ok … mode=S` に戻る（回帰）。
   - reboot（shutdown の hook）で Debian に戻る（`smu: all features off` の行は log の file system の後なので disk に残らないかもしれない。p004 と同じ）。
5. 落ちた時: `A4 features: … refused (n)` の行で止まった message が分かる。Tier S に落ちて `A4 ok mode=S` なら system は動く。固まったら電源（design 14 の 17）。

未確認（実機前）: Tier F 全体（message の受理、RunBtc が DPM 無しで害が無いか、allowed mask 3 bit で EnableAll が通るか、metrics の転送、PMFW が fan を回すか）、high alert・CTF の fault（起こさない）、soft pptable の道（この board は ID 0）。

## P3 の file との重なり

- 変えた file: `smu.c`・`thermal-format.c`（P3 の p011 は触らない見込み）、`amdrdna-hw.h`（thermal の client の define の後・`struct amdrdna_vbios_owner` の後・`amdrdna_alert_clear_word` の宣言の後の 3 か所。P3 の未 commit の差分は 32・67・756・779・1525・1695 行付近で、重ならない）、`amdrdna-private.h`（`struct amdrdna_thermal` だけ）。
- test: `plan/ws039/tests/amdrdna-host-test.sh`（power の行を psp の行の後と dump の分岐の前に）、`audit-allow.txt`（5 語）。P3 が同じ script・allow list を変えると merge で小さい衝突がありうる（行の追加だけ）。
- 変えていない: `amdrdna.c`・`control.c`・UAPI・`platform/amd64/vmunix.mk`（新しい file を作らず thermal-format.c に入れたので build の行は不変）。

## 残り・再開点

- [x] code・host 試験 (l)・build（warning 0）
- [ ] Q1: 実機（上の手順、P3 の p011 の後）、その結果で clearance の判定、main への merge（実機前は入れない）
- [ ] Q1 へ: design.md 4.4 の 3 の「BoardReserved の手前まで」を「BoardReserved まで（offsetofend）」に、12 節の parameter に `amdrdna.smu.features=0` を足す（P4 は design.md を直さない）
- 再開点: 実機の結果が来たら、落ちた message の行に合わせて smu.c を直す（第 2 の試しの順・RunBtc を省く案など）。
