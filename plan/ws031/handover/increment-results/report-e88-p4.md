# WS031 第E-88報：P4 `intel_irq_install` 完成 — 実機で gen11 IRQ を install → master enable → uninstall まで成立。**HAL は変更なし**

## 1. 結論

**P4 は完成し、実機で割込みの install / master enable / uninstall が通りました。** 事前にお伝えしたとおり **HAL インタフェースの変更は不要**で、実際に一切変更していません。

```
P4 intel_detect_pch: type=8 id=0x7a80 source=2 bridges=1 bridge_dev=0x2918 subsys=1af4:1100
P4 intel_irq_install: rc=0 msi_irq=16 attached=1 gt_irqs=0x909 dmask=0x9090909 smask=0x9090000
                      reset_writes=62 post_writes=28
P4 de masks: pipe_masked=0x10100f80 pipe_enables=0x90700f89 port_masked=0x3f07
             misc_masked=0x80000 de_irq_mask[A]=0xefeff07f master_enabled=1
P4 irq observed: count=0 handled=0 none=0 gt=0 display=0 last_master_ctl=0x0
teardown: intel_irq_uninstall (sources reset, handler detached; irq_count=0 handled=0 none=0)
attach end: reached=P3 outcome=BLOCKED where=intel_display_driver_probe_nogem err=0
```

- **新frontier = `intel_display_driver_probe_nogem`（P5）**
- GPU-free 試験 **197 → 207 checks / 0 failures**、build 0 error/warning
- 新規 `irq.{c,h}` / `pch.{c,h}`、`power_domains` に `parity_display_power_is_enabled` を追加

## 2. HAL を触らずに済んだ理由（実装して確認済み）

HAL には既に**分離済み MSI API** があり、正本の `request_irq`/`free_irq` がそのまま対応しました。P2 が `msi_kept=1` で vector を保持していたので、P4 は接続だけです。

| HAL | 正本 | 備考 |
|---|---|---|
| `hal_irq_alloc_msi` | （P2で実施済） | ハンドラ未装着中の到着は mask+ack され NULL dispatch されない |
| `hal_irq_attach_msi` | `request_irq` | |
| `hal_irq_detach_msi_sync` | `free_irq` | 全CPUで実行完了保証 |
| `hal_irq_free_msi` | — | source 停止後。probe.c 側で P2 資源として解放 |

IF=0 制約も非該当でした（あれは boot device-probe 文脈で、parity attach は管理 kthread から走ります）。

**適応差分は事前にお伝えした2点のみ**で、いずれも HAL 変更ではありません：EOI はハンドラ責務（全経路で `hal_irq_send_eoi`）、`IRQF_SHARED` は MSI で無意味。Linux の `IRQ_HANDLED/IRQ_NONE` は MSI では spurious 検出用なので、戻り値ではなく device state に記録しています。

## 3. 実装で特に注意した点

- **ADL-P のエンジンは RCS0|BCS0|VECS0|VCS0|VCS2**。CCS / GSC0 / HECI-GSC が無いので、正本の Xe-HP 系レジスタは書いていません。
- **execlists（ご承認の判断②）** なので `irqs = RENDER_USER|CS_MASTER_ERROR|CONTEXT_SWITCH|WAIT_SEMAPHORE = 0x909`。GuC 側の分岐は「判断②の反対側」として明示的に残してあります（試験でも両方を固定）。
- **pipe fault mask は `RKL_`（0x00100F80）**であって `GEN11_`（0x00700F80）ではありません（ver>=13 の分岐）。ここは取り違えると静かに誤ったマスクを書く箇所です。
- `gen3_irq_reset` は **IIR を2回**クリアします（正本の意図的な paranoid）。試験で回数まで固定しました。
- `intel_irq_uninstall` は**先に全 source を reset** します（ハンドラを外しても device は送信を止めません）。
- **PCH 検出**：正本は QEMU q35 を明示的に扱っており（`INTEL_PCH_QEMU_DEVICE_ID_TYPE 0x2900 /* qemu q35 has 2918 */` ＋ Red Hat/QEMU subsystem）、実機でもそのとおり **ISA bridge 0x2918 / subsys 1af4:1100 → virt → PCH_ADP** で検出されました。推測ではなく正本の分岐を通っています。`PCH_ADP(8) >= PCH_ICP(6)` が SDE / icp_irq_postinstall の gate です。
- `parity_display_power_is_enabled` は正本どおり **well を逆順・always_on は skip・キャッシュ済 hw_enabled で判定**（HW 再読しない）。

## 4. 正直に報告すべき点

**(a) 割込みは観測窓（20ms）で 0 件でした。**
engine 未起動・display 未活性・vblank 無しで発生源が存在しないため、これは想定どおりで失敗ではありません。ただし**「実際に割込みを受信してハンドラが動いた」ことの実証はまだできていません**。それが可能になるのは発生源ができる P5/P6 以降です。現時点で実証できたのは「install/attach/master enable/uninstall/detach が実機で通り、teardown がクリーン」までです。

**(b) ハンドラは現状カウントのみで、サービスはしません。**
GT / display の bottom half は P5/P6 の範囲なので、`gen11_irq_handler` は master_ctl を読んで source を計数し、GU_MISC IIR を ack して master を再 enable するところまでです。コード内にもその旨を明記してあります。

**(c) 実機 ref 構成のログが途中で切れる件。**
attach 完走後の ktest 途中で 120s timeout に掛かるのは従来どおりです（ktest の数値は GPU-free 側で採取）。timeout を 240s に伸ばしても着地点が実行ごとにばらついたので、**ハングを疑って3回目を回し、attach が最後まで完走することを確認**しました（上記の `attach end` 行）。kill のタイミング差であって停止ではありません。

**(d) 試験側の誤りを1件、自分で見つけて直しました。**
IRQ-POWEROFF 試験で「電源OFF時は transcoder A も skip される」と期待しましたが、**TRANSCODER_A を持つ power well は存在せず** always-on well のみが覆うため、正本の「always_on は skip」規則により「有効」判定になるのが正解でした。**コードが正本どおりで、試験の期待値の方が誤り**だったので、期待値を是正しています。

## 5. 現在地と次

- 台帳 E-88 追記済み。
- 次は工程表どおり **P5 `intel_display_driver_probe_nogem`**。ご承認済みの判断①に従い、**hw_state readout + sanitize を優先**し、`setup_outputs` は検出・ログまで、BIOS fb 引き継ぎ無しで進めます。
- その先は P6（gem/gt、execlists 先行）→ P7前半 →**実機EU試験（描画再試験に当たるため、到達時に明示解除をいただいてから実行）**。

ご指示がなければ P5 に着手します。
