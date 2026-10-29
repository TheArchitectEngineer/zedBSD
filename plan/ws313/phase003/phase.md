<!-- awesome-plan project=zedbsd record=ws313-p003 -->

# ws313-p003: 媒体と夜の試験の手順

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i01（P7）
WS: [WS313](../ws.md)、設計 [design.md](../design.md) §2・§6・§7

## 実装（2026-10-29）

- `platform/arm64/odroid-c5.mk`: Hardkernel の DTS（`platform/arm64/odroid-c5/dts-sources.txt` の 31 file、pinned の raw URL、size と SHA-256）を
  `platform/arm64/tools/fetch-odroid-c5-dts.py` で `$(BUILD)/dts-sources/` に取り、cpp と dtc で `odroid-c5.dtb`（zedBSD の追加は `odroid-c5/odroid-c5.dts`: /chosen の simple-framebuffer）。
  `boot.scr`（`odroid-c5/boot.cmd` を `tools/make-uboot-script.py` で legacy image に、login= は config から）。`hdd-image.img`（`tools/make-odroid-c5-hdd-image.py`）。
- 試験: [tests/README.md](../tests/README.md)、`tests/c5-night-test.sh`、`tests/c5-serial.py`。

## 確かめ

- boot.scr: `mkimage -l` で header を読め、payload が `mkimage -T script` の物と一致。
- DTB: build でき、HAL の parser の host の試験が PASS（p002）。
- image の build（world と sysroot）: 未実施（Q1 の許可 2026-10-29 を受け、これから）。
- 実機: 未実施。
