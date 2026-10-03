## p006 実機結果（VFIO passthrough テストループ、2026-09-14）

Latitude 5330（`awe@10.0.10.25`）の IGD（`0000:00:02.0`、`8086:46a8` rev 0c）を `host-igd.sh` で vfio-pci に切替え、i915 selftest 付き image を QEMU で起動して attach → selftest → `/dev/gpu0` 公開まで到達した。各 attempt の後に i915 と GDM を復旧した。

### 復旧 rehearsal（QEMU なし）

`plan/ws029/phase006/host-rehearsal.json`。`attach` で driver=vfio-pci、`/dev/vfio/0` 出現、GDM inactive。`restore` 後 driver=i915、GDM active、`driver`/`gdm`/`dev_vfio`/`drm_nodes` が開始前と一致（`restored=true`）。復旧手順が成立することを確認。

### attempt 履歴（同条件の修正と再実行）

| attempt | mode | 結果 | 原因・修正 |
| --- | --- | --- | --- |
| boot-001 | boot-only | fail | harness の import 依存 `venus_rfb.py` を転送していなかった → 転送一覧に追加 |
| boot-002 | boot-only | fail | UEFI loader が GOP framebuffer 無しで `Locate GOP` 停止 → QEMU 引数を `-vga none` から `-vga std`（表示 backend なし）に変更 |
| boot-003 | boot-only | fail(attach) | i915 が `map-registers` で EINVAL → BAR0 を `drv_pci_device_claim_bar` してから map するよう修正（PCI は claim した BAR しか map させない） |
| boot-005 | boot-only | 進捗 | BAR0 は正しく map（regs va=0xffffffffe0000000, bus=0x380010000000）、GGTT/MSI まで到達、engine bring-up で停止 → 段階 log 追加 |
| boot-006 | boot-only | 進捗 | 両 engine init と `engines started` まで到達、selftest で停止 → selftest に log 追加 |
| **boot-007** | boot-only | **pass** | `i915: selftest bcs0 store=ok irq=1 seqno=1/1`、`registered native GPU node`。実機の BCS0 が store を実行し user interrupt を上げた |

### 受入

- attach 全段階（PCI enable、BAR0 claim/map、forcewake、GT reset、GGTT、MSI、engine×2、selftest、publish）を通過。
- `boot-007` boot-pass: guest.log に `attach stopped` なし、`selftest bcs0 store=ok`、`registered`。
- host 復旧: attempt 後 driver=i915、GDM active（`host_restored=true`）。attach 中は driver=vfio-pci、`/dev/vfio/0`、GDM inactive。
- 証拠: `plan/ws029/temp/remote/q314-i915-boot-007/`（result.json、guest.log、qemu.log、qmp.jsonl、console）、`plan/ws029/phase006/host-rehearsal.json`。

実機で GPU が実際に命令を実行した最初の到達点。copy/fill/store/job と host 側 RAM 照合は p007。
