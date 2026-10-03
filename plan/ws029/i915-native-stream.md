# WS029 i915 native command stream（確定版、p005）

`/dev/gpuN` の `GPU_COMMAND`（同期受理）と `GPU_COMMAND_SUBMIT`（非同期、完了通知）に渡す `buffer` の backend 定義。root 専用 ABI で、batch の内容は検証しない（header と relocation だけを検証する）。UAPI（`include/uapi/gpu*.h`）は変更しない。実装: `src/drivers/gpu/i915/i915.c` `drv_i915_stream_parse` / `i915_submit_stream`、fixture: `plan/ws029/tests/i915-stream-test.c`。

## 配置（little endian、4 byte 揃え）

```
offset  size  field
0       4     magic            0x31394958 ('XI91')
4       4     version          1
8       4     engine           0 = RCS0, 1 = BCS0
12      4     relocation_count 0..64
16      4     batch_dwords     1..16384
20      4     flags            0
24      8     reserved         0
32      16*n  relocations      n = relocation_count
32+16n  4*m   batch dwords     m = batch_dwords、末尾は MI_BATCH_BUFFER_END (0x05000000)
```

relocation（16 byte）:

```
0   4  dword_offset   batch 内の dword index。dword_offset と dword_offset+1 に、handle が指す
                      resource の GPU 仮想 address（64 bit、下位 dword が先）を書く。
                      dword_offset + 1 < batch_dwords
4   4  reserved       0
8   8  handle         同 session の GPU_RESOURCE_CREATE が返した handle
```

`bytes` は正確に `32 + 16*relocation_count + 4*batch_dwords` でなければならない（EINVAL）。

## 実行

- K は stream を検証し、session の batch pool（session ごと最大 32 object、必要 size 以上の空き object を再利用）へ batch dwords を copy し、relocation を patch する。U の buffer は変更しない。
- batch は session の PPGTT（48 bit）で `MI_BATCH_BUFFER_START_GEN8`（bit 8 = PPGTT）から実行される。request は engine ごとに直列（1 engine 1 request）。
- `GPU_COMMAND_SUBMIT` の `bytes == 0` は marker（batch なし、`flags` は core の `GPU_COMMAND_CONTEXT_FENCE`）。`timeline` 0/1 = BCS0、2 = RCS0。stream 付き submit では header の `engine` が engine を選び `timeline` は無視する。
- 完了は engine の HWSP seqno と user interrupt で判定し、`drv_gpu_complete(completion, 0)` を lock 外で呼ぶ。失敗（isolate/fault）は `EIO` 等。

## job（`GPU_JOB_RESERVE`/`COMMIT`/`CANCEL`/`CAPACITY`）

- `timeline`: 1 = BCS0、2 = RCS0（0 は EINVAL）。
- reserve は request slot（engine ごと 32）を確保し callback を保持する。満杯は `EAGAIN`。
- commit は marker request として投入する。cancel(flags=0) は slot 解放、`CANCEL_FAULT` は slot と callback を保持し、後の stop/isolate/fault が `EIO` で公開する。
- capacity は engine の空き slot 数。

## 試験 batch（`/bin/gpu-i915-test`、BCS0）

- copy: `XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | 8`、`BLT_DEPTH_32 | BLT_ROP_SRC_COPY | dst_pitch`、`0`、`rows<<16 | width`、dst lo、dst hi、`0`、src_pitch、src lo、src hi、`MI_BATCH_BUFFER_END`（relocation: dword 4 と 8）。64 KiB = 256 px × 64 行 × 4 byte、pitch 1024。
- fill: `XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | 5`、`BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | pitch`、`0`、`rows<<16 | width`、dst lo、dst hi、value、`MI_NOOP`、`MI_BATCH_BUFFER_END`（relocation: dword 4）。
- store: `MI_STORE_DWORD_IMM_GEN4`（PPGTT）、addr lo、addr hi、value、`MI_BATCH_BUFFER_END`（relocation: dword 1）。

opcode の値は `src/drivers/gpu/i915/linux/i915-commands.inc`（Linux `intel_gpu_commands.h` の MIT 転記）と一致させる。

## harness 向けの対応付け

K は resource 作成時に `i915: resource session=<s> slot=<n> bytes=<b> phys=0x<p> va=0x<v> handle=<h>` を log する。`gpu-i915-test` は `GPUI915 PASS ... src_handle=<h> dst_handle=<h>` を印字するので、harness は handle で対応付けて `phys` を得て `pmemsave` の範囲を決める。
