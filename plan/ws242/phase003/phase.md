<!-- awesome-plan project=zedbsd record=ws242-p003 -->

# ws242-p003: nvrtx の host 試験の復元と追従、identify・profile・製品の表・起動の判断の項

Status: in-progress（2026-10-29 P2: 実装済み・host 試験と build まで。実機・QEMU の試験は未実施（Queue の範囲で試験なし）。cleared にしない）
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

## 実装（2026-10-29 P2）

| file | 変更 |
| --- | --- |
| `src/drivers/gpu/nvrtx/nvrtx-private.h`・`identify.c` | `enum nvrtx_boot_choice` と `nvrtx_boot_choose(profile, gop_on_gpu, gsp_wanted, gsp_forced)`（起動の判断の純粋な関数）。動きは今のまま（verified の family だけ既定で起動、`nvrtx.gsp=1` で全 family、`nvrtx.gsp=0` で全部を firmware に） |
| `src/drivers/gpu/nvrtx/nvrtx.c` | attach の判断を `nvrtx_boot_choose()` の switch に。印は今と同じ文字列。`nvrtx.gsp=1` で未検証の family を起動する時に「not yet seen on hardware, booting it」の印を足した |
| `plan/tools/nvrtx/nvrtx-identify-host-test.{c,sh}` | 全 chip（27 の identity）→ profile、4 family の全欄、製品の表（288、整列・重複なし・search・chip に profile、laptop 121）、起動の判断 23 組、device info |
| `plan/tools/nvrtx/nvrtx-gsp-host-test.{c,sh}` | 今の API に追従（heap の引数、copy の class、window の storage、framebuffer）。Ampere の layout（12 GiB、heap 170 MiB の最小）、`nvrtx_vram_size_mib`、FWSEC v3 の合成 ROM（署名・fuse・DMA の block）、copy の c7b5、window の storage 0、work の VA 3.5 GiB の表。実 file: 4 family の image の `.fwimage` と署名の section、booter（GA/AD は meta と fuse の bank）、bootloader、falcon の bl。既定の cache を `<rev>-tu-ga-ad`、worktree では main の checkout の cache を読み取り専用で。飛ばした項を `SKIPPED n` で数える |
| `plan/tools/nvrtx/nvrtx-vk-d3-host-test.{c,sh}` | 今の node の口（submit・wait・passed・retire・last・idle・alloc_system・memory_cpu）の fake、framebuffer の views、mip の段の拒否の項を今の規則（8 段は 100x10 に多すぎ）に |

- 製品の laptop の数: review の 115 は名前の付いた行だけの数え。表の欄（名前の無い行も含む）は 121。
- 試験の道具の登録（`plan/tools.md`）は Q1 に頼む（design.md V7）。

## 確かめ（2026-10-29 P2、host と build だけ）

| コマンド | 結果 |
| --- | --- |
| `sh plan/tools/nvrtx/nvrtx-identify-host-test.sh` | `PASS nvrtx identify: 27 identities, 4 families, 288 products, 23 boot choices` |
| `sh plan/tools/nvrtx/nvrtx-gsp-host-test.sh`（main の checkout の `dc85cced…-tu-ga-ad` を読む） | `PASS nvrtx gsp: 379 checks, SKIPPED 0` |
| `sh plan/tools/nvrtx/nvrtx-vk-d3-host-test.sh` | `PASS nvrtx vk d3: 122 checks, 0 failures` |
| 既存: `nvrtx-sm75-host-test.sh`・`nvrtx-sm75-desktop-test.sh --strict`・`nvrtx-vk-host-test.sh` | 287 checks 0 failures・186 compiled 0 refused 0 disagreed・86 checks 0 failures（変更の前の基点で。p003 は compiler に触らない） |
| `make -j16 ZEDBSD_CONFIG=plan/tools/nvrtx/config-amd64-nvrtx.mk BUILD=build/ws242-p003 build/ws242-p003/vmunix` | rc 0、`warning:` 0、kernel include check PASS、amd64 vmunix check PASS |

未実施: 実機（RTX 2070 は p008）、QEMU。

## plan/tools.md の登録の案（Q1 が merge の時に。2026-10-29 Q1 の依頼）

| 道具 | 何を確かめるか | 流し方 |
| --- | --- | --- |
| [nvrtx/nvrtx-identify-host-test.sh](tools/nvrtx/nvrtx-identify-host-test.sh) | nvrtx の chip の identity の解読、TU/GA/AD の family の record の全欄、製品の表（整列・重複・chip・laptop の数）、attach の起動の判断（family × firmware の画面 × `nvrtx.gsp`）、device info の reset bit | `sh plan/tools/nvrtx/nvrtx-identify-host-test.sh`、`PASS nvrtx identify: … boot choices` |
| [nvrtx/nvrtx-gsp-host-test.sh](tools/nvrtx/nvrtx-gsp-host-test.sh) | GSP の起動の hardware に触らない部分（wire・layout と scrub 済みの末尾・radix3・queue・VBIOS と FWSEC v2/v3・record・MMU・work・display の push・heap・object・GR・cursor・hold の表）と、実 firmware（`dc85cced…-tu-ga-ad` の cache、worktree では main の checkout の物を読む）の 4 family の image・booter・bootloader | `sh plan/tools/nvrtx/nvrtx-gsp-host-test.sh [FIRMWARE-DIR]`、`PASS nvrtx gsp: N checks, SKIPPED 0`（SKIPPED が 0 でなければ実 file の項が飛んだ） |
| [nvrtx/nvrtx-vk-d3-host-test.sh](tools/nvrtx/nvrtx-vk-d3-host-test.sh) | nvrtx の Vulkan の executor の object（memory・view・pipeline・submit）を libvulkan の wire と codec で通す。submit を SM75・SM86・SM89 で流し、各世代の 3D class を bind する | `sh plan/tools/nvrtx/nvrtx-vk-d3-host-test.sh`（glslc が要る）、`PASS nvrtx vk d3: N checks, 0 failures` |
