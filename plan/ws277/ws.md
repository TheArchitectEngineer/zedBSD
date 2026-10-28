<!-- awesome-plan project=zedbsd record=ws277 -->

# WS277 VisionFive 2 の DC8200 の display driver（zero copy の scanout）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: DC8200 の zero copy の flip と hardware cursor が板で動く（beta2.md U36 の 2026-10-21 09:55）。p002・p003・p005 は実装済みで実機の行の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG008
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

VF2 の DC8200 と HDMI（inno_hdmi）を zedBSD の driver で初期化し、WS269 の display の node（gpu0）を置き換えて、PowerVR が線形で書いた render の buffer を CPU の copy 無しで scanout する（import・page flip・vblank）。設計は [design.md](design.md)（改訂 3.1、追補 版 3.2 §15、[design-review-1.md](design-review-1.md)・[design-review-2.md](design-review-2.md)・[design-review-3.md](design-review-3.md)）。HAL の案は [proposed/hal-riscv64-late-framebuffer.md](proposed/hal-riscv64-late-framebuffer.md)。

## ユーザーの決定

- 2026-10-20（起点）「PowerVRはタイルとRGBAと両方の書き出しができると思いますが、VF2のLinuxドライバはもしかするとタイルしかないかも？だからスキャンアウトのバッファにCPUでコピーしてて、GNOMEがむちゃくちゃ遅くて使い物にならなかったと思います。」→（クリック）DC8200 の自前の driver は「新しい WS で設計から」。
- 2026-10-20 03:25（クリック）: D1〜D11 は全部 (a)（推奨どおり）。原文は当時の Master の decisions-log（2026-10-27 に削除、git の履歴）。

## 結果

- Q1 が確かめた事（~/visionfive-linux、vendor の 6.12）: DC8200 の plane の modifier は `DRM_FORMAT_MOD_LINEAR` と Vivante の tile・DEC の形で、PowerVR の twiddled・tiled は無い。よって zero copy には PowerVR が線形で書く事が要る（WS269・WS270 で線形の書き出しと zero copy の scanout を入れた）。
- p002（go/no-go の attach、U-Boot の plane の引き継ぎの判定・snapshot）、p003（引き継ぎの zero copy: vblank の IRQ 95、commit の道、s0 + 2 の latch、flip の記録の環）、p005（自前の mode: HDMI の PHY・PLL、DC8200 の panel・timing・plane、GOP の無い時の電源・clock・reset）を実装。VF2・riscv64-virt・amd64 の build warning 0、host 試験 PASS（p002 16、p003 24 と 7 checks、p005 20）。
- 配布の U-Boot は console を HDMI に出さず、ExitBootServices で表示を消す。2026-10-20 06:11 ユーザーの VF2 の実機で、p005 の自前の点灯（PD_VOUT・SYS と VOUT の clock・reset の順、bootfb の memory を 1920x1080@60 で scanout）で画面が出た（「画面がつきました！」、BUG-341）。
- q1529（2026-10-21、P2）: own mode での直接 scanout（lease の claim の時の late の vblank start）、`kern_irq_unmask` の追加、診断の report が DC の AQ_INTR_ACKNOWLEDGE（0x10）を読んで割り込みを消していた誤りの直し → run 22 で `frames scanned directly`。DC8200 の cursor plane（64x64 ARGB、ADDRESS 0x146C・LOCATION 0x1470・CONFIG 0x1468）と display の ops の `cursor_set`・`cursor_move`・`cursor_hide`。
- 板（beta2.md U36、2026-10-21 09:55）: zero copy の flip と hardware cursor が動く。

## 制限・移管

- U0〜U11 の行ごとの実機の確かめは未実施（板の通しで動作を見た）。
- `location 0 then 0`（CURRENT_LOCATION 0x1450 が own mode でも 0）の理由、cursor の register の latch の時期、premultiplied の alpha と cursor の blend の違いは未確認。
- GOP の無い時の R1b は実装済み、実機は未確認。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws277-p001 | 設計（DC8200・HDMI の初期化、clock・reset・PD、plane・modifier、vblank・IRQ、display の node の置き換え、PBE の線形の書き出し） | cleared（2026-10-20、改訂 3.1） |
| ws277-p002 | go/no-go: DC8200 の attach、U-Boot の plane の引き継ぎ、R4 の記述の部分 | in-progress のまま WS を閉じた（実装・host 試験 PASS、実機 U0 は未実施） |
| ws277-p003 | 引き継ぎの zero copy（vblank・commit の道・latch・flip の記録） | in-progress のまま WS を閉じた（実装は main、板で zero copy の flip が動く。U1〜U5・U11 の行は未実施） |
| ws277-p005 | 自前の mode（HDMI の PHY・PLL、DC8200 の panel・timing・plane、GOP の無い時） | in-progress のまま WS を閉じた（実機で画面が点いた。U7 の残りと R1b の実機は未確認） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws277/phase001`〜`phase005`、`plan/ws277/tests/`）は削除する。git の commit `fb1f6afee` の `plan/ws277/` にある。Queue の履歴は [plan/history](../history/index.md)。
