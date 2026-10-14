# T2: WS037 nvrtx の方式 B 用 image（2026-10-14、main 95027c8f6）

試験は未実施（host 10.0.30.5 は準備中）。image の build と R0 の boot-test だけ。

- 手順: `yes | make firmware-licenses ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk` の後、run ごとに
  `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/<run> NVRTX_RUN=<run>`
- build の時間: R0 57 s、R1 158 s、R2 89 s、R3 122 s、R4 174 s（全部 rc 0、zedBSD の warning 0。make の jobserver の注記 1 行だけ）

| run | image（/home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/ 以下） | sha256 | zedbsd.cfg の追加の行 |
| --- | --- | --- | --- |
| R0 | R0/hdd-image.img | 2d41339e75b19eb3431c046b0659a1388ab6933a87863a98d956f8e4c08ec8ec | なし |
| R1 | R1/hdd-image.img | ead9a53ecfe820fda241bc56d39ef21f99c33253d80fc8a27eed04649d7f233d | nvrtx.gsp=1 / nvrtx.stop=P1a |
| R2 | R2/hdd-image.img | b7ba5b3c5024d96410814ae1784489d07dad4cd4709272615d324d7400a12351 | nvrtx.gsp=1 / nvrtx.stop=P2a |
| R3 | R3/hdd-image.img | 7973c942237b7dae574a32aee998ce8bff9c2156115921f5b49b985ba77039af | nvrtx.gsp=1 / nvrtx.stop=P3 |
| R4 | R4/hdd-image.img | a218496f7330177c45d5be9518f510ad4fc2fd26b9e0a7ea04b0bd1e171d3f61 | nvrtx.gsp=1 |

各 image は約 2.2 GB。zedbsd.cfg は各 run の directory の `zedbsd-native-uefi-graphical-n-login-n-kmsg-y[-<語>].cfg`（kernel=vmunix, video=640x480 の後に追加の行）。

## R0 の boot-test（GPU 無しの QEMU、QEMU の証拠）
`OUTPUT=build/ws037-nvrtx/boot-r0 plan/tools/boot-test.sh build/ws037-nvrtx/R0/hdd-image.img` → PASS（login prompt）。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/boot-r0/login.png（init: system running、getty_console、`login:`）。
nvrtx は NVIDIA の無い機械で害無く login まで到達。画面は init の行まで流れていて nvrtx の N0/N1 の行は撮れていない（kernel の行は上に流れた）。N0/N1 の文言の確認は実機の R0（仮想 VGA）で。
QEMU は終了済み、background の process 無し。
