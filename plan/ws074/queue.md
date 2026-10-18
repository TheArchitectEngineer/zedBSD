<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-34 / native Proxy DefineOwnProperty

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をこのexact finite partialへ適用。q33 terminala8d1dc5aa/history SHA256f6c7a6da71131ee2723119cdf856a88f17893f37197dabd7866185c26f42861f read-back、archive51 member検証済み。
Timebox: 最大90分、20:15:59→21:45:59 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-34-i01 | [p258](phase258/phase.md) DefineOwnProperty partial | 既存realm-aware vm_define_own_propertyのProxy分岐とprivate access_proxy_defineを追加しordinary/array/native hooksは元動作を保持。dynamic GetMethod/defineProperty、null fallback、handler this/元target/string-or-symbol key/Fromのpartial object3args、ToBoolean falseはtarget read無し/trueはpost-trap target descriptorとextensibility/元input descriptor互換性/explicit configurable:falseとfixed writable:false制約を検証。trapが渡されたrepresentationを変えても元input fieldsを保持、元target/handler/revoke/realm/key/input flagged payload/From newborn/methodを実GCから保持しdepthを全終了で復帰。通常Object.defineProperty成功は元Proxy/falseはTypeError、fixture bridgeのReflect相当boolはgenuine Chromeと独立同一対照。old135 source prefix不変、native managed realm/元descriptor sole payloadをtrapがrepresentationから削除後のpost-trap native hook GCを越えて保持・実回収、false時hook未実行、host warning0/target exact access1 source warning0・full changed C・focused DOM/CSSOM/VM/factory/producer/observer/error/Acid | in-progress | q33 GetOwn・snapshot-compatible判定、q32 From/To/Complete、q31 Get/Has/GC |

Generation bounds: engine vm/access.cだけ（vm_define_own_propertyと新Define helper/own forward decl）。tests/proxy-read.js/host-proxy-read.c（fixture-only defineReadProperty bridge・key/descriptor rooting/managed GC追従）、WS074記録。new module/public ABI2/Wayland/native_operations/VM header/JS module/runner/WPT/manifest/既存期待値/SDK/toolchain/sysroot/共有Board/cacheを変更しない。
Semantics bounds: private callable/nonProxy target制約保持、global Proxy非公開。Set/keys/prototype/extent/IsArray/call/construct/他API接続は後続未選定、whole p258 uncleared。heap-only hooksからscriptを呼ばない。new Defineのdepth/rootsだけを追加し既存ordinary sourceの意味は維持する。
Standards: Guardrail/C全文/browser component全文直接適用。new/changed function全体をformat19/manual/checker、許可forward cleanup raw件数を記録。host build単独完了確認、final source/pair/control input固定。targetはq02 exact access single-source commandの-oだけ変更、full target/p100未実施。
Verification bounds: focused semantic/GC/gatesだけ、全1508 q31/画像scripted strict q28証拠を再利用。current pair全件同値/whole PASS増分は主張しない。fixed3805・held0・未attempt275・確認済1482を維持。追加gateは具体的後退時だけ、90分/未達はunclearedとresume。push/削除/make check/QEMU無し。
Dependency graph: verified GetOwn/shared descriptor rules → Define partial → native/genuine Chrome/GC/focused gates → 後続Set/公開readiness（未選定）。
Upcoming Work Outlook: Set → keys/prototype/extent/IsArray/call/construct/global constructor/revocable → actual original WebIDL2/WPT評価・未attempt275/API/layout/p100/full target。
Primary: https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-defineownproperty-p-propertydesc 。
History: [q33](history/ws074-q20261018-33.md)、[Past Log](history/index.md)。
