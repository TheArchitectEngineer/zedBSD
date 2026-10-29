<!-- awesome-plan project=zedbsd record=ws282-p011 -->

# ws282-p011 規約の全文の見直しと license の監査

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（規約の見直しは済み。実機の試験の後に直す code があれば、その差分はもう一度見直す。license の監査は未実施）

## 規約の見直し（P6 の配下 conformance-reviewer、2026-10-29）

- 対象: `cam.c`・`cam.h`・`uapi/camera.h`、uvc の 4 file と 2 header、`camcap`・`camcheck`（`analyze.{c,h}`）・`camcommon`・`camvk`、共有の file の WS282 の hunk だけ（`gpu.c` の `drv_gpu_shared_backing`、bcm2711 の `share.c`・`render-device.c`、`pci-xhci-control.h`・`pci-xhci-isoch.h`・`pci-xhci.c` の high-bandwidth、`seat.c` の camera の行）。
- 直した物（書き方だけ、動作は変えない、概算）: 段落の comment の欠落 約 75、`}` の後の空行 約 25、条件の中の関数の呼び出し 約 50（`strcmp` 39）、`?:` 約 20、3 節以上の条件の行分け 約 15、引数の中の入れ子の呼び出し 5、`return f()` 6、関数の先頭以外の宣言 1、comment の中の `plan/` への参照 2（AGENTS §10）。`style-check.py` の指摘は 162 → 1。
- 残した物: `camcheck/main.c` の `if (setjmp(...) != 0)`（C の規格で setjmp は制御式の中にしか置けない）。他の WS の既存の書き方（`pci-xhci-control.h` の SuperSpeed の block、`seat.c` の冒頭の comment の WS・BUG の番号）は触っていない。
- P6 が差分を読んだ所: kernel 側（`gpu.c`・`pci-xhci-*.h`・`usb-uvc-desc.c`）の変更は comment・行分け・変数への置き換えだけで、短絡の順（`usb-uvc-desc.c` の wTerminalType の読みは長さの確かめの後のまま）が保たれている事。

## 同じ commit に入れた p010 の直し

- UVC 1.5 の power line frequency の 3（Auto）: `UVC_CONTROL_AUTO_FROM_1_5` と `uvc_control_fixed_maximum()`。bcdUVC ≥ 1.5 の device は 0〜3、他は 0〜2。uvc-host-test に case を足した。

## 確かめた事（host と build だけ）

- `run-cam-host-test.sh`・`run-uvc-host-test.sh`・`run-camcheck-host-test.sh`・`run-xhci-hb-host-test.sh`: 4 本とも通常と ASan/UBSan で PASS。
- vmunix: amd64・rpi5・rpi4・visionfive2・pcat が rc 0・warning 0。rpi5 の試験の config の `camcap`・`camcheck`・`camvk`、amd64 の `sessiond` が rc 0・warning 0（log は worktree の `build/ws282/logs/p011-*.log`）。clang-format は host に無く未実施。

## 未実施

- license の監査（design §8 の出典の注記の block ごと、Linux の uvc・xHCI の読んだ所が手順の参照に留まる事）。実機の試験の後の見直し。
