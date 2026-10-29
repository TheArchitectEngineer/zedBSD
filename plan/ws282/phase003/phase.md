<!-- awesome-plan project=zedbsd record=ws282-p003 -->

# ws282-p003 uvc の bind と descriptor の解析、PROBE/COMMIT

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（実装済み・試験未実施。板の dump・仕様の節の照合・実機の descriptor の hex は未実施）

## 実装した事と file

- `include/drivers/usb/usb-uvc-desc.h`・`src/drivers/usb/usb-uvc-desc.c`（c2478ee46）: USB の core を呼ばない純粋な層。VC の header（bcdUVC・clock・baInterfaceNr）と processing unit の id、VS の input header・uncompressed（27 byte）・MJPEG（11 byte）・frame（discrete と continuous）・COLORFORMAT、短い descriptor で解析を止める、GUID の byte 列の比較（YUY2・UYVY・NV12）、stride と image_bytes（§3.3）、PROBE の長さ（bcdUVC だけで 26/34/48）・要求・補正（§4.2.1 の 1〜4）、alternate の表（HS の mult、SS の companion、host が受ける印 = xHCI の encoder の条件）、最大・選択（C ≥ P の最小、無ければ ENOSPC）。
- `include/drivers/usb/usb-uvc.h`（757aba572）: driver と stream の共有の struct（`struct usb_uvc`）と内部の関数。
- `src/drivers/usb/usb-uvc.c`（d95444183）: VC（class 0x0E、subclass 0x01）に bind、VC の extra の解析、最初の VS を `drv_usb_interface_claim`、VS の alternate 0 の解析、transport（alternate 0 の bulk IN か alternate ごとの isoch IN）、dmesg の dump（`usb-uvc: <bus>-<port>: ...`、format・frame・fps・colorformat・alternate と「host accepts up to」）、`drv_cam_register`、source の ops（enum_format・enum_mode（既定の interval を先頭、残りは descriptor の順）・enum_interval（同じ順の絶対位置）・try/set_mode・start・stop・read_counters・host_payload_max）、detach（`drv_cam_unregister` → halt → 解放、halt が転送を取り戻せなければ EBUSY で core が再試行）、quiesce（halt + `drv_cam_source_fault(EIO)`）。
- `src/drivers/usb/usb-uvc-stream.c` の negotiate（2a6f87540）: SET_CUR(PROBE) → GET_CUR(PROBE)（短い答えは残り 0）、答えの format/frame が違えば EINVAL、補正、result を埋め、isoch で P > host の最大なら ENOSPC（result は埋める）、SET_FORMAT は 3 回まで試し COMMIT、答えを保存。
- build（c2aba4ad0）: `Makefile` の `CONFIG_DRIVER_USB_UVC ?= y`、amd64・arm64（usb.c の条件にも UVC）・riscv64（同じ）・pcat（i386、cam.o も）の vmunix.mk、登録 5 か所（pcat.c・rpi4-pcie.c・rp1-usb.c・vf2-pcie.c・virt-pci.c、HID の前）、`config/drivers/usb.drivers` と `menu.list` に `Camera|CONFIG_DRIVER_USB_UVC`。
- devfs（e379f1f3c、Q1 の許し「P6 の branch の中で src/kern/devfs.c に足してよい（cam の名前の判定だけ）」）: `camera_name()`（`cam` + 数字）を 0600 に。

## build のコマンドと結果

- `make -j16 ZEDBSD_CONFIG=config/ci/config-<c>.mk BUILD=build/ws282/p6c-<c> build/ws282/p6c-<c>/vmunix`: amd64・rpi4・rpi5・visionfive2・pcat の 5 つとも rc 0・warning 0、uvc の .o が 4 つずつ出来た（amd64 は vmunix check PASS）。devfs の後に amd64・rpi4 を再び rc 0・warning 0。log は worktree の `build/ws282/logs/`。
- `timeout 60 python3 -I tools/menuconfig.py --help` rc 0（menu.list の読みが通る事だけ）。

## 書いたが流していない試験

- `plan/ws282/tests/uvc-host-test.c` と `run-uvc-host-test.sh`（ae47a997a）: 手で書いた descriptor（VC の header、VS の header、MJPEG 11 byte、frame の discrete と continuous、YUY2 27 byte、COLORFORMAT、切れた frame）で解析・PROBE の補正・alternate の印と選択と ENOSPC、assembler の case（p004b）。`--compile-only` で rc 0（通常と ASan/UBSan、-Werror）。**流していない**。

## 設計と違う判断・足した物

- device の名前・場所: location は `usb <bus>-<port>`、log の tag は `<bus>-<port>`（hub の下の device は port が重なり得る。表示だけ）。
- format は driver が出せる物（cam_format が 0 でない物）だけを列挙する（対応しない GUID は dump に FourCC で出す）。
- 帯域の見積もり（§4.2.1 の 4）は bcdUVC・quirk に依らず P が 0 の時に当てる（design どおり）。

## 未実施・足りない物

- 板の dump（5330 + HDMI capture + webcam）、実機の descriptor の hex（`plan/ws282/tests/descriptors/`）、仕様の PDF での節の確定（source の「Value(s) referenced」の節番号は design の記載のまま、未照合）。
- ENUM で列挙する format の index と uvc の format の対応は host 試験だけ（未実施）。
