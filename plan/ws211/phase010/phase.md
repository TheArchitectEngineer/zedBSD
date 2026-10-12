<!-- awesome-plan project=zedbsd record=ws211-p010 -->

# ws211-p010: compositor の 1 コマの CPU の仕事（replay・libvulkan・kernel の decode）を縮める

Parent: [WS211](../ws.md)
Status: in-progress（2026-10-12 P4、内訳を測り、設計と実装中）
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
