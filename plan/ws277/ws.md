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

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（DC8200・HDMI の初期化、clock・reset・PD、MMU、plane・modifier、vblank・IRQ、WS269 の display の node の置き換え、PowerVR の PBE の線形の書き出し、coherency） | — | in-progress（2026-10-20 P1、改訂 3.1 が出来た、Q1 の差分の確認待ち） |
