<!-- awesome-plan project=zedbsd record=ws037-p003 -->

# ws037-p003: 定数の一括の改名、driver の骨格、段の印（N0: GPU を見つけて ID を読む）

Status: planned
Disposition: normal
Parent: [WS037](../ws.md)
Queue: none
依存: p001（作業の文書と監査の表）、p002（試験の道）
実行者: phase-runner（high）

## 範囲

1. **定数の一括の改名**（code を書く前、ws.md の「ライセンスの扱い」2）: `plan/ws037/temp/` の作業の文書の定数（register・bit・field・class・method の名前）を全て独自の名前（例 `NVRTX_…`）に一括で変え、対応表を temp に置く（commit しない）。register の定義は監査の表の MIT の出典から取る。
2. **骨格**: `src/drivers/gpu/nvrtx/`（`nvrtx.c`・`device.c`・`mmio.c`・`stage.c` など）。PCI の attach（vendor 0x10de、Turing 以降の device の表は p001 の表から）、BAR0（MMIO）・BAR1・BAR2/3 の map、`CONFIG_DRIVER_PCI_NVRTX`（menuconfig、既定 n、CI の config には入れない）。i915 の `src/drivers/gpu/i915/` の attach の形を手本。
3. **段の印**: `stage.c` が GOP の framebuffer に 1 行ずつ印を書く（例 `nvrtx: N0 found 10de:1f02 at 01:00.0`、`nvrtx: N0 boot0=… chipset=TU106`）。印は kernel の log にも出す。HAL の API の変更は要らない見込み（framebuffer は今の console と同じ口）。要るなら差分を plan に置いて承認を得る。
4. **N0 の確かめ**: chip の ID の register（boot0）を読み、chipset が TU106 であること、BAR の大きさを印に出す。GPU を初期化しない（読むだけ）。

## 受け入れ

- build（amd64）exit 0・自前の warning 0。`CONFIG_DRIVER_PCI_NVRTX=n` の build が変わらない。
- 実機（p002 の方式）で段の印 N0 が GOP の画面に出て、chipset が TU106（写真か capture）。他の機器（i915 の 5330・QEMU）の boot-test が PASS（nvrtx を有効にした image でも、NVIDIA の GPU が無ければ何もしない）。
- `git grep` で GPL の source の名前・comment が code に無い（改名の対応表と照らす）。
