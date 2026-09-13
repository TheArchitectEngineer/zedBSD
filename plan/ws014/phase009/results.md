# WS014 p009: GPUレビュー対応とフレームワーク共通化

状態: 実装・統合検証中。q312-i01 / p009をまだclearedにしない。開始時のGitHub同期は[start-sync.json](start-sync.json)、Phaseは[#396](https://github.com/awemorris/zedBSD/issues/396)。source、patch、資料のgit add/commit/pushはユーザーが行う。

## 実装の責務

| 範囲 | 共通GPUフレームワーク | Venus backend / libvulkan |
| --- | --- | --- |
| 容量と待機 | device/openの容量世代、QUERY→WAIT、終了・割込み・故障、observerとcallback参照の保持 | backendは実slotのtry-reserveとFREE通知。Uは必要なmutexを外して待ち、取得後に状態・世代を再確認 |
| job監督 | 予約10秒・実行60秒・停止10秒の既定値と設定/実効値照会、session単位のsticky error、有限停止と全体故障への移行 | backendはnative投稿・完了、contextの停止確認、隔離、checked resetを提供 |
| 資源寿命 | 論理ERRORだけではcallback所有権を落とさない。closeも停止確認または隔離の後に資源破棄へ進む | backendは実DMA停止を証明し、不明な資源を再利用しない。native fence完了とhost callbackの最後の使用を分ける |
| direct WSI | 既存fdの故障・表示変更通知を提供 | Uは画像不足時だけwaiter固有の起床pipeを作り、画像返却・故障・表示変更を同時に待つ |
| NULL fence | job completionと容量通知を供給 | terminal private fenceをまとめてresetし、READYを再利用。PREPARING中のproofを競合submitへ貸さない |

内部opsはversion8、新しい容量照会/待機は48byte、実効期限照会は40byte。既存UAPIのlayoutを変更せず追加した。GPU固有のfenceはdrv_gpu内に保持し、汎用kernの機能を増やしていない。timelineのVenus固有上限もbackend検証へ移した。

詳細は[共通契約](gpu-job-contract.md)、[U同期・WSI](userland-sync-wsi.md)、[transport](transport-sync.md)、[stock互換の検証](stock-compat/README.md)を参照する。

## 検証の読み方

| 証拠 | 確認すること | 証拠の限界 |
| --- | --- | --- |
| [共通層の限定試験](gpu-core-verification/verification.json) | 実productionコードの容量枯渇、observer、late callback、期限、raw close、reset中の再故障、GPUなしbuild | fixtureの停止ACKは実native GPUの停止証明ではない |
| [U限定試験](userland-verification/verification.json) / [最終admission境界](userland-verification/admission-boundary.json) | 3mutexを外した待機、再試行所有権、reset batch、実ppoll/pipe2のlost wake・複数waiter・故障 | native peerのtransaction計数をそのまま実GPUの速度改善率へ外挿しない |
| [公開API検証](api-verification/verification.json) / [API family](family-verification/verification.json) | 170 API、両ABI、Noct8file再生成、対象familyの限定検証 | Vulkan CTSの合格を意味しない |
| [renderer停止の提案](renderer-quiesce/README.md) | raw streamを含むnative idleとcallback退役の停止契約 | 提案の適用・意味fixture・最終VMの状態を別々に記録する |

## 途中で見つかった問題

- 最初の複数process VMは`vkCreateDevice`で失敗した。backendの静的capabilityに新しい容量能力のbitが漏れていたためで、広告と実get_infoのfixtureを修正した。submitが正常に処理された試験とは数えない。
- managed job callbackがすべて退役していても、raw opaque streamが未追跡native仕事を投稿している場合がある。一般UAPIの停止証明をライブラリの使い方に依存させず、context全体のnative idle確認を追加する方針とした。
- 共通層のraw-only close、monitor起動失敗、withdrawal、reset中の再故障は、再現結果と修正後結果を保存した。正常なcloseでも停止確認前に資源を破棄しない。
- Uのcontext故障を無条件にdevice故障へ広げた途中実装は既存Wayland試験で検出し、通知対象とerrorの範囲を修正した。
- production Venus停止処理のローカル適用が自動承認レビューで一度拒否された。操作の実際の範囲、既存の実行承認、具体差分、限定検証を[承認文脈](renderer-quiesce/approval-context.md)へ記録し、適用と検証の結果を混同しない。
- 2構成のイメージを同時buildした際、異なるBUILD directoryでも共有される`build/amd64/sysroot`の生成処理が競合した。生成済みsysrootを`make sysroot-amd64`で再構築し、イメージbuildを逐次実行して両構成PASSを確認した。GPU実装の失敗や修正として数えず、初回logも保持する。

## 現在の制限

stock virglrenderer1.1.0のcallbackには、OOMやproxy断を実GPU停止と区別できない経路があり、必要なOPAQUE共有契約も不足する。strict pairを維持する。stockの通常描画が常に壊れる、またはDEVICE_LOST時のfence SUCCESSがVulkan違反だとは主張しない。

実行期限60秒は運用既定値であり、正当な長時間computeにも適用される。停止能力を持たないbackend、停止ACKを確認できない場合、共有transportの故障では全体隔離・checked resetへ進む。任意GPU仕事を安全に強制cancelする実装ではない。

直接/Wayland表示は通常BLOB scanoutとGPU内共有を維持する。一般Wayland/toolkit、任意GPU間のDMA、native i915、CTSはこのPhaseの受入ではない。HAL、host system package、GDM、VFIOは変更対象にしない。
