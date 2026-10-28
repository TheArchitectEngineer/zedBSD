<!-- awesome-plan project=zedbsd record=ws269-hardware-notes -->

# WS269 PowerVR BXE-4-32 の知見と制約（hardware と firmware の事実）

WS269 の ws.md（2026-10-21 時点）の「知見と制約」の節を、WS の完了の時（2026-10-28）にこの file へ移した。中身は変えていない。親: [WS269](ws.md)。

- 「板 N」は VF2 の実機の起動の番号。dmesg は Q1 の scratchpad にあり、git には入っていない。
- commit は main に merge 済みの SHA。
- 詳しい経過は phase010q（`fb1f6afee` の `plan/ws269/phase010q/phase.md`）（P1）と phase008（`fb1f6afee` の `plan/ws269/phase008/phase.md`）（P2）の各 Queue の節にある。

### 1. 電源と clock

- **PMU の hardware event の mask**: JH7110 の PMU の hardware event の turn-on mask（PMU 0x04 の bit 7）が GPU を mask していると、GPU の job は fault を出さずに止まる。
  - 印は firmware の trace の POW「HW Request … Aborted … retrying」。
  - `vf2-gpu.c` の power_up は、domain を入れた後に event mask を 0 にする。power_down は切る前に 0xffffffff にする。vendor の runtime resume・suspend と同じ（Q1、2026-10-20）。
- **POW_RASCALDUST**: firmware の system flags の bit 4 を立てる。firmware が Rascal と dust の電源を管理する（Q1 の直し）。
- **PDS・USC の exec base**: POW_RASCALDUST の下では、電源が切れると exec base が消えるので、kick ごとに送り直す。
- **clock**:
  - vendor の既定は 594 MHz（PLL2 ÷ 2）。我々の既定は 396 MHz（÷ 3）。
  - `POWERVR_VF2_CORE_594` で 594 MHz にできる。CRG の gpu_core の divider の最小を 2 にした（q1477・q1478、7440a6a75）。
  - 594 MHz にしても TA の lockup は変わらない（板 73）。
- **起動列**: DDK 1.19 の RGXStart と行ごとに一致する。MIPS では MMU_CBASE_MAPPING を書かない（q1475、ea6fa44dd）。

### 2. firmware

- **firmware の版**: linux-firmware の `rogue_36.50.54.182_v1.fw`（1.1.OS@6976702、mainline の ABI）を使う。
  - DDK 1.19 の firmware（6345021）とは別物。
  - command の layout は mainline のもので、geometry の command は 112 byte。IMG の TA の command は 64 byte の別の形（q1465）。
- **config**: system flags 0x20010（POW_RASCALDUST と CSW MEDIUM）、OS config flags 0xF（TDM・GEOM・3D・CDM の context switch）。
  - Linux の vendor の driver と同じ値（q1463、c69e90e34）。
  - context switch を切っても結果は同じだった（板 59・66）。
- **KCCB の完了の待ち**: firmware は KCCB の return slot を書かない。os_data の `kccb_cmds_executed` を待つ（session の stop の cleanup も同じ。q1406〜、q1450）。
- **EVENT_STATUS の bit 4**（ISP_END_MACROTILE）:
  - firmware の起動前は 0（板 70）。
  - 最初の 3D の job の後は立ったままになるが、害は無い（q1477）。
- **host から読む register**: BIF・CLK_CTRL・CAT_BASE などは 0 に見える。
  - Linux でも同じで、正常。host の probe では判定できない（板 55、q1460）。
- **trace の decode**: `plan/tools/vf2/sfdec.py <mainline の pvr_rogue_fwif_sf.h> <dmesg>` を使う（Master の Tools に登録）。
  - id は number | group<<12 | 引数の数<<16 | 0x70000000。
- **context reset の通知（105）**: その context の session だけを lost にする。
  - device を lost にするのは、firmware の fault（112）、restart の要求、restart の失敗だけ（q1450、1511943ec、板 81）。

### 3. TA・3D（要点）

- **TA の stream の終わり**（TA の lockup の原因と修正）: VDM の control stream は、終わりに PPP_STATE0/1 の block で terminate の PPP state（3 語）を指してから、STREAM_TERMINATE で終える。
  - 3 語は次のとおり。
    - header: pres_terminate（bit 25）
    - TERMINATE0: clip の right（bit 18〜26）と bottom（bit 0〜8）。どちらも 32 px の block の数 −1
    - TERMINATE1: 0
  - Mesa の `pvr_sub_cmd_gfx_build_terminate_ctrl_stream` と同じ形。
  - この block が無いと、firmware の HWR が lockup になる（fault は出ない）。
  - 板 77 で TA が初めて完走し、1e92b88cf で全 stream に入れた。板 78 で三角形と job check が通った。
- **geometry の register**（q1482、4bb158b50。IMG の capture と一致）:
  - TE_PSG 0x20000（completeonterminate）
  - PPP_CTRL 0x202
  - TPU の border colour table の address
  - resume の size は Mesa の VDMCTRL_PDS_STATE0
  - flags 3（FIRST|LAST。SINGLE_CORE は立てない）
- **HWRT の record**: 全 field が Mesa・mainline と一致する（q1480、259097c5e）。
- **region headers**: 専用の window の 0xDBF0000000（256 MiB）に置く（6079f96cc）。
- **free list の entry**: PM_META_PROTECT（bit 62）と SLC bypass で map する（1e117cef4）。
- **border colour table**: session ごとに 256 KiB を 0 で埋めて置き、SLC bypass にする（840811785、c47af3678）。
- **LOAD**:
  - 3D の背景の object から、shared register の texture state で sample する道は動かない。sample は 0 を返す。probe 1〜5 の形のどれでも同じだった（板 92〜95）。
  - そこで LOAD は、compiler の texture の shader による全画面の draw で行う（q1514、40bca42a6）。
  - 板 96 で vkmatrix 15/15 になった。16 draw を越える pass の job の分割も、これで通った。

### 4. compute（要点）

- **workgroup の PDS program**: Mesa の bytes をそのまま使う（data 8 語・code 10 語）。中身は次のとおり。
  - task word
  - workgroup id の DOUTW を cf0〜cf2 に
  - local id の DOUTW を unified store に
  - base workgroup の add32 ×3
  - DOUTU の cc
  - 以前の PDS には local id の DOUTW と cc が無く、fault するか一部だけを書いた。Mesa の PDS で 128 wg が 4096/4096 になった（板 87、P2 の q1496 566676bef、P1 の q1498 38776e4af）。
- **CDM の stream**: K0、kernel、fence kernel、terminate の順に並べる。
  - K0 は shared register を確保する。
    - usc_common 64 byte、target ALL、sd NONE、common_shared、max_instances 8、1 instance。
    - program は halt だけ。
  - kernel は sd_type USC。
  - fence kernel は sd PDS、common_shared、max_instances 8。
  - K0 が無いと、fence kernel が lockup する（板 91）。
  - fence が無いと、kick の完了が store より先になる（P2 の q1503 57d3029e1）。
  - commit は 5a22f829e と 748d362be、確認は板 93。
  - 1 submission の中の dispatch の順序は、各 dispatch の後の fence kernel で守られる（q1502）。
- **temps の field**: ceil(temps/4) にする。
  - 旧の手書きの kernel は 1 register 多く要った（P2 の q1492・q1499、板 87・88）。
- **max_instances**: 0 は 32 の意味になる（q1492）。

### 5. USC（shader）の制約（板 95 で確定、P2 の q1505）

| 項目 | 板 95 の結果 | 扱い |
| --- | --- | --- |
| store の data を address の register の対の直後に置く | 離すと 0/32（対の直後の register の値が store される） | **唯一の hardware の制約**。compiler は TEMP_STORE_DATA（r8）を TEMP_ADDRESS（r6:r7）の直後に置く（Mesa と同じ） |
| in-place の 64 bit の add | 32/32 | 制約ではない |
| madd の source を次の群で書き換える | 32/32 | 制約ではない |
| madd の source に特殊 register の定数を使う | 32/32 | 制約ではない |
| madd の結果を次の群で読む | 32/32 | 制約ではない |
| nop（`02 80 6e 00`）と wdf（`02 80 6a`）の encoding | Mesa 26.2.5 と同じ | ― |

- compiler は群の間に間隔を入れない（Mesa と同じ）。
- 以前「hazard」に見えた現象（半分の lane の値が壊れる）の原因は、PDS の不備（local id の DOUTW が無い）だった。
- Mesa の pco の命令列は `plan/tools/powervr/p008/mesa-pco-fill-bxe-4-32.txt` にある（q1481）。

### 6. memory と cache

- core は CPU の cache を snoop しない。
- CPU は非 cache の view でだけ触る。
- CPU が書くか読む buffer は、GPU の SLC も迂回する（border table も含む）。
- clean・invalidate は要らない。memory の型は HOST_COHERENT にする（P2 の q1508）。
- vkcoherency（buffer 1000 回・image 200 回）で stale 0・wrong 0（板 95）。

### 7. userland の Vulkan

- **queue family**: 1 つで、GRAPHICS・COMPUTE・TRANSFER を持つ（863b44cf1、板 81）。
- **API の版**: libvulkan は API 1.0 だけを公開する。
- **PPM の header**: libc の fscanf は 1 行しか読まない（BUG-344）。gpu-selftest は PPM の header を自前で読む（b70559027、板 83）。
- **試験の結果**: gpu-selftest 13/13、vkmatrix 15/15、vkcoherency PASS（板 89〜96）。

### 8. 板の運用と道具

- **VF2 の板の起動**:
  - SSH で /boot/boot0/vmunix を置き換え、読み戻して確かめる。
  - SwitchBot の plug（`plan/tools/switchbot.py`）で電源を入れ直す。
  - dmesg の ring は 32 KiB なので、起動の間に繰り返し読む。
  - 道具は `VF2_HOST=user@host VF2_PASSWORD=… plan/tools/vf2/run.sh VMUNIX OUTDIR`（Master の Tools）。
    - host と password は環境変数 VF2_HOST・VF2_PASSWORD で渡す。
    - Master の Tools への登録は Q1。
- **診断の switch**:
  - `POWERVR_DIAGNOSTICS`
  - `POWERVR_RENDER_CHECK_FULL_TRACE`
  - `POWERVR_FIRMWARE_BOOT_TRACE`
  - `POWERVR_RENDER_LOAD_PROBE`（a4f8663f2 など）
- **Linux（Debian、DDK 1.19、IMG の Vulkan）の取得**:
  - `plan/ws269/tests/p010-linux/` の capture*.py（gdb で bridge の kick の command を取る）と walk.py がある。
  - kick の command は取れた（TA 64 byte、CDM 48 byte）。
  - StarFive の kernel では /dev/mem で RAM を読めず、kcore も無いので、stream の本体は読めない（q1495）。

### 9. 除外した仮説

| 仮説 | 結果 | 板・Queue |
| --- | --- | --- |
| page catalogue の set・register range | set や range を変えても同じ lockup | 47・55・57 |
| context switch の flags | 同じ | 59・66 |
| static switch state を 0 にする | 同じ | 49 |
| border colour table が無い | 入れても同じ（入れたままにした） | 61 |
| resume size を 0 にする、single-core の flag | 同じ | 62・63・76 |
| PM_META_PROTECT が無い | 入れても同じ（入れたままにした） | q1471 |
| region headers の heap | 同じ | 67 |
| 594 MHz | 同じ | 73 |
| 起動前の EVENT_CLEAR | 起動前の EVENT_STATUS は 0 | 70 |
| CLK_CTRL・register_check の副作用 | 退行の原因は fence の除去だった | 92、q1503 |
| HWRT・起動列・geometry の register | Mesa・DDK・IMG の capture と一致 | q1480・q1475・q1482 |
| LOAD の address・layout、BGRA の sample、描いた直後の image の sample | どれも正しい | q1510・q1511 |

- **Linux との比較**:
  - TA の kick までの firmware の trace の並びは、Linux と同じだった。違いは firmware の外の stream にあった。
  - IMG の TA の command の値（ppp_ctrl 0x202、te_psg 0x20000、flags 3）も、我々と同じだった（q1465・q1482）。
