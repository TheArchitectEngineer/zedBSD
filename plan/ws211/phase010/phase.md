<!-- awesome-plan project=zedbsd record=ws211-p010 -->

# ws211-p010: compositor の 1 コマの CPU の仕事（replay・libvulkan・kernel の decode）を縮める

Parent: [WS211](../ws.md)
Status: in-progress（実装 agent/p4 f7873c64f・af2dd3887、host 試験 PASS・build warning 0。place25 で Pi 待ち）
Phase disposition: normal
Queue: P4（2026-10-12 ユーザー「ぜんぶやりましょう。」、Q1 の割り当て）

## 由来

- 2026-10-12 ユーザー「ぜんぶやりましょう。」（Q1 の提案: 変わらない層の CPU の仕事を使い回して 1 コマを縮める）。
- Pi の数字（place23、800x480 と 1024x600）: draw→submit の p50 約 24〜30ms が解像度でほとんど変わらない（CPU の仕事）。ws213-p003 の Pi の計測で walk は 1ms 未満、replay p50 約 18ms・最長 56ms。kernel の v3d の perf に decode の hold_us（最大 21ms）。

## 範囲

- compositor（`userland/desktop/wayland/`）、libvulkan（`userland/desktop/libvulkan/`）、bcm2711 の driver（`src/drivers/gpu/bcm2711/`）の中。
- HAL の API・toolchain は変えない。UAPI（`include/uapi/`）を変える時は先に Q1 に。

## 1. 内訳（Pi の log、place23 の 2 つの boot）

元: scratchpad の `res480-logs`（800x480、479 コマ）と `p23b-logs`（1024x600、228 コマ）の session.log・kernel.log。集計の script は scratchpad の `p4-p010/lat.py`（KWL LAT の時刻の差、KWL DRAWS の cmds で分けた）。

### 1.1 KWL LAT の区間（ms、800x480 / 1024x600）

| 区間 | 中身 | p50 | p90 | 最長 |
| --- | --- | --- | --- | --- |
| draw→recorded | main の walk、render thread への受け渡し、replay（下の 1.2） | 14 / 18 | 39 / 38 | 100 / 92 |
| recorded→queued | `vkQueueSubmit`（kernel への 1 往復） | 4 / 5 | 10 / 10 | 90 / 36 |
| queued→submit | `vkQueuePresentKHR`（libvulkan の WSI: copy の command buffer の Begin・End、Submit の 3 往復） | 4 / 5 | 9 / 12 | 51 / 54 |
| draw→submit | 合計 | 24 / 30 | 59 / 59 | 171 / 121 |

- walk（main の一覧作り）は `KWL PERF walk` で p50 0.15〜0.9ms（最長は 1 回だけ 53ms、ほかは 3ms 未満）。draw→recorded のほとんどは replay。
- replay（`KWL PERF walk ... replay`、5 秒の窓ごとの p50）は 3〜27ms。描く物が 1〜2 個の窓（App Home を開いていない小さなコマ）でも 8〜13ms。

### 1.2 replay の中身（compose.c の `compose_replay_commands`）

1 コマに kernel への往復が 4 回ある。

1. `vkBeginCommandBuffer(copy_command)`（往復）
2. host image の copy の記録（ほとんどのコマで 0 個）
3. `vkEndCommandBuffer(copy_command)`（往復。中身が空でも送る）
4. `vkBeginCommandBuffer(command)`（往復）
5. 一覧の再生（`kwl_list_replay`: libvulkan が各 vkCmd を手元の byte 列に encode するだけ、往復なし）
6. `vkEndCommandBuffer(command)`（往復。kernel の render worker が全部の命令を decode する）

さらに submit 1 回と present の 3 回で、**1 コマに 8 往復**。

### 1.3 命令の数と時間（draws で分けた p50）

| 1 コマの draws | n | cmds（draw・pipeline・set・push の和）p50 | draw→recorded p50 | draw→submit p50 |
| --- | --- | --- | --- | --- |
| 60 未満（800x480） | 178 | 104 | 10ms | 19ms |
| 60〜99 | 167 | 226 | 15ms | 25ms |
| 100〜299 | 91 | 337 | 27ms | 41ms |
| 300 以上（App Home） | 43 | 651 | 35ms | 57ms |
| 60 未満（1024x600） | 60 | 70 | 5ms | 14ms |
| 60〜99 | 90 | 242 | 20ms | 32ms |
| 100〜299 | 76 | 273 | 23ms | 35ms |
| 300 以上 | 2 | 643 | 35ms | 64ms |

- 直線で近似すると draw→recorded ≈ 1.3〜5ms ＋ **1 命令あたり 46〜52µs**。解像度に依らないのはこの命令の数に比例する部分。
- kernel の perf（`v3d: requests ... hold_us decode=`、2 秒の窓の最長）: 668〜819 命令の End が 19〜95ms、251〜394 命令が 24〜59ms。1 命令あたり約 28µs 以上。
- 1 つの命令（push constant・bind pipeline・draw）の最長が 13〜31ms の窓がある（`command=18000/op132` など）。push の decode は数 µs の仕事なので、decode の途中で render worker（kthread、優先度 8、CPU に固定）が別の thread に CPU を譲った（10ms の quantum）と見る。

### 1.4 kernel の decode が命令あたり重い理由（source を読んだ結果）

- `vulkan-object.c` の `bcm2711_vulkan_object_find` は、session の全 object の片方向 list を先頭から歩く（O(n)）。新しい object は先頭に入る。
- 各 vkCmd の decode は、最初に command buffer を identity で探す（`vulkan-record.c` の `bcm2711_vulkan_record_dispatch`）。command buffer は起動時に作った古い object なので、**毎命令 list を最後近くまで歩く**。さらに pipeline・pipeline layout・descriptor set・vertex buffer を探す（1 命令あたり 1〜4 回）。
- host（Xeon 6130）の計測（scratchpad の `p4-p010/walk.c`、malloc の node の list）: 1 node 2〜3ns。n=1000 で最古の object の探索 3.4µs、n=3000 で 9.9µs。A72 は数倍遅い。compositor の session は host image（1 枚 5 object）、client の絵の import（1 枚 3 object）、descriptor set、swapchain、WSI の slot などで数百〜数千の object を持つと見られる（正確な数は place25 の `objects=` で出す）。
- 命令ごとの decode の残り（record の 600 byte 程度の memset・memcpy、参照数の増加、検査）は 1µs 未満の仕事。

### 1.5 内訳のまとめ（800x480 の普通のコマ、226 命令、draw→submit 25ms）

| 部分 | 推定 | 根拠 |
| --- | --- | --- |
| walk | 0.3ms | KWL PERF walk |
| kernel の decode の object の探索 | 約 10ms（226 × 約 45µs） | 1.3 の傾き、1.4 |
| 往復 8 回（ioctl・worker の起床・返事の待ち、decode 以外の仕事） | 約 5〜8ms | 1.3 の切片と submit・present の p50 4〜5ms |
| decode の途中で CPU を譲る待ち | p50 には小さく、p90・最長に効く | 1.3 の 1 命令 13〜31ms |
| libvulkan の encode | 1ms 未満 | 手元の byte 列への書き込みだけ |

## 2. 設計

### 2.1 使い回し（secondary command buffer・記録済みの command buffer）を今は採らない理由

- bcm2711 の driver は secondary command buffer を持たない（`vkCmdExecuteCommands` の decode が無い）。足すのは driver の大きな仕事。
- 記録済みの primary を層ごとに分けて submit で並べると、層ごとに render pass が分かれ、V3D（tile の GPU）では pass ごとに tile の load・store が増える（GPU の時間が増える）。
- コマごとに damage の renderArea・scissor が変わるので、同じ命令の列になるコマは少ない。
- 使い回して省けるのは encode と decode だけで、decode の重さの大半は 1.4 の O(n) の探索。探索を O(1) にすれば、使い回しで省ける分は 1 命令 1〜2µs 程度になる。

### 2.2 採る直し（効く順）

1. **driver: object の探索を O(1) に**（`vulkan-object.c`・`vulkan-private.h`）。session に固定 1024 の hash の bucket を持たせ（session の構造体の中、確保を増やさない）、identity を Fibonacci hash で bucket に分ける。bucket の中は kind と identity で比べる。list（全 object の走査に使う既存の 4 か所）は残し、取り外しを O(1) にするため前の link を持つ。live の上限 4096 で bucket あたり平均 4 以下。
2. **compositor: 空の copy の command buffer を記録しない**（compose.c の `compose_replay_commands`）。host image の copy が 0 個のコマは copy_command の Begin・End（往復 2 回）を省き、submit も command 1 つにする（今も frame_copies が 0 なら command だけを送っている）。1 コマ 8 往復 → 6 往復。
3. **計測を足す**（place25 で直しの効果と残りを見る）:
   - kernel: decode の時間を generic timer の counter で測る（今は 1ms の tick）。窓の decode の合計 µs・命令の数・1 命令あたりの ns・最長の request の µs と命令の数・最長の 1 命令の µs、session の object の数の最大を `v3d: decode` の行に出す。
   - compositor: render thread の 1 コマの各段（copy の Begin・End、Begin、encode、End、Submit、Present）の時間を `KWL PERF rec` の行に出す（窓ごとの平均と最長）。

### 2.3 採らない・別にする物（提案）

- libvulkan の `vkBeginCommandBuffer` を End の stream に入れて往復を減らす: kernel が Begin を requested=0 で受けない（`vulkan-command-buffer.c` が EINVAL）。wire の約束の変更なので、place25 の往復の時間を見てから Q1 に提案する。
- WSI の present の copy の command buffer を slot ごとに使い回す（往復 2 回減る）: libvulkan の WSI（WS141 の範囲）の content の記録や cursor の lane との関係があり、往復の時間を見てから。
- render worker の kthread の優先度を user の thread より上げる（decode の途中で CPU を譲らない）: kernel の scheduling の方針の変更。place25 の 1 命令の最長が counter で数 ms 以上なら Q1 に提案する。

## 3. 実装（2026-10-12 P4、agent/p4）

- **f7873c64f（driver）**: `vulkan-private.h` の session に `buckets[1024]`（構造体の中、確保は増えない）、object に `link`（自分を指す pointer の場所）と `bucket_next`。`vulkan-object.c` の publish は list の先頭と bucket に入れ、find は identity の bucket だけを見る（Fibonacci hash の上位 10 bit、kind と identity で比べる）、remove は bucket から探して list からも O(1) で外す、close は bucket も空にする。list を歩く既存の 4 か所（`vulkan-device.c`・`vulkan-descriptor-pool.c`）は、次を取ってから外す今の形のまま正しい。
  - 計測: `render-runtime.c` が decode の前後で `kern_rtc_read_counter`（generic timer）を読み、`v3d-hardware.h` の perf に request の数・命令の数・counter の和・最長の request とその命令の数・session の object の数の最大を足す。`vulkan-native-job.c` が `v3d: decode requests= cmds= us= ns/cmd= max_us=/cmdsN objects=` を出す（v3d.perf=1 の時、2 秒の窓）。1 命令ごとの counter の計測は、`vulkan-stream.c` を link する WS141 の試験に counter の stub が無いので入れなかった（1 命令の最長は今の `v3d: requests` の `command=` の tick で見る）。
- **f3c506c85（WS141 の試験、Q1 の許可）**: `plan/ws141/tests/vulkan-device-host-test.sh` は main で既に link できなかった（`vulkan_content_note_image`）。今回 `kern_rtc_read_counter` も要るので、stub を `plan/ws141/tests/vulkan-device-link-stubs.c` に置き、script の cc の行に足した。
- **af2dd3887（compositor）**: `compose.c` の `compose_replay_commands` は、host image の copy が 0 個のコマで copy_command の Begin・End をしない（往復 2 回減る。`compose_op_run` は前から frame_copies が 0 なら command だけを送っている）。render thread の各段の時間を `kwl_render_outcome`（`render-thread.h`、5 つの値と copied）に入れ、main が `KWL PERF rec frames= copied= begin= encode= end= submit= present= | max end= submit= present=`（ms、5 秒の窓）を出す（`kwl.h`・`main.c`）。
  - あわせて `plan/ws211/tests/anim-host-test.c` の link の壊れ（ws213-p004 の `kwl_compose_want_layer` の stub が無い、main で既に FAIL）を直した。
- **c8fbd678a（ws213 の試験）**: P3 の p009 で `kwl_scene_copy` に `restore` が増えたので、`plan/ws213/tests/list-replay-host-test.c` の 2 か所を直した（golden は同じ）。

## 4. 確かめ（2026-10-12 P4、host）

- `sh plan/ws211/tests/object-index-host-test.sh`（新規）→ PASS。本物の `vulkan-object.c` で、kind の違う同じ identity、重複の EEXIST、上限 4096 の ENOSPC、list の先頭・末尾・中ほどの取り外し、list を歩きながらの取り外し（descriptor pool の形）、参照が残る取り外し、取り外した identity の再公開、close を見る。各段で list の link と index が同じ集合であることを確かめる。続けて WS141 の `vulkan-device-host-test.sh` を流す（全部 PASS）。
  - 最古の object（compositor の command buffer の位置）の探索（Xeon 6130）: n=300 で index 7ns・list 1.1µs、n=1000 で 3.8ns・3.8µs、n=4000 で 5ns・15µs。
- WS141 の `v3d-hardware`・`vulkan-object`・`vulkan-stream` の host 試験 → PASS。
- ws213 の `cursor-due`・`input-thread`・`list-replay`（799 行）・`render-thread`、ws211 の `anim`・`batch`・`bind`・`damage` → PASS（main f1be68774 の merge の後に再度）。
- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete`（rc 0、arm64 vmunix check PASS）、同じく `build/arm64/bin/wayland`、amd64 の `build/amd64/bin/wayland`、`make KEILAND_ASK=n KEILAND_LINUX_BUILD=build/keiland-linux keiland-linux`（rc 0）。FreeBSD の build は未実施。`git diff --check` 空。`plan/tools/style-check.py` で新しい file の指摘 0、変えた file は base より増えていない。
- 未実施: Pi（place25）、T1（i915・Venus、compositor の変更は全 backend で動く。driver の変更は bcm2711 だけ）。

## 5. Pi で見ること（place25、scratchpad/place25）

- 中身: `p25-vmunix`（cksum 1437434276、`/boot/boot0/vmunix` へ、元は `.bak`）、`p25-wayland`（cksum 1594188947、`/bin/wayland` へ）、`place25.sh`。agent/p4 f1be68774（main ＋ p010、P3 の p009 の compositor 側を含み driver 側は含まない）。解像度に依らない。
- `v3d: decode ... ns/cmd= objects=`: 1 命令あたりの decode が数 µs 以下になること、objects の数（前の 46〜52µs/命令の原因の大きさ）。
- `KWL PERF rec`: copied が 0 のコマが多いこと、end（decode を含む End の往復）・begin・submit・present の平均と最長。往復 1 回の時間は begin（中身の無い往復）で見る。
- KWL LAT: draw→recorded・draw→submit の p50 と p90 を place23 の表（1.1、1.3）と比べる。目安は draw→submit の p50 が 24〜30ms から 10ms 前後へ。
- `v3d: requests` の `command=`（1 命令の最長の tick）が 10ms を超え続けるなら、render worker の優先度の提案（2.3）へ。
