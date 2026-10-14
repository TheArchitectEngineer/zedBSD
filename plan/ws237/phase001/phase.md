<!-- awesome-plan project=zedbsd record=ws237-p001 -->
# ws237-p001: 設計（SKL・KBL・CFL の display と段 (1) の readout・flip・vblank）

Status: cleared の候補（2026-10-14 P2: 設計を書いた。Q1 の判定待ち。Q1 は方針を 2026-10-14 に了承）
Disposition: normal
Parent: [WS237](../ws.md)
Queue: Q1 の起動の指示（2026-10-14、P2、worktree `/home/awe/zedBSD-worktrees/p2`、branch `agent/p2`）
依存: [WS229](../../ws229/ws.md) の N0-lite・Gen9 の GT（main に merge 済み）、[BUG-310](../../bugs/BUG-310.md) の bootfb（9cdd5689c、SV7 の UAT で OK）

## 範囲

code は書かない。SKL・KBL・CFL（SV7 は KBL-R、UHD 620）の display（pipe A・plane 1・transcoder・DDI A の eDP、DPLL0・CDCLK、watermark、vblank の割り込み、backlight の PWM）、Gen12 の display との差、N0-lite と bootfb の作り、試験の道と切り分け、段ごとの見積もり。

## 結果

設計の本文: [design.md](design.md)。要点:

1. **段 (1) は bootfb の上に flip の present を足す**（§3.1）。query・mode・claim・release・lease は bootfb の物。present は「前の flip の latch を待つ → 表示されていない方の scanout（GGTT の display window、`scanout.c` の既存の物を 2 枚）へ GPU が blit → `PLANE_SURF` を書く（arm）」。失敗したら firmware の SURF に戻して今の copy の道へ（この boot の間）。CPU の copy が無くなり、vblank に合わせて切り替わるので tearing が無い。
2. **readout**（§3.2）は書き込み無しで PW・DC・DPLL・DDI・transcoder・pipe・plane・scaler・PSR を読み、解読（host で試す）が「灯っている pipe がちょうど 1 つ、plane 1 が linear の XRGB8888、rotation・async・offset 無し、PSR・DC5/6 無し」の時だけ flip を使う。外れたら理由の語を log に出して copy の道。mode は plane の大きさ、refresh は DP の M/N と DPLL の link rate から。
3. **watermark・DDB・CDCLK・DPLL・PLANE_CTL は触らない**（同じ大きさ・形式・tiling なので firmware の値が正しい）。書くのは PLANE_SURF（stride が違う時だけ PLANE_STRIDE も）と DE_PIPE の割り込みの mask だけ。
4. **割り込み**（§3.4）: plane 1 の FLIP_DONE（DE_PIPE の bit 3）を claim で有効に。`irq.c` の Gen9 の分岐から display の op を呼び、`device.c` の interim の handle から flip の module へ。来なくても 1 ms の poll で latch を確かめるので動く。
5. **boot の語**（§3.5）: 新しい key `i915.gen9.display=flip|copy`。既定は `display/gen9-flip.h` の定数 1 つ（0 = copy）。実機で通ったら Q1 がこれを 1 に。2026-10-14 Q1 了承。
6. **試験の道**（§4）: host（解読・refresh・register の値）、build、QEMU の回帰（T1）、SV7 の passthrough（T1。guest から host の GOP の灯した eDP が見えれば flip の log で判定、見えなければ fallback の確かめ。絵は SV7 の panel なので QMP の PNG に出ない）、**SV7 の素の起動（ユーザー）が flip の本当の受け入れ**、5330 の回帰（軽く）。
7. **見積もり**（§5、±50 %）: (1) 1.5 LW、(2) backlight 1.0、(3) mode set と外部 6.0、(4) suspend・resume 1.5、規約 0.5。合計 10.5 LW。

## 判断が要る点（Q1）

- 段 (1) の flip は passthrough の guest で eDP が見えない時、素の起動でしか確かめられない。素の起動の手順は p002 の phase.md に書く（Q1 の指示、2026-10-14）。
- 新しい boot の key（`include/kern/boot.h`・`src/kern/boot.c`）は WS237 の範囲の source として p002 で足す（HAL の API・toolchain ではない）。

## 確かめ

設計の文書だけ。Linux の file の license は先頭の行で確かめ（全部 MIT）、値は Linux 6.8.12 の行と SKL PRM Vol.12 で照らした（design.md §0・§6）。PW の STATE の bit は `SKL_PW_CTL_IDX_PW_1`=14・`PW_2`=15（`i915_reg.h:5461-5462`）から bit 28・30 で、今の N0-lite の値と一致。
