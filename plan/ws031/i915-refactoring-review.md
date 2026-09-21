# WS031 i915再構成案 — 敵対的レビューと修正

作成日: 2026-09-21。対象: [設計本文](i915-refactoring-design.md)、
[関数移動台帳](i915-refactoring-functions.md)。基準commit:
`7e7ff337c7f145e5fd79e4daaafa05a1cd4a4bcc`。
centrisの現行ソースを静的に追跡したレビュー。ドライバ修正、build、実機試験は実施していない。

## 1. 結論

**初稿のままの一括移動は不可。** 2,988関数を列挙していても、起動runnerを試験と誤分類し、
関数内外の状態、共有画像のVM、wire返信、停止責任を十分に引き継いでいなかった。

一枚のドライバを共通部／描画／表示／compilerへ分ける方針自体に、今回調べた経路を
原理的に実現不能にする理由は見つかっていない。ただし、それは次の契約補完を条件とする
設計判断であり、全構成でlinkすることや全Vulkanワークフローの保証ではない。

| 判定対象 | 今回の結論 |
| --- | --- |
| 明示関数の列挙漏れ | 既存2,988定義と独立した関数形の字句抽出を突合。追加候補83箇所は24名のinitializer／反復／lockマクロで、追加の明示関数定義は見つからなかった |
| ファイル自体の漏れ | 初稿は376 C/H/INC。manifestとrun.shを含め378全ファイルの保全台帳を追加 |
| 意味上の移動漏れ | あり。runnerの本番起動、状態・定数・生成器・ツリー外呼出元などを補正 |
| module exportの完成度 | 未完。H/Eと主要契約を補強したがI候補1,116件、static跨ぎ201辺は一括実装の根拠にならない |
| libvulkanの現行正常系 | E-127〜130の経路を保存する条件を整理。再実行したわけではない |
| libvulkanの全機能 | 現i915には未対応がある。Waylandの描画側import、一般のexternal memory/fence、semaphore意味論、停止回復などは配置整理で完成しない |

## 2. 指摘一覧と反映

P1は起動不能・描画/共有破綻・危険な資源寿命につながる事項、P2は移行/検証漏れ。
「資料修正済み」はコードの問題が解消した意味ではない。

| ID / 重要度 | 根拠と反例 | 資料へ反映した改善 / 残件 |
| --- | --- | --- |
| A01 / P1 | `parity/runner.c:79,160,191,205`を全部testsへ移すと、`src/kern/main.c:181`のreadiness通知から`drv_i915_parity_attach`へ至る本番起動を失う。`i915.c:305`もrunner登録を呼ぶ | 5関数の主担当をdevice.cへ訂正。登録とreadyの順不同・一度だけ起動を残す。新E `drv_i915_runtime_ready`とツリー外callerを明記。試験本体/結果集計だけtestsへ |
| A02 / P1 | `parity/probe.c:2430`のforcewake保持と`rctx/rlcd`がresidentを支える。LCD callbackから抜けるとcallerのteardownへ戻る。enable関数へ切るだけではpower/参照が先に消える | device/pipe所有の永続状態へ移す。request workerとdisplay workerの責務を分離。callback再入、自分のworkerを待つdeadlock、unpublish EBUSY後のteardownを禁止。順序の実機再検証は残件 |
| A03 / P1 | `resident_display.c:306`は表示openの`owner->vk`、RCS0 context、VMを使う。`legacy_shim.c:505`がpanel二面をそのVMへbind。render側VAを渡すだけでは別VMで読めない | Vulkan object表を持たないblit contextを明示。src alias＋panel dst＋state/batch/shaderを同じ表示VMへbind。表示openにVkDevice作成を要求しない |
| A04 / P1 | `context.c:708`はmapped replyと20-byte trailer、長いcommandは180の外部streamを使う。旧案のexecute(bytes,size)だけではresource lookup/保持とpartial failureが不明 | transport.cを新設、178/179/180/137を明示所有。resource_id lookup/viewの境界、外部stream copy、返信可視化、decode resultを定義。新APIは提案で、実装検証は未実施 |
| A05 / P1 | `vk->objects`と`gfx_memories`はdevice/global、libvulkan wire_idはprocess内の採番。別processの同じIDやcloseが衝突し得る。`inst_token`はstaticでgeneric free不可 | session＝open単位とし、kind/wire_idをsession内で照合。VkDevice単位には分割しない。blob attachもsessionを受ける。型別destructorと非所有tokenを区別、共有はbackingのみ。多process/異常close検証は残件 |
| A06 / P1 | `vk/vk.c:194`がflags7を便宜上宣言し、`device.c:278`はSTRICT/QUIESCE/JOB/CAPACITYを必須とする。一方`legacy_shim.c:473,481,491`のreset/recoverはENOTSUP | 「能力をcallbackに合わせれば終わり」ではないと訂正。bitsを落とすだけなら現libvulkanの初期化を壊す。正常系保存と、監督/停止の契約修復を別の変更段階・未達事項として記録 |
| A07 / P1 | `i915.c:1943`でresidentのtimeline 1をRCS0へ特別対応。単にredirectを外すと同じ1が旧BCS0へ戻る。またreserve/commitとdecoder completionは別経路 | timeline対応表をdevice profileに持たせる。CAPACITY→RESERVE→native submit→COMMIT→実完了とcancel失敗をjob.c/request.cで保存。0のdecoder通知をGPU fenceへ流用しない |
| A08 / P1 | `gfx-draw.c`のheap offset、`gfx_eot_only`、rect shader cache、session state/batchが関数表にない。`gfx_eot_only`はselftest由来でもrect buildで使う | 本番のEU命令列とshader metadataを保全。heap layoutをrender内部契約へ、機種thread数をdevice-infoへ、cacheをdevice/target所有へ。参照shader/診断readbackだけtestsへ |
| A09 / P1 | `ms_pool[2]`、`ms_sel`、`parity_lcd_cur_i915/wm`、glue内callback/globalが移動後も暗黙の選択対象を使うと別pipe/deviceへ誤書込み。retained状態はprepareのmemsetを跨ぐ | state/pipe/retentionへ所有先を明記。全glue callerの引数化が終わるまでは旧TU群と排他を保ち、一部だけglobalを除去しない。マクロ展開と型layoutの全件確認は未完 |
| A10 / P2 | 一般bind(`drv_gpu_ops *`)案ではpresent.cがdisplay.cのconst sub-opsを安全に追記するstorage契約がない | device-owned `i915_ops_storage`に全sub-ops実体を置く。担当field初期化→top-level pointer接続→検証→公開後不変。EBUSY時はstorageも保持 |
| A11 / P2 | 非関数資材とツリー外生成器が索引の外。`gen_vk_server_codec.py:265`は旧出力パスを直書き。Makefileはtest/productionを混在link | 全378ファイルをhash付きで保全。実際のkernel codec generator、LCD generator/spec、manifest、shell runner、kernel/public header、build listを追従対象に列挙 |
| A12 / P2 | `kind_name`をtestsへ移すとproduction `log_trace`から逆依存。`lcd_hw_check.c`は実GPUへpatternを書く試験なのに本番diagnostics案だった。legacy resetの台帳も本文と不一致 | kind_nameはdiagnosticsへ、scanout_hw_checkはtests/displayへ訂正。legacy reset/domain resetをreset.cへ揃えた。201の跨ぎ辺は未解決を隠さず残す |
| A13 / P1 | 同期実行だからという理由で`rec_submit`のwait/signal semaphore読み捨てを正当化できない。未signal semaphoreを待つsubmitや独立producerには意味論が必要 | 同期backendとVulkan同期objectの意味を分離。render/syncが状態/依存を所有し、未対応を成功にしない。非同期化・一般Vulkan互換の実装済み扱いを禁止 |
| A14 / P1 | `lcd_modeset_compat.h:97`の`for_each_intel_crtc_in_pipe_mask`は空走査、`lcd_wm_compat.h:47`は選択pipe、`n1_compat.h:454`はregistry走査。n1はincludeの度に再定義する。単純連結では最後の定義が全callerへ効いて挙動が変わる | private headerとTUの隔離を指定。modeset/watermark/takeoverで別の意味を持つloopへ展開してから統合する。名前衝突7組だけの検査で安全とは言わない |

A01補足: 現`try_launch`は`ready && !launched`だけで、登録が揃ったかを検査しない。
ready先行でSYNC_ONLY試験が起動する余地もあるため、本文の順不同起動は既存の保証ではなく
新しい本番開始契約として検証する。単にrunnerのファイル名を変えるだけでは足りない。

## 3. libvulkanワークフロー照合

この表の「既存証拠」はE-127〜130とソースの照合であり、このレビューで得た新しいruntime結果ではない。
Uのパスは`userland/base/libvulkan/`相対、Kは`src/drivers/gpu/i915/`相対。

| ワークフロー | 実際のU側入口 / K側受け口 | 新構成で保存する処理 | 判定・限界 |
| --- | --- | --- | --- |
| node発見→instance→device | U context_open/instance.c/device_validate、K get_info/get_capset、inst dispatch | version1.3.269 wire契約、168-byte capset、queue count、flags7、GPU ops capabilities、instance/physical/device object | 正常デモ接続の証拠あり。flags7の全契約達成は未証明（A06） |
| 複数VkDevice/queue | U device.cはphysicalのcontextを共有しtimeline slotを予約 | open内の共通object namespace、device child関係、queue slot配分 | 一つの公開queue slotという現行制約を保持。VkDeviceごとに新sessionを作る設計へ変えない |
| command/reply往復 | U context_transaction、K i915_vk_command_reply→drv_i915_vk_command→cmd dispatch | handleとresource_idの区別、178 selector、179 seek、137 trailer、capacity/offsetとCPU可視化 | 正常往復の証拠あり。selectorを複数回変更する不正入力や全境界の安全性は別検証 |
| 大きいshader/command | U writer.bytesがGPU_COMMAND_MAX−128超なら180、K inst_execute_streams | resourceを保持しoffset/bytes検査、私有copy、同じreply cursorへdecode | 現実装あり。削除/固定長化不可。ネスト・count・依存情報の厳格検査は追加の改善要件 |
| allocate→blob→map→bind | U memory.c:vkAllocateMemory→memory_export、K gfx_allocate_memory→i915_blob_create→blob_attach | Vk memoryのwire_idとblob_idを対応付け、blob=実backing、coherent CPU mapとGPU VAが同じbytesを指す | E-127〜130で使用。孤立blobとVk memoryのattach失敗を混同しない |
| pipeline→draw | U pipeline.c/commands.c/queue.c、K gfx-obj/gfx-rec/gfx-draw/compile | generated codec、recorded object参照、binary metadata、state heap、draw batch、実GPU待機 | 回転直方体の証拠。任意SPIR-V/任意pipeline対応の証明ではない |
| queue submit / fence | U sync.c:CAPACITY/RESERVE/COMMIT/CANCEL、K job callbacks/request/shim | reservationはnative副作用より前、markerは同じqueueの後、callback exact-once、失敗時sticky error | 正常経路あり。ハング・cancel/close競合・未監督の同期blit等は受入未了 |
| 同一node direct display | U wsi-image→wsi-display:別fdのIMPORT、K share_import→rd_present→shim_map_panel | export元保持、表示alias独立VA、GPU copy→panel back buffer→flip、表示lease | E-130で40秒726frame。**render fdとdisplay fdは別**。live shader hashと参照binary同一性を混同しない |
| 共有不可時CPU fallback | U wsi-swapchain.c:一度だけ経路選択、wsi-display.c:write/present、K resource_write/CPU present | SHARED試行失敗を分類。FORMAT/FEATURE未対応だけCOPYへ、OOM/device lossを隠さない | 現行fallbackを保持。通常E-130経路をCPUへ戻す理由にはしない |
| Waylandのrenderer側import | U wsi-wayland.c/wsi-image.cのGPU_RESOURCE_IMPORT→Vulkan memory import | raw aliasだけでなく受信contextのVkDeviceMemoryへ実backingを関連付け、同期と終了後保持 | **現i915では完結する根拠なし**。gfx_allocate_memoryがexternal宣言を読み捨てる。Venusでの受入をi915へ流用不可 |
| 標準OPAQUE_FD / external fence | U memory.c/external-fence.c/external-properties.c、K GPU allocation/fence opsと対応opcode | image共有とallocation共有を区別、UUID/metadata/権限/消費規則を保存 | i915はGPU_CAP_SHAREを宣言するがALLOCATION_SHARE/FENCEとは別。一般external機能の互換性をこの再構成で保証しない |
| 異なるGPUへ表示 | U display node照合/constraints/import試行、K scanout import_imageは現NULL | foreign DMAを検証できないとき拒否、許容されたCOPY選択のみ | 無条件foreign importは不可。deviceUUIDやsame-node shareを一般化しない |
| destroy/reopen/stop/reset | U context_close/device teardown/WSI job drain、K close/stop_poll/isolate/reset | callback完了、sync queue、display選択、mappingとexport、quarantineの寿命を一元追跡 | E-130 happy pathを超える一般回復は未達。ENOTSUP resetを配置変更で成功へ変えない |
| semaphore / idle | U vkQueueSubmit/WaitIdle、K rec_submit/inst_wait_idle | 未signal待機、signal消費、別queue/deviceの順序、pending参照 | wait/signal読み捨てとidle即成功は同期happy path依存。一般workflowは成立すると判定できない |

### 3.1 四つの識別子を混同しない

| 名前 | 所有範囲 / 使途 | 移行先 |
| --- | --- | --- |
| UAPI resource handle（64bit） | GPU coreのfile/session資源操作 | resource.c/session.c |
| resource_id / slot（32bit） | rendererへ渡すshared reply/streamの識別 | resource.cのlookup_id、transport.c |
| Vulkan wire_id / blob_id（64bit） | processが採番するobject ID、blob_idはmemoryへの関連付け | session内object.c/memory.c。device全体で検索しない |
| timeline slot | 0 decoder、非零GPU queue。engine enumと同じ値とは限らない | device profile、job.c/engine.cの明示対応 |

### 3.2 dispatchの保持規則

初稿はinst.cをvulkan.cに吸収していたが、ライフサイクル入口へのdispatch逆流を避け、
instance.cとtransport.cへ分けた。

| 現行opcode/分類 | 最終主担当 | 必ず残すもの |
| --- | --- | --- |
| 178 / 179 / 180 / 137 | transport.c | reply select/seek、external stream、version/trailer |
| 0 / 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 / 11 / 12 / 19 / 20 / 155 | instance.c | create/destroy/問い合わせ/device queue2/idle |
| gfx_obj_dispatchでhandledになるもの | memory/image/descriptor/pipeline/render-pass/sync/object | 現行の優先順位と各codec |
| gfx_rec_dispatchでhandledになるもの | command.cとsurface変換後のblit/draw | 記録→submit、型別object保持 |
| 残るres/pipe/cmdbuf/sync/wsi枝 | 各所有moduleへ照合して統合 | oldという名前だけで削らない。fixture、残opcode、destroyを確認 |
| 未知/未実装 | dispatchで明示失敗 | payload長がないためreaderを進めて成功扱いしない |

これはopcode全件の意味論認証表ではない。移行前にlibvulkanのAPI台帳／実送信opcodeと
K dispatchを機械比較し、handler存在だけでなくwire shape・出力・失敗・破棄も固定する。

## 4. 関数以外の保全と誤分類の訂正

| 現行データ・契約 | 所属先 | 失うと何が壊れるか |
| --- | --- | --- |
| i915.cのPCI ID表、ops/sub-ops、capability、prototype、条件付きcompile | i915.c/device-info.c/ops.hと各owner | PCI認識、登録、callback一致、mode別link |
| runner.cのg_runner / readiness / device保持 / 起動lock | device.c（test結果部分はtests） | early attach、二重起動防止、遅延起動 |
| probe.cのmmio/GT/engine/display依存、rctx/rlcd、forcewake取得順と逆順解放 | device-owned state＋power.c/display | enable帰還後にdangling pointer、早すぎる電源解放 |
| legacy_shim.cのcontexts/run queue/sync queue/work/sync_done/stop | context.c/request.c、display処理はpresent.c | callback drain、sleep/wakeup、同一worker再入 |
| shimのmap_vm/map_va[2]/map_pages[2]、display_up/failed | display/scanout.c/present.c | 別VMのpanel mapping、二面切替、失敗後の再利用 |
| rdのmutex/owner/lease/next_lease/sequence/present_tick/active | display device内のlease/present状態 | 別device混線、lease再利用、wait一致 |
| ms_pool/ms_ops_pool/ms_retained_pool/ms_retained_ops_pool/ms_sel | pipe state、停止未確認のretention owner | prepareのmemsetで保持を失う、pipe取り違え |
| parity_lcd_cur_i915/parity_lcd_wm等のglue選択状態 | display引数化。移行中は関連TU群と同じ排他 | 関数の配置先だけ変えて別deviceへMMIO |
| lk/lcdb_locks/lcdb_scanoutとresident buffer | display device/pipe/scanout。trial集計はtests | callback stackに依存する長寿命bufferの消失 |
| i915_vk_object_entry/table、gfx_memories、inst_token | render/object.c / session memory list / instance.c非所有token | cross-process ID衝突、free(static token)、memory detach漏れ |
| gfx_session/gfx_batch/gfx_kernels、GFX_* heap配置 | render内部型/state.h/batch.h/pipeline | stateとshaderのoffset不一致、draw/blitで同一buffer破壊 |
| gfx_rect_fill_kernel/copy_kernel | render/blit.cのdevice/target cacheと参照寿命 | device差のbinary誤再利用、close競合/リーク |
| gfx_eot_only | data/render-eot.inc、render/stateとblitが参照 | production rectのinstruction heap初期化を失う |
| shader_binaryのpayload/URB/push/input metadata | compiler/compiler.h、利用者pipeline/state | codeだけ正しくてもvertex/fragmentのレジスタ契約が崩れる |
| DPLL/CDCLK/PHY/WA/MOCS/PCI/EDID表、register macros、firmware bytes | dataの機能別fragment＋各所有型 | 機種選択、電圧/clock、cache属性、出典消失 |
| callback initializerの関数アドレス・inline・iterator/lock macro | 対応owner header/TU、ops storage | 通常のcall検索には出ない依存。staticを別TUへ切れない |
| generated codecとvkc arena、generated shader fixture | production codecはdata/codec、参照shaderはtests | generatorで旧path復活、arena解放後参照、kernel codecとの乖離 |
| copyright/SPDX/出典SHA/抽出manifest | 各断片＋data/provenance | 一枚岩化しても出典を消してよいわけではない |

全ファイルを分類した[保全台帳](i915-refactoring-assets.md)は、
これら以外の変数・型・macroを捨ててよいというallowlistではない。
各旧ファイルを削除する前に、本文全体の差分で「移動／統合／明示廃止」の行き先を確認する。

## 5. 実装段階の受入条件（まだ実行していない）

| 段階 | 必要な検証 / 合格の基準 |
| --- | --- |
| 起動分離 | test無効でnode公開、register→readyとready→register、起動失敗回収、公開後EBUSY保持 |
| ファイル移動 | 378ファイルの全要素の対応、H/E/I/O/S/T再分類、static/変数/typedef/macro衝突、callback initializer、生成include順序 |
| wire境界 | 通常/大stream、reply増大、bad resource_id/offset、trailer未到達、副作用後失敗、未知opcode、arena pointer保存禁止 |
| session/memory | 同じwire_idを別processで使用、同じopenの複数VkDevice、blob_id=0/nonzero、map後close、export元終了後import |
| 実GPU描画 | 既存vkdemo/libvulkan無改造、compiler metadata照合、E-128の独立画素oracle、E-130のGPU copy/flip |
| 別表示open | descriptor/lease/constraints、二つのVM、alias→panel PAT3、通常CPU転送なし、COPY拒否/選択の失敗分類 |
| job/停止 | 予約後producer停止、native前後cancel、GPU hang、同期blit中close、callback中close、scanout保持、停止未確認の隔離 |
| 構成別link | i915なし、通常native、resident render-only、resident display、test有無。全TU/外部caller/include/generator出力を確認 |
| 一般libvulkan機能 | Wayland renderer import、external memory/fence、semaphore/idleは現行制限を明示し、実装した範囲のみ別途受入 |

## 6. 今回実際に確認したこと／未確認

実施: ソースと上記U/K経路の静的追跡、2,988行の再照合、独立関数形抽出、
378ファイルのhash/配置索引、6,648個のソース上のdefine出現の計数、
配置・宣言先変更74定義の反映、static跨ぎ再集計201辺、同一配置先の同名定義6組、文書リンク/表/空白検査。
6,648は条件別・重複名を含む出現数で、全展開を検証した数ではない。

未実施: preprocess後の全symbol/型/マクロ展開の完全監査、全137 core＋拡張APIの意味論検証、
build/link、compiler再生成、kernel/実GPUの試験。従って「移動漏れゼロを証明」「全workflowが動く」
「実装へ即着手できる最終設計」とは報告しない。

[Awesome Plan](../../docs/agent/awesome-plan/awesome-plan.md)に従い、設計レビューと実装・受入を分離した。
WS/Phase/Queue/Masterの状態は変更せず、この資料は共同レビュー用のローカル草案とする。
