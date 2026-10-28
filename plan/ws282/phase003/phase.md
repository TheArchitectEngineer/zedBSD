<!-- awesome-plan project=zedbsd record=ws282-p003 -->

# ws282-p003 uvc の bind と descriptor の解析、PROBE/COMMIT

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（実装中、試験は未実施）

## 再開点

- 2026-10-29 P6: `include/drivers/usb/usb-uvc-desc.h`・`src/drivers/usb/usb-uvc-desc.c`（VC/VS の descriptor の解析、GUID、PROBE の record・補正、alternate の表と選択。純粋な層）を書いた、未 build。次: `usb-uvc.h`・`usb-uvc.c`（bind・claim・dump・登録・source の ops）、`usb-uvc-stream.c`（PROBE/COMMIT の control transfer）、build の登録。
