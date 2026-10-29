<!-- awesome-plan project=zedbsd record=ws243-p004 -->

# ws243-p004: 実機の debug（RTX 2070、`nvrtx.video=1`）

Parent: [WS243](../ws.md)、設計 [design.md](../design.md) §9・§10
Status: in-progress（2026-10-29 q1677-i02 P5。実機は未実施、10.0.10.3 の枠待ち）
担当: P5（worktree p5-ws243、branch agent/p5-ws243、基点は P6 の 9dbf7fda5）

## 範囲（Q1 の依頼、2026-10-29 q1677-i02）

- main（WS250 の取り消しと復帰・session ごとの channel、WS242 の chip の profile、WS291 の直し）を取り込み、P6 の hook を新しい形に合わせる。
- build（amd64 vmunix warning 0）と host の試験。
- 10.0.10.3 の RTX 2070 で: RM が NVDEC の channel・context・object を受けるか、GR の channel の copy の object、H.264 の decode の hash が
  software の decoder と一致するか、query の status、1080p30 の readback の速さ、compositor の frame の時間、WS250 の rtx-hang-check の非退行。
  `nvrtx.video=1` の時だけ動く。見つけた不具合を直し確かめる。
- guardrail の一時の例外（readback）と cached な memory の type はユーザーに問い中（推しのまま、[ws.md](../ws.md)）。

## main の取り込み（f422d14ae、2026-10-29 16:3x）

- `git merge --no-ff --no-commit main`（main 15a818d5b）。衝突: `platform/amd64/vmunix.mk`・`nvrtx-gpu.h`・`nvrtx-node.h`・`work.c`。印の残りが無いことを確かめて commit（親 9dbf7fda5 15a818d5b）。
- 合わせた点:
  - copy の object（GRCE、subchannel 4）: WS250 で GR の channel が `struct nvrtx_work_channel`（driver の work channel と session の channel）になったので、
    channel ごとに作る（`handle_copy`、driver は `NVRTX_HANDLE_WORK_COPY`、session は 0xc5b50010 + 番号）。最初の push の SET_OBJECT・free（3D → copy → compute）も channel ごと。
  - video の可否: node は NVDEC が動き driver の channel に copy の object がある時、session はさらに自分の channel（出来ていれば）に copy の object がある時。
  - decode の待ち（`nvrtx_vk_video_decode`）: node の lock を外して待つ（`nvrtx_vk_gpu_wait` と同じ、design R9）、喪失は `vk_node_lost`（session と channel の両方）で見る。
  - NVDEC の chid 16 と handle 0xf1f00010 が WS250 の session の最初の channel（chid 16〜47、handle 0xf1f00010 + 番号）と同じだったので、chid 48・handle 0xf1f00030・object 0xc4b00030 へ。
  - `plan/tools/nvrtx/nvrtx-vk-d3-host-test.{sh,c}` が P6 の時から link できていなかった（video の file と node の口 2 つ）ので足した。

## 確かめた事（host、2026-10-29 16:3x〜16:4x）

- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws243 build/ws243/vmunix` → rc 0、warning 0、`amd64 vmunix check: PASS`（cksum 499145062）。
- `plan/ws243/tests/nvdec-host-test.sh` 88 checks 0 failed、`wire-host-test.sh` 32 checks 0 failed、`plan/tools/nvrtx/nvrtx-vk-host-test.sh` 86 checks 0 failures、
  `nvrtx-vk-d3-host-test.sh` 209 checks 0 failures、`nvrtx-identify-host-test.sh` PASS（27 identities）、`nvrtx-gsp-host-test.sh` PASS 393 checks、
  `nvrtx-qmd-host-test.sh` PASS 733 checks、`plan/ws250/tests/nvrtx-recover-host-test.sh` 45 checks 0 failed。
- 試験の program: 同じ config の `build/ws243/bin/media-probe`・`dynamic/libmedia.so`・`dynamic/libvulkan.so`、`BUILD=build/ws243/usb` の mview と
  `plan/ws039/tests/vkhang-build.sh build/ws243/usb` の vkhang（warning 0）。
- clip（`build/ws243/clips`、git に入れない）: `h264-high-720p30.mp4`（Video.mp4 から ffmpeg・libx264 High、B 2、90 枚）・`h264-high-1080p30.mp4`（同 1080p、150 枚）・
  `video-1080p24-main.mp4`（`/home/awe/Video.mp4`、Main 149 枚）。参照は `plan/ws243/tests/nv12-hashes.py`（host の ffmpeg の decode の NV12 を media-probe と同じ形で hash）。

## 実機の script（5ebab854a）

- `PERF_BUILD=build/ws243 sh plan/ws243/tests/rtx-p004.sh OUT build/ws243/vmunix RESTORE [video|hang|restore]...`
  - video: `rtx-video-check.sh`（kernel と `nvrtx.video=1` → 再起動 → V0 dmesg の nvdec・copy の object の行、V1 clip ごとの hash の照合、V2 1080p30 の速さ 3 回、
    V3 mview の窓の frame 時間を単独と decode の間で、line を外す）。
  - hang: `plan/ws250/tests/rtx-hang-check.sh`（video の line 無し、同じ kernel）。
  - restore: `plan/ws242/tests/rtx-regress.sh restore=RESTORE`（kernel の照合、Debian と RTX の vfio-pci）。

## 実機（未実施）

## 未実施・残り

- 実機の全部（上）。p005（規約の全文の見直し）、p006 案（GPU の道）。
