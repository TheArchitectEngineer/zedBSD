<!-- awesome-plan project=zedbsd record=ws242-p008 -->

# ws242-p008: RTX 2070 の実機の回帰（段ごとの止めと諦めの道を含む）

Status: cleared（2026-10-29 P5、q1678-i02。RTX 2070 の実機、下の「実機」）
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i02（P5）
依存: ws242-p004〜p007、機械（ユーザーの回答 Q242-4「RDNA と差し替え (推し)」: 10.0.10.3 の GPU を RTX 2070 に差し替え、SwitchBot で電源、判定は ssh と boot-test.sh）
設計: [design.md](../design.md) §4、第 2 版の表の p006。review の M3・M6

## 範囲

- 起動・display・desktop・vkcube 相当（WS037 の受け入れと同じ）。`nvrtx.stop=` の段ごとの止めで画面が残ること、p006 の諦めの道を故意に通した時の画面と次の warm reboot。
- 担当は T1（Q1 が割り当てる）。q1678 の範囲の外（この Queue は試験なし）。→ q1678-i02 で P5 が流した（Q1 の割当）。

## 故意に失敗させる方法（2026-10-29 P5、q1678-i02）

p006 の止めの道（`gsp_hold()`）は、program が答えない時にしか通らない。実機でそれを起こす試験だけの boot word を足した（`stage.c` の `nvrtx_stage_fail_wanted()`、`gsp.c` の 3 か所）:

| boot word | 諦める所 | gsp_hold の点 | reset する falcon | 走りうる物 |
| --- | --- | --- | --- | --- |
| `nvrtx.fail=frts` | FWSEC-FRTS が region を作った後（「frts ok」の後） | FWSEC | GSP | 無し（FWSEC は halt 済み） |
| `nvrtx.fail=booter` | booter_load が 0 で返った後（RISC-V の確かめの前） | BOOTER | GSP・SEC2 | GSP の RISC-V（起動の直後） |
| `nvrtx.fail=gsp-init` | INIT_DONE を 200 ms だけ待ち（sequencer はその間に回る）、来ても来なくても諦める | INIT | GSP | GSP-RM（init の途中か直後） |

- 本物の失敗との差: 本物は falcon が halt しない・event が来ない。故意の物は、program が答えた後に「答えなかった」として同じ `gsp_hold()` を通す。bus master を切る・falcon を reset・`nvrtx_console_restore()`・memory を保つ・region を残すのは同じ。
- 安全の範囲: どれも region を残し、GPU は電源を切るまで held。host の memory は bus master を先に切るので壊れない。次の GSP の起動は「frts stop: region … already set」で firmware の画面のまま（warm reboot）。電源の入れ直し（SwitchBot AC276E42B252）で戻る。unload・teardown の点（GSP-RM が display を持った後）は今回は通さない（display を GSP-RM が持った後の戻りは電源の入れ直しで戻るか確かでない）。
- 流し方: `PERF_BUILD=build/ws242 sh plan/ws242/tests/rtx-regress.sh OUT VMUNIX fail=gsp-init warm cycle`（boot line の変更 → Debian を通した再起動 → driver の行・session・desktop の shot。`warm` で次の warm reboot、`cycle` で電源の入れ直しの後）。

## 実機（2026-10-29 14:50〜15:21・16:12〜 P5、q1678-i02、10.0.10.3 の RTX 2070、RDNA 機の時間の枠と lock の中）

実機の証拠だけ（QEMU は使っていない）。物理の画面は誰も見ていない（ユーザー不在、capture の装置も無い）: 「画面」の判定は driver の印（`nvrtx:` の行）・ssh・keiland-shot（compositor の出力）まで。

- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws242/k build/ws242/k/vmunix`（c4c167ba9 ＋ nvrtx.fail、00576629c の tree、warning 0、cksum 2048309826）。mview・libvulkan・libc は同じ config の `BUILD=build/ws242/usb`、vkhang は `plan/ws039/tests/vkhang-build.sh build/ws242/usb`（warning 0）。
- host 試験（同じ tree）: identify `27 identities, 4 families, 288 products, 24 boot choices`・gsp `393 checks, SKIPPED 0`・qmd `733 checks`・vk-d3 `209 checks, 0 failures`・vk `86 checks, 0 failures`・sm75 `287 checks, 0 failures`・desktop `186 compiled, 0 refused, 0 disagreed`・`plan/ws250/tests/nvrtx-recover-host-test.sh` `45 checks, 0 failed`。
- コマンド: `PERF_BUILD=build/ws242 sh plan/ws242/tests/rtx-regress.sh OUT build/ws242/k/vmunix STEP...`。log は worktree p5-ws242 の `build/ws242/r1`（hang boot）・`r2`（stop=static-info stop=takeover fail=gsp-init warm cycle）・`r3`（fail=booter fail=frts warm restore=va1）。

| step | 結果 |
| --- | --- |
| hang（`rtx-hang-check.sh`、kernel を入れる） | S0 mview scene-5000 **49.39 fps**、`VKHANG ok`、session channel 3 行。S2 vkhang lost 2001 ms、mview 完走 30.90 fps、stopped 1・stuck 0、control ok。S3 5/5 lost、chid 24〜28、control ok。S4 2/2 lost、mview 完走 12.90 fps。S5 kill の後 200 ms で終わる、control ok。終わりの shot 1920x1080（desktop、Welcome の窓）。WS250 p004 の r3（49.17・30.91・12.90）と同じ形: WS250 は壊れていない。S2 の shot は WS250 と同じく 0 byte（script の既知の点） |
| boot | zedBSD の NVMe の kernel cksum 2048309826、`gsp-init ok: GSP-RM is up after 976 ms`、takeover ok（1920x1080 SOR 2）、`gpu-device ok: gpu device registered`、session の socket あり、desktop の shot 6222418 byte |
| stop=static-info | gsp-init ok → `static-info skipped` → unload・FWSEC-SB・booter_unload・`unload ok: region torn down`・`unload console: bar1 mode 001fff00 again`。ssh 可、session 無し（GPU device が無いので compositor は出ない） |
| stop=takeover | gsp-init ok → `takeover skipped` → `display: the GSP stays up to keep the console`。ssh 可、session 無し |
| fail=gsp-init | 200 ms の progress（cmd put 2 got 2、msg put 0、riscv 00000035）→ `gsp-init fail on purpose` → `bus master off (0), falcon reset gsp 0 sec2 0`。machine は止まらず ssh 可、session 無し |
| warm（boot line を戻し Debian を通して再起動） | **region は残っていなかった**: `frts ok` → gsp-init ok（961 ms）→ takeover ok → desktop（shot 6222418 byte） |
| cycle（Debian、SwitchBot AC276E42B252 で入れ直し、zedBSD） | 普通に起動（gsp-init ok 969 ms、desktop） |
| fail=booter | `booter fail on purpose` → bus master off (0)…（印は 79 桁で切れて falcon reset の値は読めず）。ssh 可 |
| fail=frts（fail=booter の後の warm の起動） | `frts ok`（region は残っていなかった）→ `frts fail on purpose` → bus master off (0)…。ssh 可 |
| warm | frts ok → gsp-init ok（973 ms）→ takeover・gpu-device ok、desktop |
| restore=va1 | NVMe の zedBSD の kernel cksum 1463007375（va1 と一致）、Debian 6.12.111、RTX 4 function vfio-pci、BootOrder 0002,0000（15:21） |

分かった事:

- 3 つの止めの点（frts・booter・gsp-init）で、bus master を切って falcon を reset しても host は止まらず、zedBSD は firmware の console のまま起動を続けた（ssh）。GPU device が無いので desktop は出ない（firmware の画面の上の console だけの見込み、物理の画面は未観測）。
- 止めの後の次の起動は Debian を通した warm reboot で、3 回とも region が残っていなかった（`frts ok`）。hold の印の「power cycle before the next GSP boot」は、この機械のこの道では要らなかった（Debian の vfio-pci の bind の reset か、warm reboot の POST が WPR2 を消した見込み、どちらかは未確かめ）。zedBSD から zedBSD への直の warm reboot は試していない。
- 不具合（直した）: hold の印が 1 行で 79 桁（`NVRTX_STAGE_LINE_COLUMNS`）を超え、falcon reset の値と「firmware の画面を戻した・memory を保つ」の部分が切れていた。`gsp_hold()` の印を 4 行（何が・bus master と reset の値・画面と memory・region）に分け、region の行は「残った region は次の GSP の起動を電源を切るまで止める」と条件の形にした。gsp-init の本物の失敗の印の重複（`gsp-init fail (%d)` の後の `gsp-init fail`）も分けた。
- 直しの確かめ（16:12〜16:23、枠と lock を取り直して）: `make … BUILD=build/ws242/k2 build/ws242/k2/vmunix`（warning 0、cksum 2637149643）、`rtx-regress.sh build/ws242/r4 build/ws242/k2/vmunix kernel=build/ws242/k2/vmunix fail=gsp-init warm restore=build/ws242/vmunix-va1`: fail=gsp-init の印が 4 行とも切れずに出た（`gsp-init fail on purpose (nvrtx.fail=gsp-init)`・`hold: bus master off (0), falcon reset gsp 0 sec2 0`・`hold: the firmware's picture is back, memory kept for a dump`・`hold: a region left stops the next GSP boot until power-off`）、ssh 可。次の warm は frts ok・gsp-init ok（960 ms）・desktop（shot 6222418 byte）。
- 返却（16:23）: Debian 6.12.111、RTX 4 function vfio-pci、BootOrder 0002,0000、NVMe の zedBSD の kernel は vmunix-va1（cksum 1463007375 一致）、zedbsd.cfg に nvrtx.stop・nvrtx.fail の行は無し。lock は free。

未実施: 物理の画面の目視（止め・失敗の時に firmware の console が映っているか）、zedBSD から zedBSD への直の warm reboot での region の残り、unload・teardown の点の故意の失敗、RTX 30・40・GTX 16 の実機（機械が無い）、`nvrtx.gsp=0` の起動。S2 の shot（script の既知の 0 byte）。

## 判定

受け入れ（起動・display・desktop・mview の描画、段ごとの止め、諦めの道を故意に通した時と次の warm reboot）を、物理の画面の目視を除いて実機で確かめた。cleared（2026-10-29 P5）。
