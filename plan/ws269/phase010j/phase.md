<!-- awesome-plan project=zedbsd record=ws269-p010j -->

# ws269-p010j: depth と vkdemo の offscreen

- ID: ws269-p010j（親 [ws269](../ws.md)）
- Status: in-progress（q1387-i01 は前半。fragment の command への ZLS の word の受け渡し（P1 の render-job）と実機は後半）
- Disposition: normal
- Queue: q1387 / q1387-i01（2026-10-20 P2、前半）
- 設計: [design-p010.md](../design-p010.md) 版 2.3 の §3・§4.3・§10 の p010j
- 依存: O-p010i（render-draw.c、main に merge 済み）

## 受け入れ条件（design-p010.md §10 の p010j の行）

depth（D32・D16、ZLS の load・store、test）と vkdemo の offscreen の通し、gpu-selftest の `depth`。host 試験、build。実機: gpu-selftest の `depth`。

## q1387-i01 の範囲（前半、Q1 の依頼）

ZLS の load・store の register の word、ISP の depth の state（PPP の ISP の word）、depth の image の layout、render-pass.c の depth attachment の受け入れ、vkstage の depth の shader の compile。触らない: render-job・transfer-job・vk-transfer・vk-queue.c・render-command。

## 作った物（2026-10-20 P2）

- `src/drivers/gpu/powervr/render-depth.{h,c}`（新規、純粋な関数）: `powervr_render_zls_words` が job の depth から fragment の command の depth の word を作る。
  - CR_ISP_ZLSCTL: extent は physical の大きさを 16 の tile で数え 2 に揃えて 1 を引く。twiddled なら load・store の twiddled。format は D32_SFLOAT が F32Z（0）、D16_UNORM が 16BITINT（2）。zloaden・zstoreen と force。
  - CR_ISP_ZLOAD_BASE（load と store の address）、zls_stride（layer の大きさ）、CR_ISP_BGOBJDEPTH（clear の値: D32 は float の bit、D16 は整数だけで `_mesa_float_to_unorm(x, 16)` と同じ偶数への丸め）、`process_empty_tiles`（load か store がある時）。
  - store は STORE の時、load は LOAD の時か「STORE で area が depth の view の全体でない時」（Mesa の `pvr_sub_cmd_gfx_job_init`）。ZLS の tile（32×32）より小さい buffer の load・store は ENOTSUP（Mesa の alignment の transfer は p011）。
- `render-pass.{h,c}`: job の depth に physical の幅・高さ、twiddled、layer の大きさ、`partial` を足した。**部分の renderArea で depth の STORE がある時の EPERM をやめ**、`partial` を立てて受ける（Q1 の依頼。design-p010.md 版 2.3 の注記）。depth の view は level 0 だけ（他は ENOTSUP）。clear しない時の値は 1.0。vk-pass.c の EPERM の log の枝は残っているが、もう通らない。
- ISP の depth test・write・compare op の PPP の word は p010i の `powervr_draw_ppp_build` にある（depth の attachment がある時だけ test、`depthWriteEnable` で書く）。vkstage の depth の pipeline（colour の shader、D32、LESS）で確かめた。
- depth の image の layout: image-state.c の OPTIMAL の twiddled（2 の冪に padded）をそのまま使う。D32・D16 の format の表はある（ZLS の format は上）。
- `platform/riscv64/vmunix.mk` に render-depth.c。

## 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws269/tests/p010/run.sh OUT`（`depth-host-test.c` を足した、`pass-host-test.c`・`draw-host-test.c` を直した） | `render-depth host test: 12 passed, 0 failed`、`render-pass host test: 16 passed`（部分の area と depth の layout の field）、`render-draw host test: 37 passed`（vkstage の depth の pipeline: colour の shader が key 付きで compile、ISPA は LESS で書く）。他（stage・image・target・kick・program・command・selftest・data・render・transfer・render-load）も PASS |
| executor の host 試験 | `sh plan/ws269/tests/p009/run.sh OUT` | 165 passed, 0 failed |
| M9 の照合（tree に入れない） | `build/p010j/m9.c`（Mesa 26.2.5 の gen_pack_header を `python3 -I` で走らせた cr.h） | `M9: 12 words/values compared, 66 fields, 0 mismatches`（ZLSCTL と ZLOAD_BASE、D32 の clear・store、load、部分の area 1024×512、D16 の 512×256、linear の 48×48、store なし） |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010j/vf2 vmunix -j16` | rc=0、warning 0 |

- 未実施: 実機（gpu-selftest の `depth`）、vkdemo の offscreen の通し（draw の kick の後）。

## 残り（後半）

- P1 の render-job の fragment の command に `powervr_render_zls_words` の word（ZLSCTL・ZLOAD_BASE・BGOBJDEPTH・zls_stride）と `process_empty_tiles` を入れる。
- vkdemo の offscreen（`--offscreen`）と gpu-selftest の `depth` の実機。
- 32×32 より小さい depth の load・store（Mesa の alignment の transfer）、stencil、level 0 以外の depth（p011）。

## q1388-i01（2026-10-20 P1）: ZLS を render-job へ

- `render-command.{c,h}`: fragment の setup に depth（`isp_zlsctl` 16+120、`isp_zload_store_base` 16+128、BGOBJDEPTH、`zls_stride` 468、flags の DEPTHBUFFER bit 7）。depth の無い command は前と同じ。
- `render-job.c`: depth の ENOTSUP を外し、P2 の `powervr_render_zls_words` の control・base・background depth・stride を fragment の command に。process_empty_tiles は ISP_CTL で常に立てている。
- 確かめ: command-host-test 29/0、p010 の host 試験 15 本 0 failed、p009 165/0、3 つの build warning 0。実機は未確認。
