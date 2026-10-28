<!-- awesome-plan project=zedbsd record=ws269 -->

# WS269 VisionFive 2 の GPU（Imagination PowerVR BXE-4-32）の driver

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: VF2 の板で Keiland の desktop が PowerVR で動く（beta2.md U36、2026-10-21: desktop・3 window・lockup 無し・vkmatrix 36/36、gpu-selftest 13/13）。2026-10-21 ユーザー「表示内容自体は完璧です」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG008
Related Milestones: MG006
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない。速さは [WS291](../ws291/ws.md)）
<!-- awesome-plan-current:end -->

## 目標

JH7110 の PowerVR BXE-4-32 を kernel の driver が認識し、電源・clock・reset を入れ、firmware を載せて GPU を起こし、Keiland が使う Vulkan の範囲（[WS270](../ws270/ws.md) の desktop に要る物）で描画できるようにして、VisionFive 2 の実機で確かめる。設計は [design.md](design.md)（改訂 3.1）と graphics の追補 [design-p010.md](design-p010.md)（版 2.2）。

## ユーザーの決定

- 2026-10-19（起点）「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 2026-10-19「~/にvisionfive-linuxとしてクローンしてくれるとうれしいです。」（`~/visionfive-linux`、StarFive の vendor kernel、branch `JH7110_VisionFive2_6.12.y_devel` 4cecf169f38e）
- 2026-10-19 ユーザーの許可「ISC の source の無い VF2 の部品は GPL の手順を参照してよい」（参照の範囲は Guardrail の出典の規則: 値は `Value(s) referenced:`、code・構造・名前は写さない）。
- 2026-10-19 22:04（クリック）: D1 (a)、D2 (a)、D3 (a)、D5 (a)、D6 A、D7 (a)（D4 は Guardrail で決まっている）。原文は当時の Master の decisions-log（2026-10-27 に削除、git の履歴）。
- 2026-10-20 07:24「PowerVRのコードは完了を目指して作業をお願いします。あとでUATにおいて、どこまで実行できたかわかるログが出れば、デバッグしやすいと思います。」（不在の間の指示。design-p010.md §9 の D-P1〜D-P7 は推奨で進めた: D-P1 free list は global 固定 16 MiB、D-P2 gpu-selftest は VF2 の試験の image だけ、D-P3 (b) compiler が pack と blend、D-P4 submit は ioctl の中で待つ（後に p017 で非同期へ）、D-P5 段の行は英語の短い語、D-P6 read-only の sysctl `hw.gpu.powervr.stages` を試験の段の報告の leaf として残す案、D-P7 p010 の受け入れを vkdemo と gpu-selftest に。ユーザーの確かめは記録に無い（未確認））
- 2026-10-21「表示内容自体は完璧です。カーソル移動や画面遷移は、全画面書き換えは2fpsも出なかったり、カーソルも3fpsくらいだったりしてます。これは最適化できそうですか？」、同日「非同期に変える」（D-P4 の変更、p017）。

## 結果

- kernel の driver（`src/drivers/gpu/powervr/`、`src/drivers/platform/visionfive2/vf2-gpu.c` ほか）: PMU（PD_GPUA）・CRG・riscv64 の uncached の別名、firmware の parser と MIPS の page table と起動、GPU の MMU・CCB・fence・recovery・render node、display の node と scanout の import、render の FW の object（free list・HWRT・render context）、TA・3D・transfer・compute の kick、段の行（`powervr: <段>: ok|fail(<理由>)`、sysctl `hw.gpu.powervr.stages`）、非同期の submit の段階 A（p017）。
- userland: Venus の decode と executor、SPIR-V から USC への compiler（compute・vertex・fragment、pack と blend）、image・format・sampler・descriptor・pipeline・draw・depth・render pass・blit、WSI の readback と shared の道。試験の program（gpu-selftest・vkmatrix・vkcoherency・vkcompute・vkstage・vkdemo・vkreset）。
- firmware の package（p003）: imagination の linux-firmware の fork、branch `powervr` の 8a58f81883f7 の `rogue_36.50.54.182_v1.fw`（1.1.OS@6976702、sha256 `59127d3a02e48876310a520b7dc355f7430d3748d2940caaa33f39237d517abb`）。資料の版は mainline v7.3・Mesa 26.2.5（p002・p010b）。
- 板（VF2 の実機、2026-10-21）: kernel の自己試験（compute check、render check の terminate-state・三角形・transfer・job check）が全部 ok、gpu-selftest 13/13、vkcoherency PASS、vkmatrix 15/15（板 96）、context reset 0、HDMI に vkdemo の cube。安定性（p015）: 1 回の起動で連続 10 回と電源の入れ直し 3 回が全部 13/13・15/15・PASS・reset 0、vkreset の lockup → context reset → 次の process が通る。beta2.md U36（2026-10-21）: desktop・3 window・lockup 無し・vkmatrix 36/36。
- 文書: `docs/reference/visionfive2-gpu.md`（p016）。
- hardware と firmware の事実（PMU の event mask、TA の stream の終わりの terminate の PPP state、Mesa の workgroup の PDS、USC の制約、memory と cache、除外した仮説ほか）は [hardware-notes.md](hardware-notes.md)。

## 制限・移管

- 描画の速さは [WS270](../ws270/ws.md) p007 で desktop が 60 fps 前後に、重い 3D の model 50 fps は [WS291](../ws291/ws.md)（PowerVR、P3）。
- 制限: 1 queue（graphics・compute・transfer の family 1 つ）、API 1.0、linear の image を SAMPLED にしない、1 sample、render target は 1 layer。
- [BUG-342](../bugs/BUG-342.md)（session の GPU の VA に mainline の static data area が無い）は scheduled のまま。
- 長時間の安定性はユーザーの確認待ちのまま（p015）。非同期の submit の段階 B・C と板の確認（p017）は未実施。
- 規約の全文の見直し（p012）は 2 回（N4）まで済み、vkmatrix・vkcoherency の段落の粒度は残件。残りは WS の外の一括の見直しへ。
- 各 Phase の実機の行の確かめは、上の板の通し（gpu-selftest・vkmatrix・U36）で代え、Phase ごとには判定していない（2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）。
- [proposal-next-phases.md](proposal-next-phases.md)（N2〜N6）は Phase にしないまま閉じた。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws269-p001 | 設計（driver の構成・firmware・userland の Vulkan の道・Phase の分割） | cleared（2026-10-19、改訂 3.1） |
| ws269-p002 | 資料の固定（FW の版と sha256、mainline の tag、Mesa の版、BRN、vendor の値） | cleared（2026-10-19、q1326） |
| ws269-p003 | firmware の package | cleared（2026-10-19、q1328） |
| ws269-p004 | PMU・CRG の GPU の行・組み込みの記述・riscv64 の uncached の別名・attach の早い段 | in-progress のまま WS を閉じた（実装は main、板の通しで動作） |
| ws269-p005 | 遅い段: FW の parser・MIPS の page table・FW の起動・health・IRQ | in-progress のまま WS を閉じた（同上） |
| ws269-p006 | coherency・GPU の MMU・resource・CCB・fence・recovery・render の node・share | in-progress のまま WS を閉じた（同上） |
| ws269-p007 | display の node（claim・present）と scanout の import | in-progress のまま WS を閉じた（同上、display は WS277 の DC8200 へ） |
| ws269-p008 | USC・PDS の encoder、kernel の中の試験の道 | in-progress のまま WS を閉じた（同上） |
| ws269-p009 | 最小の Venus の decode（compute） | in-progress のまま WS を閉じた（同上、vkcompute PASS） |
| ws269-p010 | graphics の設計の追補（design-p010.md） | in-progress のまま WS を閉じた（版 2.2） |
| ws269-p010a | UAT の段の行と gpu-selftest の骨 | in-progress のまま WS を閉じた（gpu-selftest 13/13） |
| ws269-p010b | 資料の固定（Mesa・mainline の path と SHA、BXE の feature の分岐） | cleared（2026-10-20） |
| ws269-p010c | image・format・view・sampler | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010d | render の FW の object（前半）: heap・free list・HWRT・GROW・CLEANUP | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010n | render の FW の object（後半）: render context・SR・PT・kick | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010e | PBE の store・EOT・固定の program・render check の三角形 | in-progress のまま WS を閉じた（板で render check ok） |
| ws269-p010f | transfer（copy・clear、IPF・FAST_2D） | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010o | transfer の blit（FAST_SCALE） | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010g | render pass・framebuffer・load/store・clear・job の列 | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010-compiler | SPIR-V から USC への compiler（最初の試行の記録） | in-progress のまま WS を閉じた（p013・p014 に続く） |
| ws269-p010h | descriptor と compiler の境、PDS の data の program | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010i | graphics の pipeline と draw | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010j | depth と vkdemo の offscreen | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010k1 | WSI の readback の道 | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010k2 | readback の道で vkdemo が画面に | planning のまま WS を閉じた（板で vkdemo の cube は HDMI に出た） |
| ws269-p010l | WSI の shared の道と p007 の import、dc8200 の zero copy | in-progress のまま WS を閉じた（zero copy は WS277） |
| ws269-p010m | 仕上げ、T1 の回帰、p010 の受け入れ | planning のまま WS を閉じた（受け入れは板の gpu-selftest・vkmatrix で代えた） |
| ws269-p010p | format の広げ（F16・raw 64/128 bit の copy） | in-progress のまま WS を閉じた（実装は main） |
| ws269-p010q | 実機の前の点検（cache・MMU・SLC・順・時間切れの後） | cleared（2026-10-21、板 96） |
| ws269-p011 | Keiland が使う Vulkan の機能の不足を driver 側で埋める | in-progress のまま WS を閉じた（driver 側の未対応は無くなった、q1518） |
| ws269-p012 | 規約の全文の見直し | in-progress のまま WS を閉じた（2 回まで済み、残りは一括の見直しへ） |
| ws269-p013 | USC の encoder の広げ | in-progress のまま WS を閉じた（実装は main） |
| ws269-p014 | SPIR-V から USC の compiler（compute の一般、vertex・fragment、pack と blend） | in-progress のまま WS を閉じた（実装は main） |
| ws269-p015 | 安定性（連続実行・電源の入れ直し・reset からの回復・長時間） | in-progress のまま WS を閉じた（連続 10 回・入れ直し 3 回は PASS、長時間は未確認） |
| ws269-p016 | VF2 の GPU の文書 | cleared（2026-10-21） |
| ws269-p017 | 非同期の submit | in-progress のまま WS を閉じた（段階 A は main、板は未確認） |

## 記録の所在

Phase の計画・結果（`plan/ws269/phase001`〜`phase017`）は削除する。試験（`plan/ws269/tests/`）のうち回帰に使う物は plan/tools へ移す（移した先は [plan/tools.md](../tools.md)）。git の commit `fb1f6afee` の `plan/ws269/` にある。設計とレビューは [design.md](design.md)・[design-p010.md](design-p010.md)・[design-review-1.md](design-review-1.md)・[design-review-2.md](design-review-2.md)・[design-review-3.md](design-review-3.md)・[design-p010-review-1.md](design-p010-review-1.md)・[design-p010-review-2.md](design-p010-review-2.md)。Queue の履歴は [plan/history](../history/index.md)。
