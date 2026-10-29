<!-- awesome-plan project=zedbsd record=ws242-p006 -->

# ws242-p006: GSP の起動の段ごとに諦めて firmware の画面に戻す道

Status: in-progress（2026-10-29 P2: 実装済み・host 試験と build まで。実機・QEMU の試験は未実施（RTX 2070 の故意の失敗は p008）。cleared にしない）
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: ws242-p004
設計: [design.md](../design.md) 第 2 版の V1（表の p005b）。review の B1・M3。ユーザーの回答 Q242-3「諦めの道を先に (推し)」

## 範囲

1. 実装の最初に今の `gsp.c`・`boot.c` の失敗の段（FWSEC-FRTS・booter_load・RISC-V の起動・INIT_DONE）の state・region・memory・console の行方を
   表にし、各段の安全な戻し方（bus master を切る順、GSP falcon の reset と止まったことの確認、FWSEC-SB・booter_unload が走れるか）を決める。
2. 戻せる段では falcon を止め、region を壊し、`nvrtx_console_restore()` で firmware の画面に戻し、memory を返す。止まったことを確かめられない時は
   bus master を切って memory を保つ（今の `FAILED_HOLD`）。
3. host で試せる部分（段 × 結果 → 戻し方の表）を純粋な関数に出して試験。RTX 2070 での段ごとの故意の失敗は p008。

## 受け入れ

- host 試験が通る。amd64 vmunix が warning 0。実機の確かめは p008。

## 今の失敗の段（2026-10-29 P2 が `gsp.c`・`boot.c` から読んだ事実）

| 段 | 失敗 | 走りうる program | 変更の前 | 画面（firmware の console） |
| --- | --- | --- | --- | --- |
| layout（§2.1 を含む） | 何でも | 無し | `FAILED`、memory を返す。GPU は未変更 | 残る |
| devinit の待ち | 何でも | 無し | `FAILED`、返す | 残る |
| frts | region が既にある | 無し | `FAILED`（power cycle の印） | 残る |
| frts | FWSEC が halt しない | GSP の falcon の FWSEC | `FAILED_HOLD`（何もしない） | 残る見込み（FWSEC は display に触らない） |
| frts | FWSEC の結果が悪い | 無し | `FAILED`、region は残りうる、返す | 残る |
| booter | booter_load が halt しない | SEC2 の booter、起動されたかもしれない GSP | `FAILED_HOLD` | 未確認 |
| booter | 他の失敗・RISC-V が走らない | （halt 済み） | `REGION_SET` → 返す、region は残る | 残る見込み |
| gsp-init | INIT_DONE が来ない | GSP の RISC-V（途中まで init の GSP-RM） | `FAILED_HOLD`（memory も console も保つ） | **未確認**（GSP-RM が BAR1・scanout を変えたか） |
| unload | GSP が止まらない | GSP | `FAILED_HOLD` | 未確認 |
| region の teardown | FWSEC-SB か booter_unload が halt しない | GSP の falcon か SEC2 | `FAILED_HOLD` | 未確認 |

## 設計と実装（2026-10-29 P2）

決めた戻し方（`FAILED_HOLD` の 5 点を `gsp_hold()` 1 つに）:

1. 先に PCI の bus master を切る（`drv_pci_device_set_bus_master(…, false)`）。GPU から system memory への DMA が止まるので、まだ走る falcon・GSP が memory を壊さない。register の MMIO（CPU から GPU）は効く。
2. その点で走りうる falcon を reset（`nvrtx_falcon_reset`。FWSEC と gsp-init と unload は GSP の falcon、booter_load と teardown は GSP と SEC2 の両方）。どの falcon かは純粋な関数 `nvrtx_gsp_hold_falcons()`（新しい `hold.c`、host 試験で表を見る）。reset の成否は印に出すだけ（bus master が切れているので、成否によらず memory は安全）。
3. `nvrtx_console_restore()` で VRAM の window を firmware の mode に戻す（GSP-RM が BAR1 を変えていなければ何もしない）。
4. memory は返さない（`FAILED_HOLD` のまま、detach は EBUSY）。外から dump できる今の調べの道を残すため。量は数 MB〜数十 MB で、電源を切るまで。
5. region は壊さない（booter_unload は unload に答えた GSP の region しか壊さない、`gsp_teardown()` の comment）。次の GSP の起動は power cycle が要る（今と同じ、「frts stop: region … already set」で諦めて firmware の画面のまま）。

選ばなかった案: bus master を切らずに falcon の reset だけ（reset が効かない時に DMA が続く）／reset の後に memory を返す（dump の道が無くなる、reset が本当に効いたかを確かめる手段が無い）／region を booter_unload で壊す（init を終えなかった GSP に booter_unload が何をするか公開の source に無い）。

印: 「<段> fail…; bus master off (n), falcon reset gsp n sec2 n; the firmware's picture is back, memory kept for a dump, power cycle before the next GSP boot」。

RTX 2070（検証済みの family）の失敗の時の動きも変わる（今までは falcon も bus master もそのままで dump を待った）。dump は memory が残るので取れる。GSP の内部の状態は reset で消える。

未確認（実機でしか分からない）: GSP-RM の途中の init が GOP の scanout を止めるか（止めていれば window を戻しても画面は戻らない）、Ampere の GSP の falcon が HS の lock の中で engine の reset を受けるか、bus master を切った後の次の POST で GOP が画面を出すか。p008 の RTX 2070 で故意に gsp-init を失敗させて確かめる案（試験だけの仕組みは p008 で決める）。

| file | 変更 |
| --- | --- |
| `hold.c`（新）・`nvrtx-gsp.h`・`platform/amd64/vmunix.mk` | `enum nvrtx_gsp_hold`、`nvrtx_gsp_hold_falcons()` |
| `gsp.c` | `gsp_hold()`、5 点（frts・booter・gsp-init・unload・teardown）をそれに |
| `nvrtx.c`・`nvrtx-gpu.h` | `FAILED_HOLD` の comment |

## 確かめ（2026-10-29 P2、host と build だけ）

| コマンド | 結果 |
| --- | --- |
| `sh plan/tools/nvrtx/nvrtx-gsp-host-test.sh` | `PASS nvrtx gsp: 393 checks, SKIPPED 0`（新: hold の表 6 項） |
| `make -j16 ZEDBSD_CONFIG=plan/tools/nvrtx/config-amd64-nvrtx.mk BUILD=build/ws242-p004 build/ws242-p004/vmunix` | rc 0、`warning:` 0、amd64 vmunix check PASS（hold.c を compile） |

未実施: 実機（RTX 2070 での段ごとの止めと故意の失敗は p008）、QEMU。
