<!-- awesome-plan project=zedbsd record=ws282-p004b -->

# ws282-p004b streaming（payload の解析と frame の組み立て、bulk、`camcap`）

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（実装済み・試験未実施）

## 実装した事

- `include/drivers/usb/usb-uvc-payload.h`・`src/drivers/usb/usb-uvc-payload.c`（6e7611894）: design §4.5 の規則 1〜10。最初の payload（last_fid −1）、FID の変化で前の frame を終える（EOF 無しは正常、短い非圧縮は INCOMPLETE）、FID の変化ごとに `drv_cam_frame_begin` を 1 回、slot が無ければ NO_BUFFER を 1 回、同じ FID で slot が無い payload は捨てる、no_fid の device は EOF で FID を反転、ERR は ERROR、PTS/SCR は frame の最初の物（header が短ければ読まず `short_header++`）、溢れは入る分で終えて ERROR + OVERFLOW、MISSED/ERROR の packet は数えて ERROR、MJPEG は SOI 無しで ERROR・末尾 64 byte に EOI 無しで INCOMPLETE、時刻は frame の最初の payload の URB の完了（`CAM_METADATA_APPROXIMATE`）。
  - 空の frame（data の無い slot）は discard して取り直す（cam.c は sequence を take の時に slot に書くので、使い回すと古い番号になる。discard と take で新しい番号にした。空の frame の番号は飛びになる）。
- bulk（同じ file と stream.c）: URB 1 本、`min(P, 64 KiB)` を packet の倍数に、`DRV_USB_URB_SHORT_OK`、timeout 0。payload は URB をまたぐ（最初の URB で header を保存、短い URB か累計が P で終わり、保存した header の EOF）。payload の外の 0 byte の URB は無視。bulk の STALL・IO_ERROR は fault(EIO)（halt の回復は未実装）。**未実地**（design §4.4 のとおり）。
- COMMIT の再送（start）、watchdog（p004a の欄）。
- `userland/tests/camcap/`（7e28d915d）: `-d -f mjpeg|yuyv -w -h -i -b -n -o -a -t -l --probe-only`。GET_INFO を表示、`-l` で format の一覧（camcommon）、SET_FORMAT（ENOSPC は payload と host_payload_max を出して終了 2）、REQUEST_BUFFERS・QUERY・mmap、QUEUE 全部・STREAM_ON、DEQUEUE（timeout）ごとに sequence・bytes・flags・time、最後（`-a` は全部）を MJPEG は DHT を補って `.jpg`、YUYV は BT.601 限定 range で `.ppm`、fps、counters、STREAM_OFF。package は `ZEDBSD_USER_PROGRAMS += camcap` の時だけ（`plan/ws282/tests/config.mk`）。

## 書いたが流していない試験

- `plan/ws282/tests/uvc-host-test.c`（ae47a997a）の assembler の case: FID と EOF、EOF の後の同じ FID、FID だけで終わる短い frame（INCOMPLETE、ERROR でない）、空の frame の discard と番号、壊れた header・MISSED・PTS の短い header と正しい PTS・溢れ・slot 無しで drop 1 回と sequence の飛び、MJPEG の SOI/EOI/padding、no_fid、bulk の URB またぎ・短い URB・累計での終わり・0 byte。`--compile-only` で rc 0。**流していない**。

## 未実施

- 板で最初の frame（P ≤ 1024 の mode）、camcap の実機での動作。camcommon の規約の全文と DHT の表の照合（前の P6 の記録のとおり未レビュー）。
- 偽の URB の層の ring の試験（p004a の欄）。
