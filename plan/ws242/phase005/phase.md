<!-- awesome-plan project=zedbsd record=ws242-p005 -->

# ws242-p005: GOP の scrub 済みの末尾（256 MiB）の検査、firmware の package の確かめ

Status: in-progress（2026-10-29 P2: 実装済み・host 試験と build まで。実機・QEMU の試験は未実施。cleared にしない）
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

## 実装（2026-10-29 P2）

- `layout.c`・`nvrtx-gsp.h`: `nvrtx_wpr_scrubbed(layout)`。量は ogkm の式（frame buffer の大きさ − non-WPR heap の始まり、nvrtx の `vram_size − outer_heap`）、256 MiB 以下で 0、超えると ENOSPC。
- `gsp.c` の layout の段: `nvrtx_wpr_layout` の後に確かめ、超えれば「layout fail: the reserved …M reach below the scrubbed end of the VRAM」（「VRAM too small」と分けた）。layout の失敗は今の道（`gsp_prepare` の失敗 → `NVRTX_GSP_FAILED`、GPU は未変更）。
- RM は TU・GA で heap を縮め、Ada で scrubber を走らせるが、nvrtx は諦めるだけ（design.md §2.1・§7）。RTX の製品の典型の大きさ（RTX 2070 8 GiB 137 MiB、GA 12 GiB 233 MiB、AD 24 GiB 235 MiB）は全部 256 MiB に入るので、この検査は実機の製品では通る見込みの guard。scrub を誰がするか（GOP か devinit か）は公開の source で確定していない（review m1）。

## firmware の package の確かめ（読むだけ）

main の checkout の `build/sources/firmware/nvidia-gsp/dc85ccedc9c973682fbcf4d628ca61174bcc3120-tu-ga-ad` の 16 file を `userland/firmware/nvidia-gsp/Makefile` の production の表（大きさと sha256）と照らした: 16/16 一致（2026-10-29 P2、`stat`・`sha256sum`）。

## 確かめ（2026-10-29 P2、host と build だけ）

| コマンド | 結果 |
| --- | --- |
| `sh plan/tools/nvrtx/nvrtx-gsp-host-test.sh` | `PASS nvrtx gsp: 387 checks, SKIPPED 0`（新: 255・256 MiB と 256 MiB+1 page・257 MiB の境界、RTX 2070・GA 12 GiB・AD 24 GiB の layout、heap 200 MiB の拒否） |
| `make -j16 ZEDBSD_CONFIG=plan/tools/nvrtx/config-amd64-nvrtx.mk BUILD=build/ws242-p004 build/ws242-p004/vmunix`（p004 の tree に足した build） | rc 0、`warning:` 0、amd64 vmunix check PASS |

未実施: 実機、QEMU。
