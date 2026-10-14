<!-- awesome-plan project=zedbsd record=ws236-p005 -->

# ws236-p005: libmedia（`media_vkvideo_ops`）・Video Player・vkvideo-probe での再生

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-14。vkvideo-probe の直しまで。libmedia は無変更の見込み。test-wait（T1、下の依頼））
Phase disposition: normal

## 調べたこと（libmedia は変えない）

- libmedia（`vkvideo-runtime.c`）は decode と transfer の family が同じなら queue を 1 つで作る（1594 行付近）→ V3D の family 0 で足りる。
- session の memory は binding 1 つを 1 回の bind で（p004 の 1 page で通る）、bitstream は host-visible、image は NV12 の OPTIMAL で COINCIDE（`DPB|DST|TRANSFER_SRC`）、読み出しは `vkCmdCopyImageToBuffer` の PLANE_0・PLANE_1（p004 の CPU の copy）、status の query は family が対応すると言うので RESULT_STATUS_ONLY の pool（p004）。
- Video Player は stderr に `MEDIA backend=<名前> codec=…` を書く（`media-app/decoder.c:112`）。Vulkan Video の時は `backend=vulkan-video`。

## 変えた file

| file | 内容 |
| --- | --- |
| `userland/tests/vkvideo-probe/frame.c`・`frame.h`・`main.c` | Intel（vendorID 0x8086）以外では plane を linear の行として hash する（J4、Q1 許可） |
| `plan/ws236/tests/high-1080p30-10s.mp4`・`baseline-1080p30-10s.mp4`・`make-test-mp4.sh` | Video Player の試験の動画（testsrc2、10 秒、High は B 2 枚、Baseline）、約 1.1 MB ずつ |

## T1 への依頼（Q1 経由、実機の Pi 4）

1. image: `plan/ws236/tests/config-rpi4-video.mk`（CI の rpi4 ＋ VCHIQ ＋ VIDEO ＋ vkvideo-probe）。file の複写: `/root/ws236/` に `pb-high-352-pyramid.h264`・`.sha256`、`p-baseline-64.h264`・`.sha256`、`i-high-352-cqm.h264`・`.sha256`、`high-1080p30-10s.mp4`、`baseline-1080p30-10s.mp4`（全て `plan/ws236/tests/`）。
2. 起動の後、`dmesg | grep -E 'rpi4-(vchiq|mmal)|v3d: (vk|video)'`（`rpi4-vchiq: connected`、`v3d: vk: capset declares H.264 video decode` は Vulkan の app が開いた後に出る）。
3. `vkvideo-probe --list`（family 0 に VIDEO_DECODE と H.264、video の拡張 3 つと synchronization2）。
4. `vkvideo-probe --expect=/root/ws236/p-baseline-64.sha256 /root/ws236/p-baseline-64.h264`、同じく `i-high-352-cqm`、`pb-high-352-pyramid`（終了 code 0 が合格、4 は hash の不一致）。各 1 回、`--time` を付けて decode の時間も。
5. Video Player: `videoplayer /root/ws236/high-1080p30-10s.mp4 2>/tmp/vp.log` を desktop で再生し、`/tmp/vp.log` の `MEDIA backend=` の行、再生中の画面の PNG 2 枚、別の ssh で `top -b -n 3`（CPU の使用率）。同じことを `baseline-1080p30-10s.mp4` で。比較に software の decode（config-rpi4 の既定の image、VIDEO なし）で同じ動画の `top`。
6. seek: 再生中に 2 回 seek し、画が乱れず続くこと（PNG 1 枚）。
7. 合否（Q1 判定）: 3 が出る、4 の 3 つが exit 0（R1 が外れていれば pyramid は不一致になり得る → Baseline だけにする判断の材料）、5 で `backend=vulkan-video`・画が正しい・CPU が software より下がる、6 で乱れない。dmesg に `picture … skipped` が多数出ないこと。
