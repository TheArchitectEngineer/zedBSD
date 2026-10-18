<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-36 / canonical numeric keys and shared ToBigInt

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q35 terminal5c48f0599/history SHA2560f9073ef7f9d9e7b9f84893d8b074a3854740883816742b8c2af137235826a62・archive49 members read-back済み。
Timebox: 最大90分、20:30:56→22:00:56 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-36-i01 | [p258](phase258/phase.md) TypedArray prerequisite conversions partial | VM-owned pure vm_string_to_numberとvm_canonical_numeric_indexを既存number formatter/parserから共有しinternal integer/string/symbol keyのcanonical判定・-0/NaN/Infinity・shortest rounding/exponentsを提供。VM heap allocation/JS実行無し（C buffer allocation可）。VM-owned vm_to_bigintはToPrimitive(number hint)→BigInt identity/boolean0・1/string StringToBigInt/Number・null・undefined・Symbol TypeError/invalid string SyntaxError、全JS whitespace trimとrealm/input/fresh primitive実GC保持を実装。既存BigInt constructorのNumber専用integer/RangeError pathを保ちnonnumberを共有、asIntN/asUintNはconstructor経由からToBigIntへ修正しNumber拒否/順序を実測。precode native/Chromeの公開差保存、old235 source prefix保全、Chrome genuine typedarray0 Reflect.definePropertyのcanonical判定/genuine BigInt.asIntN4096と独立同一対照、native managed realm/input/output pressure・実回収、host warning0/target exact operation・builtin_bigint2 warning0/full changed C/DOM等focused gates/Acid、全1508 censusとq31全callback/completion差・旧PASS保持/新gain別audit | in-progress | q35 explicit TypedArray Set residual、TC39 canonical numeric index/ToBigInt、既存StringToNumber/number formatter/BigInt parser |

Generation bounds: engine vm/operation.c/vm.h（new conversion APIs/type/tracerと必要なown helper declarations、元parser/formatterの意味は不変）、js/builtin_bigint.cのbigint_call/bigint_as_nと対応own declarationsだけ。tests/proxy-read.js/host-proxy-read.c（canonicalReadIndex/toBigIntRead fixture bridge・pressure追従）、WS074記録。new module/public ABI2/Wayland/native_operations/TypedArray JS実装/actual Set/routing/runner/WPT/manifest/期待値/SDK/toolchain/sysroot/共有Board/cacheは変更しない。
Semantics bounds: canonicalはvalid property keysの無script abstract operationで、ToKeyはfixture/consumerが先に行う。BigInt oracleのgenuine4096-bit asIntNは全選定入力が範囲内と明示しfull unbounded oracleと偽らない。constructor Number pathとToBigInt Number拒否を区別し、Unicode blanksは新VM ToBigInt側でvm_is_spaceを用いてtrim、既存bigint parserを変更しない。TypedArray専用SetのENOTSUP/通常prototype routing/全Proxy/global constructor未達は保持。
Standards: Guardrail/C全文/browser component全文直接適用。new/changed functions全体のmanual/format19/checker/raw許可cleanup、helper/table/public順/ANSI/GC根拠を確認。host serialized終了確認・final source/pair/input固定。targetはq02 exact operation/builtin_bigint2 commandの-oのみ、full target/p100未実施。
Verification bounds: 変更した公開BigInt APIは正規precode/independent controls/全1508を実測。manifest3805/eligible1508 digest82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7/未attempt275/held0不変、settle5000/process20。new whole gainはprerequisite/意図的negativeの到達をauditしてから加算、不能なら保留。static/scripted/strictはq28原証拠再利用でcurrent pair全capture同値は未測定。追加試験は具体的risk/gateだけ、90分/未達はunclearedとresume。push/削除/make check/QEMU無し。
Dependency graph: verified q35 ordinary/Proxy Set → shared canonical index/ToBigInt + public BigInt corrections → native/genuine Chrome/GC/gates/all1508 → TypedArray Define/専用Set・通常prototype routing（後続未選定）。
Upcoming Work Outlook: TypedArray Define/Set/content-type metadataと数値key規則 → 通常prototype routing → delete/keys/prototype/extent/IsArray/call/construct/global constructor/revocable → original WebIDL2/WPT・未attempt275/API/layout/p100/full target。
Primary: https://tc39.es/ecma262/multipage/abstract-operations.html#sec-tobigint / https://tc39.es/ecma262/multipage/abstract-operations.html#sec-canonicalnumericindexstring 。
History: [q35](history/ws074-q20261018-35.md)、[Past Log](history/index.md)。
