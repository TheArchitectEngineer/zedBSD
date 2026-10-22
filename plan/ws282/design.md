# WS282 camera の driver の枠組み（/dev/camN）と USB Video Class の driver「uvc」の設計

- 版 2（2026-10-23 P3、ws282-p001）。範囲の変更（2026-10-23 ユーザー、Q1 経由）:「USB Video classを優先しましょう。CSIは今回はやりません。/dev/camNのAPIはそのまま使います。uvcドライバを実装します。カメラは今から購入します。まずは手元にあるHDMIキャプチャでもデバッグには使えそうですか？」→ この版は **/dev/camN の枠組み + UVC の driver「uvc」**。版 1 の Pi 5 の CSI（rpicam・RP1 の CFE・DPHY・OV5647・RP1 の I2C）は §13「今回はやらない」と付録 A に移した。版 1 の [review（2026-10-23）](phase001/review-20261023.md)の指摘のうち枠組み・buffer・cache・所有・devfs/sessiond に関わる物は全部反映した（§12 の表）。ユーザーの判断 D1〜D7 は推しで決定済み（2026-10-23「decisionは推しでいいです」）で、CSI に固有の物は付録に記録した。
- HDMI capture への答え（要約、詳細は §6 D8）: **使える**。手元の安い HDMI capture（MS2109 など）は UVC 1.0/1.1 の device で YUYV と MJPEG を出すので、uvc の最初の試験の device になる。制約: 今の xHCI の isochronous は 1 interval 1024 byte まで（high-bandwidth と SuperSpeed は未対応、§4.7）なので、拡張（p005）までは MJPEG か小さな YUYV（640x480 で約 13 fps）に限られる。
- 資料（全部読んだ上で書いた）:
  - zedBSD: `include/drivers/usb/usb.h`（class の driver の契約: URB、isochronous の packet、claim、alternate）、`src/drivers/usb/usb.c`（`drv_usb_interface_set_alternate`、zero-bandwidth の endpoint、URB の確保）、`src/drivers/usb/usb-bt.c`（SCO: isochronous の URB の作法、worker の thread、停止と drain）、`src/drivers/usb/usb-hid.c`（attach・detach・quiesce・worker の join）、`src/drivers/pci/pci-xhci.c`（`xhci_isoch_enqueue`・`xhci_isoch_finish`・`xhci_request_alloc`: staging への複写、packet の上限、64 KiB の境界）、`src/drivers/pci/pci-xhci-isoch.c`・`.h`（FIFO 4 要求、32 packet）、`include/drivers/pci/pci-xhci-control.h`（endpoint context の encoder: isochronous は FS/HS の 1 transaction だけ、SS と high-bandwidth は拒む）、`include/kern/cdev.h`（`cdev_register_managed`・finalizer）、`src/drivers/generic/hidraw.c`（hotplug する cdev の作法）、`src/kern/vm/device.c`・`include/kern/vm-device.h`、`include/kern/pmem.h`・`src/kern/pmem.c`、`src/drivers/audio/audio.c`（cached の run を user に map する前例）、`src/kern/devfs.c`（node の mode）、`userland/desktop/sessiond/seat.c`、`userland/desktop/libjpeg-compat/`（MJPEG の展開）、`userland/tests/audio-loop/Makefile`（試験の program の package）、`platform/{amd64,arm64,riscv64}/vmunix.mk`・`Makefile`（USB の class driver の build）、`config/ci/config-rpi4.mk`・`config-rpi5.mk`、GPU 側は版 1 と同じ（`gpu.c`・`bcm2711/{share.c,render-device.c,buffer.c,vk/vulkan-memory.c}`・`gpu-share.h`・`gpu-scanout.h`、§3.6）。
  - 仕様（公開、USB-IF）: USB Video Class 1.1（descriptor の subtype、PROBE/COMMIT の control、payload header）、USB 2.0（isochronous の high-bandwidth の wMaxPacketSize の bit 12:11）、USB 3.x（SuperSpeed endpoint companion）、xHCI 1.2（isochronous の TRB の TBC/TLBPC、endpoint context の Mult・Max Burst・Max ESIT payload）。GPL の code は参照していない（UVC の値は仕様の事実）。
  - 版 1 の資料（Linux rpi-6.12.y の rp1_cfe・ov5647 など、firmware の dtb）は付録 A に。
- license: zedBSD の code は Zlib。UVC・USB・xHCI の仕様の定数（descriptor の subtype、GUID、request の番号、header の bit、TRB の field）は公開の仕様の事実として使い、code の block の前に「Value(s) referenced: USB Video Class 1.1 §x.y」と書く。Linux の uvcvideo の quirk の表を後で使う時は [Guardrail の「MIT・GPL の出典のデータの引用」](../guardrail.md)に従い「Value(s) referenced: linux drivers/media/usb/uvc/uvc_driver.c (uvc_ids)」と書く（p006 で該当があれば）。

## 0. 要約

- `/dev/camN` は zedBSD 独自の fixed-width の ioctl の ABI（group `'C'`、`include/uapi/camera.h`）。format は RAW（Bayer）・YUV（YUYV・UYVY・NV12）・RGB・圧縮（MJPEG）を宣言し、**今回は uvc が YUYV と MJPEG を実装**（NV12 と UYVY は device が出せば通る。RAW は枠組みの表に残し、CSI の WS が使う）。mode は (format, 幅, 高さ, frame の間隔) の組。buffer は枠組みが確保して `mmap` する形（今回: CPU が書く cached の連続 run）と、GPU が確保して export した fd を camera が取り込む形（後の Phase p008、§3.6）。frame ごとに sequence・時刻・`used_bytes`（MJPEG は可変長）・device の PTS/SCR。control は今回無し（ABI は用意、uvc は全部 UNSUPPORTED、後の p010）。
- kernel の中は 2 層: **枠組み**（`src/drivers/camera/cam.c`、node・所有・buffer・queue・状態機械・ABI・hotplug）と **source の driver**（枠組みに `struct drv_cam_source_ops` でつながる。今回は `src/drivers/usb/usb-uvc*.c`、将来は CSI の receiver + sensor）。source は「空いている slot を取って書いて返す」API（§2.1）で frame を渡す。
- uvc: VideoControl の interface に bind し、VideoStreaming の interface を claim。descriptor（VC header、VS input header、format（uncompressed・MJPEG・frame based は宣言だけ）、frame（size・間隔））を解析して log に dump。`SET_FORMAT` = PROBE/COMMIT の交渉。transport は isochronous（alternate の帯域の選択、`pci-xhci-isoch` の 32 packet × 4 URB、worker の thread）と bulk（短い packet で区切る payload）。payload header（FID・EOF・ERR・PTS・SCR）で frame を組み、MJPEG はそのまま渡す（userland の libjpeg-compat が展開）。対象の板: x86（xHCI）・Pi 5（RP1 の xHCI）・Pi 4（VL805）・VF2（PCIe の xHCI）。
- **xHCI の制約（§4.7）**: 今の endpoint context の encoder は isochronous を FS/HS の 1 transaction（≤ 1024 byte/interval）に限り、high-bandwidth（wMaxPacketSize の bit 12:11）と SuperSpeed を拒む。HS の capture の YUYV（640x480@30 で 18 MB/s）や SS の capture には **xHCI の拡張（p005、WS197 の source、Q1 の承認）** が要る。それまでは MJPEG と ≤ 8 MB/s の mode。
- 最初の device は手元の HDMI capture。PC の HDMI から既知の画面（色の patch + frame counter の帯）を入れ、`camcheck` が画素と落ちを判定する試験を p006 に入れる（§7.3）。
- Phase: p002 枠組み（6〜8 LW）、p003 uvc の descriptor と attach・PROBE/COMMIT（3〜4 LW）、p004 streaming（isoch・bulk・frame の組み立て）と `camcap`（5〜7 LW）、p005 xHCI の isoch の拡張（3〜4 LW）、p006 画素の試験と堅牢化（2〜3 LW）、p007 Pi 5・Pi 4・VF2（1〜2 LW）、p008 GPU の取り込み（4〜5 LW、WS285 の為）、p009 文書・seat（1〜2 LW）、p010 control（2〜3 LW、後で）、p011 規約の全文（2 LW）。合計 29〜40 LW（p010 を除けば 27〜37）。

## 1. 責務と境界

| 層 | 場所 | 責務 | 持たない物 |
| --- | --- | --- | --- |
| user の ABI | `include/uapi/camera.h` | `/dev/camN` の ioctl・mmap・poll の契約。format・mode・buffer・control・metadata の記述 | hardware の名前・USB の descriptor |
| 枠組み（camera core） | `src/drivers/camera/cam.c`、`include/drivers/camera/cam.h` | cdev の登録と退役（hotplug）、open の所有、format の問い合わせの中継と検査、buffer の確保・mmap・import・queue・dequeue・poll、stream の状態機械、frame の完了と metadata、control の中継、counters、FAULT | USB、画素の解釈（複写・変換をしない） |
| source の driver: uvc | `src/drivers/usb/usb-uvc.c`（bind・VC・枠組みへの登録）、`usb-uvc-desc.c`（VS の descriptor の解析、host で試験）、`usb-uvc-stream.c`（PROBE/COMMIT、transport、URB、worker、payload の組み立て）、`include/drivers/usb/usb-uvc.h`・`usb-uvc-desc.h` | USB の interface の claim、alternate の選択、URB、payload header の解析、frame を slot に書く、停止・detach | buffer の所有、user の ABI |
| 将来の source: CSI の receiver + sensor | 付録 A | — | — |

境界の原則:
- 枠組みは source を関数表（§2.1）でしか知らない。source は枠組みの slot の API だけで frame を渡す。
- 枠組みは画素に触らない。YUYV の変換・MJPEG の展開・debayer は userland（`camcap`・`camcheck`・WS285 の shader）。
- 眠る物（USB の control transfer、alternate の切り替え、buffer の確保）は mutex の下・spinlock の外。spinlock の下は slot の列の操作と counters だけ。URB の完了の callback は xHCI の command gate の下で走り得る（usb.h の isochronous の契約）ので thread を起こすだけ。

## 2. kernel の中の interface

### 2.1 枠組みの公開 API（`include/drivers/camera/cam.h`）

```c
/* 枠組みに登録する 1 台の camera。 */
struct drv_cam_device_desc {
	const char *driver_name;			/* "uvc"（GET_INFO） */
	char device_name[64];				/* product string（"USB Video" など） */
	char location[32];				/* "usb 1-3" のような位置（GET_INFO） */
	const struct drv_cam_source_ops *source;
	void *source_argument;
	uint32_t flags;					/* DRV_CAM_SOURCE_CPU_WRITES | DRV_CAM_SOURCE_DMA（後者は将来の CSI、§3.4） */
	uint32_t capabilities;				/* CAM_CAP_*（source が足す分。IMPORT_GPU は枠組みが足す） */
	uint32_t max_buffers;				/* 8 */
	uint32_t buffer_alignment;			/* 1（uvc）。DMA の source は address・stride の揃え */
	/* DRV_CAM_SOURCE_DMA の時だけ（付録 A の H3 の形）: */
	struct drv_dma_device *dma;
	uint64_t dma_bus_offset;
	uint64_t dma_physical_limit;
};

int drv_cam_register(const struct drv_cam_device_desc *desc, struct drv_cam_device **result);
/*
 * 退役（USB の detach）: 新しい ioctl・mmap を ENODEV で拒み、DEQUEUE で待つ thread を起こし（ENODEV）、
 * 進行中の ioctl が無くなるのを待ち、cdev を unregister する。戻った後、枠組みは source の ops を呼ばない。
 * buffer と device の構造体は最後の open・mapping の release の後に cdev の finalizer が解放する。
 */
void drv_cam_unregister(struct drv_cam_device *device);

/*
 * frame の受け渡し（source → 枠組み）。眠らない（spinlock の中）。どの context でも呼べる。
 * take: 空き列の先頭の slot を ACTIVE にして返す。無ければ EAGAIN（source は frame を捨て、drv_cam_frame_dropped）。
 * done: ACTIVE の slot を完了列へ（used_bytes、info の時刻・flags・PTS）、DEQUEUE と poll を起こす。
 * discard: ACTIVE の slot を空き列の先頭へ戻す（frame の途中で止めた・誤り）。
 */
struct drv_cam_frame_info {
	uint64_t timestamp_ns;		/* 単調 counter（kern_rtc_read_counter）を ns に。0 なら枠組みが done の時刻を入れる */
	uint32_t flags;			/* CAM_FRAME_ERROR・CAM_FRAME_INCOMPLETE・CAM_METADATA_PTS・CAM_METADATA_SCR */
	uint32_t device_pts;		/* UVC の PTS（device の clock、32 bit） */
	uint64_t device_scr;		/* UVC の SCR（48 bit） */
};
int drv_cam_slot_take(struct drv_cam_device *device, struct drv_cam_slot **slot);
void *drv_cam_slot_address(const struct drv_cam_slot *slot);		/* CPU が書く address（cached。DMA の source は NULL） */
uint64_t drv_cam_slot_bus_address(const struct drv_cam_slot *slot);	/* DMA の source 用 */
size_t drv_cam_slot_capacity(const struct drv_cam_slot *slot);		/* image_bytes */
void drv_cam_slot_done(struct drv_cam_device *device, struct drv_cam_slot *slot, size_t used_bytes, const struct drv_cam_frame_info *info);
void drv_cam_slot_discard(struct drv_cam_device *device, struct drv_cam_slot *slot);
void drv_cam_frame_dropped(struct drv_cam_device *device, uint32_t reason);	/* CAM_DROP_NO_BUFFER・CAM_DROP_OVERFLOW・CAM_DROP_ERROR・CAM_DROP_TRANSPORT: counters */
void drv_cam_source_fault(struct drv_cam_device *device, int error);		/* 続けられない（USB の切断・endpoint の halt の回復の失敗）: FAULT */
```

### 2.2 source の関数表

```c
struct drv_cam_mode_request {	/* SET/TRY_FORMAT の入力 */
	uint32_t format;		/* CAM_FORMAT_* */
	uint32_t width, height;
	uint32_t interval_100ns;	/* 0 なら mode の既定 */
};
struct drv_cam_mode_result {	/* source の答え（UVC は PROBE の答え） */
	uint32_t format, width, height, interval_100ns;
	uint32_t stride_bytes;		/* 非圧縮: 幅 × byte/画素（UVC は揃えない）。圧縮: 0 */
	uint32_t image_bytes;		/* 1 frame の最大 byte（MJPEG は device の dwMaxVideoFrameSize、§4.3） */
	uint32_t bayer_order;		/* RAW だけ */
	uint32_t sample_shift;		/* RAW の unpacked だけ */
	uint32_t mode_index;
};

/* 全部 process context、枠組みの device->mutex の下（USB の control transfer で眠ってよい）。 */
struct drv_cam_source_ops {
	int (*enum_format)(void *argument, unsigned index, struct cam_format_desc *desc);		/* ENOENT で終わり */
	int (*enum_mode)(void *argument, uint32_t format, unsigned index, struct cam_mode_desc *desc);	/* format ごとの mode（§3.2） */
	int (*try_mode)(void *argument, const struct drv_cam_mode_request *request, struct drv_cam_mode_result *result);	/* UVC: PROBE だけ */
	int (*set_mode)(void *argument, const struct drv_cam_mode_request *request, struct drv_cam_mode_result *result);	/* UVC: PROBE + COMMIT */
	int (*start)(void *argument);		/* alternate の選択、URB の投入、worker の開始。空き列には 1 つ以上ある */
	void (*stop)(void *argument);		/* URB の cancel と drain、worker の停止、ACTIVE の slot の discard、alternate 0 */
	int (*control_info)(void *argument, uint32_t id, struct cam_control_info *info);	/* NULL なら全部 UNSUPPORTED */
	int (*get_control)(void *argument, uint32_t id, int64_t *value);
	int (*set_control)(void *argument, uint32_t id, int64_t value);
	int (*read_counters)(void *argument, struct cam_counters *counters);	/* source の分（packet の誤り、missed） */
};
```

- `set_mode` は IDLE（buffer の確保前）でだけ呼ばれる。`image_bytes` は buffer の大きさになる（`REQUEST_BUFFERS` が page に揃える）。
- `start` の中で source は URB を投入し worker を起こす。frame は `drv_cam_slot_take` → 書く → `drv_cam_slot_done`。`stop` は ACTIVE の slot を全部 `discard` してから戻る（枠組みはその後に列を未使用に戻す）。
- 将来の DMA の source（CSI）は `drv_cam_slot_take` を割り込みの中で呼んで `bus_address` を hardware に渡す（付録 A の §A.4）。

### 2.3 hotplug と寿命

- `cdev_register_managed(name, rdev, &cam_cdev_ops, device, cam_finalize, &node)`（hidraw.c の型）。登録の reference は枠組みが持ち、`drv_cam_unregister` で `cdev_unregister` + `cdev_release`。finalizer は最後の open と mapping の後に走り、buffer（`kern_pmem_free`）と device を解放する。
- 番号 `cam0`〜`cam3` は枠組みの slot の表（hidraw の `hidraw_slot_claim` の型）: 抜いた device の番号は退役の後に再利用する。`/dev/system` の event（`KERN_SYSTEM_EVENT_ADD/REMOVE`、hidraw と同じ経路）を出し、sessiond の seat が挿した時に権限を与えられるようにする（p009）。
- 進行中の ioctl の数（admission、usb-hid の `close_admission` の型）: ioctl と mmap の入口で spinlock の下に数え、`unregistered` なら `ENODEV`。`drv_cam_unregister` は flag を立て、DEQUEUE の waitq を起こし、数が 0 になるまで waitq で待つ。
- source の detach の順（uvc）: streaming なら `stop`（URB の cancel・drain） → `drv_cam_unregister`（戻れば ops は呼ばれない） → uvc の構造体を解放。

## 3. `/dev/camN` の ABI（`include/uapi/camera.h`）

### 3.1 node と権限と所有

- cdev の名前 `cam0`…`cam3`（`CAM_DEVICES_MAX` 4）。
- devfs の mode: **0600 root**。`devfs.c` の `devfs_cdev_inode()` の名前の規則に `camera_name()`（`cam` + 数字）を足して 0600 にする（今は該当しない名前が 0666、devfs.c:590）。node が最初に出る p003 の前に Q1 に依頼する（他の WS の source、M10。差分は §11 の p003）。sessiond の seat が `/dev/gpu%u` と同じ型で `/dev/cam%u` を seat の user に渡し（`sessiond_seat_give`: session の user には 0600 uid、login 画面の account には渡さず root 0600 のまま。`sessiond_seat_restore`: root 0600。挿した時の event で与える）、p009 で Q1 に依頼する。D4。
- open: 何個でも open できる。問い合わせ（`GET_INFO`・`ENUM_FORMAT`・`ENUM_MODE`・`GET_FORMAT`・`TRY_FORMAT`・`GET_CONTROL_INFO`・`GET_CONTROL`・`GET_COUNTERS`）はどの open でもよい。
- **所有（M5）**: `SET_FORMAT` か `REQUEST_BUFFERS`（count > 0）を最初に呼んだ open が所有者になる。所有者がいる間、他の open の `SET_FORMAT`・`REQUEST_BUFFERS`・`QUEUE/DEQUEUE_BUFFER`・`STREAM_ON/OFF`・`SET_CONTROL`・`mmap` は `EBUSY`。所有は所有者の `REQUEST_BUFFERS` の count 0（buffer の解放と同時。format を設定しただけの時も count 0 で手放せる）か close で手放す。所有者が stream 中に close したら枠組みが stop して buffer を解放する。
- **mapping が残る間は close が来ない（L2）**: `vm_device_create` は file を retain し、最後の mapping の release の後に `file_close` する（`kern/vm/device.c` の `vm_device_put`）。fd を閉じても `munmap` していなければ cdev の close は呼ばれず、所有と stream は続く。stream は `munmap`（または process の終了）で止まる。host の試験に入れる（§7.1）。
- 抜かれた後（`drv_cam_unregister` の後）の ioctl・mmap は `ENODEV`、`poll` は `POLLERR | POLLHUP`。

### 3.2 ioctl の一覧（group `'C'`、`_IOWR` は `uapi/ioctl.h`）

全部の record は `version`（`CAM_ABI_VERSION` 1）と `size`（record の大きさ）で始まり、`reserved` は 0（gpu.h・audio.h と同じ作法）。pointer は持たない（fd と offset だけ）。

| 番号 | 名前 | record | 意味 |
| --- | --- | --- | --- |
| 1 | `CAM_GET_INFO` | `cam_info` | `driver`（"uvc"）、`name`（product string）、`location`（"usb 1-3"）、capability の bit（`CAM_CAP_RAW`・`CAM_CAP_YUV`・`CAM_CAP_RGB`・`CAM_CAP_COMPRESSED`・`CAM_CAP_IMPORT_GPU`・`CAM_CAP_EXPORT`・`CAM_CAP_CONTROLS`）、`max_buffers`（8）、`format_count` |
| 2 | `CAM_ENUM_FORMAT` | `cam_format_desc` | `index` 番目の format: `format`（§3.3）、`flags`（`CAM_FORMAT_SUPPORTED`: この camera で今使える、`CAM_FORMAT_COMPRESSED`: frame が可変長）、`bits_per_pixel`（非圧縮）、`bytes_per_group`・`pixels_per_group`（packed の形）、`mode_count` |
| 3 | `CAM_ENUM_MODE` | `cam_mode_desc` | 入力 `format`・`index`。出力: 幅・高さ、`interval_type`（`CAM_INTERVAL_DISCRETE`/`CONTINUOUS`）、`interval_count` と `intervals[8]`（100 ns 単位、先頭が既定）または `interval_min/max/step`、`max_frame_bytes`（圧縮の時の device の申告）、`bayer_order`・`native_format`・crop（RAW の時） |
| 4 | `CAM_GET_FORMAT` / 5 `CAM_SET_FORMAT` / 6 `CAM_TRY_FORMAT` | `cam_format` | 入力: `format`・幅・高さ・`interval_100ns`（0 で既定）。出力: `interval_100ns`（device が決めた値）、`stride_bytes`、`image_bytes`、`bayer_order`、`sample_shift`、`mode_index`。幅と高さは mode と一致させる（丸めない）。`SET` は IDLE でだけ（buffer 確保中・stream 中は `EBUSY`）、所有を取る。`TRY` は所有を取らずどの open でもよい（UVC は PROBE だけ） |
| 7 | `CAM_REQUEST_BUFFERS` | `cam_request_buffers` | `count`（0 で解放と所有の放棄、最大 `max_buffers`。確保できた数を返す。mmap が残っていれば `EBUSY`）、`memory`（`CAM_MEMORY_DEVICE`: driver が確保、`mmap` で見る。`CAM_MEMORY_IMPORT`: 各 buffer を fd で渡す、§3.6、p008）、`bytes`（out、1 buffer の大きさ = `image_bytes` を page に揃えた物） |
| 8 | `CAM_QUERY_BUFFER` | `cam_buffer` | `index` の buffer の `mmap_offset`（= `index × roundup(image_bytes, PAGE)`、L1）、`bytes`、`flags`（`CAM_BUFFER_QUEUED`・`CAM_BUFFER_ACTIVE`（source が書いている）・`CAM_BUFFER_DONE`・`CAM_BUFFER_MAPPED`・`CAM_BUFFER_IMPORTED`） |
| 9 | `CAM_QUEUE_BUFFER` | `cam_buffer` | `index` の buffer を空き列に入れる。未使用の buffer だけ受ける（空き列・ACTIVE・完了列にある物は `EBUSY`、M7-6）。`CAM_MEMORY_IMPORT` なら `fd` も: 最初の 1 回で取り込み、以後は `fd` が −1 なら同じ物。取り込み済みの slot に別の fd は `EBUSY`（L7） |
| 10 | `CAM_DEQUEUE_BUFFER` | `cam_buffer` | 完了した buffer を順に返す。無ければ待つ（`O_NONBLOCK` なら `EAGAIN`）。`sequence`、`timestamp_ns`、`used_bytes`、`metadata`（§3.5）、`flags` に `CAM_BUFFER_ERROR`。待ちは `STREAM_OFF`・close で `EINVAL`、FAULT で `EIO`、退役で `ENODEV` で返る（M7-5） |
| 11 | `CAM_STREAM_ON` / 12 `CAM_STREAM_OFF` | なし（`_IO`） | ON には空き列に 1 つ以上の buffer が要る（`EINVAL`）。OFF は空き列・ACTIVE・完了列（未 DEQUEUE）の buffer を全部「未使用」に戻す（その frame は失われる、M7-6） |
| 13 | `CAM_GET_CONTROL_INFO` | `cam_control_info` | `id` の最小・最大・刻み・既定・`flags`（`CAM_CONTROL_READ_ONLY`・`CAM_CONTROL_UNSUPPORTED`）。uvc は今回全部 UNSUPPORTED |
| 14 | `CAM_GET_CONTROL` / 15 `CAM_SET_CONTROL` | `cam_control` | `id`・`value`（int64）。`SET` は所有者だけ。今回は `EOPNOTSUPP`（p010 で UVC の processing unit の brightness などを id 32 以降に） |
| 16 | `CAM_GET_COUNTERS` | `cam_counters` | frame 数、落とした frame（buffer 無し・溢れ・誤り・transport の別）、source の誤り（packet の誤り・missed・短い header）、sequence の飛び |
| 17 | `CAM_EXPORT_BUFFER` | — | 予約（§3.6 の案 B）。`ENOTTY`、`CAM_CAP_EXPORT` は 0（L7） |

`poll`: `POLLIN` は dequeue できる frame がある時。`POLLERR` は FAULT の時、`POLLHUP` は退役の後。`read`/`write` は `EOPNOTSUPP`。

### 3.3 format の宣言

```c
/* RAW（Bayer）。packed は MIPI CSI-2 の並び。unpacked は 16 bit little endian の word に 1 画素（値の位置は sample_shift）。今回は source が無い（付録 A）。 */
#define CAM_FORMAT_RAW8		0x0101U
#define CAM_FORMAT_RAW10P	0x0102U
#define CAM_FORMAT_RAW10	0x0103U
#define CAM_FORMAT_RAW12P	0x0104U
#define CAM_FORMAT_RAW12	0x0105U
#define CAM_FORMAT_RAW16	0x0106U
/* YUV。YUYV = 画素 2 つが Y0 U Y1 V の 4 byte（UVC の YUY2）。UYVY = U Y0 V Y1。NV12 = Y の面 + UV の面（幅 × 高さ × 3/2）。 */
#define CAM_FORMAT_YUYV		0x0201U
#define CAM_FORMAT_UYVY		0x0202U
#define CAM_FORMAT_NV12		0x0203U
#define CAM_FORMAT_YUV420P	0x0204U
/* RGB（宣言だけ）。 */
#define CAM_FORMAT_RGB565	0x0301U
#define CAM_FORMAT_RGB888	0x0302U
#define CAM_FORMAT_XRGB8888	0x0303U
/* 圧縮（frame は可変長、used_bytes）。MJPEG = frame ごとに 1 つの JPEG（SOI〜EOI）。H264 は宣言だけ（UVC の frame based）。 */
#define CAM_FORMAT_MJPEG	0x0401U
#define CAM_FORMAT_H264		0x0402U
```

- uvc が「使える」と答える物: device の VS の format descriptor の GUID から **YUY2 → YUYV、UYVY → UYVY、NV12 → NV12**（uncompressed、UVC 1.1 §3.9.2.3 の GUID: `32595559-0000-0010-8000-00AA00389B71` = YUY2、`59565955-…` = UYVY、`3231564E-…` = NV12）、**MJPEG → MJPEG**（§3.9.2.5）。他の GUID（I420 など）と frame based（H.264）は log に出して「対応しない」。
- 非圧縮の stride = 幅 × byte/画素（UVC は行の padding をしない）、`image_bytes = stride × 高さ`（NV12 は × 3/2）。device の `dwMaxVideoFrameSize` がこれと違えば log に出し、大きい方を `image_bytes` にする。
- MJPEG の `image_bytes` = device の `dwMaxVideoFrameSize`（PROBE の答え）。0 や明らかに小さい値（幅 × 高さ / 4 未満）を言う device があれば `幅 × 高さ × 2`（YUYV と同じ）を使う（R4）。user には `used_bytes` で実際の長さを返す。
- RAW の `bayer_order`・`sample_shift` は付録 A（CSI の source が答える）。

### 3.4 buffer の model

- `CAM_MEMORY_DEVICE`、source が `DRV_CAM_SOURCE_CPU_WRITES`（uvc）: 枠組みが `kern_pmem_alloc(roundup(image_bytes, PAGE), PAGE, &run)` で buffer ごとに物理的に連続な run を取り（`vm_device_create` が 1 つの物理 base を取るので連続が要る。audio.c の shadow の ring と同じ型）、kernel の address は direct map（`kern_pmem_to_kernel`、cached）。source の worker は CPU でこの address に書き、user は `mmap(fd, mmap_offset, bytes, PROT_READ)` で cached に見る（`vm_device_create(file, paddr, address, bytes, 0（cached）, PROT_READ, release, slot)`）。device の DMA はこの memory に触らない（USB の data は xHCI の staging から `kern_memcpy` で URB の buffer に写される、`xhci_isoch_finish`）ので cache の操作は要らない。
  - 2592x1944 級の非圧縮や 1080p の YUYV（4,147,200 byte）は 4 MB の連続 run × 4。断片化で失敗したら確保できた数を返す（0 なら `ENOMEM`、R6）。
- `CAM_MEMORY_DEVICE`、source が `DRV_CAM_SOURCE_DMA`（将来の CSI、H3 の反映）: `drv_dma_alloc_coherent(desc->dma, …)` で取り、non-coherent なら uncached の alias（HAL が確保の時に direct map の line を clean + invalidate）、mapping は `VM_DEVICE_UNCACHED_RAM`。物理 = `device_address − dma_bus_offset`。p002 では **実装せず**（source が無く試せない）、flag の検査で `ENOTSUP`。付録 A の §A.3 に手順を残す。
- `CAM_MEMORY_IMPORT`: §3.6（p008）。
- queue は 2 つの列（空き = user が QUEUE した物、完了 = source が done した物）と ACTIVE の slot（source が持つ。uvc は 1 つ、DMA の source は「今」「次」の 2 つ）。slot の状態: UNUSED → (QUEUE) FREE → (take) ACTIVE → (done) DONE → (DEQUEUE) UNUSED、(discard) で ACTIVE → FREE（先頭）。
- 最小の buffer 数は 2（連続の取り込みに 4 を勧める）。`mmap` の範囲は 1 つの buffer の中に収まる事（またがる・はみ出す mmap は `EINVAL`、L1）。

### 3.5 frame の metadata（`struct cam_frame_metadata`、`cam_buffer` に埋め込む）

| field | 意味 | 出所 |
| --- | --- | --- |
| `sequence` | 完了した frame の通し番号（落とした frame も数える: 飛びで落ちが分かる） | 枠組み（`take` ごとに +1、drop でも +1） |
| `timestamp_ns` | frame の最初の payload を受けた時刻（単調 counter を ns に。input event と同じ源）。`CAM_METADATA_APPROXIMATE`（USB の URB の完了の粒度 = 4 ms 以内の遅れ） | source |
| `used_bytes` | 書いた byte（MJPEG は可変） | source |
| `device_pts` | UVC の payload header の PTS（device の clock、`dwClockFrequency` は `GET_INFO` の `clock_hz`）。`CAM_METADATA_PTS` が立つ時だけ | source |
| `device_scr` | UVC の SCR（STC 32 bit + SOF 11 bit、48 bit）。`CAM_METADATA_SCR` | source |
| `exposure`・`analogue_gain` | CSI の sensor 用（付録 A）。uvc は `CAM_METADATA_EXPOSURE_UNKNOWN`・`GAIN_UNKNOWN` | — |
| `flags` | `CAM_FRAME_ERROR`（payload header の ERR か packet の誤りが frame の間に出た）、`CAM_FRAME_INCOMPLETE`（EOF を見ずに FID が変わった: frame は返すが欠けているかも。MJPEG は EOI が無ければ ERROR も立てる）、`CAM_FRAME_SEQUENCE_GAP` | source・枠組み |

### 3.6 GPU（Vulkan）へ複写なしに渡す（p008、WS285 の為。H1・H2 の反映）

案 A（GPU が確保・camera が取り込む、D3）を p008 で実装し、案 B（camera が export）は番号の予約だけ。経路と GPU 側の差分は版 1 の review の通り:

1. app が Vulkan で export 付きの memory を確保し `vkGetMemoryFdKHR` で fd を得る。libvulkan は `GPU_ALLOCATION_EXPORT`（`libvulkan/memory.c` の `vulkan_memory_allocation_fd`）で **allocation だけの共有**（image 無し）を作る。bcm2711 では `render_export` → `bcm2711_shared_create(buffer, NULL)` で `has_image = false`。
2. 枠組みは `CAM_QUEUE_BUFFER` の最初の 1 回で `handle_fd_get(fd, KERNEL_HANDLE_DRIVER)` し、GPU core の新しい関数 `drv_gpu_shared_backing(handle, &backing, &device_identity)` で物理 page の列と cache の方針を得る。**H1**: 既存の `get_scanout_backing` は allocation だけの共有で `ENOTSUP`（share.c:192）なので、`struct drv_gpu_share_ops` に `get_allocation_backing` を足し（`gpu-share.h`）、bcm2711 の `share.c` に `bcm2711_shared_allocation_backing`（`has_image` を要らず、`flags = CONTIGUOUS | (buffer->uncached ? DRV_GPU_BACKING_UNCACHED : 0)`）、`render-device.c` にその配線、`gpu-scanout.h` に `DRV_GPU_BACKING_UNCACHED 8U`、`gpu.c` に `drv_gpu_shared_backing`（`handle->ops == gpu_shared_operations()`、`shared->allocation.version != 0`、`gpu_scanout_import` と同じ範囲の検査）。約 80〜100 行、WS031/WS141/WS279 の source、Q1 の承認（D9）。
3. 枠組みは連続（`CONTIGUOUS`）・MMIO でない・`UNCACHED`・大きさ（`bytes ≥ image_bytes`）を確かめ、満たさなければ `EXDEV`。handle は slot の解放まで retain。
4. **H2（cache）**: Pi 5 の Vulkan の memory は `bcm2711_buffer_create_uncached`（`vk/vulkan-memory.c:119`）の Normal non-cacheable の run で、`kern_pmem_map_uncached` が alias を作る時に direct map の line を clean + invalidate する。uvc の worker が CPU で書く時は、枠組みが **自分の uncached の alias**（`kern_pmem_map_uncached` をもう 1 回。HAL の規則「uncached の alias がある間 direct map で触らない」を守る）を slot の address にする。cached の backing（`UNCACHED` 無し。今の Pi 5 には無い）は `EXDEV`。GPU の job が読み終える前に同じ buffer を `QUEUE` しない契約（fence は camera が知らない。app は `vkWaitForFences` の後に `QUEUE`、R8）。V3D の job は開始で cache を無効化して input を読む（`v3d71-job.c`、WS279 p003）事を `camvk` で確かめる。
5. uncached の alias への CPU の書き（YUYV 1080p で 4 MB/frame）は cached より遅い（Pi 5 で測る。遅ければ cached の staging に組んでから `memcpy` する 2 段にする）。

案 B `CAM_EXPORT_BUFFER` は番号 17 の予約だけ。

### 3.7 control の id

- 今回は無し（uvc の `control_info` は全部 `CAM_CONTROL_UNSUPPORTED`）。ABI の id は予約する: 1〜10 は sensor（露光・gain・flip・test pattern・AEC/AGC・AWB・blank、付録 A）、**32 以降は UVC の processing unit と camera terminal**（brightness 32、contrast 33、hue 34、saturation 35、sharpness 36、gamma 37、white balance temperature 38・auto 39、backlight 40、gain 41、power line frequency 42、exposure（CT）48・auto 49、focus 50・auto 51、zoom 52）。p010 で `GET_CUR/MIN/MAX/RES/DEF/INFO`（UVC 1.1 §4.2.2）に写す。

### 3.8 拡張の口

- RAW と DMA の source は付録 A（CSI の WS）。ISP の制御の ioctl は番号 64 以降を予約。
- 複数の camera: uvc は device ごとに `camN`。
- H.264（UVC の frame based）は `CAM_FORMAT_H264` の宣言だけ。

## 4. uvc の driver の方式

### 4.1 bind と descriptor（Value(s) referenced: USB Video Class 1.1 §3.7（VC）・§3.9（VS）・§A（定数））

- `drv_usb_driver` の id: interface class 0x0E（CC_VIDEO）、subclass 0x01（SC_VIDEOCONTROL）。VC の interface に bind し、attach で: 
  1. VC の extra descriptor（CS_INTERFACE 0x24）を `drv_usb_host_interface_extra` で読む: VC_HEADER（subtype 0x01: `bcdUVC`、`wTotalLength`、`dwClockFrequency`、`bInCollection`、`baInterfaceNr[]` = VS の interface の番号）、VC_INPUT_TERMINAL（0x02、camera terminal 0x0201 の control の bit）、VC_OUTPUT_TERMINAL（0x03）、VC_SELECTOR_UNIT（0x04）、VC_PROCESSING_UNIT（0x05、`bmControls`）、VC_EXTENSION_UNIT（0x06）。p003 は header と unit の id を記録するだけ（control は p010）。
  2. VS の interface（`baInterfaceNr` の最初の 1 つ。複数の VS を持つ device は最初だけ）を `drv_usb_interface_claim(vc, vs)` で claim する（usb-bt の SCO の interface と同じ作法。core が probe 中なら EBUSY で、uvc は attach を ENODEV で終える）。
  3. VS の alternate 0 の extra descriptor を `usb-uvc-desc.c` で解析: VS_INPUT_HEADER（0x01: `bNumFormats`、`wTotalLength`、`bEndpointAddress`、`bmInfo`、`bTerminalLink`、`bStillCaptureMethod`、`bTriggerSupport`、`bControlSize`、`bmaControls`）、VS_FORMAT_UNCOMPRESSED（0x04: `bFormatIndex`、`bNumFrameDescriptors`、`guidFormat`、`bBitsPerPixel`、`bDefaultFrameIndex`、aspect、interlace、copy protect）、VS_FRAME_UNCOMPRESSED（0x05: `bFrameIndex`、`bmCapabilities`、`wWidth`、`wHeight`、`dwMinBitRate`、`dwMaxBitRate`、`dwMaxVideoFrameBufferSize`、`dwDefaultFrameInterval`、`bFrameIntervalType`（0 = 連続: min/max/step、n = 離散 n 個）、`dwFrameInterval[]`）、VS_FORMAT_MJPEG（0x06: `bmFlags`、他は uncompressed と同じ）、VS_FRAME_MJPEG（0x07、frame は uncompressed と同じ形）、VS_COLORFORMAT（0x0D、記録だけ）、VS_FORMAT_FRAME_BASED（0x10）/VS_FRAME_FRAME_BASED（0x11、宣言だけ）。他の subtype は長さで飛ばす。長さが足りない descriptor は log に出して解析を止める（device を拒まず、そこまでの format で動く）。
  4. 解析の結果を `dmesg` に dump する（p003 の受け入れ。HDMI capture の実体をこれで知る）: `usb-uvc: 1-3: UVC 1.00, clock 1000000 Hz, VS interface 1, endpoint 0x81 isochronous`、`usb-uvc: 1-3: format 1 MJPEG: 1920x1080 30 60 fps, 1280x720 30 60 fps, …`、`usb-uvc: 1-3: format 2 YUY2: 1920x1080 5 fps, 1280x720 10 fps, 640x480 30 fps`、`usb-uvc: 1-3: alternates 1..6 packet 128 256 512 1024 2048(x2) 3072(x3)`。
  5. transport: VS の alternate 0 に endpoint があれば **bulk**（alternate は 1 つ、endpoint は bulk IN）。無ければ **isochronous**（alternate 1 以降に isochronous IN が 1 つずつ。alternate 0 は zero-bandwidth で usb.c が endpoint を作らない）。各 alternate の `wMaxPacketSize`（bit 10:0 の size、bit 12:11 の追加の transaction の数 = mult）と SS なら companion の `bytesPerInterval` を表にする。
  6. `drv_cam_register`（`driver_name "uvc"`、`device_name` = product string（`drv_usb_device_get_string`）、`location` = bus と port の path）。
- 同じ device の UAC（audio）の interface は別の class なので触らない（zedBSD に USB audio の driver は無い。HDMI capture の音は今回対象外）。
- detach（抜去・driver の停止）: §2.3 の順。`quiesce` は worker の join と URB の drain（usb-hid の型）。

### 4.2 PROBE/COMMIT の交渉（Value(s) referenced: UVC 1.1 §4.3.1.1、request の番号 §A.8）

- control の request: `SET_CUR` 0x01、`GET_CUR` 0x81、`GET_MIN` 0x82、`GET_MAX` 0x83、`GET_RES` 0x84、`GET_LEN` 0x85、`GET_INFO` 0x86、`GET_DEF` 0x87。`wValue` = control の selector << 8（`VS_PROBE_CONTROL` 0x01、`VS_COMMIT_CONTROL` 0x02）、`wIndex` = VS の interface の番号、request type = class・interface（0x21 out / 0xA1 in）。`drv_usb_control(device, …, timeout 1000 ms)`。
- record の長さ: UVC 1.0 は 26 byte（`bmHint` 2、`bFormatIndex` 1、`bFrameIndex` 1、`dwFrameInterval` 4、`wKeyFrameRate` 2、`wPFrameRate` 2、`wCompQuality` 2、`wCompWindowSize` 2、`wDelay` 2、`dwMaxVideoFrameSize` 4、`dwMaxPayloadTransferSize` 4）、1.1 は 34（+ `dwClockFrequency` 4、`bmFramingInfo` 1、`bPreferedVersion` 1、`bMinVersion` 1、`bMaxVersion` 1）、1.5 は 48。長さは `bcdUVC` で決め、`GET_LEN` が通ればそれを優先する（stall する device があるので失敗は無視）。
- `try_mode`（`TRY_FORMAT`）: `bmHint` = 1（frame interval を固定）、`bFormatIndex`・`bFrameIndex`・`dwFrameInterval` を入れて `SET_CUR(PROBE)` → `GET_CUR(PROBE)` で device の答え（`dwMaxVideoFrameSize`・`dwMaxPayloadTransferSize`・実際の interval）を読む。COMMIT はしない。
- `set_mode`（`SET_FORMAT`）: 同じ PROBE の往復の後、`GET_CUR` の答えをそのまま `SET_CUR(COMMIT)`。答えの `bFormatIndex/bFrameIndex` が頼んだ物と違えば `EINVAL`（丸めない方針）。`dwMaxPayloadTransferSize` が 0 なら frame の byte × fps / (8000 × 0.9)（HS）で見積もる（R4 の quirk）。
- 失敗（stall、timeout）は `EIO`。`SET_FORMAT` の中で 3 回まで試す（device によって最初の PROBE が stall する、R4）。

### 4.3 transport と URB（isochronous）

- alternate の選択: COMMIT の `dwMaxPayloadTransferSize` = P。各 alternate の isochronous IN の **1 interval の容量** C = size × (mult + 1)（HS）、SS は companion の `bytesPerInterval`。**今の xHCI が受ける alternate**（§4.7: FS/HS、mult 0、C ≤ 1024。SS は不可）の中で C ≥ P の最小の物。無ければ受けられる中で最大の物を選び、log に「帯域が足りない（P、C）」を出す（device は fps を落とすか frame を欠く。MJPEG なら質を落とす device もある）。p005 の拡張の後は mult 1〜2 と SS の alternate も候補になる。
- `drv_usb_interface_set_alternate(vs, alt)` → endpoint が enable される（usb.c:3340〜、`host_interface_disable(old)` → `SET_INTERFACE` → enable）。endpoint の enable が xHCI の encoder に拒まれると（`ENOTSUP`/`EINVAL`）、次に小さい alternate へ落ちる。
- URB: `drv_usb_urb_alloc(device, endpoint, packets)` で **4 本**（`DRV_XHCI_ISOCH_QUEUE_MAX`）、1 本 **32 packet**（`DRV_XHCI_ISOCH_PACKETS_MAX`。HS では 32 × 125 µs = 4 ms、4 本で 16 ms の余裕）。buffer は `kern_malloc`（xHCI が自分の staging（`drv_dma_alloc_coherent`）に受けて完了の時に `kern_memcpy` で写す: `xhci_isoch_finish`。class driver の buffer に cache の操作は要らない）。packet の `offset` は **packet の長さを 4096 に切り上げた stride**（`xhci_isoch_enqueue` が 64 KiB の境界をまたぐ packet を `EINVAL` で拒むので、3072 byte の packet を詰めて並べると 22 番目がまたぐ。stride 4096 なら 1 packet が境界をまたがない）。1 本 = 32 × 4096 = 128 KiB（staging も同じ大きさの連続の coherent の run。xHCI の `xhci_request_alloc` に大きさの上限は無いが、SS の 48 KiB の packet では 1 本 8 packet（512 KiB）に減らす、p005）。
- 投入: `start` で 4 本を `drv_usb_urb_setup(urb, buffer, bytes, 0, 0, uvc_completion, uvc)` → `drv_usb_urb_setup_isochronous(urb, packets, 32)` → `drv_usb_urb_submit`。完了の callback（xHCI の command gate の下で走り得る）は **URB の番号の bit を立てて worker を起こすだけ**（`usb_bt_sco_completion` の型）。
- worker（device ごとに 1 本、`kthread_create(uvc_worker, …, SCHED_PRIORITY_DEFAULT)`、`start` で作り `stop` で join）: 完了した URB の packet を順に見て（`drv_usb_urb_iso_result`: OK/SHORT は `actual` の byte、MISSED/ERROR は counters に）、§4.5 の payload の解析と複写を行い、URB を再投入する。再投入が `EBUSY`/`EAGAIN`（endpoint の halt の回復中、skip wait）なら 1 tick 待って繰り返す。`ENODEV`（切断）なら `drv_cam_source_fault`。
- 停止（`stop`）: `stopping` を立てて worker を起こし join（zombie を待つ、usb-bt の型）→ 4 本を `drv_usb_urb_cancel`（isochronous は 1 本の cancel で endpoint の全部が終わる）→ `drv_usb_urb_drain(urb, 1000 ms)` → ACTIVE の slot を `discard` → `drv_usb_interface_set_alternate(vs, 0)`（帯域を返す）。drain が timeout したら URB と buffer を解放せず（xHCI がまだ持つ）log に出して `EIO`（detach の時は core の quiesce に任せる）。

### 4.4 transport と URB（bulk）

- alternate 0 の bulk IN。URB 4 本、1 本の大きさ = `dwMaxPayloadTransferSize`（device はこの大きさの payload transfer を送り、短い packet で終える）、flag `DRV_USB_URB_SHORT_OK`。xHCI は `DRV_USB_HCD_CAP_CONCURRENT_URBS` を持つので endpoint に 4 本同時に出せる。完了は 1 本 = 1 payload transfer（header 1 つ + data）。worker は同じ（§4.5 の解析を URB 1 本 = 1 payload として呼ぶ）。
- UHCI/EHCI の host（isochronous を持たない）では bulk の device だけ動く。

### 4.5 payload の解析と frame の組み立て（Value(s) referenced: UVC 1.1 §2.4.3.3 payload header）

- payload（isochronous は packet 1 つ、bulk は URB 1 本）の先頭: `bHeaderLength`（≥ 2、≤ actual）、`bmHeaderInfo`: bit 0 FID（frame の識別、frame ごとに反転）、bit 1 EOF（この payload で frame が終わる）、bit 2 PTS（4 byte が続く）、bit 3 SCR（6 byte が続く）、bit 4 RES、bit 5 STI（still）、bit 6 ERR（誤りのある frame）、bit 7 EOH。data は `payload + bHeaderLength` から `actual − bHeaderLength` byte。`actual` が 0 の packet は何も無い（device が idle）。`actual < 2` や `bHeaderLength > actual` は壊れた payload: counters に数え、今の frame に ERROR。
- 状態（worker が持つ）: `current`（ACTIVE の slot か NULL）、`offset`（書いた byte）、`fid`（最後に見た FID）、`skipping`（buffer が無くて今の frame を捨てている）。
  1. FID が前と違う（新しい frame の始まり）: `current` があれば EOF を見ていない → `CAM_FRAME_INCOMPLETE` を付けて `done`（Linux の uvcvideo と同じく、EOF を立てない device がある）。`drv_cam_slot_take`: `EAGAIN` なら `skipping = 1`、`drv_cam_frame_dropped(NO_BUFFER)`。取れたら `offset = 0`、時刻 = 今、PTS/SCR をこの payload から（あれば）。
  2. data を `current` の address + `offset` に `kern_memcpy`。`offset + 長さ > capacity` なら溢れ: `discard`、`dropped(OVERFLOW)`、`skipping = 1`。
  3. ERR の bit: frame に `CAM_FRAME_ERROR`（捨てない。user が判断する）。
  4. EOF: `done(used_bytes = offset)`。MJPEG なら末尾の 2 byte が EOI（0xFF 0xD9）でなければ ERROR も付ける。`current = NULL`。
  5. `skipping` の間は data を捨て、FID が変わるまで何もしない。
- MISSED/ERROR の packet（isochronous）: counters に、今の frame に ERROR（data が欠けている）。
- 時刻: frame の最初の payload を見た時（worker。URB の完了の粒度 = 4 ms 以内の遅れ、`APPROXIMATE`）。

### 4.6 counters と診断

- source の counters: payload の誤り（短い header・ERR）、packet の MISSED・ERROR、URB の再投入の失敗、frame の drop の種類、xHCI の isoch の統計は `hw.usb` の既存の物。
- `GET_COUNTERS` で user に、`dmesg` に `start`/`stop` の 1 行（alternate、packet の大きさ、P、URB の本数）。

### 4.7 xHCI の isochronous の制約と拡張（p005、WS197 の source）

- 今: `drv_xhci_endpoint_context_encode`（`pci-xhci-control.h`）は isochronous を「FS/HS、1 interval に 1 transaction」に限り、`wMaxPacketSize` の bit 11〜15 が 0 でない（high-bandwidth）と SuperSpeed を拒む（注記「SuperSpeed and high-bandwidth endpoints are refused」）。`xhci_isoch_enqueue` も packet の長さを `max_packet_size & 0x7ff` 以下に限る。よって **1 interval ≤ 1024 byte = HS で 8.19 MB/s** まで。
- HDMI capture（HS、MS2109 級）の alternate は 3072（mult 2）まであり、YUYV 640x480@30（18.4 MB/s）・MJPEG 1080p の高い bit rate・720p 以上の YUYV には high-bandwidth が要る。USB 3 の capture（MS2130 級、SS、bulk の物が多い。isochronous なら burst）には SS の isochronous が要る。
- 拡張の内容（p005、`pci-xhci-control.h`・`pci-xhci.c`、Q1 の承認。xHCI 1.2 §6.2.3（endpoint context の Max Packet Size・Max Burst Size・Mult・Max ESIT Payload）・§4.11.2.3・§6.4.1.3（Isoch TRB の TBC・TLBPC）を仕様として参照）:
  1. HS の high-bandwidth: Max Packet Size = size、**Max Burst Size = mult**（HS では追加の transaction の数をここに置く）、Max ESIT Payload = size × (mult + 1)。packet の長さの上限を ESIT payload に。
  2. SS: Max Burst = companion の `bMaxBurst`、Mult = companion の `bmAttributes` の bit 1:0、Max ESIT Payload = companion の `wBytesPerInterval`。Isoch TRB の TBC = ceil(長さ / (size × (burst + 1))) − 1、TLBPC = 最後の burst の packet 数 − 1。
  3. `xhci_isoch_enqueue` の 64 KiB の境界の検査はそのまま（class driver が stride で避ける）。
  4. host の試験: `pci-xhci-control.h` の encoder は inline なので host の試験に入れやすい（`plan/ws197/tests` の型で `xhci-isoch-host-test` に HS mult・SS の encode の期待値）。回帰: BT の SCO（T1、5330 か Pi 4 の BT の headset）と usb-hid。
- 見積もり 3〜4 LW。p004 は拡張無しで（MJPEG と小さな YUYV で）frame を出し、p005 の後に高い mode を p006 で測る。

### 4.8 build の計画

- `Makefile`: `CONFIG_DRIVER_USB_UVC ?= y`（`CONFIG_DRIVER_USB_BT` の隣）、共通の `-D` に `-DCONFIG_DRIVER_USB_UVC=…`。
- `platform/amd64/vmunix.mk`: `AMD64_USB_CLASS_SOURCES += src/drivers/usb/usb-uvc.c src/drivers/usb/usb-uvc-desc.c src/drivers/usb/usb-uvc-stream.c src/drivers/camera/cam.c`（`ifeq ($(CONFIG_DRIVER_USB_UVC),y)`）。`platform/arm64/vmunix.mk` の `ARM64_USB_SOURCES`、`platform/riscv64/vmunix.mk` の `RISCV64_PCI_DEVICE_SOURCES` に同じ。driver の登録は各 platform の USB の class driver の登録の列（`drv_usb_hid_driver_register` の隣）に `drv_usb_uvc_driver_register()`。
- `config/ci/config-rpi4.mk`・`config-rpi5.mk`・amd64・vf2 の ci の config に `CONFIG_DRIVER_USB_UVC := y`。
- host の試験: `$(BUILD)/tests/cam-host-test`（枠組み）、`uvc-desc-host-test`（descriptor の解析。入力は実機から取った descriptor の byte 列を `plan/ws282/tests/descriptors/*.hex` に）、`uvc-payload-host-test`（frame の組み立て）。`platform/amd64/vmunix.mk` の host test の型（`$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Isrc …`）。
- userland: `userland/tests/camcap/`・`userland/tests/camcheck/`（`ZEDBSD_USERLAND_PACKAGE`、tests の群。`camcheck` は libjpeg-compat に link）。

## 5. 失敗と回復・並行性・資源

- **stream の状態機械**（枠組み、`enum cam_state`）: `IDLE`（format 可）→ `BUFFERS`（REQUEST_BUFFERS 済み、format 不可）→ `STREAMING`（STREAM_ON: source の `start` が成功した後に spinlock の下で）→ `STOPPING`（source の `stop` の間。slot の take は `EAGAIN`）→ `BUFFERS`。`FAULT`（`drv_cam_source_fault`、または 2 s 間 frame が無い watchdog）: `poll` が `POLLERR`、`DEQUEUE` が `EIO`、`STREAM_OFF`（`stop` を通す）で `BUFFERS` に戻る。`GONE`（退役）: 全部 `ENODEV`。
- **lock**: `device->lock`（spinlock、rank DEVICE）が列・slot の状態・counters・admission の数を守り、source の worker（`take`/`done`/`discard`）と ioctl の両方が取る（`spin_lock_irqsave`。DMA の source は割り込みからも取るので irqsave）。`device->mutex`（mutex、rank DEVICE）が ioctl 同士（format・buffer・stream の開始/停止・control）を直列にする。順序は mutex → spinlock。**spinlock を持ったまま眠らない・譲らない**（WS278 の GEM の停止の教訓: `kern_usleep_range` は tick に譲る）。source の `set_mode`・`start`・`stop`・control は mutex の下・spinlock の外。slot の address への `kern_memcpy` は lock の外（slot は ACTIVE で source だけが持つ）。
- **DEQUEUE の待ち**: `device->mutex` を持たずに、spinlock + `waitq_sleep` で眠る（`STREAM_OFF` が mutex を取れるように）。起こす側: `done`、`STREAM_OFF`・close（`EINVAL`）、FAULT（`EIO`）、退役（`ENODEV`）。`O_NONBLOCK` は `EAGAIN`。
- **監視（watchdog）**: 枠組みは device ごとに thread を持たず、uvc の worker が自分で見る（URB の完了が 1 s 無い = device が止まった: `drv_cam_source_fault`）。DMA の source の為の watchdog の thread は付録 A（CSI の WS で）。
- **所有権と寿命**: buffer（`struct drv_cam_slot`）は枠組みが所有し、`REQUEST_BUFFERS 0`・close で解放。mmap 中は `vm_device_mapping` が slot を retain する（release callback で数を減らし、mapping が残る間は run を解放しない: 「REQUEST_BUFFERS 0 は mmap が残っていれば EBUSY」）。import した GPU の handle は slot が retain し、解放で `handle_put`。ACTIVE の slot は source の `stop` が `discard` するまで未使用に戻さない。退役の後は finalizer が全部解放する。
- **USB の失敗**: endpoint の halt（babble・transaction error）は xHCI の worker が回復し、その間の投入は `EBUSY`（worker が待って繰り返す）。切断は URB が `DISCONNECTED` で終わり、投入が `ENODEV` → `FAULT`、続いて core の detach → 退役。
- **buffer の枯渇**: 空き列が無ければ frame を捨てる（`NO_BUFFER`）。user は `GET_COUNTERS` と sequence の飛びで知る。
- **close**: stream 中の close は停止 → buffer 解放。mapping が残っていれば close 自体が来ない（§3.1）。
- **shutdown**: 枠組みは何もしない。suspend は Future Work（USB の suspend は core が URB を止める）。

## 6. ユーザーの判断

### 6.1 決定済み（2026-10-23「decisionは推しでいいです」。CSI に固有の物は付録 A に写した）

| # | 問い | 決定 | 今の版での扱い |
| --- | --- | --- | --- |
| D1 | camera の connector | どちらでも（driver が探す）。22 pin の cable はある | CSI: 付録 A |
| D2 | RP1 の DesignWare I2C の code | (a) `lpss-i2c.c` から core を抜いて共有 | CSI: 付録 A（今回はやらない） |
| D3 | GPU との buffer の共有の向き | A: GPU が確保・camera が取り込む | §3.6、p008 |
| D4 | `/dev/camN` の権限 | 0600 root + sessiond が seat の user に渡す | §3.1、p003・p009 |
| D5 | IMX219 | 後で | CSI: 付録 A |
| D6 | 既定の mode と露光 | 1296x972 RAW10、sensor の AEC/AGC on | CSI: 付録 A。uvc の既定は device の既定の format/frame/interval |
| D7 | RAW10 の既定の memory の形 | 16 bit unpacked、`sample_shift` を ABI で | ABI に残す（§3.3）、CSI: 付録 A |

### 6.2 今回の問い（Q1 とユーザー）

| # | 問い | 答え・勧め |
| --- | --- | --- |
| D8 | 手元の HDMI capture は debug に使えるか（ユーザー） | **使える。** 安い HDMI capture（Macrosilicon MS2109 など）は UVC 1.0/1.1 の device で、MJPEG（1080p まで）と YUYV（USB 2.0 の帯域の制約で 1080p は数 fps、640x480 で 30 fps）を出し、UAC の audio も持つ（無視する）。uvc の最初の試験の device にできる。制約: (1) 今の xHCI は isochronous を 1024 byte/interval に限る（§4.7）ので、p005 の拡張までは MJPEG と小さな YUYV（640x480 で約 13 fps）だけ。(2) 実体（UVC の版、format、alternate、isochronous か bulk か）は p003 の descriptor の dump で確かめる。(3) MJPEG の frame は DHT（Huffman の表）を省く物が多い: libjpeg-compat が既定の表を補うかを p004 で確かめ、補わなければ `camcap` が標準の表（JPEG の仕様の Annex K、公開）を挿す。(4) 画素の正しさは PC の HDMI から既知の画面を入れて測る（§7.3）。capture は入力を自分の mode に scale するので patch は大きく、限定 range（16〜235）と全 range の違いは測って記録する |
| D9 | §3.6 の GPU core と bcm2711 の share の差分を誰が入れるか（Q1） | p008 の時に WS279 が完了していれば WS282 の担当（Q1 の承認の下）、していなければ WS279 の担当 |
| D10 | p005 の xHCI の isochronous の拡張（WS197 の source）を誰が入れるか（Q1） | WS282 の担当が Q1 の承認の下で入れる（WS197 は完了の WS）。回帰は T1（BT の SCO、usb-hid） |
| D11 | 板の試験は誰が流すか（Q1、L6） | x86（5330）と Pi 4/Pi 5/VF2 は T1 に依頼（AGENTS.md の規則）。HDMI の source の PC の操作（pattern の表示）はユーザーか Q1。担当が自分で SSH する運用なら Q1 の許可 |
| D12 | 買う camera（ユーザー、任意） | UVC 1.1 の USB 2.0 の webcam（YUYV と MJPEG を両方持つ物。例: 720p/1080p の一般的な webcam）。USB 3 の camera（SS）は p005 の後。Pi の camera module は CSI なので今回は対象外 |

## 7. 試験の計画

### 7.1 host の試験（p002〜p004）

- `cam-host-test`（枠組み、偽の source: `start` で試験の thread が `take`/`done` を呼ぶ）: 状態機械・列・slot の状態・metadata・**所有（2 つ目の open の `SET_FORMAT` と `REQUEST_BUFFERS` が EBUSY、所有者の `REQUEST_BUFFERS 0` で手放せる）**・`REQUEST_BUFFERS 0` と mmap の残り・**close の後も mapping が残る間は stream が続き、munmap で止まる**・STREAM_OFF での未使用への戻し（完了列の未 DEQUEUE も）・`QUEUE_BUFFER` の重複（EBUSY）・枯渇で sequence が飛ぶ事・`discard`・FAULT の遷移・DEQUEUE の待ちが STREAM_OFF で EINVAL、退役で ENODEV で返る事・退役の admission（進行中の ioctl が終わるまで `drv_cam_unregister` が待つ）・mmap_offset の式・ioctl の record の version/size/reserved の検査・MJPEG の `used_bytes`。cdev・copyin/copyout は host では直接関数を呼ぶ（既存の host の試験の作法）。
- `uvc-desc-host-test`: 実機から取った descriptor の byte 列（p003 で HDMI capture と webcam から `hw.usb` の dump か driver の log で取り、`plan/ws282/tests/descriptors/` に置く。公開の webcam の descriptor の写しも可）を解析し、format・frame・interval・alternate の表が期待と一致する事、壊れた descriptor（長さ不足・未知の subtype）で止まらない事。
- `uvc-payload-host-test`: 偽の packet の列（FID の反転、EOF、ERR、PTS/SCR、header の長さの誤り、MISSED、溢れ、buffer 無し）で frame の組み立てと flags・counters が期待通り。
- `xhci-isoch-host-test`（p005）: encoder の HS mult と SS の値。
- build: amd64・rpi4・rpi5・vf2 の ci の config で warning 0。

### 7.2 QEMU

- QEMU の UVC の device model は無い（QEMU は USB の webcam を emulate しない。host の camera の passthrough は `-device usb-host` で可能だが T1 の環境に camera が要る）。QEMU の試験は **無し**。p005 の回帰（BT の SCO・HID）は実機。

### 7.3 実機

| 段 | 板と device | 見る物 | 期待 |
| --- | --- | --- | --- |
| p003 | 5330（x86、xHCI）+ HDMI capture | `dmesg` の `usb-uvc:` の dump、`ls -l /dev/cam0` | UVC の版・format・frame・interval・alternate の表が出る、`crw------- root`。descriptor の byte 列を取って tests/ に |
| p004 | 5330 + HDMI capture（PC の HDMI を入れる） | `camcap -f mjpeg -o f.jpg`（1 frame）、`camcap -f yuyv -w 640 -h 480 -o f.ppm`、`camcap -n 300 -o /dev/null` | JPEG が PC の画面として見える（host で PNG に、`build/review/ws282/` に置いてユーザーに見せる）、YUYV の PPM が見える、300 frame で drop の数と fps（≤ 1024 byte/interval の alternate での値を記録） |
| p005 | 5330 + HDMI capture、BT の headset（SCO の回帰）、USB の keyboard/mouse | `camcap -f yuyv -w 1280 -h 720 -n 300`、`camcap -f mjpeg -w 1920 -h 1080 -n 300`、SCO の通話の回帰（T1） | 3072 の alternate が選ばれ fps が上がる（720p YUYV で ≥ 10 fps、1080p MJPEG で 30 fps）、SCO と HID が前と同じ |
| p006 | 5330 + HDMI capture + pattern の PC | `camcheck`（§7.4）: YUYV と MJPEG で 100 frame | 8 patch の色が許容内（YUYV ±8/255、MJPEG ±16/255）、counter の帯が単調（重複は capture の仕様として数だけ記録、飛びは drop の数と一致）、stop/start 100 回で leak なし、stream 中の抜去で `ENODEV` と再挿入で `cam0` が戻る、2 つ目の open の `EBUSY` |
| p007 | Pi 5（RP1 の xHCI）、Pi 4（VL805）、VF2（PCIe の xHCI）+ HDMI capture か webcam | p004 と p006 の短い版（`camcap` 1 frame と `camcheck` 30 frame） | 3 枚で同じ結果。non-coherent の板（Pi 4/5、VF2）で画素が壊れない（xHCI の staging の複写の確認） |
| p008 | Pi 5 + capture + WS279 の V3D | `camvk`（Vulkan の memory に取り込み、compute で YUYV → gray の PGM） | `camcap` と同じ絵、前の frame が読めない |

### 7.4 試験の道具

- `userland/tests/camcap/`: `/dev/camN` を開き、format・幅・高さ・interval・buffer 数を引数で、N frame 取って最後の 1 枚（または全部）を書く: YUYV → PPM（BT.601 の限定 range → RGB）、MJPEG → `.jpg` のまま（と `--decode` で libjpeg-compat で PPM）、RAW → PGM（付録 A）。counters・metadata・fps を出す。回帰に残す（master.md の Tools に登録、p009）。
- `userland/tests/camcheck/`（p006）: 既知の pattern の判定。pattern は `plan/ws282/tests/pattern/pattern.html`（どの PC の browser でも全画面で出せる: 上の帯に 8 色の patch（白・黄・シアン・緑・マゼンタ・赤・青・黒、75%）、中に灰の段、下の帯に **frame counter**（16 個の黒/白の正方形で 16 bit の数、`requestAnimationFrame` ごとに +1）と 1 画素の市松（scale の検出用、判定には使わない））。`camcheck` は N frame を取り、各 frame で 8 patch の平均 RGB と counter を読み、許容・単調・重複/飛びの数を出して PASS/FAIL と 1 枚の PNG（`build/review/ws282/`）を残す。限定/全 range は patch の白と黒の値から判定して記録する（失敗にしない）。
- `userland/tests/camvk/`（p008）。

## 8. 引用する値と手順の出典

| 出典 | 何を | どの block |
| --- | --- | --- |
| USB Video Class 1.1（USB-IF、公開）§3.7・§3.9・§A | VC/VS の descriptor の subtype と field、GUID、request の番号、control の selector | usb-uvc-desc.c、usb-uvc.c、usb-uvc-stream.c |
| 同 §2.4.3.3 | payload header の bit | usb-uvc-stream.c |
| 同 §4.3.1.1 | PROBE/COMMIT の record（1.0/1.1/1.5 の長さ） | usb-uvc-stream.c |
| USB 2.0 §9.6.6 | wMaxPacketSize の bit 12:11（high-bandwidth） | usb-uvc-stream.c、pci-xhci-control.h（p005） |
| USB 3.2 §9.6.7 | SuperSpeed endpoint companion（bMaxBurst、bmAttributes の Mult、wBytesPerInterval） | 同上 |
| xHCI 1.2 §6.2.3・§4.11.2.3・§6.4.1.3 | endpoint context の Max Burst・Mult・Max ESIT Payload、Isoch TRB の TBC/TLBPC | pci-xhci-control.h・pci-xhci.c（p005） |
| JPEG（ITU-T T.81）Annex K | 標準の Huffman の表（MJPEG が DHT を省く時に camcap が挿す） | userland/tests/camcap |
| Linux `drivers/media/usb/uvc/uvc_driver.c`（`uvc_ids` の quirk）— **該当があれば p006 で**、Guardrail の規則で「Value(s) referenced:」 | device ごとの quirk（帯域の修正、PROBE の長さ） | usb-uvc.c |
| 版 1 の Linux rpi-6.12.y の出典 | 付録 A の表 | （CSI の WS で） |

## 9. 危険と確かめる点

- R1 **xHCI の帯域**（§4.7）: 拡張（p005）まで 8.19 MB/s。HDMI capture の実用の mode は p005 の後。拡張は WS197 の source で、SCO の回帰が要る。
- R2 **device の個性**: UVC の device は仕様から外れる物が多い（PROBE の stall、`dwMaxPayloadTransferSize` 0、EOF を立てない、FID を反転しない、MJPEG の DHT 無し、`dwMaxVideoFrameSize` の嘘、GET_LEN の stall）。設計は Linux の uvcvideo が持つ既知の対処（§4.2・§4.5）を**仕様の範囲で**入れ、device ごとの quirk は p006 で手元の device に合わせて足す。
- R3 **worker の遅れ**: 4 本 × 4 ms = 16 ms の間に worker が再投入しないと missed service（xHCI の skip wait）。worker は `SCHED_PRIORITY_DEFAULT`。負荷の高い時（WS285 の compute と同時）の drop を p006 で測り、要れば優先度を上げるか URB を 8 本に（xHCI の FIFO は 4 なので本数は 4 のまま packet を増やす方向）。
- R4 **MJPEG の buffer の大きさ**: device の申告が小さいと溢れ（`OVERFLOW` で drop）。幅 × 高さ × 2 を下限にする。
- R5 **cache**: uvc の経路は CPU の複写だけで device の DMA は xHCI の staging に閉じる。non-coherent の板（Pi 4/5、VF2）で xHCI の staging の扱いが正しい事は既存の USB の driver（storage・NCM）で通っている。GPU の取り込み（p008）だけ uncached の alias（§3.6）。
- R6 **連続した物理 memory**: 1080p の YUYV で 4 MB × 4。断片化で失敗したら数を減らして返す。
- R7 **hotplug の競合**: 抜去の detach と ioctl・mmap・worker の競合は admission と `stop` → `unregister` の順で閉じる。mapping が残る process が居ても detach は進み（buffer は finalizer まで残る）、process は `ENODEV` を見る。host の試験で順序を確かめる。
- R8 **GPU の fence 無し**（§3.6）。
- R9 **capture の scale と range**: HDMI capture は入力を自分の mode に scale し、色の range を変え得る。`camcheck` は patch を大きくし、range は記録する。
- R10 **license**: UVC・USB・xHCI・JPEG の仕様の定数は公開の事実。Linux の quirk の表を使う時は Guardrail の規則で明示。p011 で監査。

## 10. 選ばなかった案と理由

- **V4L2 互換の ABI**（`VIDIOC_*`）: pointer を含む record・多数の type・Linux の header の互換が要り、zedBSD の既存の device（gpu・audio・evdev）が独自の fixed-width の ABI を取っているのと合わない。独自の小さな ABI にした。
- **uvc が YUYV を RGB に変換して渡す**: kernel で画素に触らない方針。WS285 は GPU で変換する。`camcap` が CPU で変換する。
- **MJPEG を kernel で展開する**: 同上。libjpeg-compat が userland にある。
- **read(2) で frame を読む**: 複写が要り GPU へ渡せない。mmap と import にした。
- **isochronous の URB を worker 無しで callback で処理する**: callback は xHCI の command gate の下で走り得て、投入も複写もできない（usb.h の契約）。worker にした（usb-bt の SCO と同じ）。
- **packet を詰めて並べる**: 64 KiB の境界で `EINVAL`。stride を 4096 にした（staging が 33% 増えるが 128 KiB）。
- **xHCI の拡張を p004 の前に**: 拡張無しでも MJPEG と小さな YUYV で経路が全部確かめられる。descriptor と frame の組み立ての不確かさ（R2）を先に潰す。
- **CSI を付録に残さず消す**: 調べの結果（register・clock・順序・license の扱い）は CSI の WS で使う。設計の本文から外し付録に圧縮した。
- **枠組みの watchdog の thread**（版 2 の前の案）: uvc は worker が URB の完了で device の生死を見られる。DMA の source（CSI）の時に足す。
- **control を最初から**: ユーザーの指示（最初は無し）。ABI の id だけ予約。

## 11. 実装の Phase

| Phase | 内容 | 依存 | 見積もり | 受け入れの条件 |
| --- | --- | --- | --- | --- |
| p002 | 枠組み: `include/uapi/camera.h`、`include/drivers/camera/cam.h`、`src/drivers/camera/cam.c`（cdev と退役、所有、format の中継、CPU 書きの buffer と cached の mmap、slot の API、queue、`O_NONBLOCK`・poll、metadata、counters、状態機械、FAULT）、host の試験 | p001 | 6〜8 LW | host の試験が全部 PASS、amd64 と rpi5 の config で warning 0、規約の全文（自分で読み直す） |
| p003 | uvc の bind と descriptor: `usb-uvc.c`・`usb-uvc-desc.c`・`usb-uvc.h`、VC/VS の解析と dump、VS の claim、PROBE/COMMIT（`try_mode`/`set_mode`）、`drv_cam_register`、build の配管（§4.8）。**Q1 に依頼**: `src/kern/devfs.c` の `camera_name()` の 0600 の規則（M10、約 15 行）。板（5330 + HDMI capture）で dump と `/dev/cam0`、descriptor の byte 列を tests/ に | p002（登録の形。p002 の host の試験と並行可） | 3〜4 LW | `dmesg` の dump、`/dev/cam0` が 0600 root、`ENUM_FORMAT/MODE` と `TRY_FORMAT` が device の値を返す、desc の host の試験 PASS |
| p004 | streaming: `usb-uvc-stream.c`（alternate の選択、isochronous と bulk の URB、worker、payload の解析と frame の組み立て、停止）、`camcap`（YUYV → PPM、MJPEG → jpg/PPM）。板で最初の frame（MJPEG と 640x480 の YUYV） | p003 | 5〜7 LW | 板で MJPEG と YUYV の絵がユーザーの目視で PC の画面、300 frame 連続で落ちなし（drop の数を記録）、payload の host の試験 PASS |
| p005 | xHCI の isochronous の拡張（HS の high-bandwidth、SS の burst、TBC/TLBPC、§4.7。WS197 の source、Q1 の承認 D10）、uvc の alternate の選択に反映。T1 に BT の SCO と HID の回帰 | p004（並行可: encoder と host の試験は先に書ける） | 3〜4 LW | 板で 3072 の alternate が選ばれ 720p YUYV ≥ 10 fps・1080p MJPEG 30 fps、SCO・HID の回帰 PASS、host の試験 PASS |
| p006 | 画素の試験と堅牢化: `camcheck` と `pattern.html`、drop・重複の測定、stop/start 100 回、stream 中の抜去と再挿入、2 つ目の open、device の quirk（手元の device に合わせて） | p004（高い mode は p005） | 2〜3 LW | §7.3 の p006 の表。`camcheck` が PASS |
| p007 | Pi 5・Pi 4・VF2 の確認（同じ image の config、T1） | p006 | 1〜2 LW | 3 枚で `camcap` と `camcheck` の短い版が PASS |
| p008 | `CAM_MEMORY_IMPORT`（§3.6: `drv_gpu_shared_backing` + bcm2711 の `get_allocation_backing` + `DRV_GPU_BACKING_UNCACHED`（D9）、枠組みの取り込みと uncached の alias）、`camvk`。WS285 の為 | p004、WS279 の V3D 7.1 が main にあり Vulkan の export が動く事 | 4〜5 LW | `camvk` の PGM が `camcap` と一致、前の frame が読めない、uncached の書きの速さの記録 |
| p009 | `docs/reference/camera.md`（ABI の文書、docs/README の一覧に追加）、**Q1 に依頼**: `userland/desktop/sessiond/seat.c` の `/dev/cam%u`（`SEAT_CAMERA_COUNT 4`、give/restore、挿した時の event）、`camcap`・`camcheck` を Tools に登録 | p006 | 1〜2 LW | 文書、seat の切り替えと挿入で `/dev/cam0` の所有が変わる |
| p010 | control（UVC の processing unit・camera terminal の GET/SET、id 32 以降）。後で（ユーザー「control は最初は無し」） | p004 | 2〜3 LW | brightness などが `camcap --control` で効く |
| p011 | 規約の全文の見直し（p002〜p009 の C）と license の監査 | p008・p009（p010 は済んでいれば含める） | 2 LW | 違反 0、監査の記録 |

合計 29〜40 LW（p010 を除けば 27〜37）。板の試験は p003・p004・p005・p006・p007・p008 の末（6 回、うち p007 は 3 枚）。

## 12. 版 2 の変更（review 2026-10-23 の指摘 → 変更 → 節）

| 指摘 | 変更 | 節 |
| --- | --- | --- |
| H1 `drv_gpu_shared_backing` が allocation の export で ENOTSUP | bcm2711 の share に `get_allocation_backing`、`gpu-share.h` の op、`gpu-scanout.h` の `DRV_GPU_BACKING_UNCACHED`、gpu.c の `drv_gpu_shared_backing`。場所・行数・担当（D9）・p008 の依存 | §3.6、§6.2、§11 p008 |
| H2 取り込んだ GPU の buffer の cache | Pi 5 の Vulkan の memory は uncached の run である事を確かめ、`UNCACHED` だけ受け、枠組みは自分の uncached の alias で書く（uvc は CPU で書く）。cached は `EXDEV`。fence の契約 | §3.6、§9 R5・R8 |
| H3 driver の buffer の alias と cache | uvc の buffer は CPU だけが書く cached の run（device の DMA は xHCI の staging に閉じる）。DMA の source の為の `drv_dma_alloc_coherent` の形は flag で分け、p002 は `ENOTSUP`、手順は付録 A | §3.4、付録 A.3 |
| M1 Bayer の表 | 付録 A に直した表（GBRG） | 付録 A.5 |
| M2 REMAP の値の位置 | ABI の `sample_shift` は残し、測定は CSI の WS | §3.3、付録 A |
| M3 `kern_usleep_range` は譲る | source の眠る op は mutex の下・spinlock の外。slot の API は眠らない | §1、§2.2、§5 |
| M4 停止の順序と競合 | uvc: `stopping` → worker の join → cancel/drain → discard → alternate 0。CSI の順は付録 A.4 | §4.3、付録 A.4 |
| M5 所有の穴 | `SET_FORMAT` も所有を取る | §3.1、§7.1 |
| M6 電源の状態機械と control の保持 | uvc には電源の遷移が無い。CSI の案は付録 A.6 | 付録 A.6 |
| M7 kernel の interface の欠け | source の関数表と slot の API を作り直し（take/done/discard、fault、counters）、DEQUEUE の待ちの規則、QUEUE の重複、STREAM_OFF の列の扱い。DMA の bus offset は desc | §2、§3.2、§5 |
| M8 AEC/AGC の metadata | uvc は露光を持たないので UNKNOWN。CSI は付録 A | §3.5 |
| M9 誤りの割り込み | CSI: 付録 A.4（IRQ_MASK 0） | 付録 A.4 |
| M10 devfs の 0600 の順 | p003 の前に Q1 に依頼、seat は p009 | §3.1、§11 |
| M11 build の計画 | §4.8 を新設（USB の class の列に uvc、3 platform） | §4.8 |
| L1 mmap の offset | `index × roundup(image_bytes, PAGE)`、またがる mmap は EINVAL | §3.2、§3.4 |
| L2 mapping が残る間 close が来ない | 挙動を書き、host の試験に | §3.1、§7.1 |
| L3 sequence と INCOMPLETE | sequence は take ごと（drop も数える）、`INCOMPLETE` は「EOF を見ずに次の frame」 | §3.5、§4.5 |
| L4 I2C の所要時間 | CSI: 付録 A | 付録 A |
| L5 22 pin の cable | D1 に記録 | §6.1 |
| L6 Phase の依存と範囲 | 全部作り直し（p011 の依存、poll は p002、板の担当 D11） | §11、§6.2 |
| L7 その他 | `CAM_EXPORT_BUFFER` は ENOTTY、別の fd は EBUSY、fence の契約。CSI の分は付録 A | §3.2、§3.6、付録 A |

## 13. 今回はやらない物（CSI、2026-10-23 のユーザーの指示）

- Pi 5 の CSI の camera（rpicam: RP1 の CFE の CSI-2 receiver と DMA、DPHY、OV5647/IMX219 の sensor、RP1 の DesignWare I2C の共有（D2 (a)）、regulator の GPIO、clock）。枠組みの ABI（RAW の format、`bayer_order`・`sample_shift`、sensor の control の id 1〜10、露光・gain の metadata）と source の API（DMA の source の flag と `bus_address`）は CSI が後から入れられる形で残す。
- 版 1 の調べは付録 A。CSI の WS を立てる時はそれを起点にし、review の H2・H3・M1〜M9・L3・L4 の反映（版 2 の最初の草稿で書いた物）を使う。

## 付録 A. 版 1 の Pi 5 の CSI（rpicam）の調べ（圧縮。出典は Linux rpi-6.12.y と firmware の dtb、license は冒頭の規則）

### A.1 板と device tree（D0 の dtb で確認）

| 物 | node | 値 |
| --- | --- | --- |
| CSI の block | rp1 の子 `raspberrypi,rp1-cfe`（2 つ、`reg` の最初の address で区別） | csi0: 0x40110000/0x40114000/0x40120000/0x40124000（CSI2 DMA、DPHY/CSI host、MIPI CFG、PiSP FE）、int 47、clock 22（MIPI0_CFG）。csi1: 0x40128000/0x4012c000/0x40138000/0x4013c000、int 48、clock 23（MIPI1_CFG）。両方 `status = "disabled"`（overlay が無い。status は見ない） |
| sensor の I2C | rp1 の子 `snps,designware-i2c` | CAM1: i2c4 = 0x40080000（int 11、pin 40/41 の function 2）。CAM0: i2c6 = 0x40088000（int 13、pin 38/39 の function 3）。100 kHz、入力 clock_sys 200 MHz |
| 電源 | 根の `regulator-fixed` の `cam1_reg`/`cam0_reg` | rp1_gpio 46 / 34、enable-active-high、startup 20 ms（ov5647 の overlay） |
| RP1 の clock | `raspberrypi,rp1-clocks` | MIPI1_CFG: control 0x0d4、div_int 0x0d8、sel 0x0e0。MIPI0_CFG: 0x0c4/0x0c8/0x0d0。親は xosc 50 MHz（aux 0）だけ、25 MHz に（ENABLE bit 11 を落とす → div_int 2 → AUXSRC 9:5 = 0、SRC = AUX_SEL（MIPI1 だけ mask bit 0）→ ENABLE） |
| DMA の window | `drv_rp1_dma_window` | bus_offset 0x10_0000_0000、`dma-coherent` 無し（RP1 の DMA は cache を見ない） |
| connector | Linux の `ov5647-overlay.dts` | 既定 = csi1 + i2c4 + cam1_reg = 基板の CAM/DISP 1。`cam0` = csi0 + i2c6 + cam0_reg = CAM/DISP 0。driver は 1 → 0 の順に探す（D1）。22 pin の cable が要る（ユーザーの手元にある） |

### A.2 RP1 の CFE の register（csi1 の例。Value(s) referenced: rp1_cfe/csi2.c・cfe.c・dphy.c）

- CSI2-DMA 0x40128000: STATUS 0x000（FS/FE/FE_ACK/LE/LE_ACK の channel ごとの bit、OVERFLOW bit 20、DISCARD_OVERFLOW 21・LEN_LIMIT 22・UNMATCHED 23・INACTIVE 24、書いて clear）、DISCARDS_* 0x008〜0x014（amount 23:0、DT 29:24、VC 31:30、読んで 0 を書く）、IRQ_MASK 0x020（bit 0〜4。STATUS の bit 20〜24 と位置が違う。Linux の既定は 0: **開けない**。INACTIVE を開けると buffer の枯渇のたびに packet ごとの割り込みになり得る、M9）、CTRL 0x024（EOP_IS_EOL bit 0）、CH_CTRL 0x028（DMA_EN 0、mode 2:1（NORMAL 0 = packed、REMAP 1 = 16 bit）、FORCE 3、AUTO_ARM 4、VC 6:5、DT 12:7、IRQ_EN_FS 13、IRQ_EN_FE_ACK 15、PACK_LINE 29、PACK_BYTES 30）、ADDR0 0x02c（address >> 4 の下位、**最後に書く**）、STRIDE 0x030、LENGTH 0x034、DEBUG 0x038（frame 31:16、line 15:0）、ADDR1 0x03c、FRAME_SIZE 0x040（height << 16 | width）、COMP_CTRL 0x044。
- DPHY/CSI host 0x4012c000: VERSION 0x000、N_LANES 0x004、RESETN 0x008、PHY_SHUTDOWNZ 0x040、PHY_RSTZ 0x044、PHY_RX 0x048、PHY_STOPSTATE 0x04c、PHY_TST_CTRL0 0x050（testclr bit 0、testclk bit 1）、PHY_TST_CTRL1 0x054（testen bit 16、testdin 7:0、testdout 15:8）。
- MIPI CFG 0x40138000: CFG 0x004（SEL_CSI bit 0）、INTE 0x02c（CSI_DMA 0、CSI_HOST 2、PISP_FE 4）、INTS 0x034。PiSP FE 0x4013c000 は触らない。
- stride は 16 byte に揃える（`BPL_ALIGNMENT`）。RAW10 packed: 1296 → 1632、640 → 800、2592 → 3248。
- REMAP（16 bit）の値の位置（LSB か MSB 寄せか）は Linux の source に無い → 実機で test pattern の最大値で測り `sample_shift` に（M2）。

### A.3 buffer（DMA の source）

- `drv_dma_device_create_window({address_bits 64, max_segment_size 16 MiB, boundary 0, coherent}, bus_offset, physical_limit)` + `drv_dma_alloc_coherent`（`rp1-ethernet.c:827-842` の型）で連続の run。non-coherent なら uncached の alias（HAL が direct map の line を clean + invalidate）。user の mapping は `VM_DEVICE_UNCACHED_RAM`。物理 = `device_address − bus_offset`（H3）。
- CSI2-DMA は scatter-gather しない（1 frame に address と stride だけ）。frame start の割り込みで次の buffer の address を渡す（二重化の register。Linux の `cfe_prepare_next_job`）。

### A.4 開始・停止・割り込み（Procedure referenced: cfe.c `cfe_start_streaming`/`cfe_stop_streaming`/`cfe_isr`、csi2.c `csi2_start_channel`/`csi2_stop_channel`/`csi2_open_rx`/`csi2_close_rx`、dphy.c `dphy_start`/`dphy_init`/`dphy_stop` の注記）

- 開始（mutex の下、spinlock の外、M3）: CH_CTRL 0 → DEBUG 0 → STATUS の channel の bit を clear → CH_CTRL = DMA_EN | IRQ_EN_FS | IRQ_EN_FE_ACK | PACK_LINE | mode | VC | DT（RAW8 0x2a、RAW10 0x2b、RAW12 0x2c）、FRAME_SIZE → 最初の buffer（LENGTH・STRIDE・ADDR1・ADDR0 の順）→ MIPICFG の CFG = SEL_CSI、INTE = CSI_DMA、IRQ_MASK = 0 → DPHY（rate = link × 2 / 1e6 の整数: 437 / 416 Mbps、hsfreqrange「≤ 449」の行 0b000110 を code 0x44 に << 1。RESETN 0、N_LANES = lanes − 1、PHY_RSTZ 0、PHY_SHUTDOWNZ 0、testclk 1、testen 0、testclr 1 → 15 µs → testclr 0 → 15 µs → transaction → 5 µs → PHY_SHUTDOWNZ 1 → 5 µs → PHY_RSTZ 1 → RESETN 0xffffffff → 10 µs。待ちは `kern_usleep_range`（tick に譲る、1 ms 単位でも害は無い）→ CTRL = EOP_IS_EOL → sensor の set_mode・control の書き直し・stream on。
- 停止（Linux の順、M4）: 状態 STOPPING（割り込みは buffer を動かさない）→ CH_CTRL = FORCE、ADDR0 に 0 を 2 回、STATUS を読み戻す → sensor の stream off → IRQ_MASK 0、INTE 0（DPHY は reset しない: 途中の packet で host を reset すると IDI が壊れる）→ FE_ACK は待たず DEBUG の line counter が 2 回同じ（上限 150 ms）→ buffer を未使用に → sensor の power off。
- 割り込み（level、vector 48、`drv_rp1_irq_establish`）: INTS に CSI_DMA が無ければ返る → STATUS を読んで書き戻す → bit 20〜24 は DISCARDS を読んで 0 を書き統計へ → FS/FE_ACK の同時の 3 通りは Linux と同じ（FE_ACK と FS が同時で FS 済みで無ければ FS が先。FS で前の FE が無ければ前の frame を `FE_UNSEEN` で完了）→ FS: 「今」←「次」、sequence（DEBUG の frame と照合）、時刻、空き列から次を `set_buffer` → FE_ACK: 完了列へ。
- watchdog: STREAM_ON から 500 ms で FS が無い、stream 中に 2 s 無い → PHY_STOPSTATE・STATUS・DEBUG・DISCARDS を log に出して FAULT（枠組みに device ごとの thread を足す）。

### A.5 OV5647（Values referenced: ov5647.c の表）

- I2C 0x36、register は 16 bit の address + 8 bit の値。chip ID: 0x0103 に 1（soft reset）、0x300a/0x300b = 0x56/0x47、0x0103 に 0。電源: regulator on → 20 ms → OE enable（0x3000-0x3002 = 0x0f/0xff/0xe4）→ stream off で LP-11。off: OE disable → SW standby（0x0100 の bit 0 を落とす）→ regulator off。
- mode（全部 RAW10、2 lane、非連続 clock）: 1296×972（87.5 MHz、link 218.5 MHz、HTS 1896、VTS 1435、32.2 fps、2x2 binning、**既定**）、640×480（55 MHz、208.333 MHz、1852、504、58.9 fps）、1920×1080（87.5、218.5、2416、1104、32.8、中央）、2592×1944（87.5、218.5、2844、1968、15.6）。
- **Bayer（M1）**: `ov5647_get_mbus_code` の表 index = hflip | vflip << 1 → {GBRG, BGGR, RGGB, GRBG}: flip なし **GBRG**、hflip BGGR、vflip RGGB、両方 GRBG。
- control: 露光 0x3500-0x3502（行、4〜VTS−4、既定 1000 → 0x00 0x3e 0x80）、gain 0x350a/b（16 = 1.0x、16〜1023、既定 32 → 0x00 0x20）、hflip 0x3821 / vflip 0x3820 の bit 1（hflip は反転の意味）、test pattern 0x503d（1 → 0x80、2 → 0x82、3 → 0x81）、AEC/AGC 0x3503 の bit 0/1（0 で auto。mode の表は 0x03 = manual。既定は両方 on、D6 → metadata の露光は UNKNOWN、M8）、AWB 0x5001、VBLANK 0x380e、HBLANK 0x380c。
- stream on の順: 共通の表 → mode の表 → VC（0x4814 の 7:6）→ 0x0100 = 1 → control の書き直し → 0x4800 = BUS_IDLE | clock gate | line sync → 0x4202 = 0 → 0x300d = 0。off: 0x4800 = gate | idle | clock lane disable、0x4202 = 0x0f、0x300d = 1。Linux の `ov5647_write` は失敗でも 0 を返すが写さない（誤りを返す）。
- I2C の所要時間（L4）: `lpss-i2c.c` の型は transfer ごとに core を off/on し 1 tick 眠るので 1 書き込み 1〜3 ms、STREAM_ON は 0.1〜0.3 s。検出は boot を待たせず thread で。
- IMX219: address 0x10、0x0000 の 16 bit が 0x0219（検出だけ、D5）。

### A.6 sensor の電源と control の保持（M6）

- 電源の状態 OFF ⇄ ON。検出で on → identify → off。STREAM_ON で on → set_mode → 保持した control を全部書く → start。STREAM_OFF で stop → off。control は枠組みが保持し、電源が無い間は保持だけ。AEC/AGC が on の間の露光は `read_control` で hardware から。

### A.7 D2 (a) の I2C の共有と build

- `lpss-i2c.c` の core（約 300 行）を `src/drivers/i2c/dw-i2c-core.c` に抜き、`rp1-i2c.c` が block の map・pin・clock を用意して呼ぶ。x86 の i2c-hid の回帰を T1 に。arm64 の build に `i2c.c`・`dw-i2c-core.c`・`rp1-i2c.c`・`rp1-cfe.c`・`rpicam.c`・`ov5647.c` を `CONFIG_DRIVER_RPICAM` で足し、`rpi5.c` の `kern_platform_refresh_devices` から `drv_rpicam_probe`（`drv_rp1_gpio_init`・`drv_rp1_irq_init` を呼んでから thread）。

### A.8 Linux の出典（CSI の WS で code の block に書く）

`rp1_cfe/csi2.c`（CSI2_* の define、`csi2_set_buffer`、`csi2_start_channel`、`csi2_stop_channel`、`csi2_open_rx`、`csi2_close_rx`、`csi2_isr`、`csi2_isr_handle_errors`）、`csi2.h`（mode）、`cfe.c`（MIPICFG_*、`cfe_start_streaming`、`cfe_stop_streaming`、`cfe_isr`、`cfe_sof_isr_handler`、`cfe_eof_isr_handler`、`cfe_prepare_next_job`、`cfe_buffer_queue`、`BPL_ALIGNMENT`、`sensor_link_rate`）、`cfe_fmts.h`、`dphy.c`（`dphy_transaction`、`dphy_set_hsfreqrange`、`dphy_init`、`dphy_start`、`dphy_stop` の注記）、`pisp_fe.c`（`FE_VERSION`）、`media/i2c/ov5647.c`（表と手順）、`media/i2c/imx219.c`（ID）、`clk/clk-rp1.c`（MIPI の clock）、`pinctrl/pinctrl-rp1.c`（pin の function）、`i2c/busses/i2c-designware-core.h`、`dts/broadcom/rp1.dtsi`・`bcm2712-rpi-5-b.dts`、overlay の `ov5647*.dts*`・`imx219*.dts*`、`dt-bindings/{clock,mfd}/rp1.h`。
