<!-- awesome-plan project=zedbsd record=ws282-p005 -->

# ws282-p005 xHCI の HS の high-bandwidth isochronous

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（実装済み・試験未実施。**前提の WS197 p007a2 の L2 の PASS は未達**: WS197 は保留、p007 は planning。branch の上の実装だけで、merge の順は Q1）

## 実装した事

- `include/drivers/pci/pci-xhci-control.h` の `drv_xhci_endpoint_context_encode`（isochronous の枝）: HS の wMaxPacketSize の bit 12:11（追加の transaction の数、3 は予約で拒む）を Max Burst Size（word1 の 15:8）に、Max ESIT Payload と Average TRB Length を packet × (mult + 1) に。bit 15:13 が 0 でない・FS で bit 12:11 が 0 でない・high-bandwidth で packet > 1024 は拒む。SS は今のまま拒む。CErr 0 も今のまま。
- `include/drivers/pci/pci-xhci-isoch.h` の `drv_xhci_isoch_burst_bits`（inline）: Isoch TRB の TBC（control の 8:7）と TLBPC（19:16）。TDPC は ceil(length / packet)（0 byte は 1）、TBC = roundup(TDPC / (MBS + 1)) − 1、TLBPC = 余りが 0 なら MBS、他は余り − 1。HCIVERSION < 1.0 は 0。
- `src/drivers/pci/pci-xhci.c`: controller に `version`（HCIVERSION）を保存。`xhci_isoch_enqueue` の packet の上限を packet × (mult + 1) に、各 TRB の control に burst の bit。64 KiB の境界の検査はそのまま（URB の予約は capacity 以上の 2 の冪で align されるので 21 × 3072 は境界をまたがない）。
- uvc: `uvc_alternate_describe` の「host が受ける」印を HS の mult 0〜2 に広げた（FS の mult・予約の 3 は拒む）。stream の packet を alternate の capacity（packet × transactions = 1 interval の payload）にし、URB の packet 数は min(32, 64 KiB / capacity)（3072 なら 21、1024 なら 32 = 今のまま）。`UVC_ISOCH_TRANSFER_MAX` を `usb-uvc.h` に。

## 仕様の照合

- xHCI 1.2（Intel の公開の PDF を GET で取り、手元で text にして読んだ。PDF は repository に入れない）: §4.11.2.3（TBC・TLBPC の式）、§4.14.1（TDPC: partial・0 byte の packet も 1）、§4.14.2.1.1（HS の Multiplier を Max Burst に、1 microframe に Max Burst + 1 回の transaction、HS の isoch の packet は 1024 まで）、§6.2.3 の表（Max Burst Size は 15:8）、Table 6-34（TBC 8:7、TLBPC 19:16）。design §4.7 の「未確認」の項（HS でも TLBPC を入れるか）は、仕様の式を MBS = mult で当てる形で決めた（Linux の HS の扱い TLBPC = TDPC − 1・TBC 0 は、TD が 1 interval 以下の時この式と同じ値）。

## 確かめた事（host と build だけ）

- `sh plan/ws282/tests/run-xhci-hb-host-test.sh`（新規）: encoder の HS mult（3 × 1024・2 × 960）・1 transaction・拒む case と、TBC/TLBPC（3072・1500・0 byte・1 transaction・複数 burst・0.96・packet 0）。通常と ASan/UBSan の両方で `xhci-hb-host-test: PASS`。
- `sh plan/ws282/tests/run-uvc-host-test.sh`: alternate の case を high-bandwidth に合わせて直し（3 × 1024 が受けられ 2048 の payload がそれを選ぶ、予約の 3・FS の mult・1100 byte は拒む）、通常と ASan/UBSan で全 case PASS（p003・p004b で書いた case も含め、この時に初めて流した）。
- `sh plan/ws282/tests/run-cam-host-test.sh`: 通常と ASan/UBSan で全 case PASS（p002 の試験、初めて流した）。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-<c>.mk BUILD=build/ws282/p6d-<c> build/ws282/p6d-<c>/vmunix`: amd64・rpi4・rpi5・visionfive2・pcat の 5 つとも rc 0・warning 0（log は worktree の `build/ws282/logs/p005-<c>.log`）。

## 他の WS への影響（Q1 へ）

- `plan/ws197/tests/xhci-isoch-test.c` の `test_context` の 1 件「high bandwidth refused」（HS 5U, 0x0800 | 512）が、この変更で FAIL になる（契約を変えたので期待どおり）。WS197 の file なので直していない。案: その check を「made で word1 の Max Burst が 1、word4 が 1024」に変える。

## 未実施

- 板（5330 + HDMI capture）で 3072 の alternate が選ばれ 720p YUYV ≥ 10 fps・1080p MJPEG 30 fps、回帰の `bt-sco-loopback`（前後）と HID（T1）。前提の WS197 p007a2 の L2 が無いので、SCO の回帰の基準も無い。
