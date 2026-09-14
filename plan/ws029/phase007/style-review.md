# WS029 静的レビューと規約確認（p007）

## 静的解析

`plan/ws029/tests/run-i915-analyzer.sh`（kernel config、`-DCONFIG_DRIVER_PCI_I915=1`、`-DCONFIG_DRIVER_PCI_I915_SELFTEST=1`、kernel include）で全新規 source を解析した。

| tool | 対象 | 結果 |
| --- | --- | --- |
| `gcc-14 -fanalyzer -fsyntax-only` | i915/{i915,uncore,ggtt,ppgtt,gem,engine,lrc,request,irq,selftest}.c | 警告 0（`plan/ws029/phase007/analyzer-gcc.log`） |
| `clang --analyze` | 同上 | 初回 1 件 → 修正後 0（`plan/ws029/phase007/analyzer-clang.log`） |

clang の初回診断: `i915.c:842 core.NullDereference`（`i915_resource_destroy` の `object->session_next`）。解析器は「session の object list を末尾まで走査して見つからず、かつ `object == NULL`」という経路を仮定した。core の契約（`drv_gpu_ops`: 「Close and resource_destroy cannot fail and must finish using the state」）で `object` は resource_create が返した非 NULL の backend object であり、この経路は起きない。不変条件を明示する `if (object == NULL) return;` を先頭に足して診断を解消した（挙動は不変）。以後 gcc/clang とも 0 件。

## 規約 checklist（`plan/coding-style.md` §14）

全新規 `.c`/`.h` に §14 を適用した。確認項目と結果:

- **file section / 関数順**: 各 source は copyright header → file 説明 → include → macro → 型 → forward declaration → 公開関数 → static 関数の順。OK。
- **forward declaration**: 全 static 関数に 1 行 forward declaration。weak extern は不要（optional collaborator なし）。OK。
- **関数定義の改行**: 戻り型・名前・各引数を別行。OK。
- **local 宣言**: 全 automatic 変数を関数先頭で宣言、初期化子に関数呼出しなし、`for` 初期化子で宣言なし、scope 限定の裸ブロックなし。OK。
- **semantic paragraph**: 各段落に purpose comment と前後の空行。critical section（spin/mutex）は acquire 後・release 前に空行、unwind ブロックは compact。OK。
- **debugger 向け制御流れ**: 条件内で関数を呼ばない、1 条件 1 `if`、`&&`/`||` の式で bool を作らない、3 節以上の条件は行分け（`i915_start` の BAR type チェック、ggtt/ppgtt の範囲チェック等）。OK。
- **return**: 各 return に purpose comment、成功 return は `Succeeded:` で最後、`return error;` 裸終端なし。OK。
- **comment**: 「Handles the ... condition.」等の空虚な形は不使用。protocol flag/counter（`busy`、`quarantined`、`resetting`、`pending_requests`、CSB counter）は観測者にとっての意味を書いた。OK。
- **転記 `.inc`**: 値の転記のみ。生成器 `gen-inc.py` が MIT 表示・出典 path・SHA-256・変換規則を header に入れ、未転記 symbol 参照と audit SHA 不一致で失敗する。OK。
- **test-only 環境スイッチ**: production path に環境変数スイッチなし（`I915_FIXTURE_VERBOSE` は fixture 内のみ）。OK。
- **build/test/`git diff --check`**: 3 構成 build PASS、host fixture 全 PASS、`git diff --check` PASS。

## 修正履歴（レビューで直したもの）

1. clang null-deref（上記）。
2. 実機 bring-up で判明した点（p006/p007 の attempt）: BAR0 の `claim` 追加、UEFI GOP 要件のための `-vga std`、`max_resources` を 0 → `UINT32_MAX`（core が `resource_count >= max_resources` で ENOSPC を返すため）。いずれも fixture に回帰を追加済み（BAR claim は fixture の claim/release stub、max_resources は backend test の resource 作成で担保）。
