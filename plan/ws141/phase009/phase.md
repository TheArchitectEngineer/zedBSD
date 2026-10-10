<!-- awesome-plan project=zedbsd record=ws141-p009 -->

# ws141-p009: 実機のN0クロックとV3アドレス幅による初期化停止

Status: cleared
Disposition: normal
Parent: [WS141](../ws.md)
Queue: ws141-gpu-init-20261011（独立Codex担当、finished）

## 承認・有限scope

2026-10-11の実機GPUログ提供と、既存のWS141実装・Linux同等再初期化・継続修正指示に基づく。今回の範囲はN0のclock preparationとV3のPA35/VA35識別、対象host試験、arm64 kernel build、変更sourceの全文規約確認。main統合は具体的commitの承認後。実機描画/Keiland成功は本attemptでは判定しない。

| Attempt | Scope | Status | Dependency |
| --- | --- | --- | --- |
| gpu-init-i01 | p009の2停止条件のsoftware修正と確認 | cleared | main 091c65e4aのWS141実装と今回実機ログ |
| gpu-init-i02 | p007の今回差分の全文規約再確認 | cleared（software、main統合は保留） | gpu-init-i01 |

Dependency: main既存実装 → i01 → i02。今回調査は2停止条件の原因と直接の初期化順序に限定。未知の実機停止は結果・再開条件として残す。

## 観測・設計

- firmware revision6a7a16af、framebuffer1920x1080、HSM(id13)off/0HzでN0がENODEV。core(id4)もoff/200MHz。既存のCLOCK_ONはreadout後なので到達しない。
- V3D4.2 / 1core / PA35 VA35を識別後、VA==32固定判定でENOTSUP(21)。Linuxは能力幅を識別しつつ、ソフトウェアが下位4GiBと4MiBのpage tableを使う。
- HSMは最低120MHzを確保してからenable/readback。coreは既存の非ゼロrateを保持してenable。HSMの既存高いrateを下げない。MMIOは確認成功後だけ。boot HDMI選択/firmware handoff/resetの後続手順は既存経路を使う。
- V3Dの能力幅は32bit以上を受け入れる。発行VA/allocator/PTE範囲は32bitのまま。Linuxにない推測のADDR_CAP設定やtable拡大は導入しない。

## 規則・参照

AGENTS.md、[Guardrail](../../guardrail.md)、[C全文規約](../../coding-style.md)を適用。sourceはsrc/drivers/gpu/bcm2711内と本WS記録/試験のみ。HAL API/UAPI/toolchain変更なし。Master/共有Queue/Guardrail/GitHub投影はQ1担当、未更新。

読み取り専用の固定Linux資料は別worktree ws141-codex/plan/ws141/temp/linux内のdrivers/clk/bcm/clk-raspberrypi.c、drivers/gpu/drm/vc4/vc4_hdmi.c、drivers/gpu/drm/v3d/{v3d_drv.c,v3d_gem.c,v3d_mmu.c,v3d_regs.h}。動作/数値の参照のみ、GPLコードを転用しない。
公式mailbox仕様: https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface

## 検証予定・残件

clock provider request順序/zero-rate/拒否応答、実機PA35/VA35でMMUまで進むhost fixture、arm64 vmunix warning0、git diff --check、全文規約の今回差分レビュー。QEMU/実機更新は未実施。p003/p004/p005の実機条件とWS incompleteは保持。

## software結果（2026-10-11）

N0のHSM/core準備とV3の能力幅判定を修正。9件の実provider-helper試験と、既存V3D hardware試験（実機PA35/VA35・狭いVA拒否を追加）がPASS。arm64 kernel/raw-image検査PASS、warning/error0。今回C全文規約/境界/独自実装レビューとdiff-checkを完了。i01/i02の有限software範囲をcleared、Queue finished。main統合と実機確認は未実施で、p007の統合条件は保留。Keiland表示成功とは判定しない。

再開条件: 具体的commitのmain統合承認、次いでユーザーの実機起動。期待ログは `N0 clocks ready hdmi ... core ... Hz` とV3以後のV4/V5/V6。次の停止があればその直前からのログで追跡する。[commands/hash/限界](evidence.md)。
