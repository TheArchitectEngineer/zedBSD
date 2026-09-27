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

## 結果（2026-09-27）

cleared。

- 書いたもの:
  - `vm/vm.h`: `vm_value`（NaN-boxing の定数と inline の作り方・判定・取り出し。`vm_value_number` は -0 でない整数を int32 に）、
    `vm_symbol`・`vm_object`・`vm_accessor`・`vm_property`・`vm_function`・`vm_realm`、native の関数の型（`VM_THROWN` で例外）。
  - `vm/shape.c`: shape の遷移の木（heap の root の shape は登録した root。親は子を強く持つ）、key の検索（chain を辿る）、key の列挙。
  - `vm/object.c`: key（atom・symbol・int32 の index。`"17"` は 17、`"017"` と 2^31 以上は atom）、object と配列の作成、own の検索と
    prototype の chain、define（配列の length、既定の属性の index は elements、疎な index（今の長さ + 1024 より先）と既定でない属性は
    shape の整数の key、属性の変更と削除は root から shape を作り直す）、get（accessor は cell を返す）、set（通常の [[Set]] の data の
    部分: read-only と accessor は拒む、prototype の read-only が影を作らせない、拡張不可）、delete（configurable でないものは残る、
    element は穴）、`vm_array_set_length`、own keys の順、trace・finalize。
  - `vm/function.c`: native の関数（`name`・`length` は configurable だけ）、`vm_call`（native だけ。callable でなければ throw）、
    `vm_throw`。
  - `vm/realm.c`: Object.prototype、Function.prototype（呼べる）、Array.prototype（配列）、global object と `globalThis`。realm は
    cell でなく、tracer で自分の object を保つ。
- 試験:
  - `plan/ws074/tests/host-object.c` 98 検査（値の往復と判定（int32 の端、double の bit、-0、∞、NaN の正規化、pointer）、property の
    定義・読み・書き・属性・削除、同じ順の object が shape を共有、prototype の chain（読み・影・read-only の遮り）、accessor、
    拡張不可、200 個の property、配列（100 要素、穴、疎な index、属性のある index、length を縮める）、key の正規形、own keys の順、
    native の関数（name・length・呼び出し・throw・数を呼ぶと throw）、realm の chain、GC（1307 cell を回収、global から届く object は
    生き残る））: host plain・ASan（UBSan 込み）98/98、guest（QEMU、plain）98/98（回収の数も host と同じ）。
  - 前の試験: golden 12/12（ASan）、host-base 2038/2038、host-heap 31/31。
  - amd64 の build: warning 0。boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p022-20260927-boot-login.png`）。実機: 未実施。
  - `plan/ws074/tests/guest-build.sh` は p014 から engine が使う libvulkan を link するように直した。
- commit: `564a27fa`（code・試験）と、この記録の commit。

## 後回し（follow-up）

- 遷移の弱い参照（今は親が子を強く持つ）、大きな object の shape の hash の表（今は chain を辿る）、辞書 mode。
- inline cache（p023 の命令の形と一緒に）、accessor の呼び出し（interpreter の後）、Proxy・exotic object。
