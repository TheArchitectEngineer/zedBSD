# Artifact: zedbsd-parity-c1-e99（C1 compute 陽性対照、zedBSD が実機へ提出した最終 bytes）

| 項目 | 値 |
|---|---|
| artifact ID | `zedbsd-parity-c1-e99` |
| 生成元 | zedBSD 作業ツリー（HEAD e298eabe + 未コミット差分 = `e97-e103-changes.patch`）、`src/drivers/gpu/i915/parity/eu_test.c` `parity_eu_test_build_batch()` と同ファイルの `eu_marker_cs[]`／IDD 設定 |
| image | vmunix sha256 `96274bf33473669aee57bc46aa72b5c918b061482fe83d2b0a3a7e984703e24f`、hdd-image `1a5f7878db284b018f832d18e16afe1e7a7bf60c0fe6a305a68d09ea152e6017`、BOOTX64.EFI `57f8eab693354e318aa694753bb7ecedc246efba38943b9fb2259de0f0c60f4f` |
| build | clean、`BUILD=build/eu-e99 CONFIG_DRIVER_PCI_I915_PARITY=y ZEDBSD_TEST_CPPFLAGS=-DPARITY_EU_TEST=1` |
| 対象 GPU | Gen12.0 LP、ADL-P 8086:46a8（VFIO）、RCS0、execlists |
| VA | shared page `0x100400000`（IDD @+896、kernel @+1024、READY/EU/DONE/CS marker @+0xc00/0xc20/0xc28/0xc30、IDD 読み戻し @+0xd00、kernel 読み戻し @+0xe00）、batch `0x100401000`。SBA の全 base = `0x100400000`（batch 内） |
| batch | 1288 bytes（322 dword）、sha256 `c6437cf230c5541a70969ba6660ad1c06ed95bcc117e206be3c7126d5daf4f50`、FNV-1a64 `5dfb47d3c10b0560` → `e99-c1-batch.bin` / `.hex` |
| kernel | 144 bytes（36 dword）、sha256 `4faa29cd303b66cc02c36ff18018bc86c8a4b4db30c0a3d293900b90da988398` → `e99-c1-kernel.bin` |
| IDD | 32 bytes、sha256 `8438f3232c8b994da024199ce0bba23f2f97479152d6bf5aae8e43624e8312eb` → `e99-c1-idd.bin` |
| 固定 state | VFE／MIDL／walker は batch 内。batch 外の state object は shared page（IDD＋kernel）のみ。FNV-1a64（IDD..kernel 末尾の連続領域）`444e3a7a4e9c1abd`（E-98 と同値＝shared page 側は不変） |
| 取得方法 | 提出 object の CPU mapping を提出後に読んだログ行（`EU-TEST batch[...]`／`fixture-idd`／`fixture-kernel`）から `tools/eu_artifact.py extract` で生成。ログ `e99-run-parity-hw-eu.log` |
| 実機結果 | **PASS**：READY=0xc0ffee10、EU=0xc0ffee02、DONE=0xc0ffee20、CS=0xc0ffee30、request 完了（HWSP seqno 到達＋CSB で context complete、polls=16）、kernel context への park request も完了、reset なし、teardown 正常 |

## E-98 提出 batch（HANG）との全差分

`tools/eu_artifact.py diff e98-batch-as-submitted.hex e99-c1-batch.hex`

```
dword   6  byte 0x0018  old=61041310 new=69041310 xor=08000000
dword 261  byte 0x0414  old=61041312 new=69041312 xor=08000000
```
これ以外の差はありません（旧 hex ファイル末尾の 6 dword は保存時の 0 埋めで、batch_dwords=322 の範囲外）。batch VA、診断 copy、順序、長さは不変です。

## Linux replay（`linuxvm/linux-c2-replay.c`、sha256 先頭 d8cc6a8e5ee91a0a）との意図的な差

完全同一 batch ではありません。差は次の 2 点で、opcode／mask／長さ／同期命令は比較から除外していません。

| 差 | zedBSD | Linux replay | 理由 |
|---|---|---|---|
| batch VA | 0x100401000 | 0x100600000 | zedBSD は shared page の次頁に batch object を置く。batch 内に自己参照アドレスは無い（MI_BATCH_BUFFER_START は ring 側） |
| 診断 copy | MI_COPY_MEM_MEM × 44（IDD 8 語＋kernel 36 語を +0xd00／+0xe00 へ） | 無し | GPU の PPGTT 越しに IDD／kernel が読めることの確認用 |

Linux 側で本 artifact をそのまま流す比較（L-ZED-EXACT）は未実施です（今回 zedBSD 側が PASS したため、指示の分岐 A に該当）。
