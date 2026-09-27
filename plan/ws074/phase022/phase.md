<!-- awesome-plan project=zedbsd record=ws074p022 -->

# ws074-p022: VM の核 2: 値・object と shape・配列の elements・関数・realm の骨組み

Phase ID: `ws074-p022`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲（正常系のワンパス、design.md §11.1・§11.4・§11.6・§11.7）

- 値: 64 bit の NaN-boxing（JSC の方式: int32 は `0xFFFE…`、double は `2^49` を足す、pointer は上位 16 bit が 0、undefined・null・
  true・false・empty は小さい値）。`vm/vm.h` の inline の関数。
- object: shape（hidden class）の遷移の木と slot の配列、prototype、属性（writable・enumerable・configurable・accessor）。
  key は値（atom の string・symbol の cell・整数の index）。削除と属性の変更は shape を作り直す。
- elements: 既定の属性の密な index の値（穴は empty）。疎な index と既定でない属性の index は shape の整数の key。配列の `length`。
- 関数: native の関数の cell（`name`・`length`）と `vm_call`（native だけ。interpreter は p023）。例外の値は realm に置く。
- realm の骨組み: global object、Object.prototype・Function.prototype・Array.prototype。
- own keys の順（整数の昇順、string の挿入順、symbol の挿入順）。

## 受け入れ

1. amd64 の build（warning 0）、style-check（新しい file）0。
2. host の試験 `host-object`（値の往復と判定、property の定義・読み・書き・削除・属性、prototype の chain、配列の length、
   own keys の順、GC の後も生きる・死ぬ、native の関数の呼び出し）を plain・ASan で。golden と前の host の試験が下がらない。
3. guest で `host-object` が同じ結果。boot test。

## 後回し（follow-up）

- 遷移の弱い参照（今は親が子を強く持つ）、大きな object の shape の hash の表（今は chain を辿る）、辞書 mode。
- inline cache（p023 の命令の形と一緒に）、accessor の呼び出し（interpreter の後）、Proxy・exotic object。
