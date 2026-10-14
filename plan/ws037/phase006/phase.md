<!-- awesome-plan project=zedbsd record=ws037-p006 -->

# ws037-p006: display（段 P8）

Status: in-progress（2026-10-15 P1。P8a（display の起動と GOP の readout）を実装、build まで。実機は未。P8b は設計まで）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: Q1 の割当（2026-10-14 夜「(3) p006（display: GOP の readout と GSP の RM の display で mode set・scanout・flip）」、ユーザーの判断「display … ここまでベータ2に入れます」）
依存: p005（P4〜P6、copy の channel・VRAM の書き込みの道）。既定 off（`CONFIG_DRIVER_PCI_NVRTX=n`）のまま。
設計の正: [nvrtx-design.md](../nvrtx-design.md) の P8 と 2 節の 0（**GOP の出力先以外に scanout を始めない**、Guardrail）。値の出典は作業の文書 `temp/channel-mmu-display.md` の 6 節と nouveau（MIT）の `r535/disp.c`・`r570/disp.c` と nvrm の header（570.144 の番号。r570 で変わった system の control（GET_SUPPORTED 0x730107・GET_ACTIVE 0x73010c・CONNECT_STATE 0x730108）は r570 の値、他は r535 の値。大きさと offset は header を scratchpad で compile して確かめた）。

## 段

| 段 | すること | 印（合格） |
| --- | --- | --- |
| P8a | display の instance memory（VRAM 64 KiB、copy の block の 1 MiB 下、0 で埋める）を `INTERNAL_DISPLAY_WRITE_INST_MEM`（0x20800a49、24 byte）で渡す → `NV04_DISPLAY_COMMON`（0x0073、0x00730000）→ `INTERNAL_DISPLAY_GET_STATIC_INFO`（0x20800a01、windowPresentMask）→ `INTERNAL_INIT_BRIGHTC_STATE_LOAD`（0x20800ac6、status 0x56 = ACPI の _DSM 無し、nouveau と同じ）→ `DP_SET_MANUAL_DISPLAYPORT`（0x731365）→ `GET_NUM_HEADS`（0x730102）・`GET_ALL_HEAD_MASK`（0x730287）・`GET_SUPPORTED`（0x730107）→ root `TU102_DISP`（0xc570、0xc5700000）→ head ごとに `GET_ACTIVE`（0x73010c）→ 点いた head は `OR_GET_INFO`（0x73028b、SOR の番号と protocol）と ARMED の timing（0x68a000 + head×0x400 の +0x64・+0x6c・+0x70・+0x0c） | `P8 heads N (mask), windows …, outputs …`、点いた head ごとに `P8 head H: display … SOR n DP-A/TMDS-A …` と `P8 head H: WxH of HTxVT, clock …`、`P8 ok: display, heads …, windows …, lit …` |
| P8b | GOP の head を引き継ぐ: core channel（0xc57d、push buffer 4 KiB を coherent sysmem、`INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER`）、RAMHT と ctxdma（notifier と VRAM 全体）を instance memory に kernel が書く、core の init の method（notifier・window の usage bounds）、window channel（0xc57e、GOP の head の window 0）、自前の surface（VRAM、GOP と同じ大きさ、形式 A8R8G8B8 の pitch）を PRAMIN で塗る、mode set は **GOP と同じ timing と SOR**（`SOR_SET_CONTROL`・`HEAD_SET_*` を ARMED の値から）、`UPDATE` と notifier の完了、flip（`SET_OFFSET` と `UPDATE`） | 画面に自前の surface（試験の模様）、notifier の完了、flip の後に別の模様 |
| P8c | text console の付け直し（設計の判断の項目 12: pcat の text の口、WS037 の範囲の外の依存）と `drv_gpu_ops` の display の ops | （後） |

- P8a は画面を変えない（RM の object と読みだけ）。P8b から画面を書き換える。
- 停止: `nvrtx_display_stop()` が root → common を FREE（`gsp_finish` で P6 の graphics・channel の片付けの前）。`nvrtx.stop=P8` で P8 の手前で止まる。

## 確認

| 確認 | 結果 |
| --- | --- |
| nvrtx=y の kernel（`config-amd64-nvrtx.mk`、`BUILD=build/ws037-y`） | rc 0・warning 0・`kernel include check: PASS`・`amd64 vmunix check: PASS` |
| host 試験 | P8a は RM の control と register の読みだけで、host で試せる builder は無い（追加なし） |
| 実機（方式 B） | **未実施** |

## T2 への手順（R8a）

image は `config-amd64-nvrtx.mk` の `NVRTX_RUN=R7`（stop 無し。P5 の後に P8a が走る）。方式 B では guest の OVMF が RTX の option ROM の GOP で RTX の出力を点けていれば、その head が lit に出る（点いていなければ lit 0 で、それも結果として記録）。

合格: R7 の印（`P5 ok …`・`P6 ok`・`P7 ok`）に加えて `P8 begin` → `P8 heads …` → `P8 ok: display, heads …` → `P8b is not in this kernel; stopping after P8a` → `P6 ok: channel and vaspace freed`（`P6: N display frees failed` が無い）→ `P7 ok: region torn down` → login prompt。点いた head の行（SOR・protocol・大きさ・clock）は値を記録（RTX の出力の monitor の解像度と照らす）。FAIL は `rm: …` の行を PNG に。`P7 fail` なら host を reboot。

## Resume point

P8a の実機の結果を待つ。次は P8b（core channel と window channel、GOP の head の引き継ぎ、自前の surface、flip）。P8b の画面の確かめは RTX の出力（方式 B ならユーザーの写真、方式 A の素の起動）。
