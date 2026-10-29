<!-- awesome-plan project=zedbsd record=ws243-p003 -->

# ws243-p003: Vulkan Video の object と H.264 の decode の命令

Parent: [WS243](../ws.md)、設計 [design.md](../design.md) §3〜§7・§13
Status: cleared（2026-10-30 q1677-i02 P5。p004・p006 の RTX 2070 の実機で H.264 の hash 4 本が software と一致、query の status も p004 で確かめた）

## 実装（commit）

- ed1e22561（新しい file）: `src/drivers/gpu/nvrtx/vk/vk-video.c`（wire の reader、capability・format・family、session（3 つの work buffer、run の 8 KiB）・memory の要求と bind・parameters（作成・template・update の順の確かめ）、RESULT_STATUS の query pool（作成・結果）、7 つの記録（video 4・query 3）を自分の struct に deep copy して object を保持、decode の実行（確かめ → DPB の entry → pic・slice の表・push → NVDEC → status で query に COMPLETE/ERROR）、`vk/vk-video-h264.c`（scaling の fall-back A・B と zig-zag → raster、DPB の entry、picture の記述、NV12 の配置）、`vk/nvrtx-video.h`、`vk/exec-copy.c`（copy engine の BL → pitch の plane copy）。
- eda161137（executor 側の共有 file の hook を 1 commit、P5 の WS250 と重なる）: `nvrtx-vk-objects.h`・`nvrtx-vk.h`・`nvrtx-exec.h`、`vk-execute.c`（decode は drain → run、NV12 の image → buffer は copy engine、NV12 を sample・quad の source にしない）、`vk-record.c`・`vk-command.c`（video の記録と drop）、`vk-dispatch.c`（route 1 行）、`vk-resource.c`（NV12 の image・view、layout の問い合わせから除く）、`vk-memory.c`（video の image は 4 KiB 整列）、`vk-device.c`（family の VIDEO_DECODE、NV12 の optimal の features）、`vk-node.c`・`nvrtx-node.h`（`nvrtx_node_video_available`・`nvrtx_vk_video_available`・`nvrtx_vk_video_decode`、timeout で session の run_lost）、`runtime.c`（video がある時 capset 176 byte、native の tag と bit 0）。
- 755587973: host 試験 `plan/ws243/tests/nvdec-host-test.{c,sh}`。
- e55368a69: 既存の `plan/tools/nvrtx/nvrtx-vk-host-test.{c,sh}` を追従（新しい file を link、node の video の口 2 つの stub）。

## 確かめた事

- build: amd64 の vmunix、rc 0・`amd64 vmunix check: PASS`・warning 0（p002 と同じ回）。libvulkan・libmedia は変えていない（`git diff 69c15ae94 HEAD -- userland/` が空）ので、venus-reg・arm64・riscv64 の libvulkan の build はしていない（変わり得ない）。
- T7 wire の往復（review M9）: `plan/ws243/tests/wire-host-test.sh`（libvulkan の video.c の static な encoder をそのまま呼んで decode・begin・parameters の作成を書き、vk-video.c の static な reader で読む）→ `ws243 wire round trip: 32 checks, 0 failed`、3 つとも全 byte を読み切った。
- host 試験: `plan/ws243/tests/nvdec-host-test.sh` → 88 checks, 0 failed（T1 pic の位置と値、slice の表、T3 push、T4 大きさと NV12 の配置、T2 scaling、DPB、T5 copy engine、T6 RM の params と devinfo）。`plan/tools/nvrtx/nvrtx-vk-host-test.sh` → `PASS nvrtx vk: 86 checks, 0 failures`（既存の試験が壊れていない）。

## 未実施・残り

- 実機（p004）: libmedia（vkvideo-probe の readback の道）で 720p・1080p の hash の照合、query の status、copy engine の readback、1080p30 の readback の速さ（R3）、compositor の frame 時間（R9）。
- 規約の全文の見直し（p005）。
- GPU の道（SAMPLED の NV12 と export、p006 案）。
