<!-- awesome-plan project=zedbsd record=ws277-p001 -->

# ws277-p001 DC8200 の display driver の設計

- 親: [ws277](../ws.md)、Queue: q1341 / q1341-i01、q1343 / q1343-i01、q1346 / q1346-i01、q1347 / q1347-i01（P1、2026-10-20）
- Status: in-progress（改訂 3.1 が出来た、Q1 の差分の確認待ち）

## 結果（2026-10-20 P1）

- [design.md](../design.md) 版 1。PBE は LINEAR を書ける（Mesa の値）、DC8200 は線形・32 bit の address・MMU 無しで読む。最小の道は U-Boot の mode を引き継ぎ plane の address だけを書き換える（D1 (a)）。判断 D1〜D6、Phase の案 p002〜p007。HAL の変更は無し。コードは無し。
- 2026-10-20 q1343-i01: [design.md](../design.md) 版 2。[レビュー 1](../design-review-1.md) の B1〜B3・M1〜M9・Minor 1〜11 に対応（§12 の表）。present は latch の後に返る、shareable の SLC_BYPASS、mode と同じ大きさだけ zero copy、panic は `reveal_fatal`（HAL 変更無し）、判断 D1〜D9、他 WS への依頼 R1〜R7（§9、Q1 が各 WS の Phase へ渡す）、実機の試験の一覧 U0〜U10、LW 52〜83。
- 2026-10-20 q1346-i01: [design.md](../design.md) 版 3。[レビュー 2](../design-review-2.md) の新 B1・新 M1〜M6・Minor 1〜11 に対応（§13 の表）。Q1 の判断を反映: buffer の寿命は display.c の中の保持の数（R8 不要）、latch は vblank の IRQ を p003 に繰り上げ s0 + 2 で判定（D10 (a)）、出力喪失は ENXIO と書き戻し（D11）、shadow を止めて書く、SLC 迂回を buffer の属性と照会の口に（R3）、libvulkan の条件を 1.3.269 / 1.4.343 の選択に（R7）、R4 は VF2 の platform の wrapper の text の ops（serial-console は汎用のまま）、描き直しは呼ばない、M6 の戻し先は p005 の前の追補（§4.7）。go/no-go を小さい p002 に分け、Phase は p002〜p008、LW 53〜85。他 WS への依頼 R1〜R7 を直した（§9）。
- 2026-10-20 q1347-i01: [design.md](../design.md) 版 3.1。[レビュー 3](../design-review-3.md)（Blocker 無し）の M1〜M3・Minor 1〜10 に対応（§14 の表）。M1: s0 + 2 の前提（IRQ は latch と同じ点か後、外れたら s0 + 3）・s0 は手順 (4) の後・代価は最大 refresh/2・p004 で 60 fps に戻す・U11 で IRQ の位置を実機で確かめる。M2: Q1 の判断で serial-console に汎用で追加だけの口を足す（R4・§4.6）。M3: p002 の依存に R4 の記述の部分。R3 を縮めた（Minor 4）。LW 54〜87。

## 目的

VF2 の画面を CPU の copy 無しで出す道を決め、`plan/ws277/design.md` に書く。コードは書かない。

## 受け入れの条件

1. vendor の `~/visionfive-linux`（`drivers/gpu/drm/verisilicon/`、`inno_hdmi.c`、`jh7110.dtsi` の dc8200・hdmi の node）と mainline の状況、JH7110 の TRM を読み、DC8200 の初期化（PD・clock・reset、PLL と pixel clock、HDMI の PHY・DDC・EDID）、plane（format・modifier・stride の制約）、MMU、vblank の IRQ、page flip を出典付きで書く。未確認は未確認。
2. PowerVR が線形の RGBA（または DC8200 が読める形）で render の target を書けるか（PBE の memory の layout、mainline・Mesa の値）と、DC8200 が読む memory の制約（32 bit の address、coherency: GPU の書き込みの後に DC8200 が読む時の SLC・L2 の扱い）。
3. WS269 の display の node（gpu0、boot framebuffer の copy）を DC8200 の node に置き換える形（drv_gpu_display_ops、import・BLOB の present、U-Boot の framebuffer からの引き継ぎ）、libvulkan が shared の道を選ぶ条件（WS269 p007 の後半の報告: renderer の capset）との整合。
4. Phase の分割（依存・LW・実機の確認の点）とユーザーの判断（D1…）。出典の規則は plan/guardrail.md（値は Value(s) referenced、手順は Procedure referenced、code・構造・名前は写さない）。HAL の API の変更が要るなら proposed/ に置くだけ。
