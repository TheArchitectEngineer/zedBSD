# p006: 規約と検証の確認範囲

対象はq309が追加・変更したproduction sourceとbuild、公開header、protocol metadata、限定試験。既存ファイルの無関係な部分を一括整形せず、終了済みWS030や旧Phase全体の再審査とはしない。p004自体のclearanceにも代用しない。

適用元は[全C規約](../../coding-style.md)、[設計方針](../../master-design-policy.md)、[自動化の対応範囲](../../standards/automation.md)。userlandのVulkan/Wayland/zwl/wltestは独立実装であり、公開protocolの事実と固定した公式宣言を参照する。ライセンス・revision・SHAは各API-PROVENANCEと実装資料に記録した。

| 規約の範囲 | 確認・整理した内容 |
| --- | --- |
| §1–3 構成と関数 | 公開関数をstatic定義より先へ配置。static前方宣言を一行、定義の型・名前・引数を分離。型/owner、public/static関数の目的を記載。callback tableの宣言順だけ下記の限定例外を採用。 |
| §4 宣言 | 関数先頭のANSI C宣言、初期化内の関数呼出しとfor宣言を避け、意味のある名前を使用。実target構文とdeclaration-after-statement診断を確認。 |
| §5–8 制御・段落 | if/loop/returnの目的、lock境界とblock後の空行、失敗する呼出しの個別評価、短絡順、複数条件の改行、braceと分割callを整理。変更時に所有権と評価順を保持。 |
| §9–11 確保と終了 | 複数allocationを一括で確認せず、一件ずつ検査。部分初期化の回収先を保持し、成功・失敗returnを分離。WSI teardownのerrnoはunlock/custom allocatorより前に保存。 |
| §12–14 試験境界・権利・最終確認 | productionに隠れたtest環境変数分岐を追加しない。wltest/zwlの有限CLIを明示。copyright/ライセンス、対象build、意味fixture、静的検査、diff確認を実施。 |

K/zwl、GPU/Venus、Wayland client/WSIとroot app/buildを担当別に読み、GPU担当がwltest rendererと共有WSIを独立レビューした。そこで見つかったextentの過大広告、画像別present semaphore、record段階のownership state更新、暗黙unmap、delay=1000ms、maxImageCountの境界を修正し、必要なfixtureまたは実表示で確認した。単に文字上のstyleだけを根拠に動作が正しいとは判断しない。

## 適用上の限定事項

- immutable ops/listener tableに必要なcallback前方宣言は、そのtableより先に置く。Cの名前解決のための最小の順序例外であり、§2の順序だけを満たすために不要なaccessor層を追加しない。rootが当該範囲の実装判断として採用した。prototypeの一行形式とpublic→static定義の順序は維持する。
- `clang-format 19.1.7 --dry-run --Werror`は**exit 1**。repository設定は引数定義を詰め、長い前方宣言を折り返し、80列でcall/commentを再配置するため、全C規約の指定と一致しない。raw診断は`build/q309-final/clang-format.log`へ保存し、自動適用しなかった。formatter合格とは報告せず、全文基準の確認と`git diff --check`を区別する。
- GCC `-fanalyzer`のzwl/main.cにあるlistener leak診断は、caller所有のfdをcalleeのreturnまでで評価したもの。listenerをserverに保持し、bind失敗後もmainのservice_cleanupでcloseする経路を確認した。実socketを使い、bind失敗→cleanup→fdのEBADFと元path保持を追加fixtureで検証した。診断自体は`build/q309-wltest-style/zwl-main-analyzer.log`に保持する。他の対象analyzerは診断なし。
- 同梱Clang 23.1.0はRunAnalysis未搭載のため、静的解析にはhost Clang 19とGCC 14.2を使用した。実target buildは同梱toolchainの`make -j16`。補助C89構文確認では既存libc headerの`restrict`を`__restrict`へ読み替えた。無関係なlibc header改変は加えない。

## 検証記録

詳細な対象とcommandは[GPU](gpu-sharing.md)、[K/zwl](kernel-compositor.md)、[Wayland/WSI](wayland-implementation.md)を参照。最終VMの判定・hash・失敗履歴は最終結果資料で一括管理する。

- K handle/fd/SCMと直接syscallの実fixture、GPU共有/transport/EDID/console、実Wayland stream/queue、WSIとshared swapchain：通常実行・ASan/UBSan。
- 旧direct WSI、Vulkan memory/context/sync、157 dispatch/export、両ABIの公開header：変更影響に対応する限定回帰。
- Noct再生成：vulkan_core.h、codec.c/h、dispatch-table.inc、api-commands.tsv、opcodes.hの6出力がbyte-identical。`build/q309-final/regenerated/verification.json`へhashを保存。banner/templateと維持ファイルの不一致はgenerator側で修正し、無関係な生成差分を除いた。
- 同一GPUの実process間共有、scanoutの画素・資源対応、終了・再起動・console：許可済みprivate QEMU/Venusの有限attempt。

HAL配下およびinclude/halにはq309差分がない。aggregate `make check`、git add/commit/pushは実行していない。

## q309最終受入

最終kernelでq309-direct-002とq309-wayland-004を受入済み。先行実測時点の未完了記述は履歴として保持し、現状は[結果と失敗履歴](results.md)および[最終証拠](final-evidence/verification.json)を参照する。p006 cleared、p004はplanning・未queue。
