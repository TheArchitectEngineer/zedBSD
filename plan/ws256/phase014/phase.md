<!-- awesome-plan project=zedbsd record=ws256-p014 -->

# ws256-p014 SD カード（DW MMC の PIO と JH7110 の glue、板の前の code）

Status: in-progress（2026-10-19 P2: 板の前の code と host 試験は済み、Q1 の判定待ち。実機の確かめは p015 の時に下の「p015 で画面に出る行」で行う。IDMAC は未着手（下の「残り」））

- 親: [WS256](../ws.md)。Queue: q1288-i01（P2）。
- 範囲: [design-peripherals.md](../design-peripherals.md) §3.3（改訂 4）。`src/drivers/mmc/dw-mmc.c` の SD の初期化（CMD0・8・ACMD41・2・3・9・7・16、ACMD51・ACMD6、high speed の CMD6）と CMD17/18/24/25、FIFO の PIO、JH7110 の glue（`vf2-mmc.c`）、block の device（mmcblk0）として publish、`visionfive2.c` からの attach。host 試験（fake の register の model で command の順と FIFO の読み書き）、build warning 0（VF2・virt）。HAL（hal.h）は変えない。rpi4-sdhci.c は読むだけ（切り出さない、design の既定）。
- 完了の条件: host 試験が合格、build が warning 0（VF2・virt）、p015 の時に画面に出す行をこの文書に書く。Q1 が合格とする。

## 実装（2026-10-19、P2、commit 1912cc7ab・34e200d35・4015f554c・52a5611a8）

| file | 中身 |
| --- | --- |
| `include/drivers/mmc/dw-mmc.h`（新規） | glue が渡す `struct drv_dw_mmc_config`（map 済みの register、ciu の rate、fifo-depth、bus-width、max-frequency、high speed の可否、電源の後の待ち、card detect の callback、unit 番号）、見つけた card の `struct drv_dw_mmc_card`、`drv_dw_mmc_attach`・`drv_dw_mmc_card_info` |
| `src/drivers/mmc/dw-mmc.c`（新規） | DesignWare Mobile Storage Host の PIO の driver。割り込みも DMA も使わず raw の interrupt status を poll する。HCON から FIFO の幅（32・64 bit）、VERID から FIFO の port（2.40a より前は 0x100、以後 0x200）、fifo-depth が無ければ FIFOTH の reset 値から深さ。reset → 割り込みを全部 mask → DMA を切る → FIFO の閾値を半分 → TMOUT 最大 → 1 bit → PWREN → 400 kHz。SD: CMD0 → CMD8（0x100｜0x23、答えなければ SDSC）→ ACMD41（0x00300000｜HCS、1 秒まで 10 ms ごと）→ CMD2 → CMD3 → CMD9（CSD 版 1・2 の容量）→ CMD7（R1b）→ CMD16(512) → 25 MHz → ACMD51（SCR）→ 4 本なら ACMD6 と CTYPE → 版 1.10 以上で CSD の CCC に switch の class があり slot が許せば CMD6 の問い合わせ → high speed があれば CMD6 の切り替え → 50 MHz（max-frequency 以下）。転送は 128 block（64 KiB）までの run ごとに 1 command（1 block は CMD17/24、複数は CMD18/25 と controller の auto stop）。データ毎に FIFO を reset、書き込みの後は CMD13 で READY_FOR_DATA まで待つ。失敗した run は FIFO の reset → CMD12（stop abort）→ CMD13 で戻して 3 回まで。戻らない card は failed にして以後は即 EIO。disk は `mmcblk<unit>`、removable、512 byte、CSD の容量、`DISK_STATS_KIND_SDMMC`。FIFO が 1 秒動かない時は ETIMEDOUT（無限の回りを防ぐ） |
| `src/drivers/platform/visionfive2/vf2-mmc.c/.h`（新規） | JH7110 の glue。`starfive,jh7110-mmc` で enabled かつ reg が 0x16020000（SDIO1）の node を選ぶ（mmc0 の eMMC と disabled は飛ばす）。register を map → pinctrl-0 → biu・ciu の lookup → `assigned-clock-rates`（SDCARD 50 MHz → 分周 8 で 49.5 MHz）→ 2 つの clock の enable → reset の解除 → ciu の rate を CRG から計算。`cd-gpios`（GPIO 41、active low）を input にして callback で読む。DT から fifo-depth・bus-width・max-frequency・cap-sd-highspeed・post-power-on-delay-ms。sample phase（下）は読んで log に出すだけ |
| `src/kern/platform/visionfive2.c` | `probe_soc` が成功したら tree を残し、`disk_registry_reset` の後、ramdisk の次に `drv_vf2_mmc_attach` で mmcblk0 を publish（失敗しても起動は続く。boot device は ramdisk のまま）。`kern_platform_refresh_devices` で SD の partition の一覧を log に出す。partition の scheme を MBR から `drv_partition_scheme_pcat_auto`（GPT と MBR を見分ける）に変えた: ramdisk の root.img は MBR、SD の UEFI image（disk-image.mk）は GPT なので |
| `platform/riscv64/vmunix.mk` | vf2 の object に `vf2-mmc.c`・`dw-mmc.c`・`disklabel/gpt.c`・`disklabel/pcat.c`（virt の行は不変） |

- 出典（block ごとに `Value(s) referenced:`・`Procedure referenced:`、file の先頭には書かない）:
  - register の offset・bit、command の組み立て、clock の手順、PIO の手順、FIFO の閾値、HCON・FIFOTH・STATUS の field: OpenBSD `sys/dev/fdt/dwmmc.c`（ISC、master 859883f17cb8、P1 の `build/tmp/ws256-docs/obsd-dwmmc.c`）。
  - SD の command 番号・OCR・CSD・CID・SCR・switch の field と初期化の順: OpenBSD `sys/dev/sdmmc/sdmmcreg.h`・`sdmmc.c`・`sdmmc_mem.c`（ISC、同じ commit）。P2 が取得: `build/tmp/p014-docs/obsd-sdmmcreg.h`（4e9e7b80ff2a424a30f045941a07411158375cd5b39a9bf3b71268de17aac5fa）、`obsd-sdmmc.c`（8dfa91743a5003427493c23021b77323614051adb813b1a2d28c0578b5feb094）、`obsd-sdmmc_mem.c`（479db30dc446a323840b5ad9762b2ca30232c5d22dd69469eb583b28229eec9a）、URL は `https://raw.githubusercontent.com/openbsd/src/859883f17cb8e20088fece5398547e4264ad30b3/sys/dev/sdmmc/<file>`。
  - FIFO の port の版による違い（DW_MMC_240A・DATA_OFFSET・DATA_240A_OFFSET）と sample phase の register（SDMMC_UHS_REG_EXT 0x108、STARFIVE_SMPL_PHASE bits 20:16）: Linux v6.12（GPL、値の引用、Guardrail 2026-10-15 の「GPL の file の定数もデータ」）。P2 が取得: `build/tmp/p014-docs/linux-v6.12-dw_mmc.h`（8f246ae68bf09e16c01bfd1f4152c2519ae18ce3eeea1cb5d0a3dfe0ea860c88）、`linux-v6.12-dw_mmc-starfive.c`（32e2966a41bac088b93a3e27eb8c042c44ffcf51d74cb875c809340fc0d26251）、URL は `https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/mmc/host/<file>`。
  - mmc1 の address・`starfive,sysreg` の値: Linux v6.12 `jh7110.dtsi`（P1 の directory）。
  - GPL の手順は使っていない（sample phase の tuning を書かなかったので、決定 13:39 の「手順の参考」も使っていない）。code・comment・構造・symbol の名前は写していない。
- 回復の手順（FIFO の reset → CMD12 → CMD13）は OpenBSD に無い自前の組み立て（SD の CMD12 で転送を止め CMD13 で tran に戻るのを待つ）。controller の reset（CIU の reset と clock の取り直し）は入れていない（実機で FIFO の reset だけで戻らない時に足す）。

## 設計との違い・見つけた事

- **sample phase**: design §3.3 は「`starfive,sysreg` の sample phase（Linux の参考）」と書いたが、Linux v6.12 の `dw_mmc-starfive.c` は sysreg を使わず、controller の `UHS_REG_EXT`（0x108）の bits 20:16 を **tuning（UHS の SDR50/SDR104 と HS200）の時だけ** 書く。VF2 の slot は UHS 無し（3.3 V の 50 MHz まで、DT に UHS の cap 無し）なので tuning は走らず、Linux でも sample phase は firmware の値のまま。→ glue は sample phase を書かず、`UHS_REG_EXT` の phase と sysreg の field（0x9c、shift 1、mask 0x3e）を読んで log に出すだけにした。50 MHz で CRC の誤りが出る板では、この 2 つの値を見て固定の phase を試す（p015 の結果次第）。design の文言の直しは Q1 に依頼する。
- **partition の scheme**: VF2 は MBR の scheme だったので、SD の UEFI image（GPT、ESP は LBA 2048、FAT の serial 5A45-4253）の partition が見えなかった。`pcat_auto` に変えた（MBR の ramdisk はそのまま MBR として読む）。これで loader が作る `boot0=UUID=5A45-4253` が SD の ESP に解決されるようになる（今までは解決されなかった）。ramdisk の FAT の serial は 7A65-6442 で重ならない（重なると `block_identity_resolve` が EEXIST）。root の探し方（boot device = ramdisk）は変えていない。
- 書いていない物: IDMAC（DMA）、割り込み（IRQ 75）、UHS・1.8 V・tuning、eMMC（mmc0）、card の抜き差しの検出（attach の時に 1 回読むだけ）、write protect、card status の error bit の検査（OpenBSD と同じく見ない。CRC・timeout・READY_FOR_DATA だけ）、試験の cdev（scratch partition の raw の読み書き）。

## 試験（2026-10-19、P2、host と build だけ。QEMU・実機は無し）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験（driver と glue） | `plan/ws256/tests/dw-mmc/run.sh build/p014-host` | exit 0、PASS 60・FAIL 0（log build/tmp/p014-host.log、`OK: 0 failure(s)` が driver・glue の 2 回） |
| 既存の vf2-soc の host 試験（回帰） | `plan/ws256/tests/vf2-soc/run.sh build/p014-vf2soc` | exit 0、PASS 94・FAIL 0（vf2 の CRG・pinctrl・syscon・fdt は今回変えていない） |
| VF2 の vmunix | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p014-vf2 -j16 vmunix` | exit 0、`warning:`・`error:` 0（build/tmp/p014-vf2-4.log）。dw-mmc.o・vf2-mmc.o・gpt.o・pcat.o を compile |
| virt の vmunix | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p014-riscv64-virt -j16 vmunix` | exit 0、`warning:`・`error:` 0（build/tmp/p014-virt-2.log） |

- rpi4・amd64 は build していない（共有の file は riscv64 の `vmunix.mk` の vf2 の行だけで、fdt.c・disklabel は変えていない）。

host 試験の中身（`plan/ws256/tests/dw-mmc/`: `dw-mmc-host-test.c` は fake の controller（register を memory に持ち、command register の start bit で command を走らせる）と fake の SD card、`vf2-mmc.dts` は VF2 の形の木（mmc0・disabled の node・mmc1 と板の property））:
- 空の slot は ENODEV で command 0 本。同じ unit の 2 回目は EBUSY。
- SDHC（32 bit FIFO、VERID 0x290a）: command の順が CMD0・8・ACMD41×2（1 回目は busy）・2・3・9・7・16・ACMD51・ACMD6・CMD6×2。全部の command に start と hold、CMD12・13 以外に wait_prvdata、CMD0 は初期化の clock で応答無し、CMD8 の引数 0x123 と CRC、ACMD41 の 0x40300000 と CRC 無し、CMD2・9 は long、CMD9・7 は RCA、CMD16 は 512、ACMD51 は data の read、ACMD6 は 2、CMD6 は 0x00ffffff → 0x80fffff1。clock の分周は 62（399 kHz）→ 1（24.75 MHz）→ 0（49.5 MHz）。CTYPE は 4 bit、FIFOTH は (2, 15, 16)、DMA は off、PWREN は 1。CID（manufacturer・OEM・product・revision・serial）、CSD 版 2 の容量（C_SIZE 59639 → 61071360 block）、disk の名前・容量・block の大きさ・run の上限。
- 読み書き: CMD17（block 100）、CMD18 の 8 block（auto stop、BYTCNT 4096）、CMD24 の後に CMD13、CMD25 の 3 block（auto stop）、書いた物の読み戻し、200 block は 128＋72 の 2 run、flush。
- 回復: CRC の誤り 1 回は CMD18 → CMD12 → CMD13 → CMD18 で同じ data、3 回続くと EIO で 0 byte、その後の request は通る。
- SDSC（CMD8 に答えない、64 bit FIFO、VERID 0x210a で port 0x100、fifo-depth を FIFOTH から 16）: ACMD41 は 0x00300000、CSD 版 1 の容量 4096 block、byte の address（block 3 → 1536）、1 bit の 24.75 MHz、mmcblk1、64 bit FIFO の読み書き。
- CSD の CCC に switch の class が無い card は ACMD6 の後に CMD6 を送らず 24.75 MHz。
- glue（VF2 の形の木、PLL2 1188 MHz、axi_cfg0 / 3）: card detect が high なら ENODEV で command 0 本、low なら mmcblk0。SDIO1 AHB と SDCARD の gate が開く、SDCARD の分周 8（49.5 MHz）、SDIO1 の reset が解ける、bring-up の順、fifo-depth 32・bus-width 4・cap-sd-highspeed が木から入る、sample phase と sysreg の field を log に出す。

## p015 で画面に出る行（実機、ユーザーが framebuffer を目で見る: 2026-10-19 の決定 #10・#15）

SD から起動した時（U-Boot → BOOTRISCV64.EFI → vmunix、initrd は ESP の root.img）、boot の画面（kernel の log）に p013 の行（`visionfive2: pll0 …`・`axi_cfg0 … sdcard …`・`SD card present (gpio 41)`）の後に次の行が出る。

1. `vf2-mmc: sample phase P, sysreg 9c field F` — 値は記録するだけ（firmware が残した値。CRC の誤りが出た時の手掛かり）。
2. `dw-mmc: mmcblk0 controller version XXXXXXXX, FIFO 32 x W bytes, ciu 49500000 Hz` — ciu は p013 の `sdcard … Hz` と同じ 49500000 のはず（PLL2 1188 MHz、axi_cfg0 / 3、SDCARD / 8）。version の下 16 bit と FIFO の幅（W = 4 か 8）を記録する（HCON の DMA64 は IDMAC の時に読む）。
3. `dw-mmc: mmcblk0 SDHC card NNNNN, manufacturer MM, OEM OO, revision a.b, serial SSSSSSSS` — card の型（32 GB 以下は SDHC、それより大きい card は SDXC、2 GB 以下の古い card は SDSC）と product の名前が card の表示と合う。
4. `dw-mmc: mmcblk0 N blocks of 512 bytes (M MiB)` — card の公称の容量と合う（例 32 GB の card は約 30000 MiB 前後、公称は 10^9 byte なので MiB では 1 割ほど小さい）。
5. `dw-mmc: mmcblk0 bus 4-bit, 49500000 Hz, high speed` — 4 bit と high speed。古い card は `24750000 Hz, default speed`。
6. `visionfive2: SD card is mmcblk0`
7. （`boot: platform devices detected` の後）`visionfive2: mmcblk0 has 1 partitions` と `visionfive2: mmcblk0 partition 1 start 2048 blocks B (X MiB) "EFI system", EFI system` — zedBSD の disk.img を書いた SD の時。B は image の ESP の大きさ（64 MiB 以上）。別の SD（Debian の image など）なら、その partition の表が出る。
8. 出てはいけない行: `dw-mmc: controller 0 unusable`、`dw-mmc: card in slot 0 unusable (E, status S)`、`visionfive2: no SD card disk (E)`、`dw-mmc: read of … failed`・`write of … failed`、`visionfive2: SD card slot not brought up`、`mmcblk0 partition table unreadable`。出た時は E（errno）と S（raw の interrupt status）を記録する。
9. 起動が今まで通り ramdisk の root で login まで進む（boot device は ramdisk のまま。`/boot/efi` か boot0 に SD の ESP が見えるようになる見込み: 上の「partition の scheme」）。

p015 の時に追加で見るとよい事（design §3.3 の実機の試験）: ESP の FAT を `ls`（boot0 か /boot/efi の mount で）。scratch の partition の raw の読み書きの一致は試験の cdev か raw の device の道具が要る（このPhaseでは作っていない）。

## 残り

- Q1 の判定と merge。
- 実機の確かめ（p015 の時、上の 1〜9）。
- IDMAC（DMA）: design §3.3 で p014 の後半か Future Work。p023（coherency の確かめ）の結果の後に、HCON の DMA64 で descriptor の 32/64 bit を決め、§2.4 の短い確かめを SD の scratch の partition で流す。今回は未着手（Q1 の依頼の範囲が PIO）。
- design §3.3 の sample phase の文言の直し（上の「設計との違い」、Q1 へ）。
- 試験 `plan/ws256/tests/dw-mmc/` を回帰に残すかは Q1 の判断（残すなら Master の Tools・試験の一覧へ）。
