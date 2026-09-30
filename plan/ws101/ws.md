<!-- awesome-plan project=zedbsd record=ws101 -->

# WS101: GPU の compute（i915 の Vulkan の compute、GLES 3.1 の compute、Noct の自動並列化が 5330 の GPU で動く）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p011（L1、Noct と G3）in-progress（2026-09-30）: toolchain の差分を `plan/ws101/p011-toolchain/` に用意して main に報告。main の適用と build の後に、WS101 の分（`mix.nct`、測り方、script）と p014（G3 が passthrough で CPU と一致）。段の計画は下の「段（L1〜L3）の計画」
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
| [ws101-p003](phase003/phase.md) | 実行器の object と記録: vkCreateComputePipelines（66）、bind point の分離、vkCmdDispatch（110）の記録（design §2.1） | cleared（2026-09-30。host の試験 PASS、kernel の build PASS。dispatch は p004 まで ENOTSUP） | p002 |
| [ws101-p004](phase004/phase.md) | 実行器の batch: `render/compute.c`（IDD・CURBE・NumWorkgroups の置き場所・VFE・walker・3D への戻り・scratch 768 thread 分）、transfer_pending（dispatch と SSBO を書く draw）、HDC だけの PIPE_CONTROL、storage の range、WS101 の genxml の dumper（design §2.2〜§2.4） | cleared（2026-09-30。host の試験 PASS（Mesa の genxml で batch と IDD を decode）、kernel の build PASS。IDD の SLM・barrier は p006、GPU は p005） | p003 |
| [ws101-p005](phase005/phase.md) | 実機の bring-up と kernel の試験の場面 `vkcs`（ADD・ID・ODD・PUSH・ATOMIC-SSBO・MIXED・MANYOPS・SPILL）。eu-test の 1 thread の設定から段階的に（design §2.6・§5.1） | cleared（2026-09-30。5330 の passthrough で vkcs 9/9 PASS、回帰 vkx・vke1・vke2・vkc PASS。試験の kernel の大きさの残りが 6.6 KB） | p004 |
| [ws101-p006](phase006/phase.md) | SLM・barrier・fence・SLM の atomic（IR・parser・EU、barrier の一様性の検査）、binary の SLM・barrier と IDD の field（p004 の XXX）、vkcs の SHARED・REDUCE・ODD-BARRIER・ATOMIC-SHARED・LOOP・REFUSE | cleared（2026-09-30。試験の build の分割（I915_TEST_SET、main の判断）、5330 の passthrough で vkcs 15/15 PASS、回帰 PASS） | p005 |
| [ws101-p007](phase007/phase.md) | vkCmdDispatchIndirect（111）、0 の group、vkcs の INDIRECT・LENGTH・MANY、G1 の passthrough での受け入れの run（design §2.5） | cleared（2026-09-30。5330 の passthrough で vkcs 21/21（INDIRECT・INDIRECT-ID・INDIRECT-GPU・INDIRECT-ZERO・LENGTH・MANY N=4,000,000）、回帰 PASS、glescompute の indirect PASS） | p006 |
| [ws101-p008](phase008/phase.md) | GLSL ES 3.10 の compute（版・stage・SSBO std430・shared・built-in・barrier と規則・atomic・`.length()` → SPIR-V）、Noct の shader の形の fixture。host の試験（design §3.2） | cleared（2026-09-30。host の試験 PASS。WS068 の glsl-host の fail/version.vert（310 es は未対応を期待）の更新を main に依頼） | p001（p002〜p007 と並行できる）、D4（main の記録） |
| [ws101-p009](phase009/phase.md) | libglesv2・libegl の ES 3.1 の compute の API（design §3.3）、`gl31.h`、export と stub、版の名乗り（D1）、libGL への漏れの分離、GLES の compute の試験の program。Venus で G2 | cleared（2026-09-30。D1 は main の指示で既定の案。Venus で glescompute PASS（surfaceless・default・1000 回）、WS068 の egl-p020・p024・p027・glx-p031・p033 PASS、egl-p030 は版の検査だけ FAIL（egltest の期待が 3.0 固定、main に依頼）） | p008、D1 |
| [ws101-p010](phase010/phase.md) | GLES の compute の i915 での G2（passthrough）。基本は p005 の後、shared・barrier は p006、indirect は p007 の後 | cleared（2026-09-30。5330 の passthrough で glescompute PASS（indirect は SKIP、p007 の後に再実行）、egl-p030・p027 の場面 PASS） | p009、p005（p006・p007 の分は後） |
| [ws101-p011](phase011/phase.md) | **L1** Noct の toolchain の差分（design §4.1、D2 の形: target だけの patch の一覧と level、`ZEDBSD_NOCT_ACCEL`）を `plan/ws101/p011-toolchain/` に用意し main が当てる。WS101 の分: G3 の見本 `mix.nct`（N = 4,000,000、整数）、型名と offload の確認の方法、CPU と GPU の時間の測り方、試験の script | in-progress（2026-09-30。toolchain の差分と README を用意、main へ報告。main の適用と build の後に WS101 の分） | p010、D2・D3・D5（決定済み） |
| ws101-p014 | **L1** G3 が動く: 5330 の passthrough で `noct --gpu mix.nct` の結果が CPU の run と全要素で一致し、offload が起きた（DECLINED で CPU に戻っていない）証拠がある。Venus でも一致 | planned | p011（main の適用） |
| ws101-p015 | **L1** デモの場面 S13: デモの image（`ZEDBSD_NOCT_ACCEL := y`）の Terminal から、CPU と GPU の run を 1 つの手順で走らせて時間を並べて出す script（`/usr/share/…/s13.sh` 等）が passthrough で通る | planned | p014 |
| ws101-p016 | **L2** 時間の分解: CPU の run と GPU の run の各部分（EGL の初期化、shader の compile、upload、dispatch、readback）を passthrough と素の 5330 で測り、3 倍に何が足りないかを出す | planned | p015 |
| ws101-p017 | **L2** 3 倍: p016 で最も大きい 1 つか 2 つを直す（例: 呼び出しごとの buffer の作り直し、readback の経路、barrier の数）。素の 5330 で GPU の中央値 ≤ CPU の中央値 / 3 | planned | p016 |
| ws101-p012 | **L3** 素の 5330: G1 の受け入れの残り（vkcs 21 step を素の機械で）、G3 の時間、GPU が固まったときの回復の手順。ユーザーの確認 | planned | p017 |
| ws101-p018 | **L3** 10 倍: 素の 5330 で GPU の中央値 ≤ CPU の中央値 / 10（届かなければ到達した倍率と理由を記録して段を閉じる） | planned | p012 |
| ws101-p013 | 規約の全文との照合（WS101 の全ての変更）と回帰（host、vkx・vke1・vke2・vkc・vkcs・GLES、boot test） | planned | 各段の終わりに部分的に、最後に全体 |

## 段（L1〜L3）の計画（2026-09-30 朝 ユーザーの方針「広く浅く」、main の指示）

デモ critical の WS は、全 WS が同じ段にそろってから次の段へ進む。WS101 は G1・G2 が passthrough で済んでおり（p001〜p010・p007）、
G3 が動けば「まず動く」の段（L1）に届く。

| 段 | 目標（数値） | 測り方 | Phase |
| --- | --- | --- | --- |
| L1 | G3 が CPU と一致して動き、デモの場面 S13 が通る。N = 4,000,000（整数の hash の DOALL、`mix.nct`）で GPU の run の全要素が CPU の run と一致、offload が起きた証拠がある | 5330 の passthrough（`plan/ws101/tests/hw/` の script、guest の disk の log）。一致は Noct の出力の checksum と全要素の比較。offload は libglesv2 の dispatch が起きたこと（p011 で方法を決める: 例 `glescompute` と同じ calls の counter、または Noct の `--gpu-list`・DECLINED の表示） | p011、p014、p015 |
| L2 | GPU が CPU の 3 倍（D3）: 素の 5330 で、warm-up の後の 5 回の中央値で GPU ≤ CPU / 3 | `mix.nct` の中で Noct の時計（p011 で API を確かめる）を読み、CPU（`--gpu` なし、JIT が効いた後）と GPU（最初の 1 回の初期化と compile を除く）の 5 回の中央値。passthrough でも同じ数を取り、素の機械の数を判定に使う | p016、p017 |
| L3 | 10 倍（D3 の伸び）・素の 5330 で G1 の受け入れ | 同じ測り方で GPU ≤ CPU / 10。G1 は vkcs 21 step の素の機械での PASS | p012、p018 |

各段の終わりに p013 の照合と回帰を、その段で変えた範囲について行う。

依存の図と日程の目安は [design.md](design.md) §6。G3 に要るのは p002〜p005・p008〜p011（Noct の DOALL は shared・barrier・indirect を使わない。
SSBO の atomic は p002・p005 に入れた）ので、p006・p007 が遅れたら G3 の経路を先にする。

## ユーザーと main の判断（design.md §7）

- （2026-09-30 朝、D1・D2・D3・D5 はユーザーが決定: D1 名乗ってよい、D2 構成で選ぶ形で許可、D3 N = 4,000,000・3 倍以上、D5 整数だけ。master の「fg010 に必要な判断」）
- D1: GLES の「OpenGL ES 3.1」の名乗り（ES 3.1 の全体は実装しない）。既定の案: compute を持つ device で名乗り、未実装の 3.1 の関数は error の stub。他の ES3 の app が 3.1 の経路で stub に当たる危険がある。p009 の前。
- D2: Noct の build の変更（toolchain の変更。main とユーザーの許可、実装は main）。既定の案: 構成で `ZEDBSD_NOCT_ACCEL := y` を置いた amd64 の `/bin/noct` だけ accel を ON。代案: 別の `/bin/noct-gpu`。patch level を上げると共有の host の Noct（`build/NoctLang`）も作り直しになる（避けるなら target だけの patch の一覧を分ける）。p011 の前。
- D3: G3 の N = 4,000,000（Noct は group の数 ⌈N/64⌉ が device の上限 65535 を越えると error）と倍率（素の 5330 で GPU が 3 倍以上、伸び 10 倍。CPU の時間を測ってから決め直す）。
- D5: デモの見本は整数だけでよいか（Noct の GLES の backend は float の program を GPU に出さず CPU で走らせる）。代案は Noct に float を許す patch（Noct の意味の変更、toolchain の変更）。
- D4（main）: ws068 の ws.md の p035 の行に、GLES 3.1 の compute の部分集合を WS101 へ移したと記録する。p008 の前。
