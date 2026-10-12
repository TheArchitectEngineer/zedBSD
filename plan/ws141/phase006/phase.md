<!-- awesome-plan project=zedbsd record=ws141-p006 -->

# ws141-p006: kernel Vulkan実行器とSPIR-V compiler

Status: in-progress
Disposition: normal
Parent: [WS141](../ws.md)
Queue: [完成までの承認と有限実行scope](../execution-20261009.md#完成までの継続承認2026-10-09)

## scope/criteriaと依存

既存Venus wire/libvulkanを使うkernel実行器とV3D4.2のSPIR-V compilerを追加し、Keilandの描画/表示経路を成立させる。未実装capabilityは公開しない。

依存: p004 native job/cache/MMUとp005 resource/share。software実装の依存と実機のwhole acceptanceを区別する。実機はユーザーが後で行うと回答済み。

## 規則と確認

C全文 `plan/coding-style.md`、Guardrailのsource/ownership/HAL/GPL/scanout規則を適用。固定Linux/Mesa一次sourceを照合し、GPL資料/改名表はignored tempのみ。named rpi4 build（warning/error0）と必要な短いhost検証、formatter/style補助+全文manualを実施。QEMUはQ1/T1、Master/共有記録は担当から更新しない。

## 設計変更の出典（2026-10-09）

ユーザー「完成まで自走してください。」と回答「WS141に実行器・compilerも含め、Keiland表示まで進める」。p006の別WS判断を本WS内実装に確定し、p005はこの出力をdesktop描画の依存とする。実機関門を削除しない。[全体変更/実行範囲](../execution-20261009.md#完成までの継続承認2026-10-09)。


## trusted native loweringの依存（2026-10-09、i12）

[p004](../phase004/phase.md#i12のnative-job診断と後続interface2026-10-09)でCL/TFU/CSDのkernel内部job型/runnerを追加。p006はSPIR-V出力のQPU shader/uniform/attribute/texture、CLの間接list/pool/targetをp005のownerへ紐付け、入力CPU clean、GPU completion/必要なTMU clean、CPU output invalidateの境界を守る。retired=falseのfailureはp005/common recoveryにquarantineを渡し、Vulkan完了として通知しない。まだcapabilityを公開しない。[i12結果と制限](../execution-20261009.md#i12-native-v3dv1v10のsoftware結果2026-10-09)。

## p005 resource ownerの出力契約（2026-10-09、i13）

rendererのsession-local protocol resource IDとglobal native VAは別に管理する。resource viewの独立referenceをjobが保持し、common callbackの終了とuncertain DMAのstorage retirementを分離する。CPU/cache/view編集はcontroller mutexでnative executionから排他。BLOBは現段階のstorage登録ではblob_id=0だけを受け、p006はVulkan allocation objectへのnonzero blob_id bindingを追加する。COMMAND/CAPSETは実行器・compilerの完成後、登録前にbindする。allocation-only stopからworker join/callback drainへ置換する依存を保持。[i13](../execution-20261009.md#i13の二deviceallocation共有の実装2026-10-09継続中)。


## p005 workerのscoped prerequisite（2026-10-09）

private workerとjob tableはactual source host/buildで確認済み。prepared trusted payloadが独立VA viewをretainし、FIFO executor/disposerがcontroller mutex内、common completionはlock外でFINISHING後にslotを返す。Vulkan queue作成/破棄がIRQ guard下のsession timeline ownershipを管理する。最大8supervised marker/16全slot、commit allocation0、decoder completionとactual GPU completionの区別を保つ。p005 whole clearanceを依存出力として偽称せず、[このsource出力](../execution-20261009.md#i13-checkpoint-native-workerとsupervised-reservation2026-10-09)を使用する。COMMAND/CAPSET/JOB公開はp006の全bindingが完成してから。


## i14開始とcompiler部品（2026-10-09）

[統合済みowner/worker](../execution-20261009.md#i13-worker統合とi14-compiler開始2026-10-09)をscoped prerequisiteとして開始。既存device-independent Zlib scalar SPIR-V parserをread-only source再利用し、QPU4.2 encoder、register/value管理、VPM vertex/fragment interface、uniform/TMU/出力/終了を新規実装する。未実装capabilityを公開しない。kernel Vulkan wire/object/descriptor/queue実行器とnonzero blob bindingは後続で接続。実機whole acceptanceは後日。

## compilerのsoftware checkpoint（2026-10-09）

独立QPU encoder/scalar compilerを実装し、actual Keiland quad/panelの6stage variantsと各fragmentのblend/swap組合せを確認。固定Mesa decoder/repackerで全native wordsを照合、別scalar IR interpreterとの32input差分、actual uniform consumption/TMU4result/target4channelとnative4allocation refusalのownership unwindをPASS。RPi4 named build exit0・warning/error0。scalar証拠をnative GPU実行やWS clearanceとは扱わない。unsupported operations/control effectsは拒否しcapabilityを公開しない。[正確な範囲、制限、次のVulkan runtime](../execution-20261009.md#i14-compilerのsoftware-checkpoint2026-10-09)。

## Vulkan session/objectのsource出力（2026-10-09、継続中）

compilerはmainへ統合済み。新private Vulkan session/object ownerを追加し、typed ID/session isolation、registry/dependency/prepared workの独立reference、old identity再利用、closing namespace withdrawal、live ownerが残るcloseのEBUSY/arena保持、actual partial allocation unwindをhostで確認。caller controller mutex/worker joinを前提にしたsource部品であり、nodeのcommand/capset/runtimeへは未接続。private software gateはPASS、whole p006はin-progress。[範囲/確認/復帰点](../execution-20261009.md#i14-vulkan-sessionobjectのsoftware出力2026-10-09)。


## Vulkan transportのcheckpoint（2026-10-09）

実client wire writerでSET/SEEK/VERSION/外部streamを検証しPASS、named rpi4 y build warning/error0。明示reply capacity/retained CPU owner/最後のrelease atomic trailerと外部streamのbounded immutable copyを実装。typed command callbackは未接続、native GPU completionとは別。p006/i14はin-progress、次はinstance/device/memory/pipeline/draw/queue。詳細は[実行記録](../execution-20261009.md#i14-vulkan-streamのsoftware出力2026-10-09)。


## HOST_COHERENT memoryのmapping依存（2026-10-09）

実libvulkanのdiscoveryはhost coherent memoryを必須とする。cached V3D RAMをcoherentと偽らず、既存HALのNormal NC kernel aliasと同属性のuser translationを使う。shared GPU/VMの4 pathの具体的差分を[依存提案](../uncached-ram-mapping-proposal.md)へ用意し、未適用。担当source境界のため承認を求める。HAL API変更無し。private instance/device/runtimeは独立継続、COMMAND/CAPSETをdependency未達で公開しない。p005/p006 in-progress、Master/shared projectionはQ1。


## native root/queryとNormal NC memoryのcheckpoint（2026-10-09）

ユーザー承認のshared4 pathを適用し、private bufferのNormal NC alias lifetimeとrender/display→VM cache属性を接続。instance/physical/device/queue parent graphとexact timeline/domain所有、callbackが残るdomainのreuse拒否、native busy/faultをidle成功にしない処理、actual client codecでの有限physical queriesを追加。actual source host2 PASS、rpi4 y build warning/error0。native runtimeのmemory budget/limit enforcementは未接続、COMMAND/CAPSET/JOB未公開。次はVkDeviceMemory/nonzero BLOB、resource/pipeline/draw/queue。p006/i14 in-progress。[scope/commands/hash/制限](../execution-20261009.md#i14-vulkan-native-rootqueryとnormal-nc-owner2026-10-09)。

## i14 native memory/BLOB checkpoint（2026-10-09）

実client wireでlazy VkMemory/type0/export/importとnonzero placed BLOBを実装。actual VA/MMU sourceを含むhostでindependent references・declaration budget・OOM・failed flush quarantine/recovery PASS、rpi4 y build warning/error0。void commandのactual header requested=1を照合して先行root destructionも修正。COMMAND/CAPSET/JOBはまだ未公開、次はbuffer/image/layout/bindingとtyped rendering runtime。[正確な出力/修正/制限](../execution-20261009.md#i14-vkdevicememoryとplaced-blobのsoftware出力2026-10-09)。p006/i14はin-progress、Keiland/実機/最終適合は未達。

## i14 buffer/image/binding checkpoint（2026-10-09）

typed create/destroy/requirements/bind/linear layoutを実client codecへ接続。logical buffer extentとrounded requirementを分離、raster colour image pitchを定義し、independent binding/prepared ownerをactual VA source hostで確認。host3範囲/rpi4 y build PASS、style total0。public runtime未公開、次はview/sampler/shader moduleとdescriptor/pipeline/draw。[設計/失敗と修正/結果/制限](../execution-20261009.md#i14-bufferimagerequirementsbindingのsoftware出力2026-10-09)。p006/i14 in-progress、Keiland/実機/p007未達。

## i14 immutable input checkpoint（2026-10-09）

actual client handle/record encoderとnative sourceでcolour view・nearest/linear sampler・copied SPIR-V moduleを追加。image/root/module/jobの独立保持、registry OOM、arena/stream overwrite後のactual Keiland module byte保持を確認。host4範囲/rpi4 y build/style PASS。stage/entry/native compiler keyはpipeline createの後続、public runtimeは未公開。[結果/失敗と修正/制限/再開](../execution-20261009.md#i14-immutable-viewsamplerspir-v-moduleのsoftware出力2026-10-09)。次はdescriptor/pipeline layout、p006/i14 in-progress。

## i14 canonical layout checkpoint（2026-10-09）

actual client recordでdescriptor/pipeline layoutを追加。canonical bindings・same-device immutable sampler・finite set/push interfaceとindependent graph retentionをhostで確認。host5範囲/rpi4 y build/style PASS、次は512-set pool/set/updateとimmutable draw snapshot。public runtime未公開、p006/i14 in-progress。[結果/制限/再開](../execution-20261009.md#i14-canonical-descriptorpipeline-layoutのsoftware出力2026-10-09)。

## i14 pool/set checkpoint（2026-10-09）

actual client recordでfinite pool/reset/destroyとall-or-nothing set batch allocate/freeを追加。old prepared setのindependent graph/charge保持とcapacity reuse、2nd registry OOMの全rollbackをnative hostで確認。host6範囲/rpi4 y build/style PASS、next ordered descriptor update/copyとdraw snapshot。public runtime未公開、p006/i14 in-progress。[結果/限界/復帰点](../execution-20261009.md#i14-descriptor-poolset所有のsoftware出力2026-10-09)。

## i14 descriptor更新 checkpoint（2026-10-09）

actual client selected-field framing/generated copy recordでordered writes/copiesを接続。complete staged validation/rollback、destination immutable override、exact logical uniform interval、independent draw snapshotsのmutable set/reset/public retirement非依存をhostで確認。host7範囲/RPi4 y build/style PASS、public runtime未公開、p006/i14 in-progress。[結果/修正/限界/復帰点](../execution-20261009.md#i14-ordered-descriptor更新とdraw-snapshotのsoftware出力2026-10-09)。next render pass/framebuffer/compiled pipeline、native command/queue/common binding。

## i14 target checkpoint（2026-10-09）

single-colour render pass/framebufferをactual client recordへ接続。clear/load/present mapping/backdrop dependencyを保持、bounded native target compatibility/ownershipとprepared graphを確認。host8範囲/RPi4 y build/style PASS、public runtime未公開、p006/i14 in-progress。[結果/限界/復帰点](../execution-20261009.md#i14-render-passframebufferのsoftware出力2026-10-09)。next compiled graphics pipeline/recorded native draw/queue/common binding。

## i14 compiled pipeline checkpoint（2026-10-09）

same-device module/layout/passとactual native compilerを接続。唯一main entry/stage・canonical varying/FIFO・descriptor/push permission・attribute interfaceを確認しpartial compile/OOMを全退役。host9範囲/RPi4 y build/style PASS。wire create/destroy/public runtimeは未接続、native codeのGPU uploadはprepared draw ownerの後続責務。p006/i14 in-progress。[結果/限界/復帰点](../execution-20261009.md#i14-compiled-pipeline-graphのsoftware出力2026-10-09)。

## i14 pipeline wire checkpoint（2026-10-09）

actual private client selected-state encoderをreadonly wrapperで実行、independent native finite decoder/complete batch/partial member resultsとcompiler/typed ownerへ接続。host9範囲/RPi4 y build/style PASS、single compiled stack framesを確認、total public runtime call pathは後続。p006/i14 in-progress、COMMAND/CAPSET/JOB未公開。[結果/限界/復帰点](../execution-20261009.md#i14-graphics-pipeline-wire-batchのsoftware出力2026-10-09)。next recorded command/native prepared draw/queue/common binding。

## i14 primary/graphics recording checkpoint（2026-10-09）

actual client primary pool/buffer codecsとreal public9vkCmdのfinite native recordingを接続。whole batch rollback、pending mutation拒否、pool非cycle/registry退役とindependent old graph、selected colour clear/raw state/typed interfaces、first node OOM→End failure/clean re-recordを確認。ordinary descriptor update-after-recordの想定をVulkan 1.0仕様へ修正し、set generation/current validationとpending update拒否を追加。host11範囲/RPi4 y build/style PASS、actual native CL/GPU completionの証拠にはしない。p005/p006/i13/i14 in-progress、COMMAND/CAPSET/JOB未公開、Keiland/実機/p007未達。next immutable current draw preparation/native VA/code/uniform/TMU/CL、transfer/barrier/queue/common/public runtime。Master変更無し。[正確な結果/想定訂正/失敗と修正/限界/復帰点](../execution-20261009.md#i14-primary-command所有とactual-vkcmd記録のsoftware出力2026-10-09)。

## i14 ordered draw state checkpoint（2026-10-09）

exact push ranges/canonical set prefix compatibilityとdescriptor disturbance、stage別push/partial vertex/dynamic stateの2回walkを実装。logical fetch/compiled uniform/whole target/sample backing/alias feedbackをnative準備前に確認。actual host12範囲/RPi4 y build/style PASS。callbackはborrowed state観察のみ、独立native prepared owner/CL/DMA/queue/public bindingは後続、p006/i14 in-progress、Keiland/実機/p007未達。[結果/fixture correction/限界/再開](../execution-20261009.md#i14-ordered-draw-stateのsoftware出力2026-10-09)。

## i14 prepared CPU graph checkpoint（2026-10-09）

独立primary graph/consumed descriptor snapshots、distinct ordinary set/command pending charges、全OOM/counter rollback、pending free/reset/destroy guardを追加。actual host13範囲/RPi4 y build/style PASS。native code/VA/CL/DMA/queueは未接続。workerはfalse-retirement payloadを自動再disposeしないため、actual native disposerで明示quarantine transfer/checked reset後のreleaseを接続する残条件を記録。p006/i14 in-progress、Keiland/実機/p007未達。[正確な契約/結果/限界/再開](../execution-20261009.md#i14-immutable-prepared-cpu-graphのsoftware出力2026-10-09)。

## i14 native recordとtexture配置のcheckpoint（2026-10-09）

[実行記録](../execution-20261009.md#i14-native-shaderfetchtexture-recordとuif変換のsoftware出力2026-10-09): private 4.2 shader/attribute/texture/sampler serializerとstrict non-XOR UIF pixel変換を追加。callerはactual VPM capacity、uploaded code/uniform/default/fetch/scratchの独立owned intervalsを提供し、VCMを2 batchesとする。texture scratchの作成は先行image writeのnative completionとCPU visibility後のFIFO execution時。final fragment switchでscoreboardを取得し、real centre WをRF0へ供給するshader flagを有効にする。

確認: [native-state-host-test.sh](../tests/native-state-host-test.sh) / [XMLと逆pixel oracle](../tests/native-state-check.py) で8 full record・4847 pixels/padding/raster・atomic refusal PASS、rpi4 y build warning/error0、style total0。GPU upload/CL/queue/public bindingと実機は未達。near-final full-standard/license/similarityはp007で再確認する。Phase in-progress、変更は同Phase内、foreign interface/dependency変更無し。

## i14 native upload owner checkpoint（2026-10-09）

[詳細](../execution-20261009.md#i14-native-upload-storageのsoftware出力2026-10-09): private cached code/scratch allocationとnative VAの独立ownerを追加。actual PA bitsで配置し、compiled codeのlittle-endian upload/full padded cache cleanを実施。retired=falseは全保持、trueはNULL消費し、failed translation retirementをnative space quarantineが保持。whole prepared job quarantineとclosing session lifetimeは後続接続のまま。

[hardware host](../tests/v3d-hardware-host-test.sh) のactual mapping/refcount/cache/reset failure boundaryとcode bytes/padding確認がPASS、rpi4 y build warning/error0/style total0。fixtureの旧typed BLOB参照/linkとreset error期待を追従し、actual Vulkan source成功をmockで代替していない。public runtime/GPU launch/Keilandは未接続、Phase/WSをcleared/completedにしない。変更は同Phase内でforeign interface無し。

## i14 integer viewport uniforms checkpoint（2026-10-09）

[詳細](../execution-20261009.md#i14-integer-viewport-uniformのsoftware出力2026-10-09): copied IEEE viewportからXY shader scale/depth range/offsetをkernel integerのみでatomic生成。guard/round/stickyとnearest-evenを使用し、subnormal/逆depth/±zeroを保持。native-state hostのindependent host FP oracleで256境界＋1024固定sampleが全bits一致、既存XML/pixelもPASS、y build warning/error0/style total0。

次にuniform streamとFIFO時点のUBO read、owned descriptor pointers/native storage/quarantineを接続する。general softfloat/public API/HAL変更は無し。同Phaseのinternal loweringで、foreign dependencyを変更しない。Phase in-progress、actual GPU/Keiland acceptance/p007未達。

## i14 FIFO scalar uniforms checkpoint（2026-10-09）

[詳細](../execution-20261009.md#i14-fifo-scalar-uniform-streamのsoftware出力2026-10-09): actual compiled順のconstant/push/viewport/UBO/current coherent read/native descriptor addressをindependent CPU streamへ完成。whole prefix OOM/late refusal解放、typed logical bounds、stage別copied stateとcanonical bindingを確認。actual Vulkan-device host14範囲とy build warning/error0/style0 PASS。GPU mapping ownership/queue launchは未接続。次はowned code/uniform/TMU/fetch preparation、whole-job/session quarantine/CL/queue/runtime、実機とp007。Phase in-progress、foreign commitment/HAL/UAPI変更無し。

## i14 whole native draw preparation checkpoint（2026-10-09）

[詳細](../execution-20261009.md#i14-whole-native-draw-preparationのsoftware出力2026-10-09): FIFOのactual sampled UIF/owned descriptors/code/uniform/default/packed fetch/shader recordsをnative mapping rootへ保持、job-wide256MiBの全page padding予算、firstVertex rebase・float default・late OOM全解放・uncertain保持を実装。actual Vulkan/compiler/MMU source host15とy build warning/error0/style0 PASS。既存pipelineのmissing-component未実装限定を解消し、実pipeline R32→vec2 compile/releaseも確認。native CL/GPU launchは未接続。whole prepared/native job quarantineとsession/queue/runtime、実機とp007は残る。Phase内部、foreign scope/HAL/UAPI変更無し、in-progress保持。

## i14 native BCL/clipper checkpoint（2026-10-09）

[詳細](../execution-20261009.md#i14-native-draw-bclとclipperのsoftware出力2026-10-09): 116byte complete native draw BCL stateをwhole ownerへcopied、4.2 integer fine/coarse/guardband depth/viewport edge、drawable-area-scissor restriction、facing/provoking/interpolation/disabled inherited state/owned shader/rebased drawを実装。zero-input dummy readも追加。fixed XML全byte、800 new arithmetic cases、actual compiler/MMU root15範囲、named y warning/error0/style0 PASS。source/license fixed hash確認。pass list/実GPU launch/runtime/whole job+session quarantine/実機/p007は未達、Phase in-progress。同Phase内部loweringでforeign interface/依存変更無し。


## i14 whole native pass checkpoint（2026-10-09）

Complete BCL/RCL/generic tile listと、independent output/linked draw/9pass storage ownersを追加。fixed XML全7stream、actual compiler/prepared/MMU host16範囲、named y build warning/error0/style0を確認。256supertile上限に合わせ最大64×64tilesは4×4groupへまとめる。prepare/OOM中のtarget mutation無し、false whole retain/true complete teardown、全padded budget/zero draws/late rollbackを確認。CLEARはraw stateコピーだけでexecution前rectangle clear/native launchは後続、whole job/controller quarantine/closing sessionを公開前に接続する。native execution/Keiland/実機の証拠ではない。p006/WS未達を保持。[詳細/訂正/復帰点](../execution-20261009.md#i14-whole-native-pass-clとgpu-ownerのsoftware出力2026-10-09)。

## i14 private native executor checkpoint（2026-10-09）

整数UNORM clear＋whole passのpreflight/exact rectangle clear→existing native CL runner→retirement/output visibilityを接続。1037 independent IEEE colour cases/actual native graph＋explicit runner fixtureのhost17範囲/named y build warning/error0/style0を確認。single-use replay refusal、outside pixels保持、fault前CPU mutation無し、syntheticuncertain全root保持。native runnerはこのhostで明示mock、physical GPU/IRQ/cache/resetの証拠ではない。公開前にwhole prepared/session/controller quarantineを実装する。WS/Phase acceptance保持。[詳細/復帰点](../execution-20261009.md#i14-deferred-clearとnative-pass-executorのsoftware出力2026-10-09)。


## i14 whole native jobとclosed-session retirement checkpoint（2026-10-09）

whole pending primary/current passをpersistent controller quarantineへallocation無しでtransferし、閉じたrenderer/namespaceは最後のtyped ownerまで保持する。actual checked reset→payload→closed session→translation recoveryの順を接続。actual Vulkan host18範囲/actual renderer close・provider reset failure・retained destructor host/named y warning/error0/style0 PASS。runner/recoveryのhostモデルはphysical DMA proofではない。transfer/barrier/submit/public dispatch/common bindingとKeiland/実機/p007は後続、Phase in-progress保持。内部lifetime契約、外国scope/HAL/UAPI変更無し。[詳細/再開点](../execution-20261009.md#i14-whole-pending-native-jobとclosed-rendererのsoftware出力2026-10-09)。


## i14 explicit dependencies/implicit attachment layout checkpoint（2026-10-09）

same-device coherent dependency nodeをwhole pending primaryに保持し、FIFO barrierは全imageのold layout/backing preflight後にvisibilityとnew layoutをpublishする。native passのinitial/final layout lifecycleもnative retirementへ接続。actual public barrier encoder/runtime host19範囲、actual close/reset host、final named y warning/error0/style0 PASS。UNDEFINED discardと末尾mismatchのatomic refusalを区別して確認。native transfer/primary queue submit/public/common binding・physical Keiland・p007は未達、Phase in-progress保持。内部runtime出力、foreign scope/HAL/UAPI変更無し。[詳細/次](../execution-20261009.md#i14-explicit-barrierとimplicit-pass-layoutのsoftware出力2026-10-09)。


## i14 external-sharing dependency admission checkpoint（2026-10-09）

actual WSIのfamily0↔external pairを、exact bound memoryのexternal declarationがある場合に限定してprivate barrierへ接続。typed export/private allocationに基づく数値resource-description fixtureで双方のadmission/refusalを確認し、actual public barrier/whole native graph host19範囲・named y warning/error0/style0 PASS。external provider/GPU実動作とpublic queue/fence publicationは未達。shared Vulkan headerの欠けたcore tokenはprivate標準値で表現、共有source/HAL/UAPI変更無し。native GPU meta transfer/runtime/physical/p007は後続、Phase in-progress保持。[証拠/再開点](../execution-20261009.md#i14-external-family共有メモリのadmission-checkpoint2026-10-09)。


## i14 full-image native GPU clear checkpoint（2026-10-10）

actual public vkCmdClearColorImage→typed immutable primary→prepared pending graph→zero-draw native tile clear/storeを接続。TRANSFER_DST用途・bound same-device・remaining ranges・FIFO current layout、独立output/9storage、OOM/budget rollback、zero-draw quarantine/reset lifetimeを確認。actual encoder/native owner＋explicit runner host20範囲、final named RPi4 y warning/error0/checks3/style0 PASS。CPU target write無し。mock runnerはGPU pixelを書かず、実機clear/Keiland成功は未確認。copy/blit/readback・primary queue submit/fence/semaphore/public/common binding・final runtime stack/p007が残り、Phase in-progress/WS incompleteを維持。Master/shared source/HAL/UAPI変更無し。[証拠/失敗と修正/復帰点](../execution-20261009.md#i14-full-image-gpu-clearのsoftware出力2026-10-10)。


## i14 native image copy/blit checkpoint（2026-10-10）

actual public copy/blit→完全immutable primary/pending graph→内部kernel-compiled texture quad/native passを接続。raw copy、nearest/linear拡縮・両axis反転・RGBA/BGRA conversion、source全sample footprint/physical alias、multi-region FIFO/whole-pass quarantineを確認。temporary数値metaのみで公開仮object無し、CPU destination pixel write無し。actual client/kernel-source＋明示runner host21範囲、final RPi4 y warning/error0/checks3/style0とown SPIR-V validator PASS。実GPU pixelはmockしないため実機/Keiland成功は未確認。buffer readback/transfer・QueueSubmit/fence/semaphore/public/common binding・final stack/p007が残り、Phase in-progress/WS incompleteを維持。Master変更無し。[正確な範囲/codec誤りの修正/確認/復帰点](../execution-20261009.md#i14-native-image-copyblitのsoftware出力2026-10-10)。


## coherent buffer transfer手順（2026-10-10）

buffer copy/upload/readbackはNormal NC coherent backingのCPU byte/row copyをFIFO native completion後に実行する。4.2 TFUのraster出力非対応と既存coherent memory境界に基づく内部手順の選択。image copy/blit/clearとKeilandのGPU描画条件は維持。physical aliases・whole preflight・explicit layout・paddingとnext native visibilityを確認する。p005のownership/public interface/dependency/WS acceptanceは変更無し。[統合と改訂理由](../execution-20261009.md#image-copyblitのmain統合とcoherent-transfer設計2026-10-10)。


## coherent buffer copy checkpoint（2026-10-10）

actual public CopyBufferのcomplete typed byte vectorをpending primaryへ保持し、FIFO native retirement後のNormal NC CPU転送へ接続。exact13/17byte以外保持、record後source更新、physical alias/末尾failure無部分retain・pending disposalをactual host22範囲で確認。native runnerは起動しない。RPi4 y warning/error0/checks3/style0 PASS。buffer/image upload/readback・queue/fence/semaphore/public/common binding・final stack/p007と実機/Keilandが残り、in-progress/incompleteを維持。Master変更無し。[正確な範囲/検証/復帰点](../execution-20261009.md#i14-coherent-buffer-copyのsoftware出力2026-10-10)。


## coherent buffer-image transfer checkpoint（2026-10-10）

actual public upload/readbackをimmutable pending primaryとFIFO Normal NC CPU行copyへ接続。部分upload/packedとpadded readback/unused padding alias/late invalid region/OOM/layout failure不変/whole pending lifetimeをactual host23範囲で確認。RPi4 y warning/error0/checks3/scoped style0/diff0 PASS。native GPU launch無し。queue/fence/semaphore/public/common binding・final runtime stack/p007・実機/Keilandが残り、in-progress/incompleteを維持。Master変更無し。[正確な範囲/検証/復帰点](../execution-20261009.md#i14-coherent-buffer-image-uploadreadbackのsoftware出力2026-10-10)。


## core sync checkpoint（2026-10-10）

native fence/binary semaphoreのtyped device ownership、初期状態/status/atomic reset/destruction/OOMを実装。実client codec/transport＋明示pending/fault modelのhost24範囲、RPi4 y checks3/warning-error0/scoped style0 PASS。QueueSubmit completion/common marker/public binding/Keiland/physical/final runtime stack/p007は残り、in-progress/incompleteを維持。Master変更無し。[詳細/検証/復帰点](../execution-20261009.md#i14-core-fencebinary-semaphoreのsoftware出力2026-10-10)。


## actual QueueSubmit checkpoint（2026-10-10）

実legacy client submit encoderとnative QueueSubmitを接続。全graph/prepared8MiB/256actionのpreflight後、既存COMMAND worker内でnative退役を直列確認し、binary chainsとfinal native fenceを完成させる。OOM/prefix refusal/one-time/uncertain whole quarantine/empty work fault refusalを実client private encoder＋actual kernel source/明示runner host25範囲で確認。RPi4 y checks3/warning-error0/scoped style0/diff0 PASS。COMMAND/CAPSET/JOB binding/public runtime・final stack/p007・実機/Keilandは残り、in-progress/incompleteを維持。Master変更無し。[exact scope/内部手順/限界/次](../execution-20261009.md#i14-actual-legacy-queuesubmitのsoftware出力2026-10-10)。


## public runtimeのsoftware checkpoint（2026-10-10）

actual render openでcomplete Vulkan ownerを作り、production combined20route/async immutable snapshot single-worker/real callback drain/capset168byte strict queue/owned domain/JOB capacityを登録前にbind。actual host26範囲とactual renderer register/open/close/hardware-owner host2 summary、final RPi4 y warning/error0/checks3 PASS。公開LTO ELFにruntimeが残ることを確認。受理後mutable input、whole CPU OOM unwind、fence completed after worker、16slot limit、close joinとfault error handoffをactual callbacksで確認。single-thread/common observerとMMIO fixtureの限界を保持し実機/Keiland成功と偽らない。legacy pending no-overtake EAGAINは内部手順でAPI/WS criteria不変。Statusはin-progress、whole callgraph16KiB/p007/full physical acceptanceは未達。[全手順・commands・結果・制限](../execution-20261009.md#i14-public-native-vulkan-runtimeの接続2026-10-10)。


## public runtime stack修正のcheckpoint（2026-10-10）

全命令LTO inspectionでpipeline frame4560byteを発見し、4件metadataを既存command arenaへ移して256byteに削減。深いconstant graphとparserによるcompute stage上書きをprivate compilerのiterative preflightで止め、read-only共有parserを維持。actual graphics4件partial batch/arena再利用、実Keiland scalar/decoderと2048段拒否・exact9level受け入れ・OOM/診断のhost、RPi4 y warning/error0/checks3/style0を確認。partial context estimateはworker9344byteで、IRQ/outer caller/unknown indirectを含むwhole16KiB証明は後続。whole Phase/p007/実機関門は未clear。[命令の取りこぼしの訂正/範囲/検証/残件](../execution-20261009.md#i14-public-runtimeのstack修正とgraphics-admission2026-10-10)。

## 最終software監査（2026-10-10）

SPIR-V/compiler/typed Vulkan/QueueSubmit/public caller stackの最終sourceを[p007 audit](../p007-software-audit.md)で確認。対応hostとnamed rpi4 y/n build PASS、code修正と制限は[実行記録i15](../execution-20261009.md#i15-最終software監査と検証2026-10-10)。software結果で実機のwhole acceptanceをclearせず、Statusはin-progressを保持する。Master/共有投影はQ1。

## software統合の終了（2026-10-10）

i14 compiler/Vulkan/Keiland software経路のsoftware/統合部分をcleared。final runtime/stack/全source監査とmain merge5eb8867f3を確認。whole Phaseの実機/Keiland条件は未達、Statusはin-progressを維持。[全結果と復帰条件](../execution-20261009.md#i15-main統合software実行の終了2026-10-10)。

## 共通frontendへの移行（2026-10-10）

ユーザーの明示指示でparser/IR依存を新[p008](../phase008/phase.md)のGPU共通moduleへ移す。V3D QPU backendとpublic Vulkan/Keiland契約は保持。静的改善と最終検証は[今回i16〜i18](../compiler-refactor-20261010.md)、実機whole acceptance未確認。

## Keiland起動の実機依存補完（2026-10-11）

実機でV3D選択とVulkan objects/compose初期化からKWL READYまで進むが、user timing命令のSIGBUSで終了。[p012](../phase012/phase.md)のEL0 counter初期化補完後に実描画を再確認する。現時点でcompiler/render queueが実frameを完了したと主張せず、software確認と物理受入れを区別する。

## counter修正後の実機到達点（2026-10-11）

[p012](../phase012/phase.md)のcounter trap消失をSSH確認。自動ログインとgreeterはOUTPUT open 1920x1080/images3まで進むが、最初の描画submitがresult=-4（VK_ERROR_DEVICE_LOST）でframes0/error5。実描画の受入れは未達、Phase状態を維持。kernel/SSHは応答し、原因のjob/register証拠は未採取。過去のsignal10と今回のsubmit失敗を区別する。次は既存p006内のGPU送信失敗調査、実機の更新・再起動は今回行っていない。

## submit-i01: 初回描画失敗の修復（2026-10-11）

Queue ws141-first-submit-20261011 / active / submit-i01 / in-progress / Codex。ユーザー「修正を行ってください。」とKeiland完成までの継続承認に基づく有限scope。既存V3D描画経路の最初のsubmit失敗を実code/Mesa固定一次資料と照合し、特定できた不整合を修復する。必要な失敗段階・native IRQ/queue register診断、限定host確認、arm64 warning0 build、p007規約レビュー、承認済み自動main統合を含む。HAL API/shared計画/toolchain変更無し。実機での最終表示成功は別証拠を要し、実機再起動はユーザーが行う運用を保持。一次sourceの数値事実を参照しcodeは独自実装、GPL資料はignored temp内のみ。

### 診断配置と再起動の承認

ユーザー回答「配置してよい。配置後に電源OFF/ONする」。続いて「デバッグダンプはカーネルにもアプリにも入れていいです。SSHで自由に更新してください。カーネルについて、リセットは私に言ってくれればやります。rebootコマンドが効くなら使っていいです。」と承認。これ以後、この修復に必要な診断追加・SSH配置・利用可能なreboot実行は再確認せず実施できる。保存済みの復帰用kernelを保持し、更新時はsync/read-back照合する。

### submit-i01診断checkpoint

診断82094cc1aをmain統合。make -j16 build/arm64/vmunix exit0/warning0/error0、ARM64 checker3 PASS。C全文manual/style-check total0/diff-check PASS、clang-format19.1.7対象範囲出力を確認し全文規約の独立引数/tabsを保持。failure loggingのみのため実装を写す追加mockは作らない。image SHA256=61bbed74b2f2ae961d783bb04543cdf50a3db4ce9402f844538f0661bb24702f、cksum599806020/8077312。実機へ転送後FAT staging/cmp/sync/rename/sync/cmpと全体cksum一致を確認。旧版はUFS /var/backups/vmunix.before-submit-diag-20261011へ保存（counter修正2c674080版）。転送中に先行確認が部分長7833600を観測したが、転送終了と配置完了後のcmp/cksumで正しい8077312全体一致を確認してからreboot手順へ進んだ。

配置後の再cmpとcksumで/tmpと/boot/boot0の双方599806020/8077312、UFS旧版3003282929/8077312を確認。sync後/sbin/rebootはexit0で要求受理、SSH切断後に復帰せず。ユーザーへ連絡し「電源OFF/ONした」の回答を受領。再起動コマンドによる実際のreset成立は未確認で、GPU修復と混同しない。

### submit-i01 実機での期限切れ確認とコピー改善

電源再投入後の82094cc1aで、通常描画はnative jobを投入する前の `submit prepare opcode=133 error=5`。V10の意図的timeout42とは別事象。libvulkanの通知結果診断をSSH配置し、`VULKAN decoder failed status=42` を実測。GPU_COMMAND_WAITのioctl自体のtimeoutではなく、kernelがcompletionへETIMEDOUTを公開していた。QueueSubmitの予約は応答後にcommitされるため、実行も同期で行う現在のV3D backendでは準備中も10秒のreservation期限が走る。期限を延ばして成功とはせず、まず準備の過剰な処理を減らす。

native texture converterは4byteごとのkern_memcpyを呼び、arm64のword8byte経路に入れず、Normal NCのsourceを1pixelあたり4回byte-readしていた。UIF内で連続した4pixel/16byteのutile行をまとめ、右端だけ有効pixel数に縮める。source/padding/alias/overflowの検証とUIF配置は維持。pass準備の段階・元error・完成draw数・staging bytes・所要msを、失敗または1秒以上の場合にteardown前に記録する。実機での速度改善と描画成立は次のkernel起動待ちで未確認。

libvulkan buildの初回は既存sysrootの前提からLLVM再取得へ進もうとしてDNS失敗。toolchainを再buildせず、既存の本worktree arm64 sysroot completionをmake -oで保持し、`make -j16 -o /home/awe/zedBSD-claude1/.claude/worktrees/rpi4-sshd/build/arm64/sysroot/.zedbsd-sysroot-complete build/arm64/dynamic/libvulkan.so` がwarning/error0で成功。旧実機libraryを `/var/backups/libvulkan.before-submit-diag.so` に保存、staging/cmp/rename/sync後cksum2182268690/222880を確認、`service restart greeter` 成功。ユーザーの継続SSH更新承認の範囲内。shared LLVM/ソース/成果物は変更しない。

改善版 `make -j16 build/arm64/vmunix` はwarning/error0、ARM64 checker3 PASS。libvulkan最終整形後再buildもwarning/error0、配置済みbinaryとcksum一致。kernel SHA256 `46d65e3ef46c82fb47480441048e9bc4ec48640ed798cd325aab31a82a38054f`、cksum `2120890253 8077312`。source統合と実機転送へ進む。p007変更scope検証完了、whole p006実機描画は未達。

改善/診断commit `ee848d0eb` をclean main `82094cc1a` からfast-forward統合し、3sourceが検証worktreeと同一、main cleanを確認。SSHで転送終了を待ってcksum2120890253/8077312を確認、greeterを停止。旧版を `/var/backups/vmunix.before-prepare-fix-20261011` に保存/cmp、新版をFATの別名へ書き込み/cmp/sync/rename/sync/cmp。最終 `/boot/boot0/vmunix` は2120890253/8077312、旧版599806020/8077312。全処理exit0。ユーザーへ電源OFF/ONを依頼済み。次はこの版のpass prepare行/decoder status/実描画をSSH確認する。現時点では旧kernelが稼働中で、改善版の物理効果は未確認。p006/WS未clear、shared投影Q1、push無し。

### submit-i01 実描画到達とアルファ合成の調査

ee848d0eb起動後、ユーザーから「GPUでWaylandが表示された」と写真の証拠。青いアイコンは青く、文字と角丸に矩形の背景が残る。RGBA順の単純な交換と決めず、実機の小さなoffscreen Vulkan読み戻しでblendを調査する。通常sessionは依然frames0/status42（35draw準備10177ms）、fallback greeterは70draw/251904000bytes/約6.5〜7.8秒で描画できている。初回期限とalphaの両方が未解消、whole受入れ未clear。3000ms待ちはboot診断だけで通常submitにはない。

ユーザー「修正完了まで、SSHでカーネルを更新して、rebootコマンドを使い、自走のループ」「再起動できないときは教えて」の継続承認。必要なkernel/app更新・再起動・ログ確認を自走し、再起動が戻らない場合だけ物理電源操作を依頼する。色/alphaと既存準備期限の修復が有限scope、Master変更なし。診断はplan/ws141/temp/colour-probeに置き、通常公開Vulkanを使う。SSHの非seatユーザーにはGPUが列挙されないためsudoで試行する。

実機offscreen 64x64（同じ頂点色、背景RGBA=(51,102,153,255)）で、blend無効=(32,64,0,128)、alpha0.5=(32,65,0,129)、alpha0=(0,2,0,2)、alpha1=(64,128,0,255)。opaqueは正しく、透明で背景を保持できない。APIはADD/ONE/ONE_MINUS_SRC_ALPHAを正しく保持している。Mesa固定一次資料のformat keyではUNORM8はhalf-float tile readであり、2個のpacked RG/BAを4成分へ展開する。現在のfloat32読み出しという仮定と実測の小値が一致するため、private QPUにhalf-low/high変換を追加し、tile読み出しを2pairに修復した。write側の既存float32変換とチャンネル順は維持。

限定検証: qpu-format-host-test（固定Mesa pack/unpackによるFMOV low/highの一致）、shader-host-test（実Keiland SPIR-Vと数値比較）PASS。fixtureの誤ったfloat32 tile前提も修正し、halfで正確に表せる非対称の背景値を使用。make -j16 build/arm64/vmunix exit0/warning0/error0/checker PASS、production style-check total0/diff-check PASS。SHA256=6a930700e67354bbf80e9f0c3a2356a831dc7ab2fdfcbe0848ee40568a5b88eb、cksum2513064968/8077312。実機の修正版結果は未確認。

修正commit70879c7c9をmainへfast-forwardしcleanを確認。実機の旧kernelをUFS `/var/backups/vmunix.before-alpha-20261011`（cksum2120890253/8077312）へ保存/cmp、新版をFAT stagingへcopy/cmp/sync/rename/sync/cmp。配置完了のcksum2513064968/8077312で一致、2026-10-11 09:06 JSTにsync後reboot要求を送った。物理再起動と修正後結果の確認待ち。

### 実表示の改善と接続待ち

ユーザー回答「Waylandが正常表示されました」。70879c7c9のアルファ修復は物理画面で改善した。ただしユーザー回答には電源操作の有無が明記されていないためreboot自体の成功/失敗とは分ける。SSHは10.0.30.2へtimeout/No route、ホストのARP隣接状態FAILEDでpingも届かない。現在IPをユーザーへ確認中。修正版のoffscreen再読み戻しは未実施、実機の表示証拠を数値試験と偽らない。

### 同一passの画像変換の共有

準備期限切れの独立修復として、描画ごとに同じ画像全体をコピーしていた処理を、同一の未送信pass内だけで再利用する。最初のdrawが所有する変換済みtexelを後続drawが借り、sampler/texture record・shader・uniform・vertexは個別に保持。passに画像書き込みやattachment feedbackは入れられず、先行queueは退役済み、primaryが実resourceを保持。別pass/別frameにはキャッシュを持ち越さない。whole-pass退役が証明されるまで元のdrawも保持し、途中失敗は借りた画像を解放しない。既存budgetは実際に新しく獲得したpage分だけ減らし、timeout定数の延長はしない。

actual vulkan-device-host-testで既存確認と、2drawの1画像共有・第二drawの4KiB削減・全体解放・借用後budget失敗時のrollback/二重解放なしを確認。既存diagnosticがhostリンクへ追加したclock/log依存にfixtureの非時間測定/表示なしstubを補い、productionを切り替える試験環境変数は追加していない。build最終sourceでmake -j16 build/arm64/vmunix exit0/warning0/error0/checker PASS、style-check production total0/diff-check PASS。SHA256 d06bf2e8410ad99493f56b588254f1af03c2dda499e9be3fc011b51378cbe8a3、cksum952016436/8077312。実機へは未配置（接続待ち）、速度向上を実測済みとはしない。

### 応答遅延の仮説と起動直後の採取

ユーザーは時刻が時々更新されるため、GPU負荷によるカーネル応答の極端な遅延を指摘。ARP削除後もFAILED、30秒SSHもNo route。V3D workerはSCHED_PRIORITY_DEFAULT、pass preparationはsleepable controller mutex内、hardware guardは短い状態取得/発行部分だけ。tile copyとcache cleanの全体をirq禁止にする経路は調べた範囲ではない。GPU job待機もguard外でsleepする。したがって「GPU準備7〜10秒」は実測事実だが、「それがネットワーク不通の直接原因」は未確定。ユーザーへ電源再投入を依頼し「電源ONしました1分待ってアクセス」の指示を受領、既存再接続loopを止めて待機する。

電源ONの回答後、依頼どおり1分待ってSSHを試行。さらに30秒追加待機して再試行したが、いずれもNo route to host。サービス停止コマンドは接続できていないため実行されず、重複コピー削減版も未配置のまま。実機の `ifconfig en0` または操作不能の情報を依頼。GPU負荷だけを原因と断定せず、現行IP/link/RX/TXの証拠を次の再開条件とする。SSH不通が続く場合はbuild/arm64/vmunix（cksum952016436/8077312）をSDから手動更新する必要がある。

### submit-i01 SSH復帰とPTE同期範囲（2026-10-11）

ユーザー「ssh kei@10.0.30.2 で入れます」の後に接続成功。実機 /boot/boot0/vmunix はcksum952016436/8077312で、既に同一pass画像共有版と一致（配置経緯は未確認、旧未配置記録は当時の観測）。通常43drawは17,063,936bytes/約2.1〜2.7秒に減り14frame表示したが、1回11,132msの準備がありdecoder status42で終了。fallback greeterは70draw/12,304,384bytes/約2.7〜2.9秒。greeterをservice stopで停止し、独立診断を実施。sessiondはservice名ではなくunknown-serviceとなったため停止成立とはしない。

実機offscreen再読み戻しはblend無効=(32,64,0,128)、alpha0.5=(58,115,77,255)、alpha0=(51,102,153,255)、alpha1=(64,128,0,255)。全て期待値に一致し、ユーザーの正常表示報告を数値でも確認した。RGBA順の交換は不要。

次の準備負荷として、各native bufferのmap/unmapごとに4MiBのページ表全体をcache cleanしていた。private pages_syncへGPU VAと長さを渡し、変更PTEに対応するCPU範囲だけcleanする。HALは端のcache lineを含める実装でありAPI変更なし。MMUC flush→TLB invalidate、前後admit、失敗時quarantine/保持、全体recoverのfull-table cleanは維持。範囲は4KiB整列・非空・4GiB内で事前検証する。

v3d-hardware hostの初回linkは既存pass timing診断のclock_ticks/kern_logf stub不足で失敗。未完了p006が参照する実hardware fixtureに観測用stubを補い、再実行PASS。変更PTE先頭/末尾/全体recover/無効範囲と従来cache/retirementを確認。vulkan-device hostもPASS。make -j16 build/arm64/vmunix exit0/warning0/error0、ARM64 checker PASS。production style-check total0、diff-check PASS、clang-format19出力との比較は全文規約を優先して独立引数行を保持。kernel SHA256=4dd6fbfcc4edd875c7b7cd576d45e6b0b83d28929d824dcfbb832b9dc64c5796、cksum513824364/8077312。新kernelの実機速度はまだ未確認。


PTE範囲修正da55b9a53をmainへfast-forward統合しclean確認。SSH転送終了後、旧kernelをUFS /var/backups/vmunix.before-range-sync-20261011へ保存/cmp（952016436/8077312）。新版をFAT stagingへcopy/cmp/sync/rename/sync/cmpし、最終513824364/8077312の一致とexit0を確認。進捗照会はコピー途中7,208,960bytesを観測したが、再起動は完了後の最終全体照合を待った。次はreboot後の描画・SSH・準備時間を確認する。


reboot要求はexit0。初めの2回はtimeout/No routeだったが、約2分後の3回目にSSHが戻り、起動直後のkernel/session logを取得。今回は電源操作を依頼せず再起動できた。通常desktopは描画開始したが準備は約2〜5秒であり、範囲cleanだけで応答遅延が解消したとは判定しない。内訳採取を続ける。


範囲同期版は通常18frameで再びstatus42。prepareは96draw/32,714,752bytes/7,506msまで増え、frameの全処理が予約期限に届く可能性が残る。greeter serviceを停止し、既存slow-pass診断へtex/copy/program/recordの内訳を追加する。clockはwall time（preemptionを含む）でCPU占有時間と誤認しない。copyはtexの内数、成功したdrawの合計なので失敗drawの未publish分は含まない。余計なper-drawログを出さず、既存の1秒以上/失敗passの行へ記録する。public API/動作制御変更なし。対象device host PASS、production style0/diff-check PASS。

内訳診断kernel最終build exit0/warning0/error0/ARM64 checker PASS。1466012252 8077312 build/arm64/vmunix、dffbdc237d5b20fad3bc462a1636b6c5cf0b84c105a1d272fd8a252e7a026c46  build/arm64/vmunix。


内訳診断25fb2ba4bをmain統合、旧kernelを /var/backups/vmunix.before-prepare-timing-20261011（513824364/8077312）へ保存し、新版1466012252/8077312をFAT staging/cmp/sync/rename/sync/cmpで配置。reboot exit0、約2分でSSH復帰。定常43drawの代表値はtotal2385ms/tex410ms（内copy170ms）/program1505ms/record382ms。起動負荷中はtotal5968ms/program3650ms。画像copyだけが主因ではない。

既存MMU/cache waitは最初のbusy read直後にkern_usleep_range(10,10)を呼ぶ。実装は次のscheduler tickまで休止し、CPU負荷でそれ以上待つため、1drawあたり多数の小さなcode/uniform mappingへ待ちが累積する可能性がある。private wait_clearに最大32回の短いMMIO readを追加し、それでもbusyの場合のみ従来の有限yield loopへ移る。IRQを禁止せず、既存flush順序/異常時保持を変えない。実機効果は次の配置後に判定する。

短いpoll版build exit0/warning0/error0/ARM64 checker PASS。4227642040 8077312 build/arm64/vmunix、1ba0b953d89d5db6e5bfc97135a776a9ba31419a6eb2eada75215f9ebed0bb51  build/arm64/vmunix。host初回はmockがclear命令前のcontrol readにもbusyを注入していたため失敗。命令write後だけbusyにするfixtureへ修正し再実行する（productionをtestに合わせて変更しない）。

host再実行PASS、短時間busyのsleep0完了とstuck時の既存timeout/retirementを確認。実機性能は配置後に確認する。


短いpoll版5d6b650d1をmain統合、旧1466012252/8077312を /var/backups/vmunix.before-fast-mmu-20261011へ保存し、新4227642040/8077312をFAT staging/cmp/sync/rename/sync/cmpで配置。reboot exit0、再接続成功。43drawの代表total2383ms/program1502msで前版との差は明瞭でなく、速さの修復成功とはしない。native storageに物理確保/map/cleanの内訳を追加、既存slow-pass行へoriginal ownerだけ合算する（tex等の内数）。この追加計測は対象device host PASS、C全文manual/style0/diff-check/clang-format19比較、arm64 warning0 build PASS。
2232080428 8077312 build/arm64/vmunix、78965e0b160dc05c1df9dc63e04fb2e9d1aacb97faa2d655c7ac0392a9d1441a  build/arm64/vmunix。新診断版は未配置。service stop greeterのSSHが途中切断したため停止成功は未確認、接続を再確認する。


storage内訳診断9f3a203e9をmain統合、新2232080428/8077312をFAT staging/cmp/sync/rename/sync/cmpで配置。旧4227642040/8077312は /var/backups/vmunix.before-storage-timing-20261011。reboot exit0でSSH復帰。43drawの代表total2280ms/alloc943ms/map499ms/clean5ms、copy149ms。GPU MMU待ち以外に物理確保も遅い。arm64のalloc_ramが毎回page1からbitmapを走査し、512KiBの固定heapで収まらない小さいkern_mallocも物理pageへfallbackする実codeを確認。

HAL APIを変えず、arm64/page.cに「ここより前は全て使用中」というfirst_free_candidateを保持する。先頭の占有bitだけを飛ばしてから従来と同じalignment/boundary/limit/sizeのfirst-fit検索に入り、低いpageの解放時は同じIRQ除外内で候補を戻す。サイズやalignmentの都合で飛ばした空きholeを候補から除外しない。既存1CPU実装と排他を維持。ユーザーのGPU応答遅延修復およびHAL実装変更の既存許可に基づく必要な補完で、hal.h/責務/ABIの変更なし。

実page.cを含む限定host試験: `cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -idirafter include -I src plan/ws141/tests/arm64-page-cursor-host.c -Wl,--gc-sections -o build/ws141-page-cursor-host && build/ws141-page-cursor-host` PASS。固定seed4096回の断片化/解放/size/alignment/DMA上限/boundaryを独立first-fit参照と比較し、選ぶ物理pageも一致、全解放後の低位再利用と候補以前の使用中不変条件を確認。GPU performanceの実機証拠は配置後に採取する。

候補保持版の最終arm64 build exit0/warning0/error0/ARM64 checker PASS。4190670006 8077312 build/arm64/vmunix、7007255529342daec9988c20307ca3a9538064a9b2ba0d2db000d8321cda4766  build/arm64/vmunix。


物理page候補版13737b751をmain統合し、旧2232080428/8077312をUFS /var/backups/vmunix.before-page-cursor-20261011へ保存、新4190670006/8077312をFAT staging/cmp/sync/rename/sync/cmpで配置。rebootから約2分でSSH復帰。SSH応答約0.46秒、prepareが1秒以上の診断は出ず、desktopは継続。ただしcomposeは0.8〜2.4秒、後の操作中は2.8秒程度を観測。ユーザーも「多少反応するがまだ1000ms程度」と報告。idle poll90%は操作時の描画性能を証明しない。引き続きsubmit-i01 in-progress。

残る全面CLEARはuncached RAMへ4byte単位の各byte storeを全画素で行い、その後GPUでLOADしていた。全面かつimage全域のCLEARだけ既存のnative tile clearへ切替。partial rectangle/小さいframebufferはCPUで正確な矩形のみclear後LOADする従来契約を保持。BGRAは既存vkCmdClearColorImageと同じpackを使い、prepareはtargetを変更せず、GPUの完了/retirement契約も維持。
対象vulkan-device-host PASS（初回は旧state.load==1の固定期待で失敗、全面clearの期待のみ修正。partial矩形の既存確認と全面時CPU未変更の実executor確認を実施）。production style0、全文manual review/diff-check PASS、clang-format19比較は独立引数/宣言行の全文規約を優先。arm64 build exit0/warning0/error0/checkerPASS。kernel2480702249/8077312、sha256 aaaaf3fca26c48b623e51ab33c8a984671cbcdb15b25768fd7fbdcfd2f92563d。実機効果は配置後に確認する。


全面clear e6914742dをmain統合、旧4190670006/8077312を /var/backups/vmunix.before-native-clear-20261011へ保存、新2480702249/8077312を全体cmp/sync付きで配置。reboot後SSH復帰し通常sessionは継続。offscreen4条件の読み戻しは前回と同じ期待RGBA全件一致。compose0.8〜3.8秒は残り、clear変更だけの実測改善とはしない。250ms以上のnative submissionにprepare/execute/release/buffer-copyのwall時間内訳を1行記録する診断を追加。公開API/動作順序/timeout変更なし。対象device host PASS、production style0/全文manual/diff-check PASS、clang-format19比較は全文規約優先、arm64 build warning0/checkerPASS。診断kernel1993742562/8077312を次に配置する。


ユーザー「初期化時の3000msのウェイトも短くしてほしい」に従い、診断撮影用stage pauseのみ3000→100msへ短縮。HW固有のsettling/ready/timeout値は変更しない。stage-host限定PASS、C全文review/style0/diff-check PASS、arm64 warning0/checkerPASS。kernel994709821/8077312。直前の355d15c26診断kernel1993742562はFATへ配置・照合済みだが再起動前だったため、短縮版を配置して一度だけ再起動する。旧動作版2480702249は /var/backups/vmunix.before-submit-timing-20261011に保全済み。


9f3d3d14a短縮版994709821/8077312をFATへ配置/cmp/syncしreboot、SSH復帰と通常Waylandを確認。100msのstage pauseで初期化成立。代表submissionは461〜548ms（prepare100〜133、execute304〜306、release55〜113）、操作負荷中は1.2〜3.2秒。small64x64 offscreen alpha0.5は期待58,115,77,255、250ms以上のsubmission診断は新たに出ず。
次の診断はCLのbin/render/cache-cleanのwall時間と、CPUtarget validation/clear時間を区別し、IRQ到着tickからworkerがcompletionを消費するまで20ms以上の遅れを記録。IRQguard内はtick記録だけ、待機/allocateなし。イベント優先/retirement/timeout制御には時刻を使わない。既存hardware/device限定host PASS、production style0/diff-check/C全文manual/clang-format19比較、arm64 warning0/checkerPASS。kernel2309215655/8077312。実機へ配置して原因を絞る。submit-i01はin-progress。


2309215655/8077312を実機へ全体cmp/sync付きで配置し、reboot後SSHと通常sessionを確認。CL代表値bin1〜2ms/render304〜1051ms/clean0、target clear slowとIRQ-to-worker20ms以上のdiagnosticは無し。prepare/releaseは負荷中0.5〜1秒だがrender側にも明確な遅延がある。

共通compilerのif-conversionはpanel.fragの全branchを算術評価してSELECTで選び、文字/画像でもglassと5tap blurを実行する実codeを確認。ユーザーのGPU応答修復scope内で、compositorの同じGLSLを7個のimmutable modeへpreprocessする必要補完を実装。既存generic fallback、dark glass mode -1、premultiplied blend、descriptor/push APIは保持。7pipelineのpartial-open cleanupと縮小varying契約を確認し、source-vs-specializedの数値比較と実native compiler検証、arm64 compositor build後にSSH更新して計測する。shared master/queueは変更せず、submit-i01はin-progress。


2026-10-11 panel mode分割: 7生成SPIR-Vとgenericはglslc/vulkan1.0 validator PASS。固定Mesa independent decoder/native compilerに7vertex/coordinate/fragment pairを渡すhost PASS。32inputs/modeのgeneric sourceとspecialized source出力一致（light/dark glass含む）、texture無し3mode/1sample3mode/blur5samplesを確認。縮小varyingをnative FIFOへ詰め、実compiled sourceとのscalar差分も一致。既存blend/swizzle試験もPASS。新pipeline部分のownership/error cleanupとABI不変をC全文manual確認、clang-format19範囲比較は定義の独立引数/blank/tabs規約を優先。arm64 compositor make（既存sysroot完了stampを-oで維持、toolchain再build無し）exit0/warning0/error0、dynamic ELF dependency checker PASS。wayland cksum2734866792/949352。実機へ未配置、速度改善はまだ未確認。


5243251c9をmain自動統合し、旧コンポジタ259030305/936624をUFS /var/backups/wayland.before-panel-modes-20261011へ保存。service stop greeterはSSHが閉じたが再接続でき、2734866792/949352をstage/cmp/rename/sync/cmpで/bin/waylandへ配置しservice start greeter成功。通常session/Files/Settings起動を確認。GPUexecute代表48〜49ms、操作負荷137〜177msへ短縮。一方prepare190〜457ms/release108〜1016msが残り、全体改善の完了とはしない。

MMU/cacheのself-clearは32readで未完了だと10us sleepに入るが、schedulerはtickへ丸める。IRQguard外の有限fast burstのみ1024readへ拡張して短い命令を待つ。既存yield/異常timeout/retirementは保持し、serialized hardware counterで実yield数を記録して効果を区別する。hostの80/96read遅延モデルとstuck timeoutを確認し、実機比較する。

2026-10-11 bounded maintenance burst: hardware host PASS（80/96readの短いMMUC/TLB busyをyieldなしで処理、stuck10000yieldの既存拒否/retentionも維持）。device host PASS、C全文manual/style0/diff-check PASS、clang-format19出力比較。arm64 kernel make exit0/warning0/error0/checker3PASS。cksum1502649265/8077312。変更はprivate hardware counter/finite burstでHAL API不変、実機効果は未確認。


ca30cbc4dの1502649265/8077312をFATへ全体cmp/sync付き配置、旧2309215655はUFS /var/backups/vmunix.before-maintenance-20261011。rebootからSSHが自動復帰、通常session/Files/Settingsを確認。maintenance-yields=0でprepare186〜448ms/release81〜726msは残る。スリープ回数を増やした修正とはせず、CPU側の多重upload/mapを減らす。

同pass内のearlier drawが持つ、exact compiled program pointerのcode3個と常に同じdefault256bytesを借用する。uniform/push/texture-sampler/fetch/shader recordはdraw別を保持し、元drawと借用drawはwhole-pass DMA retirementまで保持。別pass/別compiled objectでは共有しない。借用はowner一覧/予算へ二重計上せず、失敗時は自分の取得prefixだけ巻戻す。既存texel共有と同じprivateなlifetime境界、HAL/user/API変更無し。

2026-10-11 pass内code/default共有: 実device host PASS。2drawで既存texture+code3+defaults1の5page/owner削減、code/default同一owner、別compiled identityは同じ命令列でも独立upload、borrow後budget/late-OOMと退避/二重free無しを確認。uniform/vertex snapshotの既存確認は維持。C全文manualでprepared pipeline lifetime/whole-pass retention/partial cleanup/budgetを確認、production style0/diff-check PASS、clang-format19範囲比較は全文規約優先。arm64 kernel exit0/warning0/error0/checker3PASS。実機比較は配置後。

次の必要補完: native passのprepareはDMA開始前、releaseはwhole-pass DMA終了後にcontroller mutex内で行い、全inputごとのMMUC/TLB flushを一回にまとめられる。privateなbegin/end区間でPTE更新範囲を集計し、prepare end成功前はjobをlaunchしない。release中のzero-ref view/physical backingはend flush成功まで保持し、失敗ならquarantineへ残す。通常の単発map/releaseは従来の即時flush。resource/native/HAL公開APIは不変。nested/active-DMA batchを拒否し、限定hardware/device hostでpublication順序・実所有・失敗保持を確認して実機測定する。

2026-10-11 translation batch最終source: hardware host PASS（2mapのflush4trace一回/dirty12byte、2retirementのphysical holdをend成功まで維持、nested/active-DMA拒否、empty/double end、prepare/retirement flush失敗のRAM保持とchecked reset/recover）。device host PASS（未完batchはCPU clear/native launchを拒否、実FIFO job/budget/late-OOM/uncertain DMA retentionの既存確認も維持）。C全文manualでcaller mutex/no-DMA間隔、PTE dirty範囲、map publication、zero-reference backing、single batch consumption、failed flush quarantine、ordinary immediate resource操作の契約を確認。production style0/diff-check PASS、clang-format19比較は規約の定義/引数/paragraph形式を優先。最終arm64 kernel build exit0/warning0/error0/checker3PASS、cksum1299371254/8077312。

共有code版07b549f07の1591733483/8077312をFATへ全体cmp/sync付き配置（旧1502649265はUFS /var/backups/vmunix.before-shared-code-20261011）しreboot exit0。その後SSH timeout/no-routeが続いたためユーザーへ電源OFF/ONを依頼。最新translation batch kernel1299371254は未転送・未起動。物理shared-code効果/新batchの性能は未確認で、再起動失敗の原因も未判定。SSH復帰後にscreen/ログを確認し、転送・全体照合・rebootと測定を再開する。submit-i01/p006はin-progress、WS incompleteを保持。


2026-10-11 user再起動後の観測: cursor移動は約100ms。追加受け入れ目標はマウス移動33ms以内（30fps相当）、16ms（60fps相当）はstretch。既存submit-i01の応答修復をこの測定条件で継続する。shared-code kernelのSSH再確認は10.0.30.2のARP応答無し/No route to host、現在IPをuserへ確認中。translation-batch kernel1299371254/8077312はまだ未転送。

source調査: compose_regionはbuffer ageを含むdamageを正しく蓄積してscissorへ渡すが、compose_recordのrenderAreaは常にwhole output。LOAD passの部分更新でもV3D RCLが全画面tile load/storeを行う。regionが存在する時だけrenderAreaにも同じ矩形を設定する。whole frame/CLEAR、viewport、buffer age、exact scissor、LOAD保持は維持し、既存native passのpartial tile保存契約を使う。新API/HAL責務変更無し。実機効果は配置後に確認する。


2026-10-11 damage renderArea補完: whole/CLEAR passは従来全域、partial LOAD passだけbuffer-ageを含むregionをrenderArea/scissor双方へ渡す。全source周辺manual reviewで変更範囲/LOAD既存画素/viewport不変を確認、style変更箇所0（compose.c既存blank-after-brace4件は範囲外）、clang-format19範囲比較/diff-check PASS。既存native-pass fixed-Mesa XML oracle PASS（7 complete streams、partial/grouped tile geometry・draw preservation）、arm64 compositor build exit0/warning0/error0、dynamic ELF dependency checker exit0。cksum2528461358/949352。性能/描画の実機確認は未実施、SSH接続の現在IP回答待ち。33ms目標は未達のまま、submit-i01 in-progress。


cursor計測準備: 既存の明示 --log-frames 診断へaccepted pointer座標/monotonic usを追加し、draw行も座標を持たせる。最後に採用した入力→draw→submit→shownを同じframe/座標で照合できる。通常運用のログ/動作/フレーム順序は不変。これはcompositor採用後のsoftware時間であり、USB polling以前や画面の走査位置までの時間を含まない。33ms cursor latencyの達成と単なるframe平均fpsを区別する。


2026-10-11 cursor採用timestamp診断: --log-frames時のみpointer/drawの座標とmonotonic timestampを出し、既存submit/shownのframeへ対応可能。診断offでは追加の時計読取/log無し。C全文manualで単一event-loop ownership/動作不変/既存公開診断を確認、変更範囲style0（compose.c既存4件は範囲外）、clang-format19範囲比較/diff-check PASS。arm64 compositor最終build exit0/warning0/error0、dynamic ELF dependency checker exit0。cksum4130193432/949384。No route to host/ARP FAILEDが継続、現在IPのuser回答待ち。kernel1299371254/8077312とこのcompositorは未配置・未起動、33ms/16ms性能は未確認。main自動統合のみ実施、push/GitHub publication無し。p006 submit-i01 in-progress/WS incompleteを保持。


2026-10-11 SSH復帰: 実機ファイルのkernel1299371254/8077312、wayland4130193432/949384が最新成果物と一致（ユーザー側image再作成済みの状態）。追加転送はせず、既存session scriptをUFSへ保存して --log-framesを有効化、greeterを停止/再起動。userがdesktop/窓内cursorを移動。26 complete samplesでaccepted→shown median1084ms/min461/max2367、partial0。inputごとのprintfは1000行以上になり計測負荷が大きいため、latest accepted timestampをevent-loop fieldでcoalesceし既存draw行に一度だけ出す。通常運用では時計読取無し。drawにdirty/damage/client cursor id/opacityを添えて全域無効化の原因を絞る。kernelの既存prepare内訳は33ms以上を最大1秒に一度出す（failureは常時）、private/controller mutexの時刻だけで制御。公開API/HAL変更無し。


coalesced診断最終確認: native device host PASS、C全文manual/diff-check/style変更範囲0（compose既存blank4は範囲外）、clang-format19範囲比較。初回kernel buildはengine forward declarationのままhardware時刻に触れincomplete-typeで失敗、private headerを明示して修正。最終arm64 kernel/app build exit0/warning0/error0、kernel checker3PASS/dynamic app checker exit0。kernel260505095/8077312、wayland459945060/949352。両方を実機/tmpへ転送exit0、配置前の旧kernel/appをUFSへ保存して更新・rebootする。33ms目標は未達でsubmit-i01継続。


702a1dca7をmainへ自動統合。旧kernel1299371254/app4130193432を実機UFS /var/backups/vmunix.before-cursor-prepare-20261011 とwayland.before-coalesced-trace-20261011へ保存・全体cmpし、greeter停止後に新kernel260505095/app459945060をstage/cmp/sync/rename/sync/cmpで配置。全体照合exit0。rebootを別のSSHで実行、復帰確認と新しいログによる全域無効化の切り分けを続ける。


診断版reboot exit0、約1分後にSSH/ARP/ping復帰（ping0.923ms）。kernel260505095/app459945060のinstalled cksum一致。実pass96draw/29,941,760bytesでprepare197〜369ms、texture169〜315ms、その内copy154〜273ms。code19〜33/records7〜19ms。Raster→UIF変換の16byte utile rowごとに可変長kern_memcpyを呼んでおり、数百万の小コピーに一般引数検証/word loopを繰り返す。4pixel完全行は固定16byteの明示compiler builtinへ置換し、境界1〜3pixelは従来exact-length copyを保持。kernel -mgeneral-regs-only/-fno-builtinのまま、明示builtinの数値コピーだけを使う。unaligned input/padding/byte order/UIF layout/所有/キャッシュ順序は不変。既存native-state independent pixel oracleとARM64 machine codeで実長16byte/尾の正確なaccessを確認し、実機測定する。


2026-10-11 lightweight mouse再計測: user「やりました。目視では1000msくらい」。40 complete input samplesはaccepted→shown min845/median1035/max1149ms（build/ws141-coalesced-full-trace.log）。dirty0/damage1の連続frameもあるがKWL DAMAGE部分passは出ていない。native texture固定16byte化はindependent native-state host PASS、style0/diff-check/C全文manual PASS。ARM64同一flagsの-Sで完全utile行はldp/stp各16byte、端だけkern_memcpyを確認、SIMDなし。arm64 kernel warning0/error0/checker3PASS、cksum666078535/8077312。実機配置前、性能効果未確認。


訂正: lightweight full traceにはKWL DAMAGE frame31〜46等が存在し、clean/damageのcursor framesは部分passを使っていた（以前のtailだけの調査では見落とし）。部分passでも画像全体のnative snapshotが必要なdrawを96個recordしている。必要補完として、glass_shape_drawとcompose_quad_partで既存のhead/layer変換後のquadが現在scissorから完全に離れている時だけrecordを省く。border1pixelを保守的に残し、shapeの負extent/NaNは従来GPUへ任せる。重なるquadのUV/blend/geometry・描画順・render pass/loadは変えず、公開GPU/HAL API無し。scissor変更を追跡する既存scissor_nowを使う。実機でdraw/bytesと応答を比較する。

clip周辺reviewでoffscreen backdropはlogical output座標を小viewportへscaleし、head wallpaperはscissor_now更新前だったと確認。backdrop_recording private flagで全small passのcullを無効にし、output再開だけ有効に戻す。headのscissor_nowを実vkCmdSetScissor直後へ先に更新。offscreen scene/blur・拡張headの描画を保つ。新HAL/UAPI変更なし。


2026-10-11 fixed utile copy 10a543fd3をmainへ自動統合、666078535/8077312を実機/tmp転送。旧260505095を/var/backups/vmunix.before-texture-inline-20261011へ保存/cmpし、greeter停止後FAT stage/cmp/sync/rename/sync/cmpで更新exit0。reboot exit0だがSSH timeout→No route to host。userへ電源OFF/ON依頼、復帰・実機効果未確認。

clipped quad最終source: glass/output image quadsのpure vertex/fragment描画のみ省き、intersect/境界1pixel/負scaleは保持。current scissor trackingはoutput/head/temporary shell clip/restoreをsource全件確認、offscreen backdropは専用flagで省略を止める。C全文manual、formatter19範囲出力比較、diff-check PASS、style変更行0（compose既存blank4は対象外）。arm64 compositor build exit0/warning0/error0/dynamic ELF checker exit0、cksum1980286597/949392。実機未転送。33ms目標未達のまま。


2026-10-11 再作成imageの実機: user電源ON後SSH復帰、host key再登録（image更新の既存承認）。installed kernel666078535/8077312、wayland1980286597/949392はinline-copy/cull最終成果と一致。root UFS使用51%、/tmp8KB、FAT84%で容量不足ではない。greeter停止後SSH応答0.25秒。userによる「DHCP/sshd起動後、Wayland起動でping停止」の事実を保持し、焼直しが必要だった原因はまだ未確定。

sourceでRPi text ops suspend/resumeがno-op、early consoleのsuspend関数は未接続と確認。newline scrollはIRQをmaskして2000cellを描画し、cellごとに16rowのcache clean/barrierを行う。GPU所有中のログで長いIRQ停止になる仮説を検証する。既存kern_text_suspend/resumeをdisplay leaseのclaim成功/console restore成功へ接続し、board private cons headerで既存HAL実装のsuspend/resumeをRPi text opsへ配線。公開hal.h/API・common GPU API不変。failed restoreはsuspendedを保持、成功release/closeだけresume、新claimのrestoreは途中resumeしない。resumeはretained cellsを保持して復描画し、同状態resumeはno-op。短い実display callback hostでclaim/failed release/close/recovery順を検証しwarning0 build、実機比較する。33ms未達、submit-i01はin-progress。


console lease最終source: actual display-device host PASS、failed release/close/reclaimとsuccessful release/closeのtext state検証。C全文manualでmutex→text observer→console output lockの順序/IRQ復帰/retained state、no rendering while graphics-owned、HAL内部headerのみを確認。clang-format19範囲比較/production変更行style0/diff-check PASS（cons既存style6件は変更なし）。arm64 make exit0/warning0/error0/checker3PASS、kernel959624230/8077312。旧image初期kernel666078535へ戻せるbackupをUFSへ保存し、修正版の配置/比較へ進む。


24c26a5d4をmainへ自動統合、kernel959624230を実機FATへstage/cmp/sync/rename/sync/cmpで配置。旧kernel666078535はUFS /var/backups/vmunix.before-console-lease-20261011、rc.conf/sessionも同目录backup。今回だけgreeter自動起動を停止してreboot、SSH復帰とnewkernel一致を確認。その後rc.confを元ファイルへcmp付き復帰し、--log-frames付きsessionでservice start greeter。Wayland起動後SSH応答0.72秒、user「ウィンドウ内は300msくらい」。長いIRQ-off console repaintは所有中なくなったが、full frame97draw/29,982,720bytes、prepare約290ms/execute175ms/release300msは残り33ms未達。

次の有限最適化: 標準Vulkan bufferを用いcursor抜きのsceneを保管。normal scene pass後、cursor描画前のregionをImageToBufferへcopyし、同sceneのcursor-only部分frameではBufferToImageでbuffer-ageを含むdamageを復元してcursorだけ描く。既存damage history/partial LOAD/shot/head copyは維持、scene commitまたはdirtyは通常再合成。cachevalidはfence成功時のみ、failed recording/submit/fenceとoutput closeで無効化。transfer SRC/DST非対応またはallocation/load-pass不可なら既存経路、multiheadは通常再合成。新HAL/UAPI/OS境界なし。desktopのstill領域（bar/edge/影を除く）もcursor damage可能にする。実command recording hostのbarrier順/region row stride/old cursor消去/失敗cleanupとactual transferの既存限定試験、warning0 build、実機のsoftware時間で検証。公開ポリシー変更ではなく承認されたcursor応答修復内の実装判断。


cursor-free scene最終source: scene.c/scene.hを三OSのcompositor一覧へ登録。標準transfer buffer・cache geometry/valid/changed/writtenのprivate stateだけ、image transfer usage不足/optional allocation不足は従来renderへ戻る。acquire semaphore waitにTRANSFERも含め、同queueのbuffer read/writeとPRESENT→TRANSFER→PRESENTをbarrierで順序付け。scene変更は全surface commit/unknown dirtyで再合成、captureはcursor前、validはfence成功でのみ公開、closeはidle後destroy。buffer-age矩形をそのままbuffer full-width strideで復元、old cursor領域を保持。multiheadは通常scene描画/shotとheadcopyを維持。still desktopはbar/16px左右edge/48px下edgeを除きcursor damage、decoration/gesture/scene変更は既存dirty経路。

production scene recording host PASS（actual commandのfull/partial capture・old cursor消去・unselected pixels・RAW channel・partial client更新・barrier/layout/access/stride・memory/bind failure cleanup）。system VulkanでC89 compile PASS、repo Vulkan headerは既存C++ commentのため-std=c89を拒否し-std=c99で実productionを再compileしてPASS、own追加CはC89形式。actual Vulkan device/ordered buffer/image transfers host PASS。C全文manual（cache init/publication/invalidation/lifetime、swapchain/reacquire/acquire stage、existing backdrop pass compatibility/viewport/bind persistence/scene composition/client commit/head callbacks）、formatter19範囲比較/diff-check。scene/changed production style0、compose既存4/shell既存5は対象外。arm64 app make exit0/warning0/error0/dynamic ELF checker exit0、wayland886953165/953832。実機更新/33ms確認は次、submit-i01 in-progress。
