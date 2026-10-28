<!-- awesome-plan project=zedbsd record=ws113 -->

# WS113: Keilandの外部ディスプレイ・複数画面設定

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001〜p008・p011〜p015 cleared（p008・p012〜p014 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）。2026-10-08 ユーザーの UAT「HDMIに出力されました。extendもmirrorも動いています。」。p010 は 2026-10-04 ユーザー「KMSでのLinux・FreeBSD用互換実装は、あとまわしにしてよいです（別Phaseにする）」）
<!-- retained-after-completion --> Phase phase004・phase005・phase006・phase007とtests/ の 4 項目 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG001（GPU・API の契約と回帰の証拠）
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

zedBSD の i915 で外部 display の接続・切断を Vulkan の Display の拡張から compositor へ知らせ、Settings から libkeiland 経由で compositor の拡張を操作して複数の display を設定できるようにする。構成は全 display を別々に使う拡張表示と、全 display へ同じ画面を出すミラーリングの二択。拡張表示の窓は常に 1 つの display だけに見え、pointer が隣の画面に入った時点で窓全体の所属を切り替える。受け入れ D1〜D5（HPD → Vulkan の hotplug の通知 → compositor の再列挙、拡張・mirror の切り替え、Settings の配置の drag、窓の所属、規約と回帰）。設計の索引は [design.md](design.md)、全文の方針は [standards/ws113-display.md](../standards/ws113-display.md)。

## ユーザーの決定

- 2026-10-02: pointer の境界で切り替え、「単一 display」は拡張の時だけ、実機の完了はまず zedBSD の i915。ミラーでは同じ論理の desktop の窓が全物理画面へ複製される。
- 2026-10-02「複数 display は標準アプリの次」。
- 2026-10-02（D-ATOMIC）「WS113は推奨でよいです。」→ (a) logical owner の同時の更新（最大 1 frame 程度の両画面での見え・不表示は許容）。
- 2026-10-04 夜（UAT-3 の後）「Settingsのディスプレイのタブを実装してほしいです。ただし、ドライバの動作で、すでに伝えてあるとおり、GOPの出力先を起動時に有効なスキャンアウト先にして、ほかは出力しない、というルールの導入と、Vulkan display拡張でHDMIの挿抜の通知をコンポジタが受けるのと、コンポジタが動的にスキャンアウト先を変更するのを、実装する必要があります。これはzedBSDを優先にして、KMSでのLinux・FreeBSD用互換実装は、あとまわしにしてよいです（別Phaseにする）」
- 2026-10-04 夜「SettingsのDisplayには、内蔵LCDの場合、明るさ調節がほしい」→ p013・p005・p006。
- 2026-10-08（UAT）「HDMIに出力されました。extendもmirrorも動いています。」、拡張の時に個々の display を off（p014）、2 つ目以降の display の窓のリサイズ・bar と dock・状態（p015）。
- 2026-10-08: 全文規約の分はベータ3、Linux・FreeBSD の単一 display の維持は 10/13 以降。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」→ p008・p011a・p012〜p014。

## 結果

- i915: GOP の出力先だけを引き継ぐ scanout の規則（外部 display の優先を廃止）、接続の全出力の列挙と HPD の topology の sequence（p002）、1 出力の付け替え（p011a）、2 つ目の pipe の同時の scanout（p011）、`GPU_DISPLAY_POWER`・`GPU_DISPLAY_REFRESH`（p012）、内蔵の panel の backlight の device と provider（p013）。
- libvulkan: 標準の Display の API・拡張で hotplug と複数の出力を公開（p003）。
- compositor: 1 出力の切り替え（`kwl_output_switch`、p004a）、複数の同時の出力と displays.conf（p004b）、`kl_system_displays_v1`（`kl_system_manager_v1` v4）と明るさ・Fn の key（p005）、窓の出力の所属と画面の間の移動（p007）、2 つ目以降の display の窓・bar・dock（p015）、個々の display の off（p014）。
- Settings の Display の頁（拡張・mirror の二択、配置の drag、明るさの slider、p006）。
- 証拠: QEMU の Venus（T1-355c・T1-366・T1-367・T1-368・T1-369・T1-370・T1-380 ほか）と 5330 の実機のユーザーの UAT（2026-10-08 HDMI・USB-C（TC2）の拡張・mirror と窓の移動、2026-10-13 の一括の cleared）。

## 制限・移管

- p010（Linux・FreeBSD の KMS での互換）は着手せず（ユーザー「あとまわしにしてよいです」）。
- 規約の全文と最終の照合（p009）は WS の外の一括の見直しへ。
- p015 の head の上の press の 5330 の UAT は未実施。5330 で eDP を off にすると session が落ちる件は BUG-266。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws113-p001 | 契約・能力と実機の fixture | cleared（2026-10-08 Q1 の判定） |
| ws113-p002 | i915 の scanout の規則・inventory・HPD | cleared（2026-10-08 Q1 の判定、実機の分は p008） |
| ws113-p003 | Vulkan の Display の列挙・通知 | cleared（2026-10-07、T1-355c） |
| ws113-p004a | 1 出力の切り替え | cleared（T1-357b） |
| ws113-p004（p004b） | 複数の同時の出力 | cleared（2026-10-08 Q1 の判定、T1-367・5330 の UAT） |
| ws113-p005 | compositor の拡張と libkeiland | cleared（2026-10-08 Q1 の判定、T1-366） |
| ws113-p006 | Settings の Display の頁 | cleared（2026-10-08、T1-368） |
| ws113-p007 | 窓の出力の所属と画面の間の移動 | cleared（2026-10-08、T1-369、5330 でユーザーの確認） |
| ws113-p008 | 実 i915 の全経路の受け入れ | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに…」） |
| ws113-p009 | 最終の全文規約と WS の受け入れ | planned のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws113-p010 | Linux・FreeBSD の KMS での互換 | 着手せず（ユーザー「あとまわしにしてよいです」、Phase の directory なし） |
| ws113-p011 | i915 の 2 つ目の出力 | cleared（2026-10-08 Q1 の判定、5330 の UAT） |
| ws113-p011a | i915 の 1 出力の付け替え | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに…」） |
| ws113-p012 | native の power と refresh の境界 | cleared（2026-10-13 同上） |
| ws113-p013 | 内蔵の panel の明るさの下層 | cleared（2026-10-13 同上） |
| ws113-p014 | 拡張の時に個々の display を off | cleared（2026-10-13 同上。BUG-266） |
| ws113-p015 | 2 つ目以降の display の窓・bar・dock | cleared（2026-10-08 Q1、T1-380） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）は削除する（Q1 の pipeline）。p001 の契約の文書（`phase001/contracts-beta2.md`・`contracts.md`・`source-audit.md` ほか、design.md が索引する）の扱いは Q1 の判断（移して残す案あり）。git の commit `fb1f6afee` の `plan/ws113/` にある。Queue の履歴は [plan/history](../history/index.md)。
