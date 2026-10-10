<!-- awesome-plan project=zedbsd record=ws141-p010 -->

# ws141-p010: critical core clockと実時間のV3D期限

Status: cleared
Disposition: normal
Parent: [WS141](../ws.md)
Queue: ws141-core-timeout-20261011 / finished / Codex独立担当

## 承認・有限範囲

ユーザーのN0 core clock preparation failed(13)、V10待機写真、その後の「お、待ってたら抜けて動きました。」を既存WS141の修正指示へ反映する。GPU driver内部のcore clock状態解釈とV3D jobの待機期限、対象host試験、arm64 build、変更source全文規約再確認を実施する。main統合は具体的commit承認後。実機描画成功・全WS完了をこの写真から推定しない。

| Attempt | Scope | State | Prerequisite |
| --- | --- | --- | --- |
| core-timeout-i01 | p010 software修正/対象host/build | cleared | main211be00dc、今回実機写真/復帰報告 |
| core-timeout-i02 | p007変更差分準拠確認 | cleared（software） | i01 |

## 観測と設計

- 写真でV3〜V9通過、clear4096pixelおよびTFU64pixel一致。ユーザーが待機後の復帰を確認。V10フリーズとは断定しない。
- coreはLinux firmware clock driverでCLK_IS_CRITICAL（バス共有、常時必要）。GET_STATEのgate flagがoffでもGET_RATEは200MHz。coreの実用rateをゲート状態だけで0へ変換する独自判定を取り除く。HSM/V3D等のgate確認は保持し、providerエラー/存在/zero rateは拒否する。
- kern_usleep_rangeはtick単位でyieldする。10us×50000回を500msとみなしたjob deadlineはtick1msなら50sへ伸びる。各progress windowを実counterの500msで判定し、最大4windowと既存有限iteration guardを保持。時間源の欠落/変化はEIO、DMA未退役の保持を維持する。
- V10 timeout完了とreset開始をログで区別する。reset自体の手順変更/診断削除は対象外。

適用: AGENTS.md、Guardrail、C全文規約。変更範囲はBCM2711 GPU内部、plan/ws141。HAL/UAPI/toolchain変更なし。固定Linux clk-raspberrypi.cのcritical coreとprepare/get-rateを動作参照するのみ、コードを転用しない。共有Master/Queue/cache/GitHubはQ1担当。

検証: 実core gate-off/rate-presentモデルとHSM拒否、実jobの1ms/10ms刻み/時間源異常の期限とDMA保持、arm64 warning0、全文規約レビュー。実機の最終scanout/Keilandは未確認でWS incomplete保持。

## 追加の実機証拠（2026-10-11）

ユーザーの続報に `V10 reset 1 post-reset noop complete` および `render node ready native Vulkan/strict queue` を確認。V7 noop、V8 RGBA8全4096pixel、V9 TFU全64pixel、V10 reset/post-reset noopは実機ログ上で成功。停止故障とした仮説は取り下げ、過長待機だけを修正する。displayのcore ENODEVとKeiland実画面は未解消/未確認。

## software結果

core leaf-gate判定を限定修正し、V3D jobを実counterの500ms windowへ変更。期限後のV10 reset開始を明示。provider10ケースと既存actual V3D hardware/job host（1ms/10ms tickで各500ms、counter欠落/周波数変化でEIO・DMA保持）がPASS。arm64 kernel/raw-image build/check warning0/error0。全文規約と最終差分確認を完了。有限Queue finished。main統合は承認待ち、displayの実機再確認は未実施。[証拠](evidence.md)。

## 自動統合の承認と統合結果（2026-10-11）

ユーザー「mainへのコミットは自動でお願いします。」、確認回答「今後は自動で統合してください。」により、このセッションの承認済み作業のmainコミット/統合は以後自動実施する。従来のQ1担当による個別main統合確認をこの指示で置換する。pushや実機更新の指示ではない。共有記録への投影は引き続きQ1へ保留。

USB `3e9ace7b9` とGPU `461906448` を専用worktreeでmerge `afe01157f` にまとめ、現在のconfig.mkで `make -j16 build/arm64/vmunix` を実行。warning0/error0、arm64 ELF/raw-image check PASS。USB/GPU sourceが各個別検証版と一致することも確認。clean main `211be00dc` を `afe01157f` へfast-forwardし、変更17path全てが検証済みworktreeとbyte一致した。main統合完了。実機の新core判定/待ち時間/USBログ抑制は未確認で、WSの残る実機条件は保持。push・実機更新/再起動は未実施。

最新USBログでdevice1/port1 `2109:3431` class09、usb-hub、4portsの登録を確認し、slot1の反復Stopped通知がhub監視と整合することを確認。USBキーボードの入力成功を意味しない。GPUは旧版のV7〜V10成功とrender node登録までユーザー確認済み。
