<!-- awesome-plan project=zedbsd record=ws113-p003 -->

# ws113-p003: Vulkan Displayの列挙・通知

Parent: [WS113](../ws.md)
Status: in-progress（q850-i01、P2、2026-10-07。実装・build（warning 0）・host 試験まで。QEMU は T1 への依頼を Q1 へ送った（test-wait の番号は Q1 が付ける）、実機の抜き差しは p008）
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: q850 / q850-i01（P2、2026-10-07 ユーザーの N8 の決定でベータ2 へ）
Purpose / goal: libvulkanから標準Display API/拡張でhotplugと複数出力を公開
Prerequisites: p002 cleared/実driverイベント
Investigation bound: 120分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

VK_EXT_display_controlのdevice hotplug fenceとVK_KHR_displayの再列挙、複数surface/swapchain、世代/ACK/lease寿命、切断時の結果を実装。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

独立Vulkan clientで接続/切断通知、再列挙、2出力同時呈示、切断時refusal/回復を確認。標準entry/拡張advertisementは実装済みのみ。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: EXT_display_control全4entryとinstance EXT_display_surface_counter依存、public ABI/provenance/dispatch/広告を一体で扱う。独立native openのidle/0output monitor、fence毎cursor、pending reset no-op、signal保持/fresh再登録、status/wait-any/all/timeout/destroy/teardownと外部payload復帰を設計する。coherent snapshot、固定global plane mapping、旧generation mode/surface拒否。D-ID採択済transport、D-ATOMIC採択時だけ標準present_id/wait等の追加完了契約を実装。

Verification / resume: H04–H10を独立Vulkan clientで確認。別fence/device/processが同じeventを独立受信、lease/swapchain無しでも通知。struct-type enumのみを対応証拠にしない。properties2KHRの既存wrapperとi915 opcode148未実装を区別。strict物理消去を標準present_waitの存在だけで宣言しない。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: local keyをimmutable standard displayNameへ転送、instance handleのname寿命を保持。GPU UUIDのmissing queryは別能力。core display-event waitの切断をSURFACE_LOSTなど非規範resultへ変えない。native capabilityを全EXTentryへ結線。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p003-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p003: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。H04–H10を独立Vulkan clientで確認。別fence/device/processが同じeventを独立受信、lease/swapchain無しでも通知。struct-type enumのみを対応証拠にしない。properties2KHRの既存wrapperとi915 opcode148未実装を区別。strict物理消去を標準present_waitの存在だけで宣言しない。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p003: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 私有Vulkan identity拡張は不採用。standard短portkeyのpolicyと実GPU UUIDqueryを別gateにする。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p003: mainのD-ID A2/旧bootpreferred技術採択messageを受領。local keyをimmutable standard displayNameへ転送、instance handleのname寿命を保持。GPU UUIDのmissing queryは別能力。core display-event waitの切断をSURFACE_LOSTなど非規範resultへ変えない。native capabilityを全EXTentryへ結線。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-05 計画（q702、ベータ2）

入力: [契約の確定](../phase001/contracts-beta2.md) D-EXT・D-ATOMIC（present_wait は足さない）・D-QEMU。

範囲（`userland/desktop/libvulkan/`）: `VK_EXT_display_surface_counter`（instance、`vkGetPhysicalDeviceSurfaceCapabilities2EXT`、counter は 0）と `VK_EXT_display_control`（device、4 entry）を、header（Vulkan-Headers 1.3.269 の宣言）・API-PROVENANCE・`api-commands.tsv`・dispatch・procaddr の gating・extension の bit と一緒に足す。device event の monitor（lease 用と別の native の open、contracts.md §4.1 の per-fence の cursor、idle・0 台でも POLLPRI を見る）、display event（p012 の `GPU_DISPLAY_REFRESH`）、power（p012 の `GPU_DISPLAY_POWER`）。`vkGetPhysicalDeviceDisplayPropertiesKHR` は接続している出力だけを返し、VkDisplayKHR は instance の寿命の間は同じ connector に同じ handle（切断しても再利用しない）。切断した出力の swapchain は `VK_ERROR_SURFACE_LOST_KHR`・`OUT_OF_DATE`、他の出力は続ける。

手順: 1) 宣言と dispatch（`tools/maintain-api.noct`・`maintain-dispatch.noct` で生成）。2) monitor の thread と fence の payload（contracts.md §4.2 の寿命）。3) display の handle の表を connector の slot と generation に結ぶ（wsi.c）。4) host の試験: fence の寿命（H06）、per-fence の cursor（H05 の host の部分）、ABI（LP64）、未 enable の procaddr。5) 独立の Vulkan の試験 program（`userland/tests/` に `display-events`: 登録 → 待つ → 再列挙 → 再登録を log に出す）。

試験: host（上の 4）。QEMU（T1、Venus の `max_outputs=2`）: `display-events` が 2 つの display を列挙し、2 つの display に別々の swapchain で present できること、抜き差しを QEMU で起こせるか確かめる（QMP・monitor で virtio-gpu の出力を無効にする方法を探す。無ければ実機だけ）。実機: HDMI の抜き差しで fence が発火し再列挙の結果が変わる（p008 にまとめてよい）。
受け入れ: 4 entry と surface counter の依存が全部揃ってから広告、host の試験 PASS、QEMU の 2 出力の列挙と present、build warning 0（zedBSD、Linux の libvulkan は対象外）、規約。目安 3h。依存: p002（i915 の inventory）、p012（power・refresh の native。Venus の分も p012）。衝突: WS083（Vulkan Video、libvulkan）の Phase と同じ file に当たるか Q1 が確かめる。

### D-LIMIT の反映（2026-10-05）

native の `ENOSPC`（同時に出せる数の制限）を `display_error()` の既定の `SURFACE_LOST` と分け、display の claim を伴う swapchain の作成では `VK_ERROR_INITIALIZATION_FAILED`（致命的でない）にする。Venus の scanout の数の上限でも同じ。host の試験に ENOSPC の変換の case を足す。

## 実装（2026-10-07、q850-i01、P2）

| 部分 | file |
| --- | --- |
| 宣言 | `include/libc/vulkan/vulkan_display_control.h`（新規、`tools/maintain-display.noct` で Debian の Vulkan-Headers 1.4.309 から 2 拡張の block をそのまま選ぶ）、`vulkan.h` が include、`API-PROVENANCE.md` に出所と license |
| dispatch | `tools/maintain-dispatch.noct`（新しい header を読む、192 → 197）、`dispatch-table.inc`・`api-commands.tsv` を再生成（差分は 5 行の追加だけ） |
| 拡張の bit・列挙・依存 | `internal.h`（`VULKAN_INSTANCE_SURFACE_COUNTER`・`VULKAN_DEVICE_DISPLAY_CONTROL`）、`instance.c`（instance の拡張は常に列挙、KHR_display が要る。device の拡張は renderer の node が `GPU_CAP_DISPLAY`・`_EVENTS`・`_CONTROL` を全部持つ時だけ）、`device.c`（display_control は swapchain と instance の surface_counter が要る） |
| 4 entry と capabilities2 | `wsi-display-control.c`（新規）。thread は作らない: event の fence は登録時の cursor を持ち、`vkGetFenceStatus` の観測ごとに kernel に一度だけ待たずに聞く（wait は既存の 1 ms ごとの観測の経路）。hotplug の cursor は renderer に属す display node の `GPU_DISPLAY_EVENTS` の sequence の和（discovery の open で非破壊の QUERY、ACK しない）。FIRST_PIXEL_OUT は `GPU_DISPLAY_REFRESH` の数（timeout 0）、generation が変われば cursor を取り直す（再接続そのものでは signal しない）、切断中は pending のまま |
| fence | `sync-internal.h`（`struct vulkan_sync` に event の欄）、`sync.c`（status: 未発火の event を観測し latch、未 submit の event fence は native に聞かず NOT_READY。reset: signal 済みの event は SPENT にして再生しない、未発火の reset は監視を続ける） |
| power | `wsi-display.c` の `vulkan_wsi_display_power`（この device の plane の lease の open で `GPU_DISPLAY_POWER`、ESTALE は snapshot を更新して 1 回だけ再試行。lease が無い・EOPNOTSUPP・切断は `VK_ERROR_UNKNOWN`） |
| D-LIMIT | `wsi-display.c` の claim: native の `ENOSPC` を `VK_ERROR_INITIALIZATION_FAILED` に（他は今の `display_error()`） |
| counter | `wsi-swapchain.c`: `VkSwapchainCounterCreateInfoEXT` の 0 でない counter を拒む。`vkGetSwapchainCounterEXT` は `VK_ERROR_OUT_OF_DATE_KHR` |
| node | `wsi-display-nodes.c` の `vulkan_wsi_display_node_topology` |
| 試験 | `plan/ws113/tests/host-display-events.c`・`.sh`（host）、`userland/tests/display-events/`（独立の Vulkan client、`platform/amd64/vmunix.mk` に vkdemo と同じ link の規則）、`plan/ws113/tests/config-amd64-p003.mk`・`display-events-p003.sh`（T1） |
| 文書 | `userland/desktop/libvulkan/README.md` の「Display の制御と抜き差しの通知」 |

設計からの差: contracts.md §4.1 の「library 内の monitor」は thread ではなく、観測の時の fence ごとの比較にした。ACK と POLLPRI を使わないので、他の open・他の process の通知を消費しない（§4.1 の 2・3 の目的は満たす）。compositor が hotplug を待つには、自分の loop で fence の status を見る（p004）。

規格との差（記録）: `vkDisplayPowerControlEXT` の失敗は規格では OUT_OF_HOST_MEMORY だけ。lease の無い display・power の制御の無い出力（i915 の HDMI）・切断は `VK_ERROR_UNKNOWN` を返す。power は swapchain を持つ device だけが変えられる。

## 確認（2026-10-07）

- `sh plan/ws113/tests/host-display-events.sh` PASS（plain と ASan/UBSan: hotplug の fence ごとの cursor（後で登録した fence は前の変化で signal しない）、登録時の sample の失敗の後の cursor、未知の event の拒否、refresh の cursor と次の境界、generation の変化の後の rebase（ESTALE → snapshot の更新 → 新しい cursor → 次の境界で signal）、切断中の pending、数 0 からの最初の境界、他の physical の display の拒否、power の state の変換と未知の state、capabilities2 の写しと counter 0、foreign の record の拒否、counter の読みは OUT_OF_DATE で値を書かない）。
- build（warning 0）: `make BUILD=build/ws113-p003 ZEDBSD_CONFIG=plan/ws014/tests/config-vkdemo-amd64.mk build/ws113-p003/dynamic/libvulkan.so`（ELF の checker が 197 の export を api-commands.tsv と照合）、`ZEDBSD_CONFIG=plan/ws113/tests/config-amd64-p003.mk build/ws113-p003/bin/display-events`。
- style-check: 新規の file（wsi-display-control.c・display-events/main.c・host-display-events.c）は違反 0、変えた既存の file は新しい違反 0。
- 未実施: QEMU（T1: `config-amd64-p003.mk` の image で `display-events-p003.sh`。2 出力は `zdesktop-guest.sh` の `max_outputs=1` が固定なので、`VENUS_OUTPUTS` を足す変更を Q1 に依頼した）、QEMU での抜き差し（QMP に virtio-gpu の出力を切る方法を見つけていない。抜き差しは実機の p008）、実機（5330: eDP の FIRST_PIXEL_OUT、2 つ目の出力の swapchain が `ENOSPC` → INITIALIZATION_FAILED（p011 の前）、HDMI の抜き差しで hotplug の fence が signal し再列挙が変わる。p008 にまとめる）。fence の status・reset の sync.c の経路は host では通していない（QEMU の reset-spent で確かめる）。

## T1-355（2026-10-07、FAIL）の解析

- 1 出力（`t1-355-run1b`）: 最後の行は `power index=0 state=off result=0`。PNG 3 枚とも赤（off.png も）。probe は止まっていない見込み: Venus の guest では 1 frame（acquire・clear・submit・fence の待ち・present）が 50〜100 ms かかり、120 frame で 6〜12 秒、power off がその後で、試験の固定の待ち（2・5・6・9 秒）が早すぎた（off.png は off の前、`cat` は on の前）。直し: 試験は probe の行（present・power off・power on・done）を待ってから撮る（`wait_line`）、frame 60・hold 4。probe と libvulkan は変えない。
- 2 出力（`t1-355-run2`、`VENUS_OUTPUTS=2`）: `displays count=1`。Venus の driver の CONNECTED は host の GET_DISPLAY_INFO の enabled（`venus/display.c` の `display_refresh`）。QEMU の virtio-gpu は head 0 だけを最初から enabled にし、他の head は UI が `ui_info` で大きさを渡した時だけ enabled にする（GTK の tab、D-Bus display の `SetUIInfo`）。今の launcher（`egl-headless` と VNC）は head 1 に `ui_info` を渡さないので、2 つ目は接続していない扱いになる。QEMU の設定の性質で、driver・libvulkan の不具合ではない。
  - 2 出力と抜き差しを QEMU で試すには、launcher に `-display dbus,gl=on`（session bus は `dbus-run-session`）を選べるようにし、`gdbus call … /org/qemu/Display1/Console_1 … SetUIInfo` で head 1 を有効・無効にする（`virtio_gpu_ui_info` が enabled を変え VIRTIO_GPU_EVENT_DISPLAY を出す＝抜き差しの模擬）。screendump は QMP の `head`。p004a の QEMU の試験（scanout 0 → 1）もこれが要る。Q1 に提案（launcher は WS035 の共有の道具）。
