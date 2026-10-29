<!-- awesome-plan project=zedbsd record=ws242-p005 -->

# ws242-p005: GOP の scrub 済みの末尾（256 MiB）の検査、firmware の package の確かめ

Status: planned
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: ws242-p003
設計: [design.md](../design.md) §2.1、第 2 版の V8（設計の第 2 版の表の p005a）。review の m1・m7(b)(d)

## 範囲

1. layout の後に「VRAM の末尾から外の heap（non-WPR heap）の始まりまで」（ogkm の `fbSize − gspFwRsvdStart`、nvrtx の `layout->outer_heap`）が
   256 MiB 以下かを確かめ、超えれば layout の段で諦める（GPU は未変更）。印は「VRAM too small」と分ける。
   scrub を誰がするか（GOP か devinit か）は公開の source で確定していない（m1）。Ada の scrubber は入れない。
2. host 試験で境界（255・256・257 MiB、ogkm の式から期待値）と RTX 2070・GA・AD の典型の VRAM の layout。
3. firmware の package: main の checkout の `build/sources/firmware/nvidia-gsp/dc85cced…-tu-ga-ad` の中身を `userland/firmware/nvidia-gsp/Makefile` の
   大きさ・sha256 の表と照らす（読むだけ）。

## 受け入れ

- host 試験が通る。amd64 vmunix が warning 0。
