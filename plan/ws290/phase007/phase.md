<!-- awesome-plan project=zedbsd record=ws290-p007 -->

# ws290-p007 executor の土台

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p007（版 4.5、ghci.md cb937e6）。
- 状態: in-progress（実装・host 試験・build は済み。P3 の review と commit 待ち、2026-10-28 09:30）
- 依存: p005・p006

## 範囲

executor の土台: vk.h・stream・node・object・reply・vk-caps・device・format（FMT）・memory（heap）・command・queue（空の submit、fence・semaphore・event）。libvulkan が送る op の全部（`userland/desktop/libvulkan/` の `GPU_OP_*`、1.1 の `*2`・`EXTERNAL_*`・`GET_DEVICE_QUEUE2`・`RECORD_QUEUE_TIMELINE` を含む）の表を作り、op ごとに答える・記録する・断るを決める（R21）。session の番号は `GPU_OP_CREATE_DEVICE` で取る（R16）。+ `CAPSET | COMMAND | NOTIFICATION | FENCE | JOB | JOB_CAPACITY` と recovery

詳細は design の該当の節（§4〜§16）と §14 の p007 の場面。

## 受け入れ

- p007 の場面 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 30 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

- design §19: 仕様（d9252e4）の残りは無い。この Phase の code に効く決まった事は N75（A8B8G8R8 51・52・55・56・57 を 37・38・41・42・43 と同じに受ける。B4G4R4A4・A2B10G10R10_UINT・E5B9G9R9 は答えない）と N74（slot は stage の和で 16、set は 96）。どちらも `vk/vk-format.c`・`vk/vk-caps.c` に入れた。
- design 版 4.10（p006 の review の取り決め）: `maxTexelBufferElements` = 4,194,304、image は全部 linear（STORAGE は linear 必須）、shader が触る物は 4 GiB 未満の VA。p007 の code はこの通り。

## op の表（R21）

libvulkan が送る op（`userland/desktop/libvulkan/` が名指す `GPU_OP_*` の全部）の扱い。「答える」は driver が答えを作る、「記録する」は object・記録を kernel が持つ、「断る」は VkResult で断る（frame は続く）か frame を止める（ENOTSUP・EINVAL、libvulkan は session を DEVICE_LOST にする）。p007 の列は今の code、最終の列は design の Phase。

| op（番号） | p007 の扱い | 最終（Phase） |
| --- | --- | --- |
| SET_REPLY_STREAM 178・SEEK_REPLY_STREAM 179・EXECUTE_STREAMS 180 | transport（`vk-stream.c`）。180 は 1 本・依存無し・flags 0 の形だけ（他は ENOTSUP）、blob を写してから実行、深さ 4・64 MiB まで | 同じ |
| ENUMERATE_INSTANCE_VERSION 137 | 答える: frame の trailer（result 0、u64 1、版 1.1 = 0x401000 を release で最後に） | 同じ |
| CREATE_INSTANCE 0・DESTROY_INSTANCE 1 | 答える（何も持たない） | 同じ |
| ENUMERATE_PHYSICAL_DEVICES 2 | 答える（1 個、識別子は libvulkan の物） | 同じ |
| GET_PHYSICAL_DEVICE_FEATURES 3・PROPERTIES 6・QUEUE_FAMILY_PROPERTIES 7・MEMORY_PROPERTIES 8 | 答える（`vk-caps.c`、`struct ghci_caps` から） | 同じ（p011 で discrete の型 2 つ） |
| GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES 4・IMAGE_FORMAT_PROPERTIES 5 | 答える（FMT か §8.5.1 の表、`vk-format.c`） | 同じ |
| GET_PHYSICAL_DEVICE_PROPERTIES2 148 | 答える: ID properties だけ（device UUID = "ghci"・PCI の ID・GHCI の版、driver UUID 固定、LUID 無し）。他の chain は ENOTSUP | 同じ |
| GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES2 150 | 答える: external memory の機能は 0、image の上限は 5 と同じ | 同じ |
| GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES 161 | 答える: external memory の機能は 0 | 同じ |
| GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES 33 | 答える: 0 個 | 同じ |
| CREATE_DEVICE 11 | 記録する: GHCI session を `ghci_vm_open` で取る（R16）。queue は family 0 に 1 本、feature の超過は VK_ERROR_FEATURE_NOT_PRESENT、16 個目は VK_ERROR_TOO_MANY_OBJECTS（U11）、CP_HEAP の上限は VK_ERROR_OUT_OF_DEVICE_MEMORY | 同じ |
| DESTROY_DEVICE 12 | 記録する: device の object・heap を返し、session を close | 同じ |
| GET_DEVICE_QUEUE2 155（+ RECORD_QUEUE_TIMELINE 1000384005） | 記録する: queue と timeline（1〜63）、timeline の job は device の submit に従う。timeline の record の無い形は EINVAL | 同じ |
| ALLOCATE_MEMORY 21（+ RECORD_IMPORT_RESOURCE 1000384002、EXPORT_MEMORY_ALLOCATE_INFO） | 記録する: host memory を確保（import は session の resource の allocation）し device の session に VM_MAP。型 0 だけ、256 MiB まで。import の id が無ければ VK_ERROR_INVALID_EXTERNAL_HANDLE | p011 で local（型 2 つ） |
| FREE_MEMORY 22 | 記録する: VM_UNMAP、物理は unmap の fence の後 | 同じ |
| GET_DEVICE_MEMORY_COMMITMENT 27 | 答える: 全部 | 同じ |
| CREATE_BUFFER 50・DESTROY_BUFFER 51・GET_BUFFER_MEMORY_REQUIREMENTS 30・BIND_BUFFER_MEMORY 28 | 記録する: size は 4 byte 単位、alignment 256、型 bit 1。bind は 1 回・256 揃え・memory の中、違反は VK_ERROR_OUT_OF_DEVICE_MEMORY。sparse の flag は FEATURE_NOT_PRESENT | 同じ |
| CREATE_IMAGE 54・DESTROY_IMAGE 55・GET_IMAGE_MEMORY_REQUIREMENTS 31・BIND_IMAGE_MEMORY 29・GET_IMAGE_SUBRESOURCE_LAYOUT 56 | 記録する: 5 と同じ判定（VK_ERROR_FORMAT_NOT_SUPPORTED）、linear の配置（`layout.h`、§8.5.2: 行・面・layer を 64 byte に揃える）、256 MiB を超えれば OUT_OF_DEVICE_MEMORY | p008〜p009 で tiled を足すかもしれない |
| GET_IMAGE_SPARSE_MEMORY_REQUIREMENTS 32 | 答える: 0 個 | 同じ |
| CREATE_FENCE 35・DESTROY_FENCE 36・RESET_FENCES 37・GET_FENCE_STATUS 38 | 記録する（host 側）: SIGNALED の作成、submit の ticket で SUCCESS・NOT_READY・reset で終わった ticket は DEVICE_LOST | 同じ |
| CREATE_SEMAPHORE 40・DESTROY_SEMAPHORE 41 | 記録する: 1 本の ring の順で足りるので待ちは置かない | 同じ |
| CREATE_EVENT 42・DESTROY_EVENT 43・GET_EVENT_STATUS 44・SET_EVENT 45・RESET_EVENT 46 | 記録する: executor heap の 64 bit の語、host が読み書き | p009 で GPU の SetEvent・WaitEvents |
| QUEUE_SUBMIT 18 | 記録する: 空（記録の無い command buffer だけ、または submit 0 個）は device の session に BIND_SESSION・FENCE・BIND_SESSION(0) だけを置き、fence・job・frame はその ticket を待つ。記録の有る command buffer は ENOTSUP（frame を止める）。終わっていない command buffer・他の device の物は EINVAL。lost は VK_ERROR_DEVICE_LOST | p009 で記録を `CMD_*` に |
| CREATE_COMMAND_POOL 85・DESTROY_COMMAND_POOL 86・RESET_COMMAND_POOL 87 | 記録する（family 0 だけ、他は INITIALIZATION_FAILED） | 同じ |
| ALLOCATE_COMMAND_BUFFERS 88・FREE_COMMAND_BUFFERS 89・BEGIN_COMMAND_BUFFER 90・END_COMMAND_BUFFER 91・RESET_COMMAND_BUFFER 92 | 記録する: begin で記録を空にし、end で EXECUTABLE。記録が 16 MiB を超えれば end が OUT_OF_HOST_MEMORY | 同じ |
| CMD_* 93〜136 | 記録する: codec で引数を読み（形の違う物は EINVAL）、wire の形のまま command buffer に積む。実行しない | p009（実行）・p008（pipeline・descriptor の object） |
| CREATE_SAMPLER 70・CREATE_IMAGE_VIEW 57・CREATE_BUFFER_VIEW 52・CREATE_SHADER_MODULE 59・CREATE_PIPELINE_CACHE 61・GET_PIPELINE_CACHE_DATA 63・MERGE_PIPELINE_CACHES 64・CREATE_GRAPHICS_PIPELINES 65・CREATE_COMPUTE_PIPELINES 66・CREATE_PIPELINE_LAYOUT 68・CREATE_DESCRIPTOR_SET_LAYOUT 72・CREATE_DESCRIPTOR_POOL 74・RESET_DESCRIPTOR_POOL 76・ALLOCATE_DESCRIPTOR_SETS 77・FREE_DESCRIPTOR_SETS 78・UPDATE_DESCRIPTOR_SETS 79 と各 DESTROY | 断る（route が無い: ENOTSUP、frame を止め log） | p008 で記録する（pipeline は compile、不足は U8 の VkResult） |
| CREATE_RENDER_PASS 82・CREATE_FRAMEBUFFER 80・GET_RENDER_AREA_GRANULARITY 84 と各 DESTROY | 断る（ENOTSUP） | p009 |
| CREATE_QUERY_POOL 47・DESTROY_QUERY_POOL 48・GET_QUERY_POOL_RESULTS 49 | 断る（ENOTSUP） | p009 |
| QUEUE_BIND_SPARSE 34 | 断る（ENOTSUP、queue family に SPARSE が無いので正しい client は送らない） | 同じ |
| ビデオの op（0x10000〜0x1000d: GET_PHYSICAL_DEVICE_VIDEO_CAPABILITIES・VIDEO_FORMAT_PROPERTIES・QUEUE_FAMILY_VIDEO_PROPERTIES・CREATE/DESTROY_VIDEO_SESSION・GET_VIDEO_SESSION_MEMORY_REQUIREMENTS・BIND_VIDEO_SESSION_MEMORY・CREATE/UPDATE/DESTROY_VIDEO_SESSION_PARAMETERS・CMD_BEGIN/END/CONTROL_VIDEO_CODING・CMD_DECODE_VIDEO） | 断る（ENOTSUP）。capset の native の語が 0（video の queue 無し）なので libvulkan は送らない | 同じ |
| QUEUE_WAIT_IDLE 19・DEVICE_WAIT_IDLE 20・WAIT_FOR_FENCES 39・MAP_MEMORY 23 ほか libvulkan が送らない物 | 断る（ENOTSUP） | 同じ |

node の ops: `get_capset`（id 4・版 0・176 byte）、`command`（同期の frame）、`commands`（submit: frame を同期で実行し、completion を session の最後の ticket（timeline 付きの空の fence frame はその queue の device の最後の ticket）の後に service thread が完了。drain: session が自分の completion を ticket の答えまで退ける）、`jobs`（reserve・commit・cancel・capacity、marker は reserve の timeline の device の最後の ticket に結ぶ）、`recovery`（下）。capability は `CAPSET | COMMAND | NOTIFICATION | FENCE | JOB | JOB_CAPACITY`。FENCE は core の fence の fd で、`gpu_ops_validate` が backend に何も求めない（job の完了から core が signal する）ので出す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 09:30 P3 の手伝い（q1644-i01、ユーザー 2026-10-28「P3は仮の扱いで実装を行ってよいです」）: 実装と試験。commit は P3。
  - **code（vk/ は書き直し）**: `vk/vk.h`（型）、`vk-node.c`（attach・capset・command・submit・drain・jobs・recovery・detach）、`vk-stream.c`（transport）、`vk-dispatch.c`（route の表）、`vk-object.c`（session・object の表・destroy・arena）、`vk-reply.c`（create・destroy の形、VkResult、float の語を整数で作る）、`vk-caps.c`（feature・limit・queue・memory・format・image の上限・不足の log、§15）、`vk-format.c`（§8.5.1 の表、FMT の読み、FMT の bit → Vulkan）、`vk-device.c`、`vk-memory.c`（memory・buffer・image・executor heap）、`vk-command.c`（pool・buffer・記録）、`vk-queue.c`（submit・fence・semaphore・event・completion の退け・drain）。core: `ghci.h`（`GHCI_VM_ISOLATED`・`GHCI_MAPPING_HELD`・`ghci_vm.closed`、`struct ghci_executor_ops`、core の reset の欄、`ghci_session.vk`）、`ghci.c`（weak の `ghci_vk_attach`、session の open・close、VkDeviceMemory の blob）、`vm.c`（isolate）、`recover.c`（core の fault・reset、service の completion）、`cp.c`（`ghci_cp_ticket_locked`）、`jobs.c`・`ghci-words.h`（marker を 64 bit の ticket に、judge で退ける）、`layout.h`（GHCI 1.0 の linear: 行を 64 byte に揃える）、`platform/amd64/vmunix.mk`・`platform/riscv64/vmunix.mk` の GHCI の一覧（jobs・capset・vk/ の 11 file と `i915/vk/vk-codec.c`（他の driver と重ならないよう filter-out））。
  - **p005 の review の制約**: (M6) isolate は DEAD と別の印 `GHCI_VM_ISOLATED`: unmap は mapping を HELD にして何も置かず、close は `closed` にして番号・chunk・mapping を device に残し、次に終わった reset（driver の回復か core の reset）の `ghci_vm_after_reset` で closed は退役、未 close は DEAD になる。(L2) core の reset は submit_log を最初に空にしてから reset する（LOST の間の close が vm を退役させても log を読まない）。
  - **試験**: `plan/ws290/tests/ghci-vk-host-test.c`（新、`ghci-host-test.sh` の末尾の判定の前に節を足した）: capset の byte、登録（capability、no-su は executor 無し、flat・prot は有り）、profile ごと（kc705-uma・kc705-discrete・minimal・full・flat・prot・no-blt・discrete）の wire での feature・properties（版・PCI の ID・型・limit）・queue family・memory の型、format（full は FMT、minimal は表）、flag ごとの差（INDIRECT_DRAW・DUAL_SOURCE・LOGIC_OP・MRT_BLEND・MSAA・MAX_MRT・LDS_KB・ETC2・SRGB・BC・TEX_FFILT・ROP_PACK・BLT・PROT（差無し）の 14 件、差は試験に手で書いた）、device（15 個と 16 個目の TOO_MANY_OBJECTS、destroy の SESSION_CLOSE と番号の戻り、FEATURE_NOT_PRESENT、flat は packet 無し）、memory（VM_MAP の bus、blob、buffer・image の要件・bind・subresource の配置、import、free の VM_UNMAP）、記録（3 command を wire のまま、形の違う command で frame が止まる、空の submit が fence だけ、記録の有る submit は ENOTSUP）、fence・semaphore・event、job と frame（service が完了、stop_poll の EAGAIN と 0）、recovery（stop、isolate で packet 無し・memory を保つ・他の session は続く、fault で LOST、core の reset で退役・quarantine の返却・log に session が無い）、LOST の間の close と core の reset（L2）。
  - **結果**: `sh plan/ws290/tests/ghci-host-test.sh` rc=0: words 141/0、ref 56/0、model 254/0、driver 587/0、old-names PASS（34 sources）、ghci-isa PASS、gsi1 50/0、**vk 354/0** → PASS ghci290-host-test。build（各 timeout 1800、出力 build/ws290-p007-*）: config-amd64-ghci.mk・config-visionfive2-ghci.mk・config/ci/config-amd64.mk・config/ci/config-visionfive2.mk、全部 rc=0・warning 0（vk/ の .o と vk-codec.o が GHCI の 2 つに入ったことを確かめた）。QEMU・実機は無し。
  - **design と違えた所**（P3 の判断を頼む）: (1) `maxDescriptorSetSampledImages`・`Samplers` は §15.5 の表の 16 でなく §11.4 と §8.6（N74）の 96。(2) `textureCompressionETC2` は FMT0[7] と FMT0[9]（sRGB）の両方で true（ETC2 の sRGB の format が要るため）。`textureCompressionBC` は常に false。(3) GCLK_HZ = 0 の `timestampPeriod` は仕様の公称 100 MHz（10 ns）。(4) memory の heap の大きさは 256 MiB（design に値が無い）。(5) 記録の有る command buffer の submit は p009 まで ENOTSUP（frame を止める）。p008・p009 の object（sampler・view・pipeline・descriptor・render pass・framebuffer・query）の op は route が無く ENOTSUP。(6) 完了（drv_gpu_complete）は service thread のほか、commit で既に終わった marker（ioctl の文脈）と、drain・isolate の退けでも呼ぶ（割り込みの handler では呼ばない）。(7) FMT_BUFFER の bit の読み方: bit5 → VERTEX_BUFFER、bit0 → UNIFORM_TEXEL_BUFFER（圧縮・depth を除く）、bit8 → STORAGE_TEXEL_BUFFER（表で storage の物だけ）。仕様は FMT_BUFFER を「buffer の機能」としか書かない。(8) FMT が有っても driver が descriptor に書けない format（表に無い物）は 0、表に storage の無い format の STORAGE は落とす、BLT = 0 の blit は driver の描画（sampled → BLIT_SRC、colour → BLIT_DST、depth 無し）。(9) TRANSFER_SRC・DST（1.1 の bit）を機能の有る format に付ける。
  - **未実施・残り**: 記録の実行（p009）、pipeline・descriptor（p008）、discrete の memory 型 2 つ（p011）。event の GPU 側は p009。実機・QEMU。規約の全文の見直しは p015。
  - **消してよい古い file**（Q1 へ、P3 の確認の後）: `src/drivers/gpu/ghci/vk/vk-engine.c`・`vk-engine.h`（firmware の relay の frame の完了。新しい完了は vk-queue.c）、`vk-relay.c`（firmware への relay。記録の decode は vk-command.c に移した）、`vk-program.c`（firmware の program の relay）、`vk-session.c`（1.1 の object 表と PROT の grant。vk-object.c に置き換えた）、`vk-node.h`（1.1 の宣言。vk.h に置き換えた）。どれも build に入らず、どこからも include されない。
