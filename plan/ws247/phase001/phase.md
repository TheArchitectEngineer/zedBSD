<!-- awesome-plan project=zedbsd record=ws247-p001 -->

# ws247-p001: nvrtx の定数・表・手順に block ごとの出典を付け、本文の出典の名前を除く

Status: in-progress（q984〜q986、2026-10-16）
Parent: [WS247](../ws.md)
Approval: 2026-10-16 ユーザー「WS247の作業を行います。」（ws.md の「残り」1・2・4 の範囲）

## 範囲

1. ws.md の残り 1: draw-push.c・scanout.c・disp-records.c の comment の本文に残る出典の名前（nouveau・Mesa・NVK・NAK・Linux・open-gpu）を除き、file の上部の出典の一覧を block ごとへ。
2. ws.md の残り 2: [unreferenced-defines.md](../unreferenced-defines.md) の block（hardware・firmware・class の値）に、block の直前の複数行の comment で「Value referenced: / Values referenced:」を付ける。手順を参考にした関数には「Procedure referenced:」。自前の値（大きさ・時間切れ・handle の選び方など）は付けない。出典が見つからない値は付けずに一覧（下の「出典の無い値」）へ。
3. ws.md の残り 4: vram.c の open-gpu-kernel-modules の 2 path の確認（手元に無ければ「未確認」と記録）。

**comment だけを変える。** code・値・識別子・log の文字列は変えない。

## 分担

| Queue | 担当 | worktree / branch | file |
| --- | --- | --- | --- |
| q984 | P1 | p1 / agent/p1-ws247 | GSP・boot・memory: nvrtx-gsp.h・gsp.c・rpc.c・boot.c・falcon.c・vbios.c・rom.c・layout.c・vram.c・mmu.c・radix3.c・records.c・identify.c・perf.c・nvrtx-mem.h・nvrtx.c・file.c・stage.c・runtime.c・nvrtx-private.h・nvrtx-gpu.h |
| q985 | P2 | p2 / agent/p2-ws247 | display・channel: scanout.c・nvrtx-disp.h・disp-records.c・display.c・present.c・cursor.c・share.c・objects.c・channel.c・node.c・nvrtx-node.h |
| q986 | P3 | p3 / agent/p3-ws247 | draw・compute・vk: draw-push.c・exec-push.c・compute.c・draw.c・nvrtx-vk-objects.h・work.c・vk-execute.c・gr.c・image.c・nvrtx-draw.h・nvrtx-work.h・nvrtx-compute.h・vk-pipeline.c・vk-device.c・nvrtx-vk.h・vk-record.c・work-records.c・vk-stream.c・vk-node.c・compute-launch.c・vk-queue.c・vk-object.c・vk-layout.c・vk-dispatch.c・vk-command.c・queue.c・nvrtx-exec.h |

compiler/ は WS246 の範囲で、ここでは触らない。

## 確かめ

- comment だけの差分: 各 file で `cpp -fpreprocessed -P` の出力（空白を除く）が変更の前後で同じ。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws247-k build/ws247-k/vmunix` rc 0・warning 0（担当の worktree）。
- 出典の名前の監査: `grep -niE 'nouveau|mesa|nvk|nak|linux|open-gpu'` の hit が referenced: の行（とその折り返し）だけ。

## 結果

- **q986 P3（cleared の候補、main に merge 046ce5e1c、提出 97f772767）**: 27 file を監査し 18 file の comment を変更。draw-push.c の上部の一覧（Mesa の 8 path と SHA-256・派生の宣言）を消し block ごとへ（Values 13・Procedure 11）。exec-push.c の method 47 件は clc597.h の NVC597_* と一致。cpp の比較 27/27 同じ、build rc 0・warning 0、監査の grep は referenced: の段落だけ。段の名前の残り（vk-dispatch.c の D2/D3、work.c の D1）も comment から除いた。
- **q985 P2（cleared の候補、main に merge 7f159e376、提出 0adcc03a2・a3ffb1fa0・c0cd135d9）**: 11 file のうち 8 file。scanout.c 26（値 15・手順 11）、disp-records.c 34、nvrtx-disp.h 24、objects.c 17、display.c 10（既存 4 か所を関数名に具体化）、channel.c 7（PRAMIN・TLB・USERD・doorbell・copy の subchannel、channel_flush・channel_submit に Procedure）、present.c 2、share.c 1。scanout.c の上部の出典の段落を関数へ移し、本文の出典の名前 scanout.c 21・disp-records.c 16 を除いた。cursor.c・node.c・nvrtx-node.h は自前の値だけで 0。構造体の大きさ・field の位置は ogkm 570.144 の header を host で compile して一致を確かめた（表示 channel の push buffer の登録 56 bytes・channel の確保 40 bytes は 570.144 の layout なので ogkm と r570 を引く）。cpp の比較 11/11 同じ、build rc 0・warning 0、監査の grep は referenced: の段落の外で 0。
- Q1: vk-node.c の log の段の名前 `D3` → `executor`（`D3 fail` → `executor fail`）。runtime.c の `D2` は P1 の merge の後に `renderer` へ。

## 出典の無い値

| file | 値 | 扱い |
| --- | --- | --- |
| compute.c・work.c | COMPUTE_VA_LIMIT・WORK_VA_LIMIT（1<<47） | 自前の選択（vmmtu102 の page の段は 49 bit） |
| vk-execute.c | EXECUTE_BUFFER_START_ALIGN（256） | 出典なし |
| nvrtx-compute.h | NVRTX_QMD_CBUF_ALIGN（0x100） | 自前の保守的な値（NVK は Turing で 64） |
| gr.c | GR_CHIPSET_TU102/104/106 が scrubber を要すること | chipset の番号だけ出典あり、scrubber の対象の根拠なし |
| image.c | IMAGE_ELF_*・IMAGE_SECTION_* | ELF の公開仕様（付けない） |
| nvrtx-vk-objects.h | NVRTX_VK_IMAGE_SIDE_MAX（16384） | 自前の上限（NVK は 32768） |
| scanout.c | SCANOUT_REG_SOR_CAPS（0x64013c） | **参照の tree に定義の無い番地**。SOR の caps は NV_PDISP_FE_SW_SOR_CAP(i)＝0x640144＋i*8（sorc37d_get_caps も 0x144＋or*8）。scanout.c:1273 で読んで値を捨てている。code の直しは WS247 の範囲外、WS037 の後続で判断（読みを消すか 0x640144＋or*8 に） |
| channel.c | CHANNEL_VA_LIMIT（1<<47） | field（NV_RAMIN_ADR_LIMIT）は dev_ram.h にあるが 47 bit の出典なし、自前の選択 |
