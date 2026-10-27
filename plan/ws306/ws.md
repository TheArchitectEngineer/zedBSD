<!-- awesome-plan project=zedbsd record=ws306 -->

# WS306 GPU の driver の枠組みの「ゲーム最適化」の UAPI と driver の識別

- Status: planning（2026-10-27 新設。WS291 の RDNA の最適化の完了の後に始める）
- Master: [plan/master.md](../master.md)。Primary Milestone: MG006。関係: [WS291](../ws291/ws.md)（RDNA の NGG の culling を、この flag の時だけ入れる）、[WS226](../ws226/ws.md)（Vulkan 1.3 相当と拡張の capability。VK_EXT_mesh_shader の task・mesh の feature はそちら）。

## 目標

`/dev/gpu`（GPU の driver の枠組み）に、session（process）ごとの「ゲーム最適化」の UAPI を足す。ON/OFF に加えて、細かい option の ON/OFF を明示する文字列も渡せる。GPU の名前と driver の版をアプリが取れるようにし、ゲームが driver を見て option を選び、driver の不具合を避けながら互換性をゲームの側で制御できるようにする。全 GPU の driver（amdrdna・NVIDIA・PowerVR など）が同じ UAPI で自分の最適化を切り替える。

## ユーザーの決定（原文）

- 2026-10-27 ユーザー「GPUドライバフレームワークに、ゲーム最適化、というUAPIを追加して、このゲーム最適化がかかっているときだけ、NGGをmesh shader以外でも有効にするのが、私のセンスでは正しいような気がします。」
- 2026-10-27 18:25 ユーザー「では、ゲーム最適化はON/OFFの他に、細かいオプション群のON/OFFを明示指定もできる文字列も渡せるようにしましょう。あと、GPUの名前やドライバのバージョンも取得できるようにしましょう。これにより、ゲームがドライバを見て、細かいオプションを制御できるようになります。ドライバのバグを回避しつつゲームの互換性をゲーム側が制御できるという点では、Windowsより優れたものにできるかもしれません。」
- 2026-10-27 ユーザー「ゲーム最適化の細かいオプションの一覧もUAPIで取得できるようにしましょう。」→ option の一覧（名前・説明・既定値など）を UAPI で取れるようにする。Vulkan の core にはこの仕組みが無い（Q1 の説明: 近い物は VK_EXT_layer_settings（layer 向けの設定の受け渡し）、VkApplicationInfo の app・engine の名前による driver 側の profile、VK_KHR_driver_properties、VK_AMD_rasterization_order のような個別の拡張）ので、Vulkan への見せ方は zedBSD の独自の拡張を設計で考える。
- 2026-10-27 ユーザー「それでは、VulkanのAPIではなくて、環境変数をlibvulkanが読んで、UAPIを叩く形にしましょう。これにより、ゲームがゲーム最適化のON/OFFと個別ゲーム最適化オプションのON/OFFを制御できます。また、vulkanの情報を出力する標準のコマンドってありますか？そこにゲーム最適化オプションの一覧を出せばいいと思います。」→ 指定は環境変数（libvulkan が読んで UAPI を叩く。Vulkan の独自の拡張は作らない）。一覧の表示は Khronos の標準のコマンド `vulkaninfo`（Vulkan-Tools）に出す。2026-10-27 の時点で zedBSD の tree に vulkaninfo は無い（`grep -rli vulkaninfo userland` が 0 件）ので、userland/packages に移植する（tarball と patch、ライセンスは Apache-2.0 の見込みで監査が要る）。
- WS291 との順（2026-10-27 ユーザー「WS306ですが、AMD RDNAドライバの最適化をWS291でやりきったあとに、要素の最適化をオプションに移して実装し直す作業という認識なので、両方のWSに記録してください。」）→ WS306 は [WS291](../ws291/ws.md) で AMD RDNA の最適化をやりきった後に始める。WS291 で入れた最適化の要素（NGG の culling など）を option に移して実装し直す作業。
- 同じ日の task・mesh shader: VK_EXT_mesh_shader（task と mesh は同じ拡張の 2 つの feature）で出す。ゲーム最適化とは関係なく、使える GPU では常に出す（Q1 の説明にユーザーが「はい」の流れ。詳細は WS226 で）。

## Q1 の解釈（未確定、設計の Phase で決める）

- RDNA: ゲーム最適化が ON の session の pipeline だけを culling 入りの NGG で compile する（三角形の少ない描画では culling を飛ばす判断は残す）。OFF の時は passthrough の NGG か従来の経路（desktop の frame-ops を測って決める）。
- 名前と版は Vulkan の `VkPhysicalDeviceProperties`（deviceName・driverVersion）と `VkPhysicalDeviceDriverProperties`（driverName・driverInfo）にも写す。
- 有効にする方法（決定済み、上の 2026-10-27 の原文）: 環境変数を libvulkan が読み、session の flag と option の文字列を UAPI で kernel に渡す。環境変数の名前と option の文字列の書式は p001 で決める。
- option の文字列は kernel で長さと書式を検査し、知らない option は無視か拒否（設計で決める）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計: UAPI（session の flag・option の文字列・option の一覧・名前と版の取得）、libvulkan が読む環境変数の名前と書式、知らない option の扱い、各 driver の option の表（amdrdna の NGG culling から）、vulkaninfo への一覧の出し方、design-reviewer | — | planning |
| p002〜 | 実装: UAPI と gpu の枠組み、libvulkan、amdrdna の option（NGG culling）、vulkaninfo の移植（userland/packages、ライセンスの監査）。p001 で分ける | p001 | planning |

## Resume point

- 2026-10-27: 新設。使用量が戻ったら p001 の設計から。
