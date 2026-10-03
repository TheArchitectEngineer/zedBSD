# WS031 E-105 報告: 同一ソースの回帰 4/4 PASS、bilinear 4/4 PASS（完全一致）、表示と libvulkan の事前調査

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

| 項目 | 結果 |
|---|---|
| 残り 4 モードの回帰（E-104 と同じソース） | **4/4 PASS**。提出 bytes は保存済み artifact と byte 一致 |
| bilinear の小試験 | **4/4 PASS**。事前に固定した「全画素の完全一致」で合格、channel の最大差は 0 |
| 表示側の事前調査 | 対象機、正本との対応表、採取計画を作成。現状は panel を点灯できる段階にない |
| libvulkan の経路調査 | 呼出し図を作成。現状は端から端まで繋がっていない |
| 出典の対応表 | parity 配下が名指しする上流 53 ファイルはすべて MIT |

「ここで機能追加を止める」という E-104 の記述は撤回し、回帰基準と引き継ぎ README、台帳を新しい順序に合わせて更新しました。現行の execlists、参照起動条件、10 ms タイマー、HAL 非変更は維持しています。git commit / push はしていません。

## 1. 残り 4 モードの回帰

E-104 と同じソース（commit f4dba354、作業ツリー差分なし）から各モードを clean build し、GPU なしで 383 checks を確認してから、同一 image を独立起動で 1 回ずつ実行しました。最初の異常で止まる作りで 4/4 完走です。

| モード | 実機結果 | 提出 bytes の照合 |
|---|---|---|
| C1 単独＋反復 | EU-TEST PASS、EU-REPEAT PASS（5/5） | batch、kernel、IDD が E-99 の artifact と一致 |
| 単色 PS 描画 | DRAW-TEST PASS（1024/1024） | batch、state が E-101 の artifact と一致 |
| R1 | R1 PASS（12/12） | batch hash 2 本が pin 値と一致 |
| テクスチャ初回 | TEX-TEST PASS（1024/1024、texture・guard 無変更） | batch、state、RT が E-103 の artifact と一致 |

全モードで teardown 正常、reset なし、cleanup=1 です。T3 は E-104 で同じソースにて実行済みなので繰り返していません。これで 5 モードすべてが同一ソース状態で実機確認済みです。

## 2. bilinear

### 2.1 変更範囲

- 変わるのは SAMPLER_STATE の 2 dword だけです。generator に linear 版の sampler を足し、anv と blorp が非 nearest のときに設定する address rounding も同じにしました。
- 再生成した include は linear sampler の 4 行が増えただけで、PS を含む他の全語は不変です（patch 適用時に機械検査）。batch は E-103 の textured batch と byte 同一です。

### 2.2 比較方法（実機実行の前に固定）

- uv=(pixel+0.5)/32、8×8 texture なので、texel 空間の座標は (2·pixel−3)/8 です。各軸の重みは 1/8 の倍数になり、各 sample は Σ(重み×texel)/64 です。
- 全 channel が 64 の倍数のテスト画像（画像 3）を使うと、この和は UNORM8 単位で厳密に整数になります。丸めの判断が存在しないので、**全画素の完全一致**を合格条件にし、許容誤差は設けませんでした。
- 範囲外の近傍は edge texel へ clamp します。
- 期待画像は kernel 内（整数演算）と host 側（独立実装）の両方で計算します。nearest の期待とは 1008 画素で異なるので、nearest と同じ結果になる入力ではありません。

### 2.3 実機結果

```
BL step=1 ctx=A filter=nearest  pixels 1024/1024 max_channel_diff=0   (対照)
BL step=2 ctx=A filter=linear   pixels 1024/1024 max_channel_diff=0   differs_from_nearest=1008
BL step=3 ctx=A filter=nearest  pixels 1024/1024 max_channel_diff=0   (filter の変更が残らない)
BL step=4 ctx=B filter=linear   pixels 1024/1024 max_channel_diff=0   (新規 context)
BL PASS: steps=4/4 | teardown 正常・reset なし / ktest 385/0 / cleanup=1
```

内側の補間と clamp 端の両方を含む全画素が一致しました。host 側の独立検証も 4/4 です。読み戻した RT の画像を添付します。

小さな不備が一つあります。最終 step の texture dump が常に texture B を出す作りで、bilinear では bind しているのは A でした。RT、state、batch の dump と、各 step の hash による host 側の同定は正しいです。次の増分で直します。

mipmap、anisotropic、sRGB、頂点 UV は範囲外のままです。

## 3. 表示側の事前調査（[survey-display-lcd.md](plan/ws031/handover/notes/survey-display-lcd.md)）

- **対象機は Dell Latitude 5330（laptop）で、内蔵 panel があります。** この機体は KVM ホストそのものです。native 試験で zedBSD を直接起動している間は KVM の試験環境が使えません。
- VFIO 下では ASLS が 0 で、OpRegion も VBT もありません。bare metal では firmware が用意しますが、parity は OpRegion VBT を保持していません。
- parity の VBT parser は block を数えるだけです。本物の VBT が得られると encoder が 0 個になります。
- AUX／DPCD、PPS、backlight、PLL の計算と enable、link training、transcoder・pipe・plane の書込み、watermark、vblank／flip 完了、HPD の実処理は未実装です。
- GGTT の窓は 1 MiB で、FHD の framebuffer（約 8 MiB）が入りません。
- GOP が pipe を active で残した場合、parity は停止も引継ぎもできず、probe は `intel_initial_commit` で止まります。
- 正本の呼出し順と parity の対応表、1 回で済ませる参照データの採取計画（13 項目、bare metal 必須のものを明示）を文書にしました。

## 4. libvulkan の経路調査（[survey-libvulkan-path.md](plan/ws031/handover/notes/survey-libvulkan-path.md)）

- libvulkan は Khronos の loader ではなく、Venus wire protocol の client としてプロジェクト固有に実装されています（entry point 169 個）。
- kernel 側の executor が stream の decode と GPU 固有処理を担当します。境界は `/dev/gpuN` の ioctl で、GPU core が version、size、アドレス範囲、handle の所有を検査します。
- 現状は繋がりません。executor が返す capability set を libvulkan が拒否します。executor の多くは stub です。parity 構成では device が公開されません。
- CPU で描画する fallback は見つかりませんでした。
- present、display、scanout の ops は現在 NULL です。通常 client の入口は GPU core の既存契約が既にあるので、表示側に別の入口を作らず、parity の device をその ops 表へ繋ぐのが自然だと考えます（提案であり決定ではありません）。

## 5. 出典・元表示

- [parity-notice-map.md](plan/ws031/parity-notice-map.md) を自動抽出で作りました。parity 107 ファイル中 36 が上流ファイルを名指しし、20 が port と明言しています。
- 名指しされた上流 53 ファイルは**すべて MIT** です（SPDX 行 42、permission notice 文 11、GPL は 0）。名指しは複製の証明ではないので、区分は人が確定します。
- 次の増分で、header が「port of …」と明言しているファイルから、上流の copyright 行と MIT 表示を内容非変更の差分として復元します。新しい名義は記入しません。コメントだけの変更で kernel binary が byte 同一になることも確認します。

## 6. ご相談

**bare metal Linux での参照採取**は、私だけでは進められません。VBT、OpRegion、PPS、backlight、GOP の引継ぎ状態は、KVM ホスト（Latitude 5330）で i915 が panel を駆動している Linux 起動でしか取れません。vfio-pci のバインドを外す操作になるので、実施の可否と日程をご指示ください。採取コマンドは文書の §4 にまとめてあります。それまでは、既存の VFIO Linux guest で取れる項目の採取と、VBT parser などの fake 試験で先行できる実装を進めます。

## 7. 提出物（`plan/ws031/handover/`）

`increment-results/`：`e97-e105-changes.patch`（base 2bf790a4）、`e105-run-parity-hw-{eu,draw,r1,tex,bl}.log`、`e105-bl-last-*`、`e105-bl-last-rt.png`。`notes/`：調査 2 本。ツール：`reftex.c`、`eu_artifact.py`、`sweep_hw.sh`、`notice_map.py`。台帳は E-105、回帰基準は [regression-baseline.md](plan/ws031/regression-baseline.md)。
