<!-- design-reviewer のレビュー 1（2026-10-20、ws277 design.md 版 1）。Q1 が報告の本文をそのまま置いた -->

## WS277 p001 設計のレビュー 1

対象: `/home/awe/zedBSD-claude1/plan/ws277/design.md`（版 1）、`plan/ws277/ws.md`、`plan/ws277/phase001/phase.md`。
file は編集していません。build・試験・QEMU も走らせていません。
照合した物:
- zedBSD: `display.c`・`render.c`・`render.h`・`gpu.c`・`gpu-scanout.h`・`gpu-display.h`（kernel と uapi）、`wsi-display.c`・`wsi-swapchain.c`・`memory.c`・`context.c`、`vf2-*.h`、`text-display.h`・`serial-console.c`、WS256 p011・design-peripherals、WS269・WS270 の design。
- vendor の `~/visionfive-linux`（4cecf169f38e）。
- mainline は gh api で取得: `drivers/gpu/drm/verisilicon/*`、`jh7110.dtsi`。
- Mesa は GitLab API で main（0913fce718fe）を取得: `pbestate.xml`、`pvr_image.c`、`pvr_wsi.c`、`common/device_info/bxe-4-32.h`。Mesa の blob 検索は 401 で拒まれたので使っていません（未確認として扱います）。

### Blocker

**B1. flip の完了と libvulkan の hold の解放が合わない（tearing と UAPI の契約違反）**
- 場所: §4.2 の wait の行、「二重の buffer の保持」、§3.2 の順序。
- 根拠:
  - libvulkan は PRESENT の ioctl が成功で返った直後に、前の front の `holds--` をしています（`wsi-display.c:1475-1483`）。acquire が見るのは `image_available`（holds だけ）で（`wsi-swapchain.c:3795-3803`）、GPU_DISPLAY_WAIT を待ちません。
  - libvulkan のこの道の注記は「The kernel returns only after checked native selection and flush have completed」（`wsi-display.c:1425`）です。
  - 設計は「新しい address は vblank で効く。古い buffer を返すのは wait の完了の時」としています。
- 起こる事: present が返った時点で、前の buffer がまだ scanout 中なのに app に再び渡り、GPU がそこへ描きます（tearing・崩れた画）。
- 直し方: present（または `DRV_GPU_DISPLAY_UNGATED` と `adoption_wait`。bcm2711 の形、`gpu-display.h:60-75`）が、latch（commit が vblank で採られた事）の後に返る契約にします。
  - p002 の時点から latch を待つ必要があります。IRQ は p003 なので、p002 は `CURRENT_LOCATION` か commit bit の自己 clear の polling で待ちます（自己 clear するかは未確認）。そうでなければ、p003 まで zero copy を出しません。
  - 設計にこの契約を明記してください。

**B2. GPU の SLC に残った render の結果を DC8200 が読めない**
- 場所: §3.2「scanout する buffer も同じ『SLC を迂回』の属性で写す」。
- 根拠:
  - SLC_BYPASS は render の session の GPU MMU の写像ごとに付き、mappable な blob だけが対象です（`render.c:981-985`、`render_blob_create` は `GPU_BLOB_MAPPABLE` の時だけ mappable、`:688-691`）。
  - WSI の shared の image は `GPU_BLOB_SHAREABLE` で作られ、HOST_VISIBLE の時だけ MAPPABLE が付きます（`memory.c:1246-1256`）。device-local の swapchain の image は SLC を迂回しません。
  - FW が完了時に SLC を書き出すかは WS269 でも未確認です（ws269 design.md:124・241）。
  - 設計には、どこで迂回の属性を付けるかの仕組みがありません。export・import の時点では写像はもう出来ています。
- 起こる事: DC8200 が DRAM の古い内容を出します。p002 の試験は CPU が書いた mappable な blob（迂回あり）を使うので、この誤りを検出できません。
- 直し方: 次のどちらかを WS269 の render.c の変更として計画に入れ、Q1 経由で WS269 の該当 Phase に依頼します。
  - (a) `GPU_BLOB_SHAREABLE` の blob を常に SLC_BYPASS で写す。
  - (b) flip の前に FW に `SLCFLUSHINVAL` を送る。
- 試験は GPU が書いた non-mappable な image で行います（WS269 p010 の後）。

**B3. frame が mode より小さい時、DC8200 が buffer の外を読む（他の process の memory が画面に出る）**
- 場所: §1.5「address と stride だけを書き換える」、§4.2 の present（BLOB）。
- 根拠:
  - 今の `output_mode` は mode より小さい extent も受けます（`display.c:648-658`）。
  - import の境界の検査は `stride × image.height` だけです（`display.c:939-943`）。
  - plane の `SIZE`（`vs_dc_hw.c:1626-1628`、width | height<<15）を書き換えなければ、DC8200 は U-Boot の mode の高さぶんを読みます。
- 起こる事: buffer の後ろの物理 memory（他の process の data）が画面に出ます。
- 直し方: 次のどちらかにします。
  - (a) zero copy は幅・高さが mode と一致する時だけにし、ほかは EINVAL か copy の道へ回す。
  - (b) `SIZE`・`TOP_LEFT`・`BOTTOM_RIGHT` と背景色を設定する（mainline `vs_primary_plane.c` atomic_update の Procedure referenced）。
- どちらでも、境界の検査は DC が実際に読む範囲（plane の高さ × stride）で行います。

### Major

**M1. constraints の placement を立てると、zero copy が黙って働かない**
- 場所: §4.2 の constraints（`CONTIGUOUS|DMA32`、`max_dma_address`）。§4.2 は「render.h は今のままで足りる」とも書いています。
- 根拠:
  - libvulkan は placement を `GPU_BLOB_CREATE_PLACED` で送ります（`wsi-display.c:1944-1950`、`memory.c:1259-1272`）。
  - core は条件付きの要求を `blob_create_placed` の無い backend に ENOTSUP で返します（`gpu.c:2697-2705`）。
  - PowerVR の render node は `blob_create` だけを持ちます（`render.c:156`）。
  - ENOTSUP を受けると libvulkan は copy の道へ fallback します。
- 直し方: 次のどちらかにします。
  - render node に `blob_create_placed` を足す。全部が連続で 0xffffffff 以下なので、検査して受けるだけです。WS269 の source です。
  - placement は 0 のままにし、import の時に `backing->pages[0]`・連続・上限を検査する。

**M2. release・close の後始末が足りない**
- 場所: §4.2 の claim・release、二重の buffer の保持。
- 根拠: 今の `resource_destroy` は import の buffer をすぐ解放します（`display.c:466-468`）。core は close の前に session の resource を全部 destroy します（`display.c:349`）。front の resource が先に消え、`end_lease` はその後に呼ばれます。
- 直し方: 次を設計に明記します。
  - front の buffer の参照は resource から display 側へ移す。`render.h` に retain は無いので、参照の所有の移し替えで行う。
  - release・close では次の順に行う。U-Boot の plane の register を全部（address・stride・CONFIG の format・swizzle・SIZE・CONFIG_EX）attach の時の snapshot へ戻す → commit → latch を待つ → 参照を外す。
  - 参照を外すのは thread の文脈で行う。`buffer_free` は uncached の別名を外して `kern_free` するので、IRQ の中では呼べない（`render.c` の `buffer_free`。bcm2711 も「Borrowed images are let go only in thread context」）。
  - vblank が来ない時（HDMI を抜いた等）の待ちの上限と、その時の扱い。

**M3. panic の画面が消える（BUG-158 の後退）**
- 場所: 設計に無い。
- 根拠:
  - VF2 の text の ops は serial-console で、suspend・resume は何もせず、`reveal_fatal` もありません（`serial-console.c:68-69`、`visionfive2.c:188`）。
  - panic の文字は HAL の fbcons が bootfb に描きます。zero copy 中の DC8200 は render の buffer を出しているので、panic が画面に出ません（今の copy の道なら bootfb が出ています）。
- 直し方: kernel 側に致命の時の hook を設計に入れます。割り込み禁止・lock 無しで plane の register を bootfb へ戻すものです。例えば VF2 用の text の ops に `reveal_fatal` を持たせる。kernel の中で済むので HAL の変更は要りません。

**M4. plane・panel の特定と、引き継ぎの判定が足りない**
- 場所: §2.4、§4.1 の引き継ぎの判定。
- 根拠:
  - primary plane は 2 つあり、+0x4 の offset が付きます（`vs_dc_hw.c:1290-1309` `get_addr_offset`、mainline `VSDC_FB_ADDRESS(n)` = 0x1400 + 4n）。plane が出る panel は `CONFIG_EX` の bit 19 で決まります。panel も 2 つです。
  - 設計は 0x1400 を固定で使っています。
  - bit 12 は mainline では `VSDC_FB_CONFIG_EX_COMMIT`（`vs_primary_plane_regs.h:37`、set して commit）です。vendor はさらに `PANEL_CONFIG_EX` の bit 0 も操作します（`vs_dc_hw.c:1540-1547`）。設計の「shadow を外す・戻す」という読みは、この手順を正しく表していません。
- 直し方:
  - attach の時に両方の primary の `CONFIG_EX`（FB_EN bit 13、DISPLAY_ID bit 19、DEC bit 1）と、`CONFIG`（tile・rotation・scale・format）を読み、HDMI の panel（推測では 0。vendor `vs_dc.c:747-755`）に出ている物を選ぶ。
  - 判定の条件を「address == bootfb、stride == pitch、SIZE == W×H、linear・無回転・無拡縮・DEC 無し」にする。
  - commit は mainline の手順（Values referenced）で書き、vendor の PANEL bit 0 の要否は p002 で確かめる、とする。
  - model の register 0x20（0x8200）も読む。

**M5. format と alpha**
- 場所: §1.5 と §2.4 が矛盾しています（§1.5 は address と stride だけ、§2.4 は format の対応を p002 で決める）。
- 根拠:
  - U-Boot の format と frame の BGRA・RGBA が違えば、`CONFIG` の format・swizzle（bit 26・23）を書く必要があります。
  - ARGB を blend 有りで出すと、alpha が 0 の frame は背景色（黒）になります。mainline は primary を X 系の format だけにし、`VSDC_FB_BLEND_CONFIG_BLEND_DISABLE`（regs.h:51）を書きます。
- 直し方: XRGB・XBGR の扱い（alpha を無視）か blend 無効を必須にし、§1.5 の「address と stride だけ」を直します。

**M6. D4 の条件が不正確で、他の WS への依頼が抜けている**
- 場所: §4.3、D4。
- 根拠: libvulkan が shared の道を選ぶ条件は次の全部です（`context.c:180-210`）。設計の「少なくとも HOST_SCANOUT の相当」とは違います。
  - vendor の capset の長さと magic。
  - flag がちょうど `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT`。
  - WINQ の XML の版。
  - STRICT_QUEUE・QUIESCE の意味の義務も伴います。
- 直し方: D4 (a) の依頼の中身を列挙し、Q1 経由で WS269 p010（planned）・WS270 の Phase への変更として出します（Awesome Plan §5 の foreign Phase への comment）。列挙する中身:
  - 正確な capset。
  - PBE の LINEAR と 32 byte の stride。
  - SLC（B2）。
  - placed の確保（M1）。

**M7. D1 の根拠と、p002 の試験が誤りを検出できない**
- 場所: D1 の推奨理由、§6 の p002・p003。
- 根拠:
  - 実機の U-Boot が HDMI と GOP を出すか自体が未確認です（WS256 p011 は in-progress で実機の目視待ち、ws256/ws.md:41）。
  - GPU が線形で描いた frame は WS269 p010 が出来るまで無く（p008・p009 の後、planned）、それまで zero copy の効果は測れません。D1 の「zero copy の効果をすぐ測れる」は誤りです。
  - p002 の「色の帯が出る」は copy の道でも同じ見え方になるので、zero copy になっている事を区別できません。
- 直し方:
  - p002 の試験を次のように直します。
    - present の後に CPU が blob を書き換え、再び present しないまま画面が変わる事を見る（zero copy の時だけ変わる）。
    - plane の ADDRESS の読み戻しを画面の行に出す。
    - libvulkan と同じく、present が返った直後に前の buffer へ書いて、tearing が出ない事を見る（B1 の検出）。
  - D1 の説明に、実機の GOP が go/no-go の条件である事、効果の測定は p006（WS269 p010 の後）である事を書く。

**M8. mode の選び方（p004・p005）と refresh**
- 根拠:
  - UAPI の mode の操作は ENUMERATE と VALIDATE だけで、mode を設定する op がありません（`uapi/gpu-display.h:39-40・139-157`）。p004 の「720p へ切り替えて画が出る」、p005 の「選んだ mode で画が出る」には、UAPI 上の経路がありません。
  - p002 で refresh を `hdmitx0_pixelclk` から求めると、DT の固定の 297 MHz（実際の rate ではない）を使うことになります。1080p60 なら 120 Hz と誤って報告されます。
- 直し方: mode を選ぶ経路を定義します（claim の時、present の width・height・refresh、または boot parameter。i915 がどうしているかは未確認なので p004 の前に調べる）。refresh は vblank の間隔の実測か「不明なら 60 Hz」にし、DT の固定値は使わない、と明記します。

**M9. D3 の範囲が足りない（他の WS の source の変更）**
- 根拠: `vf2-pmu` は `DRV_VF2_PD_GPU` 以外を EPERM にします（`vf2-pmu.h:41`）。p004 で PD_VOUT を操作するには、vf2-crg への voutcrg の追加に加えて vf2-pmu の変更も要ります。どちらも WS256 の source です。
- 直し方: D3 に vf2-pmu を含め、他の WS の source の変更をまとめた表を設計に置きます。表に入れる物:
  - vf2-crg・vf2-pmu（WS256）。
  - render.c の SLC と placed の確保（WS269）。
  - visionfive2.c の組み込みの記述。

### Minor

1. **FOREIGN は外さない**: render は gpu1、display は gpu0 で、core はこれを foreign と扱います（`gpu.c:5008`、UAPI `gpu-scanout.h` の「FOREIGN additionally permits backing from another device」）。§4.2 の「FOREIGN は外すか」は UAPI の意味と合いません。
2. **2 pixel の前提は確定できる**: BXE-4-32（Mesa `device_info/bxe-4-32.h`、`PVR_DEVICE_IDENT_36_V_54_182`）には `has_pbe_stride_align_1pixel` が無いので、2 pixel 単位が確定します（Values referenced）。§1.1・§8 の「未確認」を外せます。
3. **零埋めの写像は確定済み**: render の buffer の零埋めは、確保の時に uncached の別名を通して既に行われています（`render.c:322-333`）。§3.2 の「実装で確かめる」は不要です。
4. **mainline の DTS と CID の書き方**: mainline の `jh7110.dtsi` には voutcrg の node 自体があります（約 1202 行、PD_VOUT 付き）。§0 S3 の「voutcrg の入力だけ」は誤解を招きます。CID 0x310 の扱いは vendor（rev 0x5721）と mainline（0x5720）で違います。
5. **offset の揃えが矛盾**: §2.4 は「offset 0 だけを受ける」、§4.2 は「offset_alignment = 32」です。どちらかに統一してください。
6. **lock と IRQ の文脈**: vblank の待ちを display の mutex を持ったまま行うかどうかを決めてください（UNGATED にするかどうか）。`AQ_INTR_ACKNOWLEDGE` は読むと clear されるので、polling の道では読まないでください（IRQ と競合します）。vblank の IRQ の中では参照を外さないでください（M2）。
7. **WS269 p007 の依存の書き方**: p007 は in-progress（実機は未実施）です。「WS269 p007（実装済み）」は Phase 全体の依存としては成り立ちません。「import の code（merge 済み）」という出力の範囲の依存として書いてください。
8. **LW**: B1〜B3・M1〜M5 を足すと、p002 の 8〜12 LW は過小です。見積もり直しが要ります。
9. **実機の確認の書き方**:
   - 「実機（T1・5320 ではなく VF2）」は意味が取りにくい書き方です。
   - 判定の方法には次の問題があります。
     - VF2 の板の接続と、実機の判定の方法（WS256 の #10・#15）が未決です。
     - 合格がユーザーの目視に頼っています。memory の運用では、ユーザーにその場で試験を頼まず、試せる物の一覧に載せる事になっています。
   - 依頼は T1 か一覧の形で書いてください。serial の log で判定しない点は AGENTS.md に合っています。
10. **stride の幅**: vendor の `struct dc_hw_fb` の stride は u16 です（`vs_dc_hw.h:399`）。register の幅が未確認のうちは、stride ≤ 65535 を検査してください（4K でも収まります）。
11. **ws.md の完了の条件 2**: fps の測定は WS269 p010・p011 と WS270 に依ります。WS277 の完了がそこまで閉じられない事を ws.md に書いてください。

### HAL

上の直し方（panic の hook は kernel の text の ops、写像と IRQ は既存の kernel の API）であれば、`hal.h` の変更は要らないと判断します。§7 の結論は保てます。
ただし M3 を HAL の fbcons 側で解く設計にすると HAL の責務の変更になり、承認が要ります。

### 確かめた事実（設計どおりで正しかった物）

- **PBE の値**: `MEMLAYOUT` の値、linestride が 2 pixel 単位の 16 bit である事、Mesa の LINEAR の扱い（`pvr_image.c:50-100・467-472`）。
- **DC8200 の address と IRQ**: dc8200・hdmi・dssctrl・voutcrg の address、IRQ 95・99、clock・reset の一覧、`hdmitx0_pixelclk` の 297 MHz。
- **DC8200 の register と plane**: modifier の先頭が LINEAR、plane の address が 32 bit、vendor の DMA の mask が 40 bit である矛盾、MMU 無しの import が連続の sg だけを受ける事、`pitch_alignment` 32、PLL の表と 297 MHz の上限、timing の register の offset。
- **zedBSD 側**: libvulkan の `copy_display` の分岐、`render.c:13`・`vf2-gpu.c:60`・`gpu-scanout.h:29-35`。

### 未確認（推測を含む）

- VF2 の配布の U-Boot が HDMI と GOP を出すか。ExitBootServices の後も DC8200 が動き続けるか。GOP の base と plane の ADDRESS が一致するか（U-Boot の copy framebuffer の時は一致しない）。
- HDMI が panel 0 か（vendor の clock の扱いからの推測）。commit bit が自己 clear するか、latch の時点。
- FW が完了時に SLC を書き出すか。
- i915 などが mode をどう選んでいるか。

### 結論

**このままでは受け入れられません。**
- Blocker の 3 件は、どれも実装すると画の崩れ・古い画・他の process の memory の表示につながります。B1 と B3 は p002 の範囲そのものに入ります。
- M1 と M6 は、直さないと zero copy が黙って働かない欠陥です。
- 版 2 で B1〜B3 と M1〜M9 を直し、他の WS（WS256 の vf2-crg・vf2-pmu、WS269 の render.c と p010、WS270）への依頼を Q1 経由の foreign Phase への変更として明記してから、再レビューしてください。
- 全体の方針（線形の ARGB 系による zero copy、U-Boot の mode の引き継ぎから始める事、DC8200 の MMU を使わない事、32 bit・連続の制約）は妥当です。
