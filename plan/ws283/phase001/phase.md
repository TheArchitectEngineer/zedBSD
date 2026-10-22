<!-- awesome-plan project=zedbsd record=ws283-p001 -->

# ws283-p001 bcm2711 の Vulkan の compute の調べと設計

- 親: [ws283](../ws.md)。担当: P2（2026-10-23、Q1 の依頼。第 1 版は worktree `agent/p2-compute`、第 2 版は `agent/p2-compute2`）。Queue: Q1 が記録する。
- Status: in-progress（設計の第 2 版、Q1 の判定と Q-a〜Q-c 待ち）
- 範囲: 調べと設計だけ。実装・build・実機の試験はしない。

## 目的と受け入れの条件

1. compiler（SPIR-V → V3D 4.2・7.1 の QPU）が compute に欠く物を、今の code と Mesa v3dv の compute の扱いから洗い出す（workgroup・invocation の ID、共有 memory、barrier、storage buffer の読み書き、vector の load・store、atomic、push constant、specialization constant、loop）。
2. CSD の dispatch の設定（register の queue、workgroup の大きさの限界、共有 memory の大きさ、supergroup）を 4.2 と 7.1 で、Mesa の出典付きでまとめる。
3. 実行器の部品（compute の queue family、pipeline、storage の descriptor、`vkCmdDispatch` → CSD の job、render の job との同期、indirect）を決める。
4. 試験の計画（host と Pi 5 の実機）と、YOLOX-Nano 416×416 の性能の見込み。
5. Phase の分け方と LW の見込み、ユーザーの判断が要る点。

## 結果

### 第 1 版（2026-10-23 P2、`3d515b76e`）

- design.md の第 1 版。p002〜p010 の案で 51〜77 LW。D1〜D4 をユーザーに、D5 を Q1 に上げた。
- design-reviewer の review: [review-20261023.md](review-20261023.md)（High 6・Medium 10・Low 7）。

### 第 2 版（2026-10-23 P2）

- [design.md](../design.md) の第 2 版。review の全部の指摘、ユーザーの D1〜D4 の回答（「decisionは推しでいいです」、Q1 経由: 推しの通り）、Q1 の判断（D5 lane の model を `plan/tools/` へ、H1 WS284 は spec constant を使わない、M5 COMPUTE の flag は compute が十分になってから）を入れた。
- 要点: 7.1 を先に作り 4.2 は p013 の port。直線の compute（p004）＋実行器（p005）の直後に最初の実機の確かめ（p006）。block を持つ一般の scheduler を独立の Phase（p008）に。全ての shader の address を `base + umin(offset, limit)` で clamp（隔離の不変条件）。COMPUTE の flag は `CONFIG_DRIVER_BCM2711_COMPUTE`（既定 n）で gate し、p012 で既定を y。
- 見込み: **p002〜p014 で 77〜121 LW**（実時間 19〜50 時間）。YOLOX-Nano 416 fp32 は Pi 5 で p011 の後 40〜120 ms（8〜25 fps）、最初に動く形は 100〜250 ms（4〜10 fps）、source 順なら 2〜4 倍遅い（推定、clock と TMU の速さが未知）。
- WS284 の Pi 5 の Phase は WS283 の p007（分岐と loop）の後に始められる（第 1 版の番号の p004 は第 2 版の p007）。
- 調べに使った物（第 2 版で足した物）: Z `compiler/shader.c` 555〜580 行・`shader-private.h` 20〜50 行・`shader-schedule.c` 1〜60 行と関数の一覧、`compiler/qpu.h`、`compiler/ir.h` 505〜560・660〜690 行、`compiler/spirv.c` 1790〜1830 行、`render-worker.c` 1280〜1352・1890〜1915 行、`v3d-job.c` 415〜560 行、`v3d-job.h` 36〜50 行、`v3d71-hardware.c` 430〜505 行、`vk/vulkan-query.c` 100〜130・268〜287 行、`include/uapi/gpu-op.h` 950〜960 行、top の `Makefile` 337〜340・810 行、`userland/desktop/libglesv2/gles.c` 2425〜2472 行、`build/NoctLang/src/accel/accel_vulkan.c` 1120〜1145 行、WS284 design §3.6・§3.7・§5・§6。M `compiler/nir_to_vir.c` 40〜56・250〜300・457〜492・590〜720・3093〜3215・3405〜3428・3695〜3715・4225〜4245・4555〜4575 行、`compiler/qpu_schedule.c` 462〜502・1416〜1510・2370〜2410 行、`compiler/qpu_validate.c` の失敗の一覧、`common/v3d_util.c` 30〜100 行、`vulkan/v3dv_pipeline.c` 214 行、`vulkan/v3dv_device.c` 896〜979 行、`vulkan/v3dv_limits.h`、`compiler/v3d_compiler.h` 62・69 行。
- 実行したコマンド: 読むだけ（`grep`・`sed -n`・`ls`・`wc`）。build・試験は無し。

## review の指摘の扱い（第 2 版）

| # | 指摘 | 扱い | design |
| --- | --- | --- | --- |
| H1 | spec constant が前段に無い | Q1 の判断: WS284 は build の時の variant と push constant。WS283 は今は足さず、後の項目（Future Work の候補）に。§0 と §1.13 の「前段は変えない／前段が処理する」を直した | §0・§1.13・§5 |
| H2 | scheduler は fragment 専用 | block を持つ一般の scheduler（block の約束、TMU・rtop・unifa・SYNCB の資源、compute の終わり、block ごとの verify と fallback）を独立の Phase p008（8〜12 LW）に。効率の見込みを schedule の有無で分けた（2〜2.5 倍、source 順は 2〜4 倍遅い） | §1.7・§3.3・§4 |
| H3 | supergroup の試験が無い | wg 24・40（barrier あり・なし）、1 workgroup の 24（部分 batch）、wg 8 × 3（余り）、256（CFG3 の 0）を HT3・HW2・HW6 に。canary で範囲外の書き込みを見る | §2.4 の例の表・§3 |
| H4 | loop の branch に msfign=P が無い | loop・SKIP・store の branch は全部 msfign=P。dispatch されていない lane の store は hardware が落とす推測を HW2 で確かめ、駄目なら active を MSF から作る fallback。lane の model に dispatch されていない lane を入れる | §1.6 の 1・§3.1 |
| H5 | address の clamp は隔離の要 | 不変条件に: 全ての shader の計算した address（storage・shared・ldunifa・vector）を `base + umin(offset, limit)` で clamp、省く最適化はしない。空の binding は session ごとの dummy の page。lane の model と rule checker で試す | §1.3・§1.9・§1.10 |
| H6 | ldunifa の制約が無い | NonWritable の binding だけ（前段に印を 1 つ足す、p011、Q1 の了承）、汚れの表で GPU の書いた data を ldunifa で読む前に waited clean、一様な address だけ（WorkgroupId は wg_size が 16 の倍数の時だけ一様）、窓 K の clamp。L2T の clean の省略とは汚れの表で両立させる | §1.10・§2.5 |
| M1 | register の数・MOVE の合併 | 24／56 に直した。loop の変数の MOVE の合併を p007 に。WS284 の conv1x1（P = 4）の予算 38〜42 で 2 thread、p007 の完了の条件に入れた | §1.1・§1.6 の 8・§1.11 |
| M2 | TMU の規則 | 入力の FIFO（16/threads、vec4 の store は vec2 × 2 に分ける）、出力の FIFO、lookup 8 個、TMUAU＋config、sequence の不可分、block の終わりに持ち越さない、TMUWT を最後に置かない | §1.4 |
| M3 | hazard の規則と検出 | 規則 14 個を列挙。M の validate を写さず一覧から書いた独立の rule checker（`plan/tools/v3d-qpu/qpu-rules.c`）を p003 で作り、全 binary と graphics の shader に流す | §1.8・§3.1 HT-R |
| M4 | dispatch の分割 | M と同じく group の base を uniform で渡し WG_OFFSET は 0。arena・CFG4 の 32 bit・batch の目安で分割。2 秒の上限を明記。arena の大きさは HW8 まで M の数 | §1.2・§2.3 |
| M5 | COMPUTE の flag | Q1 の判断に沿い `CONFIG_DRIVER_BCM2711_COMPUTE`（既定 n、試験の image は y）で gate、p012 で既定を y。libglesv2 は `vertexPipelineStoresAndAtomics` が要るので bcm2711 では 3.1 に上がらない事を確かめた（features は全部 0）。noct の accel は p012 で確かめる。Vulkan 1.0 の必須の atomic・image が無い制限を明記 | §2.6・§4 p012 |
| M6 | dispatch の費用・WS284 の問い | `sched_yield()` は他の session が待たない時に compute の dispatch を同じ step で続けて減らす。code は execution ごとに 1 回 upload（第 1 版の「作る時」は誤りと直した）。WS284 への答え: GPU → GPU は write combiner の flush と slice の無効化で足り、L2T の waited clean は CPU の読み・UBO の写し・ldunifa の前だけ（省ける見込み、HW9 で確かめて p011）。HW7 で往復を先に測る | §2.1・§2.2・§2.3 の 6・§2.5 |
| M7 | 整数の除算 | mul-high が無いので magic number は不可。2 の冪は shift、それ以外は float の 2 段＋整数の補正の一般の形（約 20〜25 命令）。端の値と乱数の host の試験、HW1 | §1.5 |
| M8 | 全 lane が無効の TMU | load は clamp 済みの無条件の lookup、store は ALLNA の branch で囲む。HW5 で囲まない版を確かめ、安全なら p011 で省く選択肢 | §1.4 |
| M9 | Phase の分け方 | 7.1 を先に作り 4.2 は p013（8〜14 LW）。4.2 の `run_compute` は一度も走っていないと明記。lane の model と rule checker を p003 に計上、試験の program を各 Phase に、最初の実機を p006（直線＋実行器の直後）、binary の interface を p004 で先に決める。WS284 の開始は p007 の後。合計 77〜121 LW | §1.12・§4 |
| M10 | barrier を含む loop の deadlock | barrier を含む loop は wgs_per_sg を 1 に固定し、loop の条件が workgroup の中で一様と証明できる時だけ通す（できなければ compile で断る）。lane の model は deadlock を失敗にする | §1.9 |
| L1 | 性能の見込みの不整合 | WS284 §5 の前提（800 MHz、0.523 G MAC、130 MB、122 dispatch）に揃え、TMU の速さ・入力の読み直し・2 thread の FIFO を未知の項として明記。HW10 に TMU の速さと counter と clock | §3.3 |
| L2 | limit・barrier の欠け | minStorageBufferOffsetAlignment 32、storage buffer の数 8・24、maxComputeWorkGroupSize を前段と同じ値、CFG3 の 256 → 0、UNIFA（waddr 9）、HOST・INDIRECT の stage と access、indirect の 0 と 65535 超、indirect の時の arena と uniform | §1.1・§2.2・§2.4 |
| L3 | ID の衝突 | 試験は HT・HW、risk は RK に改名 | §3・§6 |
| L4 | 試験の欠け | ID の全部（4×2×3、(3, 5, 2)）、一様でない loop、2 thread の強制、範囲外の canary、全 lane が偽の述語、大きい dispatch の分割、barrier を含む loop、vkreset・vkcoherency を HT2・HW に | §3.1・§3.2 |
| L5 | payload の移し・hang の上限 | payload は移さずに読む。CFG4 の窓と 2 秒の上限を明記 | §1.2・§2.3 の 2・7 |
| L6 | UAPI の注記 | p005 で `gpu-op.h` の「bcm2711 has no route」（66・110・111）を直す（注記だけ） | §2.2・§4 p005 |
| L7 | §0 の記述 | 前段の欠け（spec constant・NonWritable）を §0 に書き、phase.md の「確かめていない事」から spec constant を外した | §0 |

## 判断が要る点

- ユーザー: 無し（D1〜D4 は回答済み）。§2.6 の制限（atomic・storage image が無いまま COMPUTE の flag を出す、D4 の結果）はユーザーに知らせる。
- Q1: Q-a 7.1 を先に作る順（WS279 のユーザーの好みと向きが逆）、Q-b p011 での共有の前段の NonWritable の印、Q-c top の `Makefile` の `CONFIG_DRIVER_BCM2711_COMPUTE`。D5 の file の移動と Master の Tools の登録、後の項目の Future Work への登録、WS284 への連絡（design §5）。

## 未実施・残り

- 第 2 版の design-reviewer の review（Q1 が起動するなら）。
- 確かめていない事（design に「推測」と書いた物、各々の確かめは §3 の HW）: 7.1 の slice をまたぐ共有 memory の可視性（HW6）、dispatch されていない lane の store（HW2）、全 lane が偽の条件付きの TMU（HW5）、2 thread の CFG5（HW3）、wg_in_mem の巡回と arena（HW8）、L2T の clean の省略（HW9）、SFU の RECIP の精度（HW1）、TMU の実効の速さと clock（HW10）、dispatch の 1 回の費用（HW7）。

## 再開の条件

Q1 の第 2 版の判定と Q-a〜Q-c の回答、D5 の file の移動。その後 p002（encoder）から。
