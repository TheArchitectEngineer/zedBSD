<!-- awesome-plan project=zedbsd record=ws252 -->

# WS252: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT（描画の engine）の対応

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。2026-10-19 p001 cleared（q1265、design.md 版 3.1））
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS245](../ws245/ws.md)（Gen12 の display の書き直し、§7 #18・#20・#21）
Resume point: p001 cleared、p002〜p008 は実機まで planned、判断 D1・D2・D3・D5 待ち（design.md 版 3.1 §8。聞く順は D2 → D1-R →（D2=全部の時だけ）D1-I → D3 → D5）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（2026-10-15、クリック）

WS245 design.md §7 #20「RKL・ADL-S・ICL の GT（描画の engine）は起こさない。この WS は display だけ。GT の対応は別の WS。」への回答「別の WS を今立てる」。

## 前提

- 今の device.c には RKL・ADL-S/RPL-S の ID が無く i915 が付かない（efifb）。WS245 §7 #18 の回答「今のまま（efifb）」で、display も今は付けない。
- ICL（display 11）は WS245 の p045 で display を新しく支える（p014 は canceled で p045 へ、`plan/ws245/ws.md:164,215`）。
- display の xd の表（`intel/display-tables-xd.h`）は RKL・ADL-S・RPL-S・ICL の ID を既に無条件に持つ。attach を決めるのは `i915.c:80-89` の `identifiers[]` だけ（design.md §1）。
- この WS は GT（render・copy・compute の engine、GuC/HuC の要否、Vulkan の executor）を対象にする。device.c に ID を足して i915 を付けるかどうかは、この WS の設計で WS245 #18 と合わせて決める（ユーザーの判断）。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | 調べと設計: 今の Gen12 の GT（TGL・ADL-P）の code との差（RKL の GT は Gen12 系、ADL-S は Gen12、ICL は Gen11）、engine・firmware・WA、実機の有無、Phase の分割。design-reviewer を通す。[phase001](phase001/phase.md)、[design.md](design.md) | cleared（q1265、版 3.1、[design-review-3.md](design-review-3.md) の Minor 1〜9 に対応） |
| p002 | Gen12 の platform の記述の行（TGL・ADL-P・RKL・ADL-S・RPL-S）、ID は足さない、gen12-golden を直して platform ごとに、Linux から期待を抜き出す script（design.md §4・§5・§6.1）。xd の表との統合は WS245 と合わせる（Q1） | planned（実機まで） |
| p003 | RKL・ADL-S・RPL-S の attach: `identifiers[]` と `device.c` に ID（xd の表は既にある）、D1-R の値に従う（A: config.mk の build option、前例 `CONFIG_AMDRDNA_CONTROL`。共有の Makefile・kernel-options.list・MAC-T001 の追従は Q1 と調整／B: 起動の語／C: 既定）、DMC | planned（実機まで） |
| p004 | ICL の GT の core と WA の list（D2=全部のとき） | planned（実機・D2） |
| p005 | ICL の compiler と state（Gen11 の EU、gen110） | planned（p004） |
| p006 | ICL の video・PXP の除外・attach（D1-I）・DMC | planned（p005） |
| p007 | 実機の通し（GT・Vulkan・blit・video、5330 の回帰）。RKL・ADL-S 3 LW ＋ ICL 1〜3 LW。依存は design.md §5 の群ごとの表 | planned（実機） |
| p008 | 規約の全文の見直し | planned（最後の実装） |
- 決定（2026-10-19 ユーザー、クリック）: 「設計まで」（実機が無いので p001 の設計で止める。実装は実機ができてから）。

## 受け入れ条件（design.md §8 の D の結果ごと、版 3.1。D1 は群ごと: D1-R＝RKL・ADL-S/RPL-S、D1-I＝ICL で D2=全部の時だけ。下の A・B・C は各群に当てる）

- 共通: design.md が design-reviewer を通る。p002 以降は実機（D5）を得るまで planned（実機の前に p002 を入れる案は 2026-10-19「設計まで」で決定済み）。
- D1=A（既定）: 既定の image では RKL・ADL-S/RPL-S（と ICL）は efifb のまま（`identifiers[]`・`device.c` の ID は build option の時だけ入る）。実機ができたら p002・p003・p007 で、試験の config.mk で ID の option を立てた image の上で GT の初期化・Vulkan の三角形・blit・video が動き、5330 の回帰が合格。option の無い既定の image で efifb のままである事も確かめる。
- D1=B: 既定の image に ID を入れ、起動の語の無い時は efifb のまま（実機で確かめる）、語の有る時に i915 が付き上の通しが動く。DMC の firmware が image に入る。
- D1=C: 既定で i915 が付き、実機で起動・表示・desktop と上の通しが動く。
- D2=全部: ICL の実機で上と同じ通しが動く（ICL の attach は D1-I に従う。群の値が違う時の image は design.md §5）。D2=Future Work（既定）: ICL を Future Work に移し、この WS の題と目標から外す（ICL を除いた上の条件で完了）。D2=外す: 同じく外し、Future Work にも入れない。
- Gen12 の MOCS（TGL・RKL の表、entry 63）は WS252 の外（[BUG-335](../bugs/BUG-335.md)）。この WS では TGL・ADL-P の MOCS の列が不変である事を host の golden で確かめる。
- 最後に規約の全文の見直し（p008）が cleared。
