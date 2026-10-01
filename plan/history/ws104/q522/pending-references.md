# WS104 完了後の旧参照の扱い

WS104 の範囲はほかの WS の Markdown を変えない。commands.md は短い互換の案内を残し、各節番号は移動先で保つ。次の link / prose の変更は各 WS の次回の計画照合時に行う。実装や Phase の再開を許可するものではない。

| 所有 record | 旧参照 | 確認済みの履歴先 / 結果 |
| --- | --- | --- |
| WS079 guide.md §4 / §8 | WS104 patches/p001-paths.patch | [archive](../design/p001-paths.patch)。p001 適用済み、host script の include path は現行 code で修正済み |
| WS089 guide.md §4 / §8 | WS104 patches/p001-paths.patch / patches | [archive](../design/p001-paths.patch)、[design](../design/)。p001 / p003 適用済み、host source path は現行 code で修正済み |
| WS105 design.md の Vulkan header の前提 | WS104 edits-compositor.md | [archive](../design/edits-compositor.md)。p004 適用済み、zwl-gpu.h は vulkan_external.h を include しない |
| WS105 WS / Phase prerequisite projections | WS104 を未完了とする時点の説明 | WS104 completed、p001 / p003 / p007 の出力は verified。Master / Queue Outlook は今回照合済み。WS105 の実行・Linux の受け入れは未開始 |

GitHub publication は repo 指示で deferred。cache / archive / event outbox は pending。公開時には remote の最新版・identity・取消 / reopen・Issue / artifact link を照合し、コメントと evidence を公開して read-back してから close を扱う。現時点の remote close / 同期完了は主張しない。
