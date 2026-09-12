<!-- awesome-plan project=zedbsd record=ws014-p004 -->

# WS014 p004: 最終API整理・規約全文確認

<!-- awesome-plan-current:start -->
Status: planning
Phase disposition: normal
Parent: [WS014](https://github.com/awemorris/zedBSD/issues/15)
Queue: none
Dependencies: cleared ws014-p006 final GPU-sharing/Wayland output; corrected ws014-p005 and ws030-p004 accepted library
<!-- awesome-plan-current:end -->

Combined ID: `ws014-p004`
Primary Milestone: MG006

## 目標・依存

p002/p003/p005/p006で修正された最終ソースとU/K・callback・PCI連携資料を照合し、WS014の受け入れを確認する。p003の描画基盤に加え、p005のテクスチャ付き3D shader描画とp006のkernel handle・GPU共有・最小Wayland WSIの必要出力が成立してから行う。

## 手順・受け入れ

適用するcoding-style.md/Guardrail全文を読み、変更範囲の規約・層分け・所有権・参照寿命・エラー経路をレビューして残る問題を解決する。公開するversion/feature、必須/任意callback、未対応機能を整理する。p003/p005/p006の最終ソースでのbuild・実画面取得ループ証拠を確認し、修正によって必要な限定回帰だけを行う。変更がない場合は既存の有効な検証を無意味に繰り返さない。

## 引き渡し

既知の制限、API差分、再現手順、host構成を後続i915 WSへ渡す。WS014完了は本Phaseの終了だけで自動判定せず、framework＋virtio/Venusで宣言した表示経路の受け入れを確認する。現在未着手。

## 適用規約・実行境界

[Guardrail](https://github.com/awemorris/zedBSD/issues/363)とローカルplan/coding-style.mdの全文を実装前に読む。HAL責務/hal.hの変更は別途適用承認が必要。既存PCI/VFS/VMの責務を確認し、大規模refactor前の配置を仮定しない。aggregate make checkは禁止。必要な対象buildはmake -j16と意味のある限定確認を用いる。無関係な変更を保護する。

ユーザーは計画・GitHub公開を指示した。まだ有限Queue、実行範囲と調査上限は選択していない。コード実装/build/QEMUは未実行。資料のgit add/commitはユーザーが行うためエージェントはadd/commit/pushしない。

## q307開始: p005をp004の前へ追加（2026-09-13）

ユーザーがテクスチャ付きの回転直方体デモをuserland/base/vkdemoとして作り、vertex/fragment shaderとAPI不足を確認するよう依頼。[p005](https://github.com/awemorris/zedBSD/issues/387)を追加し、p003 cleared → p005 → p004の順とする。q307/q307-i01はp005だけを実行。p003/q306のclear/終了は維持し、p004とnative i915は未実行。

独自GLSL→SPIR-V、実texture/depth/graphics pipeline、時間の進む同一process、GPU readbackとVNC実画面、独立した幾何/texture照合で確認する。既存GPU APIを再利用し、必要なU共通化と実測された不足だけを補う。HALの追加変更は未許可。見積240 active minutes、120分ごとの点検、有限build/VM/pollを適用する。GitHub同期はユーザー明示承認済み、git add/commit/pushはユーザーが行う。

## p005からの引き渡し（q307完了）

[p005](https://github.com/awemorris/zedBSD/issues/387)の3D shader/texture/depthと連続frame・正常終了・同VM再openが成立した。追加U graphics API、共通session client、Kの非同期unmap待機修正を最終API・規約レビューへ含める。新ioctl/HAL変更なし。全Vulkan/libvulkan/WSI適合とnative i915は完了と解釈しない。本p004はplanningであり、次の有限Queueが選ばれるまで未実行。

## q308による依存の更新

[p005](https://github.com/awemorris/zedBSD/issues/387)の旧clearは標準API要求により失効した。標準API化したp005の実測と [WS030の最終library契約/意味論/規約結果](https://github.com/awemorris/zedBSD/issues/392) を待ってから本Phaseへ進む。本p004はplanning、q308には含めず未実行。旧q307のgraphics/wait修正は履歴証拠として保持する。全Vulkan1.0/shared libraryとdirect-display実装はWS030、native i915は後段のWS029が所有する。

## q308の標準API・console契約の引き渡し

WS030はVulkan1.0 core137＋direct-display WSI18と/lib/libvulkan.soを完成し、WS014 p005は標準APIだけのアプリへ訂正済み。q308-lifecycle-003で実描画6枚、正常/異常終了後の再open、640×480 console復帰・文字更新、別process表示競合拒否を確認した。GPU dynamic resources/device mmap/共有VM、PCI cache契約、native display lease/virtual FIFO、文字snapshot/workerを最終framework/API確認の入力にする。

詳細は [WS030](https://github.com/awemorris/zedBSD/issues/388) と [p005](https://github.com/awemorris/zedBSD/issues/387) のq308結果。本p004はplanning・未queueのまま。WS030 p004の標準library受け入れを本p004のclearanceへ流用しない。native i915は別WS029。HALは既承認patchを超えて変更しない。

## WS014 p006追加: kernel handle・GPU共有・最小Wayland（2026-09-13）

ユーザー指定により[WS014 p006](https://github.com/awemorris/zedBSD/issues/393)を一つのplanned Phaseとして追加した。kernel_handle/handle_fd_*とSCM_RIGHTS、GPU/Venusの別context共有、GPU画像を扱えるWSI、VK_KHR_wayland_surface、最小client library、全画面zwl、標準APIのwltestを本Phaseで実装・検証する計画。コード配置はlibc/include/wayland/、userland/base/libwayland/・zwl/・wltest/、公開libraryは/lib/libwayland-client.so。

中核のK/driver実装を先に進め、Wayland通信/WSI/試験アプリを接続して実測から設計を改善する。新経路はCPU readbackを必須にせず、GPU allocationの実共有と同期・寿命を確認する。linux-dmabuf-v1、ゲストdma-buf/DRM、EGL、一般DEは採用しない。内部の段取りは別Phaseへ分割しない。

順序はp005 cleared → p006 planned → p004 planning。p004はp006の最終ソース/API/検証を受けて規約確認する。WS030 completedとq308 finished、既存Phaseのclearを維持。今回作成したのは計画であり、active Queue・新しい実装/試験結果はない。HALの追加差分は従来どおり個別承認、git add/commit/pushはユーザー担当。

## q309完了: GPU handle共有・Wayland WSI・virtio scanout（2026-09-13）

WS014 p006 / q309-i01をcleared、q309をfinishedとする。active Queueなし。WS014はincomplete、p001/p004 planning、p004未queue。WS030 completedと既存Phaseのclearanceを維持し、native i915は別WS029のまま。

kernel_handle/handle_fd_*と共通fd参照・SCM_RIGHTS、GPU/Venusの独立process/context共有、VK_KHR_wayland_surface、最小libwayland-client.so、全画面zwl、標準Wayland/Vulkanアプリwltestを実装した。共有GPU imageはGPU copyと所有権同期を経て別processへ渡り、SET_SCANOUT_BLOB/RESOURCE_FLUSHで表示する。通常の新WSI経路にCPU readbackや再uploadを必須としない。旧copy経路もGOPではなくvirtio 2D scanoutであり、直接表示の互換経路として保持する。

GET_DISPLAY_INFOとGET_EDIDのbase/CTA progressive DTDで表示・モードを列挙。実QEMUでは1280×800、74,994mHz、320×200mmを取得した。custom framebuffer寸法はEDID寸法と独立に扱い、1〜100Hzのguest nominal pacingを検証する。virtioは物理pixel clockを設定しないため、物理vblank同期の保証とは区別する。

最終q309-wayland-004は43.869秒、QEMU exit0でPASS。FIFO/MAILBOX各6枚の320×240実VNC画像、計921,600画素が独立期待値と全画素一致し、12回の表示が実import資源とBLOB scanoutに対応した。生成元終了後の独立renderer import/GPU copyも検証専用readbackの1024画素が一致。swapchain再作成、client中断・再open、compositor通常終了・SIGKILL・再起動、実SURFACE_LOST/cleanup=0、強制終了直後の640×480 console復帰とechoによる画面更新を確認した。

同じ最終kernelのq309-direct-002も42.705秒、QEMU exit0でPASS。標準vkdemoの回転直方体6枚をGPU readback/VNC/独立oracleで照合し、通常終了とSIGINT後の再open、console復帰、表示競合拒否とowner完走を確認した。K/fd/SCM・GPU/EDID・Wayland/WSIの実コード限定fixtureとsanitizer、157 Vulkan dispatch/export、両ABIの公式header照合、Noct再生成、rootfs配置、対象buildと全適用規約を確認した。正式CTSや全Wayland SDK互換は主張しない。

途中のharness起動待ち不足とEDID/custom mode回帰を修正し、失敗証拠と再実行理由を保存した。最終reviewのMSG_PEEK二重put疑義は、rights付きpeekを既存guardが拒否するため到達不能と確認し、本体変更を戻して拒否後の参照寿命を追加検証した。formatterは規約と設定の不一致によりexit1でありPASSとは扱わず、全文確認とdiffcheckを記録した。HAL追加変更なし。ローカル結果はplan/ws014/phase006/results.md、技術資料3件、conformance.mdとfinal-evidence/verification.json、Queue履歴はplan/history/queue-q309.md。これらsource/doc/imageのgit add/commit/pushはユーザーが行う。本同期はGitHub Issues/Projectの計画・受入結果である。

受け取る資料はlocal/uncommitted plan/ws014/phase006/{results,gpu-sharing,kernel-compositor,wayland-implementation,conformance}.md とfinal-evidence/verification.json。drv_gpu_ops v4の必要境界と初版制約を再点検する。p004は自動実行・自動clearしない。
