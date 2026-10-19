<!-- awesome-plan project=zedbsd record=ws277 -->

# WS277 VisionFive 2 の DC8200 の display driver（zero copy の scanout）

- ID: ws277、Master: [master.md](../master.md)、Primary Milestone: MG008（fg020）
- Status: incomplete（2026-10-20 01:13 Q1 作成、設計 p001 を開始）
- 起点（2026-10-20 ユーザー）: 「PowerVRはタイルとRGBAと両方の書き出しができると思いますが、VF2のLinuxドライバはもしかするとタイルしかないかも？だからスキャンアウトのバッファにCPUでコピーしてて、GNOMEがむちゃくちゃ遅くて使い物にならなかったと思います。」→（クリック）DC8200 の自前の driver は「新しい WS で設計から」。
- Q1 が確かめた事（~/visionfive-linux、vendor の 6.12）: DC8200 の plane の modifier は `DRM_FORMAT_MOD_LINEAR` と Vivante の tile・DEC の形（`drivers/gpu/drm/verisilicon/vs_dc_hw.c:412-432`）。PowerVR の twiddled・tiled の形は無い。よって zero copy には PowerVR が線形で書く事が要る（PBE の線形の書き出しは未確認）。
- 担当: P1（設計 p001）。

## 完了の条件（案、設計で確定）

1. VF2 の DC8200 と HDMI（inno_hdmi）を zedBSD の driver で初期化し、WS269 の display の node（gpu0）を置き換えて、PowerVR が線形で書いた render の buffer を CPU の copy 無しで scanout する（import・page flip・vblank）。
2. Keiland の desktop の実用の fps（WS270 §2.3 の段 2）を実機で測る。注記（設計 版 2）: この測定は GPU が線形で描く frame と capset（WS269 p010・p011、WS270 の該当 Phase、[design.md](design.md) §9 の R3・R5〜R7、版 3 では p007）に依るので、WS277 の完了はそれらが main に入るまで閉じられない。

## 依存

- WS256 p011（boot framebuffer、U-Boot の GOP）、WS269 p006・p007（render の share・lookup、display の node の ops）。
- 設計 版 3.1（[design.md](design.md) §7・§9）の依存: p002 は WS269 p007 の display の node の code（merge 済み、出力の範囲）と R4 の記述の部分（WS256、`src/kern/platform/visionfive2.c` の DC8200・HDMI の address・IRQ。無ければ driver の既定の address で始め後で置き換える、版 3.1）。p003 は WS269 の lookup の code（merge 済み、出力の範囲）と R4 の text の部分（serial-console に汎用で追加だけの口と VF2 の wrapper の text の ops、版 3.1）、R3 の照会の口（無ければ試験の blob だけ）。p005 は R1（voutcrg の provider）・R2（PD_VOUT）。p004 は p003 と U11 の結果。p007 は R3（版 3.1 で縮めた: `render_export` が非 mappable・非 shareable の blob の image の export を拒む）・R5（`blob_create_placed`、任意）・R6（linear の rowPitch 32 byte・transfer の LINEAR）・R7（capset、1.3.269 を推奨）と WS269 p010・p011、WS270 の該当 Phase。R8 は要らない。

## 決定（2026-10-20 03:25 ユーザー、クリック）

D1〜D11 は全部 (a)（推奨どおり）。原文は Master の decisions-log。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（DC8200・HDMI の初期化、clock・reset・PD、MMU、plane・modifier、vblank・IRQ、WS269 の display の node の置き換え、PowerVR の PBE の線形の書き出し、coherency） | — | cleared（2026-10-20 Q1、改訂 3.1。D1〜D11 は未決） |
| [p002](phase002/phase.md) | go/no-go: DC8200 の attach、U-Boot の plane の引き継ぎの判定・snapshot、COMMIT・CURRENT_LOCATION の読み戻しの記録、画面に 1 行。R4 の記述の部分（visionfive2.c） | p001、WS269 p007 | in-progress（2026-10-20 P1 q1348-i01: 実装・build（VF2・riscv64-virt・amd64、warning 0）・host 試験 PASS。実機 U0 待ち） |
| [p003](phase003/phase.md) | 引き継ぎの zero copy: vblank の IRQ 95、commit の道（dc_commit_lock・fatal の印）、s0 + 2 の latch、D11 の書き戻しと ENXIO、保持の数、copy との行き来、flip の記録の環、`dc8200.zerocopy=test`。先行 R1・R2・R4（q1350） | p002 が go、WS269 の lookup、R4、R3（無ければ試験の印） | in-progress（2026-10-20 P1 q1351-i01: 実装・build（VF2・riscv64-virt・amd64、warning 0）・host 試験 PASS。実機 U1〜U5・U11 待ち） |
| [p005](phase005/phase.md) | 自前の mode: 追補（design 版 3.2 §15、道の優先・`display.mode=`・戻し先）、HDMI の PHY・PLL、DC8200 の panel・timing・plane、GOP の無い時の電源・clock・reset と確保した framebuffer | p002（D1 no-go の順）・R1・R2、R1b（GOP の無い時） | in-progress（2026-10-20 P1 q1353-i01: 実装・build（VF2・riscv64-virt・amd64、warning 0）・host 試験 PASS。実機 U7 待ち、GOP 無しは R1b 待ち） |

> 2026-10-20 Q1 の記録: ユーザーの VF2 の serial の log（U-Boot SPL 2021.10、`U-Boot 2021.10 (Oct 10 2025) Build: jenkins-github_visionfive2_6.12-16`、`In: serial / Out: serial / Err: serial`）。配布の U-Boot は console を HDMI に出しておらず、GOP の framebuffer を渡さない見込み（未確認）。D1 は no-go の見込みなので、§7 の no-go の順（p005 を先に、その後に p003 の code を使う）で進める。同じ log で SD（mmc1）が `Card did not respond to voltage select! : -110` で認識されていない。

> 2026-10-20 06:11 Q1 の記録: ユーザーの VF2 の実機で、U-Boot が ExitBootServices で消した表示を p005 の自前の点灯（PD_VOUT・SYS と VOUT の clock・reset の順、bootfb の memory を 1920x1080@60 で scanout）で点け直し、画面が出た（「画面がつきました！」、BUG-341）。p005 の U7 の主の部分の実機の確認。
