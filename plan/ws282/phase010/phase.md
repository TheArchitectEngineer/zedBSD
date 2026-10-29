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

- 2026-10-29 Q1「usb.org から UVC 1.5 の仕様を web の GET（パラメータ無し）で取って selector と bit の値を照合してよい。」→ `https://www.usb.org/sites/default/files/USB_Video_Class_1_5.zip`（HTTP 200、application/zip、6,902,548 byte、SHA-256 `67ed3227f98189735b6fac2f5088dde3f5415eca066967b617d5b8f96283c1e9`）を scratchpad に取り、`UVC 1.5 Class specification.pdf`（Revision 1.5、2012-08-09、SHA-256 `df9978fe…ee56b5`）を pdftotext で読んだ。bot の確認には当たっていない。PDF は repository に入れない。
- 照合した値（全部一致）: CT の selector（§A.9.4: AE mode 0x02・exposure absolute 0x04・focus absolute 0x06・focus auto 0x08・zoom absolute 0x0B）、PU の selector（§A.9.5: backlight 0x01〜WB temperature auto 0x0B）、camera terminal の layout（Table 3-6: bTerminalID 3・wTerminalType 4・bControlSize 14・bmControls 15、bit D1 AE mode・D3 exposure abs・D5 focus abs・D9 zoom abs・D17 focus auto）、processing unit（§3.7.2.5: bControlSize 7・bmControls 8、D0 brightness〜D6 WB temperature・D8 backlight・D9 gain・D10 power line・D12 WB auto）、ITT_CAMERA 0x0201（§B.2）、request の番号（§A.8: GET_MIN 0x82〜GET_DEF 0x87）、GET_INFO の D0〜D2（Table 4-3、§4.1.2）、AE mode の D0〜D3（§4.2.2.1.2）、wBrightness・wHue は signed、dwExposureTimeAbsolute は 4 byte（1 = 0.0001 s）。
- 違い: power line frequency は UVC 1.5 で 3（Auto）が足されている（Table 4-35）。1.1 の 0〜2 の固定の範囲を、bcdUVC ≥ 1.5 の device では 0〜3 にする（`uvc_control_fixed_maximum()`、p011 の commit に入れた）。

## 確かめた事（host と build だけ）

- `sh plan/ws282/tests/run-uvc-host-test.sh`: 新しい case `controls`（PU と CT の bitmap と ID、camera でない入力 terminal は無視、bitmap の無い短い PU、presence、符号つき・無しの encode/decode と範囲、exposure の 4 byte、power line の固定の範囲、AE の選びと読み）を足し、通常と ASan/UBSan で全 case PASS。
- vmunix: amd64・pcat・rpi4・rpi5・visionfive2 が rc 0・warning 0。`bin/camcap`（rpi5 の試験の config）が rc 0・warning 0。

## 未実施

- 実機（5330 + webcam）で `camcap --controls` と `--control 32=…` で明るさが変わる事（design の受け入れ）。HDMI capture は control を持たない見込み。extension unit・relative の control・pan/tilt は対象外。
