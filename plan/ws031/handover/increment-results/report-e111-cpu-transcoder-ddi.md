# WS031 E-111 追補: cpu transcoder の呼出し元ごとの取り込みと DDI の語（実機 PASS、未書込み）

2026-09-19。E-110 報告の続き。台帳 `plan/ws031/results-ws031.md` E-111。

## 結論
- 正本の **呼出し元 `hsw_configure_cpu_transcoder()` そのもの**を生成 file に入れ、writer 間の順序も正本 text から得るようにしました。17 操作（write と read-modify-write を区別して記録）。
- DDI 側: `TRANS_MSA_MISC`、`TRANS_DDI_FUNC_CTL2`、**`TRANS_DDI_FUNC_CTL` = 0x8a210002（Linux の dump と一致）**、`DDI_BUF_CTL` の値 0x2（+enable = 0x80000002 = dump）。
- host 42/0、ktest 451/0、実機 `AUX-TEST verdict: PASS … lcd_a_match=1`（新しい語を含む判定）＋`SCANOUT-TEST verdict: PASS`。表示 register への書込みはまだ 0 件。今回増えた hardware 操作は DDI_BUF_CTL_A の**読出し 1 回**（現在値 0x80 = idle／disable）。

## E-110 報告の訂正（1 件）
E-110 で「13 語が正本の書込み順」と書きましたが、正本由来だったのは**各 writer の内部の順**で、M/N → timings → PIPESRC という writer の並びは私の glue の順でした。正本では PIPESRC は `hsw_configure_cpu_transcoder` の外で `hsw_crtc_enable` が書きます（**訂正 2026-09-19: 当初「後に」と書きましたが誤りで、正本の順は PIPESRC → `bdw_set_pipe_misc` → `hsw_configure_cpu_transcoder`、PIPESRC が先です。未確認の記憶で書いた記述でした**）。値（13 語とも Linux と一致）は変わりません。以後は「writer だけでなく呼出し元も正本から取り込む」方式にし、enable 列全体（`hsw_crtc_enable`、DDI の pre_enable／enable）も同じ方式で取り込みます（未移植の callee は名前つき step として記録）。

## 17 操作（pipe A／TRANSCODER_A、正本の順）
| # | 操作 | 意味 | Linux dump |
|---|---|---|---|
| 0–3 | write 0x60030／34／40／44 | DATA_M/N、LINK_M/N（LINK_N 最後。ver 13 は M2/N2 なし） | 一致 |
| 4–11 | write 0x6007c、0x60028、0x60000〜0x60014 | context latency、VSYNCSHIFT、timings | 一致（0x6007c は dump に無し） |
| 12 | rmw 0x420c0 set 0x80000000 | CHICKEN_TRANS_A: PIPE_VBLANK_WITH_DELAY（ver 12–13） | readout 無し |
| 13 | write 0x60420 = 0 | TRANS_VRR_CTL（flipline なし） | readout 無し |
| 14 | write 0x6002c = 0 | TRANS_MULT = pixel_multiplier − 1 | readout 無し |
| 15 | rmw 0x420c0 clear 0x18000000 set 0 | frame start delay = 1 − 1 | readout 無し |
| 16 | write 0x70008 = 0 | TRANSCONF: progressive、modeset 中は enable なし（dump 0xc0000000 は後の enable＋state） | 整合 |

readout の無い 5 語は正本 text からの導出値です（期待値は field 定義から独立に記述）。Linux guest で追加採取すれば比較先にできます。

## 次
plane 語（`skl_plane_ctl`〔ADL-P の ARB_SLOTS WA 込み〕、`glk_plane_color_ctl`、`icl_plane_update_noarm／arm`。比較先 PLANE_CTL 0x94000000／STRIDE 0x78／SIZE 0x0437077f／COLOR_CTL 0x2000）→ enable 列を呼出し元ごと → DPLL／DPCLKA／combo PHY → WM／DBUF → backlight → link training → fake 試験 → LCD-B（写真つき）。
