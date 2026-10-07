# WS083 の提案の差分（承認まで当てない）

ws083-p001 の設計（[design.md](../design.md) 第 3.1 版）が要る、承認を待つ変更。ここに置くだけで、tree には当てない。

| file | 中身 | 承認 |
| --- | --- | --- |
| `gpu-op-video.diff`（SHA-256 `ad5dbbf44538df0b6f4497d0dd7a1c4a9faf635387c757e83229832d8f2936b9`） | `include/uapi/gpu-op.h`（ws167 の Kei GPU command protocol）に zedBSD 独自の 14 opcode（`0x10000`〜`0x1000d`）、`GPU_OP_PROTOCOL_VERSION` を 2 に。`git apply --check` 済み（main 6eb93d0fb） | H1（未） |
| 下の「capset の native の語」 | libvulkan と i915 の実行器の間の capset の約束（UAPI の header ではない）。Venus の fork の vendor flags の空間は使わない | HD1（未） |

## capset の native の語（HD1、design.md §4.3）

- 今: i915 の実行器の capset は 168 byte（byte 160 vendor tag `0x5a424453`、byte 164 vendor flags 7）。zedBSD の Venus の host（virglrenderer の fork、`src/drivers/gpu/venus/transport.c` 35〜39）も同じ 168 byte・同じ tag で flags 3・7・15 を出す。libvulkan は 168 byte の時だけ vendor 部を読み、flags を**完全一致**で比べる（`userland/desktop/libvulkan/context.c` 165〜190）。
- 案: native の i915 だけが capset を **176 byte** にし、byte 168 に native tag `0x5a4e4154`（"ZNAT"）、byte 172 に native の機能の bit（bit 0 = `VIDEO_DECODE_H264`）。byte 0〜167 は今と同じ（vendor flags 7 のまま）。
- libvulkan: vendor 部を読む条件を `bytes == 168` から `bytes == 168 || bytes == 176` に（flags の完全一致の判定は変えない）。`bytes == 176` かつ byte 168 が native tag の時だけ byte 172 を読み、知らない bit は無視する。
- Venus の fork と stock の Venus は 176 byte を出さないので、video は立たない。fork と flags の空間を共有しない。
- i915 の実行器が bit 0 を立てるのは、VCS0 が GT にあり、boot の `i915.debug=video` がある時だけ（実機の確認 p005 まで、D19）。
