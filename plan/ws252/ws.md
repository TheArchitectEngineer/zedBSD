<!-- awesome-plan project=zedbsd record=ws252 -->

# WS252: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT（描画の engine）の対応

<!-- awesome-plan-current:start -->
Status: planned（2026-10-15 作成、担当は未割当）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS245](../ws245/ws.md)（Gen12 の display の書き直し、§7 #18・#20・#21）
Resume point: 設計の担当の割り当て待ち。
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
| p001 | 調べと設計: 今の Gen12 の GT（TGL・ADL-P）の code との差（RKL の GT は Gen12 系、ADL-S は Gen12、ICL は Gen11）、engine・firmware・WA、実機の有無、Phase の分割。design-reviewer を通す | planned |
