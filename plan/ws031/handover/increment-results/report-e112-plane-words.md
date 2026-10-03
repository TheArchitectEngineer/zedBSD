# WS031 E-112 追補: universal plane の語（実機 PASS、未書込み）と「名前つき step」

2026-09-19。E-110／E-111 報告の続き。台帳 `plan/ws031/results-ws031.md` E-112。

## 結論
- 正本の plane writer（`icl_plane_update_noarm`／`icl_plane_update_arm`）と `skl_plane_ctl`／`glk_plane_color_ctl` ほか 27 関数を生成 file `skl_plane_port.c` に取り込み、**実機の実 scanout buffer（pitch 7680、surf 0xfdfc0000）に対する語が Linux の dump と一致**しました: PLANE_CTL **0x94000000**、STRIDE **0x78**、SIZE **0x0437077f**、POS 0、COLOR_CTL **0x2000**。
- PLANE_CTL の bit 28 は ADL-P 固有の WA（`adlp_plane_ctl_arb_slots`、cpp 4 → ARB_SLOTS(1)）でした。値を推測で組まず正本の関数から出す方式の効果が出た箇所です。
- host 56/0、ktest 452/0、実機 `SCANOUT-TEST plane words … match=1`（合格条件に追加）＋`AUX-TEST verdict: PASS`。表示 register への書込みは引き続き 0 件、今回増えた hardware 操作も 0 件。

## 「名前つき step」
未移植の callee を黙って消さないため、emit hook に `step(name)` を足しました。正本がその関数を呼ぶ位置で、語の列に step として記録されます。今回の列:

```
write 0x70188 = 0x00000078   PLANE_STRIDE
write 0x7018c = 0x00000000   PLANE_POS
write 0x70190 = 0x0437077f   PLANE_SIZE
write 0x70194/98/a0          KEYVAL 0 / KEYMSK 0 / KEYMAX 0xff000000 (plane alpha)
write 0x701a4 = 0            PLANE_OFFSET
write 0x701c0 = 0            PLANE_AUX_DIST (ADL-P は flat CCS でないので書く)
write 0x701c8 = 0            PLANE_CUS_CTL (primary は HDR plane)
write 0x701cc = 0x00002000   PLANE_COLOR_CTL
STEP  skl_write_plane_wm     ← watermark は未移植。ここに入る
write 0x70180 = 0x94000000   PLANE_CTL
write 0x7019c = surf         PLANE_SURF（最後 = update を arm）
```

enable 列（`hsw_crtc_enable`、DDI の pre_enable／enable、disable 側）を呼出し元ごと取り込むときも同じ仕組みを使い、「どこが未実装か」と前提条件表を列から作ります。

## 範囲と拒否
この片は primary plane／linear XRGB8888／全画面／rotation 0 だけです。他 format、tiled modifier、sprite plane、64 の倍数でない pitch、4 KiB 非整列の surf は、**正本 code を走らせる前に glue が −22 で拒否**します。linear に固定した helper（`is_surface_linear`、DPT 不使用、aux plane なし等）は `lcd_plane_compat.h` に名前を挙げて列挙しています。

## 出典まわり
- 固定参照に `i915_drm.h`（colorkey 構造体）を追加、`drm_fourcc.h` は file 全体を 1 行の置換つきで取り込み（manifest に記録）。
- generator の不備を 1 件修正: 先頭が 1 行の SPDX comment で始まる元 file（`intel_psr_regs.h`）から抽出した header に Copyright 行が入っていなかった → 次の comment も保持するよう修正、再生成済み。
- `lcd_drm_plane_defs.h` は 3 つの元 file が 1 file に混在しています（出典台帳に「未監査・要分割」と記載）。

## まだ比較していない dump 値（今後の比較先）
PLANE_WM 0x70240 = 0x80004010、PLANE_BUF_CFG 0x7027c = 0x0fdb0000、DPCLKA_CFGCR0 0x164280 = 0x01e07800、DPLL0 enable 0x46010 = 0xcc000000、BLC_PWM_PCH_CTL2 0xc8254 = 0x00017700。

## 次
enable／disable 列を呼出し元ごと → 前提条件表 → DPLL enable／DPCLKA／combo PHY signal level → watermark／DDB → backlight → link training（成功の証拠を明示）→ fake 試験 3 系統 → LCD-B 実機 1 回（buffer 読戻し＋register／link 状態＋写真）。compiler 側は SEND descriptor（出典つき転記）→ 3D state との突合せ → 初の GPU 実行試験。
