<!-- awesome-plan project=zedbsd record=ws206-p004-values -->

# ws206-p004: 値の出典の表（BCM43455 の bring-up・SDPCM・BCDC・event）

Parent: [ws206-p004](phase.md)

2026-10-13 P4 が上流を取得して照合した（scratchpad に取得、tree に入れていない。字面は写さず値だけを使った）:

- **bwfm** = OpenBSD の bwfm（ISC）。`sys/dev/ic/bwfm.c` v1.113、`sys/dev/ic/bwfmreg.h` v1.26、`sys/dev/sdmmc/if_bwfm_sdio.c` v1.44、`sys/dev/sdmmc/if_bwfm_sdio.h` v1.3（`raw.githubusercontent.com/openbsd/src/master/...`、2026-10-13 取得）。
- **WHD** = Infineon wifi-host-driver（Apache-2.0）、tree `bd99da7b005c97500d7c697f544b36fabdaa3f5e`（branch master、2026-10-13 取得）の `WHD/COMPONENT_WIFI6/`。
- **SDIO** = SD Association の SDIO Simplified Specification（CCCR）。
- 行番号は取得した版の行。「Pi」の欄は実機で読む値（空欄＝未実施、Q1 がユーザーに頼む）。

## SDIO の function 0・1 の register

| 値 | driver の名前 | 出典 | Pi |
| --- | --- | --- | --- |
| CCCR 0x04 割り込みの enable、bit 0 master・1 F1・2 F2 | `BCM43455_CCCR_INTERRUPT_*` | SDIO。WHD `whd_sdio.h:47,146-148`、`whd_bus_sdio_protocol.c:410-411`（master\|F1\|F2） | |
| CCCR 0x06 I/O abort（function の番号を書く） | `BCM43455_CCCR_IO_ABORT` | SDIO。WHD `whd_sdio.h:49`、`whd_bus_sdio_protocol.c:1322` | |
| CCCR 0xf0 card capability: CMD14 support 0x02、CMD14 ext 0x04 | `BCM43455_CCCR_CARD_CAPABILITY` | bwfm `if_bwfm_sdio.h:20-22`、`if_bwfm_sdio.c:456-458`（save-restore の時だけ） | |
| CCCR 0xf1 card control: WLAN reset 0x02 | `BCM43455_CCCR_CARD_CONTROL` | bwfm `if_bwfm_sdio.h:24-25`、`if_bwfm_sdio.c:320-322` | |
| F1 0x10008 watermark、値 8 | `BCM43455_F1_WATERMARK` | bwfm `if_bwfm_sdio.h:31`、`if_bwfm_sdio.c:450`。WHD `whd_sdio.h:101` | |
| F1 0x1000a〜c window（低・中・高） | `BCM43455_F1_WINDOW_*` | bwfm `if_bwfm_sdio.h:41-43` | |
| F1 0x1000d frame control、bit 0 read frame terminate | `BCM43455_F1_FRAME_CONTROL` | WHD `whd_sdio.h:106,228` | |
| F1 0x1000e chip clock CSR: force ALP 0x01・force HT 0x02・ALP req 0x08・HT req 0x10・HW req off 0x20・ALP avail 0x40・HT avail 0x80 | `BCM43455_CLOCK_*` | bwfm `if_bwfm_sdio.h:44-52`。WHD `whd_sdio.h:107,209-` | |
| F1 0x1000f SDIO pull-up（0 で切る） | `BCM43455_F1_PULL_UP` | bwfm `if_bwfm_sdio.h:67`、`if_bwfm_sdio.c:1038`（prepare の最後） | |
| F1 0x1001e wakeup control、bit 1 HT wait | `BCM43455_F1_WAKEUP_CONTROL` | bwfm `if_bwfm_sdio.h:68-69`、`if_bwfm_sdio.c:453-455` | |
| F1 0x1001f sleep CSR、bit 0 KSO | `BCM43455_F1_SLEEP_CONTROL` | bwfm `if_bwfm_sdio.h:70-71`、`if_bwfm_sdio.c:311-317`（SDIO core rev 12 以上） | |
| window: offset mask 0x7fff、4 byte の flag 0x8000、窓 32 KB | `BCM43455_WINDOW_*` | bwfm `if_bwfm_sdio.h:74-76` | |
| F2 の address = chipcommon の base の窓の中 \| 0x8000、**固定 address**（FIFO） | `BCM43455_FRAME_ADDRESS`、read/write の increment 0 | bwfm `if_bwfm_sdio.c:951-966`（`sdmmc_io_read_multi_1`） | |
| F1 block 64、F2 block 512 | `rpi4-wifi.c` | bwfm `if_bwfm_sdio.c:281-282` | |

## chip・core・EROM

| 値 | driver の名前 | 出典 | Pi |
| --- | --- | --- | --- |
| chipcommon 0x18000000、chip id 0x00（id bit 0〜15、rev 16〜19、type 28〜31、1 = AI） | `BCM43455_CHIPCOMMON_*`、`BCM43455_CHIP_*` | bwfm `bwfmreg.h:20-28` | chip id・rev・type: |
| capabilities 0x04（PMU bit 0x10000000）、ext 0xac（AOB 0x40）、EROM pointer 0xfc | `BCM43455_CHIPCOMMON_*`、`BCM43455_CAPABILITY_*` | bwfm `bwfmreg.h:29-34` | caps: / ext: |
| 4345 の chip id 0x4345、43455 は rev 6（rev 9 は 43456） | `BCM43455_CHIP_ID`・`_REVISION_MIN` | bwfm `bwfmvar.h:35`、`if_bwfm_sdio.c:377-381` | |
| RAM base 0x198000 | `BCM43455_RAM_BASE` | bwfm `bwfm.c:1563-1564` | |
| core id: chipcommon 0x800・D11 0x812・PMU 0x827・SDIO dev 0x829・ARM CR4 0x83e | `BCM43455_CORE_*` | bwfm `bwfmreg.h:68-75` | |
| wrapper: I/O control 0x408（CLK 0x1・FGC 0x2）、reset control 0x800（reset 0x1） | `BCM43455_WRAP_*`・`BCM43455_IO_CONTROL_*` | bwfm `bwfmreg.h:57-65` | |
| CR4 halt 0x20、D11 PHY clock 0x4・PHY reset 0x8 | `BCM43455_CR4_IO_CPU_HALT`・`BCM43455_D11_IO_*` | bwfm `bwfmreg.h:81-83` | |
| CR4 cap 0x04（A 0〜3・B 4〜7 の bank 数）、bank index 0x40、bank info 0x44（大きさ mask 0x7f、1 KB の flag 0x200、単位 8 KB か 1 KB） | `BCM43455_CR4_*` | bwfm `bwfmreg.h:86-95`、`bwfm.c:1533-1556` | RAM の大きさ: |
| EROM の descriptor: type mask 0xf、component 0x1・master port 0x3・address 0x5・wide 0x8・end 0xf | `BCM43455_EROM_*` | bwfm `bwfmreg.h:121-128` | |
| component: part 8〜19・class 4〜7・rev 24〜31・slave wrap 19〜23・master wrap 14〜18 | 同上 | bwfm `bwfmreg.h:129-144` | |
| address: base 0xfffff000・port 8〜11・kind 6〜7（0 slave・2 swrap・3 mwrap）・size 4〜5（0 4K・1 8K・3 described） | 同上 | bwfm `bwfmreg.h:149-164` | |
| core の register と wrapper は 4K・8K の descriptor からだけ採る、wrapper は master port のある core は mwrap | `scan_erom` | bwfm `bwfm.c:1182-1240`（手順の要点だけ、構造は自分で） | |
| PMU: chipcommon rev 35 以上で AOB なら PMU の core、でなければ chipcommon | `read_power_unit` | bwfm `bwfm.c:1027-1040` | PMU の場所: |
| PMU control 0x600（resource reload 2<<13）、capabilities 0x604（rev mask 0xff）、chip control addr 0x650・data 0x654 | `BCM43455_PMU_*` | bwfm `bwfmreg.h:42-51`、`if_bwfm_sdio.c:324-328` | PMU rev: |
| save-restore（4345）: PMU rev 17 以上、chip control 3 の bit 2 | `BCM43455_PMU_CHIP_CONTROL_SR`・`BCM43455_SAVE_RESTORE_BIT` | bwfm `bwfm.c:1384-1401` | save-restore: |
| SDIO core rev 12 以上で KSO を立てる | `BCM43455_SDIO_REVISION_KSO` | bwfm `if_bwfm_sdio.c:311-317` | SDIO core rev: |

## core の reset と download の順

| 手順 | 出典 | Pi |
| --- | --- | --- |
| disable: reset 中でなければ I/O control = pre \| FGC \| CLK、読み戻し、reset = 1、待つ。その後 I/O control = in-reset \| FGC \| CLK、読み戻し | bwfm `bwfm.c:1062-1098` | |
| reset: disable の後、reset が 0 になるまで 0 を書く（50 回）、I/O control = post \| CLK、読み戻し | bwfm `bwfm.c:1101-1125` | |
| passive: CR4 を (今の halt, halt, halt) で reset、D11 を (PHY reset\|PHY clock, PHY clock) で disable（reset のまま） | bwfm `bwfm.c:1288-1306` | |
| active: SDIO core の int status に 0xffffffff、address 0 に reset vector（firmware の先頭の語）、CR4 を (halt, 0, 0) で reset | bwfm `bwfm.c:1275-1285`、`if_bwfm_sdio.c:1045-1053` | |
| ALP: chip clock = HW req off \| ALP req、ALP を待つ、= HW req off \| force ALP、pull-up 0 | bwfm `if_bwfm_sdio.c:1002-1041` | |
| attach の後: KSO、card control の WLAN reset、PMU の resource reload、F2 を disable、chip clock 0 | bwfm `if_bwfm_sdio.c:311-334` | |
| download: HT を要求して待つ（5 s）、firmware を RAM base に、全部を読み戻し、NVRAM を RAM の top に、読み戻し、active、chip clock 0 | bwfm `if_bwfm_sdio.c:500-548`、`:596-640` | |
| start: HT を要求して待つ、clock \| force HT、to-chip mailbox data = 4<<16、F2 enable、host int mask = mailbox bits \| chip active、watermark 8、save-restore なら wakeup の HT wait・card capability の CMD14・clock = force HT、でなければ clock を戻す | bwfm `if_bwfm_sdio.c:432-462` | |
| NVRAM: 行ごとに NUL、`#` で始まる行は捨てる、CR は捨てる、NUL を 1 つ以上で 4 byte に詰める、長さの語 = 語数 \| ~語数<<16、RAM の top に（最後の 4 byte が長さの語） | bwfm `bwfm.c` の `bwfm_nvram_convert()`（:3009-）、`if_bwfm_sdio.c:526-528`。zedBSD は前後の空白と `=` の無い行も捨てる（Pi の NVRAM では結果は同じ） | NVRAM の長さ: |

## SDPCM・BCDC・mailbox

| 値 | driver の名前 | 出典 | Pi |
| --- | --- | --- | --- |
| frame tag: 長さ（LE16）と補数、software header 8 byte: seq・channel（下位 4 bit）・next len・data offset・flow control・max seq・予約 | `bcm43455-sdpcm.c` | bwfm `if_bwfm_sdio.h:150-179` | |
| channel: 0 control・1 event・2 data・3 glom | `BCM43455_SDPCM_CHANNEL_*` | bwfm `if_bwfm_sdio.h:167-172` | |
| 送りの窓: `(max − seq) & 0xff` が 0 でなく 0x80 の bit が無い時だけ送る | `bcm43455_sdpcm_window` | bwfm `if_bwfm_sdio.c:1076-1081` | |
| 初めの seq 0xff、max 0（窓 1） | `INITIAL_SEQUENCE`・`INITIAL_CREDIT` | bwfm `if_bwfm_sdio.c:268`（max は softc の 0） | |
| 送りの詰め: 512 を超えて 512 の倍数でなければ 512 に、でなければ 4 に | `bcm43455_sdpcm_padded_length` | bwfm `if_bwfm_sdio.c:1112-1117` | |
| 受け: header 12 byte を読み、残りを 4 byte に切り上げて読む。header だけの frame は credit だけ | `receive_frame` | bwfm `if_bwfm_sdio.c:1197-1260`。WHD は tag 4 byte を先に読む（`whd_bus_sdio_protocol.c:808-905`） | |
| 受けの中止: I/O abort（F2）→ frame control の terminate → to-chip mailbox の NAK | `abort_frame` | WHD `whd_bus_sdio_protocol.c:1317-1336` | |
| SDIO core: int status 0x20、host int mask 0x24、to-chip mailbox 0x40（NAK 0x1・ACK 0x2）、to-chip data 0x48、to-host data 0x4c | `BCM43455_SDIO_*` | bwfm `if_bwfm_sdio.h:87-149` | |
| int status: to-host mailbox 0xf0（FC state 0x10・FC change 0x20・frame 0x40・host int 0x80）、chip active 0x20000000 | `BCM43455_INT_*` | bwfm `if_bwfm_sdio.h:94-120` | |
| to-host data: NAK handled 0x1・dev ready 0x2・FC 0x4・FW ready 0x8・FW halt 0x10、版 bit 16〜23 | `BCM43455_TO_HOST_*` | bwfm `if_bwfm_sdio.h:145-149`、`:79-80` | 版: |
| SDPCM の版 4、to-chip data に 4<<16 | `BCM43455_SDPCM_PROTOCOL` | bwfm `if_bwfm_sdio.h:79-80`、`if_bwfm_sdio.c:441-442` | |
| interrupt の処理: status を mailbox の bit \| chip active で切って書き戻し、host int なら mailbox を読んで ACK、frame は毎回読む | `service`・`handle_mailbox` | bwfm `if_bwfm_sdio.c` の `bwfm_sdio_task()`（:699-745） | |
| BCDC control: cmd・len・flags（error 0x1・set 0x2・if bit 12〜15・id bit 16〜31）・status | `bcm43455-bcdc.c` | bwfm `bwfmreg.h:314-329` | |
| BCDC data: flags（版 2 を bit 4〜7）・priority（0x7）・flags2（if 0xf）・data offset（4 byte 単位） | 同上 | bwfm `bwfmreg.h:331-342` | |
| firmware の誤りの数: status −23 = unsupported | `FIRMWARE_UNSUPPORTED` | WHD `whd_types.h:1066,1090`（WLAN_ENUM_OFFSET 2000 + 23） | |
| WLC: UP 2・DOWN 3・SET_PM 86・GET_VALID_CHANNELS 217・GET_VAR 262・SET_VAR 263 | `BCM43455_WLC_*` | bwfm `bwfmreg.h:248-311`。WHD `whd_wlioctl.h:1305-1539` | |

## event・iovar・CLM・country

| 値 | driver の名前 | 出典 | Pi |
| --- | --- | --- | --- |
| event: ethertype 0x886c、vendor header（subtype・length・version・OUI 00:10:18・user subtype 1）、event message（big-endian、48 byte） | `bcm43455-event.c` | bwfm `bwfmreg.h:754-783`、`bwfm.c:2367-2373`（OUI と user subtype だけを見る。subtype 0x8001 は WHD `whd_events.h:140` の「32769」、zedBSD も要求しない） | |
| event の番号: SET_SSID 0・AUTH 3・DEAUTH 5・DEAUTH_IND 6・ASSOC 7・DISASSOC_IND 12・LINK 16・PSK_SUP 46・ESCAN_RESULT 69 | `BCM43455_EVENT_*` | bwfm `bwfmreg.h:659-722`。WHD `whd_events.h:37-53` | |
| event mask: `event_msgs`、18 byte（E_LAST 139 を 8 に切り上げ） | `BCM43455_EVENT_MASK_LENGTH` | bwfm `bwfmreg.h:731-733`、`bwfm.c:518`。WHD は 16 byte（`whd_wlioctl.h:1932`）。43455 を扱う bwfm に合わせた | 受け付けたか: |
| `cur_etheraddr`・`ver`・`cap`・`roam_off`・`clmload`・`clmload_status`・`country` | iovar の名前 | WHD `whd_wlioctl.h:1073-1232`。bwfm `bwfm.c:261` | |
| `bus:rxglom` 0 | `start_settings` | **公開の寛容な出典で未確認**（WHD には `bus:txglom` だけ、`whd_wlioctl.h:1088`）。失敗しても続け、glom の frame が来たら 1 度 log に出す | glom の log の有無: |
| clmload: flag（handler 版 1<<12・BEGIN 0x2・END 0x4）、type 2（CLM）、len、crc 0、1 回 1400 byte | `bcm43455-nvram.c` | WHD `whd_wlioctl.h:4154-4172`、`whd_clm.c:54-58`。bwfm `bwfmreg.h:785-797` | CLM の status: |
| country: abbrev[4]・rev（int32）・ccode[4] の 12 byte、`XX` | `set_country` | WHD `whd_wlioctl.h:667-673,1073` | 読み戻した country・rev: |
| country の rev −1（firmware に任せる）、断られたら 0 | `COUNTRY_REVISION_ANY` | **推測、公開の寛容な出典で未確認**。Pi で読み戻した rev を log で確かめる | |
| PM 0（省電力を切る） | `POWER_SAVE_OFF` | bwfm `bwfm.c:548`（`BWFM_C_SET_PM` の値の意味は WHD `whd_wlioctl.h` の PM の定義） | |

## firmware の file（p003 の package の値）

| 値 | 出典 | Pi |
| --- | --- | --- |
| `/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin` 548728 byte、`43455c0-roml/` の版の文字列に `idsup` | [p003](../phase003/phase.md)。host 試験が P3 の検証済みの cache（sha256 `3075cb0b…6e60`）で確かめた | 読めたか: |
| `/lib/firmware/cypress/cyfmac43455-sdio.clm_blob` 2676 byte、先頭 `BLOB` | 同上（`9823842c…9cc1`） | |
| `/lib/firmware/brcm/brcmfmac43455-sdio.txt` 2074 byte、`macaddr=b8:27:eb:74:f2:6c`（全ての Pi に共通の仮の値）、`ccode` 無し | 同上（`ca709be8…9e3d`）。変換の後 1748 byte | |
