<!-- awesome-plan project=zedbsd record=ws282-p010 -->

# ws282-p010 UVC の control（processing unit と camera terminal、id 32 以降）

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（実装済み・試験未実施。2026-10-29 Q1「次は p010 に進んでください。」で着手。design は id の表（§3.7）だけで、Phase の設計は無かった。下の判断は担当の推し）

## 実装した事

- `include/drivers/usb/usb-uvc-desc.h`・`src/drivers/usb/usb-uvc-desc.c`: processing unit の bmControls（bControlSize 7、bitmap 8〜）と camera terminal（wTerminalType 0x0201 の入力 terminal、最初の 1 つ、bControlSize 14、bitmap 15〜）の読み。framework の id → UVC の (unit, selector, bmControls の bit, byte 数, 符号・switch・AE mode・固定の範囲) の表 16 個（brightness 32 … zoom 52）、`uvc_control_lookup`・`uvc_control_at`・`uvc_control_present`・`uvc_control_decode`・`uvc_control_encode`（field に入らない値は EINVAL）・`uvc_ae_mode_choose`（1 は aperture priority、無ければ auto、0 は manual）・`uvc_ae_mode_automatic`。GET_MIN/MAX/RES/INFO/DEF の request の番号と GET_INFO の bit。
- `src/drivers/usb/usb-uvc.c`: source の `control_info`（device が宣言し GET_INFO が GET を言う物だけ、SET が無ければ READ_ONLY、switch と power line は範囲が固定で GET_DEF だけ、他は GET_MIN/MAX/RES/DEF）・`get_control`（GET_CUR）・`set_control`（GET_INFO で SET が無い・auto で disabled は `EACCES`、範囲の外は `EINVAL`、AE は GET_RES の bitmap から mode を選ぶ）。VC interface への class request（wIndex = unit ID << 8 | interface）、GET の短い答えは `EIO`。宣言された control があれば `CAM_CAP_CONTROLS`。dump に unit の ID と bitmap と control の数。
- `userland/tests/camcap`: `--controls`（id 1〜10・32〜52 の範囲・既定・今の値）と `--control id=value`（8 個まで、format の後に設定し読み返す）。
- `docs/reference/camera.md` の Controls の節に範囲・単位・switch・`EACCES` を足した。

## 仕様の照合

- selector（UVC 1.1 §A.9.4・§A.9.5）、bmControls の bit（§3.7.2.3・§3.7.2.5）、値の大きさと符号（§4.2.2.1・§4.2.2.3）、AE mode の bit（§4.2.2.1.2）、GET_INFO の bit（§4.1.2）は担当の知識で書き、**仕様の PDF とは照合していない**（実機の dump で bitmap と答えを見て確かめる。code の出典の注記は節の番号まで）。

## 確かめた事（host と build だけ）

- `sh plan/ws282/tests/run-uvc-host-test.sh`: 新しい case `controls`（PU と CT の bitmap と ID、camera でない入力 terminal は無視、bitmap の無い短い PU、presence、符号つき・無しの encode/decode と範囲、exposure の 4 byte、power line の固定の範囲、AE の選びと読み）を足し、通常と ASan/UBSan で全 case PASS。
- vmunix: amd64・pcat・rpi4・rpi5・visionfive2 が rc 0・warning 0。`bin/camcap`（rpi5 の試験の config）が rc 0・warning 0。

## 未実施

- 実機（5330 + webcam）で `camcap --controls` と `--control 32=…` で明るさが変わる事（design の受け入れ）。HDMI capture は control を持たない見込み。extension unit・relative の control・pan/tilt は対象外。
