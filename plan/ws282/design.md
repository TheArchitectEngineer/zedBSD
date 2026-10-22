# WS282 camera の driver の枠組み（/dev/cam0）と Pi 5 の CSI の camera の driver「rpicam」の設計

- 版 1（2026-10-23 P3、ws282-p001）。review は Q1 が手配する（この版は未 review）。
- 資料（全部読んだ上で書いた）:
  - zedBSD: `src/drivers/platform/rpi5/rp1.c`・`rp1-irq.c`・`rp1-gpio.c`・`rp1-ethernet.c`・`rpi5-fan.c`（RP1 の clock の扱い）、`src/drivers/i2c/i2c.c`・`lpss-i2c.c`（DesignWare I2C）、`include/drivers/generic/dma.h`・`fdt.h`、`include/kern/cdev.h`・`vm-device.h`・`handle.h`・`waitq.h`・`poll.h`・`pmem.h`、`include/hal/hal.h`（cache の操作）、`include/uapi/gpu.h`・`gpu-allocation.h`・`audio.h`・`ioctl.h`、`src/drivers/gpu/gpu.c`（export/import）、`src/drivers/gpu/bcm2711/buffer.c`・`share.c`・`render-device.c`、`src/kern/devfs.c`（node の mode）、`userland/desktop/sessiond/seat.c`、`userland/desktop/libvulkan/memory.c`、`src/kern/platform/rpi5.c`、`platform/arm64/vmunix.mk`、`config/ci/config-rpi5.mk`、[WS278 の設計](../ws278/design.md)、[WS279](../ws279/ws.md)。
  - Raspberry Pi の Linux `raspberrypi/linux` rpi-6.12.y（2026-10-23 に raw.githubusercontent.com から取った物。作業用の写しは scratchpad に置き、commit しない）: `drivers/media/platform/raspberrypi/rp1_cfe/{cfe.c,cfe.h,cfe_fmts.h,csi2.c,csi2.h,dphy.c,dphy.h,pisp_fe.c,pisp_fe.h}`、`drivers/media/i2c/ov5647.c`・`imx219.c`、`drivers/clk/clk-rp1.c`、`drivers/pinctrl/pinctrl-rp1.c`、`drivers/i2c/busses/i2c-designware-core.h`、`arch/arm64/boot/dts/broadcom/{rp1.dtsi,bcm2712-rpi-5-b.dts,bcm2712-rpi.dtsi}`、`arch/arm/boot/dts/overlays/{ov5647-overlay.dts,ov5647.dtsi,imx219-overlay.dts,imx219.dtsi}`、`include/dt-bindings/clock/rp1.h`。
  - firmware の dtb: `build/sources/firmware/rpi5-boot/3d301dd924bc…/bcm2712d0-rpi-5-b.dtb`（`dtc` で読んだ。camera の node は overlay が無いと `status = "disabled"` で sensor の node も無い）。
- license: [Guardrail の「MIT・GPL の出典のデータの引用」](../guardrail.md)に従う。Linux（GPL-2.0）の register の offset・bit・sensor の register の表・clock の値は hardware の事実（データ）として使い、**該当の block の直前に複数行 comment で「Value(s) referenced: rpi-6.12.y <path> (<表・define の名前>)」「Procedure referenced: rpi-6.12.y <path> (<関数の名前>)」を書く**。file の先頭には出典を書かない。code・comment・構造・symbol の名前は写さない（独自の名前）。zedBSD の code は Zlib。この文書の §8 に出典の一覧を置く（code には写さない、設計の記録）。

## 0. 要約

- `/dev/camN` は zedBSD 独自の fixed-width の ioctl の ABI（group `'C'`、`include/uapi/camera.h`）。format は RAW（Bayer 8/10/12、packed と 16 bit の unpacked）・YUV・RGB を宣言し、今回は RAW だけ実装する（ユーザーの指示）。buffer は driver が確保して `mmap` する形と、**GPU が確保して export した fd を camera が取り込む形**（Vulkan へ複写なしに渡す、§3.6）の 2 つ。frame ごとに sequence・時刻・露光・gain の metadata。control は露光・analogue gain・flip・test pattern（sensor の AEC/AGC も宣言）。
- kernel の中は 3 層: **枠組み**（`src/drivers/camera/cam.c`、node・queue・状態機械・ABI）、**receiver の driver**（CSI-2 の受信と DMA。Pi 5 は RP1 の CFE: `src/drivers/platform/rpi5/rp1-cfe.c`）、**sensor の driver**（I2C。`src/drivers/camera/sensor/ov5647.c`、後で `imx219.c`）。receiver と sensor は `struct drv_cam_receiver_ops`・`struct drv_cam_sensor_ops` で枠組みにつなぎ、板ごとの束ね（`rpicam.c`）が device tree から両方を見つけて `/dev/cam0` にする。
- Pi 5: sensor の I2C は RP1 の DesignWare I2C（CAM/DISP 1 connector は i2c4 = RP1 GPIO 40/41、CAM/DISP 0 は i2c6 = GPIO 38/39）、電源は RP1 GPIO 46（CAM1）/34（CAM0）の regulator の enable、receiver は RP1 の csi1（CAM1）/csi0（CAM0）の CSI2-DMA・DPHY・MIPICFG の block、clock は RP1 の `clk_mipi1_cfg`/`clk_mipi0_cfg` を xosc 50 MHz から 25 MHz に、割り込みは RP1 の MSI-X vector 48（MIPI1）/47（MIPI0）。PiSP の front end は今回触らない（RAW は CSI2-DMA が直に memory へ書く）。
- sensor: chip ID を I2C で読んで決める。OV5647（5 MP、address 0x36、ID 0x5647）を最初に、IMX219（8 MP、0x10、ID 0x0219）は宣言だけ（hardware が無い、D5）。OV5647 の mode は Linux の 4 つ（2592x1944・1920x1080・1296x972・640x480、全部 RAW10、2 lane、link 218.5/208.3 MHz）。既定は 1296x972（2x2 binning、全画角、約 32 fps）。
- Phase: p002 枠組み（5〜7 LW）、p003 RP1 の I2C と sensor の検出（3〜4 LW）、p004 RP1 の CFE と最初の frame・`camcap`（6〜8 LW）、p005 control・metadata・堅牢化（3〜4 LW）、p006 GPU の buffer の取り込み（3〜4 LW）、p007 IMX219（2〜3 LW、hardware があれば）、p008 文書・seat・devfs（1〜2 LW）、p009 規約の全文（2 LW）。合計 25〜34 LW。
- ユーザーの判断 D1〜D7（§6）。

## 1. 責務と境界

| 層 | 場所 | 責務 | 持たない物 |
| --- | --- | --- | --- |
| user の ABI | `include/uapi/camera.h` | `/dev/camN` の ioctl・mmap・poll の契約。format・mode・buffer・control・metadata の記述 | hardware の名前・register |
| 枠組み（camera core） | `src/drivers/camera/cam.c`、`include/drivers/camera/cam.h` | cdev の登録（`camN`）、open の所有、format の交渉（sensor の mode × receiver の出力）、buffer の確保・mmap・import・queue・dequeue・poll、stream の状態機械、frame の完了と metadata、control の中継、hardware の失敗の回復 | register の操作、I2C |
| receiver の driver | Pi 5: `src/drivers/platform/rpi5/rp1-cfe.c`（他の板は各 platform の下） | CSI-2 の受信路（DPHY・lane・data type・VC）、frame の DMA（address・stride・size）、割り込み（frame start/end、discard・overflow）、clock と電源 | buffer の所有、user の ABI、sensor |
| sensor の driver | `src/drivers/camera/sensor/ov5647.c`（後で `imx219.c`） | I2C の register、検出（chip ID）、電源の順序（regulator の GPIO と待ち時間）、mode の表（size・Bayer・link 周波数・lane）、stream on/off、control（露光・gain・flip・test pattern） | DMA、割り込み |
| 板の束ね | `src/drivers/platform/rpi5/rpicam.c` | device tree から csi・i2c・regulator の node を見つけ、I2C の bus を作り、sensor を探し（両 connector）、receiver を作り、枠組みに登録する。`kern_platform_refresh_devices()` から起動 | 汎用の物 |
| I2C の controller | `src/drivers/platform/rpi5/rp1-i2c.c`（または `src/drivers/i2c/dw-i2c-core.c` に core を抜く、D2） | RP1 の DesignWare I2C を `drv_i2c_bus_register()` で I2C の registry に登録（`drv_i2c_transfer()` で使う） | sensor の知識 |

境界の原則:
- 枠組みは receiver・sensor を関数表（§2.2）でしか知らない。Pi 5 固有の物は `src/drivers/platform/rpi5/` に置き、sensor の driver は I2C の registry（`drv_i2c_transfer`）と regulator の抽象（枠組みが渡す「電源を入れる・切る」の callback）だけを使うので他の板（例: VF2 の CSI、将来の PiSP）でも使える。
- 今回実装する format は RAW（Bayer）だけ。YUV・RGB は ABI と枠組みの表に宣言し、receiver が「対応しない」と答える（`ENUM_FORMAT` の `flags` に supported の bit）。後で PiSP（RP1 の front end と BCM2712 の ISP back end）の WS が YUV/RGB の受信路を足すとき、枠組みの ABI は変えない（§3.8 の拡張の口）。

## 2. kernel の中の interface

### 2.1 枠組みの公開 API（`include/drivers/camera/cam.h`）

```c
/* 枠組みに登録する 1 台の camera: receiver と sensor と、板の名前。 */
struct drv_cam_device_desc {
	const char *board_name;				/* "rpicam" など、GET_INFO の driver の名前 */
	const struct drv_cam_receiver_ops *receiver;
	void *receiver_argument;
	const struct drv_cam_sensor_ops *sensor;
	void *sensor_argument;
	uint64_t dma_physical_limit;			/* receiver の DMA が届く物理 address の上限（含む）、0 で制限なし */
	uint32_t dma_alignment;				/* buffer の address・stride の揃え（RP1 は 16） */
	uint32_t dma_coherent;				/* 0 なら枠組みが cache を扱う */
};

int drv_cam_register(const struct drv_cam_device_desc *desc, struct drv_cam_device **result);
/* unregister は今回作らない（RP1 は抜けない）。将来の USB camera のために cdev_register_managed の finalizer で退役できる形にしておく。 */

/* receiver が割り込みで枠組みに知らせる（割り込み context、sleep しない）。 */
void drv_cam_frame_start(struct drv_cam_device *device, uint64_t timestamp_ns);
void drv_cam_frame_end(struct drv_cam_device *device, uint32_t flags);	/* DRV_CAM_FRAME_ERROR などを立てて渡す */
void drv_cam_receiver_error(struct drv_cam_device *device, uint32_t errors);	/* overflow・discard の種類の bit。統計に足す */
```

### 2.2 receiver と sensor の関数表

```c
/* 1 つの受信路の設定: 枠組みが sensor の mode と user の format から作る。 */
struct drv_cam_link {
	uint32_t width, height;			/* 受ける frame（sensor の出力） */
	uint32_t csi_data_type;			/* MIPI CSI-2 の DT（RAW10 = 0x2b など） */
	uint32_t virtual_channel;		/* 0 */
	uint32_t data_lanes;			/* 2 */
	uint64_t link_frequency_hz;		/* sensor の link 周波数（DDR なので bit rate はこの 2 倍） */
	uint32_t continuous_clock;		/* 0 なら clock lane は非連続 */
	uint32_t output_format;			/* CAM_FORMAT_*: receiver が memory に書く形（packed か 16 bit の unpacked か） */
	uint32_t stride_bytes;			/* 枠組みが揃えて決める */
	uint32_t image_bytes;
};

struct drv_cam_receiver_ops {
	int (*query)(void *argument, struct drv_cam_receiver_caps *caps);	/* 対応する出力 format、最大 size、lane 数 */
	int (*prepare)(void *argument, const struct drv_cam_link *link);	/* clock・DPHY の rate・channel の設定（まだ流さない） */
	int (*set_buffer)(void *argument, uint64_t bus_address, uint32_t stride, uint32_t bytes);	/* 次の frame の宛先（割り込み context から呼ばれる） */
	int (*start)(void *argument);		/* 受信を始める（sensor の stream on の前） */
	void (*stop)(void *argument);		/* 受信を止める（sensor の stream off の後） */
	int (*read_counters)(void *argument, struct cam_counters *counters);	/* discard・overflow の統計 */
};

struct drv_cam_sensor_ops {
	const char *name;			/* "ov5647" */
	int (*power_on)(void *argument);	/* regulator の GPIO、待ち、I2C の初期化（sleep してよい） */
	void (*power_off)(void *argument);
	int (*identify)(void *argument);	/* chip ID を読んで ENODEV か 0 */
	unsigned (*mode_count)(void *argument);
	int (*mode)(void *argument, unsigned index, struct drv_cam_sensor_mode *mode);	/* size・Bayer の並び・fps・link 周波数・lane・DT */
	int (*set_mode)(void *argument, unsigned index);	/* register の表を書く（stream on の前） */
	int (*start)(void *argument);		/* stream on */
	int (*stop)(void *argument);		/* stream off */
	int (*control_info)(void *argument, uint32_t id, struct cam_control_info *info);
	int (*set_control)(void *argument, uint32_t id, int64_t value);	/* sleep してよい（I2C） */
	int (*get_control)(void *argument, uint32_t id, int64_t *value);
	uint32_t (*bayer_order)(void *argument);	/* flip の今の値で変わる（OV5647） */
};
```

- sensor の mode は sensor の driver の静的な表。枠組みは `ENUM_MODE` でそのまま見せ、`SET_FORMAT` では「幅と高さが一致する mode」を選ぶ（近い物に丸めない。user が `ENUM_MODE` で選べるので丸めは要らない、Linux の `v4l2_find_nearest_size` とは違う方針。理由: 黙って別の size になる方が demo の app に不便）。fps は mode の固定値（VBLANK の control で下げられるのは後の拡張）。
- `set_buffer` は割り込み context から呼ぶので register の書き込みだけ。

### 2.3 Pi 5 の束ね（rpicam.c）が device tree から読む物

| 物 | node | 読む property | 値（D0 の dtb で確認） |
| --- | --- | --- | --- |
| CSI の block | rp1 の子で compatible `raspberrypi,rp1-cfe`（2 つ。`reg` の最初の address で csi0/csi1 を区別） | `reg` ×4（CSI2 DMA、DPHY/CSI host、MIPI CFG、PiSP FE）、`interrupts`、`clocks` の 2 番目の cell | csi0: 0x40110000/0x40114000/0x40120000/0x40124000、int 47、clock 22（MIPI0_CFG）。csi1: 0x40128000/0x4012c000/0x40138000/0x4013c000、int 48、clock 23（MIPI1_CFG）。両方 `status = "disabled"`（overlay が無いので。**status は見ない**） |
| sensor の I2C | rp1 の子で compatible `snps,designware-i2c`、`reg` の address で選ぶ | `reg`、`clock-frequency`（100000） | CAM1: i2c4 = 0x40080000（int 11、pin 40/41）。CAM0: i2c6 = 0x40088000（int 13、pin 38/39）。`clocks` は RP1_CLK_SYS（200 MHz） |
| 電源 | 根の子で compatible `regulator-fixed`、`regulator-name` が `cam1_reg`/`cam0_reg` | `gpio`（phandle、pin、flags）、`enable-active-high` | cam1_reg: rp1_gpio 46。cam0_reg: rp1_gpio 34。（dtb の `gpio = <0x2e 0x2e 0x00>`・`<0x2e 0x22 0x00>`、0x2e は rp1_gpio の phandle） |
| RP1 の clock | compatible `raspberrypi,rp1-clocks`（rpi5-fan.c と同じ経路） | `reg` | MIPI1_CFG の control 0x0d4、div_int 0x0d8、sel 0x0e0。MIPI0_CFG は 0x0c4/0x0c8/0x0d0 |

connector の対応（Linux の `ov5647-overlay.dts` の既定と `cam0` の override から）: 既定（`dtoverlay=ov5647`）= csi1 + i2c_csi_dsi（= i2c4）+ cam1_reg → 基板の **CAM/DISP 1**。`cam0` の override = csi0 + i2c6 + cam0_reg → **CAM/DISP 0**。rpicam は両方を順に（1 → 0）試す（D1）。

## 3. `/dev/camN` の ABI（`include/uapi/camera.h`）

### 3.1 node と権限

- cdev の名前 `cam0`…`cam3`（`CAM_DEVICES_MAX` 4）。
- devfs の mode: **0600 root**（`devfs.c` の `devfs_cdev_inode()` に `cam` の名前の規則を足す。今は該当しない名前が 0666 になるので、足さないと誰でも camera を開ける）。sessiond の seat が `/dev/gpu%u` と同じように `/dev/cam%u` を seat の user に渡し（`sessiond_seat_give`: 0600 uid、`sessiond_seat_restore`: root 0600）、login 画面の account には渡さない。これは `src/kern/devfs.c` と `userland/desktop/sessiond/seat.c` の小さな変更で、WS282 の p008 で Q1 に依頼する（他の WS の source）。D4。
- open: 何個でも open できる。問い合わせ（`GET_INFO`・`ENUM_FORMAT`・`ENUM_MODE`・`GET_FORMAT`・`GET_CONTROL_INFO`・`GET_CONTROL`・`GET_COUNTERS`）はどの open でもよい。**`REQUEST_BUFFERS` を呼んだ open が stream の所有者**になり、`SET_FORMAT`・buffer・`STREAM_ON/OFF`・`SET_CONTROL`・`mmap` は所有者だけ（他は `EBUSY`）。所有者が buffer を 0 個に戻すか close すると解放。所有者が stream 中に close したら枠組みが stop する。

### 3.2 ioctl の一覧（group `'C'`、`_IOWR` は `uapi/ioctl.h`）

全部の record は `version`（`CAM_ABI_VERSION` 1）と `size`（record の大きさ）で始まり、`reserved` は 0（gpu.h・audio.h と同じ作法）。pointer は持たない（fd と offset だけ）。

| 番号 | 名前 | record | 意味 |
| --- | --- | --- | --- |
| 1 | `CAM_GET_INFO` | `cam_info` | driver の名前（"rpicam"）、sensor の名前（"ov5647"）、connector の番号、capability の bit（`CAM_CAP_RAW`・`CAM_CAP_YUV`・`CAM_CAP_RGB`・`CAM_CAP_IMPORT_GPU`・`CAM_CAP_EXPORT`・`CAM_CAP_CONTROLS`）、`max_buffers`（8）、sensor の native の大きさ |
| 2 | `CAM_ENUM_FORMAT` | `cam_format_desc` | `index` 番目の format: `format`（§3.3）、`flags`（`CAM_FORMAT_SUPPORTED`: この camera で今使える）、`bits_per_sample`、`bytes_per_group`・`pixels_per_group`（packed の形） |
| 3 | `CAM_ENUM_MODE` | `cam_mode_desc` | sensor の `index` 番目の mode: 幅・高さ、`bayer_order`（`CAM_BAYER_RGGB/GRBG/GBRG/BGGR`）、`fps_num/fps_den`（固定の fps）、`native_format`（sensor が出す RAW の bit 数、OV5647 は RAW10）、crop（sensor の画素配列の中の位置と大きさ） |
| 4 | `CAM_GET_FORMAT` / 5 `CAM_SET_FORMAT` / 6 `CAM_TRY_FORMAT` | `cam_format` | 幅・高さ（mode と一致させる）、`format`（memory に書く形。RAW なら `RAW10P`（packed）か `RAW10`（16 bit unpacked）など）、`bayer_order`（out）、`stride_bytes`（out）、`image_bytes`（out）、`fps_num/den`（out）、`mode_index`（out）。stream 中・buffer 確保中は `EBUSY` |
| 7 | `CAM_REQUEST_BUFFERS` | `cam_request_buffers` | `count`（0 で解放、最大 `max_buffers`。確保できた数を返す）、`memory`（`CAM_MEMORY_DEVICE`: driver が確保、`mmap` で見る。`CAM_MEMORY_IMPORT`: 各 buffer を fd で渡す、§3.6）、`bytes`（out、1 buffer の大きさ） |
| 8 | `CAM_QUERY_BUFFER` | `cam_buffer` | `index` の buffer の `mmap_offset`（`index * image_bytes` を page に揃えた値）、`bytes`、`flags`（`CAM_BUFFER_QUEUED`・`CAM_BUFFER_DONE`・`CAM_BUFFER_MAPPED`） |
| 9 | `CAM_QUEUE_BUFFER` | `cam_buffer` | `index` の buffer を空き列に入れる。`CAM_MEMORY_IMPORT` なら `fd` も（最初の 1 回で取り込み、以後は同じ fd でも省略可） |
| 10 | `CAM_DEQUEUE_BUFFER` | `cam_buffer` | 完了した buffer を順に返す。無ければ待つ（`O_NONBLOCK` なら `EAGAIN`）。`sequence`、`timestamp_ns`、`used_bytes`、`metadata`（§3.5）、`flags` に `CAM_BUFFER_ERROR`（受信の誤りがあった frame） |
| 11 | `CAM_STREAM_ON` / 12 `CAM_STREAM_OFF` | なし（`_IO`） | ON には queue に 1 つ以上の buffer が要る（`EINVAL`）。OFF は queue の buffer を全部「未使用」に戻す（dequeue で返さない） |
| 13 | `CAM_GET_CONTROL_INFO` | `cam_control_info` | `id` の最小・最大・刻み・既定・`flags`（`CAM_CONTROL_READ_ONLY`・`CAM_CONTROL_UNSUPPORTED`） |
| 14 | `CAM_GET_CONTROL` / 15 `CAM_SET_CONTROL` | `cam_control` | `id`・`value`（int64）。stream 中でも設定できる（次の frame 以降に効く） |
| 16 | `CAM_GET_COUNTERS` | `cam_counters` | frame 数、落とした frame（buffer が無くて受けなかった数）、受信の誤り（overflow、discard の 4 種）、I2C の失敗 |
| 17 | `CAM_EXPORT_BUFFER` | `cam_buffer_export` | `index` の driver の buffer を fd（kernel handle）にする。`CAM_HANDLE_CLOEXEC/CLOFORK`。GPU 側の取り込みは別 WS（§3.6 の案 B）。今回は handle を作る所までで、取り込む側が無いので `CAM_CAP_EXPORT` は立てない |

`poll`: `POLLIN` は dequeue できる frame がある時。`POLLERR` は receiver が止まった時（§5）。

### 3.3 format の宣言

```c
/* RAW（Bayer）。packed は MIPI CSI-2 の並び（RAW10P: 4 画素の上位 8 bit が 4 byte、下位 2 bit ×4 が 5 byte 目）。unpacked は 16 bit little endian の下位に値。 */
#define CAM_FORMAT_RAW8		0x0101U
#define CAM_FORMAT_RAW10P	0x0102U
#define CAM_FORMAT_RAW10	0x0103U
#define CAM_FORMAT_RAW12P	0x0104U
#define CAM_FORMAT_RAW12	0x0105U
#define CAM_FORMAT_RAW16	0x0106U
/* YUV（宣言だけ。PiSP の後）。 */
#define CAM_FORMAT_YUYV		0x0201U
#define CAM_FORMAT_UYVY		0x0202U
#define CAM_FORMAT_NV12		0x0203U
#define CAM_FORMAT_YUV420P	0x0204U
/* RGB（宣言だけ）。 */
#define CAM_FORMAT_RGB565	0x0301U
#define CAM_FORMAT_RGB888	0x0302U
#define CAM_FORMAT_XRGB8888	0x0303U
```

- Pi 5 の RP1 で今回「使える」と答える物: `RAW8`・`RAW10P`・`RAW10`・`RAW12P`・`RAW12`（CSI2-DMA の NORMAL mode が packed を、REMAP mode が 16 bit を書く。sensor が出す DT で決まるので OV5647 は RAW10P と RAW10 の 2 つ）。`RAW16` は 16 bit の sensor 用の宣言。
- stride は `(width × bits / 8)` を 16 byte に揃える（RP1 の制限、Linux の `BPL_ALIGNMENT`）。`image_bytes = stride × height`。
- `bayer_order` は sensor の driver が flip の状態から答える（OV5647: 既定 BGGR、hflip で GBRG、vflip で GRBG、両方で RGGB。Linux の ov5647.c `ov5647_get_mbus_code` の表、値として引用）。

### 3.4 buffer の model

- `CAM_MEMORY_DEVICE`: 枠組みが `kern_pmem_alloc_limited(image_bytes を page に揃えた物, 4096, dma_physical_limit, 0)` で**物理的に連続**な run を buffer ごとに確保する（RP1 の CSI2-DMA は 1 frame に 1 つの address と stride しか取らず scatter-gather しない、Linux の `csi2_set_buffer`）。user は `mmap(fd, mmap_offset, bytes, PROT_READ)` で見る。mapping は `vm_device_create(..., VM_DEVICE_UNCACHED_RAM, PROT_READ)`（RP1 の DMA は cache を見ない: dtb に `dma-coherent` が無い。uncached なら CPU の読みが常に memory の中身。CPU の debayer は遅いが試験用）。kernel 側は direct map で触らない（触る時は `hal_dcache_invalidate_range` を frame の完了の後に。p005 で測って必要なら cached + invalidate に替える、R5）。
- `CAM_MEMORY_IMPORT`: §3.6。
- queue は 2 つの列（空き = user が QUEUE した物、完了 = DMA が書き終えた物）と「今 DMA が書いている 1 つ」「次に書く 1 つ（hardware に address を渡し済み）」。RP1 は address を frame start で latch する二重化の register（Linux の `csi2_set_buffer` の注記「ADDRESS0 は最後に書く、二重化の仕掛けを起こす」）なので、枠組みは **frame start の割り込みで次の buffer の address を渡す**（Linux の `cfe_prepare_next_job`）。空き列が空なら渡さず、hardware は次の frame を捨てる（discard の inactive に数えられる）。`QUEUE_BUFFER` で空き列に入った時に「次」が無ければその場で渡す（Linux の `cfe_buffer_queue`）。
- 最小の buffer 数は 2（連続の取り込みに 3 を勧める。Linux は 3 を強制するが、1 frame を撮るだけの試験もあるので強制しない）。

### 3.5 frame の metadata（`struct cam_frame_metadata`、`cam_buffer` に埋め込む）

| field | 意味 | 出所 |
| --- | --- | --- |
| `sequence` | frame start で数えた通し番号（捨てた frame も数える: sequence の飛びで落ちが分かる） | 枠組み |
| `timestamp_ns` | frame start の割り込みの時刻（`kern_rtc_read_counter` の単調 counter を ns に。input event と同じ源） | 枠組み |
| `exposure` | その frame の露光（sensor の単位、OV5647 は行の数） | 枠組みが「frame start の時点の control の値」を写す（sensor は次の frame 境界で反映するので ±1 frame の近似。`CAM_METADATA_APPROXIMATE` の flag を立てる） |
| `analogue_gain` | 同上 | 同上 |
| `flags` | `CAM_FRAME_ERROR`（overflow か discard が frame の間に出た）、`CAM_FRAME_INCOMPLETE`（frame end が来ずに次の frame start が来た） | receiver の割り込み |

### 3.6 GPU（Vulkan）へ複写なしに渡す

2 つの向きがある。**案 A（GPU が確保、camera が取り込む）を p006 で実装し、案 B（camera が export、GPU が取り込む）は ABI だけ宣言する**（D3）。

- 案 A `CAM_MEMORY_IMPORT`: app が Vulkan で memory を `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` の export 付きで確保し（`libvulkan/memory.c` は `GPU_ALLOCATION_EXPORT`/`GPU_RESOURCE_EXPORT` で fd を作る）、その fd を `CAM_QUEUE_BUFFER` で camera に渡す。枠組みは `handle_fd_get(fd, KERNEL_HANDLE_DRIVER)` で kernel handle を取り、GPU core に足す小さな API `drv_gpu_shared_backing(handle, &backing, &device)`（`gpu.c` の `gpu_shared_resource` の `device->ops->share->get_scanout_backing` を呼ぶだけ。bcm2711 は `render_backing` で page の列を返す）で物理 page の列を得て、**連続か・RP1 の DMA の届く範囲か・大きさが足りるか**を確かめ、連続でなければ `EXDEV`。V3D の buffer は `kern_pmem_alloc_limited` で確保した連続の run（`bcm2711/buffer.c`）なので Pi 5 では通る。handle は buffer が解放されるまで retain。
  - 利点: V3D 側に新しい import の種類が要らない（`gpu_import_ioctl` は別 device の handle を `EXDEV` で拒むので、camera の handle を V3D に取り込ませるには V3D と GPU core の変更が要る）。WS285 は Vulkan の memory をそのまま compute の storage buffer として読める。
  - 条件: `drv_gpu_shared_backing` の追加（`src/drivers/gpu/gpu.c` と `include/drivers/gpu/gpu.h`、WS031/WS141 の source。Q1 に依頼、10〜20 行）。cache: V3D の buffer は direct map で cached に触られ得るが、camera が書く間 app は触らない契約（Vulkan の `vkInvalidateMappedMemoryRanges` 相当は libvulkan 側）。
- 案 B `CAM_EXPORT_BUFFER`: driver の buffer を kernel handle（型 `KERNEL_HANDLE_DRIVER`、ops は枠組みの物、payload は buffer の物理 run と大きさ）にして fd で返す。V3D の `import_resource` が「外の物理 run」を受ける種類を足すまでは取り込めない。宣言のみ（`CAM_CAP_EXPORT` は 0）。

### 3.7 control の id

| id | 単位 | OV5647 | 備考 |
| --- | --- | --- | --- |
| `CAM_CONTROL_EXPOSURE` 1 | 行 | register 0x3500-0x3502（4 bit の小数は 0）、範囲 4〜(VTS−4)、既定 1000 | |
| `CAM_CONTROL_ANALOGUE_GAIN` 2 | sensor の単位 | 0x350a/0x350b、16 = 1.0x、範囲 16〜1023、既定 32 | |
| `CAM_CONTROL_HFLIP` 3 / `VFLIP` 4 | 0/1 | 0x3821/0x3820 の bit 1（OV5647 は hflip の register が反転の意味: Linux は `!ctrl->val` を書く）。`bayer_order` が変わる → stream 中は `EBUSY` | |
| `CAM_CONTROL_TEST_PATTERN` 5 | 0 off、1 color bars、2 squares、3 random | 0x503d | 配線の確認に使う |
| `CAM_CONTROL_AUTO_EXPOSURE` 6 / `AUTO_GAIN` 7 | 0/1 | 0x3503 の bit 0 / bit 1（0 で auto） | sensor の中の AEC/AGC。ISP が無いので demo の露光はこれに頼る（既定: 両方 on、D6） |
| `CAM_CONTROL_AUTO_WHITE_BALANCE` 8 | 0/1 | 0x5001 | RAW では digital gain の意味 |
| `CAM_CONTROL_VBLANK` 9 / `HBLANK` 10 | 行 / 画素 | 0x380e/0x380c（16 bit） | fps を下げる・露光の上限を上げる。p005 |

### 3.8 拡張の口（PiSP・他の板）

- YUV/RGB は `format` の値と `CAM_CAP_YUV/RGB` の bit で宣言済み。PiSP の front end は CSI2 の channel を `FE_STREAMING` mode にして FE の出力を memory に書く（Linux の `cfe_start_channel` の FE の分岐、`pisp_fe.c`）。枠組みは receiver の `prepare` に `output_format` を渡すだけなので、FE 付きの receiver（`rp1-cfe.c` の拡張）が YUV/RGB を「使える」と答えれば ABI は同じ。ISP の制御（AWB・色の表・lens shading）は別の ioctl の群を後で足す（番号 32 以降を予約）。
- 複数の camera: `rpicam` は connector ごとに `cam0`・`cam1`。
- 将来の USB camera（UVC）は receiver・sensor を 1 つの driver で兼ねる。

## 4. Pi 5 の rpicam の方式

### 4.1 起動の順序

`kern_platform_refresh_devices()`（割り込みが有効になった後。`rp1.c` の初期化と `drv_rp1_irq_init()` の後）から `drv_rpicam_probe(fdt_phys)`（`CONFIG_DRIVER_RPICAM`、rpi5 の config で y。既定は `Makefile` に `CONFIG_DRIVER_RPICAM ?= y`、`platform/arm64/vmunix.mk` で rpi5 の source に足す）。失敗は log に出して boot を続ける（他の RP1 の driver と同じ）。

1. **I2C**: i2c4（CAM1）の block を `drv_rp1_map` で map し、pin 40/41 を function 2（i2c4）・pull up に（`drv_rp1_gpio_set_function`・`drv_rp1_gpio_set_pull`。pinctrl-rp1.c の表: pin 40/41 の i2c4 は選択子 2、pin 38/39 の i2c6 は 3）。DesignWare の core を 100 kHz（dtb の `clock-frequency`）で用意し、`drv_i2c_bus_register` に登録する（`pci_*` は 0 を入れる。registry は bus の handle を返すので `drv_i2c_bus_find_acpi` は使わない）。入力 clock は RP1 の clk_sys 200 MHz（rp1.dtsi の `assigned-clock-rates`）。
2. **電源**: cam1_reg の GPIO 46 を `drv_rp1_gpio_set_output(46, 1)`（enable-active-high）。20 ms 待つ（`ov5647-overlay.dts` の `startup-delay-us = 20000`、OV5647 の「PWDN が low になってから 20 ms で SCCB が使える」）。
3. **sensor の検出**: OV5647 の driver の `power_on`（出力 enable の register 0x3000-0x3002 を 0x0f/0xff/0xe4、stream off の register で lane を LP-11 に）→ `identify`（0x0103 に 1 を書いて soft reset、0x300a/0x300b を読んで 0x56/0x47、0x0103 に 0）。ENODEV なら IMX219（0x10、0x0000 の 16 bit が 0x0219、p007 まで「見つけたが driver が無い」の log）。両方無ければ GPIO を戻して CAM0（i2c6、GPIO 34、csi0）で繰り返す。両方の connector に無ければ「camera 無し」の log で終わり。
4. **receiver**: 見つけた connector の csi の 4 つの block を map、clock を 25 MHz に（§4.3）、DPHY の版を読んで log（`VERSION` 0x000、Linux の `dphy_probe` と同じ読み）、割り込みを `drv_rp1_irq_establish(vector 48, LEVEL, handler, device)` で付ける。
5. `drv_cam_register` で `/dev/cam0`。sensor は `power_off`（SW standby）にして待機、stream on で起こす。

### 4.2 RP1 の CFE の block（csi1 の例）

Linux の `rp1_cfe/csi2.c`・`cfe.c`・`dphy.c` の値を引用する（§8）。

| block | base（RP1 の address） | 使う register |
| --- | --- | --- |
| CSI2-DMA | 0x40128000（0x100） | STATUS 0x000（FS/FE/FE_ACK/LE/LE_ACK の channel ごとの bit、OVERFLOW bit 20、DISCARD_* bit 21〜24。書いて clear）、DISCARDS_* 0x008〜0x014（amount 23:0、DT 29:24、VC 31:30）、IRQ_MASK 0x020、CTRL 0x024（EOP_IS_EOL bit 0）、channel 0 の CH_CTRL 0x028（DMA_EN bit 0、mode 2:1、FORCE bit 3、AUTO_ARM bit 4、VC 6:5、DT 12:7、IRQ_EN_FS bit 13、IRQ_EN_FE_ACK bit 15、PACK_LINE bit 29、PACK_BYTES bit 30）、ADDR0 0x02c（address >> 4 の下位 32 bit、**最後に書く**）、STRIDE 0x030（stride >> 4）、LENGTH 0x034（size >> 4）、DEBUG 0x038（frame 31:16、line 15:0）、ADDR1 0x03c（address >> 4 の上位）、FRAME_SIZE 0x040（height << 16 \| width）、COMP_CTRL 0x044 |
| DPHY / CSI host | 0x4012c000（0x100） | VERSION 0x000、N_LANES 0x004、RESETN 0x008、PHY_SHUTDOWNZ 0x040、PHY_RSTZ 0x044、PHY_RX 0x048、PHY_STOPSTATE 0x04c、PHY_TST_CTRL0 0x050（bit 0 testclr、bit 1 testclk）、PHY_TST_CTRL1 0x054（bit 16 testen、7:0 testdin、15:8 testdout） |
| MIPI CFG | 0x40138000（0x100） | CFG 0x004（SEL_CSI bit 0: この MIPI の block を DSI でなく CSI に）、INTE 0x02c（CSI_DMA bit 0、CSI_HOST bit 2、PISP_FE bit 4）、INTS 0x034（status） |
| PiSP FE | 0x4013c000（0x1000） | 今回は触らない（version の読みだけ log に、FE_VERSION 0x000） |

### 4.3 clock

`clk_mipi1_cfg`（RP1 の clock の controller、control 0x0d4、div_int 0x0d8、sel 0x0e0）は親が xosc（50 MHz）だけの aux の clock。Linux は 25 MHz を assign する。手順（`rpi5-fan.c` の `start_clock` と同じ型、Procedure referenced clk-rp1.c `rp1_clock_set_rate_and_parent`・`rp1_clock_set_parent`・`rp1_clock_on`）: ENABLE（bit 11）を落とす → div_int = 2 → control の AUXSRC（9:5）= 0（xosc は aux の 0 番）、SRC（mask は MIPI1 で bit 0、MIPI0 で無し）= AUX_SEL 1 → ENABLE を立てる。firmware が既に 25 MHz で回していればそのまま（log に前の値）。MIPI0 の方は 0x0c4/0x0c8/0x0d0。

### 4.4 stream の開始と停止（Procedure referenced cfe.c `cfe_start_streaming`・`cfe_start_channel`・`cfe_stop_streaming`、csi2.c `csi2_start_channel`・`csi2_open_rx`・`csi2_stop_channel`・`csi2_close_rx`、dphy.c `dphy_start`・`dphy_init`・`dphy_set_hsfreqrange`）

開始（枠組みの `STREAM_ON` → receiver の `prepare`/`start` → sensor の `set_mode`/`start`）:

1. receiver `prepare`: channel 0 の CH_CTRL を 0、DEBUG を 0、STATUS の channel 0 の bit を clear。CH_CTRL = DMA_EN \| IRQ_EN_FS \| IRQ_EN_FE_ACK \| PACK_LINE \| mode（RAW10P なら NORMAL 0、RAW10 なら REMAP 1）\| VC 0 \| DT（RAW10 = 0x2b、RAW8 = 0x2a、RAW12 = 0x2c）、FRAME_SIZE = height << 16 \| width。AUTO_ARM は立てない（frame ごとに address を渡す）。
2. 最初の buffer: LENGTH・STRIDE・ADDR1・ADDR0 の順（bus address = 物理 + RP1 の DMA の offset。`drv_rp1_dma_window` の `bus_offset`、Pi 5 は 0x10_0000_0000）。
3. MIPICFG の CFG = SEL_CSI、INTE = CSI_DMA（FE は使わない）。
4. DPHY: rate（Mbps）= sensor の link 周波数 × 2 / 1e6（OV5647 の 2592x1944・1080p・1296x972 は 218.5 MHz → 437、640x480 は 208.333 MHz → 417）。CSI2 の IRQ_MASK = 0（誤りの割り込みは STATUS で見る。Linux は `track_csi2_errors` の時だけ mask を開ける。zedBSD は **開けて数える**: OVERFLOW と DISCARD の 4 bit。R2 の診断のため）。`dphy_start`: RESETN = 0、N_LANES = lanes − 1（= 1）、PHY_RSTZ = 0、PHY_SHUTDOWNZ = 0、testclk = 1、testen = 0、testclr = 1 → 15 µs → testclr = 0 → 15 µs、test の transaction で code 0x44（HS_RX_CTRL_LANE0）に hsfreqrange の値 << 1（437/417 Mbps は表の「≤ 449」の行 0b000110）、5 µs、PHY_SHUTDOWNZ = 1、5 µs、PHY_RSTZ = 1、RESETN = 0xffffffff、10 µs。CSI2 の CTRL = EOP_IS_EOL（multipacket-line でないので）。
5. sensor `set_mode` + `start`（OV5647 の `ov5647_stream_on` の順: 共通の表 → mode の表 → VC 0（0x4814 の 7:6）→ 0x0100 が 1 でなければ 1 → control の値を書き直す → MIPI_CTRL00（0x4800）= BUS_IDLE（bit 2）\| clock lane の gate（bit 5）\| line sync（bit 4）（非連続 clock）→ 0x4202 = 0 → 0x300d = 0）。

停止: sensor `stop`（0x4800 = gate \| idle \| clock lane disable、0x4202 = 0x0f、0x300d = 1）→ receiver `stop`（CH_CTRL = FORCE、ADDR0 に 0 を 2 回書く（Linux の注記「HW がそれを要る」）、IRQ_MASK = 0、INTE = 0。DPHY は reset しない: Linux の `dphy_stop` の注記「途中の packet で host を reset すると IDI が壊れる」）→ 枠組みが「今」「次」の buffer を未使用に戻す。

### 4.5 割り込み（Procedure referenced cfe.c `cfe_isr`・`cfe_sof_isr_handler`・`cfe_eof_isr_handler`、csi2.c `csi2_isr`）

RP1 の vector 48 は level（dtb の `0x30 0x04`）。`drv_rp1_irq_establish` の handler（割り込み context、sleep しない、枠組みの spinlock の中）:

1. MIPICFG の INTS を読み、CSI_DMA の bit が無ければ返る（同じ vector を DSI1 と共有するが DSI は使わない）。
2. CSI2 の STATUS を読んで同じ値を書き戻す（clear）。channel 0 の FS と FE_ACK を見る。OVERFLOW・DISCARD の bit は counters に足し、`CAM_FRAME_ERROR` を今の frame に付ける。DISCARD の register は読んで 0 を書く（amount・DT・VC を log の統計に）。
3. 同時に来た場合の順序（Linux と同じ 3 通り）: FE_ACK があり、FS もあって「FS 済み」の印が無ければ FS を先に処理してから FE。FS があって前の frame の FE が来ていなければ（FS 済み・FE 未）、前の frame を `INCOMPLETE` で先に閉じる。
4. FS: 「今」←「次」、sequence++、時刻、control の値を metadata に写す。空き列から 1 つ取って `set_buffer`（次の frame の宛先）。無ければ「次」は空（hardware は次の frame を捨てる）。
5. FE_ACK: 「今」を完了列へ、`waitq_wake_all`、`poll_notify`。

level の vector は `rp1-irq.c` が handler の後に再度 arm する（slot->level）。handler は STATUS を clear して返るので線は下がる。

### 4.6 OV5647 の driver（`src/drivers/camera/sensor/ov5647.c`）

- I2C: address 0x36、register は 16 bit big endian の address + 8 bit の値（write は 3 byte、read は address 2 byte を書いて repeated start で 1 byte 読む）。`drv_i2c_transfer(bus, 0x36, 100000, ...)`。
- 表（Values referenced ov5647.c `ov5647_common_regs`、`ov5647_2592x1944_10bpp`、`ov5647_1080p30_10bpp`、`ov5647_2x2binned_10bpp`、`ov5647_640x480_10bpp`、`sensor_oe_enable_regs`、`sensor_oe_disable_regs`、`ov5647_modes`）を zedBSD の名前で持つ。値は sensor の register の事実。
- mode の表（`ENUM_MODE` の順）:

| index | 幅×高さ | 画素 clock | link | HTS | VTS | fps（計算） | 備考 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 1296×972 | 87.5 MHz | 218.5 MHz | 1896 | 1435 | 32.2 | 2x2 binning、全画角。**既定** |
| 1 | 640×480 | 55 MHz | 208.333 MHz | 1852 | 504 | 58.9 | 2x2 binning + 間引き |
| 2 | 1920×1080 | 87.5 MHz | 218.5 MHz | 2416 | 1104 | 32.8 | 中央の切り出し |
| 3 | 2592×1944 | 87.5 MHz | 218.5 MHz | 2844 | 1968 | 15.6 | 全画素 |

- 全部 RAW10、BGGR（flip なし）、2 lane、非連続 clock。
- RAW10 packed の stride: 1296 → 1620 → 1632（16 の倍数）、frame 1,586,304 byte。640 → 800、384,000 byte。2592 → 3240 → 3248、6,314,112 byte。unpacked 16 bit は幅 × 2。

### 4.7 DesignWare I2C（RP1）

`lpss-i2c.c` の core の部分（`lpss_core_start`・`lpss_transfer`・`lpss_run`・`lpss_set_enabled`・`lpss_count`: 約 300 行。polling で FIFO を回し、1 tick 眠る）は PCI と LPSS の reset を除けば RP1 の DesignWare（`snps,designware-i2c`、COMP_TYPE 0x44570140）にそのまま使える。入力 clock 200 MHz、SCL の count は同じ式。p003 の選択（D2）:
- 案 (a) `src/drivers/i2c/dw-i2c-core.c`（+ `include/drivers/i2c/dw-i2c-core.h`）に core を抜き、`lpss-i2c.c` は PCI の発見・電源・reset の薄い層に。x86 の i2c-hid（5330 の touch pad）の回帰を T1 に頼む。WS159 の source を触る。
- 案 (b) `src/drivers/platform/rpi5/rp1-i2c.c` に同じ手順を独立に書く（約 350 行の重複、x86 は触らない）。
- 勧め: (a)。重複は後の監査（規約の全文）で 2 回直す事になる。

## 5. 失敗と回復・並行性・資源

- **状態機械**（枠組み、`enum cam_state`）: `IDLE`（format 可）→ `BUFFERS`（REQUEST_BUFFERS 済み、format 不可）→ `STREAMING`（STREAM_ON）→ `STOPPING`（割り込みを待って片付け）→ `BUFFERS`。`FAULT`（receiver が `drv_cam_receiver_error` で致命を告げた、または sensor の I2C が連続で失敗）: `poll` が `POLLERR`、`DEQUEUE` が `EIO`、`STREAM_OFF` で `BUFFERS` に戻る。
- **lock**: `device->lock`（spinlock、rank DEVICE）が queue・状態・counters を守り、割り込み handler と ioctl の両方が取る（`spin_lock_irqsave`）。`device->mutex`（mutex、rank DEVICE）が ioctl 同士（format・buffer の確保・stream の開始/停止・control）を直列にする。順序は mutex → spinlock。**spinlock を持ったまま眠らない**（WS278 の GEM の deadlock の教訓: I2C の transfer は tick を眠るので mutex の下、spinlock の外で呼ぶ）。I2C の registry は bus ごとの mutex。
- **所有権と寿命**: buffer（`struct cam_buffer_slot`）は枠組みが所有し、`REQUEST_BUFFERS 0`・close で解放。mmap 中は `vm_device_mapping` が slot を retain する（release callback で数を減らし、mapping が残る間は物理 run を解放しない: 「REQUEST_BUFFERS 0 は mmap が残っていれば EBUSY」）。import した GPU の handle は slot が retain し、解放で `handle_put`。DMA 中の buffer は hardware が address を持つので、stop が終わるまで解放しない（`STOPPING` で FE_ACK か 100 ms の timeout を待つ）。
- **sensor の失敗**: I2C の NACK（ENXIO）は検出で「無し」、stream 中は counters に数え、3 回連続で `FAULT`。
- **受信の失敗**: OVERFLOW（memory が間に合わない）は frame に ERROR を付けて続ける。DISCARD の INACTIVE は「buffer が無くて捨てた」の統計（正常）。UNMATCHED（DT/VC が合わない）が frame ごとに出るなら sensor の設定の誤り → log（R1）。
- **割り込みが来ない**（DPHY の lock の失敗など）: STREAM_ON の後 500 ms で最初の FS が無ければ log に DPHY の STOPSTATE と CSI2 の DEBUG を出し、`FAULT`（診断に要る値、R1）。
- **close**: stream 中の close は stop → buffer 解放。mapping が残っていれば物理 run は mapping の release まで残る。
- **shutdown**（電源断・reboot）: 枠組みは何もしない（RP1 ごと落ちる）。将来の suspend は Future Work。

## 6. ユーザーの判断が要る点

| # | 問い | 選択肢 | 勧め |
| --- | --- | --- | --- |
| D1 | camera はどちらの connector に挿してあるか | 基板の印字 CAM/DISP 1（Linux の overlay の既定）/ CAM/DISP 0 | driver は 1 → 0 の順に探すので**どちらでも動く**。分かれば試験の時間が減る（答えは任意） |
| D2 | RP1 の DesignWare I2C の code | (a) `lpss-i2c.c` から core を抜いて共有（x86 の i2c-hid の回帰を T1 に）/ (b) RP1 用に別に書く（重複） | (a) |
| D3 | GPU との buffer の共有の向き | A: GPU が確保・camera が取り込む（GPU core に `drv_gpu_shared_backing` を足す）/ B: camera が export・V3D が取り込む（V3D の import の新しい種類） | A を p006 で。B は ABI の宣言だけ |
| D4 | `/dev/camN` の権限 | 0600 root + sessiond が seat の user に渡す（`/dev/gpu` と同じ）/ 0666 | 前者（camera は privacy の device） |
| D5 | IMX219（v2、8 MP）の driver | 今回 code を書く（hardware 無しで未試験）/ module が手に入ってから（p007 を保留） | 後者。chip ID の読みだけ p003 に入れ、「見つけたが driver 無し」を log に出す |
| D6 | 既定の mode と露光 | 1296x972 RAW10 約 32 fps、sensor の AEC/AGC を on / 640x480 59 fps / 露光は手動 | 1296x972、AEC/AGC on（ISP が無いので sensor の自動露光が唯一の自動。YOLOX-Nano の入力 416x416 へは GPU で縮小） |
| D7 | RAW10 の既定の memory の形 | packed（RAW10P、5 byte/4 画素、小さい）/ unpacked 16 bit（RAW10、shader で読みやすい） | 両方対応し、既定は unpacked 16 bit（WS285 の compute が 16 bit の配列を読める。`camcap` は両方試す） |

## 7. 試験の計画

### 7.1 host の試験（p002、`src/drivers/camera/tests/host-cam-test.c`）

- 偽の receiver と sensor（関数表を差し替え、`set_buffer` の address を記録し、試験の thread から `drv_cam_frame_start/end` を呼ぶ）で、枠組みの状態機械・queue・metadata・所有権（2 つ目の open の EBUSY）・`REQUEST_BUFFERS 0` と mmap の残り・STREAM_OFF での未使用への戻し・FS/FE の同時の 3 通り・buffer 枯渇で sequence が飛ぶ事・FAULT の遷移・ioctl の record の version/size/reserved の検査、を 1 本の host の program で通す。cdev・copyin/copyout は host では直接関数を呼ぶ（既存の host の試験の作法: `platform/arm64/vmunix.mk` の `rpi4-fdt-host-test` の型で `$(BUILD)/tests/` に build）。
- OV5647 の driver の host の試験: 偽の I2C（register の map を持つ）で、検出・mode の表の書き込みの順・control の register の値（露光 1000 → 0x3500-0x3502 = 0x00 0x3e 0x80、gain 32 → 0x350a/b = 0x00 0x20）・flip と Bayer の並び。
- RP1 の receiver の register の手順は host では偽の MMIO（配列）に書いた順序を照合する試験（clock の 3 段、DPHY の transaction の列、channel の設定、ADDR0 が最後）。
- build: `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/ws282 vmunix`（warning 0）と amd64 の ci の config（枠組みは rpi5 の config だけに入るので amd64 は影響なし。lpss-i2c を触る (a) なら amd64 の build も）。

### 7.2 QEMU

- raspi5 の machine は無く、camera の model も無い。QEMU の試験は **無し**。(a) で `lpss-i2c.c` を触った時だけ T1 に x86（5330 の touch pad、i2c-hid）の回帰を頼む。

### 7.3 Pi 5 の実機（`kei@10.0.30.5`、kernel の差し替えは [WS278 の手順](../ws278/tests/rpi5-hw-test.md)の「kernel だけの差し替え」）

| 段 | 見る物 | 期待 |
| --- | --- | --- |
| p003 | `dmesg` の `rpicam:` | `rpicam: i2c4 at 1f00080000 (RP1 0x40080000), 200 MHz`、`rpicam: CAM1 power on (gpio 46)`、`ov5647: chip id 5647 on i2c4 address 36`（または CAM0 の行、または `rpicam: no sensor on CAM1 (ENXIO) or CAM0`）、`/dev/cam0` が `ls -l /dev` に 0600 root で出る |
| p004 | `camcap -o /tmp/f.ppm`（既定 mode、1 frame）→ `scp` → host で `build/review/ws282/` に PNG を置いてユーザーに見せる | `dmesg` に `rp1-cfe: csi1 dphy host v1.xx, clock 25000000 Hz, vector 48 on line N`、`camcap` が `frame 0 seq=N ts=… 1296x972 RAW10 stride 2592` を出し、PPM（簡単な CPU の debayer: 2x2 の平均、black level 16/1023 を引く、gain 2 倍）が**見た目で絵になる**（試験の模様 `--test-pattern 1` の color bars が縦縞として出る事を先に確かめる） |
| p004 連続 | `camcap -n 300 -o /dev/null` | 300 frame、sequence の飛びが 0（buffer 4 で）、counters の overflow 0、所要時間から fps ≈ 32 |
| p005 | `camcap --exposure 200 --gain 16 -o a.ppm`・`--exposure 1500 --gain 64 -o b.ppm`、`--hflip`、`--mode 1`（640x480）と `--mode 3`（2592x1944）、stop/start を 100 回、buffer 1 個で枯渇、open 中に 2 つ目の open | 露光で明るさが変わる、flip で絵が反転し Bayer の並びが変わる、各 mode の絵、100 回で落ちない・leak しない（`/dev/system` の資源の数）、2 つ目の open の `EBUSY` |
| p006 | `camvk`（Vulkan で memory を export して `CAM_MEMORY_IMPORT`、compute shader で 16 bit RAW を 8 bit の gray に縮めて読み戻し PGM に） | PGM が camcap の物と同じ絵。WS279 の V3D が main にある事が前提 |

### 7.4 試験の道具

- `userland/tests/camcap/`（`ZEDBSD_USERLAND_PACKAGE`、tests の群、rpi5 の image に入れる）: `/dev/cam0` を開き、mode・format・露光・gain・flip・test pattern を引数で、N frame 取って最後の 1 枚（または全部）を PGM（RAW のまま 16 bit → 8 bit）か PPM（CPU の debayer）に書く。counters も出す。回帰に残す（master.md の Tools に登録、p008）。
- `userland/tests/camvk/`（p006）。

## 8. 引用する値と手順の出典（code には block ごとに書く。ここは一覧）

| 出典（rpi-6.12.y） | 何を | どの block |
| --- | --- | --- |
| `drivers/media/platform/raspberrypi/rp1_cfe/csi2.c`（CSI2_* の define、`csi2_set_buffer`、`csi2_start_channel`、`csi2_stop_channel`、`csi2_open_rx`、`csi2_isr`） | CSI2-DMA の register の offset と bit、channel の設定と停止の手順、二重化の address の書き順 | rp1-cfe.c の receiver |
| `.../rp1_cfe/csi2.h`（`enum csi2_mode`、`enum csi2_compression_mode`） | mode の番号 | 同上 |
| `.../rp1_cfe/cfe.c`（MIPICFG_* の define、`cfe_start_streaming`、`cfe_stop_streaming`、`cfe_isr`、`cfe_sof_isr_handler`、`cfe_eof_isr_handler`、`cfe_prepare_next_job`、`cfe_buffer_queue`、`cfe_calc_format_size_bpl`、`BPL_ALIGNMENT`、`sensor_link_rate`） | MIPI CFG の register、開始と停止の順、割り込みの FS/FE の順序の扱い、次の buffer の渡し方、stride の揃え、DPHY の rate の計算（link × 2） | rp1-cfe.c と cam.c の queue |
| `.../rp1_cfe/cfe_fmts.h`（`formats`） | DT と memory の形の対応（RAW8 0x2a・RAW10 0x2b・RAW12 0x2c、16 bit の remap） | cam.c の format の表 |
| `.../rp1_cfe/dphy.c`（define、`dphy_transaction`、`dphy_set_hsfreqrange` の表、`dphy_init`、`dphy_start`、`dphy_stop` の注記） | DPHY の register、test の transaction、hsfreqrange の表、初期化の順と待ち | rp1-cfe.c の DPHY |
| `.../rp1_cfe/pisp_fe.c`（`FE_VERSION`） | version の読みだけ | log |
| `drivers/media/i2c/ov5647.c`（define、`ov5647_common_regs`、4 つの mode の表、`ov5647_modes`、`ov5647_link_freqs`、`sensor_oe_*_regs`、`ov5647_power_on`、`ov5647_stream_on/off`、`ov5647_set_mode`、`ov5647_detect`、`ov5647_s_*`、`ov5647_get_mbus_code`、`ov5647_init_controls` の範囲） | register の address と値、mode の表、電源・検出・stream の手順、control の register と範囲、flip と Bayer | ov5647.c |
| `drivers/media/i2c/imx219.c`（`IMX219_REG_CHIP_ID`、`IMX219_CHIP_ID`、address） | 検出だけ（p003） | rpicam.c |
| `drivers/clk/clk-rp1.c`（`CLK_MIPI0_CFG_*`、`CLK_MIPI1_CFG_*`、`CLK_CTRL_*`、`AUX_SEL`、`rp1_clock_on`、`rp1_clock_set_parent`、`rp1_clock_set_rate_and_parent`、`clk_mipi0_cfg`/`clk_mipi1_cfg` の表）、`include/dt-bindings/clock/rp1.h` | clock の register と手順 | rp1-cfe.c の clock |
| `drivers/pinctrl/pinctrl-rp1.c`（`rp1_gpio_pin_funcs` の pin 34・38〜41・46） | function の選択子 | rpicam.c（rp1-gpio.c の API で設定） |
| `drivers/i2c/busses/i2c-designware-core.h` | register（zedBSD の lpss-i2c.c と同じ） | dw-i2c-core.c（既に lpss-i2c.c が引用済み） |
| `arch/arm64/boot/dts/broadcom/rp1.dtsi`・`bcm2712-rpi-5-b.dts`・overlay の dts | address・割り込み・clock・pin・regulator の GPIO・lane・非連続 clock・電源の待ち | 設計の表（code は dtb を読む） |
| MIPI CSI-2 の仕様 | DT の番号、RAW10 の packed の並び | cam.c の format の表（公開の仕様の事実） |

## 9. 危険と確かめる点

- R1 **DPHY の timing**: hsfreqrange の選択（437 Mbps）と非連続 clock の lane の起き方は Linux の値をそのまま使うが、zedBSD の待ちの実装（`kern_usleep_range`）が busy wait である事以外は同じ。FS が来ない時の診断（PHY_STOPSTATE、CSI2 の DEBUG、discard の種類）を最初から入れる。
- R2 **RP1 の CFE の文書は Linux の driver だけ**: register の意味は driver の使い方から推し量るしかない。使う register を Linux が使う範囲に限り、違う使い方（例: AUTO_ARM、compression）はしない。
- R3 **firmware の camera の扱い**: `camera_auto_detect` を config.txt に書かない（既定 0）ので firmware は I2C を触らない前提。dtb に sensor の node は無く、`rpicam` が自分で探す。もし firmware が regulator の GPIO を触っていても `rpicam` が改めて設定する。
- R4 **ISP 無しの RAW の質**: black level（OV5647 は約 16/1023）、lens shading、色の補正が無い。demo の検出には sensor の AEC/AGC と簡単な WB（GPU の shader で固定の gain）で足りる見込み。精度は WS285 で見る。
- R5 **cache**: RP1 の DMA は cache を見ない。user の mapping は uncached（遅い読み）。GPU の buffer（案 A）は V3D 側の cache の扱いに乗る。p005 で uncached の読みの速さを測り、必要なら cached + `hal_dcache_invalidate_range` に替える。
- R6 **MIP の線**: RP1 の vector 1 本を足す（USB 2・GEM 1・PWM の後）。64 本あるので足りる。
- R7 **I2C の pin と DSI の共用**: CAM/DISP の connector は DSI と I2C を共用する。display を挿していない前提。
- R8 **sensor の種類**: ユーザーの「5MP、v2?」が IMX219（8 MP）なら chip ID でそう出る → D5 を繰り上げる。OV5647 の互換の module（第三者製）は同じ ID。
- R9 **連続した物理 memory**: 2592x1944 の 16 bit は 10 MB の連続した run。`kern_pmem_alloc_limited` が断片化で失敗し得る（boot の早い時期に確保する手も。REQUEST_BUFFERS で失敗したら `ENOMEM` で buffer の数を減らして返す）。
- R10 **license**: sensor の register の表は Linux の GPL の file にしか無い（OV5647 の datasheet は NDA）。Guardrail の規則（値はデータとして出典を明示、名前と構造は独自）で使う。p009 の監査で類似を確かめる。

## 10. 選ばなかった案と理由

- **V4L2 互換の ABI**（`VIDIOC_*`）: pointer を含む record・多数の type・Linux の header の互換が要り、zedBSD の既存の device（gpu・audio・evdev）が独自の fixed-width の ABI を取っているのと合わない。libcamera を移植する計画も無い。独自の小さな ABI にした。
- **sensor と receiver を 1 つの driver に**: Pi 5 の CFE は sensor を選ばず、sensor は板を選ばないので分けた。
- **read(2) で frame を読む**: 1 frame 1.5〜10 MB の複写が要り、GPU へ渡す道が無い。mmap と import にした（read は非対応: `EOPNOTSUPP`）。
- **PiSP の front end を最初から使う**（統計・縮小が取れる）: config の構造体（`pisp_fe_config`、約 1.5 KB）と手順が大きく、RAW だけなら CSI2-DMA で足りる。後の WS。
- **camera の buffer を V3D に取り込む（案 B）を先に**: GPU core と V3D の両方に新しい import の種類が要り、WS279・WS031 の source を広く触る。案 A は GPU core に 1 関数で済む。
- **firmware の overlay（`dtoverlay=ov5647`）で sensor の node を dtb に入れる**: overlay の dtbo を FAT に置き、config.txt を sensor ごとに書く事になる。chip ID で探す方が「挿せば動く」。
- **I2C を割り込みで**: lpss-i2c.c の polling（tick を眠る）で 100 kHz の register の書き込み（1 mode 約 70 個、1 個 0.3 ms）は 20〜30 ms で済む。stream の開始の時だけなので polling で足りる。

## 11. 実装の Phase

| Phase | 内容 | 依存 | 見積もり | 受け入れの条件 |
| --- | --- | --- | --- | --- |
| p002 | 枠組み: `include/uapi/camera.h`、`include/drivers/camera/cam.h`、`src/drivers/camera/cam.c`（cdev、所有、format の交渉、buffer の確保と mmap、queue、割り込みからの完了、poll、control の中継、counters、状態機械）、host の試験 | p001 | 5〜7 LW | host の試験が全部 PASS、rpi5 の config で warning 0、規約の全文（自分で読み直す） |
| p003 | RP1 の I2C（D2 の (a): `dw-i2c-core.c` に抜く + `rp1-i2c.c`）、`rpicam.c`（dtb から csi・i2c・regulator、電源の GPIO、sensor の検出、両 connector）、`ov5647.c` の検出と電源の部分、IMX219 の ID の読み。板で `ov5647: chip id 5647` | p002（登録の形だけ。並行可） | 3〜4 LW | 板の `dmesg` に chip ID の行、`/dev/cam0` が出る（T1 でなく Q1 の板の操作、または担当が SSH で kernel を入れ替える許可を得て） |
| p004 | `rp1-cfe.c`（clock、DPHY、CSI2 channel、割り込み）、`ov5647.c` の mode と stream、`camcap`。板で最初の RAW の frame、PGM/PPM の絵 | p003 | 6〜8 LW | 板で test pattern の縞と実景の PPM がユーザーの目視で絵になる、300 frame 連続で落ちなし |
| p005 | control（露光・gain・flip・test pattern・AEC/AGC・blank）、metadata、`O_NONBLOCK`・poll、stop/start と枯渇の堅牢化、FAULT の診断、uncached の読みの計測 | p004 | 3〜4 LW | §7.3 の p005 の表 |
| p006 | `CAM_MEMORY_IMPORT`（GPU core の `drv_gpu_shared_backing` は Q1 に依頼）、`camvk` | p004、WS279 の V3D が main に | 3〜4 LW | `camvk` の PGM が `camcap` と一致 |
| p007 | IMX219（D5。module がある時だけ） | p004 | 2〜3 LW | 板で IMX219 の絵 |
| p008 | `docs/reference/camera.md`（ABI の文書、docs/README の一覧に追加）、devfs の 0600、sessiond の seat（Q1 に依頼）、`camcap` を Tools に登録 | p005 | 1〜2 LW | 文書、seat の切り替えで `/dev/cam0` の所有が変わる |
| p009 | 規約の全文の見直し（p002〜p006 の C）と license の類似の監査 | p006 | 2 LW | 違反 0、監査の記録 |

合計 25〜34 LW（p007 を除けば 23〜31）。板の試験は p003・p004・p005・p006 の末に 1 回ずつ（4 回）。
