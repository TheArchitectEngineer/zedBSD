<!-- awesome-plan project=zedbsd record=ws269-p016 -->

# ws269-p016 VF2 の GPU の文書（N5）

- 親: [WS269](../ws.md)、Status: cleared（2026-10-21 Q1。q1520 の板 103（vkmatrix 20/20）の後に F16・64・128 bit の「being verified」を外し、vkmatrix の節を 20 case に。以前: P1 q1521 で文書を書いた）
- 起点: [proposal-next-phases.md](../proposal-next-phases.md) の N5。

## 範囲

docs/ に VF2 の GPU の reference を書く。

- 書く事: Vulkan の範囲、memory、firmware と license、起動の段の行と `hw.gpu.powervr.stages`、gpu-selftest・vkmatrix・vkreset の使い方と出力の読み方。
- 板で見た事実だけを書く。docs/ から plan/ へは link しない。

## 結果（2026-10-21 P1）

- **新規**: `docs/reference/visionfive2-gpu.md`。
  - docs/style.md の分類では reference に当たる（command・API・互換）。
  - `docs/reference/README.md` の一覧から link した。
- **根拠**:
  - format の表: `src/drivers/gpu/powervr/image-state.c` の format_table。
  - sample と layer: `vk-device.c` の limits。
  - 段と理由の語: `stage.c` の表。
  - gpu-selftest の段: `gpu-selftest/main.c` の stage の表。
  - 板の結果:
    - 板 96: vkmatrix 15/15、gpu-selftest 13/13。
    - 板 98: vkreset。
- **書き方の判断**:
  - F16・64 bit・128 bit の format と、vkmatrix の copy-64・copy-128・render-f16 は「being verified」と書いた。
  - linear tiling の image は sample できない、と書いた。Q1 の指示による。code は確かめていない。
  - vkreset の時間の例は、board の値ではなく `<ms>` にした。
- **確かめた事**:
  - docs/ の中に `plan/` への link が無い（grep 0 件）。
  - link 先の source の path が存在する。

## 残件

- q1520 が merge されたら、「being verified」の 2 か所を直す（F16・128 bit の format の行と vkmatrix の 3 case）。
