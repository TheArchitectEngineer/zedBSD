<!-- awesome-plan project=zedbsd record=ws247-p001 -->

# ws247-p001: nvrtx の定数・表・手順に block ごとの出典を付け、本文の出典の名前を除く

Status: cleared（2026-10-16、q984〜q986 とも cleared）
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
- **q984 P1（cleared、main に merge、提出 c88130a3e＝11 commit）**: 21 file のうち 16 file。nvrtx-gsp.h 33、gsp.c 4＋手順 10、rpc.c 5＋5、boot.c 6＋3、falcon.c 4＋7、vbios.c 13＋10、rom.c 3＋2、layout.c 6＋8、vram.c 2、mmu.c 7＋3、radix3.c 1＋1、records.c 5＋5、identify.c 2＋2、perf.c 3、nvrtx-private.h 4、nvrtx-gpu.h 4。既存の 6 か所を関数名で具体化（vram.c の BAR1・fault・bar1 の table、nvrtx-gpu.h の handle を rm/handles.h に、falcon.c の壊れた comment）。nvrtx-mem.h・nvrtx.c・file.c・runtime.c は変更なし。ogkm は 570.144（8ec351aeb）。cpp の比較 21/21 同じ、build rc 0・warning 0。
- **ws.md の残り 4（vram.c の ogkm の 2 path）**: 確認済み。ogkm は blob の無い partial clone なので 8ec351aeb の raw を取り `git hash-object` が tree の blob と一致（osinit.c b9437d37…・kern_bus_gm107.c 9b2a1865…）。osinit.c の RmSetConsolePreservationParams が 64 KiB に揃える、kern_bus_gm107.c の kbusInitBar1_GM107 が console の BAR1 の mapping を offset 0 に保つ。関数名を referenced: に足した。
- Q1: 試験の回の注記（nvrtx-gsp.h の R3・R5、gsp.c の R6・R2）を comment から除き、runtime.c の log の `D2` → `renderer`（50a95d261）。
- **Q1 の確かめ（main 50a95d261）**: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws247-q1 build/ws247-q1/vmunix` rc 0・warning 0（kernel include check・amd64 vmunix check PASS）。nvrtx の全 file（compiler/ を含む）の監査の script で referenced: の段落の外の出典の名前 0 件。host 試験 sm75 287/0・vk-d3 122/0・vk 60/0。QEMU・実機は未実施（comment と log の文字列だけの変更）。
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
| nvrtx-gsp.h | NVRTX_BIN_VERSION（1）・NVRTX_ELF_HEADER_BYTES（64） | 版の値は nouveau に無い・ELF64 の形式（elf.h が参照の tree に無い） |
| gsp.c | GSP_USER_ADDRESS_LIMIT（0x7ffffffff000） | x86-64 の TASK_SIZE、arch/x86 が参照の tree に無い |
| runtime.c | RUNTIME_CAPSET_ID〜RUNTIME_XML_VERSION | Venus の capset（virtio-gpu・virglrenderer）、Mesa の取得物に src/virtio が無い |
| nvrtx-private.h | NVRTX_PCI_VENDOR・NVRTX_PCI_CLASS_DISPLAY(_MASK)・NVRTX_PCIE_* | PCI-SIG の標準値、pci_ids.h・pci_regs.h が参照の tree に無い |
| channel.c | CHANNEL_VA_LIMIT（1<<47） | field（NV_RAMIN_ADR_LIMIT）は dev_ram.h にあるが 47 bit の出典なし、自前の選択 |
