<!-- awesome-plan project=zedbsd record=ws141 -->

# WS141: Raspberry Pi 4 のグラフィックス driver（VideoCore VI: HVS・pixelvalve・HDMI の display と V3D 4.2）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: Pi 4 の display・V3D・Vulkan・Keiland の desktop が動く。ユーザー「見た目は大丈夫です。描画は2.5倍くらい速くなりました。」「おお、カーソルは60fpsです！」、2026-10-13 ユーザー「動作OKです」（main 1392d52 の image、出典は P3 の精査の表）。同じ driver を Pi 5（WS279）でも使う）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Raspberry Pi 4（BCM2711、VideoCore VI）で、zedBSD の自前の GPU driver により display（firmware の framebuffer からの引き継ぎ、HDMI の mode set、scanout）と 3D（V3D 4.2 の job の実行）を成立させ、zedBSD の GPU の interface（`struct drv_gpu_interface`、i915 と同じ口）に載せて desktop を表示する。

## ユーザーの決定

- 2026-10-04「完全に独立した作業として、Raspberry Pi 4のグラフィックドライバを作成します。i915のときと同じで、Linuxドライバの初期化順やコマンド投入順などをまずドキュメントにして、正本も参照しながら、i915と同じように、我々のインタフェースに適合させていきます。フレームバッファにどこまで進んだかのメッセージを表示することで、少しずつデバッグしながら先に進めます。DRM部分の書き換えは、i915を参考にします。これは独立したWSで、17時以降に着手します。」
- 2026-10-04（ライセンス）「vc4/v3dドライバのソースから作る文書は作業ファイルにして、リポジトリにコミットせず、GPLコードを参考に書き写してもいいことにします。基本は手順を書き写しますが、表現が難しいときはコードを書き写してもいいです。我々のコードを生成する前に、定数はすべて、一括で、独自の名前に変更します。最後にライセンスに問題がないか、コードに類似がないかを監査します。これにより字面でも設計でもGPLコードを含めず、独自ライセンスとします。ファームウェアに相当しそうなBLOBがソースコード中にある場合は、userland/firmwareに移してファイルからロードすることにします。」。register の定義と packet の形を MIT の Mesa・Broadcom の公開の文書・device tree の binding から取り file ごとに license を監査する案に、ユーザー「これはそうしたいですね。」
- 2026-10-05「WS037, WS044,WS048,WS141, … は、ベータ4以降としてください。」（2026-10-09 の独立のセッションへの割当で再開。ベータ3・4 の段は 2026-10-27 に取り消し）
- 2026-10-09（独立のセッション）「WS141をあなたが作業します。P1,P2とは別なセッションです。ws141/ws.mdはあなたがそのセッションが排他的に更新しますが、master.mdは更新しません。同じソースツリーを使いますが、作業は別なディレクトリで行い、パッチをあなたに提供するので、Q1がマージします。」「パッチの影響範囲が狭いので、あなたがマージしてOKです。」「実機確認は後で行う」「Q1でのマージは遅らせます。続きをお願いします。」
- 2026-10-09「Linuxと同じ再初期化へ変更する」（p003 の初回表示を Linux の順の再初期化に）。「mainにマージしてOKです。mailbox修正も承認します。」（WS048 の mailbox の容量 0 の tag の修正）
- 2026-10-09「続けてください。完成まで自走してください。」「WS141に実行器・compilerも含め、Keiland表示まで進める」（p006 を kernel の Vulkan 実行器と SPIR-V compiler の実装に拡張）
- 2026-10-11「mainに統合してください」「mainへのコミットは自動でお願いします。」「今後は自動で統合してください。」
- 2026-10-11 Pi 4 の image は案 A: ユーザー「推し通りです。」（Pi だけの kernel の snapshot の cache と変化した tile の写しを main から外し、実機で確かめた storage cache まで戻した）
- 2026-10-11 cursor の plane: ユーザー「displayなのにplaneのx,yですか。…cursorと明示されたplaneならいいと思います。」「その案でOKです。」。2026-10-12「待って、ベンダ拡張はおかしいのでは？display の hardware plane を列挙して使う拡張を、標準として使うべきでは？」「VK_KHR_display＋cursor 専用の UAPIでOKです。承認します。」→ vendor の Vulkan 拡張は作らない。cursor 専用の UAPI（`GPU_DISPLAY_CURSOR_SET`・`MOVE`・`HIDE`、無い driver は ENOTSUP）と、libvulkan が VK_KHR_display の plane の 1 つを cursor の plane として見せる。i915 の hardware cursor は別の WS。
- 2026-10-12 実機: ユーザー「おお、カーソルは60fpsです！」、hardware blend の kernel でユーザー「見た目は大丈夫です。描画は2.5倍くらい速くなりました。」
- 2026-10-22 ユーザー「stencil の対応をお願いします。」（p017）

## 結果

- `src/drivers/gpu/bcm2711/`（display: HVS・pixelvalve・HDMI、Linux の順の再初期化 R0・flip・合成・resident display・cursor の plane。V3D: power・MMU・buffer object・bin/render・TFU の job・reset・fence。kernel の Vulkan 実行器と SPIR-V → QPU の compiler）と、GPU 共通の SPIR-V の frontend・IR（`src/drivers/gpu/compiler/`、i915 と共有）が main にある。cursor の UAPI は `include/uapi/gpu-display.h`。
- 実機（Pi 4、ユーザーの確かめ）: N0・R0・V1〜V10、Keiland の desktop の表示、cursor の plane の 60 fps、hardware blend で描画が約 2.5 倍、2026-10-13「動作OKです」。QEMU の raspi4b は HVS・V3D を emulate しないので QEMU の証拠は無い（host 試験と実機だけ）。
- license と GPL の code との類似の監査は p007（[p007-software-audit.md](p007-software-audit.md)、[rpi4-gpu-license-audit.md](rpi4-gpu-license-audit.md)）。設計は [rpi4-gpu-design.md](rpi4-gpu-design.md)、Codex のセッションの実行記録は [execution-20261009.md](execution-20261009.md)・[handoff-20261009.md](handoff-20261009.md)・[compiler-refactor-20261010.md](compiler-refactor-20261010.md)。
- 同じ driver が Pi 5（BCM2712、V3D 7.1、[WS279](../ws279/ws.md)）でも使われている。

## 制限・移管

- 描画の速さの残り（p013 の tile buffer の f16、p015 の UIF、p016 の worker の mutex と cursor、p018 の部分の CopyBufferToImage、p019 の blend の係数）は設計・候補のまま閉じた。GPU の速さは [WS291](../ws291/ws.md) の範囲。
- p017 の stencil は Pi 5 で readback PASS・Notes の起動まで。線の目視と Pi 4 の実機は未実施。
- p001〜p006 の phase.md の Status は in-progress のまま WS を閉じた（実装は main。実機の全体の受け入れの記録は無く、上のユーザーの確かめで代える）。
- 範囲の外: Raspberry Pi 5（WS279）、DSI の LCD、HDMI の音、video の decode。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws141-p001 | 文書: vc4・v3d の初期化の順と command の順（作業の文書は temp）、正本と license の監査、interface の対応表、段の印 | in-progress のまま WS を閉じた（文書と review は済み、判定の記録なし） |
| ws141-p002 | 定数の一括の改名、段の印、driver の骨格（FDT・MMIO・mailbox・IRQ） | in-progress のまま WS を閉じた（実装は main、T1-092 PASS） |
| ws141-p003 | display: Linux の順の再初期化 R0、初回 scanout、vblank・flip・合成・resident | in-progress のまま WS を閉じた（実装は main、実機の R0 は p011 で成功） |
| ws141-p004 | V3D: power・MMU・buffer object、bin/render・TFU の job、reset、fence | in-progress のまま WS を閉じた（実機で V1〜V10 成功） |
| ws141-p005 | `drv_gpu_interface` への統合と Keiland の desktop | in-progress のまま WS を閉じた（desktop は実機で表示、ユーザー「動作OKです」） |
| ws141-p006 | kernel の Vulkan 実行器・SPIR-V compiler・Keiland の描画の経路 | in-progress のまま WS を閉じた（実装は main、実機で描画） |
| ws141-p007 | 規約の全文、license と GPL の code との類似の監査、BLOB の確認 | cleared |
| ws141-p008 | GPU 共通の SPIR-V frontend・IR、i915・VC4 の移行 | cleared（software・build・host） |
| ws141-p009 | 実機の clock・VA 幅の初期化の停止の修正 | cleared（software） |
| ws141-p010 | core clock・実時間の job の期限 | cleared（software） |
| ws141-p011 | R0 の拒否とブラックアウトの修正 | cleared（実機 R0 成功） |
| ws141-p012 | Keiland の EL0 の counter の例外 | cleared（実機で counter trap が消えた） |
| ws141-p013 | Keiland の描画の速さ（frame 33 ms → 16 ms） | in-progress のまま WS を閉じた（hardware blend・compiler の NOP の削減は実機 OK、tile buffer の f16 は未確認） |
| ws141-p014 | HVS の cursor plane | in-progress のまま WS を閉じた（実機で 60 fps、ユーザーの確かめ） |
| ws141-p015 | GPU が書いて読む画像を UIF で持つ（設計） | planning のまま取り下げ（速さは WS291） |
| ws141-p016 | GPU の実行中に ioctl と cursor を止めない（設計） | planning のまま取り下げ（速さは WS291） |
| ws141-p017 | bcm2711 の Vulkan の stencil（Notes） | in-progress のまま WS を閉じた（Pi 5 で readback PASS、線の目視と Pi 4 実機は未実施） |
| ws141-p018 | 部分の CopyBufferToImage の費用（候補） | planning のまま取り下げ |
| ws141-p019 | bcm2711 の Vulkan の blend の係数（案、directory なし） | planning のまま取り下げ |

## 記録の所在

Phase の計画・結果・試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws141/` にある。回帰に使う host 試験（shader の compiler の `shader-host-test.sh` 一式、`host-display-cursor.sh`）は plan/tools/ へ移す（[plan/tools.md](../tools.md)）。Queue の履歴は [plan/history](../history/index.md)。
