<!-- awesome-plan project=zedbsd record=ws141-p007 -->

# ws141-p007: 全規約・license/類似・最終確認

Status: cleared
Disposition: normal
Parent: [WS141](../ws.md)
Queue: [完成までの承認と有限実行scope](../execution-20261009.md#完成までの継続承認2026-10-09)

## scope/criteriaと依存

全WSのchanged sourceを全文規約/ライセンス/GPL字面/設計類似/BLOBで確認、最終buildとhostを確認して最新mainへ統合。実機未実施の関門は保持。

依存: p003〜p006の最終changed source。software実装の依存と実機のwhole acceptanceを区別する。実機はユーザーが後で行うと回答済み。

## 規則と確認

C全文 `plan/coding-style.md`、Guardrailのsource/ownership/HAL/GPL/scanout規則を適用。固定Linux/Mesa一次sourceを照合し、GPL資料/改名表はignored tempのみ。named rpi4 build（warning/error0）と必要な短いhost検証、formatter/style補助+全文manualを実施。QEMUはQ1/T1、Master/共有記録は担当から更新しない。

## 設計変更の出典（2026-10-09）

ユーザー「完成まで自走してください。」と回答「WS141に実行器・compilerも含め、Keiland表示まで進める」。p006の別WS判断を本WS内実装に確定し、p005はこの出力をdesktop描画の依存とする。実機関門を削除しない。[全体変更/実行範囲](../execution-20261009.md#完成までの継続承認2026-10-09)。

## i15開始（2026-10-10）

public runtimeを含む最終software sourceの監査を開始。全文C/Guardrailを適用し、最終LTOのcaller/IRQ/間接callback・再帰上限、全WS規約/license/GPL字面・設計/BLOB、actual clientの二node/scanout契約を確認する。実機のwhole acceptanceは保持する。詳細・結果は[実行記録](../execution-20261009.md)。

## 全source software確認結果（2026-10-10）

[全文audit](../p007-software-audit.md)・[183path final hash](../final-source-sha256.tsv)・[16KiB ordinary stack](../final-stack-report.json)を保存。規約のguard/return/macro、予備fragment preflight、stack一時table、独自filter生成を修正。全14host/y-n build/Python・shell/diff PASS。final source監査に未修正code残件無し。main統合後にi15/p007を判定する。physical全関門は他Phase/WSに保持し、hostをKeiland画面成功としない。

## clearance（2026-10-10）

i15の最終source監査・build/host・main統合criteriaを満たしcleared。source0f56e5200、main merge5eb8867f3、183path hash一致、ordinary16KiB stack/y-n build/全14host PASS。[統合と終了記録](../execution-20261009.md#i15-main統合software実行の終了2026-10-10)。実機/KeilandとWS acceptanceは未達のまま保持し、remote closure/projectionはQ1へ残す。

## source変更による再開（2026-10-10）

ユーザーの共通shader refactor指示によりi15の最終source適用は失効、unclearedとして記録して今回i18へ再開。旧clearanceの結果/証拠は保持。新共通frontend・vendor caller/build依存を全文規約/境界/build/host/ARM64 stackで再確認する。[今回実行](../compiler-refactor-20261010.md)。

## i18最終source検証（2026-10-10）

共通compilerとi915/VC4 callerを再確認し、全文C/境界/Zlib origin、RPi4 y/n・amd64 i915 y build、対象host、final普通経路stack15680/margin704を確認。[今回commands/results/hash](../compiler-refactor-20261010.md#i17-software-clearancei18最終検証)。main統合/readbackまではin-progress、実機のwhole条件は保持。


## i18 clearance・main統合（2026-10-10）

今回最終sourceの全文規約/境界/license、対象build/host/ordinary stackとmain統合/readbackのcriteriaを満たしi18/p007をcleared。source `fca9cd37f`、main親 `0206f4f88`、merge `ab268b857`。対象212/212 source hash一致、build/host入力差分0、main/own/integration cleanを確認した。[今回終了と残件](../compiler-refactor-20261010.md#i18-main統合今回software実行の終了2026-10-10)。旧i15 clearance・source変更による失効/再開の履歴は保持する。RPi4/Keilandの実機条件は他Phase/WSに未達のまま保持し、remote closure/projectionはQ1へ保留する。

## 実機初期化の追加修正（2026-10-11）

[p009](../phase009/phase.md)でN0のclock preparationとV3のVA35識別を修正する。p003/p004の実機受け入れは保持し、p007の今回変更sourceの準拠/buildを再確認する。旧software検証履歴は保持し、今回結果は別attemptへ記録する。共有投影はQ1。

## p009変更差分の準拠再確認（2026-10-11）

今回のC全文規約/境界/独自実装と対象host/buildを確認し、gpu-init-i02のsoftware部分をcleared。p007はmain統合条件の確認までin-progress。i18以前の監査履歴を保持、無関係な全回帰は再実行しない。[対象/結果](../phase009/evidence.md)。

## main統合・read-back（2026-10-11）

ユーザー「mainに統合してください」でsource commit `3e0ef9b456500c0caebf238d5210236948090939` の統合を承認。clean main `091c65e4a` から当該commitへfast-forwardし、変更12pathとconfig.mkが検証済みworktreeとbyte一致すること、evidenceに記録したsource/config/kernel hashが一致することを確認。警告0のarm64 buildと対象host結果がそのまま適用されるため、同一sourceの再buildは実施しない。

main統合完了。p007の今回統合条件も満たしcleared、p009のsoftware clearanceを保持。実機のN0/V4/V5/V6進行とKeiland表示は未確認でWS incompleteを保持する。push・実機更新/再起動は未実施。Master/共有Queue/history/cache/GitHub投影はQ1へ保留。以前の統合承認待ちの記述は当時の履歴。

## p010 follow-up（2026-10-11）

[p010](../phase010/phase.md)でcore clock gateの解釈と実時間job期限を修正する。実機のV3〜V9通過およびV10待機後復帰を記録し、未確認のscanout/Keiland条件は保持。p007は今回変更差分を再監査する。

## p010変更差分の準拠確認（2026-10-11）

[今回evidence](../phase010/evidence.md)のsource範囲についてC全文規約・provider境界・counter障害時DMA寿命とhost/buildを確認。core-timeout-i02 software範囲はcleared、main統合条件待ちでp007 in-progress。過去の監査履歴と未確認の実機表示条件は保持。

## 自動統合の承認と統合結果（2026-10-11）

ユーザー「mainへのコミットは自動でお願いします。」、確認回答「今後は自動で統合してください。」により、このセッションの承認済み作業のmainコミット/統合は以後自動実施する。従来のQ1担当による個別main統合確認をこの指示で置換する。pushや実機更新の指示ではない。共有記録への投影は引き続きQ1へ保留。

USB `3e9ace7b9` とGPU `461906448` を専用worktreeでmerge `afe01157f` にまとめ、現在のconfig.mkで `make -j16 build/arm64/vmunix` を実行。warning0/error0、arm64 ELF/raw-image check PASS。USB/GPU sourceが各個別検証版と一致することも確認。clean main `211be00dc` を `afe01157f` へfast-forwardし、変更17path全てが検証済みworktreeとbyte一致した。main統合完了。実機の新core判定/待ち時間/USBログ抑制は未確認で、WSの残る実機条件は保持。push・実機更新/再起動は未実施。

最新USBログでdevice1/port1 `2109:3431` class09、usb-hub、4portsの登録を確認し、slot1の反復Stopped通知がhub監視と整合することを確認。USBキーボードの入力成功を意味しない。GPUは旧版のV7〜V10成功とrender node登録までユーザー確認済み。
