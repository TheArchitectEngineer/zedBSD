<!-- awesome-plan project=zedbsd record=ws306 -->

# WS306 GPU の driver の枠組みの「ゲーム最適化」の UAPI と driver の識別

- Status: planning（2026-10-27 新設）
- Master: [plan/master.md](../master.md)。Primary Milestone: MG006。関係: [WS291](../ws291/ws.md)（RDNA の NGG の culling を、この flag の時だけ入れる）、[WS226](../ws226/ws.md)（Vulkan 1.3 相当と拡張の capability。VK_EXT_mesh_shader の task・mesh の feature はそちら）。

## 目標

`/dev/gpu`（GPU の driver の枠組み）に、session（process）ごとの「ゲーム最適化」の UAPI を足す。ON/OFF に加えて、細かい option の ON/OFF を明示する文字列も渡せる。GPU の名前と driver の版をアプリが取れるようにし、ゲームが driver を見て option を選び、driver の不具合を避けながら互換性をゲームの側で制御できるようにする。全 GPU の driver（amdrdna・NVIDIA・PowerVR など）が同じ UAPI で自分の最適化を切り替える。

## ユーザーの決定（原文）

- 2026-10-27 ユーザー「GPUドライバフレームワークに、ゲーム最適化、というUAPIを追加して、このゲーム最適化がかかっているときだけ、NGGをmesh shader以外でも有効にするのが、私のセンスでは正しいような気がします。」
- 2026-10-27 18:25 ユーザー「では、ゲーム最適化はON/OFFの他に、細かいオプション群のON/OFFを明示指定もできる文字列も渡せるようにしましょう。あと、GPUの名前やドライバのバージョンも取得できるようにしましょう。これにより、ゲームがドライバを見て、細かいオプションを制御できるようになります。ドライバのバグを回避しつつゲームの互換性をゲーム側が制御できるという点では、Windowsより優れたものにできるかもしれません。」
- 同じ日の task・mesh shader: VK_EXT_mesh_shader（task と mesh は同じ拡張の 2 つの feature）で出す。ゲーム最適化とは関係なく、使える GPU では常に出す（Q1 の説明にユーザーが「はい」の流れ。詳細は WS226 で）。

## Q1 の解釈（未確定、設計の Phase で決める）

- RDNA: ゲーム最適化が ON の session の pipeline だけを culling 入りの NGG で compile する（三角形の少ない描画では culling を飛ばす判断は残す）。OFF の時は passthrough の NGG か従来の経路（desktop の frame-ops を測って決める）。
- 名前と版は Vulkan の `VkPhysicalDeviceProperties`（deviceName・driverVersion）と `VkPhysicalDeviceDriverProperties`（driverName・driverInfo）にも写す。
- 有効にする方法の推し（ユーザーの判断待ち）: アプリか launcher が Vulkan の instance を作る時に指定し、libvulkan が session の flag と option の文字列を kernel に渡す。
- option の文字列は kernel で長さと書式を検査し、知らない option は無視か拒否（設計で決める）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計: UAPI（session の flag・option の文字列・名前と版の取得）、libvulkan の指定の口、各 driver の option の表（amdrdna の NGG culling から）、design-reviewer | — | planning |

## Resume point

- 2026-10-27: 新設。使用量が戻ったら p001 の設計から。
