<!-- awesome-plan project=zedbsd record=ws104 -->

# WS104: Keiland の OS の境界の整理

Status: completed
Primary Milestone: MG006
Related Milestones: MG007（desktop を用途別の構成へ移せる境界を提供）
Objectives: O2
Parent: [Master](../master.md)
Queue: [q522](../history/queue-q522.md) finished
Resume point: 受け入れ A1〜A6 を確認して完了。Linux の実装は [WS105](../ws105/ws.md) へ。

## 目的と成果

Keiland の OS に依存する処理を OS ごとの source module に閉じ、zedBSD の振る舞いを保った。
公開 header は `userland/desktop/keiland/`、libkeiland の network / audio と compositor の GPU / evdev / session は各 package の `zedbsd/` に置いた。
compositor の共通 code は Vulkan image と memory を受け取り、GPU・入力・OS の API は `zwl-gpu.h`・`zwl-input.h`・`zwl-os.h` に宣言した。
install path は `userland/desktop/paths.h` の 4 macro で指定する。公開版は 21、Settings は `keiland_audio_available()` で audiod の有無を調べる。

## 受け入れ（2026-10-01）

| 基準 | 結果と証拠 |
| --- | --- |
| A1 公開 header と sysroot | 24 file を本文同一で移動。移動時の sysroot の 241 file の名前・SHA256 が前後一致、再生成の stamp を確認。[q515](../history/ws104/q515/phase.md)。版 21 と新しい audio API は別の承認済み p002 の変更 |
| A2 Settings の audio 境界 | socket の直の参照 0、公開 API の socket 不在 / 通常 file / socket は 0/0/1。[q516](../history/ws104/q516/phase.md) |
| A3 OS 固有 module | 共通 source の OS include / ioctl 0。evdev 定数 header の 1 行だけが合意済み例外。最終 check C1/C2/C5 PASS |
| A4 GPU layout と境界 API | `zwl_buffer_layout` は `wayland/zedbsd/` 内のみ。GPU V1 と host / forge / fence、境界 check C3 PASS。[q518](../history/ws104/q518/phase.md)、[q522](../history/ws104/q522/phase.md) |
| A5 install path | p007 / C4 の対象の install literal の残り 0（system shell・公開 header の既存 macro・sessiond は対象外）。p007 の bin / dynamic library 全体の該当文字列は変更前後一致。host desktop は前後 PASS。[q521](../history/ws104/q521/phase.md) |
| A6 zedBSD の振る舞い | 最終 amd64 build exit 0・自前 warning 0、全文規約の変更範囲違反 0（理由つきの保持と tool 誤検出は standards-review.md）。境界 C1〜C5 PASS、故意の uapi include は C1 FAIL / exit 1、同一に復元。GPU V1 54 source、dedicated host 18 case × ordinary / sanitize、decode host 17 case × ordinary / sanitize、forge / fence guest（600 fence、generation 1、600 frame / 64 s）PASS。compositor C1/C2/C9 は 13/13 PASS、glass p059・Notes pen/PDF・Settings host / guest 8 本・host audio 14/14・音量 p004/p005 PASS。必須 PNG は目視し、boot の login PNG をユーザーに提示した。実機・Linux compositor・他 platform は未実施。証拠と各試験の summary は plan/history/ws104/q522/。 |

## 全文規約の確認

新しい source と移動した実装の全体、既存 source の変更箇所を [coding-style.md](../coding-style.md) 全文で確認した。
詳細の対象・tools・手動確認・例外・検証の限界は [q522 のレビュー](../history/ws104/q522/standards-review.md)。

## Phase と実装

| Phase | 成果 | 状態 / Queue | 実装 commit（WIP） |
| --- | --- | --- | --- |
| [ws104-p001](../history/ws104/q515/phase.md) | 公開 header と sysroot manifest | cleared / q515 | 12d7efee |
| [ws104-p002](../history/ws104/q516/phase.md) | audio available API、公開版 21 | cleared / q516 | 7e3ac1bc |
| [ws104-p003](../history/ws104/q517/phase.md) | libkeiland の OS module | cleared / q517 | 78da1387 |
| [ws104-p004](../history/ws104/q518/phase.md) | GPU buffer protocol と import の境界 | cleared / q518 | 5d413c08 |
| [ws104-p005](../history/ws104/q519/phase.md) | evdev device の境界 | cleared / q519 | 96d14088 |
| [ws104-p006](../history/ws104/q520/phase.md) | session と OS hook | cleared / q520 | 3bfe50b9 |
| [ws104-p007](../history/ws104/q521/phase.md) | install path の macro | cleared / q521 | cec34d3e |
| [ws104-p008](../history/ws104/q522/phase.md) | 全文規約・境界 checker・全体回帰・WS 完了 | cleared / q522 | cd48e74d110a1e504c2b87ac288d41cdc928367c |

各 Queue の exact approved snapshot（scope.md）・SHA256・結果は [q515](../history/queue-q515.md)〜[q522](../history/queue-q522.md) と各証拠 directory に保持した。
q518 の実装は 5d413c08、検証時の HEAD は人間の screenshot を含む 69f0f2c0。証拠のラベルを訂正済みで、試験結果は変わらない。

## 制限・移管・記録

- 試験は amd64 の QEMU / Venus と Linux host の絞った確認。実機 5330、32-bit pcat・pc98・arm64、Linux compositor は未実施。
- 物理 hotplug は未実施。入力の scan / rescan の動作を保ち、p005 で仮想 node・pen・touch・gesture を確認した。
- Linux build / module は WS105 へ。既定 path の macro の上書き、public emoji font / Open With 検索 directory / sessiond の OS 差は WS105 の既存 scope に従う。
- 再利用する [zedBSD の検証手順](../tools/keiland-linux/zedbsd-commands.md) と [OS 境界 checker](../tools/keiland-os-boundary/check.sh) を Master Tools に登録した。既存リンク用の commands.md は移動先への短い案内。
- Phase directory・patches・edits-compositor を WS104 から除去した。旧設計資料は [history/ws104/design](../history/ws104/design/) と Git に保存。ほかの WS の Markdown は変更しないという scope に従い、WS079/WS089/WS105 の旧 patch / edits 参照は履歴先をここに記録し、必要時に各 WS で参照を更新する。対象と履歴先は [pending references](../history/ws104/q522/pending-references.md) に保持した。
- commit message は全て WIP。push と GitHub の Issue/comment/Project 公開は未実施。local outbox に更新と event を保持し、公開時に remote と再照合する。remote の close / 同期済みは主張しない。

## 完了の記録

2026-10-01T05:44:23.819375+00:00、Codex Q1 / main。ユーザーの 2026-10-01「WS104 の完了まで自律的に」の指示で p002〜p008 を依存順、1 Phase / Queue で実行。
A1〜A6 と全文規約の関門を満たしたため WS104 を completed とし、後続は WS105 の Queue 選定とした。MG006 全体の受け入れは別に残る。
