<!-- awesome-plan project=zedbsd record=ws237-p005 -->
# ws237-p005: suspend・resume の時の display（Gen9 は D0 のまま sleep）

Status: in-progress — 実装と host の確かめは済み（P2、2026-10-15）。実機（SV7 の sleep）は未実施
Disposition: normal
Parent: [WS237](../ws.md)
Queue: Q1 の指示（2026-10-15）。ユーザーの判断（2026-10-15、クリック）: 案 (B)「D0 のまま sleep」。
PCI の core の変更（`src/drivers/pci/pci-power.c`、WS237 の外）は Q1 が許可。

## 判断の経緯

- (A) D3hot の後に display を全部作り直す案（CD clock・DPLL0・power well・DBUF・eDP の panel の電源の順・eDP の training・EDP の transcoder・pipe・plane）は、約 3 LW で危険が高い。
- (B) は、PCI の core に「driver が自分の function を S0 の idle の間 D0 のまま保つ」選択を足す案。Gen9 の i915 は D3hot に入らずに sleep し、display は状態を保つ。
- U2（2026-10-14、Gen9 は suspend を EBUSY で断る）は、D0 のまま保てる時だけ外す。`gen9.display` の段に関わらない（bootfb の copy の時も同じ）。

## 実装

| file | 内容 |
| --- | --- |
| `src/drivers/pci/pci-power.c`・`include/drivers/pci/pci.h` | `drv_pci_device_keep_d0(device, keep)`。driver の suspend の中から呼ぶ（`drv_pci_device_set_wake` と同じ形）。<br>付いた function は、config の保存（書き戻しも）だけをして、D3hot と platform の電源断を飛ばす。resume では driver の resume だけ。 |
| `src/drivers/gpu/i915/i915.c`・`i915.h` | Gen9 の `i915_suspend`: `drv_pci_device_keep_d0` を頼む（失敗なら従来どおり EBUSY）。<br>worker を park し、外部の display を消し、panel の light を消して `suspended_d0` を立てる。<br>`i915_resume`: light を点け、外部の display を点け直し、worker を unpark。<br>Gen12 の道（DC9・D3hot）は変えない。 |
| `display/gen9-backlight.[ch]` | `drv_i915_gen9_backlight_suspend/resume`: eDP の panel が点いていて PCH の PWM が動いていれば、CTL1 の enable を外し、resume で元の値に戻す。backlight の段でなくても働く。 |
| `display/gen9-node.[ch]` | `drv_i915_gen9_node_suspend/resume`: 点いている外部の出力を消す（lease と buffer は残す。最後に armed、無ければ live の surface を覚える）。sleep の間は hotplug を見ない。resume で同じ surface で点け直す。点かなければ log に出し、その lease の present は ENXIO。 |

eDP の panel の電源（PP_CONTROL）は落とさない。落とすと resume で eDP の link の training が要り、(A) の危険が戻る。light だけを消す。

## 確かめ（host と build だけ）

- `make -C plan/ws052/tests pci-power`: 新しい `test_keep_d0`（A を D0 に保つ。A は D0 のまま、他は D3hot、resume は A の driver の resume だけ、suspend の外の要求は EINVAL）を含めて `pci-power: every check passed`。
- `make -C plan/ws052/tests i915-park`: `every check passed`。
- `run-gen9-flip/backlight/outputs-host-test.sh`: 48・21・29、0 failures。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws237-p004 build/ws237-p004/vmunix` rc 0、warning 0。
- style-check: 変えた所の違反 0。

## known-issues に書く案（Q1 へ）

> Gen9・Gen9.5（第 6〜10 世代 Core）の機械は、sleep の間も内蔵 GPU を D0（電源の入ったまま）に保つ。
> firmware が点けた画面を失わないためで、そのため sleep の間の電池の減りが第 11 世代以降より大きい。
> 画面の明かりと外部の monitor の出力は sleep の間消える。

## 残り

- SV7 での確かめ（ws.md の「SV7 でのまとめての確かめ」の sleep の行）。
- S0 の idle の間に GPU の割り込み（flip-done）が起きる場合の扱い。lease の flip が止まっていれば起きない見込み。
