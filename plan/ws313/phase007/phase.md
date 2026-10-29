<!-- awesome-plan project=zedbsd record=ws313-p007 -->

# ws313-p007: 表示（U-Boot の mode の OSD1 の flip と CPU の copy）

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 B）
WS: [WS313](../ws.md)、設計 [gpu.md](../gpu.md) §5、[design.md](../design.md) §5

## 目的

U-Boot が HDMI に出し OSD1 が scanout している boot の framebuffer を、GPU の display の node（gpu0）として session に渡す。
guardrail「firmware が出していた出力先以外に scanout を始めない」を守り、HDMI・VENC・VPU の clock と mode、OSD の窓・形式・blend に触れない。

## 実装（2026-10-29）

- `src/drivers/gpu/mali/display/display.c`: display の node（query・mode・claim・release・present・wait、resource の create・destroy・read・write・map、scanout の query_device・constraints・import（ENOTSUP））。
  resource は連続・4 GiB より下の cacheable の RAM。OSD1 が boot の framebuffer を linear で出している時、同じ大きさ・行の幅・byte の順の frame は cache を clean して BLK1_CFG_W4 を書き換え 1 frame（20 ms）待つ flip、他は CPU の copy（alpha を 0xff）。plane が見ている resource の free は plane が離れるまで延ばす。
- `src/drivers/gpu/mali/display/osd.{h,c}`: OSD1 の register の表と、6 語の decode・framebuffer との照合・address の語（host 試験できる純粋な関数）。
- 登録は `drv_mali_gpu_attach` の最初（GPU が失敗しても残る）。

## 確かめ

- `make ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/pb build/pb/vmunix`: exit 0、warning 0（2026-10-29、配下 B）。
- host: `tests/mali-interface-host-test.c`（OSD の decode を含む）`all checks passed`（[gpu.md](../gpu.md) §8）。
- 実機: 未実施。QEMU: 未実施（S7D の machine が無い）。

## 夜の試験で見る物

- serial の `mali: display: OSD1 control … address … stride …` と `mali: display 1920x1080 … frames flipped|copied`（[gpu.md](../gpu.md) §9 の 1）。
- desktop は G6〜G8 が無いので出ない。表示の node の確かめは game mode の直の口（libkeiland-backend-zedbsd の display の ioctl）か display の試験の program で、flip の時に裂けないか・色の順・alpha を画面の写真で見る（ユーザー）。

## 再開点

- vsync の割り込み（VIU vsync SPI 197）で flip の latch を確かめる・待つ（今は 20 ms の sleep）。
- OSD の address が vsync で latch されない時の裂けの対策、U-Boot が canvas mode の時の扱い（今は copy だけ）。
