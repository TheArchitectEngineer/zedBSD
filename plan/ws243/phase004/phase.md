<!-- awesome-plan project=zedbsd record=ws243-p004 -->

# ws243-p004: 実機の debug（RTX 2070、`nvrtx.video=1`）

Parent: [WS243](../ws.md)、設計 [design.md](../design.md) §9・§10
Status: cleared（2026-10-29 q1677-i02 P5。範囲の確かめは全部済み: RM の NVDEC の channel・copy の object、H.264 の hash 3 本一致、query の status、1080p30 の readback の速さの計測（3.4 fps、30 fps に届かない）、compositor の frame 時間、hang の非退行。速さは Q1 の判断で GPU の道（p006）で取り組む）
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

## 実機（2026-10-29 22:21〜22:37、10.0.10.3 の枠 22:25〜23:10、lock は P5）

`PERF_BUILD=build/ws243 sh plan/ws243/tests/rtx-p004.sh build/ws243/p004/run1 build/ws243/vmunix build/ws243/p004/restore-vmunix video`、
同じ形で `run1h … hang`、`run1r … restore`（RESTORE は始めに機械の NVMe から写した kernel、cksum 4097497588）。要約は `build/ws243/p004/run1*/summary.txt`。

| 項 | 結果 |
| --- | --- |
| kernel と line | zedBSD 10.0.30.5、vmunix 499145062（wanted 499145062）、`nvrtx.video=1` 1 行 |
| V0 | `nvdec-channel ok: class c4b0, channel 48 on runlist 1, context 4096 byte`、copy object 4 本（driver の channel 4、session の 16〜18）、拒否 0 |
| V1 hash | 3 本とも software の decoder（host の ffmpeg、nv12-hashes.py）と一致: h264-high-720p30 MATCH 90/90、h264-high-1080p30 MATCH 150/150、video-1080p24-main MATCH 149/149。backend=vulkan-video、error=0。時刻の差は 1 µs の丸め（66666 と 66667）だけ |
| query の status | libvulkan は decode の family に queryResultStatusSupport を出し、libmedia は RESULT_STATUS の pool を使い、COMPLETE 以外は EBADMSG で止める。全 picture が error=0 で hash 一致 → 全部 COMPLETE（status の値を直に数えたのではない） |
| V2 1080p30 の readback | 3 回: 150 枚を 43.6・43.7・44.1 s、**3.4 fps**。libmedia の内訳 avg_us submit 約 5000・wait 0・copy 約 264000 |
| V3 compositor | mview の窓: 単独 59.69 fps（avg 16.71 ms、max 52 ms、hitch 1）、decode の間 59.80 fps（avg 16.68 ms、max 27 ms、hitch 0）。悪化なし |
| hang（rtx-hang-check、line なし） | S0 49.78 fps・control ok、S2 lost 2001 ms・mview 完走 31.05 fps（停滞 4331 ms 1 回）・stopped 1 stuck 0、S3 5/5 lost・channel 24〜28・control ok、S4 2/2 lost・12.74 fps（停滞 4333 ms）、S5 200 ms で終わる・control ok。WS250 の基準（S2 30.91 fps 停滞 4334 ms、S4 12.90 fps 停滞 4333 ms）と同じで非退行。S1（--probe）は流していない |
| restore | zedBSD の kernel 4097497588（wanted 4097497588）、Debian 6.12.111、RTX は vfio-pci、BootOrder 0002,0000。段の中の boot line の掃除は zedBSD が居ない時に走り FAIL と出たが、その後 zedBSD を起こして cfg を直に読み、始めと同じ（nvrtx.* の行なし）を確かめた |

- 速さの原因: nvrtx の memory type は 1 つ（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT、`vk-device.c`）で、readback の buffer は VRAM にあり、CPU は BAR1 の WC の窓（`node.c` の mapping）から読む。
  1080p の NV12 約 3.1 MB を 264 ms で、**約 12 MB/s**。GPU の decode と copy engine は待ちに出ない（wait 0）。30 fps には CPU の読みを約 9 倍以上に速くする必要がある。
- 気づいた事（WS243 の外、記録だけ）: 起動の dmesg に `vram-window bar1 before the tables: fault …`・`retry 1 (the fault record was cleared)` の行（WS242 の VRAM の窓の確かめ、毎回の物か未確かめ）。

### 判断（Q1、2026-10-29 22:3x、ユーザー不在のため）

- Q1（原文）:「(b) で進めてください。design.md で GPU の道が本線と決めてあり、guardrail「画像の処理は GPU で」に沿うので、p006 を次に。(a) の host cached な memory type は WS243 の外の提案として記録だけ（Q1 がユーザーに聞く）。」
- (a) に要る物（記録だけ）: executor 全体で、host の cached な system memory（GPU からは PCIe の snoop の付いた sysmem の aperture）の memory type を足し、readback の buffer をそこに置く。readback を使う他の app にも効くが、memory の割当と GPU の page table の両方の変更になる。

## 機械の終わりの状態（2026-10-29 22:37、lock を返した）

- Debian で起動中（RTX は vfio-pci）。NVMe の zedBSD: `/boot/boot0/vmunix` 4097497588（P1 の rtxk、始めと同じ）、`zedbsd.cfg` は始めと同じ、`/bin/wayland` 825213100・`/lib/libkeiland.so` 1274512932（変えていない）、`*.p012old` に触れていない。
- 残した物: `/boot/boot0/vmunix.old`（script が上書き）、`/root/ws243/`（media-probe・libmedia.so・libvulkan.so・mview・clip 3 本）。

## 未実施・残り

- 1080p30 の速さ（30 fps）は readback の道では未達。GPU の道（p006）で測り直す。p005（規約の全文の見直し）。S1（nvrtx.kill=0 の probe）は流していない。
