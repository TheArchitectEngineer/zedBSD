<!-- awesome-plan project=zedbsd record=ws279-p003 -->

# ws279-p003 V3D 7.1 の job の submit（noop・clear・TFU・CSD・cache の clean・hang と reset）

- 親: [ws279](../ws.md)、担当: P2（2026-10-22、worktree `agent/p2-pi5gpu`）
- Status: in-progress（実装・host・実機の確認済み、Q1 の判定待ち）
- 依存: [p002](../phase002/phase.md)。依頼の元は p002 と同じ（2026-10-22 ユーザー「整理の後、GPUのコードを実装していく、です」）。

## 目的と受け入れの条件

1. 7.1 の最小の packet（Binning・Rendering Mode Common・Render Target Part1・ZS clear・supertile・store・Clear Render Targets）で noop と clear の job。host で Mesa の 7.1 の XML から作った期待の byte と一致する。
2. TFU（0x700〜、IOC、COEF 4 つ）、CSD（0x930〜、CFG7 = 0、完了は core の bit 6、CFG4 は rev ≥ 6 で数そのもの）。
3. cache の clean（TMUWCF → L2T clean）を CSD の後に行い、CPU が GPU の書いた値を読める（design §8 U3）。
4. hang の判定（500 ms の窓で CT0CA/CT0RA が動かない）と reset（SMS の reset → PM 0x304 の bit 6 の下げ上げ → 識別の照合 → MMU・IRQ の復元）の後、job がまた動く。
5. self-test の結果が dmesg の 1 行ずつと要約の 1 行で読める。

## 作った物

- `src/drivers/gpu/bcm2711/v3d71-cl.c`: 7.1 の noop・clear の CL の image と、self-test の compute shader の QPU word（空の shader 3 語、TMU で 1 語を書く shader 8 語。Mesa 25.3.6 の 7.1 packer で作り、host 試験で照合）。
- `v3d71-job.c`: Pi 4 の `v3d-job.c` と同じ `bcm2711_v3d_job_run()` の 7.1 版。7.1 の TFU では job の `configuration` が 7.1 の ICFG、`output_format` が IOC の語全体（`v3d-job.h` の comment）。性能 counter は 7.1 でまだ読まない（0）。
- `v3d71-diagnostic.c`: `bcm2711_v3d_diagnostic()` の 7.1 版。V5 noop、V6 clear 64×64 RGBA8（全 pixel を照合）、V7 TFU raster → UB-linear（全 texel）、V8 CSD 空と store（値を照合）、V9 自分へ branch する bin list の hang → reset → noop。最後に要約の行。
- host 試験: `plan/ws279/tests/v3d71-host-test.sh`（`v3d71-cl-host-test.c`・`v3d71-cl-packet-check.py`・`v3d71-shader-host-test.c`）。

## 確認

- host（2026-10-22 P2）: `sh plan/ws279/tests/v3d71-host-test.sh build/p2-host/v3d71` → `v3d71-cl-host-test: PASS`、`v3d71-cl-packet-check: PASS (7.1 XML, noop and clear bin/render/tile)`、`v3d71-shader-host-test: PASS (11 words)`。p004 の `qpu71-format-host-test.sh` も PASS。
- 実機（2026-10-22 P2、kei@10.0.30.5 Pi 5 D0、bfea9f4a2 の build、`sudo sysctl hw.gpu.start=1`）:
  - `v3d: V5 noop ok IRQ 3/0 BFC 1 RFC 1 overflow 0`
  - `v3d: V6 clear ok RGBA8 64x64 all 4096 pixels ff317ce0`
  - `v3d: V7 TFU ok IRQ 2, all 64 tiled texels`
  - `v3d: V8 CSD empty ok IRQ 40` / `v3d: V8 CSD store ok IRQ 40 word 5a17c0de`（clean の後に CPU が値を読めた: U3 は 4.2 と同じ bit で動く）
  - `v3d: job failed kind=0 error=42 retired=0 ...`（意図した hang、bin=100000/100005）→ `v3d: V9 hang detected; reset begin` → `v3d: reset: error status 00001000` → 識別が一致 → `v3d: V9 noop ok IRQ 3/0 BFC 1 RFC 1 overflow 0` → `v3d: V9 reset ok (1 resets), noop after reset complete`（U2: ASB の無い 2712 でも reset で詰まらなかった）
  - `v3d: self-test ident ok mmu ok irq ok noop ok clear ok tfu ok csd ok reset ok`
  - `v3d: ready: V3D 7.1.10, 12 QPUs, 1150000000 Hz`
- 未実施: clear した buffer を framebuffer に写して見る確認（dmesg の全 pixel 照合で代えた）。C1 の基板。性能 counter。

## 残り・次

- p005（描画の経路）で shader state record・texture・Vulkan の native の部分を 7.1 に。TFU の UIF 出力（IOC の stride）は p005 で lowering と一緒に。
- `CONFIG_DRIVER_BCM2712_GPU` を config/ci/config-rpi5.mk で y にするかは Q1・ユーザーの判断（起動時は V0 だけなので安全側）。
