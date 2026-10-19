<!-- awesome-plan project=zedbsd record=ws277-p001 -->

# ws277-p001 DC8200 の display driver の設計

- 親: [ws277](../ws.md)、Queue: q1341 / q1341-i01（P1、2026-10-20）
- Status: in-progress（設計の文書が出来た、design-reviewer 待ち）

## 結果（2026-10-20 P1）

- [design.md](../design.md) 版 1。PBE は LINEAR を書ける（Mesa の値）、DC8200 は線形・32 bit の address・MMU 無しで読む。最小の道は U-Boot の mode を引き継ぎ plane の address だけを書き換える（D1 (a)）。判断 D1〜D6、Phase の案 p002〜p007。HAL の変更は無し。コードは無し。

## 目的

VF2 の画面を CPU の copy 無しで出す道を決め、`plan/ws277/design.md` に書く。コードは書かない。

## 受け入れの条件

1. vendor の `~/visionfive-linux`（`drivers/gpu/drm/verisilicon/`、`inno_hdmi.c`、`jh7110.dtsi` の dc8200・hdmi の node）と mainline の状況、JH7110 の TRM を読み、DC8200 の初期化（PD・clock・reset、PLL と pixel clock、HDMI の PHY・DDC・EDID）、plane（format・modifier・stride の制約）、MMU、vblank の IRQ、page flip を出典付きで書く。未確認は未確認。
2. PowerVR が線形の RGBA（または DC8200 が読める形）で render の target を書けるか（PBE の memory の layout、mainline・Mesa の値）と、DC8200 が読む memory の制約（32 bit の address、coherency: GPU の書き込みの後に DC8200 が読む時の SLC・L2 の扱い）。
3. WS269 の display の node（gpu0、boot framebuffer の copy）を DC8200 の node に置き換える形（drv_gpu_display_ops、import・BLOB の present、U-Boot の framebuffer からの引き継ぎ）、libvulkan が shared の道を選ぶ条件（WS269 p007 の後半の報告: renderer の capset）との整合。
4. Phase の分割（依存・LW・実機の確認の点）とユーザーの判断（D1…）。出典の規則は plan/guardrail.md（値は Value(s) referenced、手順は Procedure referenced、code・構造・名前は写さない）。HAL の API の変更が要るなら proposed/ に置くだけ。
