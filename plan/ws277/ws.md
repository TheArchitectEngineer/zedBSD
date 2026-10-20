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

## q1529-i01（2026-10-21、P2）: own mode での直接 scanout

- 原因: board は own mode の display を GPU の attach の後に点ける（`visionfive2.c`: `drv_vf2_gpu_attach` → `drv_powervr_display_attach` → `drv_powervr_dc_vblank_start` → `probe_display_controller` → `light_display` → `drv_powervr_dc_own_mode`）。
  attach の時点では controller が未読（probed 0）なので、vblank の開始は ENODEV になり、scanout 0 のまま、毎 present を 8 MB 複写していた。
- 修正（`display.c`）: lease の claim の時に scanout が 0 なら、`drv_powervr_dc_vblank_start` をもう一度試す（`display_scanout_start`。EALREADY も開始済みと扱う）。
  log は `powervr: display scanout N (late vblank start E)`。
- 確認:
  - ws277 の host 試験は p002 16 passes、p003 24 passes と 7 checks、p005 20 passes。
  - VF2 の vmunix は rc 0、warning 0。
  - 板は未実施。
- 残り: DC8200 の cursor plane（`cursor_set`・`cursor_move`・`cursor_hide`、GPU_DISPLAY_HAS_CURSOR）。register は Linux の vs_dc_hw.c（0x1468〜0x1478、64x64 ARGB）。

## q1529-i01（2026-10-21、P2）: DC8200 の cursor plane

- `dc8200.c` の `drv_powervr_dc_cursor(shown, physical, x, y)`: HDMI panel の cursor の register を書く（Linux の vs_dc_hw.c の dc_hw_commit と同じ順）。
  - 書く register: ADDRESS 0x146C、LOCATION 0x1470（x、y<<16）、CONFIG 0x1468。CONFIG の値は hot_x<<16、hot_y<<8、64x64（1<<5）、bit 3、bit 2、ARGB 2。
  - 隠す時は bit 0〜1 を消し、bit 3 を立てる。
  - 画面の左・上にはみ出した分は hotspot で表す。63 を越えたら隠す。
  - 書く時は panel の shadow を hold し、plane の commit bit を立て直してから hold を解く（`write_plane` と同じ枠）。
  - cursor の register が plane の commit で latch されるのか、すぐ効くのかは未確認。どちらでも frame の境で変わるように、毎回 plane の commit を立て直している。板で確かめる。
- `display.c`:
  - `cursor_set`・`cursor_move`・`cursor_hide` を display の ops に足した。
  - query は、controller を driver が持っている（scanout 1）時に GPU_DISPLAY_HAS_CURSOR を出す（query の時にも late の vblank start を試す）。
  - image は 64x64 ARGB を 3 枚、4 GiB 未満の RAM に置き、uncached の alias から書く。毎回次の 1 枚に、透明に消してから写す（RGBA は B と R を入れ替える）。
  - lease の終わり（`end_lease`）で隠す。
  - libvulkan（`wsi-display.c`）が要るものは次の 3 つで、display node はそれを満たす:
    - query の HAS_CURSOR（plane が 1 つ増えて見える）;
    - CURSOR_SET（import した image の handle・offset・stride・format）;
    - CURSOR_MOVE と CURSOR_HIDE。
- 確認:
  - ws277 の host 試験は全部 0 failures。
  - VF2 の vmunix は rc 0、warning 0。
  - 板は未実施。
  - premultiplied の alpha と DC8200 の cursor の blend の違い（縁の見え方）も未確認。

## q1529（2026-10-21、P2）: vblank の割り込みが届かない

- run 17: `interrupt enable 5 pending 1` なのに、handler は一度も呼ばれなかった（vblank 0、other 0）。
- 原因: `kern_irq_register`（`hal_irq_register`）は PLIC の source を hart へ向けるだけで、priority は 0 のまま（source は mask されたまま）。
  他の driver（firmware-boot.c など）は register の後に `kern_irq_unmask` を呼ぶ。`drv_powervr_dc_vblank_start` はそれを欠いていた。
- 修正: `dc8200.c` の `drv_powervr_dc_vblank_start` で、register の後に `kern_irq_unmask(irq)` を呼ぶ。
  ws277 の p003 と p005 の試験に stub を足した。
- `location 0 then 0` について: register（0x1450）は Linux の DC_DISPLAY_CURRENT_LOCATION と同じ番地。own mode でも 0 のままの理由は未確認。
- 確認: ws277 の host 試験は全部 0 failures、VF2 の vmunix は rc 0、warning 0。板は未実施。
- run 19（unmask の後）でも handler は来なかった。flip は 26 回あり、mismatch は 0。
  診断を足した:
  - unmask の後に `dc8200: vblank interrupt N unmasked` を出す。
  - report で、PLIC（0x0c000000）を読むだけの view から、source 95 の priority と pending、context 0〜9 の enable bit と threshold を出す（`plic_report`）。
  - 確認: VF2 の vmunix は rc 0、warning 0、ws277 の host 試験は 0 failures。
