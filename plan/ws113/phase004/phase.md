<!-- awesome-plan project=zedbsd record=ws113-p004 -->

# ws113-p004: compositorの出力・表示モード

Parent: [WS113](../ws.md)
Status: planned
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: none / 実装未承認
Purpose / goal: 全拡張または全mirrorで複数outputを描画
Prerequisites: p003 cleared/Vulkanの実複数出力
Investigation bound: 120分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

出力ごとのcompose/surface/swapchain/present、論理座標、mirror複製、disconnect再構成を実装。GPU操作はlibvulkanのみ。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

1/2 displayの拡張とmirrorが安定、異解像度mirrorも全内容、接続/切断後も残るdisplayで継続。Linux/FreeBSD単一表示を保つ。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: outputごとのcompose/swapchain/render状態とdynamic wl_outputを作る。0接続でもserver/device watcherを保持。extendedはsigned global originとhalf-open境界、mirrorは1論理desktopを各native modeへGPU aspect-fitし黒帯をopaqueに塗る。全接続集合のvalidate/stage/applyとrollback/degradedの実状態を保持する。起動/保存anchorは採択されたpolicyに従う。

Verification / resume: D01–D03/D06、H08を適用。異解像度で四隅全contentを確認、common mode/driver retiming/refresh同期を仮定しない。配置overflow、資源不足、切断中applyのtruthful snapshotを確認。p007へroot ownerを使える描画/input/output契約を渡す。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: 保存keyはlocal port scope。connected集合を旧bootpreferredで隠さず全参加。unknown/invalid keyでもworking状態を保ち、mode/capability validate。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p004-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p004: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。D01–D03/D06、H08を適用。異解像度で四隅全contentを確認、common mode/driver retiming/refresh同期を仮定しない。配置overflow、資源不足、切断中applyのtruthful snapshotを確認。p007へroot ownerを使える描画/input/output契約を渡す。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p004: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 初回全extended/internal anchor、非重複/辺で連結/edge snapをoutput state/input契約へ。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p004: mainのD-ID A2/旧bootpreferred技術採択messageを受領。保存keyはlocal port scope。connected集合を旧bootpreferredで隠さず全参加。unknown/invalid keyでもworking状態を保ち、mode/capability validate。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-05 計画（q702、ベータ2）

入力: [契約の確定](../phase001/contracts-beta2.md) D-BOOT2・D-HOTPLUG・D-RELEASE・D-STORE・D-QEMU、contracts.md §5・§6。

範囲（compositor `userland/desktop/wayland/`、GPU は libvulkan だけ）: 出力の表（token・persistent key・generation・状態 detected → validated → claimed → active → retiring → gone）、出力ごとの display の surface・swapchain・合成、global の論理座標（signed、half-open、辺で連結）、全拡張と全 mirror（mirror は各出力の native の mode へ aspect-fit、黒い帯、独立の swapchain）、device event の fence で再列挙して出力を足す・外す、0 台で server を保つ、出力ごとの `wl_output` の global（add・remove）、displays.conf の読み書き（D-STORE、明るさは p005）、設定の transaction（validate → 準備 → 再照合 → 適用 → 実 present の確認 → snapshot の publish、失敗は rollback か実状態を degraded で公開）。input の clamp を出力の集合の境界に（contracts.md §6 の共有の辺の処理）。

手順: 1) `compose.c` の 1 出力の state を出力の配列へ（swapchain・frame・damage）。2) `display.c`・`shell.c` の画面の大きさの参照を「窓の owner の出力」「論理の desktop」に分ける（窓の所属そのものは p007、p004 は全ての窓を anchor の出力に置く）。3) hotplug（fence → 再列挙 → transaction）。4) mirror の aspect-fit。5) host の試験（座標・辺の判定・transaction の validate・aspect-fit の計算、`plan/ws113/tests/host-layout.c`）。

試験: host（上の 5）。QEMU（T1、Venus の `max_outputs=2`、zdesktop `--glass`）: 拡張で 2 つの出力の PNG（`zdesktop-check.py` を出力ごとに撮れるか確かめる、QMP の screendump は head を選べる）、mirror で同じ絵、1 出力の guest で今までの回帰（boot-test、files-regress）。guest の起動に `max_outputs=2` の選択肢を足す（`plan/tools/guest/`、Q1 の許可）。実機は p008。
受け入れ: QEMU で拡張・mirror の 2 出力の PNG、1 出力の回帰 PASS、出力が 0 になっても落ちない（QEMU で起こせれば）、Linux・FreeBSD の単一 display の build を壊さない（KMS の複数出力は p010）、warning 0、規約。目安 4〜5h。依存: p003。衝突: WS099 の compositor の Phase（`compose.c`・`shell.c`・`display.c`）と同時に流さない。

### D-LIMIT の反映（2026-10-05）

anchor でない出力の swapchain の作成が `VK_ERROR_INITIALIZATION_FAILED` なら、その出力を limited にして使わずに続ける（server・他の出力は保つ、窓をその出力に置かない）。topology の変化と、他の出力の swapchain の解放の後に再び試す。QEMU の試験に「Venus の出力を上限より多くつないで、limited の出力があっても落ちない」を足すか p004 で確かめる。

## p004a: 1 出力の切り替え（設計の案、2026-10-07 q850、P2）

2026-10-07 ユーザーの N8（[ws052-p007](../../ws052/phase007/phase.md) §11、ベータ2）:「外部画面のみに切り替えて通常の利用を継続する」、蓋を開けたら内蔵へ戻す。そのための部分集合をこの Phase から分ける案（行は Q1 が ws.md に置く）。同時に 2 つ以上を出す・拡張・mirror・displays.conf・transaction は p004b に残す。

### 今の事実（main 4e0e65d1f を読んだ）

- compositor は起動時に `compose_display()`（`compose.c:766-855`）で最初の恒等変換の display を選び、その native の解像度を `server->width/height` に入れる。surface・swapchain は vkdemo の `vkdemo_display_open`（最初の display を選ぶ）で作り、`kwl_compose_output_open`・`_close` が swapchain と OS の acquire・release を持つ（handoff の時に閉じて開く道は在る）。動いている間に `server->width/height` を変える道は無い（代入は `compose.c:835` と `main.c` の option だけ）。
- `server->width/height` の参照は 27 file・約 250 箇所。大半は毎 frame・毎入力に読むだけ。起動時に大きさで作る物: glass の wallpaper と blur の image（`glass.c:596・961`）、backdrop の形（`backdrop.c:529`）、wl_output の mode と description（`protocol.c:537・594`）、xdg の configure（最大化・全画面・docked、`protocol.c:1399・1528・1636`）、画面の中央に置く surface（`display.c:278-294・422-443`）、pointer の clamp（`input.c:750-767・1004`）。
- i915 は p002 で GOP の出力（resident）だけを点け、他の出力の claim は `ENOSPC`（`display.c:2884`）。eDP を release すると console の絵が eDP に戻る（D-RELEASE）。HDMI だけに切り替えるには i915 の「点ける 1 出力の付け替え」が要る（案 p011a、下）。Venus は scanout ごとに独立に claim できる見込み（D-LIMIT の上限まで、T1 の p003 の 2 出力の試験で確かめる）。

### 設計

1. **出力の選択の口**（`compose.c`）: `compose_display` を「display を選ぶ」と「その display の size と refresh を読む」に分け、`compose->display` を任意の display にできるようにする。surface は vkdemo の関数ではなく compositor の中の `compose_surface_open(server, display)`（指定の display の mode と plane を選ぶ。plane の選び方は vkdemo と同じ）。
2. **切り替え**（新しい `output-switch.c`、`kwl_output_switch(server, VkDisplayKHR target)`）: (a) 今の出力を `kwl_compose_output_close`（device の idle → targets・swapchain・surface → release）。(b) target の size・refresh を読み、`compose->display = target`、`kwl_compose_output_open`。(c) 失敗（`VK_ERROR_INITIALIZATION_FAILED`＝D-LIMIT の limited、OUT_OF_DATE、SURFACE_LOST）なら元の display で (b) をやり直し、`KWL OUTPUT switch failed target=NAME result=R` を出す。元にも戻れなければ出力の無い状態（server は動き続け、hotplug を待つ）。(d) 成功なら `kwl_server_resize(server, width, height, refresh)`。
3. **大きさの変更**（`kwl_server_resize`、shell と各部の通知）: glass の wallpaper・blur・backdrop を作り直す、wl_output を bind した全 client に mode・geometry・description・done（`protocol.c`）、最大化・全画面・docked の窓に新しい size の configure、floating の窓は画面の内へ詰める（左上の点を新しい範囲に clamp、大きさは変えない）、pointer の位置を clamp、bar・App Home・画面の keyboard の layout の cache を捨てる、全体を damage。`KWL OUTPUT resized width=W height=H refresh_mhz=R`。
4. **数え直しと口**（`output-switch.c`）: device の hotplug の fence（p003 の `vkRegisterDeviceEventEXT`）を compositor の tick（`kwl_glass_tick` か main loop の 250 ms の周期）で `vkGetFenceStatus` で見て、signal なら新しい fence を登録 → 列挙し直す → 古い fence を壊す。結果を「内蔵（name が eDP・LVDS・DSI で始まる。A2 の displayName は connector の kind を含む）」と「外部」の一覧で持つ。ws052-p012 への口: `kwl_output_external_available(server)`（外部が 1 つ以上で、limited と覚えた物を除く）、`kwl_output_use_external(server)`・`kwl_output_use_internal(server)`。今の出力が抜かれた（SURFACE_LOST・OUT_OF_DATE、または列挙から消えた）時は残る出力へ切り替える（内蔵を先に）。
5. **起動時**: 今のまま（GOP の出力＝最初の display。保存の設定・D-BOOT2 の全拡張は p004b）。
6. **試験**: host（`plan/ws113/tests/host-output-switch.c`: 内蔵と外部の分類、切り替えの順と失敗の戻し、floating の窓の clamp の計算、resize の通知の順を stub で）。QEMU（T1、Venus の 2 出力、`VENUS_OUTPUTS=2`）: `kwl-output` の試験の口（compositor の debug の command か `keiland-system` の probe）で scanout 0 → 1 → 0 に切り替え、各 PNG（QMP の screendump の head 1）で desktop が出る、窓の大きさが新しい size、`KWL OUTPUT resized` の行。1 出力の回帰（boot-test）。実機（5330 の HDMI、p011a の後）は ws052-p012 の UAT にまとめる。

### p011a（i915 の 1 出力の付け替え、案）

Keiland の lease がどの出力にも無い時に、GOP の出力でない接続済みの出力の `GPU_DISPLAY_CLAIM` を許し、resident の pipe をその出力へ modeset し直す（eDP は pipe を止め panel の電源と backlight を落とす。HDMI の modeset は起動時の `display=hdmi` の道を動いている間に使う）。その出力の release で GOP の出力へ戻し console を出す。lease が 1 つでもある時の他の claim は今のまま `ENOSPC`（同時の 2 出力は p011）。実機（5330 の eDP と HDMI）の確認が要る。

目安: p004a 4〜6 h（host と QEMU）、p011a 3〜5 h（実機）。

### 設計の詳細（2026-10-07、source を読んで）

- **surface の display の指定**: compositor は `userland/tests/vkdemo/display.c` を link している（`wayland/Makefile:28`・`Makefile.linux:73`）。`vkdemo_display_open` の loop の本体（mode・plane・surface）を新しい public の `vkdemo_display_open_on(instance, physical, VkDisplayKHR, w, h, out)` に出し、`vkdemo_display_open` はそれを呼ぶ（振る舞いは不変）。下書き: `plan/ws113/temp/p004a-vkdemo-open-on.patch`（git に入れない、sha256 99f47872…）。
- **拡張の有効化**（`compose.c` の `compose_device`）: instance の拡張の一覧に `VK_EXT_display_surface_counter` があれば足し、device に `VK_EXT_display_control` があれば足して `vkRegisterDeviceEventEXT` を `vkGetDeviceProcAddr` で引く（Linux の libvulkan-compat など無い所では hotplug の通知なしで今の動き）。
- **glass**（`glass.c` の新しい `kwl_glass_resize`）: device の idle → `glass->wallpaper`・`glass->blurred` を `kwl_host_image_release` → `wallpaper_create`（`server->wallpaper_path` は settings.c が今の選択に保つ）。atlas・tiles は大きさに依らない。
- **窓**（`shell.c` の新しい `kwl_glass_output_resized`、`keyboard.c` の `keyboard_work_area`（3375-3452）と同じ型）: 全 client の窓（live・mapped・toplevel・desktop の icon でない）について、全画面は configure（`protocol.c:1527` が `server->width/height` を送る）、docked（`maximized`）は `docked_rect` → x・y・window_width・height → `kwl_window_send_configure` と `window_resized`、floating は `kwl_glass_fit` で今の大きさのまま新しい space の内へ。glass でない look は全画面の configure と位置の clamp だけ。log `KWL OUTPUT window surface=N state=docked|floating|fullscreen x= y= w= h=`。
- **wl_output**（`protocol.c` の新しい `kwl_output_changed`）: bind した全ての `KWL_OUTPUT` の object に `output_send` と同じ geometry・mode（current|preferred、新しい width・height・refresh）・scale・name・description・done を送り直す。
- **pointer**（`input.c`）: `server->pointer_x/y` を新しい範囲へ clamp。
- **その他の cache**: 画面の keyboard（`keyboard.c` の panel の座標は開く時に計算、開いていれば閉じる）、backdrop（`kwl_backdrop_destroy` は output の close で既に呼ばれる）、App Home・bar・stage は毎 frame に `server->width` から計算（確かめて、cache があれば捨てる）。

## p004a の実装（2026-10-07 q850、P2）

| 部分 | file |
| --- | --- |
| surface の display の指定 | `userland/tests/vkdemo/display.c`・`.h`: `vkdemo_display_open_on`（`vkdemo_display_open` はそれを呼ぶ、振る舞いは不変） |
| compositor の Vulkan | `compose.c`: instance の surface_counter と device の display_control を任意に有効化し `vkRegisterDeviceEventEXT` を引く、surface は `compose->display` に開く、`kwl_compose_display_read`（size・refresh・名前）、`compose_refresh` は size を引数に、起動の display を `boot_display` に、acquire・present の SURFACE_LOST・OUT_OF_DATE は失敗でなく `output_lost`（frame を出さない）、close で hotplug の fence を壊す。`compose.h`: 出力の追跡の欄（hotplug・displays・名前・limited・output_lost）、`KWL_COMPOSE_DISPLAYS`・`KWL_COMPOSE_NAME` |
| 切り替え | 新しい `output-switch.c`: `kwl_output_tick`（`display.c` の `kwl_schedule`、250 ms ごと。fence の signal で新しい fence → 列挙 → 古い fence、limited を忘れる、失った出力を内蔵 → 他へ）、`kwl_output_switch`（close → 別の display の native の size で open → 失敗は limited にして元へ、元も開かなければ次の hotplug を待つ → size が変われば fit → wl_output を全 client へ）、`kwl_output_external_available`・`_use_external`・`_use_internal`（ws052-p012 の R4 の口）。内蔵は名前の `:edp:`、無ければ起動の display |
| 大きさの変更 | `glass.c` の `kwl_glass_resize`（wallpaper・blur を作り直す）、`shell.c` の `kwl_glass_output_resized`（全画面は configure、docked は docked_rect で configure、floating は大きさのまま space の内へ）、`protocol.c` の `kwl_outputs_changed`（bind した wl_output に geometry・mode・scale・name・description・done）、pointer の clamp |
| QEMU の道具 | `plan/ws035/tests/zdesktop-guest.sh` の `VENUS_DISPLAY=dbus`（選ぶ時だけ、runtime の私的な session bus に QEMU の D-Bus display、既定は不変、2026-10-07 Q1 の許可）、新しい `plan/tools/guest/venus-head.sh`（`SetUIInfo` で head を挿す・抜く）、`plan/ws113/tests/output-switch-p004a.sh`（T1） |
| 試験 | `plan/ws113/tests/host-output-switch.c`・`.sh` |

## 確認（2026-10-07）

- `sh plan/ws113/tests/host-output-switch.sh` PASS（plain・ASan/UBSan: 内蔵と外部の分類（名前、無ければ起動の display）、外部への切り替えで size・fit・client への通知・pointer の clamp、内蔵へ戻る、拒否で limited と元への戻り、hotplug で新しい fence を先に・limited を忘れる、250 ms の間引き、失った出力の内蔵への移動、display 0 台で待ち次の display に開く、仮想の display の内蔵の扱い、同じ display への切り替えは何もしない）。
- build（warning 0）: zedBSD の `wayland`・`vkdemo`（`config-amd64-zdesktop.mk`）、`keiland-linux`（rc 0）。style-check は新規の違反 0。
- 未実施: QEMU（T1: `VENUS_DISPLAY=dbus VENUS_OUTPUTS=2` で `output-switch-p004a.sh`。D-Bus display で VNC が絵を取れるかは未確認なので試験は guest の行で判定）、実機（p011a の後、ws052-p012 の R4 とまとめて 5330 の UAT）。
- 制限: 画面の keyboard が開いている時の panel の座標は開き直すまで古い大きさ（p004b で）。
