<!-- awesome-plan project=zedbsd record=ws242-p007 -->

# ws242-p007: GTX 16・RTX 30・40 を既定で起動

Status: cleared（2026-10-29 P5、q1678-i02。RTX 2070（verified の family）の既定の起動が変わらないことを [p008](../phase008/phase.md) で確かめた。未検証の family の既定の起動は実機なし（host 試験だけ）。旧: P2 の実装済み・host 試験と build まで）
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: ws242-p004、ws242-p006
設計: [design.md](../design.md) §3、第 2 版の V5（表の p005c）。ユーザーの決定（2026-10-15）「GTX 16・RTX 30・40 はデフォルトで有効にします。」、回答 Q242-3

## 範囲

- 起動の判断（p003 の関数）で verified を既定の起動の条件から外す（`gop_on_gpu` の条件は残す）。未検証の family の起動では「not yet seen on hardware」の印。
  `verified` は「実機で見た」の記録の意味に残す（欄は足さない、m6）。`nvrtx.gsp=0` で全部を firmware に残すことを文書（driver の説明）に書く。
- 既定を変える行は最後の commit（p004・p006 より先に main に入らないこと）。

## 受け入れ

- identify の host 試験の起動の判断の組が新しい表で通る。amd64 vmunix が warning 0。

## 実装（2026-10-29 P2、最後の commit）

- `identify.c` の `nvrtx_boot_choose()`: `nvrtx.gsp` の指定が無い時、verified でない family も、firmware の画面が GPU にあれば起動する（`NVRTX_BOOT_START_UNSEEN`）。画面の無い GPU は今のとおり残す（`gop_on_gpu` の条件は変えない）。`NVRTX_BOOT_LEAVE_UNSEEN` は使われなくなったので消した。
- `nvrtx.c`: 未検証の family の起動の印「… not yet seen on hardware, booting it (nvrtx.gsp=0 leaves it to the firmware)」。driver の説明（file の先頭の comment）に、既定で全 family を起動すること、途中の失敗では戻せる所で firmware の画面に戻すこと、起動しない機械は `nvrtx.gsp=0` で全部を firmware に残せることを書いた（利用者向けの文書は tree に無い。docs/ に置くかは Q1 へ）。
- `nvrtx-private.h`: `verified` は「実機で見た」の記録の意味。
- 欄は足さない（review m6、`boot_default` は全部 1 になるので作らない）。

## 確かめ（2026-10-29 P2、host と build だけ）

| コマンド | 結果 |
| --- | --- |
| `sh plan/tools/nvrtx/nvrtx-identify-host-test.sh` | `PASS nvrtx identify: 27 identities, 4 families, 288 products, 24 boot choices`（TU11x・GA10x・AD10x の既定が起動、画面の無い時は残す） |
| tip の全部（gsp 393 SKIPPED 0・qmd 733・vk 86・vk-d3 209・sm75 287・desktop 186） | 全部 PASS |
| `make -j16 ZEDBSD_CONFIG=plan/tools/nvrtx/config-amd64-nvrtx.mk BUILD=build/ws242-p004 build/ws242-p004/vmunix` | rc 0、`warning:` 0 |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws242-ci build/ws242-ci/vmunix` | rc 0、`warning:` 0、amd64 vmunix check PASS |

未実施: 実機（RTX 30/40・GTX 16 は無し、RTX 2070 は p008）、QEMU。危険（design.md 第 2 版 V2）: laptop（121 の製品）で ACPI の data なしに GSP-RM の init が通るかは未確認。通らなければ p006 の道で firmware の画面に戻る見込み（未確認）。
