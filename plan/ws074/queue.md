<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-46 / object assignment receiver-aware dispatch

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q45 terminal2a2e16755/sourcedbcb42104/history db0a27b663941bf7bed6d71bb5a5a3b5658c897ff89d2cc5d866d280f60d5e98確認。
Timebox: 最大90分、2026-10-18 22:16:13→23:46:13 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-46-i01 | [p258](phase258/phase.md) object assignment routing partial | vm_setの全object経路を既存vm_set_receiverへ統合しordinary/array receiverのprototype Proxy/TypedArrayを実Setへ接続。sloppy/strict Boolean refusal、null/undefined/primitive既存経路、receiver identity/trap引数・例外/GCを保持。到達不能になるaccess_set_length helper宣言/定義だけ除去しarray assignmentは既存DefineLength規則へ委譲。旧585＋genuine新controls/native GC/errno/full changed C/host warning0/q02exact access1target warning0/focused/Acid/元WebIDL/全1508 q44のwhole/callback/completion比較・gain/loss audit | in-progress | q35 receiver-aware Set/q37 typed Set/Define/q44 Object.assign/q45 chain verified |

Generation bounds: vm/access.cのvm_set/access_define_length、access_set_length dead helper除去、vm/object.c vm_array_set_lengthと必要key-vector tracer/numeric comparator、vm.h length API契約comment（改訂event参照）。必要private tests（host-proxy-read.c/proxy-read.js）、WS074 records。vm_set_receiver/access_set_receiver/primitive Set/native_operations定義/comment/API2/global Proxy/Reflect/factory bounds/他source/SDK/toolchain/sysroot/WPT/runner/manifest/期待値/共有Boards/cache変更無し。新しい共通経路に未知の前提不足があればin-scope scope reconciliationを記録、強引に受け入れを縮めない。
Design: existing vm_set_receiverがrealm/base/key/value/receiverをtracerで保持し各実SetとDefineを実行。外側vm_setはobject branchを一回呼び、falseはstrictでTypeError/sloppy成功へ変換。Proxy direct既存error messageを保持、ordinary refusalは共通readonly messageとしlegacy個別messageの完全同値は約束しない。拒否後エラー生成前のowner/inputs保持も確認。primitive/nullの既存文言/branchは保存。array lengthの二回conversion・partial shrink/readonly等は既存DefineLengthを実対照で検証。全objectのshared path変更なので全1508を実測。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI declarations first/意味paragraph/全fn multiline/public-static順。clang-format19 ColumnLimit0後必要形復元、full changedfunctions manual＋checker許可singleforwardcleanup以外0。host serialized、q02 literal access compile -oだけ変更、versions実取得、raw全member read-back。
Verification bounds: old585native585/Chrome583既知huge2、q36filter資格を維持、新controls genuine対照。各protocol30秒/focused300秒、全1508eligible digest82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7/settle5000/process20固定、q44全whole/callback/completion/旧PASS差をaudit、gain個別prerequisite/negative確認。回帰は修復かuncleared、既知bug等の新nonblocking決定は勝手にしない。images/crash q28証拠再利用でnewpair全capture未測定。確認済1482/3805・held0・未attempt275保持、分母/未実行を除外しない。
Explicit residual: primitive Set exotic prototype、vm_instanceof outer/BoundFunction、for-in/spread/JSON、other invariant realm化/nested/call/construct/global constructor/revocable、array IsArray consumers/immutable proto/typed backing/p100/full target/whole p258/WS未達。no agents/push/削除/make check/QEMU。共有投影pending。
Dependency graph: q35/q37/q44/q45 verified outputs → i01。external nodesは今回membership外。
Outlook（非承認）: primitive Set/for-in/other invariant realm化→nested/call/global。

## Exact technical scope revision / original scope preserved

Event ws074-p258-q46-arraylength-design-revision-20261018: 最初の統合版を実測しnative613/615、残るsloppy/strict fixed-index shrink2条件を失敗。既存vm_array_set_lengthがdense値を先に全消去し、ascending deletionのdeleted=falseを無視してrequested lengthを確定していた。access_define_lengthもその結果をacceptedとしていた。criteriaは縮小しない。
自走承認内の技術的reconciliationとして、同一deadline/単一i01にvm/object.c vm_array_set_length＋数値key comparator/key-vector tracer必要helper、vm/access.c access_define_lengthを追加、vm.hの既存length API commentだけEPERM partial-shrink契約を追記。新module/API signature/architecture変更無し。対象object.c管理コメントを除去。新current-source凍結前に改訂WIPを保存。
Design: 既存own key snapshotをtraceし数値indexだけfilter/sort、new length metadataを先に設定→descending actual Delete→falseならfailed index+1へ復元してEPERM、higher deletions保持。fresh keyvector/arrayをnative callback GC中保持。length realm helperはvalue二回conversionの後actual old length descriptor再取得（conversion中のreadonly等変更を反映）、EPERMをBoolean refusalへ戻しrequested writable:falseを失敗時も適用。稀なENOMEM/native errnoは成功化しない。genuine additional sparse/uint32-index/descending/partial writability/conversion reentry controls、直接native length API actual GC/deletion order/EPERM対照を追加。target3 files access/object/header dependent compile exact q02、全1508比較等当初gate維持。
Primary: https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-arraysetlength 。現行algorithmは二回conversion後にoldLengthDescを取得し、存在するown numeric indexを降順Delete、最初の拒否でindex+1とrequested writableを復元する。foreign Phase変更無し、whole p258/WS acceptance未変更。
