<!-- awesome-plan project=zedbsd record=ws288-p012 -->

# ws288-p012 KC705 の RTL（board の top、JTAG の bridge、DC の HDMI）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p012 の行、§7.6 の p012 の module（`am_jtag_regs`、MIG/MMCM の wrapper、`kc705_raster` の board）、DC の HDMI（U7: ADV7511 を I2C で設定）。
- 状態: in-progress（2026-10-24 Q1）
- 方針（2026-10-24 ユーザー）:「タイミングの調整はまだ行わないので、KC705 での動作確認は後にします。実装を優先し、次のフェーズへ進んでも OK です。」

## 部分（part）

| part | 範囲 | 受け入れ | 状態 |
| --- | --- | --- | --- |
| A | RTL と simulation: `am_jtag_regs`（BSCANE2 USER2 の 72 bit の register から register bus へ）、`atg_dc_i2c`（ADV7511 の I2C の master）、DC の KC705 の HDMI の出力（ADV7511 への並列の RGB と sync）、`atg_kc705_raster` の board の top（MIG・MMCM は wrapper。sim は振る舞いの model） | 各 spec §8 の TB の PASS、board の top の sim で JTAG の bridge から fill → DBG の読み戻し、lint clean | in-progress（2026-10-24 P2） |

## 記録

- 2026-10-24 Q1: part A を P2（phase-runner、Opus 5.5 high）に依頼。worktree `~/amaterrace-worktrees/p2-kc705`（branch `p2-kc705`、基点 main b181b44）。
- 2026-10-24 ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」→ part B（板）を外し、板での確認は [p024](../phase024/phase.md)、timing は [p025](../phase025/phase.md) に。p012 は RTL と sim だけの Phase。
