<!-- awesome-plan project=zedbsd record=ws101 -->

# WS101: GPU の compute（i915 の Vulkan の compute、GLES 3.1 の compute、Noct の自動並列化が 5330 の GPU で動く）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p002（compiler の核）cleared（2026-09-30）。次は p003（実行器の object と記録）と p008（GLSL ES 3.10 の compute）。判断 D1〜D5 は design.md §7
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

「デモのときに来客から、Compute Shaderが使えるのか聞かれて、もし使えないと答えたらがっかりさせてしまいますね。これはOSCデモ（10月17日）には
使えるようにしたいです。ただし、優先事項の中では最優先ではない、くらいです。NoctにはOpenGL ESを使ってCompute Shaderを生成してGPU
アクセラレーションを行う自動並列化機能が入っており、これが動くようにするのをゴールにしたらどうでしょう。Compute Shaderに対応しておけば、
ローカルLLMも動かせるようになって、夢が広がります。」

期限は 2026-10-17 の OSC のデモ（新規実装の期間は 10/10 ごろまで。master の判断）。優先は fg010 の中で最優先ではない。

## 達成基準（案、2026-09-30 main）

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| G1 | i915 のネイティブの実行器で Vulkan の compute が動く: `vkCreateComputePipelines`、`vkCmdDispatch`・`vkCmdDispatchIndirect`、local size、組み込みの ID、shared memory と `barrier()`、SSBO と shared memory の atomic | compute の試験の群（足し算・reduction・shared memory と barrier・atomic・indirect）が 5330 の実機（passthrough と素の機械）で結果が正しい |
| G2 | libglesv2（WS068）の OpenGL ES 3.1 の compute が動く: GLSL ES 3.10 の compute shader、`glDispatchCompute`、SSBO（`glBindBufferBase`）、`glMapBufferRange`、`glMemoryBarrier`、headless の EGL（pbuffer） | GLES の compute の試験が Venus と i915 で結果が正しい |
| G3 | Noct の自動並列化（`accel_opengles.c`、headless の EGL と GLES 3.1）が zedBSD で GPU の backend を選び、見本の program（整数の演算の DOALL、例: 大きな配列の整数の hash。Noct の GLES の backend は float の program を GPU に出さないため。design §4）の結果が CPU と一致し、大きさ N で CPU より速い（N と倍率は p001 で決める） | 5330 の実機で時間を比べる |
| G4 | fg010 の台本の場面として見せられる（Noct の見本を GPU と CPU で走らせて時間を比べる） | 実機でのユーザーの確認 |

G3 の例の注（2026-09-30、p001 の設計）: Noct の OpenGL ES の backend は float を使う program を GPU に出さず、kernel の中の loop も扱わない
（DOALL と DOSUM だけ）ので、「行列の積」は GPU で走らない。G3 の見本は整数の hash の DOALL にした（[design.md](design.md) §4.2、判断 D3・D5）。

subgroup の操作（`subgroupAdd` 等）、image load/store、atomic counter は、Noct と試験が要らなければ範囲の外。ローカルの LLM は Future Work（F-061）。

## 分かっていること（2026-09-30 main の調べ）

- **i915**: device の情報は queue に `VK_QUEUE_COMPUTE_BIT` を立て、compute の上限値も報告するが、実行器（`src/drivers/gpu/i915/render/command.c`）は
  `vkCmdDispatch`・`vkCmdDispatchIndirect`（opcode 110・111）を扱わず ENOTSUP で拒む。compiler（`compiler/spirv.c`）は GLCompute を扱わない。
  kernel の試験（`tests/execution/eu-test.c`）は GPGPU_WALKER で compute のスレッドを実機で走らせている（Alder Lake は Gen12 の GPGPU_WALKER の世代）。
- **libglesv2**: GLES 3.1 の compute は ws068-p035（planning・保留）。
- **Noct**: `userland/base/noct/noct/src/accel/accel_opengles.c` は「Headless OpenGL ES 3.1 accelerator backend using EGL」で、使う API は
  EGL（`eglGetPlatformDisplayEXT`・`eglCreatePbufferSurface`・`eglBindAPI` ほか）と GLES（`glDispatchCompute`・`glBindBufferBase`・
  `glMapBufferRange`・`glMemoryBarrier` ほか）、workgroup の大きさ 64。**guest の Noct は `NOCT_ENABLE_ACCEL` が OFF で build されている**。
  有効にするのは Noct の build の規則の変更で、toolchain にあたるので **main の許可が要る**（AGENTS.md）。

## 危険

- compute に特有の設定（SLM の大きさ、barrier の数、スレッドの数）を誤ると GPU が固まり、切り分けに時間がかかる（WS031 の EU のハングの前例）。
- Venus の経路（QEMU）は host の driver で compute が動く見込みだが、Kei で compute を使った試験の記録は無い。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws101-p001](phase001/phase.md) | 設計: compiler（GLCompute・ID・SLM・barrier・atomic）、実行器（PIPELINE_SELECT GPGPU・MEDIA_VFE_STATE・interface descriptor・CURBE・GPGPU_WALKER・indirect）、GLES 3.1 の compute、Noct の accel を有効にする build の変更の案、試験と G3 の見本と N・倍率 | cleared（2026-09-30。[design.md](design.md)） | — |
| [ws101-p002](phase002/phase.md) | compiler の核: GLCompute、LocalSize・LocalSizeId、built-in（LOAD_SYSTEM、GlobalInvocationID、NumWorkgroups の A64 の読み）、compute の register の約束と binary、thread spawner への EOT、LOAD_STORAGE の predicate、効果なしの decoration、OpArrayLength、stage の分岐の見直し、SSBO の A64 の atomic、複数の動的な index、拒否（design §1） | cleared（2026-09-30。host の試験 PASS、kernel の build PASS。GPU は p005） | p001 |
| ws101-p003 | 実行器の object と記録: vkCreateComputePipelines（66）、bind point の分離、vkCmdDispatch（110）の記録（design §2.1） | planned | p002 |
| ws101-p004 | 実行器の batch: `render/compute.c`（IDD・CURBE・NumWorkgroups の置き場所・VFE・walker・3D への戻り・scratch 768 thread 分）、transfer_pending（dispatch と SSBO を書く draw）、HDC だけの PIPE_CONTROL、storage の range、WS101 の genxml の dumper（design §2.2〜§2.4） | planned | p003 |
| ws101-p005 | 実機の bring-up と kernel の試験の場面 `vkcs`（ADD・ID・ODD・PUSH・ATOMIC-SSBO・MIXED・MANYOPS・SPILL）。eu-test の 1 thread の設定から段階的に（design §2.6・§5.1） | planned | p004 |
| ws101-p006 | SLM・barrier・fence・SLM の atomic（IR・parser・EU、barrier の一様性の検査）、vkcs の SHARED・REDUCE・ODD-BARRIER・ATOMIC-SHARED・LOOP・REFUSE | planned | p005 |
| ws101-p007 | vkCmdDispatchIndirect（111）、0 の group、vkcs の INDIRECT・LENGTH・MANY、G1 の passthrough での受け入れの run（design §2.5） | planned | p006 |
| ws101-p008 | GLSL ES 3.10 の compute（版・stage・SSBO std430・shared・built-in・barrier と規則・atomic・`.length()` → SPIR-V）、Noct の shader の形の fixture。host の試験（design §3.2） | planned | p001（p002〜p007 と並行できる）、D4（main の記録） |
| ws101-p009 | libglesv2・libegl の ES 3.1 の compute の API（design §3.3）、`gl31.h`、export と stub、版の名乗り（D1）、libGL への漏れの分離、GLES の compute の試験の program。Venus で G2 | planned | p008、D1 |
| ws101-p010 | GLES の compute の i915 での G2（passthrough）。基本は p005 の後、shared・barrier は p006、indirect は p007 の後 | planned | p009、p005（p006・p007 の分は後） |
| ws101-p011 | Noct: toolchain の部分（design §4.1）は main が D2 の許可の後に行う。WS101 の分は G3 の見本（`mix.nct`、N = 4,000,000）、型名と offload の確認、CPU の時間の先の測定、Venus で正しさ、5330 の passthrough で正しさと時間（design §4.2） | planned | p010、D2、D3、D5 |
| ws101-p012 | 素の 5330: G1 の受け入れの残り、G3 の時間、G4 の fg010 の台本の場面と GPU が固まったときの回復の手順。ユーザーの確認 | planned | p011 |
| ws101-p013 | 規約の全文との照合（WS101 の全ての変更）と回帰（host、vkx・vke1・vke2・vkc・vkcs・GLES、boot test） | planned | p002〜p012 |

依存の図と日程の目安は [design.md](design.md) §6。G3 に要るのは p002〜p005・p008〜p011（Noct の DOALL は shared・barrier・indirect を使わない。
SSBO の atomic は p002・p005 に入れた）ので、p006・p007 が遅れたら G3 の経路を先にする。

## ユーザーと main の判断（design.md §7）

- D1: GLES の「OpenGL ES 3.1」の名乗り（ES 3.1 の全体は実装しない）。既定の案: compute を持つ device で名乗り、未実装の 3.1 の関数は error の stub。他の ES3 の app が 3.1 の経路で stub に当たる危険がある。p009 の前。
- D2: Noct の build の変更（toolchain の変更。main とユーザーの許可、実装は main）。既定の案: 構成で `ZEDBSD_NOCT_ACCEL := y` を置いた amd64 の `/bin/noct` だけ accel を ON。代案: 別の `/bin/noct-gpu`。patch level を上げると共有の host の Noct（`build/NoctLang`）も作り直しになる（避けるなら target だけの patch の一覧を分ける）。p011 の前。
- D3: G3 の N = 4,000,000（Noct は group の数 ⌈N/64⌉ が device の上限 65535 を越えると error）と倍率（素の 5330 で GPU が 3 倍以上、伸び 10 倍。CPU の時間を測ってから決め直す）。
- D5: デモの見本は整数だけでよいか（Noct の GLES の backend は float の program を GPU に出さず CPU で走らせる）。代案は Noct に float を許す patch（Noct の意味の変更、toolchain の変更）。
- D4（main）: ws068 の ws.md の p035 の行に、GLES 3.1 の compute の部分集合を WS101 へ移したと記録する。p008 の前。
