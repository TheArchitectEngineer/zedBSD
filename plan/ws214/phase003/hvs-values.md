# WS214: HVS の値の出典の表

[p001 の設計](../phase001/phase.md) 4.3 (8) の表。新しく使う HVS の値ごとに、値・zedBSD の名前・出典・確かめ方を書く。GPL の文と code は写さない（値・独自の名前・出典の場所・確かめ方だけ）。出典の記号: (a) zedBSD の既存の code（WS141 で監査済み、[rpi4-gpu-license-audit.md](../../ws141/rpi4-gpu-license-audit.md)）、(b) Broadcom・Raspberry Pi の公開の文書、(c) Pi の実測、(d) Linux 6.19 vc4 の読み（理解だけ）。

## p003a（2026-10-12 P2）

p003a は新しい register・bit・plane の word を使わない。変えたのは、既存の値の使い方（list を置く SRAM の場所、差し替え、underrun の許可の時期、背景の fill の要求の回数）だけ。

| 値 | zedBSD の名前（file） | 使い方の変化 | 出典 | 確かめ |
| --- | --- | --- | --- | --- |
| list の SRAM は register の窓の `0x4000` から 4096 word | `BCM2711_LIST_WORDS`（bcm2711-private.h）、`FLIP_FIRST_SLOT_LIST`・`FLIP_SLOT_WORDS`（display-flip.c） | 3 つの slot を word 256・512・768（各 256 word）に置く。slot の場所は zedBSD の割り付け（HVS の値ではない）。word 0〜31 firmware、32〜42 filter、43〜51 console は前のまま避ける | (a) readout.c の `READOUT_LIST_MEMORY`・`READOUT_COMPOSITOR_SPAN`、display-start.c の窓の大きさの検査（`0x8000`）、display-program.c の filter・console の場所 | host: display-flip-host-test（slot の場所、今の list に書かない、3 slot が足りる）。Pi: 未（焼き直しの後） |
| attach の窓の大きさの検査 `0x5000` | `bcm2711_display_flip_attach`（display-flip.c） | `0x42c4` → `0x5000`（word 1023 の終わりまで） | (a) 上の値からの計算 | host: display-flip-host-test（`0x4fff` で ENODEV） |
| 次の list の register `0x20`、今の list の register `0x30` | `publish_list`・`check_pipeline`（display-flip.c）、`service_timing`（display-irq.c） | 採られていない次の list を別の完全な list に差し替える（`0x20` を書き直す）。採用は `0x30` が出した list のどれかと一致した時、それより古い list は退役 | (a) 既存の flip（WS141 p007 以降）。差し替えで HVS が完全な list の一方だけを採ること（1.1）は (d) の理解と設計の前提で、(c) は未 | host: display-flip-host-test（差し替え、古い list を飛ばした採用）。Pi: 未 |
| PV の status `0x28` の vblank `0x80`（W1C） | `publish_list`（display-flip.c） | 新しく出す時だけ消す。差し替えでは書かない | (a) 既存の flip・R0 | host: display-flip-host-test（差し替えで書かない） |
| HVS の control `0x00` の channel 0 の underrun の許可 `0x200`、status `0x04` の `0x200`（W1C） | `enable_underrun_locked`（display-flip.c）、`service_compositor`（display-irq.c） | publish で止めない。HVS の IRQ が数えて止め（line ごとの再発を防ぐ）、選んだ PV の vblank ごとに消して許可し直す（前は採用の時だけ） | (a) 既存の flip・display-irq.c | host: display-flip-host-test（flip の report の underrun の数）。Pi: 未 |
| 背景の fill の要求 `0x44` の bit 24 | `publish_list`（display-flip.c） | 立っていない時だけ書く（前は publish ごと） | (a) display-program.c・既存の flip | host: 全部の flip の試験が通る |
| plane の word（unscaled の 8 word、固定 alpha `0x4000fff0`、per-pixel premultiplied `0x2000fff0`、位置の word 1、終わり `0x80000000`） | `encode_plane`・`cursor_words`（display-flip.c） | 変わらない。不透明な全画面の primary の下では console の plane を list に入れない | (a) 既存の flip（prepare_frame・cursor_words） | host: display-flip-host-test・display-device-host-test（list の word） |

## p003a の終わりの GPL との類似の監査（2026-10-12 P2）

- 対象: p003a で変えた `src/drivers/gpu/bcm2711/display-flip.c`・`display-flip.h`・`display-device.c`。比べた相手: Linux 6.19（`05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`、`plan/ws141/temp/linux`、ignored）の `drivers/gpu/drm/vc4/*.c`・`*.h`。
- 方法: WS141 p007 と同じ形の字面の比較（comment と include を除いた token 列の、literal の 24 token の窓と、識別子を正規化した 48 token・32 token の窓の一致）を scratchpad の使い捨ての script で行った（script と GPL の作業資料は commit しない）。
- 結果: literal 24 は 0、正規化 48 は 0。正規化 32 は display-device.c と vc4_hdmi.c の 8 窓で、既存の `bind_operations` の「構造体の op の欄へ関数を順に代入する」C の共通の構文（p003a で変えていない並び）。GPL の演算・構造・名前・comment の取り込みとして残す候補は無い。
- 設計: 状態から list を作り、採られていない次の list を差し替え、2 つ出ている間は採用の IRQ で coalesce する作りは、p001 の設計（zedBSD の flip の state・slot・serial）から書いた。Linux の DRM の atomic・vblank event・dlist の allocator（drm_mm）の構造は使っていない。HVS の値は上の表のとおり既存の zedBSD の code の値だけ。
- 限界: 有限の窓の比較は短い一致や設計の類似の自動的な証明ではない。WS の最後の p002 でもう一度行う。

## p003b（2026-10-12 P2）

出典の (d) は Linux 6.19 vc4（`05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`）の `vc4_plane.c`・`vc4_regs.h`・`vc4_hvs.c` の読み（理解だけ）。2026-10-12 ユーザーが WS141 の 2026-10-04 の決定（値は事実として使い、名前は独自、配置・comment・構造は写さない、最後に類似の監査）を WS214 に広げた（guardrail、Q1 の連絡）。作業の文書は `plan/ws214/temp/`（commit しない）。

| 値 | zedBSD の名前（file） | 出典 | 確かめ |
| --- | --- | --- | --- |
| overlay の unscaled の 8 word（cursor と同じ並び、位置 word 1、src の大きさ word 3、pointer は image の先頭＋src_y×pitch＋src_x×4） | `overlay_words`（display-flip.c） | (a) 既存の `cursor_words`・`prepare_frame` | host: display-flip-host-test（overlay の list の word） |
| 制御の word の valid bit30・word 数 bit29:24・byte の順 bit14:13・alpha の展開 bit12・RGB の展開 bit11・unity bit15・pixel の形 7 | `overlay_words`（`0x40000000`、`count << 24`、`order << 13`、`0x1000`、`0x800`、`7U`） | (a) 既存の `0x48009807` の分解、(d) で bit の意味を確認 | host: 同上（scaled の制御の word） |
| 拡大縮小の方式の 2 つの field（bit10:8・bit7:5、RGB は同じ値）と組の値 0〜7（横 PPF 縦 PPF=0 … 横 TPZ 縦なし=7） | `overlay_words` の `method` | (d) | host: PPF の組（0）と TPZ の組（3）の word |
| 方式の選び方（同じ大きさ→なし、3×dst ≥ 2×src→PPF、他→TPZ） | `scale_mode` | (d) | host: 拡大（PPF）と 1/4 の縮小（TPZ） |
| scaled の並び（制御、位置、制御 2、拡大縮小後の大きさ（高さ bit28:16・幅 bit12:0）、元の大きさ、context、pointer、pointer の context、pitch、［LBM］、横 PPF、縦 PPF＋context、横 TPZ 2 word、縦 TPZ 2 word＋context、［filter の位置 ×4］） | `overlay_words` | (d) | host: 17 word（PPF）・15 word（TPZ）の並び |
| PPF の word: AGC bit30、比 bit24:8（16.16、最下位 bit を 0 に）、位相 bit6:0（−32＋比の誤差の半分、64 以上は 63） | `polyphase_word` | (d) | host: 200→300 で `0x40aaaa60` |
| TPZ の word: 比 bit28:8、2 つ目は 0xffffffff/比 の下 16 bit | `trapezoid_words` | (d) | host: 200→50 で `0x04000000`・`0x3fff` |
| filter の位置の field bit13:0、PPF のある plane は 4 つ | `FLIP_FILTER_WORD`（32） | 位置は (a) display-program.c の `PROGRAM_FILTER_START`（filter は WS141 の独自計算）、field と 4 つは (d) | host: filter の word が 32 |
| LBM: 縦の拡大縮小の時だけ、始まりの word を置く。HVS5 の LBM は 60K word、1 行の pixel 数（横 TPZ なら dst の幅、他は src の幅）×16（縦 PPF）か ×8（縦 TPZ）を 128 に切り上げて 4 で割る | `FLIP_LINE_BUFFER_WORDS`（8192）・`FLIP_LINE_BUFFER_PIXELS`（2048）、overlay k の区画は k×8192 | (d) | host: 縦の拡大縮小で 2049 pixel の行は ENOSPC。Pi: 未（hvs-load） |
| 処理量: scaled の plane は 1 clock に 2 pixel（他は 4） | `required_clock` | (d) | Pi: 未 |
| 帯域: src の面積 × 縦の縮小の比の切り上げ × 4 byte × refresh、予算 1.5 GB/s（仮） | `read_bandwidth`・`FLIP_BANDWIDTH_BUDGET` | (d)（予算は BCM2835 の頃の値、review-1 M1） | host: 縦 1/3 で予算を超えて ENOSPC。Pi: hvs-load の実測で置き換える |

## p003b の終わりの GPL との類似の監査（2026-10-12 P2）

- 対象: p003b で変えた `display-flip.c`・`display-flip.h`・`display-device.c`。相手と方法は p003a と同じ（vc4 の `*.c`・`*.h`、literal 24 token、正規化 48・32 token）。
- 結果: literal 24 は 0。正規化 48 は display-flip.c と vc4_plane.c の 3 窓、display-device.c と vc4_plane.c の 3 窓で、どちらも「src_x・src_y・src_width・src_height・dst_x・dst_y を同じ名の欄へ写す」代入の列（矩形の 6 つの欄を構造体の間で写す C の共通の形）。正規化 32 は加えて vc4_hdmi.c との既存の `bind_operations` の代入列。PPF・TPZ の値の計算は (d) の式の事実（比、位相の補正、逆数）を独自の関数（`polyphase_word`・`trapezoid_words`）に書いた。Linux の関数の分け方（plane の状態の複製、drm_mm の LBM の割り当て、dlist の counter）は使わず、LBM は overlay ごとの固定の区画にした。取り込みとして残す候補は無い。
- 限界は p003a と同じ。WS の最後の p002 でもう一度行う。
