# ws269-p010m — format の広げ（純粋な関数の側）

- 親: [ws269](../ws.md)。Queue: q1389 / q1389-i01（P2、2026-10-20）。状態: in-progress（host 試験・M9 の pack・VF2 build まで。T1 の実機は未実施）。

## 範囲と使われ方

userland を読んだ結果（Keiland・vkdemo）: image は B8G8R8A8_UNORM・R8G8B8A8_UNORM・D32_SFLOAT だけ。vertex attribute は
R32G32/R32G32B32/R32G32B32A32_SFLOAT と、`userland/desktop/wayland/batch.c:300` の **R32_SFLOAT**（表に無く、pipeline が通らなかった）。
sRGB・R8・R8G8・R16 の image は使われていない。このため R32_SFLOAT の追加を最優先にし、他は pack・表の側を先に用意した。

## 変えたもの

- `src/drivers/gpu/powervr/image-state.{c,h}`: `POWERVR_FORMAT_FLOAT` を追加。R32_SFLOAT（tex 18、pbe 0x36、vertex buffer・sampled・transfer）・
  R16G16B16A16_SFLOAT（tex 28、pbe 0x0A、sampled+linear filter・transfer）を追加。R32G32_SFLOAT（tex 34）・R32G32B32A32_SFLOAT（tex 61）に
  texture の code と sampled・transfer の features を付けた。float の format は colour attachment を名乗らない（compiler の pack が U8888 だけ）。
- `transfer-program.c`: `half_from_float`（Mesa `pvr_float_to_f16(value, false)` と同じ、float 演算なし）と、
  `powervr_transfer_clear_colour` の pack を Mesa `pvr_pack_clear_color` に合わせて広げた: R32/R32G32/R32G32B32A32_SFLOAT は raw bits、
  R8・R8G8 unorm・sRGB の2つ・R16G16B16A16_SFLOAT は half（sRGB と F16x4 は2 word）。U8888 unorm は従来どおり。

## 確認

- host: `sh plan/ws269/tests/p010/run.sh build/p010fmt/host` 全 PASS（image 55、transfer 27 ほか 0 failed）。image の「表に無い format」の試験は R16_UNORM に変えた。
  transfer に sRGB・F16x4・R8・R8G8・R32・R32x4 の clear pack の試験を追加。
- M9: Mesa 26.2.5 の `pvr_float_to_f16` の本体を `build/p010fmt/h.c` に抜き出し、`half_from_float` と代表値15個＋0x1235 刻みの全正の float で比較、不一致 0。
- VF2: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010fmt/vf2 vmunix -j16` rc 0、warning 0。

## 未実施・残り

- 64・128 bit の raw copy（transfer の program が pixout に n word を移す・`tiles_in_flight` の pixel 幅）、transfer の PBE source format の format 別の選択、
  render-program の `pbe_source` の F16/32-bit 分け、render-load の U8888 以外、float の blit は今回入れていない（ENOTSUP のまま）。
- P1 の file で format を止めている所（今回は触っていない）:
  - `render-job.c` `job_check`: colour が `texel_bytes != 4` なら ENOTSUP。外すには render-program の pbe_source を F16 の format に広げ、
    pixel 幅（output registers）を texel に合わせてから `texel_bytes <= 16` にする。
  - `vk-transfer.c:244`: copy_image で format が違えば ENOTSUP（Vulkan の互換 format の copy）。raw format が 8/16 byte に広がった後、
    `texel_bytes` が同じなら許す形にできる。
  - clear の regs（`powervr_transfer_regs_build`）は U8888 以外の source format を選んでいないため、clear pack が広がっても kick 側はまだ U8888 だけ。
- 片付け（Q1 に依頼）: `build/p010fmt/`。
