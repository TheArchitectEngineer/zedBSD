# p008: consoleの変更通知

主画面を表示するVenus console workerは、100msごとの定期起床をやめ、文字更新・主画面の所有権変更・停止要求のwait queueで眠る。workerは従来どおり最初の主画面claimで遅延作成し、controllerの終了時にstop/reapする。claimごとのthread作成・joinは行わない。

## テキスト側の境界

`kern_text_observe`は、callerが所有する`kern_text_observer`とcondition lock/wait queueを登録する。`kern_text_unobserve`は同期的に登録を外し、進行中の通知が終わってから返る。登録に動的allocationやdriver callbackは使わない。汎用テキスト機能はGPUを知らず、変更後のgenerationを公開して登録済みqueueを起こすだけである。

通知は`text registry (LOCK_RANK_CONSOLE_TEXT)` → `subscriber condition (LOCK_RANK_POLL)` → schedulerの順に行う。backendの文字描画lockは通知前に解放済みで、klogもring lockを解放してから文字sinkへ渡す。登録・解除側はsubscriber lockを保持しない。observerとそのlock/queueのstorageはunobserveが返るまで保持する。通常のIRQ通知にsleep・GPU command・controller mutex取得を持ち込まない。

## 表示側の境界

| 状態・通知 | 処理 |
| --- | --- |
| 主画面のnative lease取得 | text subscriptionを外す。workerは所有権通知を処理して期限なしで眠る。占有中の文字出力ではworkerを起こさない。 |
| legacy scanout-zero取得 | 同じ所有権hookでsubscriptionを外す。 |
| primary release・owner終了・legacy owner終了 | subscriptionを登録し、queueを起こす。文字generationが変わっていなくてもconsoleを復帰する。 |
| 他出力のclaim | 主画面のconsoleを止めない。 |
| 表示中の文字更新 | 世代公開後にwait queueへ通知し、GPUの通常2D console経路でsnapshotを描画する。アプリのBLOB経路とは別である。 |
| detach stop | atomic stopを立て、subscriptionを同期解除し、queueを起こしてから既存の有限reapを行う。 |

workerはqueueのsequenceを読んでから所有権・text generationを確認し、描画後に同じsequenceでsleep登録する。描画中に文字が変わればsequenceが進むためsleepを拒否し、次の世代を描画する。stopも最後のsleep登録前に再確認する。GPUのpacing待ちでは従来どおりcontroller mutexを解放し、再取得後にleaseを再確認する。

失敗時は変化したエラーを一度報告して次の通知を待つ。新しい文字・所有権変更で再評価する。controllerのstop/reap期限は維持する。GPU故障時のDMA quarantineを、通知やfenceのERRORだけで解除しない。

## 限定検証

次を通常版とASan/UBSanで確認した。これはホスト上の実コードfixtureであり、実QEMUのconsole復帰とは区別する。最終ソースのhashと実行logは最終検証台帳へ保存する。

- `run-text-snapshot-test.sh`: 既存のretained glyph/cursor/BGRA snapshot、MMIO非アクセス、generation、fallbackを維持。
- `run-text-notification-test.sh`: 実`text-display.c`で2consumerへの通知、二重登録・逆lock順位の拒否、unsubscribe後の無通知、連続writerと1,000回の登録→解除→即freeを並行実行。解除後アクセスをsanitizerで検査。
- `run-venus-console-test.sh`: 実display/resourceコードでprimary占有中のsubscription解除と期限0のwait、release/legacy close後の復帰、描画中のgeneration変更によるsleep拒否、停止/reap、timeout時のDMA保持を確認。

新HAL API・HALソース変更は行っていない。現在の2D文字表示にGPUレンダリング画像を戻す変更ではなく、console自体の変更通知の改善である。
