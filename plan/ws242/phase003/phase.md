<!-- awesome-plan project=zedbsd record=ws242-p003 -->

# ws242-p003: nvrtx の host 試験の復元と追従、identify・profile・製品の表・起動の判断の項

Status: planned
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: なし
設計: [design.md](../design.md) 第 2 版の改訂 V6・V7、§4。review の M3・M4・M5

## 範囲

1. 41a28534b で消えた 3 本（`nvrtx-gsp-host-test.{c,sh}`・`nvrtx-identify-host-test.{c,sh}`・`nvrtx-vk-d3-host-test.{c,sh}`）を
   `git show 41a28534b^:plan/ws037/tests/…` から `plan/tools/nvrtx/` へ戻し、今の API に追従させる（WS の完了で消えない置き場。tools.md の登録は Q1）。
2. identify の試験: 全 chip（TU102〜AD107、GA100・GH100・GB の名前だけ）→ profile、profile の各欄の値（[p001](../phase001/phase.md) の表）、
   製品の表（`products.c`）の整列・重複なし・chip が profile のある chip。
3. 起動の判断を純粋な関数に出す（`identify.c`、family × firmware の画面 × `nvrtx.gsp` の無し・0・1 → 起動か残すかと印の種類）。
   この Phase では今の動き（verified の family だけ既定で起動）を変えない。試験で全部の組を見る。
4. gsp の試験: `nvrtx_wpr_layout` の heap の引数（Turing は 0・64 MiB、Ampere は 22・170 MiB）、`nvrtx_vram_size_mib`、copy の class、window の storage、
   vbios の v3、署名の section の名前を profile から。既定の firmware の path を `<rev>-tu-ga-ad` の cache に、実 file の項が飛んだら
   「SKIPPED n」を数えて出す。RTX 2070（TU106、8 GiB）の layout の値が今と同じ。
5. vk-d3 の試験: 今の vk の struct・API に追従。

範囲の外: §2.1 の検査（p005）、QMD 3.0（p004）、起動の既定の変更（p007）。

## 受け入れ

- 戻した 3 本と既存の 3 本（`nvrtx-sm75-host-test.sh` 287 checks、`nvrtx-sm75-desktop-test.sh --strict` 186 compiled、`nvrtx-vk-host-test.sh` 86 checks。2026-10-29 の基点 31b0f5dbe で数えた）が通る。
- gsp の試験を main の checkout の `build/sources/firmware/nvidia-gsp/dc85cced…-tu-ga-ad` を読み取り専用で指して流し「SKIPPED 0」。
- amd64 vmunix の build が warning 0。
