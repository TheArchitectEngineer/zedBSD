# WS282 camera の driver の枠組み（/dev/camN）と USB Video Class の driver「uvc」の設計

- 版 3（2026-10-23 P3、ws282-p001）。版 2 の[敵対的 review（2026-10-23）](phase001/review-v2-20261023.md)の指摘 H1〜H4・M1〜M10・L1〜L8 を全部反映した（§12 の表）。範囲は版 2 と同じ: **/dev/camN の枠組み + UVC の driver「uvc」**。CSI（rpicam）は §13 と付録 A。版 1 の review の反映は版 2 の §12（この版では付録 B に写した）。ユーザーの判断 D1〜D7 は決定済み、Q1 の判断 D9・D10 は決定済み（§6.2）、D11・D12 はユーザーに提示中。
- 版 3 で変わった要点（詳細は §12）:
  1. **xHCI の HS の isochronous IN は実機で一度も動いていない**（WS197 p007a1 は host 試験と build まで、p007a2 の 5330 の loopback L2 は未実施、WS197 は incomplete）。p004 の最初に実地の段（p004a）を置き、p005（xHCI の拡張）は WS197 p007a2 の L2 が PASS して main にある事を前提にする。SCO の回帰の「基準」は無いので、p005 の回帰は L2 の道具の再実行と HID にする（H1）。
  2. **帯域の足りない alternate で始めない**: PROBE の `dwMaxPayloadTransferSize`（P）に足りる alternate が今の host に無ければ `TRY_FORMAT`・`SET_FORMAT`・`STREAM_ON` を `ENOSPC` で拒む。HDMI capture の YUYV は p005 の後（p006）で受け入れ、p004 は P ≤ 1024 の mode（MJPEG の低い mode、webcam の小さな YUYV）で受け入れる（H2）。
  3. bulk は endpoint に **URB 1 本**（xHCI は isochronous 以外 1 本しか受けない）、payload は URB をまたぐ。bulk の device が手元に無いので**未実地**と明記（H3）。
  4. isochronous の URB は**投入の順の ring** で処理する（bit の集めでは順が入れ替わる、H4）。URB は 32 KiB で予約（M1）。
  5. frame の組み立ての状態機械を payload header の仕様から書き直し（初期の FID、EOF の後の同じ FID、FID を反転しない device、空の frame、EOF の無い device、短い非圧縮の frame、JPEG の padding、header の長さと PTS/SCR、sequence の数え方。M2）。
  6. `STREAM_ON` のたびに COMMIT、他の open の TRY は所有者がいる間は device に送らない（M3）。停止は枠組みだけが行う（M4）。信号無しは故障にせず DEQUEUE に timeout（M5）。buffer は 0 で埋める（M6）。試験の計画（counter の規則、色の matrix、MJPEG 向けの pattern、DHT の補い、抜去、leak の測り方。M7）。p005 は HS の high-bandwidth だけ、SS は p012（M8）。MJPEG の descriptor の形と GUID の byte 列（M9）。p008 は p007 に依る、suspend の影響（M10）。
- 資料（全部読んだ上で書いた）:
  - zedBSD（版 2 と同じ物に加えて、版 3 で改めて読んだ行）: `include/drivers/usb/usb.h`（`DRV_USB_HCD_CAP_CONCURRENT_URBS` の注記 :79「one active URB per endpoint」、`DRV_USB_TRANSFER_RESERVE_MAX_SIZE` 64 KiB :85、`drv_usb_urb_reserve_transfer` :916、`drv_usb_urb_setup` の timeout :929、isochronous の契約 :980-990、`drv_usb_urb_drain`・`drv_usb_urb_retired` :1044-1055）、`src/drivers/usb/usb.c`（`drv_usb_urb_setup` :2239、timeout 0 は無期限 :2687）、`src/drivers/pci/pci-xhci.c`（`xhci_request_alloc` :3651（予約があればそれを使い、`busy` は EBUSY、`length > capacity` は EMSGSIZE）、`xhci_urb_reserve` :3925（64 KiB まで、2 の冪に揃える）、`xhci_urb_enqueue` :4163（`ep->active != NULL` は EBUSY）、isochronous の enqueue :8160-8240（packet ≤ `max_packet_size & 0x7ff`、64 KiB の境界、suspend 中は EBUSY、halt は EBUSY）、suspend の拒否 :6675）、`include/drivers/pci/pci-xhci-isoch.h`（FIFO 4・32 packet）、`src/drivers/usb/usb-bt.c`（SCO の completion :1833-1861 と thread :1871-1920 の bit の集め、`drv_usb_urb_retired` :2117）、`include/kern/waitq.h`（`WAITQ_INTERRUPTIBLE`）、`include/kern/pmem.h`・`src/kern/pmem.c`（確保は 0 で埋めない）、`src/drivers/audio/audio.c:2059`（確保の直後の `kern_memset`）、`include/uapi/system.h`（event の class は 0x01〜0x80、mask 0xff）、`userland/desktop/sessiond/seat.c:70`（seat は INPUT | USB を購読）、`include/uapi/io-stats.h`（`IO_USB_TRANSFER_RESERVE_ALLOC/FREE`・`IO_USB_BUFFER_ALLOC/FREE`・`IO_XHCI_REQUEST_ALLOC`・`IO_DMA_ALLOC/FREE`）、`userland/base/sysctl/main.c`（io_stats の読み）、`include/kern/thread.h`（`thread_wait`）、`plan/ws197/ws.md`・`plan/ws197/phase007/phase.md`（p007a1 の受け入れは host 試験と build、isoch の実地は p007a2 の L2、「5330 は新しい image の後」）。
  - 仕様（公開、USB-IF）: USB Video Class 1.1（VC/VS の descriptor、PROBE/COMMIT、payload header）、同 Uncompressed Payload（GUID、format/frame の descriptor）、同 Motion-JPEG Payload（format/frame の descriptor）、USB 2.0 §9.6.6（wMaxPacketSize の bit 12:11）、xHCI 1.2 §6.2.3・§4.11.2.3・§6.4.1.3。仕様の PDF は手元に無く、節の番号は **p003 で PDF と照らして確定する**（M9。版 3 では reviewer と同じく Linux の実装と記憶で照合した）。
  - Linux `drivers/media/usb/uvc/uvc_video.c`・`uvc_driver.c`（torvalds/linux master、reviewer が 2026-10-23 に scratchpad に取得した写し。読むだけで commit しない）: frame の組み立て（`uvc_video_decode_start`・`uvc_video_decode_end`・`uvc_video_decode_data`・`uvc_video_decode_bulk`・`uvc_video_validate_buffer`）、alternate の選択（`uvc_video_start_transfer`）、PROBE の補正（`uvc_fixup_video_ctrl`）、descriptor の offset（`uvc_parse_format`）、quirk の表（`uvc_ids`）。**code・comment・構造は写さない**。組み立ての規則は payload header の仕様から自分の言葉で書き、Linux の振る舞いと一致する事を照合に使った。数式（帯域の見積もり）と offset の表を使う所は Guardrail の規則で「Values referenced: linux drivers/media/usb/uvc/uvc_video.c (uvc_fixup_video_ctrl)」のように block ごとに書く（§8）。
- license: zedBSD の code は Zlib。UVC・USB・xHCI・JPEG の仕様の定数は公開の仕様の事実として使い、code の block の前に「Value(s) referenced: USB Video Class 1.1 §x.y」と複数行の comment で書く（file の先頭には書かない、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」）。Linux から取る値・式・表（quirk の表、帯域の式、descriptor の offset）は「Value(s) referenced: linux drivers/media/usb/uvc/<file> (<関数・表>)」、手順は「Procedure referenced:」で block ごとに。p011 で監査。

## 0. 要約

- `/dev/camN` は zedBSD 独自の fixed-width の ioctl の ABI（group `'C'`、`include/uapi/camera.h`）。format は RAW（Bayer）・YUV（YUYV・UYVY・NV12）・RGB・圧縮（MJPEG）を宣言し、**今回は uvc が YUYV と MJPEG を実装**。mode は (format, 幅, 高さ, frame の間隔) の組。buffer は枠組みが確保して `mmap` する形（今回: CPU が書く cached の連続 run、確保の時に 0 で埋める）と、GPU が確保して export した fd を camera が取り込む形（p008、§3.6）。frame ごとに sequence・時刻・`used_bytes`・device の PTS/SCR・flags。control は今回無し（p010）。
- kernel の中は 2 層: **枠組み**（`src/drivers/camera/cam.c`）と **source の driver**（`struct drv_cam_source_ops`。今回は `src/drivers/usb/usb-uvc*.c`）。source は「空いている slot を取って書いて返す」API（§2.1）で frame を渡す。停止・退役は枠組みだけが source の `stop` を呼ぶ（§2.3、§5）。
- uvc: VC の interface に bind し VS を claim、descriptor を解析して dump、`SET_FORMAT` = PROBE + COMMIT、`STREAM_ON` = COMMIT の再送 + alternate の選択 + URB の投入。transport は isochronous（URB 4 本 × 32 packet、32 KiB の予約、投入の順の ring、worker の thread）と bulk（URB 1 本、64 KiB、payload は URB をまたぐ。未実地）。payload header で frame を組み立て（§4.5）、MJPEG はそのまま渡す。
- **xHCI の制約と実地の状況（§4.7）**: 今の encoder は isochronous を FS/HS の 1 transaction（≤ 1024 byte/interval）に限る。しかも **HS の isochronous IN を実機で流した記録は zedBSD に無い**（WS197 p007a1 は host 試験と build、p007a2 の L2 は未実施）。p004a で最初に実地を確かめ、帯域の拡張（p005、HS の high-bandwidth だけ）は WS197 p007a2 の L2 の後。帯域の足りない mode は `ENOSPC` で拒む。
- 最初の device は手元の HDMI capture（MJPEG の低い mode から）と、買う webcam（D12。P ≤ 1024 の YUYV の mode を持つ物）。p006 で PC の既知の画面（`pattern.html`、D11）を `camcheck` が判定する。
- Phase: p002 枠組み（6〜8 LW）、p003 uvc の descriptor と PROBE/COMMIT（3〜4）、p004a xHCI の HS isoch IN の実地（2〜3）、p004b streaming と `camcap`（5〜7）、p005 xHCI の HS high-bandwidth（4〜5、WS197 p007a2 の後）、p006 画素の試験と堅牢化（3〜4）、p007 Pi 5・Pi 4・VF2（1.5〜2.5）、p008 GPU の取り込み（4〜5）、p009 文書・seat・event（1〜2）、p010 control（2〜3、後で）、p011 規約の全文（2）、p012 SS の isochronous（後、device を買ってから、見積もり無し）。合計 **32〜43 LW**（p010 を除く。版 2 の 27〜37 から +5〜6: p004a、p005 の増、p006 の増、p007 の quirk の調べ）。

## 1. 責務と境界

| 層 | 場所 | 責務 | 持たない物 |
| --- | --- | --- | --- |
| user の ABI | `include/uapi/camera.h` | `/dev/camN` の ioctl・mmap・poll の契約。format・mode・buffer・control・metadata の記述。layout は `_Static_assert` で固定 | hardware の名前・USB の descriptor |
| 枠組み（camera core） | `src/drivers/camera/cam.c`、`include/drivers/camera/cam.h` | cdev の登録と退役（hotplug）、open の所有、format の問い合わせの中継と検査、buffer の確保（0 埋め）・mmap・import・queue・dequeue（timeout）・poll、stream の状態機械、**source の `stop` の唯一の呼び手**、frame の完了と metadata、control の中継、counters、FAULT | USB、画素の解釈（複写・変換をしない） |
| source の driver: uvc | `src/drivers/usb/usb-uvc.c`（bind・VC・枠組みへの登録・detach・quiesce）、`usb-uvc-desc.c`（VS の descriptor の解析、host で試験）、`usb-uvc-stream.c`（PROBE/COMMIT、alternate、URB の ring、worker、payload の組み立て、host で試験）、`include/drivers/usb/usb-uvc.h`・`usb-uvc-desc.h` | USB の interface の claim、alternate の選択、URB、payload header の解析、frame を slot に書く、halt（URB と worker の停止） | buffer の所有、user の ABI、停止の判断 |
| 将来の source: CSI の receiver + sensor | 付録 A | — | — |

境界の原則:
- 枠組みは source を関数表（§2.2）でしか知らない。source は枠組みの slot の API だけで frame を渡す。
- 枠組みは画素に触らない。YUYV の変換・MJPEG の展開（と DHT の補い）・debayer は userland（`camcap`・`camcheck`・WS285 の shader）。
- 眠る物（USB の control transfer、alternate の切り替え、buffer の確保、URB の drain、worker の join）は `device->mutex` の下・spinlock の外。spinlock の下は slot の列の操作と counters と admission だけ。URB の完了の callback は xHCI の command gate の下で走り得る（usb.h:980-990）ので印を付けて thread を起こすだけ。
- source の `stop` は枠組みだけが呼ぶ（`STREAM_OFF`・所有者の close・`drv_cam_unregister`・FAULT からの回復）。source は自分では止めず、続けられない時は `drv_cam_source_fault` で枠組みに知らせる（M4）。

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
	uint32_t clock_hz;				/* device の PTS の clock（UVC の dwClockFrequency）。0 なら無し */
	/* DRV_CAM_SOURCE_DMA の時だけ（付録 A の H3 の形）: */
	struct drv_dma_device *dma;
	uint64_t dma_bus_offset;
	uint64_t dma_physical_limit;
};

int drv_cam_register(const struct drv_cam_device_desc *desc, struct drv_cam_device **result);
/*
 * 退役（USB の detach）。枠組みが device->mutex を取り、STREAMING なら source の stop を 1 回呼び、
 * GONE にして（新しい ioctl・mmap・close の経路は ENODEV）、DEQUEUE で待つ thread を起こし（ENODEV）、
 * 進行中の ioctl・mmap・close（admission）が 0 になるのを待ち、cdev を unregister する。
 * 戻った後、枠組みは source の ops を二度と呼ばない。source はこの後に自分の構造体を解放してよい。
 * buffer と device の構造体は最後の open・mapping の release の後に cdev の finalizer が解放する。
 */
void drv_cam_unregister(struct drv_cam_device *device);

/*
 * frame の受け渡し（source → 枠組み）。眠らない（device->lock の中）。どの context でも呼べる。
 * take: 空き列の先頭の slot を ACTIVE にして返す。無ければ EAGAIN。STOPPING・FAULT・GONE でも EAGAIN。
 * done: ACTIVE の slot を完了列へ（used_bytes、info の時刻・flags・PTS/SCR）、DEQUEUE と poll を起こす。
 * discard: ACTIVE の slot を空き列の先頭へ戻す（frame の途中で止めた）。
 * sequence は枠組みが持ち、source は frame の始まり（FID の変化）ごとに drv_cam_frame_begin で 1 つ進める（§3.5）。
 */
struct drv_cam_frame_info {
	uint64_t timestamp_ns;		/* frame の最初の payload を見た時刻。0 なら枠組みが done の時刻を入れる */
	uint32_t flags;			/* CAM_FRAME_ERROR・CAM_FRAME_INCOMPLETE・CAM_METADATA_PTS・CAM_METADATA_SCR */
	uint32_t device_pts;		/* UVC の PTS（device の clock、32 bit） */
	uint64_t device_scr;		/* UVC の SCR（48 bit） */
};
uint32_t drv_cam_frame_begin(struct drv_cam_device *device);				/* 新しい frame: sequence を 1 つ進めて返す */
int drv_cam_slot_take(struct drv_cam_device *device, uint32_t sequence, struct drv_cam_slot **slot);
void *drv_cam_slot_address(const struct drv_cam_slot *slot);		/* CPU が書く address（cached。DMA の source は NULL） */
uint64_t drv_cam_slot_bus_address(const struct drv_cam_slot *slot);	/* DMA の source 用 */
size_t drv_cam_slot_capacity(const struct drv_cam_slot *slot);		/* image_bytes */
void drv_cam_slot_done(struct drv_cam_device *device, struct drv_cam_slot *slot, size_t used_bytes, const struct drv_cam_frame_info *info);
void drv_cam_slot_discard(struct drv_cam_device *device, struct drv_cam_slot *slot);
void drv_cam_frame_dropped(struct drv_cam_device *device, uint32_t reason);	/* CAM_DROP_NO_BUFFER・CAM_DROP_OVERFLOW・CAM_DROP_ERROR・CAM_DROP_TRANSPORT: counters */
void drv_cam_source_fault(struct drv_cam_device *device, int error);		/* 続けられない: FAULT（poll POLLERR、DEQUEUE EIO）。枠組みは次の STREAM_OFF/close で stop を呼ぶ */
void drv_cam_source_counters_changed(struct drv_cam_device *device);		/* 信号無しなどの状態の変化を poll（POLLPRI）に知らせる、任意 */
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
	uint32_t image_bytes;		/* 1 frame の最大 byte（§3.3） */
	uint32_t payload_bytes;		/* transport の 1 payload transfer の大きさ（UVC の dwMaxPayloadTransferSize。GET_INFO の host_payload_max と比べられる） */
	uint32_t bayer_order;		/* RAW だけ */
	uint32_t sample_shift;		/* RAW の unpacked だけ */
	uint32_t mode_index;
};

/* 全部 process context、枠組みの device->mutex の下（USB の control transfer で眠ってよい）。 */
struct drv_cam_source_ops {
	int (*enum_format)(void *argument, unsigned index, struct cam_format_desc *desc);		/* ENOENT で終わり */
	int (*enum_mode)(void *argument, uint32_t format, unsigned index, struct cam_mode_desc *desc);	/* format ごとの mode */
	int (*enum_interval)(void *argument, uint32_t format, unsigned mode, unsigned index, uint32_t *interval_100ns);	/* 9 個目以降（L4） */
	int (*try_mode)(void *argument, const struct drv_cam_mode_request *request, struct drv_cam_mode_result *result);	/* UVC: PROBE だけ。帯域が足りなければ ENOSPC */
	int (*set_mode)(void *argument, const struct drv_cam_mode_request *request, struct drv_cam_mode_result *result);	/* UVC: PROBE + COMMIT。ENOSPC も */
	int (*start)(void *argument);		/* UVC: COMMIT の再送、alternate の選択（ENOSPC）、URB の予約と投入、worker の開始。空き列には 1 つ以上ある */
	void (*stop)(void *argument);		/* 冪等。URB の cancel と drain、worker の停止、ACTIVE の slot の discard、alternate 0。枠組みだけが呼ぶ */
	int (*control_info)(void *argument, uint32_t id, struct cam_control_info *info);	/* NULL なら全部 UNSUPPORTED */
	int (*get_control)(void *argument, uint32_t id, int64_t *value);
	int (*set_control)(void *argument, uint32_t id, int64_t value);
	int (*read_counters)(void *argument, struct cam_counters *counters);	/* source の分（packet の誤り、missed、信号無し） */
};
```

- `set_mode` は IDLE（buffer の確保前）でだけ呼ばれる。`image_bytes` は buffer の大きさになる（`REQUEST_BUFFERS` が page に揃える）。
- `start` の中で source は URB を投入し worker を起こす。frame は `drv_cam_frame_begin` → `drv_cam_slot_take` → 書く → `drv_cam_slot_done`。`stop` は ACTIVE の slot を全部 `discard` してから戻る。`stop` は冪等（2 回呼ばれても害が無い。M4）。
- 将来の DMA の source（CSI）は `drv_cam_slot_take` を割り込みの中で呼んで `bus_address` を hardware に渡す（付録 A の §A.4）。

### 2.3 hotplug と寿命（M4 の反映）

- `cdev_register_managed(name, rdev, &cam_cdev_ops, device, cam_finalize, &node)`（hidraw.c の型）。登録の reference は枠組みが持ち、`drv_cam_unregister` で `cdev_unregister` + `cdev_release`。finalizer は最後の open と mapping の後に走り、buffer（`kern_pmem_free`）と device を解放する。
- 番号 `cam0`〜`cam3` は枠組みの slot の表（hidraw の `hidraw_slot_claim` の型）: 抜いた device の番号は退役の後に再利用する。`/dev/system` の event（§3.9）を出し、sessiond の seat が挿した時に権限を与えられるようにする（p009）。
- **admission**: ioctl・mmap・close・poll の入口で spinlock の下に数え、`GONE` なら `ENODEV`（poll は `POLLERR | POLLHUP`）。退役の後に来る close は数えずに「device が無い」として戻る（stop は呼ばない: 既に枠組みが止めている）。
- **停止の一本化**: source の `stop` を呼ぶのは枠組みの 4 つの経路だけ（`STREAM_OFF`、所有者の close（mapping が残れば最後の release の後の `file_close`）、`drv_cam_unregister`、FAULT からの `STREAM_OFF`）。どれも `device->mutex` の下で状態を見て、STREAMING か FAULT の時だけ 1 回呼ぶ。
- **uvc の detach（抜去・driver の停止）**: `drv_cam_unregister(device)` を呼ぶだけ（自分で `stop` を呼ばない）。枠組みが mutex の下で `stop` を呼び（uvc の halt: URB の cancel・drain、worker の join、alternate 0 は device が居なければ失敗してよい）、GONE にし、admission の 0 を待って戻る。戻った後、uvc は VS の interface を release し構造体を解放する。
- **uvc の quiesce**（USB の core の suspend・reset の経路、usb.h:315-319「successful quiesce keeps the binding until reset」）: uvc は `uvc_stream_halt()`（§4.3 の halt の手順、内部の flag で冪等）で URB と worker を止め、`drv_cam_source_fault(device, EIO)` で枠組みを FAULT にする（DEQUEUE は `EIO`、poll は `POLLERR`）。ACTIVE の slot は持ったまま。後で枠組みが `STREAM_OFF`（か close）で `stop` を呼ぶと、halt は何もせず（冪等）ACTIVE の slot の discard だけを行う。user は `STREAM_OFF` → `STREAM_ON` で続きを始められる（resume の後）。

## 3. `/dev/camN` の ABI（`include/uapi/camera.h`）

### 3.1 node と権限と所有

- cdev の名前 `cam0`…`cam3`（`CAM_DEVICES_MAX` 4）。
- devfs の mode: **0600 root**。`devfs.c` の `devfs_cdev_inode()` の名前の規則に `camera_name()`（`cam` + 数字）を足して 0600 にする（今は該当しない名前が 0666、devfs.c:583-589）。node が最初に出る p003 の前に Q1 に依頼する。sessiond の seat が `/dev/gpu%u` と同じ型で `/dev/cam%u` を seat の user に渡す（p009）。D4。
- open: 何個でも open できる。問い合わせ（`GET_INFO`・`ENUM_FORMAT`・`ENUM_MODE`・`ENUM_INTERVAL`・`GET_FORMAT`・`QUERY_BUFFER`・`GET_CONTROL_INFO`・`GET_CONTROL`・`GET_COUNTERS`）はどの open でもよい。
- **所有**: `SET_FORMAT` か `REQUEST_BUFFERS`（count > 0）を最初に呼んだ open が所有者になる。所有者がいる間、他の open の `SET_FORMAT`・`REQUEST_BUFFERS`・`QUEUE/DEQUEUE_BUFFER`・`STREAM_ON/OFF`・`SET_CONTROL`・`mmap` は `EBUSY`。**`TRY_FORMAT` も所有者が BUFFERS・STREAMING の間は `EBUSY`**（device に PROBE を送らない、M3）。所有者が IDLE の間（format を設定しただけ）は他の open の TRY は device に送ってよい（`start` が COMMIT を送り直すので所有者の stream に影響しない）。所有は所有者の `REQUEST_BUFFERS` の count 0 か close で手放す。所有者が stream 中に close したら枠組みが stop して buffer を解放する。
- **poll**: 所有者でない open の poll は `POLLIN` を返さない（0 か `POLLERR | POLLHUP` だけ。回り続けない、L4）。
- **mapping が残る間は close が来ない**: `vm_device_create` は file を retain し、最後の mapping の release の後に `file_close` する（`kern/vm/device.c:103・145-149`）。fd を閉じても `munmap` していなければ cdev の close は呼ばれず、所有と stream は続く。host の試験に入れる（§7.1）。
- 抜かれた後（GONE）の ioctl・mmap は `ENODEV`、`poll` は `POLLERR | POLLHUP`。

### 3.2 ioctl の一覧（group `'C'`、`_IOWR` は `uapi/ioctl.h`）

全部の record は `version`（`CAM_ABI_VERSION` 1）と `size`（record の大きさ）で始まり、`reserved` は 0（gpu.h・audio.h と同じ作法）。pointer は持たない。**layout（field の順・幅・padding）は p002 で `include/uapi/camera.h` に書き、`_Static_assert` で大きさを固定し host の試験で確かめる**（L4）。64 bit の field は 8 byte に揃え、padding は明示の `reserved` で埋める。

| 番号 | 名前 | record | 意味 |
| --- | --- | --- | --- |
| 1 | `CAM_GET_INFO` | `cam_info` | `driver`（"uvc"）、`name`、`location`、capability の bit（`CAM_CAP_RAW`・`YUV`・`RGB`・`COMPRESSED`・`IMPORT_GPU`・`EXPORT`・`CONTROLS`）、`max_buffers`（8）、`format_count`、`clock_hz`（PTS の clock）、**`host_payload_max`**（transport が 1 payload transfer に受けられる最大の byte。uvc の isochronous なら今の host が受ける alternate の最大の容量（p005 まで 1024）。bulk なら 0 = 制限なし。H2） |
| 2 | `CAM_ENUM_FORMAT` | `cam_format_desc` | `index` 番目の format: `format`（§3.3）、`flags`（`CAM_FORMAT_SUPPORTED`、`CAM_FORMAT_COMPRESSED`）、`bits_per_pixel`、`bytes_per_group`・`pixels_per_group`、`mode_count` |
| 3 | `CAM_ENUM_MODE` | `cam_mode_desc` | 入力 `format`・`index`。出力: 幅・高さ、`interval_type`（`CAM_INTERVAL_DISCRETE`/`CONTINUOUS`）、`interval_count`（離散の総数。255 まで）と `intervals[8]`（最初の 8 個、先頭が既定）または `interval_min/max/step`、`max_frame_bytes`、`bayer_order`・`native_format`・crop（RAW） |
| 4 | `CAM_GET_FORMAT` / 5 `CAM_SET_FORMAT` / 6 `CAM_TRY_FORMAT` | `cam_format` | 入力: `format`・幅・高さ・`interval_100ns`（0 で既定）。出力: `interval_100ns`、`stride_bytes`、`image_bytes`、**`payload_bytes`**（device が要る 1 payload transfer の byte、UVC の P）、`bayer_order`、`sample_shift`、`mode_index`。幅と高さは mode と一致させる。`SET` は IDLE でだけ（`EBUSY`）、所有を取る。`TRY` は所有を取らない（所有者が BUFFERS・STREAMING なら `EBUSY`）。**`payload_bytes > host_payload_max` なら `SET`・`TRY` とも `ENOSPC`**（出力の field は埋めて返す: app はどの mode が host で取れるかをこれで知る。H2） |
| 7 | `CAM_REQUEST_BUFFERS` | `cam_request_buffers` | `count`（0 で解放と所有の放棄、最大 `max_buffers`。確保できた数を返す。mmap が残っていれば `EBUSY`。**STREAMING の間は `EBUSY`**）、`memory`（`CAM_MEMORY_DEVICE`・`CAM_MEMORY_IMPORT`）、`bytes`（out）。確保した buffer は **0 で埋める**（M6） |
| 8 | `CAM_QUERY_BUFFER` | `cam_buffer` | `index` の buffer の `mmap_offset`（= `index × roundup(image_bytes, PAGE)`）、`bytes`、`flags`（`CAM_BUFFER_QUEUED`・`ACTIVE`・`DONE`・`MAPPED`・`IMPORTED`）。どの open でも |
| 9 | `CAM_QUEUE_BUFFER` | `cam_buffer` | `index` の buffer を空き列に入れる。未使用の buffer だけ受ける（空き列・ACTIVE・完了列にある物は `EBUSY`）。`CAM_MEMORY_IMPORT` なら `fd` も: 最初の 1 回で取り込み、以後は `fd` が −1 なら同じ物。別の fd は `EBUSY` |
| 10 | `CAM_DEQUEUE_BUFFER` | `cam_buffer` | 完了した buffer を順に返す。無ければ待つ。入力 **`timeout_ms`**（0 = 無期限、`O_NONBLOCK` なら待たず `EAGAIN`）。出力: `sequence`、`timestamp_ns`、`used_bytes`、`metadata`、`flags`。待ちは `STREAM_OFF` で `EINVAL`、FAULT で `EIO`、退役で `ENODEV`、timeout で `ETIMEDOUT`、signal で `EINTR`（`WAITQ_INTERRUPTIBLE`）で返る（M5・L4。「close で返る」は無い: 同じ file で眠る thread が居る間は close の op が来ない） |
| 11 | `CAM_STREAM_ON` / 12 `CAM_STREAM_OFF` | なし（`_IO`） | ON には空き列に 1 つ以上の buffer が要る（`EINVAL`）。ON は source の `start` を呼ぶ（uvc は COMMIT の再送 → alternate → URB。帯域が足りなければ `ENOSPC`）。OFF は `stop` を呼び、空き列・ACTIVE・完了列（未 DEQUEUE）の buffer を全部「未使用」に戻す。FAULT からも OFF で BUFFERS に戻る |
| 13 | `CAM_GET_CONTROL_INFO` | `cam_control_info` | `id` の最小・最大・刻み・既定・`flags`。uvc は今回全部 UNSUPPORTED |
| 14 | `CAM_GET_CONTROL` / 15 `CAM_SET_CONTROL` | `cam_control` | `id`・`value`（int64）。`SET` は所有者だけ。今回は `EOPNOTSUPP`（p010） |
| 16 | `CAM_GET_COUNTERS` | `cam_counters` | frame 数、落とした frame（buffer 無し・溢れ・誤り・transport の別）、source の誤り（packet の MISSED・ERROR、壊れた header、短い header）、sequence の飛び、**`no_signal`**（最後の frame から `watchdog` の時間が過ぎた回数）と `last_frame_age_ms`（M5） |
| 17 | `CAM_EXPORT_BUFFER` | — | 予約（§3.6 の案 B）。`ENOTTY`、`CAM_CAP_EXPORT` は 0 |
| 18 | `CAM_ENUM_INTERVAL` | `cam_interval_desc` | 入力 `format`・`mode_index`・`index`。出力 `interval_100ns`。`ENUM_MODE` の `intervals[8]` に入らない 9 個目以降を引く（L4） |

`poll`: `POLLIN` は dequeue できる frame がある時（所有者だけ）。`POLLERR` は FAULT、`POLLHUP` は退役。`read`/`write` は `EOPNOTSUPP`。

### 3.3 format の宣言

```c
/* RAW（Bayer）。今回は source が無い（付録 A）。 */
#define CAM_FORMAT_RAW8		0x0101U
#define CAM_FORMAT_RAW10P	0x0102U
#define CAM_FORMAT_RAW10	0x0103U
#define CAM_FORMAT_RAW12P	0x0104U
#define CAM_FORMAT_RAW12	0x0105U
#define CAM_FORMAT_RAW16	0x0106U
/* YUV。YUYV = 画素 2 つが Y0 U Y1 V の 4 byte（UVC の YUY2）。UYVY = U Y0 V Y1。NV12 = Y の面 + UV の面。 */
#define CAM_FORMAT_YUYV		0x0201U
#define CAM_FORMAT_UYVY		0x0202U
#define CAM_FORMAT_NV12		0x0203U
#define CAM_FORMAT_YUV420P	0x0204U
/* RGB（宣言だけ）。 */
#define CAM_FORMAT_RGB565	0x0301U
#define CAM_FORMAT_RGB888	0x0302U
#define CAM_FORMAT_XRGB8888	0x0303U
/* 圧縮（frame は可変長、used_bytes）。MJPEG = frame ごとに 1 つの JPEG（SOI〜EOI、DHT は無い事がある）。H264 は宣言だけ。 */
#define CAM_FORMAT_MJPEG	0x0401U
#define CAM_FORMAT_H264		0x0402U
```

- uvc が「使える」と答える物: uncompressed の format descriptor の `guidFormat`（16 byte）を **byte 列で**比べる（M9。GUID の Data1〜Data3 は little endian で並ぶ。Value(s) referenced: USB Video Class Uncompressed Payload §2.2 の GUID の表、節は p003 で確定）:
  - YUY2 `{0x59,0x55,0x59,0x32, 0x00,0x00, 0x10,0x00, 0x80,0x00, 0x00,0xAA,0x00,0x38,0x9B,0x71}` → `CAM_FORMAT_YUYV`
  - UYVY `{0x55,0x59,0x56,0x59, 0x00,0x00, 0x10,0x00, 0x80,0x00, 0x00,0xAA,0x00,0x38,0x9B,0x71}` → `CAM_FORMAT_UYVY`
  - NV12 `{0x4E,0x56,0x31,0x32, 0x00,0x00, 0x10,0x00, 0x80,0x00, 0x00,0xAA,0x00,0x38,0x9B,0x71}` → `CAM_FORMAT_NV12`
  - MJPEG の format descriptor（GUID は無い、§4.1）→ `CAM_FORMAT_MJPEG`。他の GUID（I420・Y8 など）と frame based（H.264）は log に出して「対応しない」。
- 非圧縮の stride = 幅 × byte/画素（UVC は行の padding をしない）、`image_bytes = stride × 高さ`（NV12 は × 3/2）。**`dwMaxVideoFrameSize` は非圧縮では frame descriptor の `dwMaxVideoFrameBufferSize` で置き換える**（PROBE の答えを信じない。L1）。それが計算と違えば log に出し、大きい方を `image_bytes` にする。
- MJPEG の `image_bytes` = PROBE の `dwMaxVideoFrameSize`。0（UVC 1.0 の device に多い）か `幅 × 高さ / 4` 未満なら frame descriptor の `dwMaxVideoFrameBufferSize`、それも小さければ `幅 × 高さ × 2`（R4）。user には `used_bytes` で実際の長さを返す。
- RAW の `bayer_order`・`sample_shift` は付録 A。

### 3.4 buffer の model

- `CAM_MEMORY_DEVICE`、source が `DRV_CAM_SOURCE_CPU_WRITES`（uvc）: 枠組みが `kern_pmem_alloc(roundup(image_bytes, PAGE), PAGE, &run)` で buffer ごとに物理的に連続な run を取り（`vm_device_create` が 1 つの物理 base を取る。audio.c の shadow の ring と同じ型）、**直後に `kern_memset(address, 0, bytes)`**（audio.c:2059 と同じ。古い中身が user に見えない、M6）。kernel の address は direct map（`kern_pmem_to_kernel`、cached）。source の worker は CPU でこの address に書き、user は `mmap(fd, mmap_offset, bytes, PROT_READ)` で cached に見る。device の DMA はこの memory に触らない（USB の data は xHCI の staging から `kern_memcpy` で URB の buffer に写される、`xhci_isoch_finish` pci-xhci.c:8001-8011）ので cache の操作は要らない。
  - 1080p の YUYV（4,147,200 byte）は 4 MB の連続 run × buffer 数。断片化で失敗したら確保できた数を返す（0 なら `ENOMEM`、R6）。
- `CAM_MEMORY_DEVICE`、source が `DRV_CAM_SOURCE_DMA`（将来の CSI）: p002 では実装せず `ENOTSUP`。付録 A の §A.3。
- `CAM_MEMORY_IMPORT`: §3.6（p008）。
- queue は 2 つの列（空き = user が QUEUE した物、完了 = source が done した物）と ACTIVE の slot。slot の状態: UNUSED → (QUEUE) FREE → (take) ACTIVE → (done) DONE → (DEQUEUE) UNUSED、(discard) で ACTIVE → FREE（先頭）。
- 最小の buffer 数は 2（連続の取り込みに 4 を勧める）。`mmap` の範囲は 1 つの buffer の中に収まる事（またがる・はみ出す mmap は `EINVAL`）。

### 3.5 frame の metadata（`struct cam_frame_metadata`、`cam_buffer` に埋め込む）

| field | 意味 | 出所 |
| --- | --- | --- |
| `sequence` | frame の通し番号。**source が新しい frame の始まり（UVC は FID の変化）ごとに 1 つ進める（`drv_cam_frame_begin`）。buffer が取れず落とした frame も進む**ので、飛びの数 = 落とした frame の数（M2-8: 1 か所でしか数えない） | source → 枠組み |
| `timestamp_ns` | frame の最初の payload を worker が見た時刻（単調 counter を ns に）。`CAM_METADATA_APPROXIMATE`（URB の完了の粒度 = HS で 4 ms 以内の遅れ） | source |
| `used_bytes` | 書いた byte（MJPEG は可変） | source |
| `device_pts` | payload header の PTS（frame の中で最初に PTS を持つ payload の値）。`CAM_METADATA_PTS` が立つ時だけ | source |
| `device_scr` | SCR（STC 32 bit + SOF 11 bit）。`CAM_METADATA_SCR` | source |
| `exposure`・`analogue_gain` | CSI の sensor 用（付録 A）。uvc は `UNKNOWN` | — |
| `flags` | **`CAM_FRAME_ERROR`**: frame の間に payload header の ERR、packet の MISSED/ERROR、壊れた header、溢れ、MJPEG で SOI が無い。**`CAM_FRAME_INCOMPLETE`**: 中身が足りない見込み — 非圧縮で `used_bytes < image_bytes`（残りの行は前の frame の画素のまま、M2-5）、MJPEG で末尾 64 byte に EOI（FF D9）が無い（padding を許す、M2-6）。FID の変化で終わった frame（EOF 無し）は**正常**で、これだけでは flag を立てない（M2-5） | source |

### 3.6 GPU（Vulkan）へ複写なしに渡す（p008、WS285 の為）

案 A（GPU が確保・camera が取り込む、D3）を p008 で実装し、案 B（camera が export）は番号の予約だけ。経路と GPU 側の差分は版 2 と同じ:

1. app が Vulkan で export 付きの memory を確保し `vkGetMemoryFdKHR` で fd を得る。libvulkan は `GPU_ALLOCATION_EXPORT`（`libvulkan/memory.c`）で allocation だけの共有を作る。bcm2711 では `render_export` → `bcm2711_shared_create(buffer, NULL)` で `has_image = false`。
2. 枠組みは `CAM_QUEUE_BUFFER` の最初の 1 回で `handle_fd_get(fd, KERNEL_HANDLE_DRIVER)` し、GPU core の新しい関数 `drv_gpu_shared_backing(handle, &backing, &device_identity)` で物理 page の列と cache の方針を得る。既存の `get_scanout_backing` は allocation だけの共有で `ENOTSUP`（share.c:192）なので、`struct drv_gpu_share_ops` に `get_allocation_backing` を足し（`gpu-share.h`）、bcm2711 の `share.c` に `bcm2711_shared_allocation_backing`、`render-device.c` にその配線、`gpu-scanout.h` に `DRV_GPU_BACKING_UNCACHED 8U`、`gpu.c` に `drv_gpu_shared_backing`。約 80〜100 行、WS031/WS141/WS279 の source。**D9（Q1 決定）: WS282 の担当が書き、Q1 が merge する。**
3. 枠組みは連続（`CONTIGUOUS`）・MMIO でない・`UNCACHED`・大きさ（`bytes ≥ image_bytes`）を確かめ、満たさなければ `EXDEV`。handle は slot の解放まで retain。
4. Pi 5 の Vulkan の memory は `bcm2711_buffer_create_uncached` の Normal non-cacheable の run。uvc の worker が CPU で書く時は、枠組みが自分の uncached の alias（`kern_pmem_map_uncached`）を slot の address にする。cached の backing は `EXDEV`。GPU の job が読み終える前に同じ buffer を `QUEUE` しない契約（app は `vkWaitForFences` の後に `QUEUE`、R8）。
5. uncached の alias への CPU の書き（YUYV 1080p で 4 MB/frame）は cached より遅い（Pi 5 で測る。遅ければ cached の staging に組んでから `memcpy` する 2 段にする）。

案 B `CAM_EXPORT_BUFFER` は番号 17 の予約だけ。

### 3.7 control の id

- 今回は無し。ABI の id は予約: 1〜10 は sensor（付録 A）、32 以降は UVC の processing unit と camera terminal（brightness 32、contrast 33、hue 34、saturation 35、sharpness 36、gamma 37、white balance temperature 38・auto 39、backlight 40、gain 41、power line frequency 42、exposure（CT）48・auto 49、focus 50・auto 51、zoom 52）。p010。

### 3.8 拡張の口

- RAW と DMA の source は付録 A。ISP の制御の ioctl は番号 64 以降を予約。複数の camera: uvc は device ごとに `camN`。H.264 は宣言だけ。

### 3.9 hotplug の event（L3）

- `/dev/system` の event は **新しい class `KERN_SYSTEM_EVENT_CAMERA 0x00000100U`**（subject `"cam0"`、ADD/REMOVE）。`include/uapi/system.h` の `KERN_SYSTEM_EVENT_CLASSES` を `0x000001ffU` に広げる。INPUT で出すと input の利用者（libkeiland の input の探し直しなど）が `/dev/input` の走査を無駄にし、USB で出すと USB の device の数だけ seat が走査するので、別の class にした。
- 変更は `include/uapi/system.h`（1 行 + mask）と `userland/desktop/sessiond/seat.c`（`SEAT_EVENT_CLASSES` に `CAMERA`、`/dev/cam%u` の give/restore）: 他の WS の source なので **p009 で Q1 に依頼**。p003〜p008 の間は event 無しで動く（seat は無く、root が `camcap` を使う）。

## 4. uvc の driver の方式

### 4.1 bind と descriptor（Value(s) referenced: USB Video Class 1.1 §3.7（VC）・§3.9（VS）・§A（定数）、Uncompressed Payload §3.1.1〜3.1.2、Motion-JPEG Payload §3.1.1〜3.1.2。節は p003 で PDF と照らす）

- `drv_usb_driver` の id: interface class 0x0E（CC_VIDEO）、subclass 0x01（SC_VIDEOCONTROL）。VC の interface に bind し、attach で:
  1. VC の extra descriptor（CS_INTERFACE 0x24）を `drv_usb_host_interface_extra` で読む: VC_HEADER（subtype 0x01: `bcdUVC` at 3（LE16）、`wTotalLength` 5、`dwClockFrequency` 7（LE32）、`bInCollection` 11、`baInterfaceNr[]` 12〜）、VC_INPUT_TERMINAL（0x02）、VC_OUTPUT_TERMINAL（0x03）、VC_SELECTOR_UNIT（0x04）、VC_PROCESSING_UNIT（0x05、`bmControls` at 8、`bControlSize` at 7）、VC_EXTENSION_UNIT（0x06）。p003 は header と unit の id を記録するだけ（control は p010）。
  2. VS の interface（`baInterfaceNr` の最初の 1 つ）を `drv_usb_interface_claim(vc, vs)` で claim する（usb-bt の SCO の interface と同じ作法。EBUSY なら attach を ENODEV で終える）。
  3. VS の alternate 0 の extra descriptor を `usb-uvc-desc.c` で解析（offset は全部 descriptor の先頭から。長さが足りない descriptor は log に出して解析を止め、そこまでの format で動く）:
     - VS_INPUT_HEADER（0x01、≥ 13 byte）: `bNumFormats` 3、`wTotalLength` 4、`bEndpointAddress` 6、`bmInfo` 7、`bTerminalLink` 8、`bStillCaptureMethod` 9、`bTriggerSupport` 10、`bTriggerUsage` 11、`bControlSize` 12、`bmaControls` 13〜。
     - VS_FORMAT_UNCOMPRESSED（0x04、**27 byte**）: `bFormatIndex` 3、`bNumFrameDescriptors` 4、`guidFormat` 5〜20、`bBitsPerPixel` 21、`bDefaultFrameIndex` 22、`bAspectRatioX` 23、`bAspectRatioY` 24、`bmInterlaceFlags` 25、`bCopyProtect` 26。
     - **VS_FORMAT_MJPEG（0x06、11 byte。M9）**: `bFormatIndex` 3、`bNumFrameDescriptors` 4、`bmFlags` 5（bit 0 = fixed size samples）、`bDefaultFrameIndex` 6、`bAspectRatioX` 7、`bAspectRatioY` 8、`bmInterlaceFlags` 9、`bCopyProtect` 10。GUID と bBitsPerPixel は無い。uncompressed の offset で読まない。
     - VS_FRAME_UNCOMPRESSED（0x05）と VS_FRAME_MJPEG（0x07、同じ形、≥ 26 byte）: `bFrameIndex` 3、`bmCapabilities` 4、`wWidth` 5、`wHeight` 7、`dwMinBitRate` 9、`dwMaxBitRate` 13、`dwMaxVideoFrameBufferSize` 17、`dwDefaultFrameInterval` 21、`bFrameIntervalType` 25（0 = 連続: `dwMinFrameInterval` 26、`dwMaxFrameInterval` 30、`dwFrameIntervalStep` 34。n = 離散 n 個: `dwFrameInterval[i]` 26 + 4i。長さは 26 + 4n か 38）。
     - VS_COLORFORMAT（0x0D、6 byte: `bColorPrimaries` 3、`bTransferCharacteristics` 4、`bMatrixCoefficients` 5。記録して dump に出す: `camcheck` の matrix の推定の参考（M7-2））、VS_FORMAT_FRAME_BASED（0x10）/VS_FRAME_FRAME_BASED（0x11、宣言だけ）。他の subtype は長さで飛ばす。
  4. 解析の結果を `dmesg` に dump する（p003 の受け入れ）: `usb-uvc: 1-3: UVC 1.00, clock 1000000 Hz, VS interface 1, endpoint 0x81 isochronous`、`usb-uvc: 1-3: format 1 MJPEG (default frame 1): 1920x1080 30 60 fps, …`、`usb-uvc: 1-3: format 2 YUY2 16 bpp: 1920x1080 5 fps, …`、`usb-uvc: 1-3: colorformat primaries 1 transfer 1 matrix 4`、`usb-uvc: 1-3: alternates 1..6 packet 128 256 512 1024 2048x2 3072x3 (host accepts up to 1024)`。
  5. transport: VS の alternate 0 に endpoint があれば **bulk**。無ければ **isochronous**（alternate 1 以降に isochronous IN が 1 つずつ。alternate 0 は zero-bandwidth で usb.c が endpoint を作らない）。各 alternate の `wMaxPacketSize`（bit 10:0 の size、bit 12:11 の mult）と、SS なら companion の `bytes_per_interval` を表にし、**今の host が受ける物に印を付ける**（§4.3 の候補の絞り。L5）。
  6. `drv_cam_register`（`driver_name "uvc"`、`device_name` = product string、`location` = bus と port の path、`clock_hz` = `dwClockFrequency`）。
- 同じ device の UAC（audio）の interface は触らない。
- detach・quiesce: §2.3。

### 4.2 PROBE/COMMIT の交渉（Value(s) referenced: UVC 1.1 §4.3.1.1、request の番号 §A.8）

- control の request: `SET_CUR` 0x01、`GET_CUR` 0x81、`GET_MIN` 0x82、`GET_MAX` 0x83、`GET_RES` 0x84、`GET_LEN` 0x85、`GET_INFO` 0x86、`GET_DEF` 0x87。`wValue` = selector << 8（`VS_PROBE_CONTROL` 0x01、`VS_COMMIT_CONTROL` 0x02）、`wIndex` = VS の interface の番号、request type 0x21（out）/0xA1（in）。`drv_usb_control(device, …, timeout 1000 ms)`。
- record の長さは **`bcdUVC` だけで決める**（< 1.1 は 26、< 1.5 は 34、それ以上は 48。`GET_LEN` は使わない。L1）。`GET_CUR` が短く返れば残りは 0 として受ける（UVC 1.0 の device が 26 byte を返す時）。
- `try_mode`（`TRY_FORMAT`）: `bmHint` = 1（frame interval を固定）、`bFormatIndex`・`bFrameIndex`・`dwFrameInterval` を入れて `SET_CUR(PROBE)` → `GET_CUR(PROBE)` で device の答えを読む。COMMIT はしない。答えの `bFormatIndex/bFrameIndex` が頼んだ物と違えば `EINVAL`（丸めない）。答えに §4.2.1 の補正を当て、`payload_bytes` = P を返す。**P が host の受ける最大（§4.3 の候補の最大の容量）を超えれば `ENOSPC`**（答えの field は埋める）。
- `set_mode`（`SET_FORMAT`）: 同じ PROBE の往復の後、補正した答えをそのまま `SET_CUR(COMMIT)`。答えを `uvc->committed` に保存する（`start` が送り直す、M3）。`ENOSPC` の条件は try と同じ（COMMIT の前に判定する）。
- 失敗（stall、timeout）は `EIO`。`SET_FORMAT` の中で 3 回まで試す（最初の PROBE が stall する device、R2）。

#### 4.2.1 PROBE の答えの補正（L1。Procedure referenced: linux drivers/media/usb/uvc/uvc_video.c (uvc_fixup_video_ctrl)、値は仕様の範囲）

1. 非圧縮: `dwMaxVideoFrameSize` を frame descriptor の `dwMaxVideoFrameBufferSize` で置き換える。圧縮で `dwMaxVideoFrameSize` が 0 かつ `bcdUVC < 0x0110`: 同じ置き換え。
2. `dwMaxPayloadTransferSize` の上位 16 bit が全部 1 なら 0 にする（16 bit の符号拡張をする device）。
3. isochronous で `dwMaxPayloadTransferSize` が device 自身の最大の alternate の容量を超えれば、その容量に切り詰める（log に 1 行）。
4. `dwMaxPayloadTransferSize` が 0 なら見積もる: 非圧縮は `幅 × 高さ × bpp / 8 × (10^7 / interval + 1) / 1000`、HS 以上は / 8、+ 12（header）、下限 1024（Values referenced: 同上）。圧縮は device の最大の alternate の容量（圧縮の bit rate は分からないので安全側）。

### 4.3 transport と URB（isochronous）

- **候補の絞り（L5）**: 各 alternate の isochronous IN の 1 interval の容量 C = size × (mult + 1)（HS）。候補は **encoder と同じ条件**（`drv_xhci_endpoint_context_encode` の純粋な検査: FS/HS、mult 0、size ≤ 1024、SS は不可。p005 の後は mult 1〜2 も）を満たす物だけ。候補の最大の容量が `GET_INFO` の `host_payload_max`。
- **選択**: COMMIT の P に対し候補の中で C ≥ P の**最小**の物。**無ければ `ENOSPC`**（`TRY`・`SET`・`start` のどれも。log に `usb-uvc: 1-3: no alternate for payload 3072 (host accepts up to 1024)`）。帯域の足りない alternate で始めない: isochronous は device が速さを決め、host が小さい alternate を選んでも device は fps を落とさず、frame が欠けるか ERR が立つ（H2）。P を下げるのは PROBE の interval・mode を変える側（app が低い mode を選ぶ）。
- `start` の手順: (1) `SET_CUR(COMMIT)` に `uvc->committed` を送り直す（M3。`STREAM_OFF` の alternate 0 や他の open の TRY で device の状態が変わっていても元に戻る）。(2) alternate の選択（上）。(3) `drv_usb_interface_set_alternate(vs, alt)` → endpoint が enable（usb.c:3412-3470。候補を絞ってあるので encoder に拒まれない。拒まれたら `EIO` で止める: 候補の条件の誤り）。(4) URB の予約と投入。(5) worker の開始。
- **URB（M1）**: `drv_usb_urb_alloc(device, endpoint, packets)` で **4 本**（`DRV_XHCI_ISOCH_QUEUE_MAX`）、1 本 **32 packet**（`DRV_XHCI_ISOCH_PACKETS_MAX`。HS の bInterval 1 で 32 × 125 µs = 4 ms、4 本で 16 ms の余裕）。packet の stride = 選んだ alternate の packet の大きさ（≤ 1024）、1 本 = 32 × size ≤ **32 KiB**。`start` で 4 本とも `drv_usb_urb_reserve_transfer(urb, 32 × size)`（`xhci_urb_reserve` は capacity 以上の 2 の冪に揃えた coherent の run を取り（pci-xhci.c:3984-3999）、64 KiB をまたがない。packet が ≤ 1024 で stride が揃っているので 1 packet も 64 KiB をまたがない）。投入のたびの `drv_dma_alloc_coherent` と uncached の alias の作成が無くなる（non-coherent の板で毎秒 250 回）。URB の buffer（`kern_malloc`、32 KiB）は class の側（xHCI が staging から `kern_memcpy` で写す）。`stop` で予約を解放する（`drv_usb_urb_free`）。
  - p005 の 3072 の packet では **1 本 21 packet × 3072 = 64,512 byte**（64 KiB の予約に収まり、21 × 125 µs = 2.6 ms/本、4 本で 10.5 ms の余裕）。2048 なら 32 packet = 64 KiB。
- **投入の順の ring（H4）**: `start` で URB 0→1→2→3 の順に `drv_usb_urb_setup(urb, buffer, bytes, 0, 0（timeout 無し）, uvc_completion, uvc)` → `drv_usb_urb_setup_isochronous(urb, packets, n)` → `drv_usb_urb_submit`。完了の callback は `uvc->lock` の下で `done_mask |= 1 << index` を立て、`uvc->worker` が NULL でなければ起こすだけ。worker は **`next = 0` から順に**、`next` の URB が終わっている間だけ（`done_mask` の bit と `drv_usb_urb_retired(urb)` の両方）処理 → 再投入 → `next = (next + 1) % 4`。番号の小さい順に全部を見る usb-bt の型は、worker が 4 ms 以上遅れて URB 3 と URB 0 が同じ wake で見えた時に 0（新しい）→ 3（古い）の順になり、FID の判定を壊す。ring なら順が保たれる。
- worker（device ごとに 1 本、`kthread_create(uvc_worker, …, SCHED_PRIORITY_DEFAULT)`）: 完了した URB の packet を順に見て（`drv_usb_urb_iso_result`: OK/SHORT は `actual` の byte、MISSED/ERROR は counters と今の frame の ERROR）、§4.5 の組み立てを行い、URB を再投入する。再投入の失敗: `EBUSY`/`EAGAIN`（endpoint の halt の回復中、suspend の準備中、skip wait）は 1 tick 眠って繰り返す（上限 1 s、超えたら fault）。`ENODEV`（切断）は `drv_cam_source_fault(ENODEV)`。`ENOMEM`・`EMSGSIZE`・`EINVAL` は予約の誤り（起きない筈）で `drv_cam_source_fault(EIO)` と log（M1）。
- **watchdog（M5）**: worker は URB の完了のたびに「最後に `done` した時刻」を見る。`max(2 s, 4 × frame の間隔)` を超えて frame が無ければ `no_signal` を数え `last_frame_age_ms` を更新する（故障にしない: HDMI の信号無し・蓋を閉じた PC は正常な待ち）。URB の完了そのものが **2 s** 無い時（isochronous の URB は device が沈黙しても 32 packet で終わるので、これは controller か切断）は `drv_cam_source_fault(EIO)`。
- **halt の手順（`uvc_stream_halt`、冪等。`stop` と quiesce が使う。L6 の順）**: (1) `uvc->lock` の下で `stopping = 1`、worker を起こす。(2) 4 本を `drv_usb_urb_cancel`（isochronous は 1 本の cancel で endpoint の全部が終わる。`EALREADY` でも空になる）→ `drv_usb_urb_drain(urb, 1000 ms)` を 4 本（この間に来る callback は bit を立てて worker を起こすだけ。worker は `stopping` を見て再投入しない）。(3) `thread_wait(worker)` で join（URB は全部 terminal なので join の後に callback は来ない）。(4) `uvc->lock` の下で `worker = NULL`。(5) `drv_usb_interface_set_alternate(vs, 0)`（帯域を返す。device が居なければ失敗してよい）。(6) 予約と URB の解放。drain が timeout したら（xHCI がまだ持つ）URB と予約は解放せず（retained）、log に出す（`stop` は void なので counters に `halt_timeouts`）。
- `stop`（枠組みが呼ぶ）= `uvc_stream_halt` → ACTIVE の slot の `discard`。

### 4.4 transport と URB（bulk。H3 の反映、**未実地**）

- alternate 0 の bulk IN。xHCI は isochronous 以外の endpoint に **URB を 1 本しか受けない**（usb.h:79、`xhci_urb_enqueue` pci-xhci.c:4163 の `ep->active != NULL` → EBUSY）。よって **URB 1 本**を回す: 1 本の大きさは **`min(dwMaxPayloadTransferSize, 64 KiB)` を packet の大きさ（512）の倍数に切り上げ**、`drv_usb_urb_reserve_transfer` で予約、flag `DRV_USB_URB_SHORT_OK`、timeout 0（信号が無い間 NAK が続くのを timeout にしない。止めるのは cancel。M5）。
- payload は URB をまたぐ（Linux の `uvc_video_decode_bulk` と同じ考え）: payload の最初の URB で header を読んで保存し、以後の URB は data だけ。payload の終わりは **短い URB（`actual < 要求）** または **累計が `dwMaxPayloadTransferSize` に達した時**。終わりで保存した header の EOF を見る（§4.5 の「payload の終わり」）。0 byte の URB は payload の途中でなければ無視。
- 完了 → すぐ再投入（callback の中では投入できないので worker。bulk の 1 本は worker が起きるまで device の FIFO が溜める。HS の bulk は 1 本の 64 KiB で約 1.3 ms 分）。worker の遅れで device の FIFO が溢れる危険は p006 で bulk の device があれば測る。
- 複数本が要るなら xHCI の bulk の FIFO（WS197 の source、Q1）を別の Phase に。
- **手元の device（HDMI capture、買う webcam）が bulk かは p003 の dump で分かる。bulk の device が無ければ bulk の経路は host 試験（偽の URB の層）だけで、受け入れに入れず「未実地」と記す。**
- UHCI/EHCI の host（isochronous を持たない）では bulk の device だけ動く。

### 4.5 payload の解析と frame の組み立て（Value(s) referenced: UVC 1.1 §2.4.3.3 payload header。M2 の反映）

payload の先頭: `bHeaderLength`（HL）、`bmHeaderInfo`: bit 0 FID、1 EOF、2 PTS（4 byte）、3 SCR（6 byte）、4 RES、5 STI、6 ERR、7 EOH。data は `payload + HL` から `len − HL` byte。isochronous は packet 1 つ = payload 1 つ（len = `actual`）。bulk は URB の列 = payload 1 つ（§4.4）。

worker の状態: `last_fid`（**−1 = まだ見ていない**）、`current`（ACTIVE の slot か NULL）、`offset`、`error`・`incomplete` の flag、`pts/scr` と有無、`frame_time`、`no_fid`（device の quirk、p006 で手元の device に合わせて）。

規則（payload ごと。「frame を終える」= 下の finish）:

1. **検査**: `len < 2` か `HL < 2` か `HL > len` → `invalid_header++`、`current` があれば `error = 1`。この payload は捨てる（`last_fid` も変えない）。
2. **header の長さと flag（M2-7）**: 必要な長さ = 2 + 4·PTS + 6·SCR。`HL` がそれより短ければ PTS/SCR は無い物として扱い `short_header++`（data を PTS として読まない）。
3. **FID の変化** `fid != last_fid`（最初の payload は `last_fid = −1` なので必ず変化）:
   - `current` があり `offset > 0` → frame を終える（**正常な終わり**。EOF を立てない device や最後の payload が失われた時の為。M2-5: INCOMPLETE は finish の検査で決める）。`current` があり `offset == 0`（空の frame）→ 終えず、同じ slot を次の frame に使う（空の frame を返さない、M2-4）。
   - `sequence = drv_cam_frame_begin()`（**FID の変化 1 回に 1 だけ**、buffer の有無に関わらず。M2-8）。`frame_time` = 今。
   - `current` が無ければ `drv_cam_slot_take(sequence)`。`EAGAIN` → `drv_cam_frame_dropped(NO_BUFFER)`（1 回だけ）、`current = NULL`。
   - `last_fid = fid`。
4. **同期していない時** `fid == last_fid` かつ `current == NULL`（EOF の後の同じ FID の payload、buffer が無くて捨てている frame の続き、溢れた frame の続き。M2-2）: data を捨てる。**`no_fid` の device で EOF が立っていれば `last_fid ^= 1`**（次の payload が新しい frame として見える。M2-3）。終わり。
5. **ERR**（bit 6）: `error = 1`（捨てない。user が判断する）。
6. **PTS/SCR**: frame の中で最初に持つ payload の値を記録する。
7. **data の複写**: `n = len − HL`。`offset + n > capacity` → 入る分だけ写し、`error = 1`、`drv_cam_frame_dropped(OVERFLOW)`、frame を終える（M2: 溢れたら終える。続きの payload は 4 で捨てられる）。そうでなければ `kern_memcpy(address + offset, data, n)`、`offset += n`。
8. **EOF**（bit 1）: `offset > 0` なら frame を終える（header だけの EOF の payload も EOF）。**`no_fid` の device なら `last_fid ^= 1`**。
9. **MISSED/ERROR の packet**（isochronous）: `missed++`/`packet_errors++`、`current` があれば `error = 1`（data が欠けている）。
10. **finish**（frame を終える）: 非圧縮で `offset < image_bytes` → `INCOMPLETE`（下の行は前の frame の画素のまま。M2-5）。MJPEG で先頭 2 byte が `FF D8` でない → `ERROR`。MJPEG で末尾 64 byte に `FF D9` が無い → `INCOMPLETE`（EOI の後に 0 の padding を付ける device を許す。M2-6）。`drv_cam_slot_done(current, offset, info{frame_time, flags, pts, scr})`。`current = NULL`、`offset = 0`、flag を消す。

- bulk の違い: 規則 1〜7 は payload の最初の URB で（header を保存）、以後の URB は 7 だけ、payload の終わり（§4.4）で 8 を保存した header で行う。
- 時刻: 規則 3 の今（worker。URB の完了の粒度、`APPROXIMATE`）。
- **host の試験（`uvc-payload-host-test`）の case**（M2・M7-6）: 最初の payload（last_fid = −1）、FID の反転と EOF の組、EOF の後の同じ FID の payload、EOF 無しの device（FID だけで終わる、INCOMPLETE は立たない）、FID を反転しない device（no_fid、EOF で反転）、空の frame（FID が変わっても slot を使い回す）、短い非圧縮の frame（INCOMPLETE）、MJPEG の SOI 無し（ERROR）・EOI の後の padding（INCOMPLETE が立たない）・EOI 無し（INCOMPLETE）、PTS/SCR の bit と HL の不整合、HL > len・len < 2、ERR の bit、MISSED の packet、溢れ（終えて続きを捨てる）、buffer 無し（sequence は進み drop は 1 回）、sequence の飛びの数 = drop の数、**URB の順（偽の URB の層: 2 本・3 本が同じ wake で終わる、ring の wrap の直後、順が逆に見える完了）**（H4）、**帯域の不足（P > 候補の最大 → ENOSPC）**（H2）、bulk の payload が URB をまたぐ・短い URB・累計で終わる・途中の 0 byte。

### 4.6 counters と診断

- source の counters: `invalid_header`・`short_header`・`missed`・`packet_errors`・`resubmit_failures`・`halt_timeouts`・`no_signal`・`last_frame_age_ms`、frame の drop の種類。xHCI の isoch の統計は WS197 の S9 の counter（`hw.usb`）と io_stats。
- `GET_COUNTERS` で user に、`dmesg` に `start`/`stop` の 1 行（alternate、packet の大きさ、P、URB の本数と予約の大きさ）。

### 4.7 xHCI の isochronous の制約・実地の状況・拡張（p004a・p005、WS197 の source。H1・M8 の反映）

- 今: `drv_xhci_endpoint_context_encode`（`pci-xhci-control.h:164-180`）は isochronous を「FS/HS、1 interval に 1 transaction」に限り、`wMaxPacketSize` の bit 11〜15 が 0 でない（high-bandwidth）と SuperSpeed を拒む。`xhci_isoch_enqueue` も packet の長さを `max_packet_size & 0x7ff` 以下に限る（pci-xhci.c:8160-8170）。よって **1 interval ≤ 1024 byte = HS で 8.19 MB/s**。
- **実地の状況（H1）**: この isochronous の経路は WS197 p007a1（2026-10-11）で host 試験と amd64/arm64 の build まで。実機で流す予定の p007a2 の L2（5330 の HCI Local Loopback、FS・8 packet・63 byte 以下の SCO）は「5330 は新しい image の後」で**未実施**。HS・1024 byte・32 packet の IN は誰も流していない。`xhci_isoch_finish` の packet ごとの複写は bulk の `xhci_completion_finish` とは別の code で、storage・NCM の実績は当たらない。WS197 は incomplete で p007 は in-progress（ベータ3）。
  - **p004a「xHCI の HS isoch IN の実地」**（新設、2〜3 LW）: uvc の最小の URB の ring（§4.3）だけで、手元の device の一番小さい alternate（P ≤ 1024 の mode）を流し、URB ごとの packet の結果（OK/SHORT/MISSED/ERROR の数、actual の byte の分布、payload header の FID の並び）を `GET_COUNTERS` と `dmesg` の 1 行の要約で読む（SSH、T1）。frame はまだ組まない（組み立ては p004b）。目的は「uvc の誤り」と「xHCI の isoch の初めての不具合（TD の照合、skip、BEI、IN の短い packet、Missed Service）」の切り分け。xHCI の側の不具合が出たら **BUG の ticket にして WS197 へ**（D10 の範囲で Q1 が WS282 の担当に直させる事もある）。受け入れ: 300 本の URB（約 1.2 s）で MISSED/ERROR が 1% 未満、FID が 2 つの値を交互に取る、`hw.usb` の isoch の counter に retained・halt が無い。
  - p005 の回帰の「基準」に SCO は使えない（一度も PASS していない）。p005 の回帰は **WS197 p007a2 の L2 の道具（`bt-sco-loopback`）を p005 の前後で流して同じ結果**、と usb-hid（keyboard/mouse）。
- **拡張（p005、HS の high-bandwidth だけ。M8）**: `pci-xhci-control.h`・`pci-xhci.c`、WS197 p007a2 が同じ file を直している最中なので **Q1 が順を決める（WS197 p007a2 の L2 が PASS して main にある事が前提）**。xHCI 1.2 §6.2.3（endpoint context）・§4.11.2.3（Isoch TRB の TBC/TLBPC）・§6.4.1.3 を仕様として参照:
  1. endpoint context: Max Packet Size = size、**Max Burst Size = mult**（HS の追加の transaction の数）、Max ESIT Payload = size × (mult + 1)。`xhci_isoch_enqueue` の packet の上限を ESIT payload に。
  2. **Isoch TRB の TBC/TLBPC**: xHCI 1.0 以上では HS の high-bandwidth でも TD の packet 数に応じて TLBPC（と TBC = 0）を入れる必要がある見込み（Linux の `xhci_get_last_burst_packet_count` の記憶。**未確認**: p005 の最初に xHCI 1.2 の §4.11.2.3 を読んで決める。入れないと 3072 の TD を 1 packet として扱う controller があり得る）。host の試験に TRB の field の期待値。
  3. 64 KiB の境界の検査はそのまま。URB は 21 × 3072（§4.3）。
  4. host の試験: `xhci-isoch-host-test` に HS mult の encode の期待値と TRB の field。
  5. 板: 5330 で 3072 の alternate が選ばれ 720p YUYV ≥ 10 fps・1080p MJPEG 30 fps。回帰: L2 の道具と HID（T1）。
  - 見積もり 4〜5 LW（版 2 の 3〜4 から +1: TBC/TLBPC の読みと試験）。
- **SuperSpeed の isochronous は p012（後）**: 1 TD = 1 TRB の作り（WS197 X4）では 48 KiB の TD に 64 KiB の stride が要り、1 本 8 packet = 512 KiB（予約の上限を超える）、SS の ESIT 125 µs で 4 本 = 4 ms の余裕しか無い。TRB の分割（1 TD に複数の TRB）か FIFO の深さの変更（`DRV_XHCI_ISOCH_QUEUE_MAX`・`PACKETS_MAX` = WS197 の定数）が要り、USB 3 の device を買ってから設計する。R3 の対策も同じ定数の変更（WS197、Q1）。
- 板の xHCI の個性（L8）: Pi 4・VF2 の VL805 と Pi 5 の RP1（DWC3）は Linux に isoch・ring に関わる quirk があり得る（未確認）。p007 の最初に Linux の `xhci-pci.c`・`xhci-ring.c` の該当の quirk（VL805 の TRB の cache など）を読んで表にする段を置く（0.5 LW）。

### 4.8 build の計画（L2 の反映）

- `Makefile`: `CONFIG_DRIVER_CAMERA ?= y`（枠組み。USB に依らない）と `CONFIG_DRIVER_USB_UVC ?= y`（`CONFIG_DRIVER_USB_BT` の隣。UVC が y なら CAMERA も y）、共通の `-D`。
- `platform/amd64/vmunix.mk`: `AMD64_USB_CLASS_SOURCES += src/drivers/usb/usb-uvc.c usb-uvc-desc.c usb-uvc-stream.c`（`ifeq ($(CONFIG_DRIVER_USB_UVC),y)`）、`src/drivers/camera/cam.c` は `CONFIG_DRIVER_CAMERA` の独立の列。`platform/arm64/vmunix.mk`: `ARM64_USB_SOURCES` は列挙した CONFIG のどれかが y の時だけ `usb.c` を入れる（:213 の `filter`）ので、**その列に `CONFIG_DRIVER_USB_UVC` を足す**。`platform/riscv64/vmunix.mk` の `RISCV64_PCI_DEVICE_SOURCES` に同じ。
- driver の登録は 5 か所: `src/kern/platform/pcat.c:283`、`src/drivers/platform/rpi4/rpi4-pcie.c:275`、`src/drivers/platform/rpi5/rp1-usb.c:731`、`src/drivers/platform/visionfive2/vf2-pcie.c:228`、`src/drivers/platform/riscv-virt/virt-pci.c:151`（`drv_usb_hid_driver_register` の隣に `drv_usb_uvc_driver_register()`）。
- `config/ci/config-rpi4.mk`・`config-rpi5.mk`・amd64・vf2 の ci の config に `CONFIG_DRIVER_USB_UVC := y`。
- host の試験: `$(BUILD)/tests/cam-host-test`、`uvc-desc-host-test`（descriptor の byte 列は `plan/ws282/tests/descriptors/*.hex`、MJPEG の format を必ず含む）、`uvc-payload-host-test`（偽の URB の層を含む）、`xhci-isoch-host-test`（p005）。`platform/amd64/vmunix.mk` の host test の型。
- userland: `userland/tests/camcap/`・`userland/tests/camcheck/`・`userland/tests/camcommon/`（DHT の補いなど共通の source、§7.4）。`camcheck` は libjpeg-compat に link。

## 5. 失敗と回復・並行性・資源

- **stream の状態機械**（枠組み、`enum cam_state`）: `IDLE` → `BUFFERS` → `STREAMING`（source の `start` が成功した後に spinlock の下で）→ `STOPPING`（`stop` の間。`take` は `EAGAIN`）→ `BUFFERS`。`FAULT`（`drv_cam_source_fault`）: `poll` が `POLLERR`、`DEQUEUE` が `EIO`、`STREAM_OFF`（`stop` を通す）で `BUFFERS` に戻る。`GONE`（退役）: 全部 `ENODEV`。信号無し（M5）は状態でなく counters。
- **lock**: `device->lock`（spinlock、rank DEVICE）が列・slot の状態・counters・admission の数を守り、source の worker（`take`/`done`/`discard`）と ioctl の両方が取る（`spin_lock_irqsave`）。`device->mutex`（mutex、rank DEVICE）が ioctl 同士（format・buffer・stream の開始/停止・control）と退役・close を直列にする。順序は mutex → spinlock。**spinlock を持ったまま眠らない・譲らない**。source の `set_mode`・`start`・`stop`・control は mutex の下・spinlock の外。slot の address への `kern_memcpy` は lock の外。
- **DEQUEUE の待ち**: `device->mutex` を持たずに、spinlock + `waitq_sleep(…, WAITQ_INTERRUPTIBLE, timeout)` で眠る。起こす側: `done`、`STREAM_OFF`（`EINVAL`）、FAULT（`EIO`）、退役（`ENODEV`）。timeout は `ETIMEDOUT`、signal は `EINTR`。
- **監視**: 枠組みは thread を持たない。uvc の worker が §4.3 の watchdog を見る（信号無し = counters、URB の完了が 2 s 無い = fault）。DMA の source の watchdog は付録 A。
- **所有権と寿命**: buffer は枠組みが所有し、`REQUEST_BUFFERS 0`・close で解放。mmap 中は `vm_device_mapping` が slot を retain する。import した GPU の handle は slot が retain。ACTIVE の slot は source の `stop` が `discard` するまで未使用に戻さない。退役の後は finalizer が全部解放する。
- **USB の失敗**: endpoint の halt は xHCI の worker が回復し、その間の投入は `EBUSY`（uvc の worker が 1 tick ずつ 1 s まで待つ）。切断は URB が `DISCONNECTED` で終わり投入が `ENODEV` → `FAULT`、続いて core の detach → 退役。
- **buffer の枯渇**: 空き列が無ければ frame を捨てる（`NO_BUFFER`、sequence は進む）。
- **close**: 所有者の close は mutex の下で停止 → buffer 解放。mapping が残っていれば close 自体が来ない（§3.1）。退役の後の close は何もしない。
- **shutdown**: 枠組みは何もしない。
- **suspend（M10）**: xHCI は isochronous が流れている間 system の suspend を `EBUSY` で拒む（pci-xhci.c:6675、WS197 X10: 2 秒待って失敗）。**camera を stream している app が居る間、蓋を閉じても 5330 は眠らない**（sleep の失敗が log に出る）。版 3 ではこれを仕様として記録し（`docs/reference/camera.md` に「stream 中は suspend できない。app は蓋・電源の event で STREAM_OFF する」）、suspend の前に枠組みが stream を止める口（power の通知 → `drv_cam_source_fault` → app が `STREAM_OFF` で再開）は Future Work（WS052 の sleep の枠組みとの接続）。uvc の quiesce（§2.3）は resume の経路の備え。

## 6. ユーザー・Q1 の判断

### 6.1 決定済み（2026-10-23「decisionは推しでいいです」）

| # | 問い | 決定 | 今の版での扱い |
| --- | --- | --- | --- |
| D1 | camera の connector | どちらでも | CSI: 付録 A |
| D2 | RP1 の DesignWare I2C の code | (a) core を抜いて共有 | CSI: 付録 A |
| D3 | GPU との buffer の共有の向き | A: GPU が確保・camera が取り込む | §3.6、p008 |
| D4 | `/dev/camN` の権限 | 0600 root + sessiond が seat の user に渡す | §3.1、p003・p009 |
| D5 | IMX219 | 後で | CSI: 付録 A |
| D6 | 既定の mode と露光 | CSI: 付録 A。uvc の既定は device の既定の format/frame/interval | — |
| D7 | RAW10 の既定の memory の形 | 16 bit unpacked、`sample_shift` | §3.3、付録 A |
| D9 | §3.6 の GPU core と bcm2711 の差分を誰が入れるか | **Q1 決定: WS282 の担当が書き、Q1 の merge で入れる** | §3.6、p008 |
| D10 | p005 の xHCI の isochronous の拡張を誰が入れるか | **Q1 決定: WS282 の担当が WS197 p007 と調整して（Q1 が重ならないよう順を決める）入れる。T1 の回帰（BT の SCO の道具、HID）** | §4.7、p005。**版 3 の訂正: WS197 は完了の WS ではなく incomplete（p007 が同じ isoch の code を変えている）。p005 は p007a2 の L2 の PASS と main への統合の後** |

### 6.2 ユーザーに提示中・今回の問い

| # | 問い | 答え・勧め |
| --- | --- | --- |
| D8 | 手元の HDMI capture は debug に使えるか（ユーザー） | **使える、ただし段階がある（版 3 で訂正）。** 安い HDMI capture（MS2109 など）は UVC 1.0/1.1 の device で MJPEG と YUYV を出す（実体は p003 の dump で確定）。isochronous は device が速さを決めるので、host が小さい alternate を選んで fps を下げる事はできず、PROBE の `dwMaxPayloadTransferSize`（P）が host の受ける 1024 byte/interval を超える mode は `ENOSPC` で拒む（H2）。推測: capture の YUYV の mode（1080p 5 fps・720p 10 fps・640x480 30 fps など）はどれも 2〜3 KB/interval を要り **p005 の前は 1 つも使えない**。MJPEG は低い解像度・fps なら P ≤ 1024 の見込み（device の答え次第。1080p 30 fps の MJPEG も 3072 前後を言う事が多い）。よって: p003（dump）と p004a（isoch の実地）は capture で、p004b（最初の frame）は capture の MJPEG の低い mode か webcam の小さな YUYV、capture の YUYV と 1080p は p005 の後（p006）。MJPEG の DHT 無しと画素の試験の注意は版 2 と同じ |
| D11 | HDMI の test pattern を誰が出すか（ユーザー、提示中） | **前提（Q1）: ユーザーの PC の browser が `plan/ws282/tests/pattern/pattern.html` を全画面で出す。** p006 で HTML を提供する（§7.4。mode の切り替え `?mode=yuyv`/`?mode=mjpeg`、counter の帯、色の patch）。T1 の実機の試験は PC の側の操作が要るので、ユーザーが居る時間に合わせる（試験の一覧に載せ、ユーザーが拾う運用）。代わりに試験する板自身の HDMI 出力で pattern を出す案は、zedBSD の HDMI 出力の確かさが別の WS の範囲なので取らない |
| D12 | 買う camera（ユーザー、提示中） | **USB 2.0 の UVC 1.1 の webcam で、YUYV と MJPEG を両方持ち、小さな YUYV の mode（320x240・640x480 の 5〜15 fps、P ≤ 1024）を持つ物**（p004b の受け入れの device。一般的な 720p/1080p の webcam は大抵これを満たすが、買う前に製品の「YUY2 対応」を確かめる）。USB 3 の camera（SS）は p012 の後。Pi の camera module は CSI なので対象外 |
| Q-1（Q1） | 他の WS の source の依頼の順 | p003 の前: `devfs.c` の 0600。p009: `system.h` の CAMERA の class と `seat.c`。p005: `pci-xhci-control.h`・`pci-xhci.c`（WS197 p007a2 の後）。p008: GPU core と bcm2711（D9） |

## 7. 試験の計画（M7 の反映）

### 7.1 host の試験（p002〜p005）

- `cam-host-test`（枠組み、偽の source）: 状態機械・列・slot の状態・metadata・所有（2 つ目の open の `SET_FORMAT`・`REQUEST_BUFFERS`・**`TRY_FORMAT`（所有者が BUFFERS の時）** が EBUSY、所有者の `REQUEST_BUFFERS 0` で手放せる）・`REQUEST_BUFFERS 0` と mmap の残り・**STREAMING の `REQUEST_BUFFERS` が EBUSY**・close の後も mapping が残る間は stream が続き munmap で止まる・STREAM_OFF での未使用への戻し・`QUEUE_BUFFER` の重複・枯渇で sequence が飛ぶ事・`discard`・FAULT の遷移と STREAM_OFF での回復・DEQUEUE の待ちが STREAM_OFF で EINVAL、FAULT で EIO、退役で ENODEV、**timeout で ETIMEDOUT**で返る事・退役の admission（進行中の ioctl が終わるまで `drv_cam_unregister` が待つ、**退役が stop を 1 回だけ呼ぶ、退役の後の close が stop を呼ばない**、M4）・**確保直後の buffer が 0**（M6）・mmap_offset の式・record の version/size/reserved と **`_Static_assert` の layout**（L4）・`ENUM_INTERVAL`・MJPEG の `used_bytes`・非所有者の poll が POLLIN を返さない事。
- `uvc-desc-host-test`: 実機の descriptor の byte 列（p003 で取って `plan/ws282/tests/descriptors/` に。**MJPEG の format を必ず含む**、M9）を解析し、format・frame・interval・alternate の表と「host が受ける alternate」の印が期待と一致、壊れた descriptor で止まらない事、GUID の byte 列の比較。
- `uvc-payload-host-test`: §4.5 の case の全部（偽の URB の層で順と wrap、ENOSPC、bulk のまたぎ）。
- `xhci-isoch-host-test`（p005）: encoder の HS mult の値と TRB の TBC/TLBPC。
- build: amd64・rpi4・rpi5・vf2 の ci の config で warning 0。

### 7.2 QEMU

- QEMU の UVC の device model は無い。QEMU の試験は無し。p005 の回帰（L2 の道具・HID）は実機。

### 7.3 実機（T1。PC の pattern はユーザー、D11）

| 段 | 板と device | 見る物 | 期待 |
| --- | --- | --- | --- |
| p003 | 5330 + HDMI capture（+ webcam） | `dmesg` の `usb-uvc:` の dump、`ls -l /dev/cam0` | 版・format・frame・interval・alternate の表と host の受ける印、`crw------- root`。descriptor の byte 列を tests/ に |
| p004a | 5330 + capture か webcam（P ≤ 1024 の mode） | `camcap --probe-only -n 300`（URB の ring だけ、frame を組まない）、`GET_COUNTERS`、`hw.usb` | §4.7 の受け入れ（MISSED/ERROR < 1%、FID が交互、retained/halt 0）。**xHCI の HS isoch IN の初めての実地の証拠** |
| p004b | 同上 | `camcap -f mjpeg -w … -o f.jpg`（capture の低い mode）または `camcap -f yuyv -w 320 -h 240 -o f.ppm`（webcam）、`camcap -n 300 -o /dev/null`、`camcap -f yuyv -w 1280 -h 720`（capture） | 絵が PC の画面（PNG を `build/review/ws282/` に）、300 frame で sequence の飛び 0・ERROR 0、**720p YUYV は `ENOSPC`**（拒む事が正しい） |
| p005 | 5330 + capture、BT（L2 の道具）、USB の keyboard/mouse | `camcap -f yuyv -w 1280 -h 720 -n 300`、`camcap -f mjpeg -w 1920 -h 1080 -n 300`、`bt-sco-loopback`（p005 の前後）、HID | 3072 の alternate が選ばれ 720p YUYV ≥ 10 fps・1080p MJPEG 30 fps、L2 の結果が前後で同じ、HID が動く |
| p006 | 5330 + capture + pattern の PC（ユーザー） | `camcheck`（§7.4）: YUYV と MJPEG で 100 frame、stop/start 100 回、2 つ目の open | 色が許容内、counter が単調非減少、drop は sequence の飛びで数え 0、**leak: io_stats の alloc − free が前後で同じ**（`IO_USB_TRANSFER_RESERVE_ALLOC/FREE`・`IO_USB_BUFFER_ALLOC/FREE`・`IO_XHCI_REQUEST_ALLOC`・`IO_DMA_ALLOC/FREE`、`sysctl` で読む）、2 つ目の open の `EBUSY`。**stream 中の抜去と再挿入は人の手が要るのでユーザーの試験の一覧へ**（T1 は未実施と記す。software の代わりは無い） |
| p007 | Pi 5・Pi 4・VF2 + capture か webcam | p004b と p006 の短い版（`camcap` 1 frame と `camcheck` 30 frame）、**IRQ の中の staging の複写の時間**（`hw.usb` の counter か 1 行の log、L7） | 3 枚で同じ結果。non-coherent の板で画素が壊れない |
| p008 | Pi 5 + capture + WS279 の V3D | `camvk`（Vulkan の memory に取り込み、compute で YUYV → gray の PGM） | `camcap` と同じ絵、前の frame が読めない |

### 7.4 試験の道具

- `userland/tests/camcommon/`: `camcap`・`camcheck`・WS285 の app が共有する小さな source: **MJPEG の DHT の補い**（frame に DHT（FF C4）が無ければ SOF の前に JPEG の仕様（ITU-T T.81 Annex K.3）の標準の Huffman の表を挿す。libjpeg-compat は DHT が無いと「Huffman table 0x%02x was not defined」で失敗する、M7-4）、YUYV → RGB（BT.601 と BT.709、限定/全 range）、PPM/PNG の書き出し。
- `userland/tests/camcap/`: `/dev/camN` を開き、format・幅・高さ・interval・buffer 数を引数で、N frame 取って最後の 1 枚（または全部）を書く。`--probe-only`（p004a: frame を組まず counters だけ）、`--timeout`（DEQUEUE の timeout）。counters・metadata・fps・`ENOSPC` の時の `payload_bytes`/`host_payload_max` を出す。回帰に残す（master.md の Tools に登録、p009）。
- `plan/ws282/tests/pattern/pattern.html`（D11）: どの PC の browser でも全画面で出せる。`?mode=yuyv`: 上の帯に **100% の原色の patch（赤・緑・青・白・黒・灰 50%、+ 黄・シアン・マゼンタ）**、中に灰の段、下の帯に **frame counter**（16 個の黒/白の正方形で 16 bit の数、`requestAnimationFrame` ごとに +1）と 8 画素の粗い市松。`?mode=mjpeg`: **市松を外し**（1 画素の市松は JPEG の最悪の入力で bit rate を上げ、帯域の問題を pattern が自分で起こす、M7-3）、patch と counter だけ。
- `userland/tests/camcheck/`（p006）: N frame を取り、(1) 最初の frame の白・黒・原色の値から **matrix（BT.601/709）と range（限定/全）を推定して記録**（VS_COLORFORMAT の値も並べる）、その上で patch の許容（YUYV ±8/255、MJPEG ±16/255）を当てる（M7-2）。(2) counter は **単調非減少だけを合否**にし（PC は 60 Hz、capture は 30 fps 以下なので 1 frame に 2 以上進むのが正常。tearing で上下が違う値を写す事もある）、増分の分布（0・1・2・3 以上の数）を記録する。(3) drop は `sequence` の飛びで別に数える（M7-1）。PASS/FAIL と 1 枚の PNG（`build/review/ws282/`）。
- `userland/tests/camvk/`（p008）。

## 8. 引用する値と手順の出典

| 出典 | 何を | どの block |
| --- | --- | --- |
| USB Video Class 1.1（USB-IF、公開）§3.7・§3.9・§A | VC/VS の descriptor の subtype と field、request の番号、control の selector | usb-uvc-desc.c、usb-uvc.c、usb-uvc-stream.c |
| 同 Uncompressed Payload §2.2・§3.1.1・§3.1.2 | YUY2/UYVY/NV12 の GUID の byte 列、uncompressed の format/frame の descriptor | usb-uvc-desc.c |
| 同 Motion-JPEG Payload §3.1.1・§3.1.2 | MJPEG の format（11 byte）/frame の descriptor | usb-uvc-desc.c |
| 同 UVC 1.1 §2.4.3.3 | payload header の bit | usb-uvc-stream.c |
| 同 §4.3.1.1 | PROBE/COMMIT の record（26/34/48） | usb-uvc-stream.c |
| USB 2.0 §9.6.6 | wMaxPacketSize の bit 12:11 | usb-uvc-stream.c、pci-xhci-control.h（p005） |
| xHCI 1.2 §6.2.3・§4.11.2.3・§6.4.1.3 | endpoint context の Max Burst・Max ESIT Payload、Isoch TRB の TBC/TLBPC | pci-xhci-control.h・pci-xhci.c（p005） |
| JPEG（ITU-T T.81）Annex K.3 | 標準の Huffman の表 | userland/tests/camcommon |
| Linux `drivers/media/usb/uvc/uvc_video.c`（`uvc_fixup_video_ctrl`）— Procedure/Values referenced | PROBE の答えの補正（符号拡張、切り詰め、帯域の見積もりの式と 1024 の下限） | usb-uvc-stream.c §4.2.1 |
| Linux `uvc_driver.c`（`uvc_ids`）— **該当があれば p006 で**、Values referenced | device ごとの quirk（NO_FID、PROBE の長さ、帯域） | usb-uvc.c |
| Linux `xhci-pci.c`・`xhci-ring.c`（p007 の前に読む）— Values referenced | VL805・DWC3 の quirk | p007 の表（code に入れるなら pci-xhci.c、WS197 の source） |
| 版 1 の Linux rpi-6.12.y の出典 | 付録 A の表 | （CSI の WS で） |

（節の番号は仕様の PDF で p003 に確定する。Linux の code・comment・構造は写さない。）

## 9. 危険と確かめる点

- R1 **xHCI の帯域と実地**（§4.7）: HS の isoch IN は未実地（p004a で最初に確かめる）。拡張（p005）まで 8.19 MB/s で、P がそれを超える mode は `ENOSPC`。p005 は WS197 p007a2 の後。
- R2 **device の個性**: PROBE の stall、`dwMaxPayloadTransferSize` 0・符号拡張・過大、EOF を立てない、FID を反転しない（`no_fid`）、MJPEG の DHT 無し・EOI の後の padding、`dwMaxVideoFrameSize` の嘘。§4.2.1・§4.5 に仕様の範囲で入れ、device ごとの quirk は p006 で手元の device に合わせて足す（Linux の `uvc_ids` を引く時は出典を書く）。
- R3 **worker の遅れ**: 4 本 × 4 ms = 16 ms（p005 の 3072 では 4 × 2.6 = 10.5 ms）の間に worker が再投入しないと missed service。負荷の高い時の drop を p006 で測り、要れば FIFO と packet の定数の変更（WS197 の source、Q1）。
- R4 **MJPEG の buffer の大きさ**: §3.3 の下限。
- R5 **cache**: uvc の経路は CPU の複写だけで device の DMA は xHCI の staging に閉じる（`xhci_isoch_finish`）。**ただし isoch の staging の複写は non-coherent の板で一度も走っていない**（H1。p007 で確かめ、L7 の時間も測る）。GPU の取り込み（p008）だけ uncached の alias。
- R6 **連続した物理 memory**: 1080p の YUYV で 4 MB × 4。断片化で失敗したら数を減らして返す。
- R7 **hotplug の競合**: 停止は枠組みだけ（M4）、admission に close も数える。host の試験で順序を確かめる。
- R8 **GPU の fence 無し**（§3.6）。
- R9 **capture の scale と色**: patch を大きく、matrix と range は推定して記録（M7-2）。
- R10 **license**: §8。p011 で監査。
- R11 **bulk の未実地**（H3）: device が無い限り host 試験だけ。受け入れに入れない。
- R12 **suspend の失敗**（M10）: stream 中は 5330 が眠らない。文書に書く。

## 10. 選ばなかった案と理由

- **V4L2 互換の ABI**: pointer を含む record・多数の type が要り、既存の device の独自の fixed-width の ABI と合わない。
- **uvc が YUYV を RGB に変換、MJPEG を kernel で展開、read(2)**: kernel で画素に触らない方針。
- **isochronous の URB を callback で処理**: callback は command gate の下で走り得る。worker にした。
- **URB の完了を bit で集めて番号の順に処理（usb-bt の型）**: 1 回の wake で 2 本終わると順が入れ替わる（H4）。投入の順の ring にした。usb-bt の SCO は組み立て直しの resync で吸収しているが順の入れ替えは数えていない（WS197 へ別に報告する価値がある: Q1 へ）。
- **packet を 4096 の stride で並べ 128 KiB の URB**（版 2）: 予約できず（64 KiB まで）投入のたびに coherent を取り直す（M1）。packet ≤ 1024 なら stride = packet で 32 KiB、予約できる。
- **帯域の足りない alternate で始めて fps が落ちるのを待つ**（版 2）: isochronous では device が速さを決め、host の alternate で fps は落ちない。`ENOSPC` で拒む（H2）。
- **bulk に URB 4 本**（版 2）: xHCI は isoch 以外 1 本（H3）。1 本を回し payload は URB をまたぐ。
- **bulk の URB を `dwMaxPayloadTransferSize` の大きさに**（版 2）: frame 全体（数 MB）を言う device で投入のたびに数 MB の coherent。64 KiB で予約。
- **xHCI の拡張を p004 の前に**: 拡張無しでも P ≤ 1024 の mode で経路が全部確かめられ、組み立ての不確かさ（R2）を先に潰せる。さらに p005 は WS197 p007a2 の後でないと衝突する（H1）。ただし版 2 の「MJPEG と小さな YUYV で」は HDMI capture では成り立たない見込みなので、webcam（D12）を p004b の device に。
- **p005 で SS の isoch も**（版 2）: TD の分割と FIFO の定数の変更が要り、device も無い。p012 に分けた（M8）。
- **source の detach が自分で stop を呼ぶ**（版 2）: 枠組みの mutex の外で `STREAM_OFF`・close と競り、二重の join・cancel になる（M4）。停止は枠組みだけ。
- **GET_LEN を優先**（版 2）: 誤った長さを返す device で SET_CUR が stall する。`bcdUVC` で決める（L1）。
- **信号無しを FAULT に**（版 2 の watchdog）: HDMI の入力が無いのは故障でない。counters と DEQUEUE の timeout（M5）。
- **EOF を見ずに FID が変わったら INCOMPLETE**（版 2）: EOF を立てない device で全部の frame が INCOMPLETE になる。中身の検査（長さ・EOI）で決める（M2-5）。
- **MJPEG の末尾 2 byte が EOI でなければ ERROR**（版 2）: padding を付ける device で誤る。末尾 64 byte で探し INCOMPLETE（M2-6）。
- **hotplug の event を INPUT か USB の class で**: 他の利用者の走査が無駄になる。新しい class（L3）。
- **枠組みの watchdog の thread**、**control を最初から**、**CSI を消す**: 版 2 と同じ理由。
- **試験する板自身の HDMI 出力で pattern**: zedBSD の HDMI 出力の確かさが別の WS の範囲。PC の browser（D11）。

## 11. 実装の Phase

| Phase | 内容 | 依存 | 見積もり | 受け入れの条件 |
| --- | --- | --- | --- | --- |
| p002 | 枠組み: `include/uapi/camera.h`（layout と `_Static_assert`）、`include/drivers/camera/cam.h`、`src/drivers/camera/cam.c`（cdev と退役（停止の一本化・admission）、所有、format の中継と `ENOSPC` の通し、0 埋めの buffer と cached の mmap、slot の API と `frame_begin`、queue、DEQUEUE の timeout・`EINTR`・poll、metadata、counters、状態機械、FAULT、`ENUM_INTERVAL`）、`cam-host-test`、`CONFIG_DRIVER_CAMERA` の build | p001 | 6〜8 LW | host の試験が全部 PASS（§7.1）、amd64 と rpi5 の config で warning 0、規約の全文（自分で読み直す） |
| p003 | uvc の bind と descriptor: `usb-uvc.c`・`usb-uvc-desc.c`・`usb-uvc.h`、VC/VS の解析（MJPEG の 11 byte の形、GUID の byte 列、COLORFORMAT）と dump、host の受ける alternate の印、VS の claim、PROBE/COMMIT（`try_mode`/`set_mode`、§4.2.1 の補正、`ENOSPC`）、`drv_cam_register`、build の配管（§4.8、5 か所の登録、arm64 の filter）。**Q1 に依頼**: `src/kern/devfs.c` の `camera_name()` の 0600（約 15 行）。板（5330 + capture + webcam）で dump と `/dev/cam0`、descriptor の byte 列を tests/ に。仕様の PDF で §8 の節を確定 | p002（登録の形。host の試験と並行可） | 3〜4 LW | `dmesg` の dump、`/dev/cam0` が 0600 root、`ENUM_FORMAT/MODE/INTERVAL` と `TRY_FORMAT` が device の値を返し P > 1024 の mode が `ENOSPC`、desc の host の試験 PASS |
| **p004a** | **xHCI の HS isoch IN の実地**（新設、H1）: `usb-uvc-stream.c` の URB の予約・投入の順の ring・worker・halt（§4.3）だけ（frame は組まない）、`camcap --probe-only`。板で §4.7 の受け入れ。xHCI の不具合は BUG の ticket にして WS197 へ（Q1） | p003 | 2〜3 LW | 300 本の URB で MISSED/ERROR < 1%、FID が交互、retained/halt 0、host の試験（偽の URB の層: 順と wrap）PASS |
| **p004b** | streaming: payload の解析と frame の組み立て（§4.5）、COMMIT の再送、`ENOSPC`、watchdog、bulk の経路（host 試験のみ、未実地）、`camcap`・`camcommon`（DHT の補い、YUYV → PPM）。板で最初の frame | p004a | 5〜7 LW | 板で **P ≤ 1024 の mode**（capture の MJPEG の低い mode、webcam の 320x240/640x480 の YUYV）の絵がユーザーの目視で PC の画面、300 frame で sequence の飛び 0・ERROR 0、720p YUYV が `ENOSPC`、payload の host の試験 PASS。bulk は「未実地」と記す |
| p005 | xHCI の HS の high-bandwidth（§4.7: encoder の Max Burst・Max ESIT、TRB の TBC/TLBPC、enqueue の上限、host の試験）、uvc の候補の絞りと 21 packet の URB。**前提: WS197 p007a2 の L2 が PASS して main にある事、Q1 が p007a2 と重ならない順を決める（D10）**。T1 に `bt-sco-loopback`（前後）と HID の回帰 | p004b、**WS197 p007a2（L2 の PASS、main）** | 4〜5 LW | 板で 3072 の alternate が選ばれ 720p YUYV ≥ 10 fps・1080p MJPEG 30 fps、L2 の結果が前後で同じ、HID、host の試験 PASS |
| p006 | 画素の試験と堅牢化: `camcheck`・`pattern.html`（D11）、HDMI capture の YUYV と 1080p MJPEG の受け入れ、drop の測定、stop/start 100 回の leak（io_stats）、2 つ目の open、device の quirk（`no_fid` など、手元の device に合わせて）、bulk の device があればその実地 | p004b（高い mode は p005） | 3〜4 LW | §7.3 の p006 の表。`camcheck` が PASS。抜去はユーザーの一覧へ |
| p007 | Linux の xHCI の quirk の表（VL805・DWC3、0.5 LW）→ Pi 5・Pi 4・VF2 の確認（同じ image の config、T1）、IRQ の中の staging の複写の時間 | p006 | 1.5〜2.5 LW | 3 枚で `camcap` と `camcheck` の短い版が PASS、複写の時間の記録 |
| p008 | `CAM_MEMORY_IMPORT`（§3.6、GPU core と bcm2711 の差分は WS282 の担当が書き Q1 が merge、D9）、`camvk` | p004b、**p007（Pi 5 の部分）**、WS279 の V3D 7.1 が main にあり Vulkan の export が動く事 | 4〜5 LW | `camvk` の PGM が `camcap` と一致、前の frame が読めない、uncached の書きの速さの記録 |
| p009 | `docs/reference/camera.md`（ABI、suspend の制約）、**Q1 に依頼**: `include/uapi/system.h` の `KERN_SYSTEM_EVENT_CAMERA` と mask、`userland/desktop/sessiond/seat.c` の `/dev/cam%u`（give/restore、CAMERA の event）、`camcap`・`camcheck` を Tools に登録 | p006 | 1〜2 LW | 文書、seat の切り替えと挿入で `/dev/cam0` の所有が変わる |
| p010 | control（UVC の processing unit・camera terminal、id 32 以降）。後で | p004b | 2〜3 LW | brightness などが `camcap --control` で効く |
| p011 | 規約の全文の見直し（p002〜p009 の C）と license の監査（§8 の block の注記） | p008・p009 | 2 LW | 違反 0、監査の記録 |
| p012 | SuperSpeed の isochronous（TD の分割か stride、FIFO の定数、WS197 の source）。USB 3 の device を買ってから設計 | p005、device | 未定 | — |

合計 **32〜43 LW**（p002 6〜8、p003 3〜4、p004a 2〜3、p004b 5〜7、p005 4〜5、p006 3〜4、p007 1.5〜2.5、p008 4〜5、p009 1〜2、p011 2。p010 を足せば 34〜46、p012 は未定）。板の試験は p003・p004a・p004b・p005・p006・p007・p008 の末（7 回、うち p007 は 3 枚）。

依存の図: p001 → p002 → p003 → p004a → p004b → {p005（+ WS197 p007a2 の L2）, p006, p010} → p006 → p007 → p008（+ WS279）→ p011、p006 → p009 → p011。外の node（context、この WS の authorized work でない）: WS197 p007a2 の L2、WS279 の V3D、Q1 の devfs/system.h/seat の差分。

## 12. 版 3 の変更（版 2 の review 2026-10-23 の指摘 → 変更 → 節）

| 指摘 | 変更 | 節 |
| --- | --- | --- |
| H1 HS の isoch IN が未実地、p005 の基準が無い | R5・§4.3 の「既存の作法で通っている」を撤回。p004a（実地の段）を新設し 2〜3 LW。D10 を「WS197 は incomplete、p007a2 と同じ code」に訂正し、p005 の前提に p007a2 の L2 の PASS と main への統合、Q1 の順の調整。p005 の回帰は L2 の道具の前後比較と HID（SCO の基準は無い） | §4.7、§6.1 D10、§7.3、§9 R1・R5、§11 p004a・p005 |
| H2 帯域の足りない alternate で始める、D8 の 13 fps | `TRY`・`SET`・`start` で候補に C ≥ P が無ければ `ENOSPC`（log だけで進めない）。`GET_INFO` の `host_payload_max`、`cam_format` の `payload_bytes` で app が知る。D8 を訂正（capture の YUYV は p005 の後の見込み）。p004b の受け入れを P ≤ 1024 の mode に、capture の YUYV と 1080p は p006。D12 の webcam を p004b の device に | §3.2、§4.3、§6.2 D8・D12、§7.3、§11 |
| H3 bulk に 4 本は EBUSY | URB 1 本、64 KiB の予約、payload は URB をまたぐ（header は最初の URB、終わりは短い URB か累計 P）、timeout 0。device が無い限り「未実地」で受け入れに入れない | §4.4、§9 R11、§11 p004b |
| H4 bit の集めで順が入れ替わる | 投入の順の ring（`next` の URB が retired の間だけ順に処理）。host の試験に偽の URB の層（2 本・3 本の同時、wrap）。usb-bt の同じ型は Q1 へ報告 | §4.3、§4.5、§10 |
| M1 128 KiB は予約できない | stride = packet（≤ 1024）× 32 = 32 KiB、`start` で `drv_usb_urb_reserve_transfer`。p005 は 21 × 3072。再投入の `ENOMEM`/`EMSGSIZE`/`EINVAL` は fault | §4.3 |
| M2 組み立ての穴 1〜8 | `last_fid = −1`、同期していない時は FID が変わるまで捨てる、`no_fid` は EOF で反転、空の frame は返さない、FID の変化は正常な終わり、INCOMPLETE は非圧縮の短い frame と MJPEG の EOI 無し（末尾 64 byte）、HL と PTS/SCR の整合、sequence は FID の変化 1 回に 1（`drv_cam_frame_begin`）。全部 host の試験の case | §2.1、§3.5、§4.5、§7.1 |
| M3 COMMIT が SET_FORMAT だけ、他の open の TRY | `start` で COMMIT を送り直す。所有者が BUFFERS・STREAMING の間の他の open の TRY は `EBUSY` | §3.1、§4.2、§4.3 |
| M4 停止の二重、close が admission に無い | 停止は枠組みだけ（4 つの経路、mutex の下で 1 回）。uvc の detach は `drv_cam_unregister` を呼ぶだけ。close・poll・mmap も admission。`stop` は冪等。quiesce は halt + `drv_cam_source_fault` | §1、§2.1、§2.3、§5、§7.1 |
| M5 watchdog が isoch で働かず、信号無しで永久に待つ | 信号無しは counters（`no_signal`・`last_frame_age_ms`）で故障にしない。URB の完了が 2 s 無い時だけ fault。DEQUEUE に `timeout_ms`（`ETIMEDOUT`）と `WAITQ_INTERRUPTIBLE`（`EINTR`）。bulk の URB は timeout 0 | §3.2、§4.3、§4.4、§5 |
| M6 buffer を 0 で埋めない | `REQUEST_BUFFERS` で `kern_memset` 0。host の試験 | §3.4、§7.1 |
| M7 試験の計画 1〜7 | counter は単調非減少だけ、増分の分布を記録、drop は sequence の飛び。matrix と range を最初の frame から推定して記録、patch は 100% の原色。MJPEG の mode では市松を外す。DHT の補いは `camcommon` で `camcap`・`camcheck`・WS285 が共有。抜去はユーザーの一覧。leak は io_stats の alloc − free。host の試験に順・ENOSPC・NO_FID・EOF 無し・空の payload。bulk は未実地 | §7.3、§7.4 |
| M8 p005 の内容と見積もり | HS の high-bandwidth だけ（TBC/TLBPC を仕様で確かめる）、4〜5 LW。SS は p012（後）。R3 の対策は FIFO と packet の定数（WS197、Q1） | §4.7、§9 R3、§11 p005・p012 |
| M9 MJPEG の descriptor、GUID、節の引用 | MJPEG の 11 byte の form を別に書き、uncompressed は 27 byte で offset を列挙。GUID は byte 列。出典を Uncompressed Payload・Motion-JPEG Payload の仕様に直し、節の番号は p003 で PDF と照らして確定と明記。desc の host の試験に MJPEG を必ず | §3.3、§4.1、§8 |
| M10 Phase の依存、suspend | p008 は p007（Pi 5）に依る。H1・H2 の依存を表に。suspend の影響（stream 中は眠らない）を §5 と docs に、止める口は Future Work | §5、§9 R12、§11 |
| L1 PROBE の細部 | 長さは `bcdUVC` だけ、GET_LEN を使わない、短い GET_CUR を受ける。補正（符号拡張、切り詰め、`dwMaxVideoFrameBufferSize`、P = 0 の見積もりは Linux の式を出典付きで） | §4.2、§4.2.1、§8 |
| L2 build の配管 | arm64 の `filter` の列に UVC、5 か所の登録、`CONFIG_DRIVER_CAMERA` を独立の knob | §4.8 |
| L3 event の class | 新しい `KERN_SYSTEM_EVENT_CAMERA`（mask を 0x1ff に）、seat の購読。p009 で Q1 に依頼 | §3.9、§11 p009 |
| L4 UAPI の細部 | layout と `_Static_assert`、`ENUM_INTERVAL`（9 個目以降）、QUERY_BUFFER はどの open でも、非所有者の poll は POLLIN 無し、STREAMING の REQUEST_BUFFERS は EBUSY、DEQUEUE の「close で返る」を削除 | §3.1、§3.2、§7.1 |
| L5 alternate の選び直しの順 | encoder と同じ条件で候補を先に絞り、選んだ alternate だけ SET_INTERFACE。拒まれたら `EIO`（候補の誤り） | §4.1、§4.3 |
| L6 thread の後始末の競合 | halt の順: stopping → cancel → drain → join → `worker = NULL`（lock の下）→ alternate 0 → 解放。callback は `worker` を lock の下で見る | §4.3 |
| L7 hard IRQ の中の複写 | p007 で時間を測り記録。遅ければ WS197 の source | §7.3、§11 p007 |
| L8 板の xHCI の個性 | p007 の最初に Linux の quirk の表を作る段（0.5 LW） | §4.7、§11 p007 |

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

## 付録 B. 版 2 の変更（版 1 の review 2026-10-23 の指摘 → 変更 → 節。参照用）

| 指摘 | 変更 | 節 |
| --- | --- | --- |
| H1 `drv_gpu_shared_backing` が allocation の export で ENOTSUP | bcm2711 の share に `get_allocation_backing`、`gpu-share.h` の op、`gpu-scanout.h` の `DRV_GPU_BACKING_UNCACHED`、gpu.c の `drv_gpu_shared_backing`。担当（D9） | §3.6、§6、§11 p008 |
| H2 取り込んだ GPU の buffer の cache | `UNCACHED` だけ受け、枠組みは自分の uncached の alias で書く。cached は `EXDEV`。fence の契約 | §3.6、§9 R8 |
| H3 driver の buffer の alias と cache | uvc の buffer は CPU だけが書く cached の run。DMA の source の形は flag で分け、p002 は `ENOTSUP` | §3.4、付録 A.3 |
| M1〜M9、M11、L1〜L7 | 版 2 の design.md §12 の表の通り（Bayer の表、`sample_shift`、眠る op は mutex の下、停止の順、所有、電源、kernel の interface、AEC/AGC の metadata、割り込み、build、mmap の offset、mapping と close、sequence、I2C、cable、Phase、その他） | 各節・付録 A |
| M10 devfs の 0600 の順 | p003 の前に Q1 に依頼、seat は p009 | §3.1、§11 |
