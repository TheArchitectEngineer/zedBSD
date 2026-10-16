<!-- awesome-plan project=zedbsd record=ws039-p009 -->

# ws039-p009: desktop（compositor が amdrdna で描く、`amdrdna.auto` の既定を上げる、reboot の後片付け）

Status: in-progress（2026-10-16 P3: 実機の desktop が amdrdna で正しく描け、ユーザーの目視 OK（「RDNAは黄色の問題が解決しました。目視です。」）。既定を on にして Q1 の merge と clearance の判定待ち）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1222 / q1222-i01
承認: 2026-10-16 ユーザー「p006, p009, p012の順でどうですか？」（Q1 の割当 q1222、範囲は design §16 の p009 の行のまま。実機 desktop の目視 OK の後に main）
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `ed2689cf0`）

## 範囲（[design.md](../design.md) §16 の p009 の行）

compositor が amdrdna で描く（renderer は p008 の executor、display は p006 の引き継ぎ）、`amdrdna.auto` の既定を上げる、reboot の後片付け。**実機: desktop**。

## 実装（2026-10-16 P3）

- `amdrdna.c`: sessiond の待ち。sessiond は `/dev/gpu0` が無い時、kernel の `hw.gpu.attaching` が 0 なら待たずに console に戻す（WS037 の R9k と同じ問題）。boot の段（`amdrdna.auto`）が GPU device の登録の段（display 付きなら A8、無しなら A5s）に届く時、`amdrdna_keep` で `kern_gpu_attach_begin()`、bring-up の thread が登録の段を終えた時・段が失敗した時・止まった時に `kern_gpu_attach_end()`（1 回だけ）。`amdrdna-private.h` に `attach_counted`・`attach_stage`。

## 実機（10.0.10.3）

| 回 | 語 | 結果 |
| --- | --- | --- |
| 1（15:43、kernel f4f63e1d2、vmunix 714ab5d4） | `amdrdna.auto=A9 amdrdna.vk=1 amdrdna.disp.own=1 login=graphical` | 起動で A2〜A9 が自動に ok（`A8 ok gpu device registered, display 1920x1080@60.000Hz`、`A9 ok draw pixels=4096 copy=4096`）。sessiond: `SESSIOND DISPLAY waited_ms=19300` → greeter=/bin/wayland、AUTOLOGIN kei、SEAT /dev/gpu0、`HANDOFF session ready=1`。compositor: `KWL DISPLAY device=zedBSD amdrdna (AMD RDNA1) width=1920 height=1080 refresh_mhz=60000`、`output-swapchain ms=1164`。ps に wayland・keiland-ime・settings。`amdrdnactl display`: lease 1、presents 25（blob の flip、shown=plane=0x8004f20000）、cursor shown（control 0x03000201・DPP 0xa5）、underflow 0。目視（ユーザー）:「デスクトップが出ました。ウィンドウが黄色をベースにした画像として合成されているように見えます。」「マウスは動きます」→ glass の部分が崩れる（下の「glass の崩れ」） |
| 2（15:52、kernel 181a1498、画面の読み出し付き） | 同上 | warm reboot（desktop の後）で Debian に戻る: ユーザー「Debianへの復帰は問題なしでした」。capture `build/tmp/amdrdna-hw-p9-2/s1.png`（写真と同じ絵）。app が起動しない（ユーザー「Terminalなどアプリが起動しませんでした」）→ 下の「CPU の窓の不足」 |
| 3（16:20 ごろ、調査用の kernel） | 同上 | zedBSD・Debian とも 10 分応答無し → ユーザーが電源を入れ直し。USB の boot log は desktop まで進み 300 s まで記録あり、ユーザー「デスクトップは起動していた」→ 固まったのではなく DHCP の失敗の見込み（BUG-326、前の起動の /etc/resolv.conf.tmp.<pid> の残り）。main（DHCP の修正入り）を merge（67fe95bef）して試験の image を作り直す |

| 4（16:50 ごろ） | 同上 | 画面が点かない → ユーザーが電源を入れ直し。原因: image を dd で書き直して USB の ESP の PARTUUID が変わり、UEFI の Boot0001 が古い PARTUUID を指したまま（firmware が USB から起動しない）。`efibootmgr --create-only` で今の ESP を指す Boot0003 を作り、以後 `hw-run.sh boot 0003`。`hw-run.sh` は書き込みの後に今の ESP を指す entry を示し（無ければ作り方を出す）、boot の前に entry が USB の ESP の PARTUUID を指すか確かめて、違えば止まる |
| 5〜7（17:00〜17:50、調査用の kernel） | 同上 | glass の切り分け（下の「glass の崩れ」）。`vkblend`（`plan/ws039/tests/vkblend.c`）で再現: 読み戻しは全部正しく、blend だけ崩れる |
| 8（17:57、export を 16-bit float に直した kernel、調査用の印あり） | 同上 | `vkblend` 14 case 全部 ok（max 0）。capture `build/tmp/amdrdna-hw-p9-7/s12.png`: glass・dock の icon が正しい。ユーザーの目視:「RDNAは黄色の問題が解決しました。目視です。」 |
| 9（18:02、正式の kernel、vmunix の SHA-256 の先頭 ceb558c0ccdb93a4） | **boot の語に amdrdna の語を書かない**（zedbsd.cfg から `amdrdna.*` を外し、`login=graphical` だけ）→ 既定の `amdrdna.auto=A9`・`amdrdna.vk=1`・`amdrdna.disp.own=1` | A5s → A6 → A7 → A8（`gpu device registered`）→ A9（`draw pixels=4096 copy=4096 ... renderer bound`）、`display lease 1 claimed`、`SESSIOND DISPLAY waited_ms=6200`、`KWL DISPLAY device=zedBSD amdrdna (AMD RDNA1)`。`vkblend` 14 case 全部 ok。capture `build/tmp/amdrdna-hw-p9-7/s13.png`（glass 正常）。`amdrdnactl display`: presents 18、flips 20、underflow（combiner）0 |

## 既定（2026-10-16 ユーザーの決定「目視OKの後に3つとも on」）

- `stage.c`: `amdrdna.auto` の既定 A9（戻す語 `amdrdna.auto=A1v` など）。`node.c`: `amdrdna.vk` の既定 1（戻す語 `amdrdna.vk=0`）。`display.c`: `amdrdna.disp.own` の既定 1（戻す語 `amdrdna.disp.own=0`）。実機 9 回目で語無しの起動を確かめた。

## 新しい機能（2026-10-16）

- `amdrdnactl screen FILE`（control の node の root だけの `AMDRDNA_IOC_SCREEN`）: plane が今読む surface を PPM に。compositor の capture が無い image で画面の証拠を取る道具。

## CPU の窓の不足（app が起動しない、2026-10-16）

- `amdrdnactl status` の `cpu_free=25MiB`（CPU の窓の VRAM の pool の残り、窓の外の gpu_free は 5862MiB）。session.log: `ZTERM FAILED run=term operation=vkAllocateMemory result=-2`、`KEILAND PRESENT_FAILED ... vkCreateSwapchainKHR result=-2`、`MUSIC FAILED operation=window error=5`、`CALENDAR FAILED operation=window error=5`。
- 原因: executor の memory type が 1 つ（DEVICE_LOCAL|HOST_VISIBLE|COHERENT）で、blob と executor の内部の確保が全部 256 MiB の窓の pool から。直しは p017（ユーザーの順: glass → p012 → 窓の解除）。

## glass の崩れ（原因と修正、2026-10-16）

- 原因: pixel program の colour export が 32-bit float 4 channel（SPI_SHADER_COL_FORMAT=32_ABGR）で、8-bit の target（B8G8R8A8）に **blend** する時、colour unit の blend が画素の channel を隣の画素と取り違える（x mod 4 = 0 の列だけ正しい）。blend しない書き込み・alpha 0 と 255（export unit の最適化が blend を省く）は正しいので、半透明の画素（glass・icon の縁）だけが崩れた。RADV は 8-bit の target に必ず 16-bit float（FP16_ABGR）で export し、この組み合わせを使わない。
- 修正（compiler/）: colour を `v_cvt_pkrtz_f16_f32` で 2 channel ずつ 2 つの VGPR に詰め、compressed の export（exp の COMPR bit、enable 0xf）、SPI_SHADER_COL_FORMAT=FP16_ABGR（4）。executor の colour target は 8-bit だけなので全部この形。出典は block の Procedure/Values referenced（ACO の export_fs_mrt_color、ac_choose_spi_color_formats、assembler の EXP）。
- 併せて（vk/）: blend する target の SX_MRT0_BLEND_OPT を、channel を何も省かない（7）と blend の関数（ADD など）にした（RADV と同じく関数を与える）。glass の原因ではなかった（この変更だけでは崩れが残った）が、実機の 8・9 回目はこの値で検証した。
- 試験 `plan/ws039/tests/vkblend.c`（`vkblend-build.sh BUILD` で image の libvulkan に link、`/tmp` に置いて走らせる）: (1) 読み戻し 240x135・256x64・1920x64・1180x40・100x30、optimal・linear、copy・CPU の窓・sample して再描画の 3 通り、(2) blend: premultiplied（ONE/ONE_MINUS_SRC_ALPHA）と straight（SRC_ALPHA/ONE_MINUS_SRC_ALPHA）、alpha 0・64・128・191・255、1 列おき・4 列おきの alpha 0 の縞。修正前: (1) 全部一致、(2) alpha 0 と 255 は正しく途中の alpha が崩れる。修正後: 14 case 全部 ok（差 0）。register: CB_BLEND0_CONTROL premultiplied 0x40000501・straight 0x40000504、CB_COLOR0_INFO 0x28828、SX_MRT0_BLEND_OPT 0x01770177、SPI_SHADER_COL_FORMAT 9 → 4。
- 切り分けの記録（調査用の kernel、既に外した）: SX_BLEND_OPT_CONTROL で最適化を止める・SX_MRT0_BLEND_OPT の関数を BLEND_DISABLED にすると alpha 0 の画素も 0 になる（最適化が alpha 0・255 を救っていた）、SX_PS_DOWNCONVERT=8_8_8_8・CB_COLOR_CONTROL の DISABLE_DUAL_QUAD は効かない。
- host 試験: compiler の host 試験 PASS（28）、compiler の desktop 試験 PASS（modules 44、passed 62）、vk の host 試験 170 checks 0 failures（期待値を FP16_ABGR と 16-bit の誤差 0.001 に）、`AMDRDNA_LINUX=plan/ws039/temp/linux amdrdna-host-test.sh` PASS（375）。試験の interpreter（`gfx10-machine.c`）に `v_cvt_pkrtz_f16_f32` と compressed の export、`gfx10-graphics-test.c` に参照の値の 16-bit への丸めを足した。`audit-vk.sh` は 82 件の FAIL で、HEAD（変更前）と同じ件数・同じ内容（この Phase で増えていない。既存の扱いは p008 の残り）。
- 以前の調べ（記録として残す）:

- 崩れるのは compositor の glass の描画（panel.frag の mode 0、light glass、generic の panel.frag でも同じ）だけ。壁紙・icon・文字・client の window の中身・mouse の cursor は正しい。
- 画素: x mod 4 = 0 の列だけ正しく、他の列は (R,G) = 正しい画素の (B,A) など、2 つの 32 bit の組に詰め替えたような値。8 行同じ。回ごとに色が変わる。
- 確かめた事: glass が sample する 480x270 の blur 済みの壁紙の中身は正しい（driver の debug の読み出し）。glass と solid（正しく描ける）の draw の register の差は program と table の address だけ（IB の取り出しと decode）。compiler の desktop の host 試験は panel.frag の全 mode PASS。
