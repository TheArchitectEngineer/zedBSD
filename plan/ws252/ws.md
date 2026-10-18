<!-- awesome-plan project=zedbsd record=ws252 -->

# WS252: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT（描画の engine）の対応

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。2026-10-19 p001 を P1 が q1262 で実施中）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS245](../ws245/ws.md)（Gen12 の display の書き直し、§7 #18・#20・#21）
Resume point: p001 の design.md 版 2 は書いた（q1263）。再レビューと §8 の D1〜D6 のユーザーの判断。p002 以降は実機を得るまで planned。
<!-- awesome-plan-current:end -->

## ユーザーの指示（2026-10-15、クリック）

WS245 design.md §7 #20「RKL・ADL-S・ICL の GT（描画の engine）は起こさない。この WS は display だけ。GT の対応は別の WS。」への回答「別の WS を今立てる」。

## 前提

- 今の device.c には RKL・ADL-S/RPL-S の ID が無く i915 が付かない（efifb）。WS245 §7 #18 の回答「今のまま（efifb）」で、display も今は付けない。
- ICL（display 11）は WS245 の p014 で display を新しく支える。
- この WS は GT（render・copy・compute の engine、GuC/HuC の要否、Vulkan の executor）を対象にする。device.c に ID を足して i915 を付けるかどうかは、この WS の設計で WS245 #18 と合わせて決める（ユーザーの判断）。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | 調べと設計: 今の Gen12 の GT（TGL・ADL-P）の code との差（RKL の GT は Gen12 系、ADL-S は Gen12、ICL は Gen11）、engine・firmware・WA、実機の有無、Phase の分割。design-reviewer を通す。[phase001](phase001/phase.md)、[design.md](design.md) | in-progress（q1263 で版 2 を書いた、再レビュー待ち） |
- 決定（2026-10-19 ユーザー、クリック）: 「設計まで」（実機が無いので p001 の設計で止める。実装は実機ができてから）。

## 受け入れ条件（design.md §8 の D の結果ごと）

- 共通: design.md が design-reviewer を通る。p002 以降は実機（D5）を得るまで planned（D6 が「入れる」なら p002 だけ先に、T1 の 5330 の回帰 1 回で合格し 5320 を一覧に載せる）。
- D1=A（既定）: RKL・ADL-S/RPL-S は efifb のまま。実機ができたら p002・p007 で、実機の上で GT の初期化・Vulkan の三角形・blit・video が動き、5330 の回帰が合格。
- D1=B: 上に加え、起動の語の無い時は efifb のまま（実機で確かめる）、語の有る時に i915 が付く。DMC の firmware が image に入る。
- D1=C: 上に加え、既定で i915 が付き、実機で起動・表示・desktop が動く。
- D2=全部: ICL の実機で上と同じ通しが動く。D2=Future Work（既定）: ICL を Future Work に移し、この WS の題と目標から外す（ICL を除いた上の条件で完了）。D2=外す: 同じく外し、Future Work にも入れない。
- D4: 「変えない」なら TGL・ADL-P の MOCS の列が不変（host の golden）。「合わせる」なら Linux の列と一致し、5320 の回帰が合格。
- 最後に規約の全文の見直し（p008）が cleared。

